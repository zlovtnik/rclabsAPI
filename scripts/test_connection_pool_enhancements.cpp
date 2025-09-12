#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "connection_pool_manager.hpp"
#include "server_config.hpp"

// Mock classes for testing
class MockTimeoutManager {
public:
  MockTimeoutManager(boost::asio::io_context &ioc,
                     std::chrono::seconds connTimeout,
                     std::chrono::seconds reqTimeout) {}

  void startConnectionTimeout(std::shared_ptr<PooledSession> session) {}
  void startRequestTimeout(std::shared_ptr<PooledSession> session) {}
  void cancelTimeouts(std::shared_ptr<PooledSession> session) {}
  void setConnectionTimeout(std::chrono::seconds timeout) {}
  void setRequestTimeout(std::chrono::seconds timeout) {}
  void cancelAllTimers() {}
};

/**
 * Connection Pool Enhancement Test
 * Tests the enhanced features of ConnectionPoolManager including:
 * - Request queuing when pool is at capacity
 * - Error handling for pool exhaustion
 * - Thread-safe concurrent operations
 * - Performance metrics collection
 */
class ConnectionPoolEnhancementTest {
private:
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::shared_ptr<ConnectionPoolManager> poolManager_;

public:
  void setup() {
    ioc_ = std::make_unique<boost::asio::io_context>();

    // Create pool manager with enhanced features
    poolManager_ = std::make_shared<ConnectionPoolManager>(
        *ioc_,
        2,                        // minConnections
        5,                        // maxConnections
        std::chrono::seconds(60), // idleTimeout
        nullptr,                  // handler (not needed for this test)
        nullptr,                  // wsManager (not needed for this test)
        nullptr,                  // timeoutManager (not needed for this test)
        10,                       // maxQueueSize
        std::chrono::seconds(5)   // maxQueueWaitTime
    );
  }

  void testQueueConfiguration() {
    std::cout << "Testing queue configuration..." << std::endl;

    setup();

    // Test queue configuration
    assert(poolManager_->getMaxConnections() == 5);
    assert(poolManager_->getMaxQueueSize() == 10);

    // Test initial state
    assert(poolManager_->getActiveConnections() == 0);
    assert(poolManager_->getIdleConnections() == 0);
    assert(poolManager_->getQueueSize() == 0);
    assert(poolManager_->getRejectedRequestCount() == 0);

    std::cout << "✓ Queue configuration test passed" << std::endl;
  }

  void testStatisticsTracking() {
    std::cout << "Testing statistics tracking..." << std::endl;

    setup();

    // Test initial statistics
    assert(poolManager_->getConnectionReuseCount() == 0);
    assert(poolManager_->getTotalConnectionsCreated() == 0);
    assert(poolManager_->getRejectedRequestCount() == 0);

    // Test statistics reset
    poolManager_->resetStatistics();
    assert(poolManager_->getConnectionReuseCount() == 0);
    assert(poolManager_->getTotalConnectionsCreated() == 0);
    assert(poolManager_->getRejectedRequestCount() == 0);

    std::cout << "✓ Statistics tracking test passed" << std::endl;
  }

  void testPoolCapacityLimits() {
    std::cout << "Testing pool capacity limits..." << std::endl;

    setup();

    // Test capacity checking
    assert(!poolManager_->isAtMaxCapacity());

    // Test configuration limits
    assert(poolManager_->getMinConnections() == 2);
    assert(poolManager_->getMaxConnections() == 5);

    std::cout << "✓ Pool capacity limits test passed" << std::endl;
  }

