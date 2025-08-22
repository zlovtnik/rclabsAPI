#include "pooled_session.hpp"
#include "request_handler.hpp"
#include "websocket_manager.hpp"
#include "timeout_manager.hpp"
#include "performance_monitor.hpp"
#include "logger.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

PooledSession::PooledSession(tcp::socket&& socket, 
                             std::shared_ptr<RequestHandler> handler,
                             std::shared_ptr<WebSocketManager> wsManager,
                             std::shared_ptr<TimeoutManager> timeoutManager,
                             std::shared_ptr<PerformanceMonitor> performanceMonitor)
    : stream_(std::move(socket))
    , handler_(handler)
    , wsManager_(wsManager)
    , timeoutManager_(timeoutManager)
    , performanceMonitor_(performanceMonitor)
    , lastActivity_(std::chrono::steady_clock::now())
    , requestStartTime_(std::chrono::steady_clock::now())
    , isIdle_(false)
    , processingRequest_(false) {
    
    HTTP_LOG_DEBUG("PooledSession created with handler: " + std::string(handler ? "valid" : "null") + 
                  ", WebSocket manager: " + std::string(wsManager ? "valid" : "null") +
                  ", Timeout manager: " + std::string(timeoutManager ? "valid" : "null") +
                  ", Performance monitor: " + std::string(performanceMonitor ? "valid" : "null"));
}

PooledSession::~PooledSession() {
    HTTP_LOG_DEBUG("PooledSession destructor - canceling timeouts");
    // Don't call cancelTimeouts() in destructor as it may use shared_from_this()
    // The TimeoutManager will clean up when it's destroyed
}

void PooledSession::run() {
    HTTP_LOG_DEBUG("PooledSession::run() - Starting session");
    updateLastActivity();
    setIdle(false);
    
    // Start connection timeout
    startConnectionTimeout();
    
    net::dispatch(stream_.get_executor(),
                 beast::bind_front_handler(&PooledSession::doRead, shared_from_this()));
}

void PooledSession::reset() {
    HTTP_LOG_DEBUG("PooledSession::reset() - Resetting session for reuse");
    
    // Cancel any active timeouts
    cancelTimeouts();
    
    // Reset state
    resetState();
    
    // Clear buffers
    clearBuffers();
    
    // Update activity and set as idle
    updateLastActivity();
    setIdle(true);
    
    HTTP_LOG_DEBUG("PooledSession::reset() - Session reset complete");
}

bool PooledSession::isIdle() const {
    return isIdle_ && !processingRequest_;
}

std::chrono::steady_clock::time_point PooledSession::getLastActivity() const {
    return lastActivity_;
}

void PooledSession::setIdle(bool idle) {
    isIdle_ = idle;
    if (idle) {
        updateLastActivity();
    }
}

void PooledSession::updateLastActivity() {
    lastActivity_ = std::chrono::steady_clock::now();
}

tcp::socket& PooledSession::getSocket() {
    return stream_.socket();
}

bool PooledSession::isProcessingRequest() const {
    return processingRequest_;
}

void PooledSession::handleTimeout(const std::string& timeoutType) {
    HTTP_LOG_WARN("PooledSession::handleTimeout() - Timeout occurred: " + timeoutType);
    
    // Record timeout for performance monitoring
    if (performanceMonitor_) {
        if (timeoutType == "CONNECTION") {
            performanceMonitor_->recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
        } else if (timeoutType == "REQUEST") {
            performanceMonitor_->recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
        }
    }
    
    if (timeoutType == "CONNECTION") {
        HTTP_LOG_INFO("PooledSession::handleTimeout() - Connection timeout, closing session");
    } else if (timeoutType == "REQUEST") {
        HTTP_LOG_INFO("PooledSession::handleTimeout() - Request timeout, sending timeout response");
        
        // Send HTTP 408 Request Timeout response
        http::response<http::string_body> timeout_res{http::status::request_timeout, req_.version()};
        timeout_res.set(http::field::server, "ETL Plus Backend");
        timeout_res.set(http::field::content_type, "application/json");
        timeout_res.keep_alive(false);
        timeout_res.body() = "{\"error\":\"Request timeout\"}";
        timeout_res.prepare_payload();
        
        try {
            sendResponse(std::move(timeout_res));
        } catch (const std::exception& e) {
            HTTP_LOG_ERROR("PooledSession::handleTimeout() - Error sending timeout response: " + std::string(e.what()));
            doClose();
        }
        return;
    }
    
    // For connection timeout or any other timeout, close the connection
    doClose();
}

