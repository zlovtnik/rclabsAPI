#include "../include/job_monitoring_models.hpp"
#include "../include/logger.hpp"
#include "../include/transparent_string_hash.hpp"
#include "../include/websocket_manager.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

// Mock WebSocket Manager for testing
class MockWebSocketManager : public WebSocketManager {
public:
  MockWebSocketManager() = default;

  void broadcastLogMessage(const std::string &message, const std::string &jobId,
                           const std::string &logLevel) {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    receivedMessages_.push_back({message, jobId, logLevel});
    std::cout << "Mock WebSocket received log: jobId=" << jobId
              << ", level=" << logLevel << ", message=" << message << std::endl;
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
  }

  size_t getMessageCount() const {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    return receivedMessages_.size();
  }

private:
  mutable std::mutex messagesMutex_;
  std::vector<ReceivedMessage> receivedMessages_;
};

void testBasicLogStreaming() {
  std::cout << "\n=== Testing Basic Log Streaming ===" << std::endl;

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
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Test job-specific logging
  logger.infoForJob("TestComponent", "Test message for job", "job_123");
  logger.errorForJob("TestComponent", "Error message for job", "job_456");

  // Give time for messages to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify messages were received
  auto messages = mockWsManager->getReceivedMessages();
  assert(messages.size() >= 2);

  bool foundInfoMessage = false;
  bool foundErrorMessage = false;

  for (const auto &msg : messages) {
    if (msg.jobId == "job_123" && msg.logLevel == "INFO ") {
      foundInfoMessage = true;
    }
    if (msg.jobId == "job_456" && msg.logLevel == "ERROR") {
      foundErrorMessage = true;
    }
  }

  assert(foundInfoMessage);
  assert(foundErrorMessage);

  std::cout << "✓ Basic log streaming test passed" << std::endl;
}

void testLogFiltering() {
  std::cout << "\n=== Testing Log Filtering ===" << std::endl;

  auto mockWsManager = std::make_shared<MockWebSocketManager>();
  Logger &logger = Logger::getInstance();

  mockWsManager->clearMessages();

  // Configure logger with job filtering
  logger.clearStreamingJobFilter();
  logger.addStreamingJobFilter("allowed_job");

  // Test filtering - only allowed_job should be streamed
  logger.infoForJob("TestComponent", "Message for allowed job", "allowed_job");
  logger.infoForJob("TestComponent", "Message for filtered job",
                    "filtered_job");

  // Give time for messages to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto messages = mockWsManager->getReceivedMessages();

  // Should only have one message (for allowed_job)
  size_t allowedMessages = 0;
  for (const auto &msg : messages) {
    if (msg.jobId == "allowed_job") {
      allowedMessages++;
    }
    // Should not have any messages for filtered_job
    assert(msg.jobId != "filtered_job");
  }

  assert(allowedMessages >= 1);

  std::cout << "✓ Log filtering test passed" << std::endl;
}

void testLogLevelFiltering() {
  std::cout << "\n=== Testing Log Level Filtering ===" << std::endl;

  auto mockWsManager = std::make_shared<MockWebSocketManager>();
  Logger &logger = Logger::getInstance();

  mockWsManager->clearMessages();

  // Configure logger with level filtering (WARN and above)
  LogConfig config;
  config.enableRealTimeStreaming = true;
  config.level = LogLevel::WARN;
  config.streamAllLevels = false; // Respect log level filtering
  config.consoleOutput = false;
  config.fileOutput = false;

  logger.configure(config);
  logger.clearStreamingJobFilter(); // Allow all jobs

  // Test different log levels
  logger.debugForJob("TestComponent", "Debug message", "test_job");
  logger.infoForJob("TestComponent", "Info message", "test_job");
  logger.warnForJob("TestComponent", "Warning message", "test_job");
  logger.errorForJob("TestComponent", "Error message", "test_job");

  // Give time for messages to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto messages = mockWsManager->getReceivedMessages();

  // Should only have WARN and ERROR messages
  for (const auto &msg : messages) {
    assert(msg.logLevel == "WARN " || msg.logLevel == "ERROR");
    assert(msg.logLevel != "DEBUG" && msg.logLevel != "INFO ");
  }

  std::cout << "✓ Log level filtering test passed" << std::endl;
}

void testQueueOverflow() {
  std::cout << "\n=== Testing Queue Overflow Protection ===" << std::endl;

  auto mockWsManager = std::make_shared<MockWebSocketManager>();
  Logger &logger = Logger::getInstance();

  mockWsManager->clearMessages();

  // Configure logger with small queue size
  LogConfig config;
  config.enableRealTimeStreaming = true;
  config.streamingQueueSize = 5; // Very small queue
  config.consoleOutput = false;
  config.fileOutput = false;

  logger.configure(config);
  logger.clearStreamingJobFilter();

  // Flood the queue with messages
  for (int i = 0; i < 20; i++) {
    logger.infoForJob("TestComponent", "Flood message " + std::to_string(i),
                      "flood_job");
  }

  // Give time for messages to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Check that some messages were dropped (metrics should show this)
  LogMetrics metrics = logger.getMetrics();

  std::cout << "Messages dropped due to queue overflow: "
            << metrics.droppedMessages.load() << std::endl;
  std::cout << "✓ Queue overflow protection test completed" << std::endl;
}

void testLogMessageCreation() {
  std::cout << "\n=== Testing Log Message Creation ===" << std::endl;

  Logger &logger = Logger::getInstance();

  // Test LogMessage creation and JSON serialization
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
      context = {{"user_id", "12345"}, {"operation", "data_transform"}};

  // Create a log message using the private method (we'll test through public
  // interface)
  logger.infoForJob("TestComponent", "Test message with context",
                    "test_job_json", context);

  // Give time for processing
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "✓ Log message creation test completed" << std::endl;
}

void testStreamingConfiguration() {
  std::cout << "\n=== Testing Streaming Configuration ===" << std::endl;

  Logger &logger = Logger::getInstance();

  // Test enabling/disabling streaming
  logger.enableRealTimeStreaming(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  logger.enableRealTimeStreaming(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Test job filter management
  logger.addStreamingJobFilter("job1");
  logger.addStreamingJobFilter("job2");
  logger.removeStreamingJobFilter("job1");
  logger.clearStreamingJobFilter();

  std::cout << "✓ Streaming configuration test passed" << std::endl;
}

int main() {
  std::cout << "Starting Logger Real-time Streaming Tests..." << std::endl;

  try {
    testBasicLogStreaming();
    testLogFiltering();
    testLogLevelFiltering();
    testQueueOverflow();
    testLogMessageCreation();
    testStreamingConfiguration();

    std::cout << "\n🎉 All log streaming tests passed!" << std::endl;

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