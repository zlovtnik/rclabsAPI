#pragma once

#include "connection_pool.hpp"
#include "lock_utils.hpp"
#include "message_broadcaster.hpp"
#include "websocket_connection.hpp"
#include <boost/asio/io_context.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

/**
 * @brief Configuration for WebSocketManager behavior
 */
struct WebSocketManagerConfig {
  ConnectionPoolConfig connectionPoolConfig;
  MessageBroadcasterConfig messageBroadcasterConfig;
  bool autoStartComponents = true; // Automatically start pool and broadcaster
};

/**
 * @brief WebSocketManager coordinates WebSocket connections and message
 * broadcasting
 *
 * This class acts as a coordinator between ConnectionPool and
 * MessageBroadcaster, maintaining backward compatibility with existing APIs
 * while providing enhanced functionality through component separation.
 */
class WebSocketManager : public std::enable_shared_from_this<WebSocketManager> {
public:
  WebSocketManager();
  explicit WebSocketManager(const WebSocketManagerConfig &config);
  ~WebSocketManager();

  // Manager lifecycle
  void start();
  void stop();
  bool isRunning() const;

  // Connection handling
  void handleUpgrade(tcp::socket socket);
  void addConnection(std::shared_ptr<WebSocketConnection> connection);
  void removeConnection(const std::string &connectionId);

  // Message broadcasting (delegated to MessageBroadcaster)
  void broadcastMessage(const std::string &message);
  void sendToConnection(const std::string &connectionId,
                        const std::string &message);

  // Enhanced broadcasting with filtering
  void broadcastJobUpdate(const std::string &message, const std::string &jobId);
  void broadcastLogMessage(const std::string &message, const std::string &jobId,
                           const std::string &logLevel);
  void broadcastByMessageType(const std::string &message,
                              MessageType messageType,
                              const std::string &jobId = "");
  void broadcastToFilteredConnections(
      const std::string &message,
      std::function<bool(const ConnectionFilters &)> filterPredicate);

  // Connection information (delegated to ConnectionPool)
  size_t getConnectionCount() const;
  std::vector<std::string> getConnectionIds() const;

  // Connection filter management (delegated to MessageBroadcaster)
  void setConnectionFilters(const std::string &connectionId,
                            const ConnectionFilters &filters);
  ConnectionFilters getConnectionFilters(const std::string &connectionId) const;
  void updateConnectionFilters(const std::string &connectionId,
                               const ConnectionFilters &filters);

  // Enhanced filter management
  void addJobFilterToConnection(const std::string &connectionId,
                                const std::string &jobId);
  void removeJobFilterFromConnection(const std::string &connectionId,
                                     const std::string &jobId);
  void addMessageTypeFilterToConnection(const std::string &connectionId,
                                        MessageType messageType);
  void removeMessageTypeFilterFromConnection(const std::string &connectionId,
                                             MessageType messageType);
  void addLogLevelFilterToConnection(const std::string &connectionId,
                                     const std::string &logLevel);
  void removeLogLevelFilterFromConnection(const std::string &connectionId,
                                          const std::string &logLevel);
  void clearConnectionFilters(const std::string &connectionId);

  // Connection analysis and statistics
  std::vector<std::string> getConnectionsForJob(const std::string &jobId) const;
  std::vector<std::string>
  getConnectionsForMessageType(MessageType messageType) const;
  std::vector<std::string>
  getConnectionsForLogLevel(const std::string &logLevel) const;
  size_t getFilteredConnectionCount() const;
  size_t getUnfilteredConnectionCount() const;

  // Advanced message routing
  void broadcastWithAdvancedRouting(const WebSocketMessage &message);
  void sendToMatchingConnections(
      const WebSocketMessage &message,
      std::function<bool(const ConnectionFilters &, const WebSocketMessage &)>
          customMatcher);
  bool testConnectionFilter(const std::string &connectionId,
                            const WebSocketMessage &testMessage) const;

  // Component access for advanced usage
  std::shared_ptr<ConnectionPool> getConnectionPool() const {
    return connectionPool_;
  }
  std::shared_ptr<MessageBroadcaster> getMessageBroadcaster() const {
    return messageBroadcaster_;
  }

  // Configuration management
  void updateConfig(const WebSocketManagerConfig &newConfig);
  const WebSocketManagerConfig &getConfig() const { return config_; }

  // Component-specific configuration
  void updateConnectionPoolConfig(const ConnectionPoolConfig &newConfig);
  void
  updateMessageBroadcasterConfig(const MessageBroadcasterConfig &newConfig);

  // Statistics and monitoring
  ConnectionPoolStats getConnectionPoolStats() const;
  MessageBroadcasterStats getMessageBroadcasterStats() const;

private:
  // Configuration
  WebSocketManagerConfig config_;

  // Component dependencies
  std::shared_ptr<ConnectionPool> connectionPool_;
  std::shared_ptr<MessageBroadcaster> messageBroadcaster_;

  // Manager state
  std::atomic<bool> running_{false};

  // Internal helper methods
  void initializeComponents();
  void startComponents();
  void stopComponents();
};