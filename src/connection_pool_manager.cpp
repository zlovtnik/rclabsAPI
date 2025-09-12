#include "connection_pool_manager.hpp"
#include "logger.hpp"
#include "performance_monitor.hpp"
#include "pooled_session.hpp"
#include "timeout_manager.hpp"
#include <algorithm>
#include <iostream>

ConnectionPoolManager::ConnectionPoolManager(
    net::io_context &ioc, size_t minConnections, size_t maxConnections,
    std::chrono::seconds idleTimeout, std::shared_ptr<RequestHandler> handler,
    std::shared_ptr<WebSocketManager> wsManager,
    std::shared_ptr<TimeoutManager> timeoutManager, MonitorConfig monitor,
    QueueConfig queue)
    : ioc_(ioc), minConnections_(minConnections),
      maxConnections_(maxConnections), idleTimeout_(idleTimeout),
      handler_(handler), wsManager_(wsManager), timeoutManager_(timeoutManager),
      performanceMonitor_(monitor.perf), maxQueueSize_(queue.maxSize),
      maxQueueWaitTime_(queue.maxWait), cleanupTimer_(std::nullopt),
      shutdownRequested_(false), connectionReuseCount_(0),
      totalConnectionsCreated_(0), rejectedRequestCount_(0) {
  if (minConnections_ > maxConnections_) {
    throw std::invalid_argument(
        "minConnections cannot be greater than maxConnections");
  }

  if (idleTimeout_.count() <= 0) {
    throw std::invalid_argument("idleTimeout must be positive");
  }

  Logger::getInstance().log(
      LogLevel::INFO, "ConnectionPoolManager",
      "Initialized with min=" + std::to_string(minConnections_) +
          ", max=" + std::to_string(maxConnections_) +
          ", idleTimeout=" + std::to_string(idleTimeout_.count()) + "s");
}

ConnectionPoolManager::~ConnectionPoolManager() { shutdown(); }

