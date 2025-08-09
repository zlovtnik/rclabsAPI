#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <functional>
#include <atomic>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Forward declaration
class WebSocketManager;

class WebSocketConnection : public std::enable_shared_from_this<WebSocketConnection> {
public:
    using MessageHandler = std::function<void(const std::string&)>;
    using CloseHandler = std::function<void(const std::string&)>;

    WebSocketConnection(tcp::socket socket, std::weak_ptr<WebSocketManager> manager);
    ~WebSocketConnection();

    void start();
    void send(const std::string& message);
    void close();
    
    const std::string& getId() const { return connectionId_; }
    bool isOpen() const { return isOpen_.load(); }

private:
    websocket::stream<tcp::socket> ws_;
    std::weak_ptr<WebSocketManager> manager_;
    std::string connectionId_;
    beast::flat_buffer buffer_;
    std::queue<std::string> messageQueue_;
    std::mutex queueMutex_;
    std::atomic<bool> isOpen_{false};
    std::atomic<bool> isWriting_{false};

    void onAccept(beast::error_code ec);
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytes_transferred);
    void doWrite();
    void onWrite(beast::error_code ec, std::size_t bytes_transferred);
    void doClose();
    
    std::string generateConnectionId();
};