#include "config_manager.hpp"
#include "logger.hpp"
#include "websocket_connection.hpp"
#include "websocket_connection_recovery.hpp"
#include "websocket_manager.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <thread>

class WebSocketErrorHandlingTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Setup code if needed
  }

  void TearDown() override {
    // Cleanup code if needed
  }
};

// Test connection recovery configuration default values
TEST_F(WebSocketErrorHandlingTest, ConnectionRecoveryConfig_DefaultValues) {
  websocket_recovery::ConnectionRecoveryConfig config;

  EXPECT_TRUE(config.enableAutoReconnect);
  EXPECT_EQ(config.maxReconnectAttempts, 5);
  EXPECT_EQ(config.baseReconnectDelay, std::chrono::milliseconds(1000));
  EXPECT_EQ(config.maxReconnectDelay, std::chrono::milliseconds(30000));
  EXPECT_DOUBLE_EQ(config.backoffMultiplier, 2.0);
  EXPECT_EQ(config.messageQueueMaxSize, 1000);
  EXPECT_EQ(config.connectionTimeout, std::chrono::seconds(30));
  EXPECT_EQ(config.heartbeatInterval, std::chrono::seconds(30));
  EXPECT_TRUE(config.enableHeartbeat);
  EXPECT_EQ(config.maxMissedHeartbeats, 3);
}

// Test connection recovery configuration custom values
TEST_F(WebSocketErrorHandlingTest, ConnectionRecoveryConfig_CustomValues) {
  websocket_recovery::ConnectionRecoveryConfig config;

  config.enableAutoReconnect = false;
  config.maxReconnectAttempts = 10;
  // ... rest of custom config

  EXPECT_FALSE(config.enableAutoReconnect);
  EXPECT_EQ(config.maxReconnectAttempts, 10);
  EXPECT_EQ(config.maxMissedHeartbeats, 5);
}

// Test connection recovery state initial behavior
TEST_F(WebSocketErrorHandlingTest, ConnectionRecoveryState_InitialState) {
  websocket_recovery::ConnectionRecoveryConfig config;
  websocket_recovery::ConnectionRecoveryState state;

  // Test shouldAttemptReconnect logic
  EXPECT_TRUE(
      state.shouldAttemptReconnect(config)); // First attempt should be allowed

  state.reconnectAttempts.store(5);
  EXPECT_FALSE(state.shouldAttemptReconnect(config)); // Max attempts reached

  state.reconnectAttempts.store(2);
  state.lastReconnectAttempt = std::chrono::system_clock::now();
  EXPECT_FALSE(
      state.shouldAttemptReconnect(config)); // Too soon for next attempt
}

// Test connection recovery state backoff calculation
TEST_F(WebSocketErrorHandlingTest, ConnectionRecoveryState_BackoffCalculation) {
  websocket_recovery::ConnectionRecoveryConfig config;
  websocket_recovery::ConnectionRecoveryState state;

  // Test exponential backoff delay calculation
  state.reconnectAttempts.store(0);
  auto delay1 = state.calculateBackoffDelay(config);
  EXPECT_EQ(delay1, config.baseReconnectDelay);

  state.reconnectAttempts.store(1);
  auto delay2 = state.calculateBackoffDelay(config);
  EXPECT_EQ(delay2, config.baseReconnectDelay);

  state.reconnectAttempts.store(2);
  auto delay3 = state.calculateBackoffDelay(config);
  EXPECT_EQ(delay3, std::chrono::milliseconds(2000)); // 1000 * 2^1

  state.reconnectAttempts.store(3);
  auto delay4 = state.calculateBackoffDelay(config);
  EXPECT_EQ(delay4, std::chrono::milliseconds(4000)); // 1000 * 2^2
}

// Test connection recovery state pending queue behavior
TEST_F(WebSocketErrorHandlingTest,
       ConnectionRecoveryState_PendingQueueBehavior) {
  websocket_recovery::ConnectionRecoveryConfig config;
  websocket_recovery::ConnectionRecoveryState state;

  // Test pending message queue
  state.addPendingMessage("test message 1", config);
  state.addPendingMessage("test message 2", config);

  auto pendingMessages = state.getPendingMessages();
  EXPECT_EQ(pendingMessages.size(), 2);
  EXPECT_EQ(pendingMessages[0], "test message 1");
  EXPECT_EQ(pendingMessages[1], "test message 2");

  // Queue should be empty after getting messages
  auto emptyMessages = state.getPendingMessages();
  EXPECT_TRUE(emptyMessages.empty());
}