std::shared_ptr<PooledSession>
ConnectionPoolManager::acquireConnection(tcp::socket &&socket) {
  // Use underlying mutex for condition variable operations
  std::unique_lock<etl_plus::ContainerMutex> lock(poolMutex_);

  if (shutdownRequested_) {
    // Capture shutdown state before releasing lock
    lock.unlock();
    // Send service unavailable response after releasing lock to avoid blocking
    try {
      sendErrorResponse(socket, "Service unavailable - server shutting down");
    } catch (...) {
      // Ignore errors when sending error response during shutdown
    }
    throw std::runtime_error("ConnectionPoolManager is shutting down");
  }

  // Clean up expired queued requests first
  cleanupExpiredQueuedRequests();

  // Try to reuse an idle connection first
  if (!idleConnections_.empty()) {
    auto session = idleConnections_.front();
    idleConnections_.pop();

    // Reset the session for reuse
    session->reset();

    // Move the session to active set
    activeConnections_.insert(session);

    ++connectionReuseCount_;

    // Record connection reuse for performance monitoring
    if (performanceMonitor_) {
      performanceMonitor_->recordConnectionReuse();
    }

    Logger::getInstance().log(
        LogLevel::DEBUG, "ConnectionPoolManager",
        "Reused idle connection. Active: " +
            std::to_string(activeConnections_.size()) +
            ", Idle: " + std::to_string(idleConnections_.size()));

    return session;
  }

  // If we can create a new connection (not at max capacity)
  if ((activeConnections_.size() + idleConnections_.size()) < maxConnections_) {
    auto session = createNewSession(std::move(socket));
    if (session) {
      activeConnections_.insert(session);

      // Record new connection for performance monitoring
      if (performanceMonitor_) {
        performanceMonitor_->recordNewConnection();
      }

      Logger::getInstance().log(
          LogLevel::DEBUG, "ConnectionPoolManager",
          "Created new connection. Active: " +
              std::to_string(activeConnections_.size()) +
              ", Idle: " + std::to_string(idleConnections_.size()));

      return session;
    } else {
      // Failed to create session, send error response
      sendErrorResponse(socket, "Failed to create session");
      ++rejectedRequestCount_;
      throw std::runtime_error("Failed to create new session");
    }
  }

  // Pool is at capacity, check if we can queue the request
  if (requestQueue_.size() >= maxQueueSize_) {
    Logger::getInstance().log(
        LogLevel::WARN, "ConnectionPoolManager",
        "Request queue full, rejecting request. Queue size: " +
            std::to_string(requestQueue_.size()));

    sendErrorResponse(socket, "Server overloaded - request queue full");
    ++rejectedRequestCount_;
    throw std::runtime_error("Request queue is full");
  }

  // Queue the request
  requestQueue_.emplace(std::move(socket));

  Logger::getInstance().log(
      LogLevel::DEBUG, "ConnectionPoolManager",
      "Queued request. Queue size: " + std::to_string(requestQueue_.size()) +
          ", Active: " + std::to_string(activeConnections_.size()) +
          ", Idle: " + std::to_string(idleConnections_.size()));

  // Wait for a connection to become available or timeout
  auto waitResult =
      connectionAvailable_.wait_for(lock, maxQueueWaitTime_, [this] {
        return !idleConnections_.empty() || shutdownRequested_;
      });

  if (shutdownRequested_) {
    throw std::runtime_error("ConnectionPoolManager is shutting down");
  }

  if (!waitResult) {
    // Timeout occurred, remove from queue and send timeout response
    Logger::getInstance().log(LogLevel::WARN, "ConnectionPoolManager",
                              "Request timed out in queue after " +
                                  std::to_string(maxQueueWaitTime_.count()) +
                                  " seconds");

    // Find and remove the timed-out request from queue
    std::queue<QueuedRequest, std::deque<QueuedRequest>> tempQueue;
    bool found = false;
    while (!requestQueue_.empty() && !found) {
      auto req = std::move(requestQueue_.front());
      requestQueue_.pop();

      auto waitTime = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - req.queueTime);

      if (waitTime >= maxQueueWaitTime_) {
        // This is the timed-out request
        sendErrorResponse(req.socket, "Request timeout - server overloaded");
        found = true;
      } else {
        tempQueue.push(std::move(req));
      }
    }

    // Restore remaining requests to queue
    while (!tempQueue.empty()) {
      requestQueue_.push(std::move(tempQueue.front()));
      tempQueue.pop();
    }

    ++rejectedRequestCount_;
    throw std::runtime_error("Request timed out in queue");
  }

  // At this point, there should be an idle connection available
  if (!idleConnections_.empty()) {
    auto session = idleConnections_.front();
    idleConnections_.pop();

    session->reset();
    activeConnections_.insert(session);

    ++connectionReuseCount_;

    // Record connection reuse for performance monitoring
    if (performanceMonitor_) {
      performanceMonitor_->recordConnectionReuse();
    }

    Logger::getInstance().log(
        LogLevel::DEBUG, "ConnectionPoolManager",
        "Acquired connection after waiting. Active: " +
            std::to_string(activeConnections_.size()) +
            ", Idle: " + std::to_string(idleConnections_.size()));

    return session;
  }

  // This should not happen, but handle gracefully
  Logger::getInstance().log(
      LogLevel::ERROR, "ConnectionPoolManager",
      "Unexpected state: no idle connections available after wait");

  sendErrorResponse(socket, "Internal server error - no connections available");
  ++rejectedRequestCount_;
  throw std::runtime_error("No connections available after wait");
}

void ConnectionPoolManager::releaseConnection(
    std::shared_ptr<PooledSession> session) {
  if (!session) {
    Logger::getInstance().log(LogLevel::WARN, "ConnectionPoolManager",
                              "Attempted to release null session");
    return;
  }

  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");

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

    Logger::getInstance().log(
        LogLevel::DEBUG, "ConnectionPoolManager",
        "Released connection to idle pool. Active: " +
            std::to_string(activeConnections_.size()) +
            ", Idle: " + std::to_string(idleConnections_.size()));

    // Process any queued requests first
    processQueuedRequests();

    // Notify waiting threads that a connection is available
    connectionAvailable_.notify_one();
  } else {
    Logger::getInstance().log(
        LogLevel::DEBUG, "ConnectionPoolManager",
        "Session not idle, not returning to pool. Active: " +
            std::to_string(activeConnections_.size()) +
            ", Idle: " + std::to_string(idleConnections_.size()));

    // Even if this session can't be reused, process queued requests
    processQueuedRequests();
  }
}

void ConnectionPoolManager::startCleanupTimer() {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  if (!shutdownRequested_) {
    scheduleCleanup();
    Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager",
                              "Started cleanup timer");
  }
}

void ConnectionPoolManager::stopCleanupTimer() {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  if (cleanupTimer_) {
    cleanupTimer_->cancel();
    Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager",
                              "Stopped cleanup timer");
  }
}

