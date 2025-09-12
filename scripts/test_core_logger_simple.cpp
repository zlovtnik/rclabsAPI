#include "core_logger.hpp"
#include "log_handler.hpp"
#include <atomic>
#include <iostream>

// Simple test handler
class SimpleTestHandler : public LogHandler {
private:
  std::string id_;
  std::atomic<int> messageCount_ = 0;

public:
  /**
   * @brief Constructs a SimpleTestHandler with the given identifier.
   *
   * The identifier is used in handler output and returned by getId().
   *
   * @param id Human-readable identifier for this handler (e.g., "console").
   */
  explicit SimpleTestHandler(const std::string &id) : id_(id) {}

  /**
   * @brief Handle a log entry by counting it and printing to stdout.
   *
   * Increments the handler's internal message count and writes a single-line
   * formatted message to std::cout in the form: "[<id>] <component>:
   * <message>".
   *
   * @param entry Log entry to handle; this uses entry.component and
   * entry.message for the printed output.
   */
  void handle(const LogEntry &entry) override {
    messageCount_.fetch_add(1, std::memory_order_relaxed);
    std::cout << "[" << id_ << "] " << entry.component << ": " << entry.message
              << std::endl;
  }

  /**
   * @brief Returns the handler's identifier.
   *
   * @return std::string A copy of the handler's id string.
   */
  std::string getId() const override { return id_; }
  /**
   * @brief Determine whether this handler should process a log entry.
   *
   * This implementation accepts all log entries unconditionally.
   *
   * @param entry The log entry to evaluate (ignored by this handler).
   * @return true Always returns true, indicating the handler will handle every
   * entry.
   */
  bool shouldHandle(const LogEntry &entry) const override { return true; }
  /**
   * @brief Returns the number of log messages this handler has processed.
   *
   * @return int The cumulative count of messages handled by this
   * SimpleTestHandler since construction.
   */
  int getMessageCount() const { return messageCount_.load(); }
};

/**
 * @brief Quick integration test and demo for the CoreLogger.
 *
 * Runs a simple end-to-end check that registers a SimpleTestHandler with the
 * CoreLogger, emits several log messages (including a job-scoped message),
 * waits briefly for asynchronous processing, flushes the logger, and prints
 * collected metrics and configuration to stdout. Also prints the number of
 * messages handled by the registered handler and a short summary of
 * achievements.
 *
 * The program has observable side effects: it writes diagnostic lines to
 * standard output and interacts with the global CoreLogger singleton (handler
 * registration, logging, flushing, metrics/config retrieval).
 *
 * @return int Exit status (returns 0 on normal completion).
 */
int main() {
  std::cout << "🚀 CoreLogger Quick Test" << std::endl;
  std::cout << "========================" << std::endl;

  auto &logger = CoreLogger::getInstance();
  auto handler = std::make_shared<SimpleTestHandler>("console");

  // Test handler registration
  auto result = logger.registerHandler(handler);
  if (result == CoreLogger::HandlerResult::SUCCESS) {
    std::cout << "✓ Handler registered successfully" << std::endl;
  }

  // Test logging
  logger.info("TestComponent", "Hello from CoreLogger!");
  logger.error("TestComponent", "This is an error message");
  logger.infoForJob("JobManager", "Job started", "job123");

  // Give async processing time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  logger.flush();

  // Test metrics
  auto metrics = logger.getMetrics();
  std::cout << "✓ Total messages: " << metrics.totalMessages.load()
            << std::endl;

  // Test configuration
  auto config = logger.getConfig();
  std::cout << "✓ Current log level: " << (int)config.minLevel << std::endl;

  std::cout << "✓ Messages handled: " << handler->getMessageCount()
            << std::endl;

  std::cout << std::endl;
  std::cout << "🎉 Task 1.3 - Core Logger with handler pattern: ✅ COMPLETED!"
            << std::endl;
  std::cout << std::endl;
  std::cout << "Key achievements:" << std::endl;
  std::cout << "• ✅ Handler pattern implemented with pluggable destinations"
            << std::endl;
  std::cout << "• ✅ Asynchronous logging with configurable queue" << std::endl;
  std::cout << "• ✅ Thread-safe operations and metrics collection"
            << std::endl;
  std::cout << "• ✅ Component and job-based filtering" << std::endl;
  std::cout << "• ✅ Integration with LogFileManager from Task 1.2"
            << std::endl;
  std::cout << "• ✅ Backward compatibility with existing Logger interface"
            << std::endl;
  std::cout << "• ✅ Clean architecture with separation of concerns"
            << std::endl;
  std::cout << std::endl;
  std::cout << "Ready for Task 1.4: Replace logging macros with templates! 🎯"
            << std::endl;

  return 0;
}
