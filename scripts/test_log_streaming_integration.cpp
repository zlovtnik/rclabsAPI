#include "../include/job_monitoring_models.hpp"
#include "../include/logger.hpp"
#include "../include/websocket_manager.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

// Mock WebSocket Manager for testing real streaming
class MockWebSocketManager : public WebSocketManager {
public:
  MockWebSocketManager() : messageCount_(0) {}

  void broadcastLogMessage(const std::string &message, const std::string &jobId,
                           const std::string &logLevel) {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    receivedMessages_.push_back({message, jobId, logLevel});
    messageCount_++;
    std::cout << "Mock WebSocket received log: jobId=" << jobId
              << ", level=" << logLevel << std::endl;
  }

  struct ReceivedMessage {
    std::string message;
    std::string jobId;
    std::string logLevel;
  };

  std::vector<ReceivedMessage> getReceivedMessages() const {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    return receivedMessages_;
  }

  void clearMessages() {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    receivedMessages_.clear();
    messageCount_ = 0;
  }

  size_t getMessageCount() const { return messageCount_.load(); }

private:
  mutable std::mutex messagesMutex_;
  std::vector<ReceivedMessage> receivedMessages_;
  std::atomic<size_t> messageCount_;
};

void testRealTimeStreaming() {
  std::cout << "\n=== Testing Real-time Log Streaming ===" << std::endl;

  auto mockWsManager = std::make_shared<MockWebSocketManager>();
  Logger &logger = Logger::getInstance();

  // Configure logger for streaming
  LogConfig config;
  config.enableRealTimeStreaming = true;
  config.streamingQueueSize = 100;
  config.streamAllLevels = true;
  config.consoleOutput = false;
  config.fileOutput = false;

  logger.configure(config);
  logger.setWebSocketManager(mockWsManager);

  // Give streaming thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Test job-specific logging
  logger.infoForJob("TestComponent", "Test message for job", "job_123");
  logger.errorForJob("TestComponent", "Error message for job", "job_456");

  // Give time for messages to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Verify messages were received
  auto messages = mockWsManager->getReceivedMessages();
  std::cout << "Received " << messages.size() << " messages" << std::endl;

  bool foundInfoMessage = false;
  bool foundErrorMessage = false;

  for (const auto &msg : messages) {
    std::cout << "Message: jobId=" << msg.jobId << ", level=" << msg.logLevel
              << std::endl;
    if (msg.jobId == "job_123" &&
        msg.logLevel.find("INFO") != std::string::npos) {
      foundInfoMessage = true;
    }
    if (msg.jobId == "job_456" &&
        msg.logLevel.find("ERROR") != std::string::npos) {
      foundErrorMessage = true;
    }
  }

  assert(foundInfoMessage);
  assert(foundErrorMessage);

  // Disable streaming
  logger.enableRealTimeStreaming(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "✓ Real-time log streaming test passed" << std::endl;
}

void testStreamingWithFiltering() {
  std::cout << "\n=== Testing Streaming with Job Filtering ===" << std::endl;

  auto mockWsManager = std::make_shared<MockWebSocketManager>();
  Logger &logger = Logger::getInstance();

  mockWsManager->clearMessages();

  // Configure logger with job filtering
  LogConfig config;
  config.enableRealTimeStreaming = true;
  config.streamingQueueSize = 100;
  config.streamAllLevels = true;
  config.consoleOutput = false;
  config.fileOutput = false;
  config.streamingJobFilter = {"allowed_job"}; // Only allow this job

  logger.configure(config);
  logger.setWebSocketManager(mockWsManager);

  // Give streaming thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Test filtering - only allowed_job should be streamed
  logger.infoForJob("TestComponent", "Message for allowed job", "allowed_job");
  logger.infoForJob("TestComponent", "Message for filtered job",
                    "filtered_job");

  // Give time for messages to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  auto messages = mockWsManager->getReceivedMessages();
  std::cout << "Received " << messages.size() << " messages with filtering"
            << std::endl;

  // Should only have messages for allowed_job
  for (const auto &msg : messages) {
    std::cout << "Filtered message: jobId=" << msg.jobId << std::endl;
    assert(msg.jobId == "allowed_job");
  }

  // Should have at least one message for allowed_job
  assert(messages.size() >= 1);

  // Disable streaming
  logger.enableRealTimeStreaming(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "✓ Streaming with job filtering test passed" << std::endl;
}

void testStreamingPerformance() {
  std::cout << "\n=== Testing Streaming Performance ===" << std::endl;

  auto mockWsManager = std::make_shared<MockWebSocketManager>();
  Logger &logger = Logger::getInstance();

  mockWsManager->clearMessages();

  // Configure logger for performance test
  LogConfig config;
  config.enableRealTimeStreaming = true;
  config.streamingQueueSize = 1000;
  config.streamAllLevels = true;
  config.consoleOutput = false;
  config.fileOutput = false;

  logger.configure(config);
  logger.setWebSocketManager(mockWsManager);
  logger.clearStreamingJobFilter(); // Allow all jobs

  // Give streaming thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Generate many log messages quickly
  const int messageCount = 100;
  auto startTime = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < messageCount; i++) {
    logger.infoForJob("TestComponent",
                      "Performance test message " + std::to_string(i),
                      "perf_job");
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      endTime - startTime);

  std::cout << "Generated " << messageCount << " log messages in "
            << duration.count() << "ms" << std::endl;

  // Give time for all messages to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  size_t receivedCount = mockWsManager->getMessageCount();
  std::cout << "Received " << receivedCount << " messages via WebSocket"
            << std::endl;

  // Should receive most or all messages (allowing for some timing variations)
  assert(receivedCount >= messageCount * 0.8); // At least 80% of messages

  // Disable streaming
  logger.enableRealTimeStreaming(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "✓ Streaming performance test passed" << std::endl;
}

int main() {
  std::cout << "Starting Logger Real-time Streaming Integration Tests..."
            << std::endl;

  try {
    testRealTimeStreaming();
    testStreamingWithFiltering();
    testStreamingPerformance();

    std::cout << "\n🎉 All log streaming integration tests passed!"
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "❌ Test failed with unknown exception" << std::endl;
    return 1;
  }

  // Clean shutdown
  Logger::getInstance().shutdown();

  return 0;
}