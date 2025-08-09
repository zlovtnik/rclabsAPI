#include "websocket_connection.hpp"
#include "websocket_manager.hpp"
#include "logger.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

WebSocketConnection::WebSocketConnection(tcp::socket socket, std::weak_ptr<WebSocketManager> manager)
    : ws_(std::move(socket))
    , manager_(manager)
    , connectionId_(generateConnectionId()) {
    WS_LOG_DEBUG("WebSocket connection created with ID: " + connectionId_);
}

WebSocketConnection::~WebSocketConnection() {
    WS_LOG_DEBUG("WebSocket connection destroyed: " + connectionId_);
}

void WebSocketConnection::start() {
    WS_LOG_DEBUG("Starting WebSocket connection: " + connectionId_);
    
    // Set suggested timeout settings for the websocket
    ws_.set_option(websocket::stream_base::timeout::suggested(
        beast::role_type::server));

    // Set a decorator to change the Server of the handshake
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(beast::http::field::server, "ETL Plus WebSocket Server");
        }));

    // Accept the websocket handshake
    ws_.async_accept(
        beast::bind_front_handler(&WebSocketConnection::onAccept, shared_from_this()));
}

void WebSocketConnection::send(const std::string& message) {
    if (!isOpen_.load()) {
        WS_LOG_WARN("Attempted to send message to closed connection: " + connectionId_);
        return;
    }

    net::post(ws_.get_executor(), [self = shared_from_this(), message]() {
        std::lock_guard<std::mutex> lock(self->queueMutex_);
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
    
    net::post(ws_.get_executor(), [self = shared_from_this()]() {
        self->doClose();
    });
}

void WebSocketConnection::onAccept(beast::error_code ec) {
    if (ec) {
        WS_LOG_ERROR("WebSocket accept failed for connection " + connectionId_ + ": " + ec.message());
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

    ws_.async_read(
        buffer_,
        beast::bind_front_handler(&WebSocketConnection::onRead, shared_from_this()));
}

void WebSocketConnection::onRead(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec == websocket::error::closed) {
        WS_LOG_INFO("WebSocket connection closed by client: " + connectionId_);
        isOpen_.store(false);
        if (auto manager = manager_.lock()) {
            manager->removeConnection(connectionId_);
        }
        return;
    }

    if (ec) {
        WS_LOG_ERROR("WebSocket read error for connection " + connectionId_ + ": " + ec.message());
        isOpen_.store(false);
        if (auto manager = manager_.lock()) {
            manager->removeConnection(connectionId_);
        }
        return;
    }

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

    ws_.async_write(
        net::buffer(message),
        beast::bind_front_handler(&WebSocketConnection::onWrite, shared_from_this()));
}

void WebSocketConnection::onWrite(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        WS_LOG_ERROR("WebSocket write error for connection " + connectionId_ + ": " + ec.message());
        isOpen_.store(false);
        isWriting_.store(false);
        if (auto manager = manager_.lock()) {
            manager->removeConnection(connectionId_);
        }
        return;
    }

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
                WS_LOG_WARN("WebSocket close error for connection " + self->connectionId_ + ": " + ec.message());
            } else {
                WS_LOG_DEBUG("WebSocket connection closed gracefully: " + self->connectionId_);
            }
            
            if (auto manager = self->manager_.lock()) {
                manager->removeConnection(self->connectionId_);
            }
        });
}

void WebSocketConnection::setFilters(const ConnectionFilters& filters) {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    filters_ = filters;
    WS_LOG_DEBUG("Filters updated for connection: " + connectionId_);
}

bool WebSocketConnection::shouldReceiveMessage(MessageType type, const std::string& jobId, const std::string& logLevel) const {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    
    // Use the shouldReceiveMessage method from ConnectionFilters
    WebSocketMessage tempMessage;
    tempMessage.type = type;
    tempMessage.targetJobId = jobId.empty() ? std::nullopt : std::optional<std::string>(jobId);
    tempMessage.targetLevel = logLevel.empty() ? std::nullopt : std::optional<std::string>(logLevel);
    
    return filters_.shouldReceiveMessage(tempMessage);
}

bool WebSocketConnection::shouldReceiveMessage(const WebSocketMessage& message) const {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    return filters_.shouldReceiveMessage(message);
}

void WebSocketConnection::updateFilterPreferences(const ConnectionFilters& newFilters) {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    filters_ = newFilters;
    WS_LOG_INFO("Filter preferences updated for connection: " + connectionId_);
}

void WebSocketConnection::addJobIdFilter(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    if (std::find(filters_.jobIds.begin(), filters_.jobIds.end(), jobId) == filters_.jobIds.end()) {
        filters_.jobIds.push_back(jobId);
        WS_LOG_DEBUG("Added job ID filter '" + jobId + "' to connection: " + connectionId_);
    }
}

void WebSocketConnection::removeJobIdFilter(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    auto it = std::find(filters_.jobIds.begin(), filters_.jobIds.end(), jobId);
    if (it != filters_.jobIds.end()) {
        filters_.jobIds.erase(it);
        WS_LOG_DEBUG("Removed job ID filter '" + jobId + "' from connection: " + connectionId_);
    }
}

void WebSocketConnection::addMessageTypeFilter(MessageType messageType) {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    if (std::find(filters_.messageTypes.begin(), filters_.messageTypes.end(), messageType) == filters_.messageTypes.end()) {
        filters_.messageTypes.push_back(messageType);
        WS_LOG_DEBUG("Added message type filter '" + messageTypeToString(messageType) + "' to connection: " + connectionId_);
    }
}

void WebSocketConnection::removeMessageTypeFilter(MessageType messageType) {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    auto it = std::find(filters_.messageTypes.begin(), filters_.messageTypes.end(), messageType);
    if (it != filters_.messageTypes.end()) {
        filters_.messageTypes.erase(it);
        WS_LOG_DEBUG("Removed message type filter '" + messageTypeToString(messageType) + "' from connection: " + connectionId_);
    }
}

void WebSocketConnection::addLogLevelFilter(const std::string& logLevel) {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    if (std::find(filters_.logLevels.begin(), filters_.logLevels.end(), logLevel) == filters_.logLevels.end()) {
        filters_.logLevels.push_back(logLevel);
        WS_LOG_DEBUG("Added log level filter '" + logLevel + "' to connection: " + connectionId_);
    }
}

void WebSocketConnection::removeLogLevelFilter(const std::string& logLevel) {
    std::lock_guard<std::mutex> lock(filtersMutex_);
    auto it = std::find(filters_.logLevels.begin(), filters_.logLevels.end(), logLevel);
    if (it != filters_.logLevels.end()) {
        filters_.logLevels.erase(it);
        WS_LOG_DEBUG("Removed log level filter '" + logLevel + "' from connection: " + connectionId_);
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

std::vector<MessageType> WebSocketConnection::getActiveMessageTypeFilters() const {
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