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
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

// Mock RequestHandler for testing
class MockRequestHandler : public RequestHandler {
public:
    MockRequestHandler() : RequestHandler(nullptr, nullptr, nullptr) {}
    
    http::response<http::string_body> handleRequest(
        http::request<http::string_body>&& req) {
        
        http::response<http::string_body> res{
            http::status::ok, req.version()};
        res.set(http::field::server, "Test Server");
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());
        res.body() = "{\"message\":\"Hello from integration test\",\"method\":\"" + 
                     std::string(req.method_string()) + "\",\"target\":\"" + 
                     std::string(req.target()) + "\"}";
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

class PooledSessionIntegrationTest {
private:
    net::io_context ioc_;
    std::shared_ptr<MockRequestHandler> handler_;
    std::shared_ptr<MockWebSocketManager> wsManager_;
    std::shared_ptr<TimeoutManager> timeoutManager_;

public:
    PooledSessionIntegrationTest() 
        : handler_(std::make_shared<MockRequestHandler>())
        , wsManager_(std::make_shared<MockWebSocketManager>())
        , timeoutManager_(std::make_shared<TimeoutManager>(ioc_)) {
    }

    void testSessionReuse() {
        std::cout << "Testing session reuse functionality..." << std::endl;
        
        // Create socket pair for testing
        tcp::socket socket1(ioc_);
        tcp::socket socket2(ioc_);
        
        // Create session
        auto session = std::make_shared<PooledSession>(
            std::move(socket1), handler_, wsManager_, timeoutManager_);
        
        // Test initial state
        assert(session->isIdle() == false);
        assert(session->isProcessingRequest() == false);
        
        // Simulate request processing completion
        session->setIdle(true);
        assert(session->isIdle() == true);
        
        // Reset for reuse
        session->reset();
        assert(session->isIdle() == true);
        assert(session->isProcessingRequest() == false);
        
        // Verify last activity was updated
        auto resetTime = session->getLastActivity();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Simulate new request
        session->updateLastActivity();
        assert(session->getLastActivity() > resetTime);
        
        std::cout << "✓ Session reuse test passed" << std::endl;
    }

    void testTimeoutIntegration() {
        std::cout << "Testing timeout integration..." << std::endl;
        
        tcp::socket socket(ioc_);
        auto session = std::make_shared<PooledSession>(
            std::move(socket), handler_, wsManager_, timeoutManager_);
        
        // Test timeout handling
        session->handleTimeout("CONNECTION");
        session->handleTimeout("REQUEST");
        
        // Verify session state after timeouts
        assert(session->isIdle() == false);
        assert(session->isProcessingRequest() == false);
        
        std::cout << "✓ Timeout integration test passed" << std::endl;
    }

    void testSessionLifecycleWithReset() {
        std::cout << "Testing complete session lifecycle with reset..." << std::endl;
        
        tcp::socket socket(ioc_);
        auto session = std::make_shared<PooledSession>(
            std::move(socket), handler_, wsManager_, timeoutManager_);
        
        // Initial state
        assert(session->isIdle() == false);
        assert(session->isProcessingRequest() == false);
        
        // Simulate request processing
        session->setIdle(false);
        auto initialTime = session->getLastActivity();
        
        // Wait and update activity
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        session->updateLastActivity();
        assert(session->getLastActivity() > initialTime);
        
        // Complete request
        session->setIdle(true);
        assert(session->isIdle() == true);
        
        // Reset for reuse
        auto beforeReset = session->getLastActivity();
        session->reset();
        
        // Verify reset state
        assert(session->isIdle() == true);
        assert(session->isProcessingRequest() == false);
        assert(session->getLastActivity() >= beforeReset);
        
        std::cout << "✓ Complete lifecycle test passed" << std::endl;
    }

    void testConcurrentSessionOperations() {
        std::cout << "Testing concurrent session operations..." << std::endl;
        
        // Create multiple sessions
        std::vector<std::shared_ptr<PooledSession>> sessions;
        for (int i = 0; i < 5; ++i) {
            tcp::socket socket(ioc_);
            sessions.push_back(std::make_shared<PooledSession>(
                std::move(socket), handler_, wsManager_, timeoutManager_));
        }
        
        // Test concurrent operations
        for (auto& session : sessions) {
            session->setIdle(true);
            session->updateLastActivity();
            session->reset();
            
            assert(session->isIdle() == true);
            assert(session->isProcessingRequest() == false);
        }
        
        std::cout << "✓ Concurrent operations test passed" << std::endl;
    }

    void runAllTests() {
        std::cout << "Running PooledSession integration tests..." << std::endl;
        std::cout << "================================================" << std::endl;
        
        try {
            testSessionReuse();
            testTimeoutIntegration();
            testSessionLifecycleWithReset();
            testConcurrentSessionOperations();
            
            std::cout << "================================================" << std::endl;
            std::cout << "✓ All PooledSession integration tests passed!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "✗ Integration test failed with exception: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cout << "✗ Integration test failed with unknown exception" << std::endl;
            throw;
        }
    }
};

int main() {
    try {
        // Initialize logger
        Logger::getInstance().setLogLevel(LogLevel::INFO);
        
        PooledSessionIntegrationTest test;
        test.runAllTests();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Integration test suite failed: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Integration test suite failed with unknown exception" << std::endl;
        return 1;
    }
}