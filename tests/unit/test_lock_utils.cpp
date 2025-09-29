#include "../include/lock_utils.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace etl_plus;

// Test basic RAII lock functionality
void testBasicLocking() {
  std::cout << "\n=== Testing Basic RAII Locking ===" << std::endl;

  std::timed_mutex testMutex;

  {
    ScopedTimedLock lock(testMutex, std::chrono::milliseconds(1000),
                         "test_mutex");
    assert(lock.owns_lock());
    std::cout << "✓ Successfully acquired lock: " << lock.getLockName()
              << std::endl;

    // Simulate some work
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cout << "✓ Lock automatically released" << std::endl;
}

// Test lock timeout functionality
void testLockTimeout() {
  std::cout << "\n=== Testing Lock Timeout ===" << std::endl;

  std::timed_mutex testMutex;

  // First thread holds the lock
  std::thread holder([&testMutex]() {
    ScopedTimedLock lock(testMutex, std::chrono::milliseconds(5000),
                         "holder_lock");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  });

  // Give the holder thread time to acquire the lock
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Second thread tries to acquire with short timeout
  bool timeoutCaught = false;
  try {
    ScopedTimedLock lock(testMutex, std::chrono::milliseconds(100),
                         "timeout_test");
  } catch (const LockTimeoutException &e) {
    timeoutCaught = true;
    std::cout << "✓ Timeout exception caught: " << e.what() << std::endl;
  }

  holder.join();
  assert(timeoutCaught);
  std::cout << "✓ Lock timeout functionality working correctly" << std::endl;
}

// Test ordered mutex and deadlock prevention
void testOrderedMutex() {
  std::cout << "\n=== Testing Ordered Mutex and Deadlock Prevention ==="
            << std::endl;

  ConfigMutex configMutex;
  ContainerMutex containerMutex;
  ResourceMutex resourceMutex;

  // Test correct ordering (should work)
  {
    ScopedTimedLock configLock(configMutex, std::chrono::milliseconds(1000),
                               "config");
    ScopedTimedLock containerLock(containerMutex,
                                  std::chrono::milliseconds(1000), "container");
    ScopedTimedLock resourceLock(resourceMutex, std::chrono::milliseconds(1000),
                                 "resource");

    std::cout << "✓ Correct lock ordering succeeded" << std::endl;
  }

  // Test incorrect ordering (should throw)
  bool orderingViolationCaught = false;
  try {
    ScopedTimedLock resourceLock(resourceMutex, std::chrono::milliseconds(1000),
                                 "resource_first");
    ScopedTimedLock configLock(configMutex, std::chrono::milliseconds(1000),
                               "config_second");
  } catch (const DeadlockException &e) {
    orderingViolationCaught = true;
    std::cout << "✓ Lock ordering violation caught: " << e.what() << std::endl;
  }

  assert(orderingViolationCaught);
  std::cout << "✓ Lock ordering enforcement working correctly" << std::endl;
}

// Test shared mutex functionality
void testSharedMutex() {
  std::cout << "\n=== Testing Shared Mutex Functionality ===" << std::endl;

  ResourceSharedMutex sharedMutex;
  std::atomic<int> readerCount{0};
  std::atomic<bool> writerActive{false};

  // Start multiple readers
  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i) {
    readers.emplace_back([&sharedMutex, &readerCount, &writerActive, i]() {
      ScopedTimedSharedLock lock(sharedMutex, std::chrono::milliseconds(1000),
                                 "reader_" + std::to_string(i));

      readerCount.fetch_add(1);
      assert(!writerActive.load()); // No writer should be active

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      readerCount.fetch_sub(1);
    });
  }

  // Give readers time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Start a writer (should wait for readers to finish)
  std::thread writer([&sharedMutex, &readerCount, &writerActive]() {
    ScopedTimedLock lock(sharedMutex, std::chrono::milliseconds(2000),
                         "writer");

    assert(readerCount.load() == 0); // All readers should be done
    writerActive.store(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    writerActive.store(false);
  });

  for (auto &reader : readers) {
    reader.join();
  }
  writer.join();

  std::cout << "✓ Shared mutex reader-writer coordination working correctly"
            << std::endl;
}

// Test lock monitoring and statistics
void testLockMonitoring() {
  std::cout << "\n=== Testing Lock Monitoring ===" << std::endl;

  LockMonitor::getInstance().enableDetailedLogging(true);
  LockMonitor::getInstance().reset();

  std::timed_mutex testMutex;

  // Perform several lock operations
  for (int i = 0; i < 5; ++i) {
    ScopedTimedLock lock(testMutex, std::chrono::milliseconds(1000),
                         "monitored_lock");
    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Small delay
  }

  // Check statistics
  auto stats = LockMonitor::getInstance().getLockStats("monitored_lock");
  assert(stats.acquisitions.load() == 5);
  assert(stats.failures.load() == 0);

  std::cout << "✓ Lock statistics:" << std::endl;
  std::cout << "  - Acquisitions: " << stats.acquisitions.load() << std::endl;
  std::cout << "  - Failures: " << stats.failures.load() << std::endl;
  std::cout << "  - Average wait time: " << stats.getAverageWaitTime() << "μs"
            << std::endl;
  std::cout << "  - Max wait time: " << stats.maxWaitTime.load() << "μs"
            << std::endl;
  std::cout << "  - Contentions: " << stats.contentions.load() << std::endl;

  LockMonitor::getInstance().enableDetailedLogging(false);
  std::cout << "✓ Lock monitoring working correctly" << std::endl;
}

// Test concurrent access patterns
void testConcurrentAccess() {
  std::cout << "\n=== Testing Concurrent Access Patterns ===" << std::endl;

  ContainerMutex containerMutex;
  std::atomic<int> counter{0};
  std::atomic<int> maxConcurrent{0};
  std::atomic<int> currentConcurrent{0};

  std::vector<std::thread> workers;

  // Create multiple worker threads
  for (int i = 0; i < 10; ++i) {
    workers.emplace_back([&containerMutex, &counter, &maxConcurrent,
                          &currentConcurrent, i]() {
      for (int j = 0; j < 5; ++j) {
        ScopedTimedLock lock(containerMutex, std::chrono::milliseconds(2000),
                             "worker_" + std::to_string(i));

        int current = currentConcurrent.fetch_add(1) + 1;
        int expected = maxConcurrent.load();
        while (current > expected &&
               !maxConcurrent.compare_exchange_weak(expected, current)) {
          // Keep trying to update max
        }

        counter.fetch_add(1);

        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        currentConcurrent.fetch_sub(1);
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  assert(counter.load() == 50); // 10 workers * 5 iterations
  assert(maxConcurrent.load() ==
         1); // Mutex should ensure only 1 concurrent access

  std::cout << "✓ Concurrent access properly serialized" << std::endl;
  std::cout << "  - Total operations: " << counter.load() << std::endl;
  std::cout << "  - Max concurrent: " << maxConcurrent.load() << std::endl;
}

// Test performance under load
void testPerformanceUnderLoad() {
  std::cout << "\n=== Testing Performance Under Load ===" << std::endl;

  ResourceMutex resourceMutex;
  std::atomic<uint64_t> operationCount{0};

  auto startTime = std::chrono::high_resolution_clock::now();

  std::vector<std::thread> workers;
  const int numWorkers = 4;
  const int operationsPerWorker = 1000;

  for (int i = 0; i < numWorkers; ++i) {
    workers.emplace_back(
        [&resourceMutex, &operationCount, operationsPerWorker]() {
          for (int j = 0; j < operationsPerWorker; ++j) {
            ScopedTimedLock lock(resourceMutex, std::chrono::milliseconds(100),
                                 "perf_test");
            operationCount.fetch_add(1);
            // Minimal work to test lock overhead
          }
        });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      endTime - startTime);

  uint64_t totalOps = operationCount.load();
  double opsPerSecond = (totalOps * 1000.0) / duration.count();

  std::cout << "✓ Performance test completed:" << std::endl;
  std::cout << "  - Total operations: " << totalOps << std::endl;
  std::cout << "  - Duration: " << duration.count() << "ms" << std::endl;
  std::cout << "  - Operations per second: " << opsPerSecond << std::endl;

  assert(totalOps == numWorkers * operationsPerWorker);
}

// Test convenience macros
void testConvenienceMacros() {
  std::cout << "\n=== Testing Convenience Macros ===" << std::endl;

  StateMutex stateMutex;
  StateSharedMutex stateSharedMutex;

  {
    SCOPED_LOCK(stateMutex);
    std::cout << "✓ SCOPED_LOCK macro working" << std::endl;
  }

  {
    SCOPED_LOCK_TIMEOUT(stateMutex, 500);
    std::cout << "✓ SCOPED_LOCK_TIMEOUT macro working" << std::endl;
  }

  {
    SCOPED_SHARED_LOCK(stateSharedMutex);
    std::cout << "✓ SCOPED_SHARED_LOCK macro working" << std::endl;
  }

  {
    SCOPED_SHARED_LOCK_TIMEOUT(stateSharedMutex, 500);
    std::cout << "✓ SCOPED_SHARED_LOCK_TIMEOUT macro working" << std::endl;
  }
}

int main() {
  std::cout << "Lock Utils Test Suite" << std::endl;
  std::cout << "=====================" << std::endl;

  try {
    testBasicLocking();
    testLockTimeout();
    testOrderedMutex();
    testSharedMutex();
    testLockMonitoring();
    testConcurrentAccess();
    testPerformanceUnderLoad();
    testConvenienceMacros();

    std::cout << "\n🎉 All tests passed successfully!" << std::endl;

    // Display final lock statistics
    std::cout << "\n=== Final Lock Statistics ===" << std::endl;
    auto allStats = LockMonitor::getInstance().getAllStats();
    for (const auto &[lockName, stats] : allStats) {
      std::cout << "Lock '" << lockName << "':" << std::endl;
      std::cout << "  - Acquisitions: " << stats.acquisitions.load()
                << std::endl;
      std::cout << "  - Failures: " << stats.failures.load() << std::endl;
      std::cout << "  - Avg wait time: " << stats.getAverageWaitTime() << "μs"
                << std::endl;
      std::cout << "  - Failure rate: " << (stats.getFailureRate() * 100) << "%"
                << std::endl;
    }

    return 0;

  } catch (const std::exception &e) {
    std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}