#include "connection_pool_manager.hpp"
#include "timeout_manager.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class ConnectionPoolManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    timeoutManager_ = std::make_shared<TimeoutManager>(ioc_);

    // Default pool configuration
    minConnections_ = 2;
    maxConnections_ = 5;
    idleTimeout_ = std::chrono::seconds(10);
  }

  void TearDown() override {
    if (poolManager_) {
      poolManager_->shutdown();
    }
    ioc_.stop();
    if (ioThread_.joinable()) {
      ioThread_.join();
    }
  }

  void createPoolManager() {
    // Create null pointers for handler and wsManager since we're testing pool
    // logic
    std::shared_ptr<void> handler = nullptr;
    std::shared_ptr<void> wsManager = nullptr;

    poolManager_ = std::make_unique<ConnectionPoolManager>(
        ioc_, minConnections_, maxConnections_, idleTimeout_, handler,
        wsManager, timeoutManager_);
  }

  void startIoContext() {
    ioThread_ = std::thread([this]() { ioc_.run(); });
  }

  tcp::socket createSocket() { return tcp::socket(ioc_); }

  net::io_context ioc_;
  std::thread ioThread_;
  std::shared_ptr<TimeoutManager> timeoutManager_;
  std::unique_ptr<ConnectionPoolManager> poolManager_;

  size_t minConnections_;
  size_t maxConnections_;
  std::chrono::seconds idleTimeout_;
};

// Test basic construction and configuration
TEST_F(ConnectionPoolManagerTest, ConstructorValidatesParameters) {
  // Valid parameters should work
  EXPECT_NO_THROW(createPoolManager());

  // Invalid parameters should throw
  EXPECT_THROW(
      {
        ConnectionPoolManager invalidPool(ioc_, 10, 5, idleTimeout_, handler_,
                                          wsManager_, timeoutManager_);
      },
      std::invalid_argument);

  EXPECT_THROW(
      {
        ConnectionPoolManager invalidPool(
            ioc_, minConnections_, maxConnections_, std::chrono::seconds(-1),
            handler_, wsManager_, timeoutManager_);
      },
      std::invalid_argument);
}

TEST_F(ConnectionPoolManagerTest, InitialStateIsCorrect) {
  createPoolManager();

  EXPECT_EQ(poolManager_->getActiveConnections(), 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
  EXPECT_EQ(poolManager_->getTotalConnections(), 0);
  EXPECT_EQ(poolManager_->getMaxConnections(), maxConnections_);
  EXPECT_EQ(poolManager_->getMinConnections(), minConnections_);
  EXPECT_EQ(poolManager_->getIdleTimeout(), idleTimeout_);
  EXPECT_FALSE(poolManager_->isAtMaxCapacity());
  EXPECT_EQ(poolManager_->getConnectionReuseCount(), 0);
  EXPECT_EQ(poolManager_->getTotalConnectionsCreated(), 0);
}

// Test connection acquisition
TEST_F(ConnectionPoolManagerTest, AcquireConnectionCreatesNewSession) {
  createPoolManager();
  startIoContext();

  auto socket = createSocket();
  auto session = poolManager_->acquireConnection(std::move(socket));

  EXPECT_NE(session, nullptr);
  EXPECT_EQ(poolManager_->getActiveConnections(), 1);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
  EXPECT_EQ(poolManager_->getTotalConnections(), 1);
  EXPECT_EQ(poolManager_->getTotalConnectionsCreated(), 1);
  EXPECT_EQ(poolManager_->getConnectionReuseCount(), 0);
}

TEST_F(ConnectionPoolManagerTest, AcquireMultipleConnectionsUpToMax) {
  createPoolManager();
  startIoContext();

  std::vector<std::shared_ptr<PooledSession>> sessions;

  // Acquire up to max connections
  for (size_t i = 0; i < maxConnections_; ++i) {
    auto socket = createSocket();
    auto session = poolManager_->acquireConnection(std::move(socket));
    sessions.push_back(session);

    EXPECT_EQ(poolManager_->getActiveConnections(), i + 1);
    EXPECT_EQ(poolManager_->getTotalConnectionsCreated(), i + 1);
  }

  EXPECT_TRUE(poolManager_->isAtMaxCapacity());
  EXPECT_EQ(poolManager_->getTotalConnections(), maxConnections_);
}

// Test connection release and reuse
TEST_F(ConnectionPoolManagerTest, ReleaseConnectionMakesItIdle) {
  createPoolManager();
  startIoContext();

  auto socket = createSocket();
  auto session = poolManager_->acquireConnection(std::move(socket));

  // Mock the session to be idle when released
  session->setIdle(true);

  poolManager_->releaseConnection(session);

  EXPECT_EQ(poolManager_->getActiveConnections(), 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), 1);
  EXPECT_EQ(poolManager_->getTotalConnections(), 1);
}

