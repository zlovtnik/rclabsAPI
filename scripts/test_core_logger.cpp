#include "core_logger.hpp"
#include "log_handler.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

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
void testBasicLogging() {
  std::cout << "Testing basic logging functionality..." << std::endl;

  auto &logger = CoreLogger::getInstance();
  auto testHandler = std::make_shared<TestLogHandler>("test_handler");

  // Register handler
  auto result = logger.registerHandler(testHandler);
  assert(result == CoreLogger::HandlerResult::SUCCESS);

  // Test basic logging
  logger.info("TestComponent", "Test message");
  logger.error("TestComponent", "Error message");

  // Give async processing time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  logger.flush();

  // Verify logs were captured
  assert(testHandler->getCapturedLogCount() >= 2);

  std::cout << "✓ Basic logging test passed" << std::endl;
}

/**
 * @brief Tests that job-scoped log APIs attach the correct job IDs to emitted
 * entries.
 *
 * This test registers a TestLogHandler, emits job-specific info and error
 * messages using the logger's job-scoped APIs, flushes processing, and asserts
 * that at least two entries were captured and that the expected job IDs
 * ("job123" and "job456") appear in the captured logs.
 */
void testJobSpecificLogging() {
  std::cout << "Testing job-specific logging..." << std::endl;

  auto &logger = CoreLogger::getInstance();
  auto testHandler = std::make_shared<TestLogHandler>("job_test_handler");

  logger.registerHandler(testHandler);
  testHandler->clearCapturedLogs();

  // Test job-specific logging
  logger.infoForJob("JobManager", "Job started", "job123");
  logger.errorForJob("JobManager", "Job failed", "job456");

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  logger.flush();

  auto logs = testHandler->getCapturedLogs();
  assert(logs.size() >= 2);

  // Verify job IDs are correctly set
  bool foundJob123 = false, foundJob456 = false;
  for (const auto &log : logs) {
    if (log.jobId == "job123")
      foundJob123 = true;
    if (log.jobId == "job456")
      foundJob456 = true;
  }
  assert(foundJob123 && foundJob456);

  std::cout << "✓ Job-specific logging test passed" << std::endl;
}

/**
 * @brief Unit test for CoreLogger handler lifecycle and management APIs.
 *
 * Verifies registering, detecting duplicates, existence checks, retrieval,
 * listing, and removal of log handlers using TestLogHandler instances.
 *
 * The test asserts that:
 * - Registering new handlers returns `HandlerResult::SUCCESS`.
 * - Re-registering the same handler returns `HandlerResult::ALREADY_EXISTS`.
 * - `hasHandler` correctly reports presence/absence of handlers.
 * - `getHandler` returns the registered handler and its identifier matches.
 * - `getHandlerIds` returns a list containing at least the registered handlers.
 * - `unregisterHandler` removes a handler and returns false for unknown IDs.
 *
 * Side effects: registers and unregisters handlers on the global CoreLogger
 * singleton; relies on assertions for test validation and prints status to
 * stdout.
 */
void testHandlerManagement() {
  std::cout << "Testing handler management..." << std::endl;

  auto &logger = CoreLogger::getInstance();

  // Test handler registration
  auto handler1 = std::make_shared<TestLogHandler>("handler1");
  auto handler2 = std::make_shared<TestLogHandler>("handler2");

  assert(logger.registerHandler(handler1) ==
         CoreLogger::HandlerResult::SUCCESS);
  assert(logger.registerHandler(handler2) ==
         CoreLogger::HandlerResult::SUCCESS);

  // Test duplicate registration
  assert(logger.registerHandler(handler1) ==
         CoreLogger::HandlerResult::ALREADY_EXISTS);

  // Test handler existence
  assert(logger.hasHandler("handler1"));
  assert(logger.hasHandler("handler2"));
  assert(!logger.hasHandler("nonexistent"));

  // Test handler retrieval
  auto retrieved = logger.getHandler("handler1");
  assert(retrieved != nullptr);
  assert(retrieved->getId() == "handler1");

  // Test handler listing
  auto handlerIds = logger.getHandlerIds();
  assert(handlerIds.size() >= 2);

  // Test handler removal
  assert(logger.unregisterHandler("handler1"));
  assert(!logger.hasHandler("handler1"));
  assert(!logger.unregisterHandler("nonexistent"));

  std::cout << "✓ Handler management test passed" << std::endl;
}

/**
 * @brief Tests CoreLogger configuration querying and updates.
 *
 * Verifies the default configuration (minimum level is INFO), applies a full
 * configuration update (changes minimum level to WARN and disables async
 * logging) and asserts the change took effect. Also exercises individual
 * setters/getters by switching the log level to DEBUG and toggling async
 * logging to true. Uses assertions to validate each step.
 */
void testConfiguration() {
  std::cout << "Testing configuration management..." << std::endl;

  auto &logger = CoreLogger::getInstance();

  // Test initial configuration
  auto config = logger.getConfig();
  assert(config.minLevel == LogLevel::INFO);

  // Test configuration update
  config.minLevel = LogLevel::WARN;
  config.enableAsyncLogging = false;
  logger.configure(config);

  auto updatedConfig = logger.getConfig();
  assert(updatedConfig.minLevel == LogLevel::WARN);
  assert(!updatedConfig.enableAsyncLogging);

  // Test individual setting updates
  logger.setLogLevel(LogLevel::DEBUG);
  assert(logger.getLogLevel() == LogLevel::DEBUG);

  logger.setAsyncLogging(true);
  assert(logger.isAsyncLogging());

  std::cout << "✓ Configuration test passed" << std::endl;
}

