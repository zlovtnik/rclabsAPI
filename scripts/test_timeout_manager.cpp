#include "logger.hpp"
#include "timeout_manager.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace net = boost::asio;

// Mock PooledSession class for testing (since it will be implemented in task 3)
// We'll use a simple class that can be used as PooledSession for testing
class PooledSession {
public:
  PooledSession(int id) : id_(id) {}
  int getId() const { return id_; }

private:
  int id_;
};

class TimeoutManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Configure logger for testing
    LogConfig config;
    config.level = LogLevel::DEBUG;
    config.consoleOutput = true;
    config.fileOutput = false;
    Logger::getInstance().configure(config);

    // Create IO context and timeout manager
    ioc_ = std::make_unique<net::io_context>();
    workGuard_ = std::make_unique<
        net::executor_work_guard<net::io_context::executor_type>>(
        ioc_->get_executor());
    timeoutManager_ = std::make_unique<TimeoutManager>(
        *ioc_, std::chrono::seconds(2), std::chrono::seconds(3));

    // Start IO context in a separate thread
    ioThread_ = std::thread([this]() { ioc_->run(); });

    // Keep sessions alive during tests
    activeSessions_.clear();
  }

  void TearDown() override {
    // Clear sessions first
    activeSessions_.clear();

    // Give some time for any pending operations
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Release work guard and stop IO context
    workGuard_.reset();
    ioc_->stop();
    if (ioThread_.joinable()) {
      ioThread_.join();
    }

    timeoutManager_.reset();
    ioc_.reset();
  }

  std::unique_ptr<net::io_context> ioc_;
  std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>>
      workGuard_;
  std::unique_ptr<TimeoutManager> timeoutManager_;
  std::thread ioThread_;
  std::vector<std::shared_ptr<PooledSession>> activeSessions_;
};

// Test basic construction and configuration
TEST_F(TimeoutManagerTest, BasicConstruction) {
  EXPECT_EQ(timeoutManager_->getConnectionTimeout(), std::chrono::seconds(2));
  EXPECT_EQ(timeoutManager_->getRequestTimeout(), std::chrono::seconds(3));
  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 0);
}

// Test timeout configuration updates
TEST_F(TimeoutManagerTest, TimeoutConfiguration) {
  timeoutManager_->setConnectionTimeout(std::chrono::seconds(10));
  timeoutManager_->setRequestTimeout(std::chrono::seconds(15));

  EXPECT_EQ(timeoutManager_->getConnectionTimeout(), std::chrono::seconds(10));
  EXPECT_EQ(timeoutManager_->getRequestTimeout(), std::chrono::seconds(15));
}

// Test connection timeout functionality
TEST_F(TimeoutManagerTest, ConnectionTimeoutBasic) {
  auto session = std::make_shared<PooledSession>(1);
  activeSessions_.push_back(session); // Keep session alive

  std::atomic<bool> timeoutCalled{false};
  std::atomic<TimeoutType> timeoutType{TimeoutType::REQUEST};

  auto callback = [&timeoutCalled, &timeoutType](
                      std::shared_ptr<PooledSession> s, TimeoutType type) {
    timeoutCalled = true;
    timeoutType = type;
  };

  timeoutManager_->startConnectionTimeout(session, callback,
                                          std::chrono::seconds(1));
  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 1);

  // Wait for timeout to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  EXPECT_TRUE(timeoutCalled);
  EXPECT_EQ(timeoutType, TimeoutType::CONNECTION);
  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
}

// Test request timeout functionality
TEST_F(TimeoutManagerTest, RequestTimeoutBasic) {
  auto session = std::make_shared<PooledSession>(2);
  activeSessions_.push_back(session); // Keep session alive

  std::atomic<bool> timeoutCalled{false};
  std::atomic<TimeoutType> timeoutType{TimeoutType::CONNECTION};

  auto callback = [&timeoutCalled, &timeoutType](
                      std::shared_ptr<PooledSession> s, TimeoutType type) {
    timeoutCalled = true;
    timeoutType = type;
  };

  timeoutManager_->startRequestTimeout(session, callback,
                                       std::chrono::seconds(1));
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 1);

  // Wait for timeout to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  EXPECT_TRUE(timeoutCalled);
  EXPECT_EQ(timeoutType, TimeoutType::REQUEST);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 0);
}

// Test timeout cancellation
TEST_F(TimeoutManagerTest, TimeoutCancellation) {
  auto session = std::make_shared<PooledSession>(3);
  std::atomic<bool> timeoutCalled{false};

  auto callback = [&timeoutCalled](std::shared_ptr<PooledSession> s,
                                   TimeoutType type) { timeoutCalled = true; };

  timeoutManager_->startConnectionTimeout(session, callback,
                                          std::chrono::seconds(2));
  timeoutManager_->startRequestTimeout(session, callback,
                                       std::chrono::seconds(2));

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 1);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 1);

  // Cancel all timeouts
  timeoutManager_->cancelTimeouts(session);

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 0);

  // Wait to ensure timeout doesn't occur
  std::this_thread::sleep_for(std::chrono::milliseconds(2200));
  EXPECT_FALSE(timeoutCalled);
}

// Test individual timeout cancellation
TEST_F(TimeoutManagerTest, IndividualTimeoutCancellation) {
  auto session = std::make_shared<PooledSession>(4);
  activeSessions_.push_back(session); // Keep session alive

  std::atomic<int> timeoutCount{0};

  auto callback = [&timeoutCount](std::shared_ptr<PooledSession> s,
                                  TimeoutType type) { timeoutCount++; };

  timeoutManager_->startConnectionTimeout(session, callback,
                                          std::chrono::seconds(2));
  timeoutManager_->startRequestTimeout(session, callback,
                                       std::chrono::seconds(1));

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 1);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 1);

  // Cancel only connection timeout
  timeoutManager_->cancelConnectionTimeout(session);

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 1);

  // Wait for request timeout to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  EXPECT_EQ(timeoutCount, 1); // Only request timeout should have occurred
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 0);
}

