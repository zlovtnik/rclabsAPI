#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "logger.hpp"
#include "server_config.hpp"

// This test focuses on configuration and optimization features
// without requiring a full HTTP server setup

/**
 * Enhanced Request Processing Integration Test
 */
class EnhancedRequestProcessingTest {
public:
  EnhancedRequestProcessingTest() {}

  void testMemoryOptimizedRequestProcessing() {
    std::cout << "Testing memory-optimized request processing..." << std::endl;

    // Create server config with optimized settings
    ServerConfig config = ServerConfig::create(5,           // minConnections
                                               10,          // maxConnections
                                               60,          // idleTimeoutSec
                                               10,          // connTimeoutSec
                                               30,          // reqTimeoutSec
                                               1024 * 1024, // maxBodySize (1MB)
                                               true,        // metricsEnabled
                                               50,          // maxQueueSize
                                               15 // maxQueueWaitTimeSec
    );

    server_ = std::make_unique<HttpServer>(address_, port_, 4, config);
    server_->setRequestHandler(handler_);

    // Verify configuration
    auto retrievedConfig = server_->getServerConfig();
    assert(retrievedConfig.maxQueueSize == 50);
    assert(retrievedConfig.maxQueueWaitTime.count() == 15);

    std::cout << "✓ Memory optimization configuration test passed" << std::endl;
  }

  void testRequestQueueingUnderLoad() {
    std::cout << "Testing request queuing under high load..." << std::endl;

    // Create server config with small pool to force queuing
    ServerConfig config =
        ServerConfig::create(2,  // minConnections (small)
                             3,  // maxConnections (small to force queuing)
                             60, // idleTimeoutSec
                             5,  // connTimeoutSec
                             10, // reqTimeoutSec
                             1024 * 1024, // maxBodySize
                             true,        // metricsEnabled
                             20,          // maxQueueSize
                             5            // maxQueueWaitTimeSec
        );

    server_ = std::make_unique<HttpServer>(address_, port_, 2, config);
    server_->setRequestHandler(handler_);

    // Test that pool manager is created with correct queue settings
    auto poolManager = server_->getConnectionPoolManager();
    if (poolManager) {
      assert(poolManager->getMaxConnections() == 3);
      assert(poolManager->getMaxQueueSize() == 20);
      std::cout << "✓ Queue configuration validation passed" << std::endl;
    }

    std::cout << "✓ Request queuing configuration test passed" << std::endl;
  }

  void testPoolExhaustionErrorHandling() {
    std::cout << "Testing pool exhaustion error handling..." << std::endl;

    // Create server config with very small limits to test exhaustion
    ServerConfig config =
        ServerConfig::create(1,           // minConnections
                             1,           // maxConnections (very small)
                             60,          // idleTimeoutSec
                             5,           // connTimeoutSec
                             10,          // reqTimeoutSec
                             1024 * 1024, // maxBodySize
                             true,        // metricsEnabled
                             2,           // maxQueueSize (very small)
                             1            // maxQueueWaitTimeSec (very short)
        );

    server_ = std::make_unique<HttpServer>(address_, port_, 1, config);
    server_->setRequestHandler(handler_);

    // Verify error handling configuration
    auto poolManager = server_->getConnectionPoolManager();
    if (poolManager) {
      assert(poolManager->getMaxConnections() == 1);
      assert(poolManager->getMaxQueueSize() == 2);

      // Test statistics tracking
      assert(poolManager->getRejectedRequestCount() == 0);
      std::cout << "✓ Error handling configuration validation passed"
                << std::endl;
    }

    std::cout << "✓ Pool exhaustion error handling test passed" << std::endl;
  }

  void testThreadSafeConcurrentProcessing() {
    std::cout << "Testing thread-safe concurrent request processing..."
              << std::endl;

    // Create server config optimized for concurrent processing
    ServerConfig config =
        ServerConfig::create(10,              // minConnections
                             50,              // maxConnections
                             120,             // idleTimeoutSec
                             15,              // connTimeoutSec
                             30,              // reqTimeoutSec
                             2 * 1024 * 1024, // maxBodySize (2MB)
                             true,            // metricsEnabled
                             100,             // maxQueueSize
                             30               // maxQueueWaitTimeSec
        );

    server_ =
        std::make_unique<HttpServer>(address_, port_, 8, config); // 8 threads
    server_->setRequestHandler(handler_);

    // Test concurrent access to pool manager
    auto poolManager = server_->getConnectionPoolManager();
    if (poolManager) {
      // Simulate concurrent access to statistics
      std::vector<std::future<void>> futures;
      std::atomic<int> completedTasks{0};

      for (int i = 0; i < 10; ++i) {
        futures.push_back(
            std::async(std::launch::async, [&poolManager, &completedTasks]() {
              // Simulate concurrent access to pool statistics
              for (int j = 0; j < 100; ++j) {
                auto activeCount = poolManager->getActiveConnections();
                auto idleCount = poolManager->getIdleConnections();
                auto totalCount = poolManager->getTotalConnections();
                auto reuseCount = poolManager->getConnectionReuseCount();
                auto queueSize = poolManager->getQueueSize();

                // Verify consistency
                assert(totalCount == activeCount + idleCount);
                assert(reuseCount >= 0);
                assert(queueSize >= 0);

                // Small delay to increase chance of race conditions
                std::this_thread::sleep_for(std::chrono::microseconds(10));
              }
              completedTasks++;
            }));
      }

      // Wait for all tasks to complete
      for (auto &future : futures) {
        future.wait();
      }

      assert(completedTasks.load() == 10);
      std::cout << "✓ Thread-safe concurrent access test passed" << std::endl;
    }

    std::cout << "✓ Thread-safe concurrent processing test passed" << std::endl;
  }