size_t ConnectionPoolManager::cleanupIdleConnections() {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");

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
      // Explicitly close the session to ensure proper cleanup
      session->handleTimeout("CONNECTION");
    } else {
      // Connection is still within idle timeout, keep it
      newIdleQueue.push(session);
    }
  }

  // Replace the idle queue with the filtered one
  idleConnections_ = std::move(newIdleQueue);

  if (cleanedUp > 0) {
    Logger::getInstance().log(
        LogLevel::INFO, "ConnectionPoolManager",
        "Cleaned up " + std::to_string(cleanedUp) + " idle connections. " +
            "Active: " + std::to_string(activeConnections_.size()) +
            ", Idle: " + std::to_string(idleConnections_.size()));
  }

  return cleanedUp;
}

void ConnectionPoolManager::shutdown() {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");

  if (shutdownRequested_) {
    return;
  }

  shutdownRequested_ = true;

  Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager",
                            "Shutting down connection pool");

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

  Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager",
                            "Connection pool shutdown complete");
}

// Metrics and monitoring methods

size_t ConnectionPoolManager::getActiveConnections() const noexcept {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  return activeConnections_.size();
}

size_t ConnectionPoolManager::getIdleConnections() const noexcept {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  return idleConnections_.size();
}

size_t ConnectionPoolManager::getTotalConnections() const noexcept {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  return activeConnections_.size() + idleConnections_.size();
}

size_t ConnectionPoolManager::getMaxConnections() const noexcept {
  return maxConnections_;
}

size_t ConnectionPoolManager::getMinConnections() const {
  return minConnections_;
}

std::chrono::seconds ConnectionPoolManager::getIdleTimeout() const {
  return idleTimeout_;
}

size_t ConnectionPoolManager::getConnectionReuseCount() const {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  return connectionReuseCount_;
}

size_t ConnectionPoolManager::getTotalConnectionsCreated() const {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  return totalConnectionsCreated_;
}

bool ConnectionPoolManager::isAtMaxCapacity() const {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  return (activeConnections_.size() + idleConnections_.size()) >=
         maxConnections_;
}

size_t ConnectionPoolManager::getQueueSize() const {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  return requestQueue_.size();
}

size_t ConnectionPoolManager::getMaxQueueSize() const noexcept {
  return maxQueueSize_;
}

size_t ConnectionPoolManager::getRejectedRequestCount() const noexcept {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  return rejectedRequestCount_;
}

void ConnectionPoolManager::resetStatistics() {
  etl_plus::ScopedTimedLock<etl_plus::ContainerMutex> lock(
      poolMutex_, std::chrono::milliseconds(5000), "poolMutex");
  connectionReuseCount_ = 0;
  totalConnectionsCreated_ = 0;
  rejectedRequestCount_ = 0;
  Logger::getInstance().log(LogLevel::INFO, "ConnectionPoolManager",
                            "Statistics reset");
}

// Private methods

std::shared_ptr<PooledSession>
ConnectionPoolManager::createNewSession(tcp::socket &&socket) {
  // For testing purposes, if handler_ or wsManager_ are null, return null
  if (!handler_ || !wsManager_) {
    Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager",
                              "Cannot create session with null dependencies");
    return nullptr;
  }

  auto session =
      std::make_shared<PooledSession>(std::move(socket), handler_, wsManager_,
                                      timeoutManager_, performanceMonitor_);

  ++totalConnectionsCreated_;

  Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager",
                            "Created new PooledSession. Total created: " +
                                std::to_string(totalConnectionsCreated_));

  return session;
}

void ConnectionPoolManager::scheduleCleanup() {
  if (shutdownRequested_) {
    return;
  }

  if (!cleanupTimer_) {
    cleanupTimer_.emplace(ioc_);
  }

  // Schedule cleanup to run every half of the idle timeout period
  auto cleanupInterval =
      std::chrono::duration_cast<std::chrono::milliseconds>(idleTimeout_) / 2;

  cleanupTimer_->expires_after(cleanupInterval);
  cleanupTimer_->async_wait(
      [this](const boost::system::error_code &ec) { handleCleanupTimer(ec); });
}

