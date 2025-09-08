#pragma once

#include "job_monitoring_models.hpp"
#include "lock_utils.hpp"
#include "websocket_connection.hpp"
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

/**
 * @brief Configuration for connection pool behavior
 */
struct ConnectionPoolConfig {
  size_t maxConnections = 1000; // Maximum number of concurrent connections
  std::chrono::seconds connectionTimeout =
      std::chrono::seconds(30); // Connection timeout
  std::chrono::seconds healthCheckInterval =
      std::chrono::seconds(60);       // Health check interval
  bool enableHealthMonitoring = true; // Enable automatic health monitoring
  bool enableConnectionCleanup =
      true;                     // Enable automatic cleanup of stale connections
  size_t cleanupBatchSize = 10; // Number of connections to clean up per batch
};

/**
 * @brief Connection pool statistics
 */
struct ConnectionPoolStats {
  size_t totalConnections = 0;
  size_t activeConnections = 0;
  size_t inactiveConnections = 0;
  size_t healthyConnections = 0;
  size_t unhealthyConnections = 0;
  std::chrono::system_clock::time_point lastHealthCheck;
  std::chrono::system_clock::time_point lastCleanup;

  std::string toJson() const;
};

/**
 * @brief Manages a pool of WebSocket connections with lifecycle and health
 * monitoring
 *
 * This component is responsible for:
 * - Storing and managing WebSocket connections
 * - Connection lifecycle (add, remove, cleanup)
 * - Health monitoring and automatic cleanup
 * - Connection statistics and metrics
 * - Thread-safe access to connections
 */
class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
public:
  ConnectionPool();
  explicit ConnectionPool(const ConnectionPoolConfig &config);
  ~ConnectionPool();

  // Pool lifecycle
  void start();
  void stop();
  bool isRunning() const { return running_.load(); }

  // Connection management
  void addConnection(std::shared_ptr<WebSocketConnection> connection);
  void removeConnection(const std::string &connectionId);
  std::shared_ptr<WebSocketConnection>
  getConnection(const std::string &connectionId) const;
  bool hasConnection(const std::string &connectionId) const;

  // Bulk operations
  std::vector<std::shared_ptr<WebSocketConnection>>
  getActiveConnections() const;
  std::vector<std::string> getConnectionIds() const;
  void removeInactiveConnections();

  // Connection health and monitoring
  void performHealthCheck();
  void startHealthMonitoring();
  void stopHealthMonitoring();
  bool isConnectionHealthy(const std::string &connectionId) const;
  std::vector<std::string> getUnhealthyConnections() const;

  // Connection filtering and querying
  std::vector<std::shared_ptr<WebSocketConnection>> getConnectionsByFilter(
      std::function<bool(const std::shared_ptr<WebSocketConnection> &)> filter)
      const;
  std::vector<std::string> getConnectionIdsByFilter(
      std::function<bool(const std::shared_ptr<WebSocketConnection> &)> filter)
      const;

  // Statistics and metrics
  size_t getTotalConnectionCount() const;
  size_t getActiveConnectionCount() const;
  size_t getInactiveConnectionCount() const;
  ConnectionPoolStats getStats() const;

  // Configuration
  void updateConfig(const ConnectionPoolConfig &newConfig);
  const ConnectionPoolConfig &getConfig() const { return config_; }

  // Resource management
  void cleanupStaleConnections();
  void forceCleanup(size_t maxToRemove = 0);

private:
  // Configuration and state
  ConnectionPoolConfig config_;
  mutable etl_plus::ContainerSharedMutex connectionsMutex_;
  std::unordered_map<std::string, std::shared_ptr<WebSocketConnection>>
      connections_;
  std::atomic<bool> running_{false};

  // Health monitoring
  std::unique_ptr<boost::asio::steady_timer> healthCheckTimer_;
  std::unique_ptr<boost::asio::steady_timer> cleanupTimer_;
  std::atomic<bool> healthMonitoringActive_{false};

  // Internal helper methods
  void scheduleHealthCheck();
  void scheduleCleanup();
  bool isConnectionStale(
      const std::shared_ptr<WebSocketConnection> &connection) const;
  void removeConnectionInternal(const std::string &connectionId);
  void updateStats(ConnectionPoolStats &stats) const;
};
