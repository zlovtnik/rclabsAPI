#include "websocket_manager.hpp"
#include "lock_utils.hpp"
#include "logger.hpp"
#include <algorithm>

WebSocketManager::WebSocketManager()
    : WebSocketManager(WebSocketManagerConfig{}) {}

WebSocketManager::WebSocketManager(const WebSocketManagerConfig &config)
    : config_(config) {
  initializeComponents();
  WS_LOG_DEBUG("WebSocket manager created");
}

WebSocketManager::~WebSocketManager() {
  stop();
  WS_LOG_DEBUG("WebSocket manager destroyed");
}

void WebSocketManager::initializeComponents() {
  // Create connection pool
  connectionPool_ =
      std::make_shared<ConnectionPool>(config_.connectionPoolConfig);

  // Create message broadcaster with connection pool dependency
  messageBroadcaster_ = std::make_shared<MessageBroadcaster>(
      connectionPool_, config_.messageBroadcasterConfig);
}

void WebSocketManager::start() {
  if (running_.load()) {
    WS_LOG_WARN("WebSocket manager already running");
    return;
  }

  running_.store(true);

  if (config_.autoStartComponents) {
    startComponents();
  }

  WS_LOG_INFO("WebSocket manager started");
}

void WebSocketManager::stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);

  stopComponents();

  WS_LOG_INFO("WebSocket manager stopped");
}

bool WebSocketManager::isRunning() const {
  return running_.load() && connectionPool_->isRunning() &&
         messageBroadcaster_->isRunning();
}

void WebSocketManager::startComponents() {
  try {
    connectionPool_->start();
    messageBroadcaster_->start();
    WS_LOG_INFO("WebSocket manager components started successfully");
  } catch (const std::exception &e) {
    WS_LOG_ERROR("Failed to start WebSocket manager components: " +
                 std::string(e.what()));
    throw;
  }
}

void WebSocketManager::stopComponents() {
  try {
    messageBroadcaster_->stop();
    connectionPool_->stop();
    WS_LOG_INFO("WebSocket manager components stopped successfully");
  } catch (const std::exception &e) {
    WS_LOG_ERROR("Error stopping WebSocket manager components: " +
                 std::string(e.what()));
  }
}

void WebSocketManager::handleUpgrade(tcp::socket socket) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, rejecting connection");
    return;
  }

  WS_LOG_DEBUG("Handling WebSocket upgrade request");

  // Create new WebSocket connection
  auto connection = std::make_shared<WebSocketConnection>(
      std::move(socket), std::weak_ptr<WebSocketManager>(shared_from_this()));

  // Start the connection (this will trigger the handshake)
  connection->start();
}

void WebSocketManager::addConnection(
    std::shared_ptr<WebSocketConnection> connection) {
  if (!connection) {
    WS_LOG_ERROR("Attempted to add null WebSocket connection");
    return;
  }

  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot add connection");
    return;
  }

  // Delegate to connection pool
  connectionPool_->addConnection(connection);
}

void WebSocketManager::removeConnection(const std::string &connectionId) {
  if (!running_.load()) {
    return;
  }

  // Delegate to connection pool
  connectionPool_->removeConnection(connectionId);
}

void WebSocketManager::broadcastMessage(const std::string &message) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot broadcast message");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->broadcastMessage(message);
}

void WebSocketManager::sendToConnection(const std::string &connectionId,
                                        const std::string &message) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot send message");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->sendToConnection(connectionId, message);
}

void WebSocketManager::broadcastJobUpdate(const std::string &message,
                                          const std::string &jobId) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot broadcast job update");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->broadcastJobUpdate(message, jobId);
}

void WebSocketManager::broadcastLogMessage(const std::string &message,
                                           const std::string &jobId,
                                           const std::string &logLevel) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot broadcast log message");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->broadcastLogMessage(message, jobId, logLevel);
}

void WebSocketManager::broadcastByMessageType(const std::string &message,
                                              MessageType messageType,
                                              const std::string &jobId) {
  if (!running_.load()) {
    WS_LOG_WARN(
        "WebSocket manager not running, cannot broadcast by message type");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->broadcastByMessageType(message, messageType, jobId);
}

void WebSocketManager::broadcastToFilteredConnections(
    const std::string &message,
    std::function<bool(const ConnectionFilters &)> filterPredicate) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot broadcast to filtered "
                "connections");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->broadcastToFilteredConnections(message, filterPredicate);
}

size_t WebSocketManager::getConnectionCount() const {
  if (!running_.load()) {
    return 0;
  }

  // Delegate to connection pool
  return connectionPool_->getActiveConnectionCount();
}

std::vector<std::string> WebSocketManager::getConnectionIds() const {
  if (!running_.load()) {
    return {};
  }

  // Delegate to connection pool
  return connectionPool_->getConnectionIds();
}

void WebSocketManager::setConnectionFilters(const std::string &connectionId,
                                            const ConnectionFilters &filters) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot set filters");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->setConnectionFilters(connectionId, filters);
}

ConnectionFilters
WebSocketManager::getConnectionFilters(const std::string &connectionId) const {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot get filters");
    return ConnectionFilters{};
  }

  // Delegate to message broadcaster
  return messageBroadcaster_->getConnectionFilters(connectionId);
}

void WebSocketManager::updateConnectionFilters(
    const std::string &connectionId, const ConnectionFilters &filters) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot update filters");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->updateConnectionFilters(connectionId, filters);
}

