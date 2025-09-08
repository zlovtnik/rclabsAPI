#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include "lock_utils.hpp"

// Benchmark for comparing different locking strategies
class ConcurrencyBenchmark {
public:
    ConcurrencyBenchmark(size_t numThreads, size_t iterations)
        : numThreads_(numThreads), iterations_(iterations) {}

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

    void benchmarkMutex() {
        std::cout << "Benchmarking std::mutex...\n";
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

        for (auto& thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  Mutex result: " << counter << " operations in "
                  << duration.count() << "ms\n";
        double safe_duration = std::max(duration.count(), 1LL);
        std::cout << "  Mutex throughput: " << (counter * 1000.0 / safe_duration)
                  << " ops/sec\n\n";
    }

    void benchmarkSharedMutex() {
        std::cout << "Benchmarking reader-writer mutex (read-heavy workload)...\n";
        etl_plus::StateSharedMutex mutex;
        std::atomic<size_t> counter{0};

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;
        for (size_t i = 0; i < numThreads_; ++i) {
            threads.emplace_back([&]() {
                for (size_t j = 0; j < iterations_; ++j) {
                    // Use exclusive lock for write operations
                    etl_plus::ScopedTimedLock lock(mutex);
                    counter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  Shared mutex result: " << counter.load() << " operations in "
                  << duration.count() << "ms\n";
        double safe_duration = std::max(duration.count(), 1LL);
        std::cout << "  Shared mutex throughput: " << (counter.load() * 1000.0 / safe_duration)
                  << " ops/sec\n\n";
    }

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

        for (auto& thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  Atomic result: " << counter.load() << " operations in "
                  << duration.count() << "ms\n";
        std::cout << "  Atomic throughput: " << (counter.load() * 1000.0 / duration.count())
                  << " ops/sec\n\n";
    }

    void benchmarkLockFree() {
        std::cout << "Benchmarking lock-free data structure...\n";
        // Simple lock-free counter using compare-exchange
        struct LockFreeCounter {
            std::atomic<size_t> value{0};

            void increment() {
                size_t expected = value.load(std::memory_order_relaxed);
                while (!value.compare_exchange_weak(expected, expected + 1,
                                                  std::memory_order_relaxed)) {
                    // Loop until successful
                }
            }

            size_t load() const {
                return value.load(std::memory_order_relaxed);
            }
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

        for (auto& thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  Lock-free result: " << counter.load() << " operations in "
                  << duration.count() << "ms\n";
        std::cout << "  Lock-free throughput: " << (counter.load() * 1000.0 / duration.count())
                  << " ops/sec\n\n";
    }
};

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