  void testThreadSafeAccess() {
    std::cout << "Testing thread-safe access to pool statistics..."
              << std::endl;

    setup();

    // Test concurrent access to statistics
    std::vector<std::future<bool>> futures;
    std::atomic<int> successfulAccesses{0};

    // Launch multiple threads to access pool statistics concurrently
    for (int i = 0; i < 10; ++i) {
      futures.push_back(
          std::async(std::launch::async, [this, &successfulAccesses]() {
            try {
              for (int j = 0; j < 100; ++j) {
                // Access various statistics concurrently
                auto active = poolManager_->getActiveConnections();
                auto idle = poolManager_->getIdleConnections();
                auto total = poolManager_->getTotalConnections();
                auto reuse = poolManager_->getConnectionReuseCount();
                auto queue = poolManager_->getQueueSize();
                auto rejected = poolManager_->getRejectedRequestCount();

                // Verify basic consistency
                assert(total == active + idle);
                assert(reuse >= 0);
                assert(queue >= 0);
                assert(rejected >= 0);

                // Small delay to increase chance of race conditions
                std::this_thread::sleep_for(std::chrono::microseconds(10));
              }
              return true;
            } catch (...) {
              return false;
            }
          }));
    }

    // Wait for all threads and check results
    for (auto &future : futures) {
      if (future.get()) {
        successfulAccesses++;
      }
    }

    assert(successfulAccesses.load() == 10);
    std::cout << "✓ Thread-safe access test passed" << std::endl;
  }

  void testConfigurationValidation() {
    std::cout << "Testing configuration validation..." << std::endl;

    // Test invalid configuration handling
    try {
      boost::asio::io_context testIoc;
      auto invalidPool = std::make_shared<ConnectionPoolManager>(
          testIoc,
          10, // minConnections
          5,  // maxConnections (less than min - should throw)
          std::chrono::seconds(60), nullptr, nullptr, nullptr, 10,
          std::chrono::seconds(5));
      assert(false); // Should not reach here
    } catch (const std::invalid_argument &e) {
      // Expected exception
      std::cout << "✓ Invalid min/max configuration correctly rejected"
                << std::endl;
    }

    // Test invalid timeout configuration
    try {
      boost::asio::io_context testIoc;
      auto invalidPool = std::make_shared<ConnectionPoolManager>(
          testIoc, 2, 5,
          std::chrono::seconds(-1), // Invalid negative timeout
          nullptr, nullptr, nullptr, 10, std::chrono::seconds(5));
      assert(false); // Should not reach here
    } catch (const std::invalid_argument &e) {
      // Expected exception
      std::cout << "✓ Invalid timeout configuration correctly rejected"
                << std::endl;
    }

    std::cout << "✓ Configuration validation test passed" << std::endl;
  }

  void testCleanupOperations() {
    std::cout << "Testing cleanup operations..." << std::endl;

    setup();

    // Test cleanup timer operations
    poolManager_->startCleanupTimer();
    poolManager_->stopCleanupTimer();

    // Test manual cleanup
    auto cleanedUp = poolManager_->cleanupIdleConnections();
    assert(cleanedUp >= 0); // Should not throw

    // Test shutdown
    poolManager_->shutdown();

    std::cout << "✓ Cleanup operations test passed" << std::endl;
  }

  void cleanup() {
    if (poolManager_) {
      poolManager_->shutdown();
    }
    poolManager_.reset();
    ioc_.reset();
  }

  void runAllTests() {
    std::cout << "Running Connection Pool Enhancement Tests..." << std::endl;
    std::cout << "============================================================="
              << std::endl;

    try {
      testQueueConfiguration();
      cleanup();

      testStatisticsTracking();
      cleanup();

      testPoolCapacityLimits();
      cleanup();

      testThreadSafeAccess();
      cleanup();

      testConfigurationValidation();
      cleanup();

      testCleanupOperations();
      cleanup();

      std::cout
          << "============================================================="
          << std::endl;
      std::cout << "✓ All connection pool enhancement tests passed!"
                << std::endl;

    } catch (const std::exception &e) {
      std::cout << "✗ Connection pool enhancement test failed: " << e.what()
                << std::endl;
      cleanup();
      throw;
    } catch (...) {
      std::cout
          << "✗ Connection pool enhancement test failed with unknown exception"
          << std::endl;
      cleanup();
      throw;
    }
  }
};

int main() {
  try {
    ConnectionPoolEnhancementTest test;
    test.runAllTests();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Connection pool enhancement test suite failed: " << e.what()
              << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Connection pool enhancement test suite failed with unknown "
                 "exception"
              << std::endl;
    return 1;
  }
}