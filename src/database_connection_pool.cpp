#include "database_connection_pool.hpp"
#include "etl_exceptions.hpp"
#include <algorithm>
#include <numeric>

DatabaseConnectionPool::DatabaseConnectionPool(const DatabaseConnectionConfig& config)
    : config_(config) {
    if (config_.maxConnections < config_.minConnections) {
        throw std::invalid_argument("maxConnections cannot be less than minConnections");
    }

    DB_LOG_INFO("Database connection pool initialized with max=" +
                std::to_string(config_.maxConnections) + ", min=" +
                std::to_string(config_.minConnections));

    // Pre-create minimum connections
    for (int i = 0; i < config_.minConnections; ++i) {
        try {
            auto conn = createConnection();
            if (conn) {
                idleConnections_.push(std::make_shared<PooledConnection>(conn));
                metrics_.totalConnections++;
                metrics_.connectionsCreated++;
            }
        } catch (const std::exception& e) {
            DB_LOG_ERROR("Failed to create initial connection " + std::to_string(i) + ": " + e.what());
        }
    }

    if (config_.enableHealthChecks) {
        startHealthMonitoring();
    }
}

DatabaseConnectionPool::~DatabaseConnectionPool() {
    closeAll();
}

std::shared_ptr<pqxx::connection> DatabaseConnectionPool::acquireConnection() {
    std::unique_lock<std::mutex> lock(poolMutex_);
    auto startTime = std::chrono::steady_clock::now();

    // Check if shutdown is in progress
    if (shutdown_.load()) {
        throw etl::ETLException(etl::ErrorCode::DATABASE_ERROR,
                               "Connection pool is shutting down - no new connections allowed");
    }

    // Wait for an available connection using predicate-based wait
    auto endTime = startTime + config_.connectionTimeout;
    while (!poolCondition_.wait_until(lock, endTime, [this]() {
        // Return true if we should stop waiting (connection available or can create new)
        return !idleConnections_.empty() ||
               activeConnections_.size() < config_.maxConnections ||
               shutdown_.load();
    })) {
        // Wait timed out - check if we should retry or fail
        if (std::chrono::steady_clock::now() >= endTime) {
            metrics_.connectionTimeouts++;
            throw etl::ETLException(etl::ErrorCode::NETWORK_ERROR,
                                   "Connection pool timeout - no available connections");
        }
        // Spurious wakeup - continue waiting
    }

    // Check if shutdown occurred while waiting
    if (shutdown_.load()) {
        throw etl::ETLException(etl::ErrorCode::DATABASE_ERROR,
                               "Connection pool is shutting down - no new connections allowed");
    }

    std::shared_ptr<PooledConnection> pooledConn;

    if (!idleConnections_.empty()) {
        pooledConn = idleConnections_.front();
        idleConnections_.pop();
    } else if (activeConnections_.size() < config_.maxConnections) {
        // Reserve a slot and release lock to create connection
        // This prevents blocking other threads during slow connection creation
        activeConnections_.emplace_back(nullptr); // Reserve slot
        
        // Release lock temporarily during connection creation
        lock.unlock();
        
        std::shared_ptr<pqxx::connection> conn;
        try {
            conn = createConnection();
        } catch (const std::exception& e) {
            // Reacquire lock to remove reserved slot on failure
            lock.lock();
            activeConnections_.pop_back(); // Remove reserved slot
            DB_LOG_ERROR("Failed to create new connection: " + std::string(e.what()));
            throw etl::ETLException(etl::ErrorCode::DATABASE_ERROR,
                                   "Failed to create new database connection");
        }
        
        // Reacquire lock to finalize connection setup
        lock.lock();
        
        if (conn) {
            pooledConn = std::make_shared<PooledConnection>(conn);
            // Replace the reserved nullptr with actual connection
            activeConnections_.back() = pooledConn;
            metrics_.totalConnections++;
            metrics_.connectionsCreated++;
        } else {
            // Remove reserved slot if connection creation failed
            activeConnections_.pop_back();
        }
    }

    if (pooledConn) {
        pooledConn->lastUsedTime = std::chrono::steady_clock::now();
        // Note: pooledConn is already in activeConnections_ (replaced nullptr placeholder)

        // Record wait time using circular buffer
        auto waitTime = std::chrono::steady_clock::now() - startTime;
        double waitTimeMs = std::chrono::duration<double, std::milli>(waitTime).count();
        {
            std::lock_guard<std::mutex> metricsLock(metricsMutex_);
            waitTimes_.push_back(waitTimeMs);
            if (waitTimes_.size() > MAX_WAIT_TIMES) {
                waitTimes_.pop_front();
            }
        }

        return pooledConn->connection;
    }

    throw etl::ETLException(etl::ErrorCode::DATABASE_ERROR,
                           "Unable to acquire database connection");
}