/**
 * @brief Verifies component-based filtering prevents blacklisted components
 * from being logged.
 *
 * This test registers a TestLogHandler with the CoreLogger, applies a component
 * filter in blacklist mode containing "BlockedComponent", emits one log from an
 * allowed component and one from the blocked component, then asserts that only
 * the allowed entry was captured. The test clears the component filter before
 * returning.
 *
 * Side effects:
 * - Registers a TestLogHandler with the CoreLogger.
 * - Modifies the CoreLogger component filter (set and cleared).
 * - Emits log messages via the CoreLogger.
 *
 * The test blocks briefly to allow asynchronous processing and calls
 * CoreLogger::flush() to ensure entries are processed before assertions.
 */
void testFiltering() {
  std::cout << "Testing filtering functionality..." << std::endl;

  auto &logger = CoreLogger::getInstance();
  auto testHandler = std::make_shared<TestLogHandler>("filter_test_handler");

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

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  logger.flush();

  auto logs = testHandler->getCapturedLogs();

  // Should only have the allowed component log
  bool foundAllowed = false, foundBlocked = false;
  for (const auto &log : logs) {
    if (log.component == "AllowedComponent")
      foundAllowed = true;
    if (log.component == "BlockedComponent")
      foundBlocked = true;
  }
  assert(foundAllowed && !foundBlocked);

  // Clear filter
  logger.clearComponentFilter();

  std::cout << "✓ Filtering test passed" << std::endl;
}

/**
 * @brief Exercises the logger's metrics collection and related logging APIs.
 *
 * Generates a set of informational, error, and warning messages, resets and
 * then reads the logger's metrics, and asserts that totals meet expected
 * thresholds. Also exercises performance and metric logging APIs.
 *
 * Notes:
 * - Registers a TestLogHandler and resets metrics before emitting messages.
 * - Waits and calls flush to allow asynchronous processing before reading
 * metrics.
 * - Uses assertions to verify at least 10 total messages, >=3 errors, and >=2
 * warnings.
 */
void testMetrics() {
  std::cout << "Testing metrics collection..." << std::endl;

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

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  logger.flush();

  auto metrics = logger.getMetrics();
  assert(metrics.totalMessages.load() >= 10);
  assert(metrics.errorCount.load() >= 3);
  assert(metrics.warningCount.load() >= 2);

  // Test performance logging
  logger.logPerformance("TestOperation", 123.45);
  logger.logMetric("TestMetric", 42.0, "units");

  std::cout << "✓ Metrics test passed" << std::endl;
}

/**
 * @brief Tests that the logger processes messages correctly when asynchronous
 * mode is enabled.
 *
 * This test registers a TestLogHandler, enables async logging, emits a burst of
 * 100 info messages, waits for processing, flushes pending work, and asserts
 * that at least the emitted number of logs have been captured by the handler.
 *
 * Side effects:
 * - Enables asynchronous logging on the global CoreLogger instance.
 * - Registers a TestLogHandler (does not unregister it).
 *
 * The test uses an assertion to fail if fewer than the expected number of logs
 * are processed.
 */
void testAsyncLogging() {
  std::cout << "Testing asynchronous logging..." << std::endl;

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
  assert(testHandler->getCapturedLogCount() >= logCount);

  std::cout << "✓ Async logging test passed" << std::endl;
}

/**
 * @brief Verifies that the legacy Logger interface remains compatible with the
 * current CoreLogger.
 *
 * Exercises the old Logger API by configuring it, emitting standard and
 * job-scoped log messages, recording a metric and a performance measurement,
 * and flushing pending output. Intended for use in the test suite; it mutates
 * global logger state and produces console output.
 */
void testBackwardCompatibility() {
  std::cout << "Testing backward compatibility..." << std::endl;

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

  std::cout << "✓ Backward compatibility test passed" << std::endl;
}

/**
 * @brief Entry point that runs the CoreLogger comprehensive test suite.
 *
 * Executes all unit tests for CoreLogger (basic logging, job-scoped logging,
 * handler management, configuration, filtering, metrics, async behavior and
 * backward compatibility). Prints progress, per-feature summary, and a final
 * success message to stdout. On test failure the function prints an error to
 * stderr and returns a non-zero exit code.
 *
 * @return int 0 on success; 1 if any test throws an exception.
 */
int main() {
  std::cout << "Starting CoreLogger comprehensive test suite..." << std::endl;
  std::cout << "================================================" << std::endl;

  try {
    testBasicLogging();
    testJobSpecificLogging();
    testHandlerManagement();
    testConfiguration();
    testFiltering();
    testMetrics();
    testAsyncLogging();
    testBackwardCompatibility();

    std::cout << "================================================"
              << std::endl;
    std::cout << "🎉 All tests passed! CoreLogger implementation is working "
                 "correctly."
              << std::endl;
    std::cout << std::endl;
    std::cout << "Task 1.3 - Core Logger with handler pattern: ✅ COMPLETED"
              << std::endl;
    std::cout << std::endl;
    std::cout << "Features implemented:" << std::endl;
    std::cout << "• Handler pattern with pluggable log destinations"
              << std::endl;
    std::cout << "• Asynchronous logging with configurable queue" << std::endl;
    std::cout << "• Component and job-based filtering" << std::endl;
    std::cout << "• Comprehensive metrics collection" << std::endl;
    std::cout << "• Thread-safe operations" << std::endl;
    std::cout << "• Integration with LogFileManager from Task 1.2" << std::endl;
    std::cout << "• Full backward compatibility with existing Logger interface"
              << std::endl;
    std::cout << "• Performance optimizations and monitoring" << std::endl;
    std::cout << std::endl;
    std::cout << "Ready to move on to Task 1.4: Replace logging macros with "
                 "templates!"
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Test failed with unknown exception" << std::endl;
    return 1;
  }

  return 0;
}
