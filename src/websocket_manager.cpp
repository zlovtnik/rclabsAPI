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
    std::scoped_lock lock(connectionsMutex_);
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

    std::scoped_lock lock(connectionsMutex_);
    connections_[connection->getId()] = connection;
    
    WS_LOG_INFO("WebSocket connection added: " + connection->getId() + 
                " (Total connections: " + std::to_string(connections_.size()) + ")");
}

void WebSocketManager::removeConnection(const std::string& connectionId) {
    std::scoped_lock lock(connectionsMutex_);
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

void WebSocketManager::updateConnectionFilters(const std::string& connectionId, const ConnectionFilters& filters) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->updateFilterPreferences(filters);
        WS_LOG_INFO("Filters updated for connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot update filters for connection (not found or inactive): " + connectionId);
    }
}

void WebSocketManager::addJobFilterToConnection(const std::string& connectionId, const std::string& jobId) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->addJobIdFilter(jobId);
        WS_LOG_DEBUG("Added job filter '" + jobId + "' to connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot add job filter to connection (not found or inactive): " + connectionId);
    }
}

void WebSocketManager::removeJobFilterFromConnection(const std::string& connectionId, const std::string& jobId) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->removeJobIdFilter(jobId);
        WS_LOG_DEBUG("Removed job filter '" + jobId + "' from connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot remove job filter from connection (not found or inactive): " + connectionId);
    }
}

void WebSocketManager::addMessageTypeFilterToConnection(const std::string& connectionId, MessageType messageType) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->addMessageTypeFilter(messageType);
        WS_LOG_DEBUG("Added message type filter '" + messageTypeToString(messageType) + "' to connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot add message type filter to connection (not found or inactive): " + connectionId);
    }
}

void WebSocketManager::removeMessageTypeFilterFromConnection(const std::string& connectionId, MessageType messageType) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->removeMessageTypeFilter(messageType);
        WS_LOG_DEBUG("Removed message type filter '" + messageTypeToString(messageType) + "' from connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot remove message type filter from connection (not found or inactive): " + connectionId);
    }
}

void WebSocketManager::addLogLevelFilterToConnection(const std::string& connectionId, const std::string& logLevel) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->addLogLevelFilter(logLevel);
        WS_LOG_DEBUG("Added log level filter '" + logLevel + "' to connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot add log level filter to connection (not found or inactive): " + connectionId);
    }
}

void WebSocketManager::removeLogLevelFilterFromConnection(const std::string& connectionId, const std::string& logLevel) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->removeLogLevelFilter(logLevel);
        WS_LOG_DEBUG("Removed log level filter '" + logLevel + "' from connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot remove log level filter from connection (not found or inactive): " + connectionId);
    }
}

void WebSocketManager::clearConnectionFilters(const std::string& connectionId) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        it->second->clearFilters();
        WS_LOG_INFO("Cleared all filters for connection: " + connectionId);
    } else {
        WS_LOG_WARN("Cannot clear filters for connection (not found or inactive): " + connectionId);
    }
}

std::vector<std::string> WebSocketManager::getConnectionsForJob(const std::string& jobId) const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    std::vector<std::string> matchingConnections;
    
    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            const auto& filters = connection->getFilters();
            if (filters.shouldReceiveJob(jobId)) {
                matchingConnections.push_back(id);
            }
        }
    }
    
    return matchingConnections;
}

std::vector<std::string> WebSocketManager::getConnectionsForMessageType(MessageType messageType) const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    std::vector<std::string> matchingConnections;
    
    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            const auto& filters = connection->getFilters();
            if (filters.shouldReceiveMessageType(messageType)) {
                matchingConnections.push_back(id);
            }
        }
    }
    
    return matchingConnections;
}

std::vector<std::string> WebSocketManager::getConnectionsForLogLevel(const std::string& logLevel) const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    std::vector<std::string> matchingConnections;
    
    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            const auto& filters = connection->getFilters();
            if (filters.shouldReceiveLogLevel(logLevel)) {
                matchingConnections.push_back(id);
            }
        }
    }
    
    return matchingConnections;
}

size_t WebSocketManager::getFilteredConnectionCount() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    size_t filteredCount = 0;
    
    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            const auto& filters = connection->getFilters();
            // A connection is considered "filtered" if it has any specific filters set
            if (!filters.jobIds.empty() || !filters.messageTypes.empty() || !filters.logLevels.empty()) {
                filteredCount++;
            }
        }
    }
    
    return filteredCount;
}

size_t WebSocketManager::getUnfilteredConnectionCount() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    size_t unfilteredCount = 0;
    
    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            const auto& filters = connection->getFilters();
            // A connection is "unfiltered" if it has no specific filters set (receives all messages)
            if (filters.jobIds.empty() && filters.messageTypes.empty() && filters.logLevels.empty()) {
                unfilteredCount++;
            }
        }
    }
    
    return unfilteredCount;
}

void WebSocketManager::broadcastWithAdvancedRouting(const WebSocketMessage& message) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, cannot broadcast with advanced routing");
        return;
    }
    
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    int sentCount = 0;
    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& connection = it->second;
        if (connection && connection->isOpen()) {
            if (connection->shouldReceiveMessage(message)) {
                connection->send(message.toJson());
                sentCount++;
            }
            ++it;
        } else {
            WS_LOG_DEBUG("Removing inactive connection during advanced routing: " + it->first);
            it = connections_.erase(it);
        }
    }
    
    WS_LOG_DEBUG("Advanced routing message broadcasted to " + std::to_string(sentCount) + " connections");
}

void WebSocketManager::sendToMatchingConnections(const WebSocketMessage& message, 
                                                std::function<bool(const ConnectionFilters&, const WebSocketMessage&)> customMatcher) {
    if (!running_.load()) {
        WS_LOG_WARN("WebSocket manager not running, cannot send to matching connections");
        return;
    }
    
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    int sentCount = 0;
    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& connection = it->second;
        if (connection && connection->isOpen()) {
            if (customMatcher(connection->getFilters(), message)) {
                connection->send(message.toJson());
                sentCount++;
            }
            ++it;
        } else {
            WS_LOG_DEBUG("Removing inactive connection during custom matching: " + it->first);
            it = connections_.erase(it);
        }
    }
    
    WS_LOG_DEBUG("Custom matcher message sent to " + std::to_string(sentCount) + " connections");
}

bool WebSocketManager::testConnectionFilter(const std::string& connectionId, const WebSocketMessage& testMessage) const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(connectionId);
    if (it != connections_.end() && it->second && it->second->isOpen()) {
        return it->second->shouldReceiveMessage(testMessage);
    }
    
    WS_LOG_WARN("Cannot test filter for connection (not found or inactive): " + connectionId);
    return false;
}