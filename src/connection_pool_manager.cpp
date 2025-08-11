#include "connection_pool_manager.hpp"
#include "pooled_session.hpp"
#include "timeout_manager.hpp"
#include "logger.hpp"
#include <algorithm>
#include <iostream>

ConnectionPoolManager::ConnectionPoolManager(net::io_context& ioc,
                                           size_t minConnections,
                                           size_t maxConnections,
                                           std::chrono::seconds idleTimeout,
                                           std::shared_ptr<RequestHandler> handler,
                                           std::shared_ptr<WebSocketManager> wsManager,
                                           std::shared_ptr<TimeoutManager> timeoutManager)
    : ioc_(ioc)
    , minConnections_(minConnections)
    , maxConnections_(maxConnections)
    , idleTimeout_(idleTimeout)
    , handler_(handler)
    , wsManager_(wsManager)
    , timeoutManager_(timeoutManager)
    , cleanupTimer_(std::make_unique<net::steady_timer>(ioc_))
    , shutdownRequested_(false)
    , connectionReuseCount_(0)
    , totalConnectionsCreated_(0)
{
    if (minConnections_ > maxConnections_) {
        throw std::invalid_argument("minConnections cannot be greater than maxConnections");
    }
    
    if (idleTimeout_.count() <= 0) {
        throw std::invalid_argument("idleTimeout must be positive");
    }

    Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager", 
        "Initialized with min=" + std::to_string(minConnections_) + 
        ", max=" + std::to_string(maxConnections_) + 
        ", idleTimeout=" + std::to_string(idleTimeout_.count()) + "s");
}

ConnectionPoolManager::~ConnectionPoolManager() {
    shutdown();
}

std::shared_ptr<PooledSession> ConnectionPoolManager::acquireConnection(tcp::socket&& socket) {
    std::unique_lock<std::mutex> lock(poolMutex_);
    
    if (shutdownRequested_) {
        throw std::runtime_error("ConnectionPoolManager is shutting down");
    }

    // Try to reuse an idle connection first
    if (!idleConnections_.empty()) {
        auto session = idleConnections_.front();
        idleConnections_.pop();
        
        // Reset the session for reuse
        session->reset();
        
        // Move the session to active set
        activeConnections_.insert(session);
        
        ++connectionReuseCount_;
        
        Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager", 
            "Reused idle connection. Active: " + std::to_string(activeConnections_.size()) + 
            ", Idle: " + std::to_string(idleConnections_.size()));
        
        return session;
    }

    // If we can create a new connection (not at max capacity)
    if (getTotalConnections() < maxConnections_) {
        auto session = createNewSession(std::move(socket));
        activeConnections_.insert(session);
        
        Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager", 
            "Created new connection. Active: " + std::to_string(activeConnections_.size()) + 
            ", Idle: " + std::to_string(idleConnections_.size()));
        
        return session;
    }

    // Pool is at capacity, wait for a connection to become available
    Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager", 
        "Pool at capacity, waiting for available connection");
    
    connectionAvailable_.wait(lock, [this] {
        return !idleConnections_.empty() || shutdownRequested_;
    });

    if (shutdownRequested_) {
        throw std::runtime_error("ConnectionPoolManager is shutting down");
    }

    // At this point, there should be an idle connection available
    if (!idleConnections_.empty()) {
        auto session = idleConnections_.front();
        idleConnections_.pop();
        
        session->reset();
        activeConnections_.insert(session);
        
        ++connectionReuseCount_;
        
        Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager", 
            "Acquired connection after waiting. Active: " + std::to_string(activeConnections_.size()) + 
            ", Idle: " + std::to_string(idleConnections_.size()));
        
        return session;
    }

    // This should not happen, but create a new connection as fallback
    Logger::getInstance().log(LogLevel::WARN, "ConnectionPoolManager", 
        "Unexpected state: creating new connection despite being at capacity");
    
    auto session = createNewSession(std::move(socket));
    activeConnections_.insert(session);
    return session;
}

void ConnectionPoolManager::releaseConnection(std::shared_ptr<PooledSession> session) {
    if (!session) {
        Logger::getInstance().log(LogLevel::WARN, "ConnectionPoolManager", 
            "Attempted to release null session");
        return;
    }

    std::lock_guard<std::mutex> lock(poolMutex_);
    
    if (shutdownRequested_) {
        // During shutdown, just remove from active set
        removeFromActiveSet(session);
        return;
    }

    // Remove from active connections
    auto activeIt = activeConnections_.find(session);
    if (activeIt == activeConnections_.end()) {
        Logger::getInstance().log(LogLevel::WARN, "ConnectionPoolManager", 
            "Attempted to release session not in active set");
        return;
    }

    activeConnections_.erase(activeIt);

    // Check if session is still usable
    if (session->isIdle()) {
        // Mark session as idle and update last activity
        session->setIdle(true);
        session->updateLastActivity();
        
        // Add to idle queue
        addToIdleQueue(session);
        
        Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager", 
            "Released connection to idle pool. Active: " + std::to_string(activeConnections_.size()) + 
            ", Idle: " + std::to_string(idleConnections_.size()));
        
        // Notify waiting threads that a connection is available
        connectionAvailable_.notify_one();
    } else {
        Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager", 
            "Session not idle, not returning to pool. Active: " + std::to_string(activeConnections_.size()) + 
            ", Idle: " + std::to_string(idleConnections_.size()));
    }
}

