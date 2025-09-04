#pragma once

#include "websocket_connection.hpp"
#include "lock_utils.hpp"
#include <boost/asio/io_context.hpp>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <functional>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WebSocketManager : public std::enable_shared_from_this<WebSocketManager> {
public:
    WebSocketManager();
    ~WebSocketManager();

    void start();
    void stop();
    
    void handleUpgrade(tcp::socket socket);
    void addConnection(std::shared_ptr<WebSocketConnection> connection);
    void removeConnection(const std::string& connectionId);
    
    void broadcastMessage(const std::string& message);
    void sendToConnection(const std::string& connectionId, const std::string& message);
    
    // Enhanced broadcasting with filtering
    void broadcastJobUpdate(const std::string& message, const std::string& jobId);
    void broadcastLogMessage(const std::string& message, const std::string& jobId, const std::string& logLevel);
    void broadcastByMessageType(const std::string& message, MessageType messageType, const std::string& jobId = "");
    void broadcastToFilteredConnections(const std::string& message, 
                                      std::function<bool(const ConnectionFilters&)> filterPredicate);
    
    size_t getConnectionCount() const;
    std::vector<std::string> getConnectionIds() const;
    
    // Connection filter management
    void setConnectionFilters(const std::string& connectionId, const ConnectionFilters& filters);
    ConnectionFilters getConnectionFilters(const std::string& connectionId) const;
    void updateConnectionFilters(const std::string& connectionId, const ConnectionFilters& filters);
    
    // Enhanced filter management
    void addJobFilterToConnection(const std::string& connectionId, const std::string& jobId);
    void removeJobFilterFromConnection(const std::string& connectionId, const std::string& jobId);
    void addMessageTypeFilterToConnection(const std::string& connectionId, MessageType messageType);
    void removeMessageTypeFilterFromConnection(const std::string& connectionId, MessageType messageType);
    void addLogLevelFilterToConnection(const std::string& connectionId, const std::string& logLevel);
    void removeLogLevelFilterFromConnection(const std::string& connectionId, const std::string& logLevel);
    void clearConnectionFilters(const std::string& connectionId);
    
    // Connection analysis and statistics
    std::vector<std::string> getConnectionsForJob(const std::string& jobId) const;
    std::vector<std::string> getConnectionsForMessageType(MessageType messageType) const;
    std::vector<std::string> getConnectionsForLogLevel(const std::string& logLevel) const;
    size_t getFilteredConnectionCount() const;
    size_t getUnfilteredConnectionCount() const;
    
    // Advanced message routing
    void broadcastWithAdvancedRouting(const WebSocketMessage& message);
    void sendToMatchingConnections(const WebSocketMessage& message, 
                                 std::function<bool(const ConnectionFilters&, const WebSocketMessage&)> customMatcher);
    bool testConnectionFilter(const std::string& connectionId, const WebSocketMessage& testMessage) const;

private:
    mutable etl_plus::ContainerMutex connectionsMutex_;
    std::unordered_map<std::string, std::shared_ptr<WebSocketConnection>> connections_;
    std::atomic<bool> running_{false};
};