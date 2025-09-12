#include "connection_pool_manager.hpp"
#include "timeout_manager.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class ConnectionPoolManagerSimpleTest : public ::testing::Test {
protected:
  /**
   * @brief Test fixture setup: initializes timeout manager and default pool
   * settings.
   *
   * Called before each test. Constructs a TimeoutManager using the fixture's IO
   * context and sets the default connection-pool configuration used by tests:
   * minimum connections = 2, maximum connections = 5, idle timeout = 10
   * seconds.
   */
  void SetUp() override {
    timeoutManager_ = std::make_shared<TimeoutManager>(ioc_);

    // Default pool configuration
    minConnections_ = 2;
    maxConnections_ = 5;
    idleTimeout_ = std::chrono::seconds(10);
  }

  /**
   * @brief Tear down the test fixture.
   *
   * Stops and cleans up resources created during SetUp:
   * - If a ConnectionPoolManager was created, calls its shutdown().
   * - Stops the Boost.AsIO io_context.
   * - Joins the IO thread if it is joinable.
   *
   * Safe to call when no pool manager or IO thread exists.
   */
  void TearDown() override {
    if (poolManager_) {
      poolManager_->shutdown();
    }
    ioc_.stop();
    if (ioThread_.joinable()) {
      ioThread_.join();
    }
  }

  /**
   * @brief Constructs a ConnectionPoolManager for tests and stores it in
   * poolManager_.
   *
   * Creates a ConnectionPoolManager using the fixture's ioc_, minConnections_,
   * maxConnections_, idleTimeout_, and timeoutManager_. The session handler and
   * wsManager are passed as null because tests exercise pool behavior only.
   * MonitorConfig is created with a null monitor and QueueConfig is initialized
   * to capacity 100 with a 30-second idle queue timeout.
   */
  void createPoolManager() {
    // Use null pointers for handler and wsManager since we're only testing pool
    // logic
    poolManager_ = std::make_unique<ConnectionPoolManager>(
        ioc_, minConnections_, maxConnections_, idleTimeout_, nullptr, nullptr,
        timeoutManager_, ConnectionPoolManager::MonitorConfig{nullptr},
        ConnectionPoolManager::QueueConfig{100, std::chrono::seconds(30)});
  }

  /**
   * @brief Starts the Boost.Asio IO context loop in a dedicated thread.
   *
   * Launches a new std::thread that calls ioc_.run() and stores it in the
   * ioThread_ member. The thread will block running the IO context until the
   * context is stopped; the caller is responsible for stopping the IO context
   * and joining ioThread_ (e.g., in TearDown).
   */
  void startIoContext() {
    ioThread_ = std::thread([this]() { ioc_.run(); });
  }

  net::io_context ioc_;
  std::thread ioThread_;
  std::shared_ptr<TimeoutManager> timeoutManager_;
  std::unique_ptr<ConnectionPoolManager> poolManager_;

  size_t minConnections_;
  size_t maxConnections_;
  std::chrono::seconds idleTimeout_;
};

// Test basic construction and configuration
TEST_F(ConnectionPoolManagerSimpleTest, ConstructorValidatesParameters) {
  // Valid parameters should work
  EXPECT_NO_THROW(createPoolManager());

  // Invalid parameters should throw
  EXPECT_THROW(
      {
        ConnectionPoolManager invalidPool(
            ioc_, minConnections_, maxConnections_, idleTimeout_, nullptr,
            nullptr, timeoutManager_,
            ConnectionPoolManager::MonitorConfig{nullptr},
            ConnectionPoolManager::QueueConfig{0, std::chrono::seconds(30)});
      },
      std::invalid_argument);

  EXPECT_THROW(
      {
        ConnectionPoolManager invalidPool(
            ioc_, minConnections_, maxConnections_, idleTimeout_, nullptr,
            nullptr, timeoutManager_,
            ConnectionPoolManager::MonitorConfig{nullptr},
            ConnectionPoolManager::QueueConfig{100, std::chrono::seconds(0)});
      },
      std::invalid_argument);

  EXPECT_THROW(
      {
        ConnectionPoolManager invalidPool(
            ioc_, minConnections_, maxConnections_, std::chrono::seconds(-1),
            nullptr, nullptr, timeoutManager_,
            ConnectionPoolManager::MonitorConfig{nullptr},
            ConnectionPoolManager::QueueConfig{100, std::chrono::seconds(30)});
      },
      std::invalid_argument);
}

TEST_F(ConnectionPoolManagerSimpleTest, InitialStateIsCorrect) {
  createPoolManager();

  // Test basic getters that don't require locks
  EXPECT_EQ(poolManager_->getMaxConnections(), maxConnections_);
  EXPECT_EQ(poolManager_->getMinConnections(), minConnections_);
  EXPECT_EQ(poolManager_->getIdleTimeout(), idleTimeout_);
}

TEST_F(ConnectionPoolManagerSimpleTest, CleanupTimerCanBeStartedAndStopped) {
  createPoolManager();
  startIoContext();

  // Should not throw
  EXPECT_NO_THROW(poolManager_->startCleanupTimer());
  EXPECT_NO_THROW(poolManager_->stopCleanupTimer());
}

TEST_F(ConnectionPoolManagerSimpleTest, ManualCleanupWorksWithEmptyPool) {
  createPoolManager();

  // Should return 0 since no connections to clean up
  EXPECT_EQ(poolManager_->cleanupIdleConnections(), 0);
}

TEST_F(ConnectionPoolManagerSimpleTest, ShutdownWorksCorrectly) {
  createPoolManager();

  // Should not throw
  EXPECT_NO_THROW(poolManager_->shutdown());

  // State should be reset
  EXPECT_EQ(poolManager_->getActiveConnections(), 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
  EXPECT_EQ(poolManager_->getTotalConnections(), 0);
}

TEST_F(ConnectionPoolManagerSimpleTest, StatisticsCanBeReset) {
  createPoolManager();

  // Should not throw
  EXPECT_NO_THROW(poolManager_->resetStatistics());

  // Statistics should be zero
  EXPECT_EQ(poolManager_->getTotalConnectionsCreated(), 0);
  EXPECT_EQ(poolManager_->getConnectionReuseCount(), 0);
}

TEST_F(ConnectionPoolManagerSimpleTest, ReleaseNullSessionHandledGracefully) {
  createPoolManager();

  // Should not crash or throw
  EXPECT_NO_THROW(poolManager_->releaseConnection(nullptr));

  EXPECT_EQ(poolManager_->getActiveConnections(), 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
}

/**
 * @brief Program entry point: initializes Google Test and runs all tests.
 *
 * Initializes the Google Test framework with the provided command-line
 * arguments and executes the test suite.
 *
 * @return int Result code from RUN_ALL_TESTS() (0 on success, non-zero on
 * failure).
 */
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}