void ConnectionPoolManager::startCleanupTimer() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (!shutdownRequested_) {
        scheduleCleanup();
        Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager", "Started cleanup timer");
    }
}

void ConnectionPoolManager::stopCleanupTimer() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (cleanupTimer_) {
        cleanupTimer_->cancel();
        Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager", "Stopped cleanup timer");
    }
}

size_t ConnectionPoolManager::cleanupIdleConnections() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    size_t cleanedUp = 0;
    auto now = std::chrono::steady_clock::now();
    
    // Create a new queue with only non-expired connections
    std::queue<std::shared_ptr<PooledSession>> newIdleQueue;
    
    while (!idleConnections_.empty()) {
        auto session = idleConnections_.front();
        idleConnections_.pop();
        
        if (shouldCleanupSession(session)) {
            // Connection has been idle too long, clean it up
            ++cleanedUp;
            Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager", 
                "Cleaning up idle connection");
        } else {
            // Connection is still within idle timeout, keep it
            newIdleQueue.push(session);
        }
    }
    
    // Replace the idle queue with the filtered one
    idleConnections_ = std::move(newIdleQueue);
    
    if (cleanedUp > 0) {
        Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager", 
            "Cleaned up " + std::to_string(cleanedUp) + " idle connections. " +
            "Active: " + std::to_string(activeConnections_.size()) + 
            ", Idle: " + std::to_string(idleConnections_.size()));
    }
    
    return cleanedUp;
}

void ConnectionPoolManager::shutdown() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    if (shutdownRequested_) {
        return;
    }
    
    shutdownRequested_ = true;
    
    Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager", "Shutting down connection pool");
    
    // Cancel cleanup timer
    if (cleanupTimer_) {
        cleanupTimer_->cancel();
    }
    
    // Clear idle connections
    while (!idleConnections_.empty()) {
        idleConnections_.pop();
    }
    
    // Clear active connections
    activeConnections_.clear();
    
    // Notify any waiting threads
    connectionAvailable_.notify_all();
    
    Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager", "Connection pool shutdown complete");
}

// Metrics and monitoring methods

size_t ConnectionPoolManager::getActiveConnections() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return activeConnections_.size();
}

size_t ConnectionPoolManager::getIdleConnections() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return idleConnections_.size();
}

size_t ConnectionPoolManager::getTotalConnections() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return activeConnections_.size() + idleConnections_.size();
}

size_t ConnectionPoolManager::getMaxConnections() const {
    return maxConnections_;
}

size_t ConnectionPoolManager::getMinConnections() const {
    return minConnections_;
}

std::chrono::seconds ConnectionPoolManager::getIdleTimeout() const {
    return idleTimeout_;
}

bool ConnectionPoolManager::isAtMaxCapacity() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return getTotalConnections() >= maxConnections_;
}

size_t ConnectionPoolManager::getConnectionReuseCount() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return connectionReuseCount_;
}

size_t ConnectionPoolManager::getTotalConnectionsCreated() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return totalConnectionsCreated_;
}

void ConnectionPoolManager::resetStatistics() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    connectionReuseCount_ = 0;
    totalConnectionsCreated_ = 0;
    Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager", "Statistics reset");
}

// Private methods

std::shared_ptr<PooledSession> ConnectionPoolManager::createNewSession(tcp::socket&& socket) {
    auto session = std::make_shared<PooledSession>(
        std::move(socket), 
        handler_, 
        wsManager_, 
        timeoutManager_
    );
    
    ++totalConnectionsCreated_;
    
    Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager", 
        "Created new PooledSession. Total created: " + std::to_string(totalConnectionsCreated_));
    
    return session;
}

void ConnectionPoolManager::scheduleCleanup() {
    if (shutdownRequested_ || !cleanupTimer_) {
        return;
    }
    
    // Schedule cleanup to run every half of the idle timeout period
    auto cleanupInterval = std::chrono::duration_cast<std::chrono::milliseconds>(idleTimeout_) / 2;
    
    cleanupTimer_->expires_after(cleanupInterval);
    cleanupTimer_->async_wait([this](const boost::system::error_code& ec) {
        handleCleanupTimer(ec);
    });
}

void ConnectionPoolManager::handleCleanupTimer(const boost::system::error_code& ec) {
    if (ec == net::error::operation_aborted) {
        // Timer was cancelled, which is expected during shutdown
        return;
    }
    
    if (ec) {
        Logger::getInstance().log(LogLevel::ERROR, "ConnectionPoolManager", 
            "Cleanup timer error: " + ec.message());
        return;
    }
    
    // Perform cleanup
    cleanupIdleConnections();
    
    // Schedule next cleanup
    scheduleCleanup();
}

void ConnectionPoolManager::removeFromActiveSet(std::shared_ptr<PooledSession> session) {
    auto it = activeConnections_.find(session);
    if (it != activeConnections_.end()) {
        activeConnections_.erase(it);
    }
}

void ConnectionPoolManager::addToIdleQueue(std::shared_ptr<PooledSession> session) {
    idleConnections_.push(session);
}

bool ConnectionPoolManager::shouldCleanupSession(std::shared_ptr<PooledSession> session) const {
    if (!session) {
        return true;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto sessionLastActivity = session->getLastActivity();
    auto timeSinceLastActivity = std::chrono::duration_cast<std::chrono::seconds>(now - sessionLastActivity);
    
    return timeSinceLastActivity >= idleTimeout_;
}