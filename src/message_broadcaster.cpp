#include "message_broadcaster.hpp"
#include "etl_exceptions.hpp"
#include "logger.hpp"
#include <algorithm>
#include <boost/asio/io_context.hpp>
#include <thread>

MessageBroadcaster::MessageBroadcaster(
    std::shared_ptr<ConnectionPool> connectionPool)
    : MessageBroadcaster(connectionPool, MessageBroadcasterConfig{}) {}

MessageBroadcaster::MessageBroadcaster(
    std::shared_ptr<ConnectionPool> connectionPool,
    const MessageBroadcasterConfig &config)
    : connectionPool_(connectionPool), config_(config) {

  if (!connectionPool_) {
    throw etl::ETLException(etl::ErrorCode::INVALID_CONNECTION,
                            "Connection pool cannot be null");
  }

  WS_LOG_DEBUG("Message broadcaster created");
}

MessageBroadcaster::~MessageBroadcaster() {
  stop();
  WS_LOG_DEBUG("Message broadcaster destroyed");
}

void MessageBroadcaster::start() {
  if (running_.load()) {
    WS_LOG_WARN("Message broadcaster already running");
    return;
  }

  running_.store(true);

  if (config_.enableAsyncProcessing) {
    startAsyncProcessing();
  }

  WS_LOG_INFO("Message broadcaster started");
}

void MessageBroadcaster::stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);

  // Stop async processing
  stopAsyncProcessing();

  // Clear queue
  clearQueue();

  WS_LOG_INFO("Message broadcaster stopped");
}

void MessageBroadcaster::broadcastMessage(const std::string &message) {
  if (!running_.load()) {
    WS_LOG_WARN("Message broadcaster not running, cannot broadcast message");
    return;
  }

  if (config_.enableAsyncProcessing && !isQueueFull()) {
    // Queue for async processing
    QueuedMessage queuedMsg{message, MessageType::SYSTEM_NOTIFICATION, "", "",
                            std::chrono::system_clock::now()};
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      messageQueue_.push(queuedMsg);
      stats_.totalMessagesQueued++;
      stats_.currentQueueSize = messageQueue_.size();
    }
    queueCondition_.notify_one();
    WS_LOG_DEBUG("Message queued for broadcast to all connections");
  } else {
    // Process immediately
    auto connections = connectionPool_->getActiveConnections();
    broadcastToConnections(message, connections);
    updateStats(connections.size());
  }
}

void MessageBroadcaster::sendToConnection(const std::string &connectionId,
                                          const std::string &message) {
  if (!running_.load()) {
    WS_LOG_WARN("Message broadcaster not running, cannot send message");
    return;
  }

  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    sendMessageToConnection(connection, message);
    updateStats(1);
    WS_LOG_DEBUG("Message sent to connection: " + connectionId);
  } else {
    WS_LOG_WARN("Connection not found or inactive: " + connectionId);
  }
}

void MessageBroadcaster::broadcastJobUpdate(const std::string &message,
                                            const std::string &jobId) {
  if (!running_.load()) {
    WS_LOG_WARN("Message broadcaster not running, cannot broadcast job update");
    return;
  }

  broadcastByMessageType(message, MessageType::JOB_STATUS_UPDATE, jobId);
  WS_LOG_DEBUG("Job update broadcasted for job: " + jobId);
}

void MessageBroadcaster::broadcastLogMessage(const std::string &message,
                                             const std::string &jobId,
                                             const std::string &logLevel) {
  if (!running_.load()) {
    WS_LOG_WARN(
        "Message broadcaster not running, cannot broadcast log message");
    return;
  }

  if (config_.enableAsyncProcessing && !isQueueFull()) {
    // Queue for async processing
    QueuedMessage queuedMsg{message, MessageType::JOB_LOG_MESSAGE, jobId,
                            logLevel, std::chrono::system_clock::now()};
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      messageQueue_.push(queuedMsg);
      stats_.totalMessagesQueued++;
      stats_.currentQueueSize = messageQueue_.size();
    }
    queueCondition_.notify_one();
  } else {
    // Process immediately
    auto connections = connectionPool_->getActiveConnections();
    size_t sentCount = 0;

    for (auto &connection : connections) {
      if (shouldProcessMessage(connection, MessageType::JOB_LOG_MESSAGE, jobId,
                               logLevel)) {
        sendMessageToConnection(connection, message);
        sentCount++;
      }
    }

    updateStats(sentCount);
    WS_LOG_DEBUG("Log message broadcasted to " + std::to_string(sentCount) +
                 " connections (job: " + jobId + ", level: " + logLevel + ")");
  }
}

