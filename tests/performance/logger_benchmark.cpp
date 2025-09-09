#include "performance_benchmark.hpp"
#include "component_logger.hpp"
#include "log_handler.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <memory>

// Logger performance benchmark
class LoggerBenchmark : public BenchmarkBase {
public:
    LoggerBenchmark() : BenchmarkBase("Logger") {}

    void run() override {
        benchmarkBasicLogging();
        benchmarkConcurrentLogging();
        benchmarkHandlerSwitching();
        benchmarkLogLevelFiltering();
    }

private:
    void benchmarkBasicLogging() {
        std::cout << "Running basic logging benchmark...\n";

        // Create a simple console handler for testing
        auto consoleHandler = std::make_shared<etl_plus::ConsoleLogHandler>();
        etl_plus::ComponentLogger<etl_plus::AuthManager> logger(consoleHandler);

        const size_t numMessages = 10000;
        const std::string testMessage = "Test log message for performance benchmarking";

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < numMessages; ++i) {
            logger.info("AuthManager", testMessage + " #" + std::to_string(i));
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        addResult(createResult("Basic Logging", numMessages, duration,
                              "Simple sequential logging operations"));
    }

    void benchmarkConcurrentLogging() {
        std::cout << "Running concurrent logging benchmark...\n";

        auto consoleHandler = std::make_shared<etl_plus::ConsoleLogHandler>();
        etl_plus::ComponentLogger<etl_plus::AuthManager> logger(consoleHandler);

        const size_t numThreads = 8;
        const size_t messagesPerThread = 1000;
        const size_t totalMessages = numThreads * messagesPerThread;

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;
        for (size_t i = 0; i < numThreads; ++i) {
            threads.emplace_back([&, i]() {
                for (size_t j = 0; j < messagesPerThread; ++j) {
                    logger.info("AuthManager", "Thread " + std::to_string(i) +
                               " message #" + std::to_string(j));
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        addResult(createResult("Concurrent Logging", totalMessages, duration,
                              std::to_string(numThreads) + " threads"));
    }

    void benchmarkHandlerSwitching() {
        std::cout << "Running handler switching benchmark...\n";

        const size_t numHandlers = 5;
        const size_t messagesPerHandler = 2000;
        const size_t totalMessages = numHandlers * messagesPerHandler;

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < numHandlers; ++i) {
            auto handler = std::make_shared<etl_plus::ConsoleLogHandler>();
            etl_plus::ComponentLogger<etl_plus::AuthManager> logger(handler);

            for (size_t j = 0; j < messagesPerHandler; ++j) {
                logger.info("AuthManager", "Handler " + std::to_string(i) +
                           " message #" + std::to_string(j));
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        addResult(createResult("Handler Switching", totalMessages, duration,
                              "Switching between different log handlers"));
    }

    void benchmarkLogLevelFiltering() {
        std::cout << "Running log level filtering benchmark...\n";

        auto consoleHandler = std::make_shared<etl_plus::ConsoleLogHandler>();
        etl_plus::ComponentLogger<etl_plus::AuthManager> logger(consoleHandler);

        const size_t numMessages = 10000;
        const size_t messagesPerLevel = numMessages / 5;

        auto start = std::chrono::high_resolution_clock::now();

        // Mix different log levels
        for (size_t i = 0; i < messagesPerLevel; ++i) {
            logger.trace("AuthManager", "Trace message #" + std::to_string(i));
            logger.debug("AuthManager", "Debug message #" + std::to_string(i));
            logger.info("AuthManager", "Info message #" + std::to_string(i));
            logger.warn("AuthManager", "Warn message #" + std::to_string(i));
            logger.error("AuthManager", "Error message #" + std::to_string(i));
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        addResult(createResult("Log Level Filtering", numMessages, duration,
                              "Mixed log levels with filtering"));
    }
};