void DatabaseConnectionPool::releaseConnection(std::shared_ptr<pqxx::connection> conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(poolMutex_);

    // Find the connection in active connections
    auto it = std::find_if(activeConnections_.begin(), activeConnections_.end(),
                          [&conn](const std::shared_ptr<PooledConnection>& pc) {
                              return pc->connection == conn;
                          });

    if (it != activeConnections_.end()) {
        auto pooledConn = *it;
        activeConnections_.erase(it);

        // Validate connection before returning to pool
        if (validateConnection(conn)) {
            pooledConn->lastUsedTime = std::chrono::steady_clock::now();
            idleConnections_.push(pooledConn);
        } else {
            // Connection is unhealthy, don't reuse
            metrics_.connectionsDestroyed++;
            metrics_.totalConnections--;
        }
    }

    poolCondition_.notify_one();
}

void DatabaseConnectionPool::closeAll() {
    shutdown_.store(true);
    stopHealthMonitoring();

    std::lock_guard<std::mutex> lock(poolMutex_);
    idleConnections_ = std::queue<std::shared_ptr<PooledConnection>>();
    activeConnections_.clear();
    metrics_.totalConnections = 0;
}

bool DatabaseConnectionPool::gracefulShutdown(std::chrono::milliseconds timeout) {
    DB_LOG_INFO("Initiating graceful shutdown of database connection pool");

    // Set shutdown flag to prevent new connections
    shutdown_.store(true);

    // Stop health monitoring
    stopHealthMonitoring();

    auto startTime = std::chrono::steady_clock::now();
    auto endTime = startTime + timeout;

    std::unique_lock<std::mutex> lock(poolMutex_);

    // Wait for all active connections to be released using predicate-based wait
    while (!poolCondition_.wait_until(lock, endTime, [this]() {
        return activeConnections_.empty();
    })) {
        // Check if we actually timed out (not just a spurious wakeup)
        if (std::chrono::steady_clock::now() >= endTime) {
            DB_LOG_WARN("Graceful shutdown timeout elapsed, forcing close of " +
                       std::to_string(activeConnections_.size()) + " active connections");
            break;
        }
        // Spurious wakeup - continue waiting
        DB_LOG_DEBUG("Spurious wakeup during graceful shutdown, continuing to wait");
    }

    // Close all connections (both idle and remaining active)
    idleConnections_ = std::queue<std::shared_ptr<PooledConnection>>();
    activeConnections_.clear();
    metrics_.totalConnections = 0;

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (activeConnections_.empty()) {
        DB_LOG_INFO("Graceful shutdown completed successfully in " +
                   std::to_string(elapsedMs) + "ms");
        return true;
    } else {
        DB_LOG_ERROR("Graceful shutdown timed out after " + std::to_string(elapsedMs) +
                    "ms with " + std::to_string(activeConnections_.size()) +
                    " active connections remaining");
        return false;
    }
}

void DatabaseConnectionPool::startHealthMonitoring() {
    if (running_.load()) return;

    running_.store(true);
    healthCheckThread_ = std::thread([this]() {
        while (running_.load()) {
            try {
                performHealthCheck();
                cleanupExpiredConnections();
                adjustPoolSize();
            } catch (const std::exception& e) {
                DB_LOG_ERROR("Health check error: " + std::string(e.what()));
            }

            std::this_thread::sleep_for(config_.healthCheckInterval);
        }
    });

    DB_LOG_INFO("Database connection pool health monitoring started");
}

void DatabaseConnectionPool::stopHealthMonitoring() {
    running_.store(false);
    if (healthCheckThread_.joinable()) {
        healthCheckThread_.join();
    }
}

bool DatabaseConnectionPool::isHealthy() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto timeSinceLastCheck = std::chrono::steady_clock::now() - metrics_.lastHealthCheck;
    return timeSinceLastCheck < config_.healthCheckInterval * 2;
}

DatabaseConnectionPool::PoolMetrics DatabaseConnectionPool::getMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    PoolMetrics metrics = metrics_;
    metrics.activeConnections = activeConnections_.size();
    metrics.idleConnections = idleConnections_.size();

    if (!waitTimes_.empty()) {
        metrics.averageWaitTimeMs = std::accumulate(waitTimes_.begin(), waitTimes_.end(), 0.0) / waitTimes_.size();
    }

    return metrics;
}

void DatabaseConnectionPool::updateConfig(const DatabaseConnectionConfig& newConfig) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    config_ = newConfig;
    DB_LOG_INFO("Database connection pool configuration updated");
}