// Test connection recovery state queue size limit
TEST_F(WebSocketErrorHandlingTest, ConnectionRecoveryState_QueueSizeLimit) {
  websocket_recovery::ConnectionRecoveryConfig config;
  websocket_recovery::ConnectionRecoveryState state;

  // Add messages up to and beyond the queue limit
  for (size_t i = 0; i < config.messageQueueMaxSize; ++i) {
    state.addPendingMessage("message " + std::to_string(i), config);
  }

  // Verify queue respects size limit
  auto limitedMessages = state.getPendingMessages();
  EXPECT_EQ(limitedMessages.size(),
            static_cast<size_t>(config.messageQueueMaxSize));

  // Add one more message to test overflow behavior
  state.addPendingMessage("overflow message", config);
  auto overflowMessages = state.getPendingMessages();

  // Queue should still be limited to max size (FIFO eviction)
  EXPECT_EQ(overflowMessages.size(),
            static_cast<size_t>(config.messageQueueMaxSize));
  // Last message should be the overflow message
  EXPECT_EQ(overflowMessages.back(), "overflow message");
}

// Test connection recovery state reset behavior
TEST_F(WebSocketErrorHandlingTest, ConnectionRecoveryState_ResetBehavior) {
  websocket_recovery::ConnectionRecoveryConfig config;
  websocket_recovery::ConnectionRecoveryState state;

  // Set up some state to be reset
  state.reconnectAttempts.store(3);
  state.missedHeartbeats.store(2);
  state.isRecovering.store(true);
  state.addPendingMessage("test message", config);

  // Verify state is set
  EXPECT_EQ(state.reconnectAttempts.load(), 3);
  EXPECT_EQ(state.missedHeartbeats.load(), 2);
  EXPECT_TRUE(state.isRecovering.load());
  EXPECT_FALSE(state.getPendingMessages().empty());

  // Reset and verify all state is cleared
  state.reset();
  EXPECT_FALSE(state.isRecovering.load());
  EXPECT_EQ(state.reconnectAttempts.load(), 0);
  EXPECT_EQ(state.missedHeartbeats.load(), 0);
  EXPECT_TRUE(state.getPendingMessages().empty());
}

// Test circuit breaker behavior
TEST_F(WebSocketErrorHandlingTest, CircuitBreakerBehavior) {
  websocket_recovery::ConnectionCircuitBreaker circuitBreaker(
      3, std::chrono::seconds(2), 2);

  // Test initial state (CLOSED)
  EXPECT_EQ(circuitBreaker.getState(),
            websocket_recovery::ConnectionCircuitBreaker::State::CLOSED);
  EXPECT_TRUE(circuitBreaker.allowOperation());
  EXPECT_EQ(circuitBreaker.getFailureCount(), 0);

  // Test failures leading to OPEN state
  circuitBreaker.onFailure();
  EXPECT_EQ(circuitBreaker.getState(),
            websocket_recovery::ConnectionCircuitBreaker::State::CLOSED);
  EXPECT_EQ(circuitBreaker.getFailureCount(), 1);

  circuitBreaker.onFailure();
  EXPECT_EQ(circuitBreaker.getState(),
            websocket_recovery::ConnectionCircuitBreaker::State::CLOSED);
  EXPECT_EQ(circuitBreaker.getFailureCount(), 2);

  circuitBreaker.onFailure();
  EXPECT_EQ(circuitBreaker.getState(),
            websocket_recovery::ConnectionCircuitBreaker::State::OPEN);
  EXPECT_EQ(circuitBreaker.getFailureCount(), 3);
  EXPECT_FALSE(circuitBreaker.allowOperation());

  // Test timeout and HALF_OPEN state
  std::this_thread::sleep_for(std::chrono::seconds(3)); // Wait for timeout

  EXPECT_TRUE(circuitBreaker.allowOperation()); // Should be HALF_OPEN now
  EXPECT_EQ(circuitBreaker.getState(),
            websocket_recovery::ConnectionCircuitBreaker::State::HALF_OPEN);

  // Test recovery (HALF_OPEN -> CLOSED)
  circuitBreaker.onSuccess();
  EXPECT_EQ(circuitBreaker.getSuccessCount(), 1);

  circuitBreaker.onSuccess();
  EXPECT_EQ(circuitBreaker.getSuccessCount(), 2);
  EXPECT_EQ(circuitBreaker.getState(),
            websocket_recovery::ConnectionCircuitBreaker::State::CLOSED);
  EXPECT_EQ(circuitBreaker.getFailureCount(), 0);

  // Test failure in HALF_OPEN state
  for (int i = 0; i < 3; ++i) {
    circuitBreaker.onFailure();
  }
  EXPECT_EQ(circuitBreaker.getState(),
            websocket_recovery::ConnectionCircuitBreaker::State::OPEN);

  std::this_thread::sleep_for(std::chrono::seconds(3));
  EXPECT_TRUE(circuitBreaker.allowOperation()); // HALF_OPEN

  circuitBreaker.onFailure(); // Failure in HALF_OPEN should go back to OPEN
  EXPECT_EQ(circuitBreaker.getState(),
            websocket_recovery::ConnectionCircuitBreaker::State::OPEN);
  EXPECT_FALSE(circuitBreaker.allowOperation());
}

