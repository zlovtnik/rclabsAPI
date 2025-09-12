#include "websocket_connection.hpp"
#include "logger.hpp"
#include "websocket_manager.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

WebSocketConnection::WebSocketConnection(
    tcp::socket socket, std::weak_ptr<WebSocketManager> manager)
    : ws_(std::move(socket)), manager_(manager),
      connectionId_(generateConnectionId()),
      circuitBreaker_(5, std::chrono::seconds(60), 3) {

  // Initialize recovery config with defaults
  recoveryConfig_.enableAutoReconnect = true;
  recoveryConfig_.maxReconnectAttempts = 5;
  recoveryConfig_.baseReconnectDelay = std::chrono::milliseconds(1000);
  recoveryConfig_.maxReconnectDelay = std::chrono::milliseconds(30000);
  recoveryConfig_.backoffMultiplier = 2.0;
  recoveryConfig_.messageQueueMaxSize = 1000;
  recoveryConfig_.connectionTimeout = std::chrono::seconds(30);
  recoveryConfig_.heartbeatInterval = std::chrono::seconds(30);
  recoveryConfig_.enableHeartbeat = true;
  recoveryConfig_.maxMissedHeartbeats = 3;

  // Initialize heartbeat timer
  auto executor = ws_.get_executor();
  heartbeatTimer_ = std::make_unique<boost::asio::steady_timer>(executor);

  WS_LOG_DEBUG("WebSocket connection created with ID: " + connectionId_);
}

WebSocketConnection::~WebSocketConnection() {
  stopHeartbeat();
  WS_LOG_DEBUG("WebSocket connection destroyed: " + connectionId_);
}

void WebSocketConnection::start() {
  WS_LOG_DEBUG("Starting WebSocket connection: " + connectionId_);

  // Set suggested timeout settings for the websocket
  ws_.set_option(
      websocket::stream_base::timeout::suggested(beast::role_type::server));

  // Set a decorator to change the Server of the handshake
  ws_.set_option(
      websocket::stream_base::decorator([](websocket::response_type &res) {
        res.set(beast::http::field::server, "ETL Plus WebSocket Server");
      }));

  // Accept the websocket handshake
  ws_.async_accept(beast::bind_front_handler(&WebSocketConnection::onAccept,
                                             shared_from_this()));
}

void WebSocketConnection::send(const std::string &message) {
  if (!isOpen_.load()) {
    // Queue message for retry if recovery is enabled
    if (recoveryConfig_.enableAutoReconnect &&
        recoveryState_.isRecovering.load()) {
      recoveryState_.addPendingMessage(message, recoveryConfig_);
      WS_LOG_DEBUG("Message queued for retry on connection: " + connectionId_);
    } else {
      WS_LOG_WARN("Attempted to send message to closed connection: " +
                  connectionId_);
    }
    return;
  }

  // Check circuit breaker
  if (!circuitBreaker_.allowOperation()) {
    WS_LOG_WARN("Circuit breaker open, dropping message for connection: " +
                connectionId_);
    return;
  }

  net::post(ws_.get_executor(), [self = shared_from_this(), message]() {
    std::scoped_lock lock(self->queueMutex_);

    // Check queue size limit
    if (self->messageQueue_.size() >=
        static_cast<size_t>(self->recoveryConfig_.messageQueueMaxSize)) {
      WS_LOG_WARN("Message queue full for connection " + self->connectionId_ +
                  ", dropping oldest message");
      self->messageQueue_.pop();
    }

    self->messageQueue_.push(message);

    if (!self->isWriting_.load()) {
      self->doWrite();
    }
  });
}

void WebSocketConnection::close() {
  if (!isOpen_.load()) {
    return;
  }

  WS_LOG_DEBUG("Closing WebSocket connection: " + connectionId_);

  net::post(ws_.get_executor(),
            [self = shared_from_this()]() { self->doClose(); });
}