void MessageBroadcaster::broadcastByMessageType(const std::string &message,
                                                MessageType messageType,
                                                const std::string &jobId) {
  if (!running_.load()) {
    WS_LOG_WARN(
        "Message broadcaster not running, cannot broadcast by message type");
    return;
  }

  if (config_.enableAsyncProcessing && !isQueueFull()) {
    // Queue for async processing
    QueuedMessage queuedMsg{message, messageType, jobId, "",
                            std::chrono::system_clock::now()};
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      messageQueue_.push(queuedMsg);
      stats_.totalMessagesQueued++;
      stats_.currentQueueSize = messageQueue_.size();
    }
    queueCondition_.notify_one();
  } else {
    // Process immediately
    auto connections = connectionPool_->getActiveConnections();
    size_t sentCount = 0;

    for (auto &connection : connections) {
      if (shouldProcessMessage(connection, messageType, jobId)) {
        sendMessageToConnection(connection, message);
        sentCount++;
      }
    }

    updateStats(sentCount);
    WS_LOG_DEBUG("Message broadcasted to " + std::to_string(sentCount) +
                 " connections by type");
  }
}

void MessageBroadcaster::broadcastToFilteredConnections(
    const std::string &message,
    std::function<bool(const ConnectionFilters &)> filterPredicate) {
  if (!running_.load()) {
    WS_LOG_WARN("Message broadcaster not running, cannot broadcast to filtered "
                "connections");
    return;
  }

  auto connections = connectionPool_->getConnectionsByFilter(
      [&filterPredicate](const std::shared_ptr<WebSocketConnection> &conn) {
        return filterPredicate(conn->getFilters());
      });

  broadcastToConnections(message, connections);
  updateStats(connections.size());
  WS_LOG_DEBUG("Message broadcasted to " + std::to_string(connections.size()) +
               " filtered connections");
}

void MessageBroadcaster::broadcastWithAdvancedRouting(
    const WebSocketMessage &message) {
  if (!running_.load()) {
    WS_LOG_WARN("Message broadcaster not running, cannot broadcast with "
                "advanced routing");
    return;
  }

  auto connections = connectionPool_->getActiveConnections();
  size_t sentCount = 0;

  for (auto &connection : connections) {
    if (shouldProcessMessage(connection, message)) {
      sendMessageToConnection(connection, message.toJson());
      sentCount++;
    }
  }

  updateStats(sentCount);
  WS_LOG_DEBUG("Advanced routing message broadcasted to " +
               std::to_string(sentCount) + " connections");
}

void MessageBroadcaster::sendToMatchingConnections(
    const WebSocketMessage &message,
    std::function<bool(const ConnectionFilters &, const WebSocketMessage &)>
        customMatcher) {
  if (!running_.load()) {
    WS_LOG_WARN(
        "Message broadcaster not running, cannot send to matching connections");
    return;
  }

  auto connections = connectionPool_->getActiveConnections();
  size_t sentCount = 0;

  for (auto &connection : connections) {
    if (customMatcher(connection->getFilters(), message)) {
      sendMessageToConnection(connection, message.toJson());
      sentCount++;
    }
  }

  updateStats(sentCount);
  WS_LOG_DEBUG("Custom matcher message sent to " + std::to_string(sentCount) +
               " connections");
}

bool MessageBroadcaster::testConnectionFilter(
    const std::string &connectionId,
    const WebSocketMessage &testMessage) const {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    return shouldProcessMessage(connection, testMessage);
  }

  WS_LOG_WARN("Cannot test filter for connection (not found or inactive): " +
              connectionId);
  return false;
}

void MessageBroadcaster::setConnectionFilters(
    const std::string &connectionId, const ConnectionFilters &filters) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->setFilters(filters);
    WS_LOG_INFO("Filters set for connection: " + connectionId);
  } else {
    WS_LOG_WARN("Cannot set filters for connection (not found or inactive): " +
                connectionId);
  }
}

