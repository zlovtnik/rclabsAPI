#pragma once

#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <chrono>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class RequestHandler;
class WebSocketManager;
class TimeoutManager;

/**
 * PooledSession extends the basic Session functionality with connection pooling support.
 * It includes timeout management, idle state tracking, and reset functionality for connection reuse.
 */
class PooledSession : public std::enable_shared_from_this<PooledSession> {
public:
    /**
     * Constructor
     * @param socket TCP socket for the connection
     * @param handler Request handler for processing HTTP requests
     * @param wsManager WebSocket manager for handling WebSocket upgrades
     * @param timeoutManager Timeout manager for connection and request timeouts
     */
    PooledSession(tcp::socket&& socket, 
                  std::shared_ptr<RequestHandler> handler,
                  std::shared_ptr<WebSocketManager> wsManager,
                  std::shared_ptr<TimeoutManager> timeoutManager);

    /**
     * Destructor - ensures proper cleanup
     */
    ~PooledSession();

    /**
     * Start the session - begins reading requests
     */
    void run();

    /**
     * Reset the session for reuse in connection pooling
     * Clears buffers, resets state, and prepares for new connection
     */
    void reset();

    /**
     * Check if the session is currently idle
     * @return true if session is idle and can be reused
     */
    bool isIdle() const;

    /**
     * Get the timestamp of the last activity
     * @return Time point of last activity
     */
    std::chrono::steady_clock::time_point getLastActivity() const;

    /**
     * Mark the session as idle
     */
    void setIdle(bool idle);

    /**
     * Update the last activity timestamp to current time
     */
    void updateLastActivity();

    /**
     * Get the underlying TCP socket
     * @return Reference to the TCP socket
     */
    tcp::socket& getSocket();

    /**
     * Check if the session is currently processing a request
     * @return true if processing a request
     */
    bool isProcessingRequest() const;

    /**
     * Handle timeout events from TimeoutManager
     * @param timeoutType Type of timeout that occurred
     */
    void handleTimeout(const std::string& timeoutType);

private:
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<RequestHandler> handler_;
    std::shared_ptr<WebSocketManager> wsManager_;
    std::shared_ptr<TimeoutManager> timeoutManager_;
    
    // Pooling and state management
    std::chrono::steady_clock::time_point lastActivity_;
    bool isIdle_;
    bool processingRequest_;
    
    // Session lifecycle methods
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytes_transferred);
    void sendResponse(http::response<http::string_body>&& msg);
    void onWrite(bool close, beast::error_code ec, std::size_t bytes_transferred);
    void doClose();
    
    // Timeout handling
    void startConnectionTimeout();
    void startRequestTimeout();
    void cancelTimeouts();
    
    // State management helpers
    void resetState();
    void clearBuffers();
};