void WebSocketConnection::onAccept(beast::error_code ec) {
  if (ec) {
    WS_LOG_ERROR("WebSocket accept failed for connection " + connectionId_ +
                 ": " + ec.message());
    return;
  }

  WS_LOG_INFO("WebSocket connection accepted: " + connectionId_);
  isOpen_.store(true);

  // Notify manager that connection is ready
  if (auto manager = manager_.lock()) {
    manager->addConnection(shared_from_this());
  }

  // Start reading messages
  doRead();
}

void WebSocketConnection::doRead() {
  if (!isOpen_.load()) {
    return;
  }

  ws_.async_read(buffer_,
                 beast::bind_front_handler(&WebSocketConnection::onRead,
                                           shared_from_this()));
}

void WebSocketConnection::onRead(beast::error_code ec,
                                 std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec == websocket::error::closed) {
    WS_LOG_INFO("WebSocket connection closed by client: " + connectionId_);
    isOpen_.store(false);
    stopHeartbeat();
    if (auto manager = manager_.lock()) {
      manager->removeConnection(connectionId_);
    }
    return;
  }

  if (ec) {
    handleError("read", ec);
    return;
  }

  // Reset heartbeat on successful message
  onHeartbeatReceived();
  circuitBreaker_.onSuccess();

  // For now, we'll just log received messages
  // In future tasks, we'll implement proper message handling
  std::string message = beast::buffers_to_string(buffer_.data());
  WS_LOG_DEBUG("Received message from " + connectionId_ + ": " + message);

  // Clear the buffer for the next read
  buffer_.consume(buffer_.size());

  // Continue reading
  doRead();
}

void WebSocketConnection::doWrite() {
  if (!isOpen_.load()) {
    return;
  }

  std::lock_guard<std::mutex> lock(queueMutex_);
  if (messageQueue_.empty()) {
    isWriting_.store(false);
    return;
  }

  isWriting_.store(true);
  std::string message = messageQueue_.front();
  messageQueue_.pop();

  // Release the lock before async operation
  lock.~lock_guard();

  ws_.async_write(net::buffer(message),
                  beast::bind_front_handler(&WebSocketConnection::onWrite,
                                            shared_from_this()));
}

void WebSocketConnection::onWrite(beast::error_code ec,
                                  std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec) {
    handleError("write", ec);
    return;
  }

  circuitBreaker_.onSuccess();

  // Continue writing if there are more messages
  doWrite();
}

void WebSocketConnection::doClose() {
  if (!isOpen_.load()) {
    return;
  }

  isOpen_.store(false);

  ws_.async_close(websocket::close_code::normal,
                  [self = shared_from_this()](beast::error_code ec) {
                    if (ec) {
                      WS_LOG_WARN("WebSocket close error for connection " +
                                  self->connectionId_ + ": " + ec.message());
                    } else {
                      WS_LOG_DEBUG("WebSocket connection closed gracefully: " +
                                   self->connectionId_);
                    }

                    if (auto manager = self->manager_.lock()) {
                      manager->removeConnection(self->connectionId_);
                    }
                  });
}

void WebSocketConnection::setFilters(const ConnectionFilters &filters) {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  filters_ = filters;
  WS_LOG_DEBUG("Filters updated for connection: " + connectionId_);
}

bool WebSocketConnection::shouldReceiveMessage(
    MessageType type, const std::string &jobId,
    const std::string &logLevel) const {
  std::lock_guard<std::mutex> lock(filtersMutex_);

  // Use the shouldReceiveMessage method from ConnectionFilters
  WebSocketMessage tempMessage;
  tempMessage.type = type;
  tempMessage.targetJobId =
      jobId.empty() ? std::nullopt : std::optional<std::string>(jobId);
  tempMessage.targetLevel =
      logLevel.empty() ? std::nullopt : std::optional<std::string>(logLevel);

  return filters_.shouldReceiveMessage(tempMessage);
}