ConnectionFilters MessageBroadcaster::getConnectionFilters(
    const std::string &connectionId) const {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    return connection->getFilters();
  } else {
    WS_LOG_WARN("Cannot get filters for connection (not found or inactive): " +
                connectionId);
    return ConnectionFilters{};
  }
}

void MessageBroadcaster::updateConnectionFilters(
    const std::string &connectionId, const ConnectionFilters &filters) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->updateFilterPreferences(filters);
    WS_LOG_INFO("Filters updated for connection: " + connectionId);
  } else {
    WS_LOG_WARN(
        "Cannot update filters for connection (not found or inactive): " +
        connectionId);
  }
}

void MessageBroadcaster::addJobFilterToConnection(
    const std::string &connectionId, const std::string &jobId) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->addJobIdFilter(jobId);
    WS_LOG_DEBUG("Added job filter '" + jobId +
                 "' to connection: " + connectionId);
  } else {
    WS_LOG_WARN(
        "Cannot add job filter to connection (not found or inactive): " +
        connectionId);
  }
}

void MessageBroadcaster::removeJobFilterFromConnection(
    const std::string &connectionId, const std::string &jobId) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->removeJobIdFilter(jobId);
    WS_LOG_DEBUG("Removed job filter '" + jobId +
                 "' from connection: " + connectionId);
  } else {
    WS_LOG_WARN(
        "Cannot remove job filter from connection (not found or inactive): " +
        connectionId);
  }
}

void MessageBroadcaster::addMessageTypeFilterToConnection(
    const std::string &connectionId, MessageType messageType) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->addMessageTypeFilter(messageType);
    WS_LOG_DEBUG("Added message type filter '" +
                 messageTypeToString(messageType) +
                 "' to connection: " + connectionId);
  } else {
    WS_LOG_WARN("Cannot add message type filter to connection (not found or "
                "inactive): " +
                connectionId);
  }
}

void MessageBroadcaster::removeMessageTypeFilterFromConnection(
    const std::string &connectionId, MessageType messageType) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->removeMessageTypeFilter(messageType);
    WS_LOG_DEBUG("Removed message type filter '" +
                 messageTypeToString(messageType) +
                 "' from connection: " + connectionId);
  } else {
    WS_LOG_WARN("Cannot remove message type filter from connection (not found "
                "or inactive): " +
                connectionId);
  }
}

void MessageBroadcaster::addLogLevelFilterToConnection(
    const std::string &connectionId, const std::string &logLevel) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->addLogLevelFilter(logLevel);
    WS_LOG_DEBUG("Added log level filter '" + logLevel +
                 "' to connection: " + connectionId);
  } else {
    WS_LOG_WARN(
        "Cannot add log level filter to connection (not found or inactive): " +
        connectionId);
  }
}

void MessageBroadcaster::removeLogLevelFilterFromConnection(
    const std::string &connectionId, const std::string &logLevel) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->removeLogLevelFilter(logLevel);
    WS_LOG_DEBUG("Removed log level filter '" + logLevel +
                 "' from connection: " + connectionId);
  } else {
    WS_LOG_WARN("Cannot remove log level filter from connection (not found or "
                "inactive): " +
                connectionId);
  }
}

void MessageBroadcaster::clearConnectionFilters(
    const std::string &connectionId) {
  auto connection = connectionPool_->getConnection(connectionId);
  if (connection && connection->isOpen()) {
    connection->clearFilters();
    WS_LOG_INFO("Cleared all filters for connection: " + connectionId);
  } else {
    WS_LOG_WARN(
        "Cannot clear filters for connection (not found or inactive): " +
        connectionId);
  }
}

std::vector<std::string>
MessageBroadcaster::getConnectionsForJob(const std::string &jobId) const {
  return connectionPool_->getConnectionIdsByFilter(
      [&jobId](const std::shared_ptr<WebSocketConnection> &conn) {
        return conn->getFilters().shouldReceiveJob(jobId);
      });
}