void ConnectionPoolManager::handleCleanupTimer(
    const boost::system::error_code &ec) {
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

void ConnectionPoolManager::removeFromActiveSet(
    std::shared_ptr<PooledSession> session) {
  auto it = activeConnections_.find(session);
  if (it != activeConnections_.end()) {
    activeConnections_.erase(it);
  }
}

void ConnectionPoolManager::addToIdleQueue(
    std::shared_ptr<PooledSession> session) {
  idleConnections_.push(session);
}

bool ConnectionPoolManager::shouldCleanupSession(
    std::shared_ptr<PooledSession> session) const {
  if (!session) {
    return true;
  }

  auto now = std::chrono::steady_clock::now();
  auto sessionLastActivity = session->getLastActivity();
  auto timeSinceLastActivity = std::chrono::duration_cast<std::chrono::seconds>(
      now - sessionLastActivity);

  return timeSinceLastActivity >= idleTimeout_;
}

void ConnectionPoolManager::processQueuedRequests() {
  // Must be called with poolMutex_ held
  while (!requestQueue_.empty() && !idleConnections_.empty()) {
    auto queuedRequest = std::move(requestQueue_.front());
    requestQueue_.pop();

    // Check if the request has expired
    auto waitTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - queuedRequest.queueTime);

    if (waitTime >= maxQueueWaitTime_) {
      Logger::getInstance().log(LogLevel::WARN, "ConnectionPoolManager",
                                "Dropping expired queued request (waited " +
                                    std::to_string(waitTime.count()) + "s)");

      try {
        sendErrorResponse(queuedRequest.socket,
                          "Request timeout - server overloaded");
      } catch (...) {
        // Ignore errors when sending error response
      }
      ++rejectedRequestCount_;
      continue;
    }

    // Get an idle connection
    auto session = idleConnections_.front();
    idleConnections_.pop();

    // Reset the session and move to active
    session->reset();
    activeConnections_.insert(session);
    ++connectionReuseCount_;

    // Record connection reuse for performance monitoring
    if (performanceMonitor_) {
      performanceMonitor_->recordConnectionReuse();
    }

    Logger::getInstance().log(
        LogLevel::DEBUG, "ConnectionPoolManager",
        "Processed queued request. Queue size: " +
            std::to_string(requestQueue_.size()) +
            ", Active: " + std::to_string(activeConnections_.size()) +
            ", Idle: " + std::to_string(idleConnections_.size()));

    // Start the session with the queued socket
    // Note: This is a simplified approach. In a real implementation,
    // we would need to properly handle the socket transfer
    try {
      session->run();
    } catch (const std::exception &e) {
      Logger::getInstance().log(LogLevel::ERROR, "ConnectionPoolManager",
                                "Error starting session for queued request: " +
                                    std::string(e.what()));

      // Remove from active set and continue
      activeConnections_.erase(session);
    }
  }
}

size_t ConnectionPoolManager::cleanupExpiredQueuedRequests() {
  // Must be called with poolMutex_ held
  size_t cleanedUp = 0;
  std::queue<QueuedRequest, std::deque<QueuedRequest>> newQueue;
  auto now = std::chrono::steady_clock::now();

  while (!requestQueue_.empty()) {
    auto request = std::move(requestQueue_.front());
    requestQueue_.pop();

    auto waitTime = std::chrono::duration_cast<std::chrono::seconds>(
        now - request.queueTime);

    if (waitTime >= maxQueueWaitTime_) {
      Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager",
                                "Cleaning up expired queued request (waited " +
                                    std::to_string(waitTime.count()) + "s)");

      try {
        sendErrorResponse(request.socket,
                          "Request timeout - server overloaded");
      } catch (...) {
        // Ignore errors when sending error response
      }
      ++cleanedUp;
      ++rejectedRequestCount_;
    } else {
      newQueue.push(std::move(request));
    }
  }

  requestQueue_ = std::move(newQueue);
  return cleanedUp;
}

void ConnectionPoolManager::sendErrorResponse(tcp::socket &socket,
                                              const std::string &errorMessage) {
  try {
    // Create a simple HTTP error response
    std::string response = "HTTP/1.1 503 Service Unavailable\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: " +
                           std::to_string(errorMessage.length() + 13) +
                           "\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "{\"error\":\"" +
                           errorMessage + "\"}";

    boost::system::error_code ec;
    boost::asio::write(socket, boost::asio::buffer(response), ec);

    if (ec) {
      Logger::getInstance().log(LogLevel::DEBUG, "ConnectionPoolManager",
                                "Error sending error response: " +
                                    ec.message());
    }

    // Close the socket
    socket.shutdown(tcp::socket::shutdown_both, ec);
    socket.close(ec);

  } catch (const std::exception &e) {
    Logger::getInstance().log(LogLevel::ERROR, "ConnectionPoolManager",
                              "Exception sending error response: " +
                                  std::string(e.what()));
  } catch (...) {
    Logger::getInstance().log(LogLevel::ERROR, "ConnectionPoolManager",
                              "Unknown exception sending error response");
  }
}