// Test multiple sessions
TEST_F(TimeoutManagerTest, MultipleSessions) {
  auto session1 = std::make_shared<PooledSession>(5);
  auto session2 = std::make_shared<PooledSession>(6);
  activeSessions_.push_back(session1); // Keep sessions alive
  activeSessions_.push_back(session2);

  std::atomic<int> timeoutCount{0};
  auto callback = [&timeoutCount](std::shared_ptr<PooledSession> s,
                                  TimeoutType type) { timeoutCount++; };

  timeoutManager_->startConnectionTimeout(session1, callback,
                                          std::chrono::seconds(1));
  timeoutManager_->startConnectionTimeout(session2, callback,
                                          std::chrono::seconds(1));
  timeoutManager_->startRequestTimeout(session1, callback,
                                       std::chrono::seconds(1));

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 2);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 1);

  // Wait for all timeouts to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  EXPECT_EQ(timeoutCount, 3);
  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 0);
}

// Test timer replacement (starting new timer for same session/type)
TEST_F(TimeoutManagerTest, TimerReplacement) {
  auto session = std::make_shared<PooledSession>(7);
  activeSessions_.push_back(session); // Keep session alive

  std::atomic<int> timeoutCount{0};

  auto callback = [&timeoutCount](std::shared_ptr<PooledSession> s,
                                  TimeoutType type) { timeoutCount++; };

  // Start first connection timeout
  timeoutManager_->startConnectionTimeout(session, callback,
                                          std::chrono::seconds(2));
  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 1);

  // Start second connection timeout (should replace first)
  timeoutManager_->startConnectionTimeout(session, callback,
                                          std::chrono::seconds(1));
  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 1);

  // Wait for the second (shorter) timeout to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  EXPECT_EQ(timeoutCount, 1);
  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
}

// Test default timeout callback
TEST_F(TimeoutManagerTest, DefaultTimeoutCallback) {
  auto session = std::make_shared<PooledSession>(8);
  activeSessions_.push_back(session); // Keep session alive

  // Start timeout without custom callback (should use default)
  timeoutManager_->startConnectionTimeout(session, nullptr,
                                          std::chrono::seconds(1));
  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 1);

  // Wait for timeout to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
  // Default callback should have been called (logged)
}

// Test custom default callback
TEST_F(TimeoutManagerTest, CustomDefaultCallback) {
  std::atomic<bool> customCallbackCalled{false};

  auto customCallback = [&customCallbackCalled](
                            std::shared_ptr<PooledSession> s,
                            TimeoutType type) { customCallbackCalled = true; };

  timeoutManager_->setDefaultTimeoutCallback(customCallback);

  auto session = std::make_shared<PooledSession>(9);
  activeSessions_.push_back(session); // Keep session alive

  // Start timeout without specific callback (should use custom default)
  timeoutManager_->startConnectionTimeout(session, nullptr,
                                          std::chrono::seconds(1));

  // Wait for timeout to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  EXPECT_TRUE(customCallbackCalled);
}

// Test cancel all timers
TEST_F(TimeoutManagerTest, CancelAllTimers) {
  auto session1 = std::make_shared<PooledSession>(10);
  auto session2 = std::make_shared<PooledSession>(11);

  std::atomic<int> timeoutCount{0};
  auto callback = [&timeoutCount](std::shared_ptr<PooledSession> s,
                                  TimeoutType type) { timeoutCount++; };

  timeoutManager_->startConnectionTimeout(session1, callback,
                                          std::chrono::seconds(2));
  timeoutManager_->startRequestTimeout(session1, callback,
                                       std::chrono::seconds(2));
  timeoutManager_->startConnectionTimeout(session2, callback,
                                          std::chrono::seconds(2));
  timeoutManager_->startRequestTimeout(session2, callback,
                                       std::chrono::seconds(2));

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 2);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 2);

  // Cancel all timers
  timeoutManager_->cancelAllTimers();

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 0);

  // Wait to ensure no timeouts occur
  std::this_thread::sleep_for(std::chrono::milliseconds(2200));
  EXPECT_EQ(timeoutCount, 0);
}

// Test null session handling
TEST_F(TimeoutManagerTest, NullSessionHandling) {
  std::shared_ptr<PooledSession> nullSession = nullptr;

  // These should not crash and should not create timers
  timeoutManager_->startConnectionTimeout(nullSession);
  timeoutManager_->startRequestTimeout(nullSession);
  timeoutManager_->cancelTimeouts(nullSession);
  timeoutManager_->cancelConnectionTimeout(nullSession);
  timeoutManager_->cancelRequestTimeout(nullSession);

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
  EXPECT_EQ(timeoutManager_->getActiveRequestTimers(), 0);
}

// Test exception handling in callback
TEST_F(TimeoutManagerTest, CallbackExceptionHandling) {
  auto session = std::make_shared<PooledSession>(12);
  activeSessions_.push_back(session); // Keep session alive

  auto throwingCallback = [](std::shared_ptr<PooledSession> s,
                             TimeoutType type) {
    throw std::runtime_error("Test exception");
  };

  timeoutManager_->startConnectionTimeout(session, throwingCallback,
                                          std::chrono::seconds(1));

  // Wait for timeout to occur - should not crash despite exception
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  EXPECT_EQ(timeoutManager_->getActiveConnectionTimers(), 0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}