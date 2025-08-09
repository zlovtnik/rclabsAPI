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

std::string WebSocketConnection::generateConnectionId() {
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    return boost::uuids::to_string(uuid);
}