std::vector<std::string> MessageBroadcaster::getConnectionsForMessageType(
    MessageType messageType) const {
  return connectionPool_->getConnectionIdsByFilter(
      [messageType](const std::shared_ptr<WebSocketConnection> &conn) {
        return conn->getFilters().shouldReceiveMessageType(messageType);
      });
}

std::vector<std::string> MessageBroadcaster::getConnectionsForLogLevel(
    const std::string &logLevel) const {
  return connectionPool_->getConnectionIdsByFilter(
      [&logLevel](const std::shared_ptr<WebSocketConnection> &conn) {
        return conn->getFilters().shouldReceiveLogLevel(logLevel);
      });
}

size_t MessageBroadcaster::getFilteredConnectionCount() const {
  auto connections = connectionPool_->getActiveConnections();
  size_t filteredCount = 0;

  for (const auto &connection : connections) {
    const auto &filters = connection->getFilters();
    if (!filters.jobIds.empty() || !filters.messageTypes.empty() ||
        !filters.logLevels.empty()) {
      filteredCount++;
    }
  }

  return filteredCount;
}

size_t MessageBroadcaster::getUnfilteredConnectionCount() const {
  auto connections = connectionPool_->getActiveConnections();
  size_t unfilteredCount = 0;

  for (const auto &connection : connections) {
    const auto &filters = connection->getFilters();
    if (filters.jobIds.empty() && filters.messageTypes.empty() &&
        filters.logLevels.empty()) {
      unfilteredCount++;
    }
  }

  return unfilteredCount;
}

MessageBroadcasterStats MessageBroadcaster::getStats() const {
  std::lock_guard<std::mutex> lock(statsMutex_);
  return stats_;
}

void MessageBroadcaster::updateConfig(
    const MessageBroadcasterConfig &newConfig) {
  std::lock_guard<std::mutex> lock(configMutex_);
  config_ = newConfig;
  WS_LOG_INFO("Message broadcaster configuration updated");
}

const MessageBroadcasterConfig MessageBroadcaster::getConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return config_;
}

void MessageBroadcaster::flushQueue() {
  if (!config_.enableAsyncProcessing) {
    return;
  }

  std::lock_guard<std::mutex> lock(queueMutex_);
  while (!messageQueue_.empty()) {
    // Process messages in batches
    std::vector<QueuedMessage> batch;
    size_t batchSize =
        std::min(static_cast<size_t>(config_.batchSize), messageQueue_.size());

    for (size_t i = 0; i < batchSize; ++i) {
      batch.push_back(messageQueue_.top());
      messageQueue_.pop();
    }

    // Process batch (unlock during processing)
    queueMutex_.unlock();
    for (const auto &msg : batch) {
      processQueuedMessage(msg);
    }
    queueMutex_.lock();
  }

  stats_.currentQueueSize = messageQueue_.size();
}

void MessageBroadcaster::clearQueue() {
  std::lock_guard<std::mutex> lock(queueMutex_);
  size_t clearedCount = messageQueue_.size();
  while (!messageQueue_.empty()) {
    messageQueue_.pop();
  }
  stats_.currentQueueSize = 0;
  stats_.totalMessagesDropped += clearedCount;

  if (clearedCount > 0) {
    WS_LOG_INFO("Cleared " + std::to_string(clearedCount) +
                " messages from queue");
  }
}

bool MessageBroadcaster::isQueueFull() const {
  std::lock_guard<std::mutex> lock(queueMutex_);
  return messageQueue_.size() >= config_.maxQueueSize;
}

size_t MessageBroadcaster::getQueueSize() const {
  std::lock_guard<std::mutex> lock(queueMutex_);
  return messageQueue_.size();
}

void MessageBroadcaster::processMessageQueue() {
  while (running_.load()) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    queueCondition_.wait_for(lock, config_.processingInterval, [this]() {
      return !messageQueue_.empty() || !running_.load();
    });

    if (!running_.load()) {
      break;
    }

    // Process messages in batches
    std::vector<QueuedMessage> batch;
    size_t batchSize =
        std::min(static_cast<size_t>(config_.batchSize), messageQueue_.size());

    for (size_t i = 0; i < batchSize; ++i) {
      if (messageQueue_.empty())
        break;
      batch.push_back(messageQueue_.top());
      messageQueue_.pop();
    }

    stats_.currentQueueSize = messageQueue_.size();
    lock.unlock();

    // Process batch
    for (const auto &msg : batch) {
      processQueuedMessage(msg);
    }
  }
}

