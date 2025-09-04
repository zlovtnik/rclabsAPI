#include <gtest/gtest.h>
#include "connection_pool_manager.hpp"
#include "timeout_manager.hpp"
#include <boost/asio.hpp>
#include <thread>
#include <chrono>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class ConnectionPoolManagerSimpleTest : public ::testing::Test {
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
        // Use null pointers for handler and wsManager since we're only testing pool logic
        poolManager_ = std::make_unique<ConnectionPoolManager>(
            ioc_, minConnections_, maxConnections_, idleTimeout_,
            nullptr, nullptr, timeoutManager_,
            nullptr, 100, std::chrono::seconds(30)
        );
    }

    void startIoContext() {
        ioThread_ = std::thread([this]() {
            ioc_.run();
        });
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
    EXPECT_THROW({
        ConnectionPoolManager invalidPool(ioc_, 10, 5, idleTimeout_, 
                                        nullptr, nullptr, timeoutManager_,
                                        nullptr, 100, std::chrono::seconds(30));
    }, std::invalid_argument);
    
    EXPECT_THROW({
        ConnectionPoolManager invalidPool(ioc_, minConnections_, maxConnections_, 
                                        std::chrono::seconds(-1),
                                        nullptr, nullptr, timeoutManager_,
                                        nullptr, 100, std::chrono::seconds(30));
    }, std::invalid_argument);
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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}