bool WebSocketConnection::shouldReceiveMessage(
    const WebSocketMessage &message) const {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  return filters_.shouldReceiveMessage(message);
}

void WebSocketConnection::updateFilterPreferences(
    const ConnectionFilters &newFilters) {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  filters_ = newFilters;
  WS_LOG_INFO("Filter preferences updated for connection: " + connectionId_);
}

void WebSocketConnection::addJobIdFilter(const std::string &jobId) {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  if (std::find(filters_.jobIds.begin(), filters_.jobIds.end(), jobId) ==
      filters_.jobIds.end()) {
    filters_.jobIds.push_back(jobId);
    WS_LOG_DEBUG("Added job ID filter '" + jobId +
                 "' to connection: " + connectionId_);
  }
}

void WebSocketConnection::removeJobIdFilter(const std::string &jobId) {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  auto it = std::find(filters_.jobIds.begin(), filters_.jobIds.end(), jobId);
  if (it != filters_.jobIds.end()) {
    filters_.jobIds.erase(it);
    WS_LOG_DEBUG("Removed job ID filter '" + jobId +
                 "' from connection: " + connectionId_);
  }
}

void WebSocketConnection::addMessageTypeFilter(MessageType messageType) {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  if (std::find(filters_.messageTypes.begin(), filters_.messageTypes.end(),
                messageType) == filters_.messageTypes.end()) {
    filters_.messageTypes.push_back(messageType);
    WS_LOG_DEBUG("Added message type filter '" +
                 messageTypeToString(messageType) +
                 "' to connection: " + connectionId_);
  }
}

void WebSocketConnection::removeMessageTypeFilter(MessageType messageType) {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  auto it = std::find(filters_.messageTypes.begin(),
                      filters_.messageTypes.end(), messageType);
  if (it != filters_.messageTypes.end()) {
    filters_.messageTypes.erase(it);
    WS_LOG_DEBUG("Removed message type filter '" +
                 messageTypeToString(messageType) +
                 "' from connection: " + connectionId_);
  }
}

void WebSocketConnection::addLogLevelFilter(const std::string &logLevel) {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  if (std::find(filters_.logLevels.begin(), filters_.logLevels.end(),
                logLevel) == filters_.logLevels.end()) {
    filters_.logLevels.push_back(logLevel);
    WS_LOG_DEBUG("Added log level filter '" + logLevel +
                 "' to connection: " + connectionId_);
  }
}

void WebSocketConnection::removeLogLevelFilter(const std::string &logLevel) {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  auto it =
      std::find(filters_.logLevels.begin(), filters_.logLevels.end(), logLevel);
  if (it != filters_.logLevels.end()) {
    filters_.logLevels.erase(it);
    WS_LOG_DEBUG("Removed log level filter '" + logLevel +
                 "' from connection: " + connectionId_);
  }
}

void WebSocketConnection::clearFilters() {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  filters_.jobIds.clear();
  filters_.messageTypes.clear();
  filters_.logLevels.clear();
  filters_.includeSystemNotifications = true;
  WS_LOG_INFO("All filters cleared for connection: " + connectionId_);
}

size_t WebSocketConnection::getFilteredJobCount() const {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  return filters_.jobIds.size();
}

size_t WebSocketConnection::getFilteredMessageTypeCount() const {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  return filters_.messageTypes.size();
}

size_t WebSocketConnection::getFilteredLogLevelCount() const {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  return filters_.logLevels.size();
}

std::vector<std::string> WebSocketConnection::getActiveJobFilters() const {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  return filters_.jobIds;
}

std::vector<MessageType>
WebSocketConnection::getActiveMessageTypeFilters() const {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  return filters_.messageTypes;
}

std::vector<std::string> WebSocketConnection::getActiveLogLevelFilters() const {
  std::lock_guard<std::mutex> lock(filtersMutex_);
  return filters_.logLevels;
}

