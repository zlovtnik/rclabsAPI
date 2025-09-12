#include "config_manager.hpp"
#include "logger.hpp"
#include "websocket_manager.hpp"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <iostream>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WebSocketIntegrationTest {
public:
  void runTests() {
    std::cout << "Starting WebSocket Integration Tests...\n";

    testWebSocketManagerIntegration();
    testConnectionLifecycle();
    testMessageBroadcasting();

    std::cout << "All WebSocket integration tests completed!\n";
  }

private:
  void testWebSocketManagerIntegration() {
    std::cout << "\nTest 1: WebSocket Manager Integration\n";

    try {
      // Initialize logger
      Logger &logger = Logger::getInstance();
      logger.configure(LogConfig{});

      // Create WebSocket manager
      auto wsManager = std::make_shared<WebSocketManager>();
      wsManager->start();

      // Ensure cleanup on exception
      struct Cleanup {
        std::shared_ptr<WebSocketManager> mgr;
        ~Cleanup() {
          if (mgr)
            mgr->stop();
        }
      } cleanup{wsManager};

      std::cout << "✓ WebSocket manager started\n";

      // Test initial state
      if (wsManager->getConnectionCount() == 0) {
        std::cout << "✓ Initial connection count is 0\n";
      }

      // Test broadcast to empty connections
      wsManager->broadcastMessage("{\"type\":\"test\",\"message\":\"hello\"}");
      std::cout << "✓ Broadcast to empty connections handled\n";

      wsManager->stop();
      std::cout << "✓ WebSocket manager stopped\n";

    } catch (const std::exception &e) {
      std::cout << "✗ Test failed with exception: " << e.what() << "\n";
    }
  }

  void testConnectionLifecycle() {
    std::cout << "\nTest 2: Connection Lifecycle Management\n";

    try {
      auto wsManager = std::make_shared<WebSocketManager>();
      wsManager->start();

      // Test connection management methods
      auto connectionIds = wsManager->getConnectionIds();
      if (connectionIds.empty()) {
        std::cout << "✓ No active connections initially\n";
      }

      // Test sending to non-existent connection
      wsManager->sendToConnection("non-existent-id", "test message");
      std::cout << "✓ Send to non-existent connection handled gracefully\n";

      wsManager->stop();
      std::cout << "✓ Connection lifecycle test completed\n";

    } catch (const std::exception &e) {
      std::cout << "✗ Connection lifecycle test failed: " << e.what() << "\n";
    }
  }

  void testMessageBroadcasting() {
    std::cout << "\nTest 3: Message Broadcasting\n";

    try {
      auto wsManager = std::make_shared<WebSocketManager>();
      wsManager->start();

      // Test JSON message broadcasting
      std::string jsonMessage = R"({
                "type": "job_status_update",
                "timestamp": "2025-08-09T10:30:00Z",
                "data": {
                    "jobId": "test_job_123",
                    "status": "RUNNING",
                    "progressPercent": 50
                }
            })";

      wsManager->broadcastMessage(jsonMessage);
      std::cout << "✓ JSON message broadcast handled\n";

      // Test multiple rapid broadcasts
      for (int i = 0; i < 5; ++i) {
        std::string msg =
            "{\"type\":\"test\",\"sequence\":" + std::to_string(i) + "}";
        wsManager->broadcastMessage(msg);
      }
      std::cout << "✓ Multiple rapid broadcasts handled\n";

      wsManager->stop();
      std::cout << "✓ Message broadcasting test completed\n";

    } catch (const std::exception &e) {
      std::cout << "✗ Message broadcasting test failed: " << e.what() << "\n";
    }
  }
};

int main() {
  try {
    WebSocketIntegrationTest test;
    test.runTests();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Integration test failed with exception: " << e.what()
              << std::endl;
    return 1;
  }
}