void WebSocketManager::addJobFilterToConnection(const std::string &connectionId,
                                                const std::string &jobId) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot add job filter");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->addJobFilterToConnection(connectionId, jobId);
}

void WebSocketManager::removeJobFilterFromConnection(
    const std::string &connectionId, const std::string &jobId) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot remove job filter");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->removeJobFilterFromConnection(connectionId, jobId);
}

void WebSocketManager::addMessageTypeFilterToConnection(
    const std::string &connectionId, MessageType messageType) {
  if (!running_.load()) {
    WS_LOG_WARN(
        "WebSocket manager not running, cannot add message type filter");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->addMessageTypeFilterToConnection(connectionId,
                                                        messageType);
}

void WebSocketManager::removeMessageTypeFilterFromConnection(
    const std::string &connectionId, MessageType messageType) {
  if (!running_.load()) {
    WS_LOG_WARN(
        "WebSocket manager not running, cannot remove message type filter");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->removeMessageTypeFilterFromConnection(connectionId,
                                                             messageType);
}

void WebSocketManager::addLogLevelFilterToConnection(
    const std::string &connectionId, const std::string &logLevel) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot add log level filter");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->addLogLevelFilterToConnection(connectionId, logLevel);
}

void WebSocketManager::removeLogLevelFilterFromConnection(
    const std::string &connectionId, const std::string &logLevel) {
  if (!running_.load()) {
    WS_LOG_WARN(
        "WebSocket manager not running, cannot remove log level filter");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->removeLogLevelFilterFromConnection(connectionId,
                                                          logLevel);
}

void WebSocketManager::clearConnectionFilters(const std::string &connectionId) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot clear filters");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->clearConnectionFilters(connectionId);
}

std::vector<std::string>
WebSocketManager::getConnectionsForJob(const std::string &jobId) const {
  if (!running_.load()) {
    return {};
  }

  // Delegate to message broadcaster
  return messageBroadcaster_->getConnectionsForJob(jobId);
}

std::vector<std::string>
WebSocketManager::getConnectionsForMessageType(MessageType messageType) const {
  if (!running_.load()) {
    return {};
  }

  // Delegate to message broadcaster
  return messageBroadcaster_->getConnectionsForMessageType(messageType);
}

std::vector<std::string>
WebSocketManager::getConnectionsForLogLevel(const std::string &logLevel) const {
  if (!running_.load()) {
    return {};
  }

  // Delegate to message broadcaster
  return messageBroadcaster_->getConnectionsForLogLevel(logLevel);
}

size_t WebSocketManager::getFilteredConnectionCount() const {
  if (!running_.load()) {
    return 0;
  }

  // Delegate to message broadcaster
  return messageBroadcaster_->getFilteredConnectionCount();
}

size_t WebSocketManager::getUnfilteredConnectionCount() const {
  if (!running_.load()) {
    return 0;
  }

  // Delegate to message broadcaster
  return messageBroadcaster_->getUnfilteredConnectionCount();
}

void WebSocketManager::broadcastWithAdvancedRouting(
    const WebSocketMessage &message) {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot broadcast with advanced "
                "routing");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->broadcastWithAdvancedRouting(message);
}

void WebSocketManager::sendToMatchingConnections(
    const WebSocketMessage &message,
    std::function<bool(const ConnectionFilters &, const WebSocketMessage &)>
        customMatcher) {
  if (!running_.load()) {
    WS_LOG_WARN(
        "WebSocket manager not running, cannot send to matching connections");
    return;
  }

  // Delegate to message broadcaster
  messageBroadcaster_->sendToMatchingConnections(message, customMatcher);
}

bool WebSocketManager::testConnectionFilter(
    const std::string &connectionId,
    const WebSocketMessage &testMessage) const {
  if (!running_.load()) {
    WS_LOG_WARN("WebSocket manager not running, cannot test filter");
    return false;
  }

  // Delegate to message broadcaster
  return messageBroadcaster_->testConnectionFilter(connectionId, testMessage);
}

void WebSocketManager::updateConfig(const WebSocketManagerConfig &newConfig) {
  config_ = newConfig;

  // Update component configurations
  updateConnectionPoolConfig(config_.connectionPoolConfig);
  updateMessageBroadcasterConfig(config_.messageBroadcasterConfig);

  WS_LOG_INFO("WebSocket manager configuration updated");
}

void WebSocketManager::updateConnectionPoolConfig(
    const ConnectionPoolConfig &newConfig) {
  config_.connectionPoolConfig = newConfig;
  if (connectionPool_) {
    connectionPool_->updateConfig(newConfig);
  }
}

void WebSocketManager::updateMessageBroadcasterConfig(
    const MessageBroadcasterConfig &newConfig) {
  config_.messageBroadcasterConfig = newConfig;
  if (messageBroadcaster_) {
    messageBroadcaster_->updateConfig(newConfig);
  }
}

ConnectionPoolStats WebSocketManager::getConnectionPoolStats() const {
  if (!connectionPool_) {
    return ConnectionPoolStats{};
  }
  return connectionPool_->getStats();
}

MessageBroadcasterStats WebSocketManager::getMessageBroadcasterStats() const {
  if (!messageBroadcaster_) {
    return MessageBroadcasterStats{};
  }
  return messageBroadcaster_->getStats();
}