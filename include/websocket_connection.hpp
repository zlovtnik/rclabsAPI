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
#include <unordered_set>
#include <vector>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Forward declaration
class WebSocketManager;

// Message types for filtering
enum class MessageType {
    JOB_STATUS_UPDATE,
    JOB_PROGRESS_UPDATE,
    LOG_MESSAGE,
    NOTIFICATION,
    SYSTEM_MESSAGE
};

// Connection filters for selective message delivery
struct ConnectionFilters {
    std::unordered_set<std::string> jobIds;           // Filter by specific job IDs
    std::unordered_set<MessageType> messageTypes;     // Filter by message types
    std::unordered_set<std::string> logLevels;        // Filter by log levels (DEBUG, INFO, WARN, ERROR)
    bool receiveAllJobs = true;                       // If true, receive updates for all jobs
    bool receiveAllMessageTypes = true;               // If true, receive all message types
    bool receiveAllLogLevels = true;                  // If true, receive all log levels
};

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
    
    // Connection filtering methods
    void setFilters(const ConnectionFilters& filters);
    const ConnectionFilters& getFilters() const { return filters_; }
    bool shouldReceiveMessage(MessageType type, const std::string& jobId = "", const std::string& logLevel = "") const;

private:
    websocket::stream<tcp::socket> ws_;
    std::weak_ptr<WebSocketManager> manager_;
    std::string connectionId_;
    beast::flat_buffer buffer_;
    std::queue<std::string> messageQueue_;
    std::mutex queueMutex_;
    std::atomic<bool> isOpen_{false};
    std::atomic<bool> isWriting_{false};
    ConnectionFilters filters_;
    mutable std::mutex filtersMutex_;

    void onAccept(beast::error_code ec);
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytes_transferred);
    void doWrite();
    void onWrite(beast::error_code ec, std::size_t bytes_transferred);
    void doClose();
    
    std::string generateConnectionId();
};