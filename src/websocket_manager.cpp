#include "websocket_manager.hpp"
#include "logger.hpp"
#include <algorithm>

WebSocketManager::WebSocketManager() {
    WS_LOG_DEBUG("WebSocket manager created");
}

WebSocketManager::~WebSocketManager() {
    stop();
    WS_LOG_DEBUG("WebSocket manager destroyed");
}

void WebSocketManager::start() {
    if (running_.load()) {
        WS_LOG_WARN("WebSocket manager already running");
        return;
    }

    running_.store(true);
    WS_LOG_INFO("WebSocket manager started");
}

void WebSocketManager::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);
    
    // Close all connections
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            connection->close();
        }
    }
    connections_.clear();
    
    WS_LOG_INFO("WebSocket manager stopped");
}

void WebSocketManager::handleUpgrade(tcp::socket socket) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, rejecting connection");
        return;
    }

    WS_LOG_DEBUG("Handling WebSocket upgrade request");
    
    // Create new WebSocket connection
    auto connection = std::make_shared<WebSocketConnection>(
        std::move(socket), 
        weak_from_this()
    );
    
    // Start the connection (this will trigger the handshake)
    connection->start();
}

void WebSocketManager::addConnection(std::shared_ptr<WebSocketConnection> connection) {
    if (!connection) {
        WS_LOG_ERROR("Attempted to add null WebSocket connection");
        return;
    }

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    connections_[connection->getId()] = connection;
    
    WS_LOG_INFO("WebSocket connection added: " + connection->getId() + 
                " (Total connections: " + std::to_string(connections_.size()) + ")");
}

void WebSocketManager::removeConnection(const std::string& connectionId) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end()) {
        connections_.erase(it);
        WS_LOG_INFO("WebSocket connection removed: " + connectionId + 
                    " (Total connections: " + std::to_string(connections_.size()) + ")");
    }
}

void WebSocketManager::broadcastMessage(const std::string& message) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, cannot broadcast message");
        return;
    }

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    WS_LOG_DEBUG("Broadcasting message to " + std::to_string(connections_.size()) + " connections");
    
    // Send message to all active connections
    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& connection = it->second;
        if (connection && connection->isOpen()) {
            connection->send(message);
            ++it;
        } else {
            // Remove inactive connections
            WS_LOG_DEBUG("Removing inactive connection during broadcast: " + it->first);
            it = connections_.erase(it);
        }
    }
}

void WebSocketManager::sendToConnection(const std::string& connectionId, const std::string& message) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, cannot send message");
        return;
    }

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->send(message);
        WS_LOG_DEBUG("Message sent to connection: " + connectionId);
    } else {
        WS_LOG_WARN("Connection not found or inactive: " + connectionId);
    }
}

size_t WebSocketManager::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    return connections_.size();
}

std::vector<std::string> WebSocketManager::getConnectionIds() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    std::vector<std::string> ids;
    ids.reserve(connections_.size());
    
    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            ids.push_back(id);
        }
    }
    
    return ids;
}

void WebSocketManager::broadcastJobUpdate(const std::string& message, const std::string& jobId) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, cannot broadcast job update");
        return;
    }

    broadcastByMessageType(message, MessageType::JOB_STATUS_UPDATE, jobId);
    WS_LOG_DEBUG("Job update broadcasted for job: " + jobId);
}

void WebSocketManager::broadcastLogMessage(const std::string& message, const std::string& jobId, const std::string& logLevel) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, cannot broadcast log message");
        return;
    }

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    int sentCount = 0;
    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& connection = it->second;
        if (connection && connection->isOpen()) {
            if (connection->shouldReceiveMessage(MessageType::JOB_LOG_MESSAGE, jobId, logLevel)) {
                connection->send(message);
                sentCount++;
            }
            ++it;
        } else {
            WS_LOG_DEBUG("Removing inactive connection during log broadcast: " + it->first);
            it = connections_.erase(it);
        }
    }
    
    WS_LOG_DEBUG("Log message broadcasted to " + std::to_string(sentCount) + " connections (job: " + jobId + ", level: " + logLevel + ")");
}

void WebSocketManager::broadcastByMessageType(const std::string& message, MessageType messageType, const std::string& jobId) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, cannot broadcast by message type");
        return;
    }

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    int sentCount = 0;
    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& connection = it->second;
        if (connection && connection->isOpen()) {
            if (connection->shouldReceiveMessage(messageType, jobId)) {
                connection->send(message);
                sentCount++;
            }
            ++it;
        } else {
            WS_LOG_DEBUG("Removing inactive connection during type-filtered broadcast: " + it->first);
            it = connections_.erase(it);
        }
    }
    
    WS_LOG_DEBUG("Message broadcasted to " + std::to_string(sentCount) + " connections by type");
}

void WebSocketManager::broadcastToFilteredConnections(const std::string& message, 
                                                    std::function<bool(const ConnectionFilters&)> filterPredicate) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, cannot broadcast to filtered connections");
        return;
    }

    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    int sentCount = 0;
    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& connection = it->second;
        if (connection && connection->isOpen()) {
            if (filterPredicate(connection->getFilters())) {
                connection->send(message);
                sentCount++;
            }
            ++it;
        } else {
            WS_LOG_DEBUG("Removing inactive connection during filtered broadcast: " + it->first);
            it = connections_.erase(it);
        }
    }
    
    WS_LOG_DEBUG("Message broadcasted to " + std::to_string(sentCount) + " filtered connections");
}

void WebSocketManager::setConnectionFilters(const std::string& connectionId, const ConnectionFilters& filters) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->setFilters(filters);
        WS_LOG_INFO("Filters set for connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot set filters for connection (not found or inactive): " + connectionId);
    }
}

ConnectionFilters WebSocketManager::getConnectionFilters(const std::string& connectionId) const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        return it->second->getFilters();
    } else {
        WS_LOG_WARN("Cannot get filters for connection (not found or inactive): " + connectionId);
        return ConnectionFilters{}; // Return default filters
    }
}