void PooledSession::doRead() {
    HTTP_LOG_DEBUG("PooledSession::doRead() - Starting read operation");
    updateLastActivity();
    processingRequest_ = true;
    
    // Record request start for performance monitoring
    requestStartTime_ = std::chrono::steady_clock::now();
    if (performanceMonitor_) {
        performanceMonitor_->recordRequestStart();
    }
    
    // Optimize memory allocation: reuse existing request object instead of creating new one
    req_.clear();
    stream_.expires_after(std::chrono::seconds(30));

    // Start request timeout
    startRequestTimeout();

    // Optimize buffer management: reserve capacity if buffer is too small
    if (buffer_.capacity() < 8192) { // 8KB minimum capacity
        buffer_.reserve(8192);
    }
    
    http::async_read(stream_, buffer_, req_,
        beast::bind_front_handler(&PooledSession::onRead, shared_from_this()));
}

void PooledSession::onRead(beast::error_code ec, std::size_t bytes_transferred) {
    HTTP_LOG_DEBUG("PooledSession::onRead() - Read completed, bytes: " + std::to_string(bytes_transferred));
    boost::ignore_unused(bytes_transferred);
    
    updateLastActivity();

    if (ec == http::error::end_of_stream) {
        HTTP_LOG_DEBUG("PooledSession::onRead() - End of stream, closing");
        processingRequest_ = false;
        return doClose();
    }

    if (ec) {
        HTTP_LOG_ERROR("PooledSession::onRead() - Error: " + ec.message());
        processingRequest_ = false;
        return;
    }

    HTTP_LOG_INFO("PooledSession::onRead() - Processing request: " + std::string(req_.method_string()) + " " + std::string(req_.target()));
    
    // Check if this is a WebSocket upgrade request
    if (beast::websocket::is_upgrade(req_)) {
        HTTP_LOG_INFO("PooledSession::onRead() - WebSocket upgrade request detected");
        
        if (!wsManager_) {
            HTTP_LOG_ERROR("PooledSession::onRead() - WebSocket manager not available for upgrade");
            http::response<http::string_body> error_res{http::status::service_unavailable, req_.version()};
            error_res.set(http::field::server, "ETL Plus Backend");
            error_res.set(http::field::content_type, "application/json");
            error_res.keep_alive(false);
            error_res.body() = "{\"error\":\"WebSocket service not available\"}";
            error_res.prepare_payload();
            sendResponse(std::move(error_res));
            return;
        }
        
        // Cancel timeouts before handing off to WebSocket manager
        cancelTimeouts();
        processingRequest_ = false;
        
        // Release the socket from the HTTP stream and pass it to WebSocket manager
        tcp::socket socket = stream_.release_socket();
        wsManager_->handleUpgrade(std::move(socket));
        return;
    }
    
    if (!handler_) {
        HTTP_LOG_ERROR("PooledSession::onRead() - Handler is null!");
        // Send a proper error response instead of just returning
        http::response<http::string_body> error_res{http::status::internal_server_error, req_.version()};
        error_res.set(http::field::server, "ETL Plus Backend");
        error_res.set(http::field::content_type, "application/json");
        error_res.keep_alive(false);
        error_res.body() = "{\"error\":\"Internal server error - handler not available\"}";
        error_res.prepare_payload();
        sendResponse(std::move(error_res));
        return;
    }
    
    try {
        HTTP_LOG_DEBUG("PooledSession::onRead() - Calling handler->handleRequest()");
        auto response = handler_->handleRequest(std::move(req_));
        HTTP_LOG_DEBUG("PooledSession::onRead() - Handler completed, sending response");
        sendResponse(std::move(response));
    } catch (const std::exception& e) {
        HTTP_LOG_ERROR("PooledSession::onRead() - Exception in handler: " + std::string(e.what()));
        // Send proper error response for exceptions
        http::response<http::string_body> error_res{http::status::internal_server_error, req_.version()};
        error_res.set(http::field::server, "ETL Plus Backend");
        error_res.set(http::field::content_type, "application/json");
        error_res.keep_alive(false);
        error_res.body() = "{\"error\":\"Internal server error\"}";
        error_res.prepare_payload();
        sendResponse(std::move(error_res));
    } catch (...) {
        HTTP_LOG_ERROR("PooledSession::onRead() - Unknown exception in handler");
        // Send proper error response for unknown exceptions
        http::response<http::string_body> error_res{http::status::internal_server_error, req_.version()};
        error_res.set(http::field::server, "ETL Plus Backend");
        error_res.set(http::field::content_type, "application/json");
        error_res.keep_alive(false);
        error_res.body() = "{\"error\":\"Internal server error\"}";
        error_res.prepare_payload();
        sendResponse(std::move(error_res));
    }
}

