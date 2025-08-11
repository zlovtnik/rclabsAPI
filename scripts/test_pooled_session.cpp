#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cassert>

// Include the headers we need
#include "../include/pooled_session.hpp"
#include "../include/timeout_manager.hpp"
#include "../include/request_handler.hpp"
#include "../include/websocket_manager.hpp"
#include "../include/logger.hpp"

namespace net = boost::asio;
namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

// Mock RequestHandler for testing
class MockRequestHandler : public RequestHandler {
public:
    MockRequestHandler() : RequestHandler(nullptr, nullptr, nullptr) {}
    
    beast::http::response<beast::http::string_body> handleRequest(
        beast::http::request<beast::http::string_body>&& req) {
        
        beast::http::response<beast::http::string_body> res{
            beast::http::status::ok, req.version()};
        res.set(beast::http::field::server, "Test Server");
        res.set(beast::http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());
        res.body() = "{\"message\":\"Hello from mock handler\"}";
        res.prepare_payload();
        return res;
    }
};

// Mock WebSocketManager for testing
class MockWebSocketManager : public WebSocketManager {
public:
    MockWebSocketManager() = default;
    
    void handleUpgrade(tcp::socket socket) {
        // Mock implementation - just close the socket
        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }
};

class PooledSessionTest {
private:
    net::io_context ioc_;
    std::shared_ptr<MockRequestHandler> handler_;
    std::shared_ptr<MockWebSocketManager> wsManager_;
    std::shared_ptr<TimeoutManager> timeoutManager_;

public:
    PooledSessionTest() 
        : handler_(std::make_shared<MockRequestHandler>())
        , wsManager_(std::make_shared<MockWebSocketManager>())
        , timeoutManager_(std::make_shared<TimeoutManager>(ioc_)) {
    }

    void testSessionCreation() {
        std::cout << "Testing PooledSession creation..." << std::endl;
        
        // Create a socket for testing
        tcp::socket socket(ioc_);
        
        // Create session
        auto session = std::make_shared<PooledSession>(
            std::move(socket), handler_, wsManager_, timeoutManager_);
        
        // Test initial state
        assert(session->isIdle() == false);
        assert(session->isProcessingRequest() == false);
        
        std::cout << "✓ Session creation test passed" << std::endl;
    }

    void testSessionReset() {
        std::cout << "Testing PooledSession reset functionality..." << std::endl;
        
        tcp::socket socket(ioc_);
        auto session = std::make_shared<PooledSession>(
            std::move(socket), handler_, wsManager_, timeoutManager_);
        
        // Get initial activity time
        auto initialTime = session->getLastActivity();
        
        // Wait a bit to ensure time difference
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Reset the session
        session->reset();
        
        // Test state after reset
        assert(session->isIdle() == true);
        assert(session->isProcessingRequest() == false);
        assert(session->getLastActivity() > initialTime);
        
        std::cout << "✓ Session reset test passed" << std::endl;
    }

    void testIdleStateTracking() {
        std::cout << "Testing idle state tracking..." << std::endl;
        
        tcp::socket socket(ioc_);
        auto session = std::make_shared<PooledSession>(
            std::move(socket), handler_, wsManager_, timeoutManager_);
        
        // Test initial state
        assert(session->isIdle() == false);
        
        // Set idle
        session->setIdle(true);
        assert(session->isIdle() == true);
        
        // Set not idle
        session->setIdle(false);
        assert(session->isIdle() == false);
        
        std::cout << "✓ Idle state tracking test passed" << std::endl;
    }

    void testLastActivityTracking() {
        std::cout << "Testing last activity tracking..." << std::endl;
        
        tcp::socket socket(ioc_);
        auto session = std::make_shared<PooledSession>(
            std::move(socket), handler_, wsManager_, timeoutManager_);
        
        auto initialTime = session->getLastActivity();
        
        // Wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Update activity
        session->updateLastActivity();
        
        auto updatedTime = session->getLastActivity();
        assert(updatedTime > initialTime);
        
        std::cout << "✓ Last activity tracking test passed" << std::endl;
    }

    void testTimeoutHandling() {
        std::cout << "Testing timeout handling..." << std::endl;
        
        tcp::socket socket(ioc_);
        auto session = std::make_shared<PooledSession>(
            std::move(socket), handler_, wsManager_, timeoutManager_);
        
        // Test connection timeout handling
        session->handleTimeout("CONNECTION");
        
        // Test request timeout handling
        session->handleTimeout("REQUEST");
        
        // Test unknown timeout handling
        session->handleTimeout("UNKNOWN");
        
        std::cout << "✓ Timeout handling test passed" << std::endl;
    }

    void testSessionLifecycle() {
        std::cout << "Testing session lifecycle..." << std::endl;
        
        tcp::socket socket(ioc_);
        auto session = std::make_shared<PooledSession>(
            std::move(socket), handler_, wsManager_, timeoutManager_);
        
        // Test initial state
        assert(session->isIdle() == false);
        assert(session->isProcessingRequest() == false);
        
        // Simulate setting idle after request completion
        session->setIdle(true);
        assert(session->isIdle() == true);
        
        // Reset for reuse
        session->reset();
        assert(session->isIdle() == true);
        assert(session->isProcessingRequest() == false);
        
        std::cout << "✓ Session lifecycle test passed" << std::endl;
    }

    void runAllTests() {
        std::cout << "Running PooledSession unit tests..." << std::endl;
        std::cout << "========================================" << std::endl;
        
        try {
            testSessionCreation();
            testSessionReset();
            testIdleStateTracking();
            testLastActivityTracking();
            testTimeoutHandling();
            testSessionLifecycle();
            
            std::cout << "========================================" << std::endl;
            std::cout << "✓ All PooledSession tests passed!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "✗ Test failed with exception: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cout << "✗ Test failed with unknown exception" << std::endl;
            throw;
        }
    }
};

int main() {
    try {
        // Initialize logger
        Logger::getInstance().setLogLevel(LogLevel::DEBUG);
        
        PooledSessionTest test;
        test.runAllTests();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test suite failed with unknown exception" << std::endl;
        return 1;
    }
}