TEST_F(ConnectionPoolManagerTest, ReuseIdleConnection) {
  createPoolManager();
  startIoContext();

  // Create and release a connection
  auto socket1 = createSocket();
  auto session1 = poolManager_->acquireConnection(std::move(socket1));
  session1->setIdle(true);
  poolManager_->releaseConnection(session1);

  // Acquire another connection - should reuse the idle one
  auto socket2 = createSocket();
  auto session2 = poolManager_->acquireConnection(std::move(socket2));

  EXPECT_EQ(session1, session2); // Should be the same session object
  EXPECT_EQ(poolManager_->getActiveConnections(), 1);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
  EXPECT_EQ(poolManager_->getTotalConnectionsCreated(), 1); // Only one created
  EXPECT_EQ(poolManager_->getConnectionReuseCount(), 1);    // One reuse
}

// Test concurrent access
TEST_F(ConnectionPoolManagerTest, ConcurrentAcquisitionIsThreadSafe) {
  createPoolManager();
  startIoContext();

  const int numThreads = 10;
  const int connectionsPerThread = 2;
  std::vector<std::future<std::vector<std::shared_ptr<PooledSession>>>> futures;

  // Launch multiple threads to acquire connections concurrently
  for (int i = 0; i < numThreads; ++i) {
    futures.push_back(
        std::async(std::launch::async, [this, connectionsPerThread]() {
          std::vector<std::shared_ptr<PooledSession>> sessions;
          for (int j = 0; j < connectionsPerThread; ++j) {
            try {
              auto socket = createSocket();
              auto session = poolManager_->acquireConnection(std::move(socket));
              sessions.push_back(session);
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } catch (const std::exception &e) {
              // Expected when pool is at capacity
            }
          }
          return sessions;
        }));
  }

  // Wait for all threads to complete
  size_t totalAcquired = 0;
  for (auto &future : futures) {
    auto sessions = future.get();
    totalAcquired += sessions.size();
  }

  // Should not exceed max connections
  EXPECT_LE(poolManager_->getTotalConnections(), maxConnections_);
  EXPECT_LE(totalAcquired, maxConnections_);
}

TEST_F(ConnectionPoolManagerTest, ConcurrentReleaseIsThreadSafe) {
  createPoolManager();
  startIoContext();

  // First acquire some connections
  std::vector<std::shared_ptr<PooledSession>> sessions;
  for (size_t i = 0; i < maxConnections_; ++i) {
    auto socket = createSocket();
    auto session = poolManager_->acquireConnection(std::move(socket));
    session->setIdle(true); // Mock as idle for release
    sessions.push_back(session);
  }

  // Release them concurrently
  std::vector<std::future<void>> futures;
  for (auto &session : sessions) {
    futures.push_back(std::async(std::launch::async, [this, session]() {
      poolManager_->releaseConnection(session);
    }));
  }

  // Wait for all releases to complete
  for (auto &future : futures) {
    future.wait();
  }

  EXPECT_EQ(poolManager_->getActiveConnections(), 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), maxConnections_);
}