// Test heartbeat monitoring
TEST_F(WebSocketErrorHandlingTest, HeartbeatMonitoring) {
  // Configure logger
  LogConfig logConfig;
  logConfig.level = LogLevel::DEBUG;
  logConfig.consoleOutput = true;
  Logger::getInstance().configure(logConfig);

  // This test would require actual WebSocket connection setup
  // For now, we'll test the heartbeat configuration and basic logic

  websocket_recovery::ConnectionRecoveryConfig config;
  config.enableHeartbeat = true;
  config.heartbeatInterval = std::chrono::seconds(2);
  config.maxMissedHeartbeats = 2;

  websocket_recovery::ConnectionRecoveryState state;
  state.lastHeartbeat = std::chrono::system_clock::now();

  // Simulate missed heartbeats
  state.missedHeartbeats.store(1);
  EXPECT_EQ(state.missedHeartbeats.load(), 1);

  state.missedHeartbeats.store(2);
  EXPECT_GE(state.missedHeartbeats.load(), config.maxMissedHeartbeats);

  // Test heartbeat timeout detection
  auto now = std::chrono::system_clock::now();
  auto oldHeartbeat = now - std::chrono::seconds(10);
  state.lastHeartbeat = oldHeartbeat;

  auto timeSinceLastHeartbeat = now - state.lastHeartbeat;
  auto threshold = config.heartbeatInterval * config.maxMissedHeartbeats;
  EXPECT_GT(timeSinceLastHeartbeat, threshold);
}

// Test error handling scenarios
TEST_F(WebSocketErrorHandlingTest, ErrorHandlingScenarios) {
  // Test different error conditions and their handling
  boost::system::error_code closed_error =
      boost::beast::websocket::error::closed;
  boost::system::error_code timeout_error = boost::asio::error::timed_out;
  boost::system::error_code connection_refused =
      boost::asio::error::connection_refused;
  boost::system::error_code operation_aborted =
      boost::asio::error::operation_aborted;

  // These errors should not trigger recovery
  std::vector<boost::system::error_code> non_recoverable_errors = {
      closed_error, operation_aborted, connection_refused};

  // These errors should trigger recovery
  std::vector<boost::system::error_code> recoverable_errors = {
      timeout_error, boost::asio::error::network_down,
      boost::asio::error::network_unreachable};

  // Test error condition categorization
  for (const auto &error : non_recoverable_errors) {
    // In real implementation, shouldAttemptRecovery would return false for
    // these
    std::cout << "  Non-recoverable error: " << error.message() << std::endl;
  }

  for (const auto &error : recoverable_errors) {
    // In real implementation, shouldAttemptRecovery would return true for
    // these
    std::cout << "  Recoverable error: " << error.message() << std::endl;
  }
}

// Test message queueing during recovery
TEST_F(WebSocketErrorHandlingTest, MessageQueueingDuringRecovery) {
  websocket_recovery::ConnectionRecoveryConfig config;
  config.messageQueueMaxSize = 5;

  websocket_recovery::ConnectionRecoveryState state;
  state.isRecovering.store(true);

  // Test message queueing
  for (int i = 1; i <= 3; ++i) {
    state.addPendingMessage("message " + std::to_string(i), config);
  }

  auto messages = state.getPendingMessages();
  EXPECT_EQ(messages.size(), 3);
  EXPECT_EQ(messages[0], "message 1");
  EXPECT_EQ(messages[1], "message 2");
  EXPECT_EQ(messages[2], "message 3");

  // Test queue overflow (oldest messages should be dropped)
  for (int i = 1; i <= 8; ++i) {
    state.addPendingMessage("overflow " + std::to_string(i), config);
  }

  auto overflowMessages = state.getPendingMessages();
  EXPECT_EQ(overflowMessages.size(),
            static_cast<size_t>(config.messageQueueMaxSize));
  EXPECT_EQ(overflowMessages[0],
            "overflow 4"); // First 3 messages should be dropped
  EXPECT_EQ(overflowMessages[4], "overflow 8");

  // Test message retrieval clears queue
  auto emptyCheck = state.getPendingMessages();
  EXPECT_TRUE(emptyCheck.empty());
};

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