std::string WebSocketConnection::generateConnectionId() {
  boost::uuids::uuid uuid = boost::uuids::random_generator()();
  return boost::uuids::to_string(uuid);
}

// Error handling and recovery methods
bool WebSocketConnection::isHealthy() const {
  if (!isOpen_.load())
    return false;

  // Check circuit breaker state
  if (circuitBreaker_.getState() ==
      websocket_recovery::ConnectionCircuitBreaker::State::OPEN) {
    return false;
  }

  // Check heartbeat status if enabled
  if (recoveryConfig_.enableHeartbeat) {
    std::scoped_lock lock(heartbeatMutex_);
    auto now = std::chrono::system_clock::now();
    auto timeSinceLastHeartbeat = now - lastHeartbeat_;
    auto threshold =
        recoveryConfig_.heartbeatInterval * recoveryConfig_.maxMissedHeartbeats;
    return timeSinceLastHeartbeat < threshold;
  }

  return true;
}

void WebSocketConnection::setRecoveryConfig(
    const websocket_recovery::ConnectionRecoveryConfig &config) {
  recoveryConfig_ = config;
  if (recoveryConfig_.enableHeartbeat && isOpen_.load()) {
    startHeartbeat();
  } else if (!recoveryConfig_.enableHeartbeat) {
    stopHeartbeat();
  }
}

void WebSocketConnection::handleError(const std::string &operation,
                                      beast::error_code ec) {
  WS_LOG_ERROR("WebSocket " + operation + " error for connection " +
               connectionId_ + ": " + ec.message());

  circuitBreaker_.onFailure();

  // Call custom error handler if set
  if (errorHandler_) {
    errorHandler_(connectionId_, operation + " error: " + ec.message());
  }

  // Check if we should attempt recovery
  if (shouldAttemptRecovery(ec)) {
    attemptRecovery();
  } else {
    // Close connection permanently
    isOpen_.store(false);
    isWriting_.store(false);
    stopHeartbeat();

    if (auto manager = manager_.lock()) {
      manager->removeConnection(connectionId_);
    }
  }
}

bool WebSocketConnection::shouldAttemptRecovery(beast::error_code ec) {
  // Don't attempt recovery for certain error conditions
  if (ec == websocket::error::closed)
    return false;
  if (ec == boost::asio::error::operation_aborted)
    return false;
  if (ec == boost::asio::error::connection_refused)
    return false;

  // Check if circuit breaker allows recovery
  if (!circuitBreaker_.allowOperation())
    return false;

  // Check recovery state
  return recoveryState_.shouldAttemptReconnect(recoveryConfig_);
}

void WebSocketConnection::attemptRecovery() {
  if (recoveryState_.isRecovering.load()) {
    WS_LOG_DEBUG("Recovery already in progress for connection: " +
                 connectionId_);
    return;
  }

  recoveryState_.isRecovering.store(true);
  recoveryState_.reconnectAttempts++;
  recoveryState_.lastReconnectAttempt = std::chrono::system_clock::now();

  WS_LOG_INFO("Attempting recovery for connection " + connectionId_ +
              " (attempt " +
              std::to_string(recoveryState_.reconnectAttempts.load()) + "/" +
              std::to_string(recoveryConfig_.maxReconnectAttempts) + ")");

  // Store pending message before attempting recovery
  // Note: In a real reconnection scenario, we would need the original socket
  // For now, we'll just queue the pending messages and notify the manager
  if (auto manager = manager_.lock()) {
    // In practice, this would trigger a new connection attempt
    // For this implementation, we'll mark as failed and let the manager handle
    // it
    isOpen_.store(false);
    recoveryState_.isRecovering.store(false);
    manager->removeConnection(connectionId_);
  }
}

