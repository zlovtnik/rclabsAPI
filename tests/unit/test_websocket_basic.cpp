#include "logger.hpp"
#include "websocket_connection.hpp"
#include "websocket_manager.hpp"
#include <gtest/gtest.h>

class WebSocketBasicTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Initialize logger for testing
    Logger &logger = Logger::getInstance();
    logger.configure(LogConfig{});
  }
};

TEST_F(WebSocketBasicTest, ManagerCreation) {
  auto wsManager = std::make_shared<WebSocketManager>();

  ASSERT_NE(wsManager, nullptr);
  EXPECT_EQ(wsManager->getConnectionCount(), 0);
}

TEST_F(WebSocketBasicTest, ManagerLifecycle) {
  auto wsManager = std::make_shared<WebSocketManager>();

  EXPECT_NO_THROW(wsManager->start());
  EXPECT_NO_THROW(wsManager->stop());
}

TEST_F(WebSocketBasicTest, ConnectionManagement) {
  auto wsManager = std::make_shared<WebSocketManager>();
  wsManager->start();

  // Test broadcast to empty connections
  EXPECT_NO_THROW(wsManager->broadcastMessage("test message"));

  // Test send to non-existent connection
  EXPECT_NO_THROW(wsManager->sendToConnection("non-existent", "test message"));

  // Test getting connection IDs
  auto ids = wsManager->getConnectionIds();
  EXPECT_TRUE(ids.empty());

  wsManager->stop();
}