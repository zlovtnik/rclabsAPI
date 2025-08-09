#pragma once

#include "websocket_connection.hpp"
#include <boost/asio/io_context.hpp>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

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
    
    size_t getConnectionCount() const;
    std::vector<std::string> getConnectionIds() const;

private:
    mutable std::mutex connectionsMutex_;
    std::unordered_map<std::string, std::shared_ptr<WebSocketConnection>> connections_;
    std::atomic<bool> running_{false};
};