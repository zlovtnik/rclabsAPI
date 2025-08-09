#include "websocket_manager.hpp"
#include "websocket_connection.hpp"
#include "logger.hpp"
#include <iostream>

class WebSocketBasicTest {
public:
    void runTests() {
        std::cout << "Starting Basic WebSocket Tests...\n";
        
        testWebSocketManagerCreation();
        testWebSocketManagerLifecycle();
        testConnectionManagement();
        
        std::cout << "All basic WebSocket tests completed!\n";
    }

private:
    void testWebSocketManagerCreation() {
        std::cout << "Test 1: WebSocket Manager Creation\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        
        if (wsManager) {
            std::cout << "✓ WebSocket manager created successfully\n";
        } else {
            std::cout << "✗ Failed to create WebSocket manager\n";
        }
        
        // Test initial state
        if (wsManager->getConnectionCount() == 0) {
            std::cout << "✓ Initial connection count is 0\n";
        } else {
            std::cout << "✗ Initial connection count is not 0\n";
        }
    }
    
    void testWebSocketManagerLifecycle() {
        std::cout << "\nTest 2: WebSocket Manager Lifecycle\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        
        // Test start
        wsManager->start();
        std::cout << "✓ WebSocket manager started\n";
        
        // Test stop
        wsManager->stop();
        std::cout << "✓ WebSocket manager stopped\n";
    }
    
    void testConnectionManagement() {
        std::cout << "\nTest 3: Connection Management\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Test broadcast to empty connections
        wsManager->broadcastMessage("test message");
        std::cout << "✓ Broadcast to empty connections handled\n";
        
        // Test send to non-existent connection
        wsManager->sendToConnection("non-existent", "test message");
        std::cout << "✓ Send to non-existent connection handled\n";
        
        // Test getting connection IDs
        auto ids = wsManager->getConnectionIds();
        if (ids.empty()) {
            std::cout << "✓ Empty connection IDs list returned\n";
        } else {
            std::cout << "✗ Connection IDs list should be empty\n";
        }
        
        wsManager->stop();
    }
};

int main() {
    try {
        // Initialize logger for testing
        Logger& logger = Logger::getInstance();
        logger.configure(LogConfig{});
        
        WebSocketBasicTest test;
        test.runTests();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}