void WebSocketConnection::sendPendingMessages() {
  auto pendingMessages = recoveryState_.getPendingMessages();

  for (const auto &message : pendingMessages) {
    send(message);
  }

  if (!pendingMessages.empty()) {
    WS_LOG_INFO("Sent " + std::to_string(pendingMessages.size()) +
                " pending messages for connection: " + connectionId_);
  }
}

// Heartbeat methods
void WebSocketConnection::startHeartbeat() {
  if (!recoveryConfig_.enableHeartbeat || heartbeatActive_.load())
    return;

  heartbeatActive_.store(true);
  {
    std::scoped_lock lock(heartbeatMutex_);
    lastHeartbeat_ = std::chrono::system_clock::now();
  }

  scheduleHeartbeat();
  WS_LOG_DEBUG("Heartbeat started for connection: " + connectionId_);
}

void WebSocketConnection::stopHeartbeat() {
  if (!heartbeatActive_.load())
    return;

  heartbeatActive_.store(false);

  if (heartbeatTimer_) {
    heartbeatTimer_->cancel();
  }

  WS_LOG_DEBUG("Heartbeat stopped for connection: " + connectionId_);
}

void WebSocketConnection::onHeartbeatReceived() {
  if (!recoveryConfig_.enableHeartbeat)
    return;

  std::scoped_lock lock(heartbeatMutex_);
  lastHeartbeat_ = std::chrono::system_clock::now();
  recoveryState_.missedHeartbeats.store(0);
}

std::chrono::system_clock::time_point
WebSocketConnection::getLastHeartbeat() const {
  std::scoped_lock lock(heartbeatMutex_);
  return lastHeartbeat_;
}

void WebSocketConnection::scheduleHeartbeat() {
  if (!heartbeatActive_.load() || !heartbeatTimer_)
    return;

  heartbeatTimer_->expires_after(recoveryConfig_.heartbeatInterval);
  heartbeatTimer_->async_wait(
      [self = shared_from_this()](beast::error_code ec) {
        self->onHeartbeatTimer(ec);
      });
}

void WebSocketConnection::onHeartbeatTimer(beast::error_code ec) {
  if (ec == boost::asio::error::operation_aborted) {
    return; // Timer was cancelled
  }

  if (ec) {
    WS_LOG_ERROR("Heartbeat timer error for connection " + connectionId_ +
                 ": " + ec.message());
    return;
  }

  if (!heartbeatActive_.load())
    return;

  checkHeartbeatTimeout();
  sendHeartbeat();
  scheduleHeartbeat();
}

void WebSocketConnection::sendHeartbeat() {
  if (!isOpen_.load())
    return;

  // Send a ping frame
  ws_.async_ping(websocket::ping_data{},
                 [self = shared_from_this()](beast::error_code ec) {
                   if (ec) {
                     WS_LOG_WARN("Heartbeat ping failed for connection " +
                                 self->connectionId_ + ": " + ec.message());
                     self->recoveryState_.missedHeartbeats++;
                   }
                 });
}

void WebSocketConnection::checkHeartbeatTimeout() {
  if (!recoveryConfig_.enableHeartbeat)
    return;

  std::scoped_lock lock(heartbeatMutex_);
  auto now = std::chrono::system_clock::now();
  auto timeSinceLastHeartbeat = now - lastHeartbeat_;

  if (timeSinceLastHeartbeat > recoveryConfig_.heartbeatInterval) {
    recoveryState_.missedHeartbeats++;

    if (recoveryState_.missedHeartbeats.load() >=
        recoveryConfig_.maxMissedHeartbeats) {
      WS_LOG_WARN("Connection " + connectionId_ + " missed " +
                  std::to_string(recoveryState_.missedHeartbeats.load()) +
                  " heartbeats, marking as unhealthy");

      // Trigger error handling
      beast::error_code timeout_ec = boost::asio::error::timed_out;
      const_cast<WebSocketConnection *>(this)->handleError("heartbeat_timeout",
                                                           timeout_ec);
    }
  }
}