// Test idle connection cleanup
TEST_F(ConnectionPoolManagerTest, CleanupRemovesExpiredConnections) {
  // Use short timeout for testing
  idleTimeout_ = std::chrono::seconds(1);
  createPoolManager();
  startIoContext();

  // Create and release a connection
  auto socket = createSocket();
  auto session = poolManager_->acquireConnection(std::move(socket));
  session->setIdle(true);
  poolManager_->releaseConnection(session);

  EXPECT_EQ(poolManager_->getIdleConnections(), 1);

  // Wait for timeout to expire
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Manually trigger cleanup
  size_t cleanedUp = poolManager_->cleanupIdleConnections();

  EXPECT_GT(cleanedUp, 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
}

TEST_F(ConnectionPoolManagerTest, CleanupTimerWorksAutomatically) {
  // Use short timeout for testing
  idleTimeout_ = std::chrono::seconds(1);
  createPoolManager();
  startIoContext();

  // Start the cleanup timer
  poolManager_->startCleanupTimer();

  // Create and release a connection
  auto socket = createSocket();
  auto session = poolManager_->acquireConnection(std::move(socket));
  session->setIdle(true);
  poolManager_->releaseConnection(session);

  EXPECT_EQ(poolManager_->getIdleConnections(), 1);

  // Wait for automatic cleanup (should happen within 2 * idleTimeout)
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Check that cleanup occurred
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
}

// Test error handling
TEST_F(ConnectionPoolManagerTest, ReleaseNullSessionHandledGracefully) {
  createPoolManager();

  // Should not crash or throw
  EXPECT_NO_THROW(poolManager_->releaseConnection(nullptr));

  EXPECT_EQ(poolManager_->getActiveConnections(), 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
}

TEST_F(ConnectionPoolManagerTest, ReleaseUnknownSessionHandledGracefully) {
  createPoolManager();
  startIoContext();

  // Create a session outside the pool
  auto socket = createSocket();
  auto session = std::make_shared<PooledSession>(std::move(socket), handler_,
                                                 wsManager_, timeoutManager_);

  // Should not crash or throw
  EXPECT_NO_THROW(poolManager_->releaseConnection(session));

  EXPECT_EQ(poolManager_->getActiveConnections(), 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
}

// Test shutdown behavior
TEST_F(ConnectionPoolManagerTest, ShutdownClearsAllConnections) {
  createPoolManager();
  startIoContext();

  // Acquire some connections
  std::vector<std::shared_ptr<PooledSession>> sessions;
  for (size_t i = 0; i < 3; ++i) {
    auto socket = createSocket();
    auto session = poolManager_->acquireConnection(std::move(socket));
    sessions.push_back(session);
  }

  // Release one to idle
  sessions[0]->setIdle(true);
  poolManager_->releaseConnection(sessions[0]);

  EXPECT_GT(poolManager_->getTotalConnections(), 0);

  // Shutdown should clear everything
  poolManager_->shutdown();

  EXPECT_EQ(poolManager_->getActiveConnections(), 0);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
  EXPECT_EQ(poolManager_->getTotalConnections(), 0);
}

TEST_F(ConnectionPoolManagerTest, AcquisitionAfterShutdownThrows) {
  createPoolManager();
  startIoContext();

  poolManager_->shutdown();

  auto socket = createSocket();
  EXPECT_THROW(poolManager_->acquireConnection(std::move(socket)),
               std::runtime_error);
}

// Test statistics
TEST_F(ConnectionPoolManagerTest, StatisticsAreAccurate) {
  createPoolManager();
  startIoContext();

  // Create, release, and reuse connections
  auto socket1 = createSocket();
  auto session1 = poolManager_->acquireConnection(std::move(socket1));
  session1->setIdle(true);
  poolManager_->releaseConnection(session1);

  auto socket2 = createSocket();
  auto session2 =
      poolManager_->acquireConnection(std::move(socket2)); // Should reuse

  auto socket3 = createSocket();
  auto session3 =
      poolManager_->acquireConnection(std::move(socket3)); // Should create new

  EXPECT_EQ(poolManager_->getTotalConnectionsCreated(), 2);
  EXPECT_EQ(poolManager_->getConnectionReuseCount(), 1);
  EXPECT_EQ(poolManager_->getActiveConnections(), 2);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
}

TEST_F(ConnectionPoolManagerTest, StatisticsCanBeReset) {
  createPoolManager();
  startIoContext();

  // Generate some statistics
  auto socket = createSocket();
  auto session = poolManager_->acquireConnection(std::move(socket));
  session->setIdle(true);
  poolManager_->releaseConnection(session);

  auto socket2 = createSocket();
  auto session2 = poolManager_->acquireConnection(std::move(socket2));

  EXPECT_GT(poolManager_->getTotalConnectionsCreated(), 0);
  EXPECT_GT(poolManager_->getConnectionReuseCount(), 0);

  // Reset statistics
  poolManager_->resetStatistics();

  EXPECT_EQ(poolManager_->getTotalConnectionsCreated(), 0);
  EXPECT_EQ(poolManager_->getConnectionReuseCount(), 0);

  // Pool state should remain unchanged
  EXPECT_EQ(poolManager_->getActiveConnections(), 1);
  EXPECT_EQ(poolManager_->getIdleConnections(), 0);
}

// Integration test with timeout scenarios
TEST_F(ConnectionPoolManagerTest, IntegrationWithTimeoutManager) {
  createPoolManager();
  startIoContext();

  auto socket = createSocket();
  auto session = poolManager_->acquireConnection(std::move(socket));

  // Verify that the session has the timeout manager
  EXPECT_NE(session, nullptr);

  // The session should be able to handle timeouts
  // (This is more of an integration verification)
  session->setIdle(true);
  poolManager_->releaseConnection(session);

  EXPECT_EQ(poolManager_->getIdleConnections(), 1);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}