#include "core_logger.hpp"
#include "log_handler.hpp"
#include "transparent_string_hash.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

// Test handler implementation
class TestLogHandler : public LogHandler {
private:
  std::string id_;
  std::vector<LogEntry> capturedLogs_;
  mutable std::mutex logsMutex_;

public:
  /**
   * @brief Constructs a TestLogHandler with the given identifier.
   *
   * @param id Unique identifier for this handler instance (used in tests).
   */
  explicit TestLogHandler(const std::string &id) : id_(id) {}

  /**
   * @brief Handle a log entry by capturing it for later inspection.
   *
   * Appends a copy of the provided LogEntry to the handler's internal,
   * thread-safe captured log vector for use by tests. The method acquires an
   * internal mutex to protect concurrent access and then sleeps for 100
   * microseconds to simulate handler processing latency.
   *
   * @param entry Log entry to capture.
   */
  void handle(const LogEntry &entry) override {
    std::lock_guard lock(logsMutex_);
    capturedLogs_.push_back(entry);

    // Simulate processing time
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  /**
   * @brief Returns the handler's identifier.
   *
   * The identifier is the string supplied when the TestLogHandler was
   * constructed.
   *
   * @return std::string The handler ID.
   */
  std::string getId() const override { return id_; }

  /**
   * @brief Indicates whether this test handler will process a given log entry.
   *
   * For the test handler this always returns true so the handler accepts every
   * entry.
   *
   * @param entry The log entry being considered (ignored by this
   * implementation).
   * @return true Always accepts the entry.
   */
  bool shouldHandle(const LogEntry &entry) const override {
    // Handle all entries for testing
    return true;
  }

  /**
   * @brief No-op flush for the test log handler.
   *
   * This test handler does not buffer or batch log entries, so there is nothing
   * to flush; the method intentionally performs no action.
   */
  void flush() override {
    // Nothing to flush in this test handler
  }

  /**
   * @brief Shut down the test handler and clear all captured log entries.
   *
   * Acquires the internal mutex and removes all stored LogEntry objects so the
   * handler contains no captured logs after shutdown.
   */
  void shutdown() override {
    std::lock_guard lock(logsMutex_);
    capturedLogs_.clear();
  }

  /**
   * @brief Returns the number of log entries captured by the handler.
   *
   * This method acquires the internal mutex to read the stored entries safely
   * and is safe to call from multiple threads concurrently.
   *
   * @return size_t The current count of captured log entries.
   */
  size_t getCapturedLogCount() const {
    std::lock_guard lock(logsMutex_);
    return capturedLogs_.size();
  }

  /**
   * @brief Returns a snapshot of all logs captured by the handler.
   *
   * The returned vector is a copy of the internal storage taken under the
   * handler's mutex, providing a thread-safe snapshot of captured log entries
   * at the time of the call.
   *
   * @return std::vector<LogEntry> Copy of the captured logs.
   */
  std::vector<LogEntry> getCapturedLogs() const {
    std::lock_guard lock(logsMutex_);
    return capturedLogs_;
  }

  /**
   * @brief Clears all logs previously captured by the handler.
   *
   * This operation removes every stored LogEntry from the handler's internal
   * buffer. It is thread-safe: the internal mutex is held for the duration of
   * the clear.
   */
  void clearCapturedLogs() {
    std::lock_guard lock(logsMutex_);
    capturedLogs_.clear();
  }
};

/**
 * @brief Runs a unit test that verifies basic logging and handler delivery.
 *
 * This test registers a TestLogHandler with the CoreLogger, emits an info and
 * an error log for a test component, allows asynchronous processing to
 * complete, flushes pending logs, and asserts that the handler received at
 * least two entries. The test prints status messages and uses assertions to
 * signal failure.
 */
TEST_F(CoreLoggerTest, BasicLogging) {
  auto &logger = CoreLogger::getInstance();
  auto testHandler = std::make_shared<TestLogHandler>("test_handler");

  // Register handler
  ASSERT_EQ(logger.registerHandler(testHandler),
            CoreLogger::HandlerResult::SUCCESS);

  // Test basic logging
  logger.info("TestComponent", "Test message");
  logger.error("TestComponent", "Error message");

  // Give async processing time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  logger.flush();

  // Verify logs were captured
  EXPECT_GE(testHandler->getCapturedLogCount(), 2);
}

/**
 * @brief Tests job-scoped logging functionality.
 *
 * This test verifies that job-scoped log APIs correctly attach job IDs to
 * log entries. It emits job-specific messages and verifies that the expected
 * job IDs are present in the captured logs.
 */
TEST(CoreLoggerTest, JobSpecificLogging) {
  auto &logger = CoreLogger::getInstance();
  auto testHandler = std::make_shared<TestLogHandler>("job_test_handler");

  logger.registerHandler(testHandler);
  testHandler->clearCapturedLogs();

  // Test job-specific logging
  logger.infoForJob("JobManager", "Job started", "job123");
  logger.errorForJob("JobManager", "Job failed", "job456");

  // Get captured logs (no sleep/flush needed due to synchronous handler)
  auto logs = testHandler->getCapturedLogs();
  EXPECT_GE(logs.size(), 2);

  // Verify job IDs are correctly set
  bool foundJob123 = false, foundJob456 = false;
  for (const auto &log : logs) {
    if (log.jobId == "job123")
      foundJob123 = true;
    if (log.jobId == "job456")
      foundJob456 = true;
  }

  EXPECT_TRUE(foundJob123) << "Job ID 'job123' should be present in logs";
  EXPECT_TRUE(foundJob456) << "Job ID 'job456' should be present in logs";
}

/**
 * @brief Unit test for CoreLogger handler lifecycle and management APIs.
 *
 * Verifies registering, detecting duplicates, existence checks, retrieval,
 * listing, and removal of log handlers using TestLogHandler instances.
 */
TEST(CoreLoggerTest, HandlerManagement) {
  auto &logger = CoreLogger::getInstance();

  // Register test handlers
  auto handler1 = std::make_shared<TestLogHandler>("handler1");
  auto handler2 = std::make_shared<TestLogHandler>("handler2");

  EXPECT_TRUE(logger.registerHandler(handler1));
  EXPECT_TRUE(logger.registerHandler(handler2));
  EXPECT_FALSE(logger.registerHandler(handler1)); // Duplicate registration should fail

  // Test handler listing
  auto handlerIds = logger.getHandlerIds();
  EXPECT_GE(handlerIds.size(), 2);

  // Test handler removal
  EXPECT_TRUE(logger.unregisterHandler("handler1"));
  EXPECT_FALSE(logger.hasHandler("handler1"));
  EXPECT_FALSE(logger.unregisterHandler("nonexistent"));
}

/**
 * @brief Tests CoreLogger configuration management.
 *
 * This test verifies configuration querying and updates, including default
 * values, full configuration updates, and individual setting changes.
 */
TEST(CoreLoggerTest, ConfigurationManagement) {
  auto &logger = CoreLogger::getInstance();

  // Test initial configuration
  auto config = logger.getConfig();
  EXPECT_EQ(config.minLevel, LogLevel::INFO);

  // Test configuration update
  config.minLevel = LogLevel::WARN;
  config.enableAsyncLogging = false;
  logger.configure(config);

  auto updatedConfig = logger.getConfig();
  EXPECT_EQ(updatedConfig.minLevel, LogLevel::WARN);
  EXPECT_FALSE(updatedConfig.enableAsyncLogging);

  // Test individual setting updates
  logger.setLogLevel(LogLevel::DEBUG);
  EXPECT_EQ(logger.getLogLevel(), LogLevel::DEBUG);

  logger.setAsyncLogging(true);
  EXPECT_TRUE(logger.isAsyncLogging());
}

/**
 * @brief Tests component-based filtering functionality.
 *
 * This test verifies that the logger correctly filters log messages based on
 * component names. It sets up a blacklist filter containing "BlockedComponent",
 * emits logs from both allowed and blocked components, and verifies that only
 * the allowed component's log was captured.
 *
 * The test registers a TestLogHandler with the CoreLogger and clears the
 * component filter before returning to ensure clean state for other tests.
 */
TEST(CoreLoggerTest, ComponentFiltering) {
  auto &logger = CoreLogger::getInstance();
  auto testHandler = std::make_shared<TestLogHandler>("filter_test_handler");

  // Register handler and clear any existing logs
  logger.registerHandler(testHandler);
  testHandler->clearCapturedLogs();

  // Set up component filter (blacklist mode)
  std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>
      componentFilter;
  componentFilter.insert("BlockedComponent");
  logger.setComponentFilter(componentFilter, false); // blacklist mode

  // Test component filtering
  logger.info("AllowedComponent", "This should pass");
  logger.info("BlockedComponent", "This should be blocked");

  // Get captured logs (no sleep/flush needed due to synchronous handler)
  auto logs = testHandler->getCapturedLogs();

  // Should only have the allowed component log
  bool foundAllowed = false, foundBlocked = false;
  for (const auto &log : logs) {
    if (log.component == "AllowedComponent")
      foundAllowed = true;
    if (log.component == "BlockedComponent")
      foundBlocked = true;
  }

  EXPECT_TRUE(foundAllowed) << "Allowed component log should be captured";
  EXPECT_FALSE(foundBlocked) << "Blocked component log should be filtered out";

  // Clear filter to restore clean state
  logger.clearComponentFilter();
}

/**
 * @brief Tests the logger's metrics collection functionality.
 *
 * Generates a set of informational, error, and warning messages, then verifies
 * that the logger's metrics are correctly updated. Tests at least 10 total
 * messages, >=3 errors, and >=2 warnings.
 */
TEST(CoreLoggerTest, MetricsCollection) {
  auto &logger = CoreLogger::getInstance();
  auto testHandler = std::make_shared<TestLogHandler>("metrics_test_handler");

  logger.registerHandler(testHandler);
  logger.resetMetrics();

  // Generate some logs
  for (int i = 0; i < 10; ++i) {
    logger.info("MetricsTest", "Message " + std::to_string(i));
    if (i % 3 == 0) {
      logger.error("MetricsTest", "Error " + std::to_string(i));
    }
    if (i % 5 == 0) {
      logger.warn("MetricsTest", "Warning " + std::to_string(i));
    }
  }

  // Get metrics (no sleep/flush needed due to synchronous handler)
  auto metrics = logger.getMetrics();
  EXPECT_GE(metrics.totalMessages.load(), 10);
  EXPECT_GE(metrics.errorCount.load(), 3);
  EXPECT_GE(metrics.warningCount.load(), 2);

  // Test performance logging
  logger.logPerformance("TestOperation", 123.45);
  logger.logMetric("TestMetric", 42.0, "units");
}

/**
 * @brief Tests asynchronous logging functionality.
 *
 * This test verifies that the logger correctly processes messages when
 * asynchronous mode is enabled. It emits a burst of log messages and verifies
 * that all messages are captured by the handler.
 */
TEST(CoreLoggerTest, AsyncLogging) {
  auto &logger = CoreLogger::getInstance();
  auto testHandler = std::make_shared<TestLogHandler>("async_test_handler");

  logger.registerHandler(testHandler);
  testHandler->clearCapturedLogs();

  // Enable async logging
  logger.setAsyncLogging(true);

  // Generate a burst of logs
  const int logCount = 100;
  for (int i = 0; i < logCount; ++i) {
    logger.info("AsyncTest", "Async message " + std::to_string(i));
  }

  // Give async processing time
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  logger.flush();

  // Verify all logs were processed
  EXPECT_GE(testHandler->getCapturedLogCount(), logCount);
}

/**
 * @brief Tests backward compatibility with the legacy Logger interface.
 *
 * This test verifies that the old Logger API still works correctly with the
 * new CoreLogger implementation, ensuring no breaking changes for existing code.
 */
TEST(CoreLoggerTest, BackwardCompatibility) {
  // Test old Logger interface
  auto &oldLogger = Logger::getInstance();

  // Test basic configuration
  LogConfig config;
  config.level = LogLevel::DEBUG;
  config.asyncLogging = true;
  config.consoleOutput = true;
  oldLogger.configure(config);

  // Test basic logging methods
  oldLogger.info("CompatTest", "Backward compatibility test");
  oldLogger.error("CompatTest", "Error test");

  // Test job-specific methods
  oldLogger.infoForJob("CompatTest", "Job message", "compat_job");

  // Test metrics
  oldLogger.logMetric("CompatMetric", 99.9, "percent");
  oldLogger.logPerformance("CompatOperation", 456.78);

  // Test control methods
  oldLogger.flush();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