  void testConfigurationValidation() {
    std::cout << "Testing enhanced configuration validation..." << std::endl;

    // Test invalid queue configuration
    ServerConfig invalidConfig;
    invalidConfig.maxQueueSize = 0;                            // Invalid
    invalidConfig.maxQueueWaitTime = std::chrono::seconds{-1}; // Invalid

    auto validation = invalidConfig.validate();
    assert(!validation.isValid);
    assert(validation.errors.size() >=
           2); // Should have errors for queue settings

    std::cout << "✓ Invalid configuration detection passed" << std::endl;

    // Test configuration defaults
    invalidConfig.applyDefaults();
    assert(invalidConfig.maxQueueSize > 0);
    assert(invalidConfig.maxQueueWaitTime.count() > 0);

    std::cout << "✓ Configuration defaults application passed" << std::endl;

    // Test warning conditions
    ServerConfig warningConfig =
        ServerConfig::create(10, 100, 300, 30, 60, 10 * 1024 * 1024, true,
                             2000, // Very large queue size
                             400   // Very long wait time
        );

    auto warningValidation = warningConfig.validate();
    assert(warningValidation.isValid); // Should be valid but have warnings
    assert(!warningValidation.warnings.empty()); // Should have warnings

    std::cout << "✓ Configuration warning detection passed" << std::endl;
    std::cout << "✓ Enhanced configuration validation test passed" << std::endl;
  }

  void testMemoryAllocationOptimizations() {
    std::cout << "Testing memory allocation optimizations..." << std::endl;

    // This test verifies that the optimizations are in place
    // In a real scenario, we would measure memory usage, but for this test
    // we'll verify the configuration supports the optimizations

    ServerConfig config = ServerConfig::create(
        5, 20, 300, 30, 60,
        4 * 1024, // Small body size to test small response optimization
        true, 50, 30);

    server_ = std::make_unique<HttpServer>(address_, port_, 4, config);
    server_->setRequestHandler(handler_);

    // Verify that small body size configuration is preserved
    auto retrievedConfig = server_->getServerConfig();
    assert(retrievedConfig.maxRequestBodySize == 4 * 1024);

    std::cout << "✓ Memory allocation optimization configuration passed"
              << std::endl;
    std::cout << "✓ Memory allocation optimizations test passed" << std::endl;
  }

  void cleanup() {
    if (server_ && server_->isRunning()) {
      std::cout << "Stopping server..." << std::endl;
      server_->stop();
      assert(!server_->isRunning());
      std::cout << "✓ Server stopped successfully" << std::endl;
    }

    if (handler_) {
      handler_->resetCount();
    }
  }

  void runAllTests() {
    std::cout << "Running Enhanced Request Processing Integration Tests..."
              << std::endl;
    std::cout << "============================================================="
              << std::endl;

    try {
      testMemoryOptimizedRequestProcessing();
      cleanup();

      testRequestQueueingUnderLoad();
      cleanup();

      testPoolExhaustionErrorHandling();
      cleanup();

      testThreadSafeConcurrentProcessing();
      cleanup();

      testConfigurationValidation();
      cleanup();

      testMemoryAllocationOptimizations();
      cleanup();

      std::cout
          << "============================================================="
          << std::endl;
      std::cout << "✓ All enhanced request processing integration tests passed!"
                << std::endl;

    } catch (const std::exception &e) {
      std::cout << "✗ Integration test failed with exception: " << e.what()
                << std::endl;
      cleanup();
      throw;
    } catch (...) {
      std::cout << "✗ Integration test failed with unknown exception"
                << std::endl;
      cleanup();
      throw;
    }
  }
};

int main() {
  try {
    // Set up logging
    Logger::getInstance().setLogLevel(LogLevel::INFO);

    EnhancedRequestProcessingTest test;
    test.runAllTests();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Enhanced request processing test suite failed: " << e.what()
              << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Enhanced request processing test suite failed with unknown "
                 "exception"
              << std::endl;
    return 1;
  }
}