std::shared_ptr<pqxx::connection> DatabaseConnectionPool::createConnection() {
    std::string connStr = buildConnectionString();

    for (int attempt = 0; attempt < config_.maxRetries; ++attempt) {
        try {
            auto conn = std::make_shared<pqxx::connection>(connStr);

            if (conn->is_open() && validateConnection(conn)) {
                DB_LOG_DEBUG("Database connection created successfully");
                return conn;
            }
        } catch (const std::exception& e) {
            DB_LOG_WARN("Connection attempt " + std::to_string(attempt + 1) +
                       " failed: " + e.what());

            if (attempt < config_.maxRetries - 1) {
                std::this_thread::sleep_for(config_.retryDelay);
            }
        }
    }

    throw etl::ETLException(etl::ErrorCode::DATABASE_ERROR,
                           "Failed to create database connection after " +
                           std::to_string(config_.maxRetries) + " attempts");
}

bool DatabaseConnectionPool::validateConnection(std::shared_ptr<pqxx::connection> conn) {
    if (!conn || !conn->is_open()) return false;

    try {
        pqxx::work txn(*conn);
        pqxx::result result = txn.exec("SELECT 1");
        txn.commit();
        return result.size() == 1;
    } catch (const std::exception& e) {
        DB_LOG_WARN("Connection validation failed: " + std::string(e.what()));
        return false;
    }
}

void DatabaseConnectionPool::performHealthCheck() {
    std::lock_guard<std::mutex> lock(poolMutex_);

    size_t unhealthyCount = 0;

    // Check idle connections
    std::queue<std::shared_ptr<PooledConnection>> tempQueue;
    while (!idleConnections_.empty()) {
        auto pooledConn = idleConnections_.front();
        idleConnections_.pop();

        if (validateConnection(pooledConn->connection)) {
            tempQueue.push(pooledConn);
        } else {
            unhealthyCount++;
            metrics_.connectionsDestroyed++;
            metrics_.totalConnections--;
        }
    }
    idleConnections_ = std::move(tempQueue);

    // Check active connections (mark as unhealthy if needed)
    for (auto& pooledConn : activeConnections_) {
        if (!validateConnection(pooledConn->connection)) {
            pooledConn->isHealthy = false;
            unhealthyCount++;
        }
    }

    {
        std::lock_guard<std::mutex> metricsLock(metricsMutex_);
        metrics_.lastHealthCheck = std::chrono::steady_clock::now();
        if (unhealthyCount > 0) {
            metrics_.healthCheckFailures += unhealthyCount;
        }
    }

    if (unhealthyCount > 0) {
        DB_LOG_WARN("Health check found " + std::to_string(unhealthyCount) + " unhealthy connections");
    }
}

void DatabaseConnectionPool::cleanupExpiredConnections() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    auto now = std::chrono::steady_clock::now();
    auto maxAge = std::chrono::hours(1); // Connections older than 1 hour

    std::queue<std::shared_ptr<PooledConnection>> tempQueue;
    while (!idleConnections_.empty()) {
        auto pooledConn = idleConnections_.front();
        idleConnections_.pop();

        auto age = now - pooledConn->createdTime;
        if (age < maxAge) {
            tempQueue.push(pooledConn);
        } else {
            metrics_.connectionsDestroyed++;
            metrics_.totalConnections--;
        }
    }
    idleConnections_ = std::move(tempQueue);
}

void DatabaseConnectionPool::adjustPoolSize() {
    std::lock_guard<std::mutex> lock(poolMutex_);

    // Maintain minimum connections
    while (idleConnections_.size() + activeConnections_.size() < config_.minConnections) {
        try {
            auto conn = createConnection();
            if (conn) {
                idleConnections_.push(std::make_shared<PooledConnection>(conn));
                metrics_.totalConnections++;
                metrics_.connectionsCreated++;
            }
        } catch (const std::exception& e) {
            DB_LOG_ERROR("Failed to create connection during pool adjustment: " + std::string(e.what()));
            break;
        }
    }

    // Remove excess idle connections
    while (idleConnections_.size() > config_.minConnections &&
           idleConnections_.size() + activeConnections_.size() > config_.minConnections) {
        idleConnections_.pop();
        metrics_.connectionsDestroyed++;
        metrics_.totalConnections--;
    }
}

std::string DatabaseConnectionPool::buildConnectionString() const {
    return "host=" + config_.host +
           " port=" + std::to_string(config_.port) +
           " dbname=" + config_.database +
           " user=" + config_.username +
           " password=" + config_.getPassword() +
           " connect_timeout=" + std::to_string(config_.connectionTimeout.count());
}
