#include <gtest/gtest.h>
#include "lock_utils.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <future>

namespace etl_plus {

// Test fixture for lock utilities
class LockUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test LockTimeoutException
TEST_F(LockUtilsTest, LockTimeoutException) {
    LockTimeoutException ex("Lock acquisition timeout");

    EXPECT_EQ(std::string(ex.what()), "Lock acquisition timeout");
    EXPECT_THROW(throw ex, std::runtime_error); // Should inherit from runtime_error
}

// Test DeadlockException
TEST_F(LockUtilsTest, DeadlockException) {
    DeadlockException ex("Potential deadlock detected");

    EXPECT_EQ(std::string(ex.what()), "Potential deadlock detected");
    EXPECT_THROW(throw ex, std::runtime_error); // Should inherit from runtime_error
}

// Test OrderedMutex basic functionality
TEST_F(LockUtilsTest, OrderedMutexBasic) {
    ConfigMutex mutex;

    EXPECT_FALSE(mutex.getId().empty());
    EXPECT_EQ(mutex.getLevel(), LockLevel::CONFIG);

    // Test basic locking
    {
        std::unique_lock<ConfigMutex> lock(mutex);
        EXPECT_TRUE(lock.owns_lock());
    } // Lock automatically released
}

TEST_F(LockUtilsTest, OrderedMutexUniqueIds) {
    ConfigMutex mutex1;
    ConfigMutex mutex2;

    EXPECT_NE(mutex1.getId(), mutex2.getId());
    EXPECT_EQ(mutex1.getLevel(), mutex2.getLevel());
}

// Test OrderedSharedMutex basic functionality
TEST_F(LockUtilsTest, OrderedSharedMutexBasic) {
    ConfigSharedMutex mutex;

    EXPECT_FALSE(mutex.getId().empty());
    EXPECT_EQ(mutex.getLevel(), LockLevel::CONFIG);

    // Test exclusive locking
    {
        std::unique_lock<ConfigSharedMutex> lock(mutex);
        EXPECT_TRUE(lock.owns_lock());
    }

    // Test shared locking
    {
        std::shared_lock<ConfigSharedMutex> sharedLock(mutex);
        EXPECT_TRUE(sharedLock.owns_lock());
    }
}

// Test ScopedTimedLock basic functionality
TEST_F(LockUtilsTest, ScopedTimedLockBasic) {
    ConfigMutex mutex;
    bool lockAcquired = false;

    {
        ScopedTimedLock<ConfigMutex> lock(mutex, std::chrono::milliseconds(100));
        lockAcquired = lock.owns_lock();
        EXPECT_TRUE(lockAcquired);
    } // Lock automatically released

    EXPECT_TRUE(lockAcquired);
}

TEST_F(LockUtilsTest, ScopedTimedLockTimeout) {
    ConfigMutex mutex;

    // Acquire lock in this thread
    std::unique_lock<ConfigMutex> mainLock(mutex);

    // Try to acquire with timeout in another thread
    auto future = std::async(std::launch::async, [&mutex]() {
        try {
            ScopedTimedLock<ConfigMutex> lock(mutex, std::chrono::milliseconds(50));
            return true; // Should not reach here
        } catch (const LockTimeoutException&) {
            return false; // Expected timeout
        }
    });

    bool result = future.get();
    EXPECT_FALSE(result); // Should have timed out
}

TEST_F(LockUtilsTest, ScopedTimedLockLockName) {
    ConfigMutex mutex;
    std::string customName = "test_lock";

    ScopedTimedLock<ConfigMutex> lock(mutex, std::chrono::milliseconds(100), customName);

    EXPECT_EQ(lock.getLockName(), customName);
    EXPECT_TRUE(lock.owns_lock());
}

// Test ScopedTimedSharedLock
TEST_F(LockUtilsTest, ScopedTimedSharedLockBasic) {
    ConfigSharedMutex mutex;

    {
        ScopedTimedSharedLock<ConfigSharedMutex> lock(mutex, std::chrono::milliseconds(100));
        EXPECT_TRUE(lock.owns_lock());
    } // Lock automatically released
}

TEST_F(LockUtilsTest, ScopedTimedSharedLockTimeout) {
    ConfigSharedMutex mutex;

    // Acquire exclusive lock in this thread
    std::unique_lock<ConfigSharedMutex> mainLock(mutex);

    // Try to acquire shared lock with timeout
    auto future = std::async(std::launch::async, [&mutex]() {
        try {
            ScopedTimedSharedLock<ConfigSharedMutex> lock(mutex, std::chrono::milliseconds(50));
            return true; // Should not reach here
        } catch (const LockTimeoutException&) {
            return false; // Expected timeout
        }
    });

    bool result = future.get();
    EXPECT_FALSE(result); // Should have timed out
}

// Test multiple lock levels
TEST_F(LockUtilsTest, MultipleLockLevels) {
    ConfigMutex configMutex;
    ContainerMutex containerMutex;
    ResourceMutex resourceMutex;
    StateMutex stateMutex;

    EXPECT_EQ(configMutex.getLevel(), LockLevel::CONFIG);
    EXPECT_EQ(containerMutex.getLevel(), LockLevel::CONTAINER);
    EXPECT_EQ(resourceMutex.getLevel(), LockLevel::RESOURCE);
    EXPECT_EQ(stateMutex.getLevel(), LockLevel::STATE);

    // Test that different levels work independently
    {
        ScopedTimedLock<ConfigMutex> configLock(configMutex);
        ScopedTimedLock<ContainerMutex> containerLock(containerMutex);
        ScopedTimedLock<ResourceMutex> resourceLock(resourceMutex);
        ScopedTimedLock<StateMutex> stateLock(stateMutex);

        EXPECT_TRUE(configLock.owns_lock());
        EXPECT_TRUE(containerLock.owns_lock());
        EXPECT_TRUE(resourceLock.owns_lock());
        EXPECT_TRUE(stateLock.owns_lock());
    }
}

// Test thread safety with multiple threads
TEST_F(LockUtilsTest, ThreadSafety) {
    ConfigMutex mutex;
    std::atomic<int> counter(0);
    const int numThreads = 10;
    const int iterationsPerThread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&mutex, &counter, iterationsPerThread]() {
            for (int j = 0; j < iterationsPerThread; ++j) {
                ScopedTimedLock<ConfigMutex> lock(mutex, std::chrono::milliseconds(1000));
                counter++;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(counter.load(), numThreads * iterationsPerThread);
}

// Test shared mutex reader-writer scenario
TEST_F(LockUtilsTest, SharedMutexReaderWriter) {
    ConfigSharedMutex mutex;
    std::atomic<int> sharedCounter(0);
    std::atomic<int> exclusiveCounter(0);
    const int numReaders = 5;
    const int numWriters = 2;
    const int iterations = 50;

    std::vector<std::thread> threads;

    // Reader threads
    for (int i = 0; i < numReaders; ++i) {
        threads.emplace_back([&mutex, &sharedCounter, iterations]() {
            for (int j = 0; j < iterations; ++j) {
                ScopedTimedSharedLock<ConfigSharedMutex> lock(mutex);
                sharedCounter++;
                std::this_thread::sleep_for(std::chrono::microseconds(10)); // Simulate work
            }
        });
    }

    // Writer threads
    for (int i = 0; i < numWriters; ++i) {
        threads.emplace_back([&mutex, &exclusiveCounter, iterations]() {
            for (int j = 0; j < iterations; ++j) {
                ScopedTimedLock<ConfigSharedMutex> lock(mutex);
                exclusiveCounter++;
                std::this_thread::sleep_for(std::chrono::microseconds(50)); // Simulate work
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(sharedCounter.load(), numReaders * iterations);
    EXPECT_EQ(exclusiveCounter.load(), numWriters * iterations);
}

// Test lock ordering enforcement (basic test)
TEST_F(LockUtilsTest, LockOrderingBasic) {
    // This is a basic test - in a real scenario, lock ordering would be
    // enforced by the deadlock detection system

    ConfigMutex configMutex;
    ContainerMutex containerMutex;

    // Test that we can acquire locks in correct order
    {
        ScopedTimedLock<ConfigMutex> configLock(configMutex);
        ScopedTimedLock<ContainerMutex> containerLock(containerMutex);
        // Should work fine
    }

    // Test reverse order (would be problematic in deadlock-prone code)
    // Disable deadlock detection for this test since we're intentionally violating ordering
    etl_plus::DeadlockDetector::getInstance().enableDeadlockDetection(false);
    {
        ScopedTimedLock<ContainerMutex> containerLock(containerMutex);
        ScopedTimedLock<ConfigMutex> configLock(configMutex);
        // This works in isolation but would be flagged by deadlock detector
    }
    etl_plus::DeadlockDetector::getInstance().enableDeadlockDetection(true);
}

// Test performance under load
TEST_F(LockUtilsTest, PerformanceTest) {
    ConfigMutex mutex;
    const int numIterations = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numIterations; ++i) {
        ScopedTimedLock<ConfigMutex> lock(mutex, std::chrono::milliseconds(100));
        // Minimal work inside lock
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete within reasonable time
    EXPECT_LT(duration.count(), 2000); // Less than 2 seconds for 10k iterations
}

// Test exception safety
TEST_F(LockUtilsTest, ExceptionSafety) {
    ConfigMutex mutex;
    std::atomic<bool> exceptionThrown(false);

    try {
        ScopedTimedLock<ConfigMutex> lock(mutex);
        exceptionThrown = true;
        throw std::runtime_error("Test exception");
    } catch (const std::runtime_error&) {
        // Lock should have been released automatically
        EXPECT_TRUE(exceptionThrown);

        // Should be able to acquire lock again
        ScopedTimedLock<ConfigMutex> newLock(mutex);
        EXPECT_TRUE(newLock.owns_lock());
    }
}

// Test timeout precision
TEST_F(LockUtilsTest, TimeoutPrecision) {
    ConfigMutex mutex;

    // Acquire lock
    std::unique_lock<ConfigMutex> mainLock(mutex);

    auto start = std::chrono::high_resolution_clock::now();

    try {
        ScopedTimedLock<ConfigMutex> lock(mutex, std::chrono::milliseconds(100));
        FAIL() << "Should have timed out";
    } catch (const LockTimeoutException&) {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Should be close to the timeout value (allowing some tolerance)
        EXPECT_GE(duration.count(), 90);  // At least 90ms
        EXPECT_LT(duration.count(), 200); // Less than 200ms
    }
}

// Test with different mutex types
TEST_F(LockUtilsTest, DifferentMutexTypes) {
    // Test all convenience types
    ConfigMutex config;
    ContainerMutex container;
    ResourceMutex resource;
    StateMutex state;

    ConfigSharedMutex configShared;
    ContainerSharedMutex containerShared;
    ResourceSharedMutex resourceShared;
    StateSharedMutex stateShared;

    // Test basic functionality for all types
    {
        ScopedTimedLock<ConfigMutex> l1(config);
        ScopedTimedLock<ContainerMutex> l2(container);
        ScopedTimedLock<ResourceMutex> l3(resource);
        ScopedTimedLock<StateMutex> l4(state);

        EXPECT_TRUE(l1.owns_lock());
        EXPECT_TRUE(l2.owns_lock());
        EXPECT_TRUE(l3.owns_lock());
        EXPECT_TRUE(l4.owns_lock());
    }

    {
        ScopedTimedSharedLock<ConfigSharedMutex> l1(configShared);
        ScopedTimedSharedLock<ContainerSharedMutex> l2(containerShared);
        ScopedTimedSharedLock<ResourceSharedMutex> l3(resourceShared);
        ScopedTimedSharedLock<StateSharedMutex> l4(stateShared);

        EXPECT_TRUE(l1.owns_lock());
        EXPECT_TRUE(l2.owns_lock());
        EXPECT_TRUE(l3.owns_lock());
        EXPECT_TRUE(l4.owns_lock());
    }
}

// Test concurrent access patterns
TEST_F(LockUtilsTest, ConcurrentAccessPatterns) {
    ConfigMutex mutex;
    std::vector<int> results;
    std::mutex resultsMutex;
    const int numThreads = 20;

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i, &mutex, &results, &resultsMutex]() {
            ScopedTimedLock<ConfigMutex> lock(mutex);
            std::lock_guard<std::mutex> resultsLock(resultsMutex);
            results.push_back(i);
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All threads should have completed
    EXPECT_EQ(results.size(), numThreads);

    // Results should contain all thread IDs (order may vary)
    std::sort(results.begin(), results.end());
    for (int i = 0; i < numThreads; ++i) {
        EXPECT_EQ(results[i], i);
    }
}

} // namespace etl_plus