void MessageBroadcaster::processQueuedMessage(const QueuedMessage &msg) {
  activeBroadcasts_++;
  try {
    switch (msg.type) {
    case MessageType::SYSTEM_NOTIFICATION:
      broadcastMessage(msg.message);
      break;
    case MessageType::JOB_LOG_MESSAGE:
      broadcastLogMessage(msg.message, msg.jobId, msg.logLevel);
      break;
    default:
      broadcastByMessageType(msg.message, msg.type, msg.jobId);
      break;
    }
  } catch (const std::exception &e) {
    WS_LOG_ERROR("Error processing queued message: " + std::string(e.what()));
  }
  activeBroadcasts_--;
}

void MessageBroadcaster::broadcastToConnections(
    const std::string &message,
    const std::vector<std::shared_ptr<WebSocketConnection>> &connections) {
  for (const auto &connection : connections) {
    if (connection && connection->isOpen()) {
      sendMessageToConnection(connection, message);
    }
  }
}

void MessageBroadcaster::sendMessageToConnection(
    const std::shared_ptr<WebSocketConnection> &connection,
    const std::string &message) {
  try {
    connection->send(message);
  } catch (const std::exception &e) {
    WS_LOG_ERROR("Failed to send message to connection " + connection->getId() +
                 ": " + e.what());
  }
}

void MessageBroadcaster::updateStats(size_t messagesSent,
                                     size_t messagesDropped) {
  std::lock_guard<std::mutex> lock(statsMutex_);
  stats_.totalMessagesSent += messagesSent;
  stats_.totalMessagesDropped += messagesDropped;
  stats_.lastMessageSent = std::chrono::system_clock::now();

  // Calculate messages per second (simple moving average)
  static auto lastUpdate = std::chrono::system_clock::now();
  auto now = std::chrono::system_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate)
          .count();

  if (elapsed > 0) {
    stats_.messagesPerSecond = static_cast<double>(messagesSent) / elapsed;
    lastUpdate = now;
  }
}

bool MessageBroadcaster::shouldProcessMessage(
    const std::shared_ptr<WebSocketConnection> &connection, MessageType type,
    const std::string &jobId, const std::string &logLevel) const {
  return connection->shouldReceiveMessage(type, jobId, logLevel);
}

bool MessageBroadcaster::shouldProcessMessage(
    const std::shared_ptr<WebSocketConnection> &connection,
    const WebSocketMessage &message) const {
  return connection->shouldReceiveMessage(message);
}

void MessageBroadcaster::startAsyncProcessing() {
  size_t threadCount = std::min(config_.maxConcurrentBroadcasts,
                                static_cast<size_t>(4)); // Max 4 threads

  for (size_t i = 0; i < threadCount; ++i) {
    processingThreads_.emplace_back(&MessageBroadcaster::processMessageQueue,
                                    this);
  }

  WS_LOG_INFO("Started " + std::to_string(threadCount) +
              " async processing threads");
}

void MessageBroadcaster::stopAsyncProcessing() {
  // Wait for active broadcasts to complete
  while (activeBroadcasts_.load() > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Join processing threads
  for (auto &thread : processingThreads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  processingThreads_.clear();

  WS_LOG_INFO("Stopped async processing threads");
}

std::string MessageBroadcasterStats::toJson() const {
  return "{"
         "\"totalMessagesSent\":" +
         std::to_string(totalMessagesSent) +
         ","
         "\"totalMessagesQueued\":" +
         std::to_string(totalMessagesQueued) +
         ","
         "\"totalMessagesDropped\":" +
         std::to_string(totalMessagesDropped) +
         ","
         "\"currentQueueSize\":" +
         std::to_string(currentQueueSize) +
         ","
         "\"activeBroadcasts\":" +
         std::to_string(activeBroadcasts) +
         ","
         "\"lastMessageSent\":\"" +
         formatTimestamp(lastMessageSent) +
         "\","
         "\"messagesPerSecond\":" +
         std::to_string(messagesPerSecond) + "}";
}
