#include "database_connection_pool.hpp"
#include "etl_exceptions.hpp"
#include <algorithm>
#include <numeric>

DatabaseConnectionPool::DatabaseConnectionPool(
    const DatabaseConnectionConfig &config)
    : config_(config) {
  if (config_.maxConnections < config_.minConnections) {
    throw std::invalid_argument(
        "maxConnections cannot be less than minConnections");
  }

  DB_LOG_INFO("Database connection pool initialized with max=" +
              std::to_string(config_.maxConnections) +
              ", min=" + std::to_string(config_.minConnections));

  // Pre-create minimum connections
  for (int i = 0; i < config_.minConnections; ++i) {
    try {
      auto conn = createConnection();
      if (conn) {
        idleConnections_.push(std::make_shared<PooledConnection>(conn));
        metrics_.totalConnections++;
        metrics_.connectionsCreated++;
      }
    } catch (const std::exception &e) {
      DB_LOG_ERROR("Failed to create initial connection " + std::to_string(i) +
                   ": " + e.what());
    }
  }

  if (config_.enableHealthChecks) {
    startHealthMonitoring();
  }
}

DatabaseConnectionPool::~DatabaseConnectionPool() { closeAll(); }

std::shared_ptr<pqxx::connection> DatabaseConnectionPool::acquireConnection() {
  std::unique_lock<std::mutex> lock(poolMutex_);
  auto startTime = std::chrono::steady_clock::now();

  // Check if shutdown is in progress
  if (shutdown_.load()) {
    throw etl::ETLException(
        etl::ErrorCode::DATABASE_ERROR,
        "Connection pool is shutting down - no new connections allowed");
  }

  // Wait for an available connection using predicate-based wait
  auto endTime = startTime + config_.connectionTimeout;
  while (!poolCondition_.wait_until(lock, endTime, [this]() {
    // Return true if we should stop waiting (connection available or can create
    // new)
    return !idleConnections_.empty() ||
           activeConnections_.size() < config_.maxConnections ||
           shutdown_.load();
  })) {
    // Wait timed out - check if we should retry or fail
    if (std::chrono::steady_clock::now() >= endTime) {
      metrics_.connectionTimeouts++;
      throw etl::ETLException(
          etl::ErrorCode::NETWORK_ERROR,
          "Connection pool timeout - no available connections");
    }
    // Spurious wakeup - continue waiting
  }

  // Check if shutdown occurred while waiting
  if (shutdown_.load()) {
    throw etl::ETLException(
        etl::ErrorCode::DATABASE_ERROR,
        "Connection pool is shutting down - no new connections allowed");
  }

  std::shared_ptr<PooledConnection> pooledConn;

  if (!idleConnections_.empty()) {
    pooledConn = idleConnections_.front();
    idleConnections_.pop();
  } else if (activeConnections_.size() < config_.maxConnections) {
    // Release lock temporarily during connection creation
    lock.unlock();

    std::shared_ptr<pqxx::connection> conn;
    std::shared_ptr<PooledConnection> pooledConn;
    try {
      conn = createConnection();
      if (conn) {
        pooledConn = std::make_shared<PooledConnection>(conn);
      }
    } catch (const std::exception &e) {
      // Reacquire lock
      lock.lock();
      DB_LOG_ERROR("Failed to create new connection: " + std::string(e.what()));
      throw etl::ETLException(etl::ErrorCode::DATABASE_ERROR,
                              "Failed to create new database connection");
    }

    // Reacquire lock to add connection
    lock.lock();

    if (pooledConn) {
      activeConnections_.push_back(pooledConn);
      metrics_.totalConnections++;
      metrics_.connectionsCreated++;
    }
  }

  if (pooledConn) {
    pooledConn->lastUsedTime = std::chrono::steady_clock::now();
    // Note: pooledConn is already in activeConnections_ (replaced nullptr
    // placeholder)

    // Record wait time using circular buffer
    auto waitTime = std::chrono::steady_clock::now() - startTime;
    double waitTimeMs =
        std::chrono::duration<double, std::milli>(waitTime).count();
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

void DatabaseConnectionPool::releaseConnection(
    std::shared_ptr<pqxx::connection> conn) {
  if (!conn)
    return;

  std::lock_guard<std::mutex> lock(poolMutex_);

  // Find the connection in active connections
  auto it = std::find_if(activeConnections_.begin(), activeConnections_.end(),
                         [&conn](const std::shared_ptr<PooledConnection> &pc) {
                           if (!pc) return false;
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

bool DatabaseConnectionPool::gracefulShutdown(
    std::chrono::milliseconds timeout) {
  DB_LOG_INFO("Initiating graceful shutdown of database connection pool");

  // Set shutdown flag to prevent new connections
  shutdown_.store(true);

  // Stop health monitoring
  stopHealthMonitoring();

  auto startTime = std::chrono::steady_clock::now();
  auto endTime = startTime + timeout;

  std::unique_lock<std::mutex> lock(poolMutex_);

  // Wait for all active connections to be released using predicate-based wait
  while (!poolCondition_.wait_until(
      lock, endTime, [this]() { return activeConnections_.empty(); })) {
    // Check if we actually timed out (not just a spurious wakeup)
    if (std::chrono::steady_clock::now() >= endTime) {
      DB_LOG_WARN("Graceful shutdown timeout elapsed, forcing close of " +
                  std::to_string(activeConnections_.size()) +
                  " active connections");
      break;
    }
    // Spurious wakeup - continue waiting
    DB_LOG_DEBUG(
        "Spurious wakeup during graceful shutdown, continuing to wait");
  }

  // Close all connections (both idle and remaining active)
  idleConnections_ = std::queue<std::shared_ptr<PooledConnection>>();
  activeConnections_.clear();
  metrics_.totalConnections = 0;

  auto elapsed = std::chrono::steady_clock::now() - startTime;
  auto elapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  if (activeConnections_.empty()) {
    DB_LOG_INFO("Graceful shutdown completed successfully in " +
                std::to_string(elapsedMs) + "ms");
    return true;
  } else {
    DB_LOG_ERROR("Graceful shutdown timed out after " +
                 std::to_string(elapsedMs) + "ms with " +
                 std::to_string(activeConnections_.size()) +
                 " active connections remaining");
    return false;
  }
}

void DatabaseConnectionPool::startHealthMonitoring() {
  if (running_.load())
    return;

  running_.store(true);
  healthCheckThread_ = std::thread([this]() {
    while (running_.load()) {
      try {
        performHealthCheck();
        cleanupExpiredConnections();
        adjustPoolSize();
      } catch (const std::exception &e) {
        DB_LOG_ERROR("Health check error: " + std::string(e.what()));
      }

      // Use condition variable for interruptible sleep
      {
        std::unique_lock<std::mutex> lock(healthMutex_);
        if (running_.load()) {
          healthCV_.wait_for(lock, config_.healthCheckInterval);
        }
      }
    }
  });

  DB_LOG_INFO("Database connection pool health monitoring started");
}

void DatabaseConnectionPool::stopHealthMonitoring() {
  running_.store(false);
  {
    std::lock_guard<std::mutex> lock(healthMutex_);
    healthCV_.notify_all();
  }
  if (healthCheckThread_.joinable()) {
    healthCheckThread_.join();
  }
}

bool DatabaseConnectionPool::isHealthy() const {
  std::lock_guard<std::mutex> lock(metricsMutex_);
  auto timeSinceLastCheck =
      std::chrono::steady_clock::now() - metrics_.lastHealthCheck;
  return timeSinceLastCheck < config_.healthCheckInterval * 2;
}

DatabaseConnectionPool::PoolMetrics DatabaseConnectionPool::getMetrics() const {
  std::lock_guard<std::mutex> lock(metricsMutex_);
  PoolMetrics metrics = metrics_;
  metrics.activeConnections = activeConnections_.size();
  metrics.idleConnections = idleConnections_.size();

  if (!waitTimes_.empty()) {
    metrics.averageWaitTimeMs =
        std::accumulate(waitTimes_.begin(), waitTimes_.end(), 0.0) /
        waitTimes_.size();
  }

  return metrics;
}

void DatabaseConnectionPool::updateConfig(
    const DatabaseConnectionConfig &newConfig) {
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
    } catch (const std::exception &e) {
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

bool DatabaseConnectionPool::validateConnection(
    std::shared_ptr<pqxx::connection> conn) {
  if (!conn || !conn->is_open())
    return false;

  try {
    pqxx::work txn(*conn);
    pqxx::result result = txn.exec("SELECT 1");
    txn.commit();
    return result.size() == 1;
  } catch (const std::exception &e) {
    DB_LOG_WARN("Connection validation failed: " + std::string(e.what()));
    return false;
  }
}

void DatabaseConnectionPool::performHealthCheck() {
  // Snapshot connections under lock, then validate outside lock
  std::vector<std::shared_ptr<PooledConnection>> idleSnapshot;
  std::vector<std::shared_ptr<PooledConnection>> activeSnapshot;

  {
    std::lock_guard<std::mutex> lock(poolMutex_);
    // Copy idle connections
    idleSnapshot.reserve(idleConnections_.size());
    std::queue<std::shared_ptr<PooledConnection>> tempQueue = idleConnections_;
    while (!tempQueue.empty()) {
      idleSnapshot.push_back(tempQueue.front());
      tempQueue.pop();
    }

    // Copy active connections
    activeSnapshot.reserve(activeConnections_.size());
    activeSnapshot.assign(activeConnections_.begin(), activeConnections_.end());
  }

  // Validate connections outside the lock
  size_t unhealthyCount = 0;
  std::vector<std::shared_ptr<PooledConnection>> healthyIdle;
  std::vector<std::shared_ptr<PooledConnection>> unhealthyActive;

  for (auto &pooledConn : idleSnapshot) {
    if (validateConnection(pooledConn->connection)) {
      healthyIdle.push_back(pooledConn);
    } else {
      unhealthyCount++;
    }
  }

  for (auto &pooledConn : activeSnapshot) {
    if (!validateConnection(pooledConn->connection)) {
      unhealthyActive.push_back(pooledConn);
      unhealthyCount++;
    }
  }

  // Update pool state under lock
  {
    std::lock_guard<std::mutex> lock(poolMutex_);

    // Update idle connections
    while (!idleConnections_.empty()) idleConnections_.pop();
    for (auto &conn : healthyIdle) {
      idleConnections_.push(conn);
    }

    // Update active connections
    for (auto &unhealthyConn : unhealthyActive) {
      unhealthyConn->isHealthy = false;
    }

    // Update metrics for unhealthy connections
    metrics_.connectionsDestroyed += unhealthyCount;
    metrics_.totalConnections -= unhealthyCount;
  }

  {
    std::lock_guard<std::mutex> metricsLock(metricsMutex_);
    metrics_.lastHealthCheck = std::chrono::steady_clock::now();
    if (unhealthyCount > 0) {
      metrics_.healthCheckFailures += unhealthyCount;
    }
  }

  if (unhealthyCount > 0) {
    DB_LOG_WARN("Health check found " + std::to_string(unhealthyCount) +
                " unhealthy connections");
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
  // Determine what needs to be done outside the lock
  size_t currentIdle = 0;
  size_t currentActive = 0;
  size_t connectionsToCreate = 0;
  size_t connectionsToRemove = 0;

  {
    std::lock_guard<std::mutex> lock(poolMutex_);
    currentIdle = idleConnections_.size();
    currentActive = activeConnections_.size();

    size_t totalCurrent = currentIdle + currentActive;

    if (totalCurrent < config_.minConnections) {
      connectionsToCreate = config_.minConnections - totalCurrent;
    } else if (currentIdle > config_.minConnections &&
               totalCurrent > config_.minConnections) {
      connectionsToRemove = std::min(currentIdle - config_.minConnections,
                                     currentIdle + currentActive - config_.minConnections);
    }
  }

  // Create connections outside the lock
  std::vector<std::shared_ptr<PooledConnection>> newConnections;
  for (size_t i = 0; i < connectionsToCreate; ++i) {
    try {
      auto conn = createConnection();
      if (conn) {
        newConnections.push_back(std::make_shared<PooledConnection>(conn));
      }
    } catch (const std::exception &e) {
      DB_LOG_ERROR("Failed to create connection during pool adjustment: " +
                   std::string(e.what()));
      break;
    }
  }

  // Update pool state under lock
  {
    std::lock_guard<std::mutex> lock(poolMutex_);

    // Add new connections
    for (auto &conn : newConnections) {
      idleConnections_.push(conn);
      metrics_.totalConnections++;
      metrics_.connectionsCreated++;
    }

    // Remove excess idle connections
    for (size_t i = 0; i < connectionsToRemove; ++i) {
      if (!idleConnections_.empty()) {
        idleConnections_.pop();
        metrics_.connectionsDestroyed++;
        metrics_.totalConnections--;
      }
    }
  }
}

std::string DatabaseConnectionPool::buildConnectionString() const {
  return "host=" + config_.host + " port=" + std::to_string(config_.port) +
         " dbname=" + config_.database + " user=" + config_.username +
         " password=" + config_.getPassword() + " connect_timeout=" +
         std::to_string(config_.connectionTimeout.count());
}
