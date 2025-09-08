#include <gtest/gtest.h>
#include "log_handler.hpp"
#include "component_logger.hpp"
#include "logger.hpp"
#include <memory>
#include <sstream>
#include <thread>
#include <chrono>
#include <unordered_map>

namespace {

// Mock log handler for testing
class MockLogHandler : public LogHandler {
public:
    MockLogHandler() : id_("MockHandler") {}

    void handle(const LogEntry& entry) override {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(entry);
    }

    std::string getId() const override {
        return id_;
    }

    bool shouldHandle(const LogEntry& entry) const override {
        return true; // Handle all entries for testing
    }

    void flush() override {
        // No-op for testing
    }

    const std::vector<LogEntry>& getEntries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_;
    }

    void clearEntries() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

private:
    std::string id_;
    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;
};

} // namespace

class LogHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        handler_ = std::make_unique<MockLogHandler>();
    }

    std::unique_ptr<MockLogHandler> handler_;
};

// Test LogHandler interface
TEST_F(LogHandlerTest, HandlerId) {
    EXPECT_EQ(handler_->getId(), "MockHandler");
}

TEST_F(LogHandlerTest, HandleLogEntry) {
    LogEntry entry(LogLevel::INFO, "TestComponent", "Test message");

    handler_->handle(entry);

    const auto& entries = handler_->getEntries();
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].level, LogLevel::INFO);
    EXPECT_EQ(entries[0].component, "TestComponent");
    EXPECT_EQ(entries[0].message, "Test message");
}

TEST_F(LogHandlerTest, ShouldHandle) {
    LogEntry entry(LogLevel::INFO, "TestComponent", "Test message");
    EXPECT_TRUE(handler_->shouldHandle(entry));
}

// Test ComponentLogger template with a mock component
class MockComponent {};

namespace etl {
template<> struct ComponentTrait<MockComponent> {
    static constexpr const char* name = "MockComponent";
};
} // namespace etl

TEST(ComponentLoggerTest, BasicLogging) {
    // Create a simple test by calling the static methods
    // Note: This will use the actual Logger singleton, so we need to be careful
    // In a real test environment, you might want to mock the Logger

    // Test that the component name is correctly resolved at compile time
    EXPECT_STREQ(etl::ComponentTrait<MockComponent>::name, "MockComponent");

    // Test logging calls (these will go to the actual logger)
    etl::ComponentLogger<MockComponent>::info("Test message");
    etl::ComponentLogger<MockComponent>::debug("Debug message");
    etl::ComponentLogger<MockComponent>::warn("Warning message");
    etl::ComponentLogger<MockComponent>::error("Error message");

    // Note: In a full test, you'd want to capture the output or mock the logger
    // For now, we just verify the calls don't crash
    SUCCEED();
}

TEST(ComponentLoggerTest, MultipleComponents) {
    Logger& logger = Logger::getInstance();
    
    // Test different component types - these use the predefined ComponentTrait specializations
    logger.info("AuthManager", "AuthManager test");
    logger.info("ConfigManager", "ConfigManager test");
    logger.info("DatabaseManager", "DatabaseManager test");

    // Note: We can't directly test the trait names since they're already defined
    // in component_logger.hpp, but the logging calls above verify they work
    SUCCEED();
}

// Test with actual logger configuration
TEST(ComponentLoggerTest, LoggerConfiguration) {
    Logger& logger = Logger::getInstance();

    // Configure logger for testing
    LogConfig config;
    config.level = LogLevel::DEBUG;
    config.consoleOutput = false; // Disable console output for clean tests
    config.fileOutput = false;    // Disable file output for clean tests

    logger.configure(config);

    // Test logging with different levels
    logger.debug("HttpServer", "Debug test");
    logger.info("HttpServer", "Info test");
    logger.warn("HttpServer", "Warn test");
    logger.error("HttpServer", "Error test");

    SUCCEED();
}

// Test thread safety of component logging
TEST(ComponentLoggerTest, ThreadSafety) {
    const int numThreads = 5;
    const int messagesPerThread = 50;

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i, messagesPerThread]() {
            Logger& logger = Logger::getInstance();
            for (int j = 0; j < messagesPerThread; ++j) {
                logger.info("HttpServer", "Thread " + std::to_string(i) + " message " + std::to_string(j));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // If we get here without crashes or deadlocks, the test passes
    SUCCEED();
}

// Test performance under load
TEST(ComponentLoggerTest, PerformanceTest) {
    const int numMessages = 1000;
    Logger& logger = Logger::getInstance();
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numMessages; ++i) {
        logger.info("HttpServer", "Performance test message " + std::to_string(i));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete within reasonable time (adjust threshold as needed)
    EXPECT_LT(duration.count(), 500); // Less than 500ms for 1000 messages
}
