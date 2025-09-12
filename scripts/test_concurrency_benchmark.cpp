#include "../include/lock_utils.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

// Benchmark for comparing different locking strategies
class ConcurrencyBenchmark {
public:
  /**
   * @brief Constructs a ConcurrencyBenchmark configured for a run.
   *
   * @param numThreads Number of concurrent worker threads to spawn for each
   * benchmark.
   * @param iterations Number of iterations each thread performs (total work =
   * numThreads * iterations).
   */
  ConcurrencyBenchmark(size_t numThreads, size_t iterations)
      : numThreads_(numThreads), iterations_(iterations) {}

  /**
   * @brief Run all concurrency benchmarks and print configuration header.
   *
   * Runs the four implemented benchmarks (OrderedMutex, reader-writer mutex,
   * std::atomic, and the lock-free counter) in that order, using the instance's
   * numThreads_ and iterations_ settings. Prints a brief header with the thread
   * and iteration configuration and forwards results from each benchmark to
   * standard output.
   */
  void runBenchmarks() {
    std::cout << "Running concurrency benchmarks with " << numThreads_
              << " threads and " << iterations_ << " iterations per thread\n\n";

    benchmarkMutex();
    benchmarkSharedMutex();
    benchmarkAtomic();
    benchmarkLockFree();
  }

private:
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
    std::cout << "  Lock-free throughput: "
              << (counter.load() * 1000.0 / duration.count()) << " ops/sec\n\n";
  }
};

/**
 * @brief Program entry point; runs concurrency benchmarks across multiple
 * thread counts.
 *
 * Iterates over a set of predefined thread counts, constructs a
 * ConcurrencyBenchmark for each (distributing a fixed total number of
 * iterations across threads), executes the benchmarks, and prints separators
 * between runs.
 *
 * @return int Exit status code (0 on success).
 */
int main() {
  // Run benchmarks with different thread counts
  std::vector<size_t> threadCounts = {1, 2, 4, 8};
  size_t iterations = 100000;

  for (size_t threads : threadCounts) {
    std::cout << "=== Benchmark with " << threads << " threads ===\n";
    ConcurrencyBenchmark benchmark(threads, iterations / threads);
    benchmark.runBenchmarks();
    std::cout << "\n";
  }

  return 0;
}
