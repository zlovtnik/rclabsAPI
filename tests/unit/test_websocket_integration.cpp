#include "config_manager.hpp"
#include "logger.hpp"
#include "websocket_manager.hpp"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WebSocketIntegrationTest : public ::testing::Test {
protected:
  std::shared_ptr<WebSocketManager> wsManager;
  Logger *logger;

  void SetUp() override {
    // Initialize logger
    logger = &Logger::getInstance();
    logger->configure(LogConfig{});

    // Create WebSocket manager
    wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();
  }

  void TearDown() override {
    if (wsManager) {
      wsManager->stop();
    }
  }
};

TEST_F(WebSocketIntegrationTest, WebSocketManagerIntegration) {
  // Test initial state
  EXPECT_EQ(wsManager->getConnectionCount(), 0);

  // Test broadcast to empty connections
  EXPECT_NO_THROW(
      wsManager->broadcastMessage("{\"type\":\"test\",\"message\":\"hello\"}"));
}

TEST_F(WebSocketIntegrationTest, ConnectionLifecycle) {
  // Test connection management methods
  auto connectionIds = wsManager->getConnectionIds();
  EXPECT_TRUE(connectionIds.empty());

  // Test sending to non-existent connection
  EXPECT_NO_THROW(
      wsManager->sendToConnection("non-existent-id", "test message"));
}

TEST_F(WebSocketIntegrationTest, MessageBroadcasting) {
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

  EXPECT_NO_THROW(wsManager->broadcastMessage(jsonMessage));

  // Test multiple rapid broadcasts
  for (int i = 0; i < 5; ++i) {
    std::string msg =
        "{\"type\":\"test\",\"sequence\":" + std::to_string(i) + "}";
    EXPECT_NO_THROW(wsManager->broadcastMessage(msg));
  }
}