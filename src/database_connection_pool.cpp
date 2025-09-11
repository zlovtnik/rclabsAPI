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

    // Wait for an available connection
    while (idleConnections_.empty() && activeConnections_.size() >= config_.maxConnections) {
        if (poolCondition_.wait_for(lock, config_.connectionTimeout) == std::cv_status::timeout) {
            metrics_.connectionTimeouts++;
            throw etl::ETLException(etl::ErrorCode::NETWORK_ERROR,
                                   "Connection pool timeout - no available connections");
        }
    }

    std::shared_ptr<PooledConnection> pooledConn;

    if (!idleConnections_.empty()) {
        pooledConn = idleConnections_.front();
        idleConnections_.pop();
    } else if (activeConnections_.size() < config_.maxConnections) {
        // Create new connection if under max limit
        try {
            auto conn = createConnection();
            if (conn) {
                pooledConn = std::make_shared<PooledConnection>(conn);
                metrics_.totalConnections++;
                metrics_.connectionsCreated++;
            }
        } catch (const std::exception& e) {
            DB_LOG_ERROR("Failed to create new connection: " + std::string(e.what()));
            throw etl::ETLException(etl::ErrorCode::DATABASE_ERROR,
                                   "Failed to create new database connection");
        }
    }

    if (pooledConn) {
        pooledConn->lastUsedTime = std::chrono::steady_clock::now();
        activeConnections_.push_back(pooledConn);

        // Record wait time
        auto waitTime = std::chrono::steady_clock::now() - startTime;
        double waitTimeMs = std::chrono::duration<double, std::milli>(waitTime).count();
        {
            std::lock_guard<std::mutex> metricsLock(metricsMutex_);
            waitTimes_.push_back(waitTimeMs);
            if (waitTimes_.size() > 100) { // Keep last 100 measurements
                waitTimes_.erase(waitTimes_.begin());
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
    stopHealthMonitoring();

    std::lock_guard<std::mutex> lock(poolMutex_);
    idleConnections_ = std::queue<std::shared_ptr<PooledConnection>>();
    activeConnections_.clear();
    metrics_.totalConnections = 0;
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
           " password=" + config_.password +
           " connect_timeout=" + std::to_string(config_.connectionTimeout.count());
}