void PooledSession::sendResponse(http::response<http::string_body>&& msg) {
    HTTP_LOG_DEBUG("PooledSession::sendResponse() - Sending response with status: " + std::to_string(msg.result_int()));
    HTTP_LOG_DEBUG("PooledSession::sendResponse() - Response body size: " + std::to_string(msg.body().size()));
    
    updateLastActivity();
    
    try {
        // Optimize memory allocation: use move semantics and avoid unnecessary shared_ptr allocation
        // for small responses that can be sent immediately
        const bool needsSharedPtr = msg.body().size() > 4096; // 4KB threshold
        
        if (needsSharedPtr) {
            HTTP_LOG_DEBUG("PooledSession::sendResponse() - Creating shared response object for large response");
            auto response = std::make_shared<http::response<http::string_body>>(std::move(msg));
            
            // Ensure the session stays alive for the duration of the write operation
            auto self = shared_from_this();
            
            http::async_write(stream_, *response,
                [self, response](beast::error_code ec, std::size_t bytes_transferred) {
                    self->onWrite(response->need_eof(), ec, bytes_transferred);
                });
        } else {
            HTTP_LOG_DEBUG("PooledSession::sendResponse() - Using direct write for small response");
            bool close = msg.need_eof();
            auto self = shared_from_this();
            
            http::async_write(stream_, msg,
                [self, close](beast::error_code ec, std::size_t bytes_transferred) {
                    self->onWrite(close, ec, bytes_transferred);
                });
        }
        
        HTTP_LOG_DEBUG("PooledSession::sendResponse() - http::async_write called successfully");
    } catch (const std::exception& e) {
        HTTP_LOG_ERROR("PooledSession::sendResponse() - Exception: " + std::string(e.what()));
        processingRequest_ = false;
        // Close the connection on error
        doClose();
    } catch (...) {
        HTTP_LOG_ERROR("PooledSession::sendResponse() - Unknown exception occurred");
        processingRequest_ = false;
        // Close the connection on unknown error
        doClose();
    }
}

void PooledSession::onWrite(bool close, beast::error_code ec, std::size_t bytes_transferred) {
    HTTP_LOG_DEBUG("PooledSession::onWrite() - Write completed, bytes: " + std::to_string(bytes_transferred) + ", close: " + (close ? "true" : "false"));
    boost::ignore_unused(bytes_transferred);
    
    updateLastActivity();
    processingRequest_ = false;

    // Record request completion for performance monitoring
    if (performanceMonitor_) {
        auto requestDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - requestStartTime_);
        performanceMonitor_->recordRequestEnd(requestDuration);
    }

    if (ec) {
        HTTP_LOG_ERROR("PooledSession::onWrite() - Error: " + ec.message());
        return;
    }

    if (close) {
        HTTP_LOG_DEBUG("PooledSession::onWrite() - Closing connection");
        return doClose();
    }

    // Instead of immediately reading again, mark as idle for potential reuse
    HTTP_LOG_DEBUG("PooledSession::onWrite() - Request completed, marking as idle");
    setIdle(true);
    
    // Cancel request timeout since request is complete
    if (timeoutManager_) {
        timeoutManager_->cancelRequestTimeout(shared_from_this());
    }
}

void PooledSession::doClose() {
    HTTP_LOG_DEBUG("PooledSession::doClose() - Closing session");
    
    // Cancel all timeouts
    cancelTimeouts();
    
    // Mark as not idle and not processing
    isIdle_ = false;
    processingRequest_ = false;
    
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    if (ec) {
        HTTP_LOG_WARN("PooledSession::doClose() - Shutdown error: " + ec.message());
    }
}

void PooledSession::startConnectionTimeout() {
    if (timeoutManager_) {
        HTTP_LOG_DEBUG("PooledSession::startConnectionTimeout() - Starting connection timeout");
        timeoutManager_->startConnectionTimeout(shared_from_this());
    }
}

void PooledSession::startRequestTimeout() {
    if (timeoutManager_) {
        HTTP_LOG_DEBUG("PooledSession::startRequestTimeout() - Starting request timeout");
        timeoutManager_->startRequestTimeout(shared_from_this());
    }
}

void PooledSession::cancelTimeouts() {
    if (timeoutManager_) {
        HTTP_LOG_DEBUG("PooledSession::cancelTimeouts() - Canceling all timeouts");
        timeoutManager_->cancelTimeouts(shared_from_this());
    }
}

void PooledSession::resetState() {
    processingRequest_ = false;
    isIdle_ = false;
}

void PooledSession::clearBuffers() {
    // Optimize memory management: don't shrink buffer capacity unless it's too large
    // This avoids frequent reallocations
    if (buffer_.capacity() > 64 * 1024) { // 64KB threshold
        buffer_ = beast::flat_buffer{}; // Reset to default capacity
    } else {
        buffer_.clear(); // Keep existing capacity
    }
    
    // Clear the request object but preserve any allocated capacity
    req_.clear();
}