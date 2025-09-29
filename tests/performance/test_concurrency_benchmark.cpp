#include "../include/lock_utils.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <tuple>
#include <vector>

// Benchmark for comparing different locking strategies
class ConcurrencyBenchmarkTest
    : public ::testing::TestWithParam<std::tuple<size_t, size_t>> {
protected:
  void SetUp() override {
    std::tie(numThreads_, iterations_) = GetParam();
    std::cout << "\n[   CONFIG  ] Threads: " << numThreads_
              << ", Iterations per thread: " << iterations_ << "\n";
  }

  size_t numThreads_;
  size_t iterations_;

  /**
   * @brief Run a throughput benchmark using an OrderedMutex
   * (etl_plus::StateMutex).
   *
   * Spawns numThreads_ threads; each thread performs iterations_ increments of
   * a local counter while holding an etl_plus::ScopedTimedLock on the shared
   * mutex. Measures wall-clock time with a high-resolution clock, prints the
   * total completed operations and elapsed milliseconds, and prints throughput
   * in operations/sec.
   *
   * Notes:
   * - Uses the class members numThreads_ and iterations_ to drive the workload.
   * - Outputs results to stdout.
   */
  void benchmarkMutex() {
    std::cout << "Benchmarking OrderedMutex...\n";
    etl_plus::StateMutex mutex;
    size_t counter = 0;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (size_t i = 0; i < numThreads_; ++i) {
      threads.emplace_back([&]() {
        for (size_t j = 0; j < iterations_; ++j) {
          etl_plus::ScopedTimedLock lock(mutex);
          ++counter;
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "  OrderedMutex result: " << counter << " operations in "
              << duration.count() << "ms\n";
    double safe_duration = std::max(duration.count(), 1LL);
    std::cout << "  OrderedMutex throughput: "
              << (counter * 1000.0 / safe_duration) << " ops/sec\n\n";
  }

  /**
   * @brief Run a read-heavy benchmark using a reader-writer (shared) mutex.
   *
   * Measures throughput of concurrent shared (read) locking by spawning
   * numThreads_ threads where each thread acquires a scoped shared lock
   * iterations_ times and increments an atomic counter. Elapsed time is
   * measured with a high-resolution clock, the final operation count and
   * elapsed milliseconds are printed to stdout, and throughput (ops/sec)
   * is computed and printed.
   *
   * Side effects:
   * - Writes results to std::cout.
   * - Uses member variables numThreads_ and iterations_ to control workload.
   */
  void benchmarkSharedMutex() {
    std::cout << "Benchmarking reader-writer mutex (read-heavy workload)...\n";
    etl_plus::StateSharedMutex mutex;
    std::atomic<size_t> counter{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (size_t i = 0; i < numThreads_; ++i) {
      threads.emplace_back([&]() {
        for (size_t j = 0; j < iterations_; ++j) {
          etl_plus::ScopedTimedSharedLock lock(mutex);
          counter.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "  Shared mutex result: " << counter.load()
              << " operations in " << duration.count() << "ms\n";
    double safe_duration = std::max(duration.count(), 1LL);
    std::cout << "  Shared mutex throughput: "
              << (counter.load() * 1000.0 / safe_duration) << " ops/sec\n\n";
  }

  /**
   * @brief Benchmarks incrementing a std::atomic counter from multiple threads.
   *
   * Runs numThreads_ threads, each performing iterations_ relaxed atomic
   * increments, measures elapsed time with a high-resolution clock, and prints
   * the final count and throughput (ops/sec) to standard output.
   *
   * @details
   * The function measures end-to-end time including thread creation and join.
   * Throughput calculation avoids division by zero by treating durations <1ms
   * as 1ms.
   */
  void benchmarkAtomic() {
    std::cout << "Benchmarking std::atomic...\n";
    std::atomic<size_t> counter{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (size_t i = 0; i < numThreads_; ++i) {
      threads.emplace_back([&]() {
        for (size_t j = 0; j < iterations_; ++j) {
          counter.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "  Atomic result: " << counter.load() << " operations in "
              << duration.count() << "ms\n";
    double safe_duration = std::max(duration.count(), 1LL);
    std::cout << "  Atomic throughput: "
              << (counter.load() * 1000.0 / safe_duration) << " ops/sec\n\n";
  }

  /**
   * @brief Run a lock-free counter benchmark and print results.
   *
   * Runs a multi-threaded benchmark that increments a simple lock-free counter
   * (an internal struct with a std::atomic<size_t> using relaxed ordering)
   * numThreads_ times concurrently, each performing iterations_ increments.
   * Measures elapsed time with a high-resolution clock and prints the total
   * operations and throughput (operations per second) to standard output.
   *
   * Notes:
   * - Uses relaxed atomic operations (memory_order_relaxed) for increment and
   * load.
   * - Relies on the class members numThreads_ and iterations_ for workload
   * size.
   */
  void benchmarkLockFree() {
    std::cout << "Benchmarking lock-free data structure...\n";
    // Simple lock-free counter using compare-exchange
    struct LockFreeCounter {
      std::atomic<size_t> value{0};

      void increment() { value.fetch_add(1, std::memory_order_relaxed); }

      size_t load() const { return value.load(std::memory_order_relaxed); }
    };

    LockFreeCounter counter;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (size_t i = 0; i < numThreads_; ++i) {
      threads.emplace_back([&]() {
        for (size_t j = 0; j < iterations_; ++j) {
          counter.increment();
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "  Lock-free result: " << counter.load() << " operations in "
              << duration.count() << "ms\n";
    double safe_duration = std::max(duration.count(), 1LL);
    std::cout << "  Lock-free throughput: "
              << (counter.load() * 1000.0 / safe_duration) << " ops/sec\n\n";
  }
};

// Test cases for each benchmark type
TEST_P(ConcurrencyBenchmarkTest, MutexBenchmark) { benchmarkMutex(); }

TEST_P(ConcurrencyBenchmarkTest, SharedMutexBenchmark) {
  benchmarkSharedMutex();
}

TEST_P(ConcurrencyBenchmarkTest, AtomicBenchmark) { benchmarkAtomic(); }

TEST_P(ConcurrencyBenchmarkTest, LockFreeBenchmark) { benchmarkLockFree(); }

// Define test parameters: (num_threads, iterations_per_thread)
INSTANTIATE_TEST_SUITE_P(
    ConcurrencyBenchmarks, ConcurrencyBenchmarkTest,
    ::testing::Values(std::make_tuple(1, 100000), std::make_tuple(2, 50000),
                      std::make_tuple(4, 25000), std::make_tuple(8, 12500)),
    [](const ::testing::TestParamInfo<ConcurrencyBenchmarkTest::ParamType>
           &info) {
      auto [threads, iterations] = info.param;
      return "Threads" + std::to_string(threads) + "_Iters" +
             std::to_string(iterations);
    });

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
