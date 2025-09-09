#include "performance_benchmark.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <map>
#include <string>
#include <sstream>
#include <fstream>

// Memory usage tracking utilities
class MemoryTracker {
public:
    static size_t getCurrentMemoryUsage() {
        // Platform-specific memory usage detection
        // This is a simplified version - in production you'd use platform APIs
        std::ifstream statm("/proc/self/statm");
        if (statm.is_open()) {
            size_t size, resident, share, text, lib, data, dt;
            statm >> size >> resident >> share >> text >> lib >> data >> dt;
            return resident * getpagesize() / 1024; // KB
        }
        return 0; // Fallback
    }

    static size_t getPeakMemoryUsage() {
        // Simplified peak memory tracking
        return getCurrentMemoryUsage();
    }
};

// Memory performance benchmark
class MemoryBenchmark : public BenchmarkBase {
public:
    MemoryBenchmark() : BenchmarkBase("Memory") {}

    void run() override {
        benchmarkMemoryAllocation();
        benchmarkMemoryLeakDetection();
        benchmarkObjectPooling();
        benchmarkCacheEfficiency();
    }

private:
    void benchmarkMemoryAllocation() {
        std::cout << "Running memory allocation benchmark...\n";

        const size_t numAllocations = 10000;
        const size_t allocationSize = 1024; // 1KB per allocation

        size_t initialMemory = MemoryTracker::getCurrentMemoryUsage();

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::unique_ptr<char[]>> allocations;
        for (size_t i = 0; i < numAllocations; ++i) {
            allocations.emplace_back(new char[allocationSize]);
            // Fill with some data to ensure allocation
            std::fill_n(allocations.back().get(), allocationSize, 'A' + (i % 26));
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        size_t finalMemory = MemoryTracker::getCurrentMemoryUsage();
        size_t memoryDelta = finalMemory - initialMemory;

        // Cleanup
        allocations.clear();

        addResult(createResult("Memory Allocation", numAllocations, duration,
                              "Allocated " + std::to_string(memoryDelta) + " KB"));
    }

    void benchmarkMemoryLeakDetection() {
        std::cout << "Running memory leak detection benchmark...\n";

        const size_t numCycles = 1000;
        size_t initialMemory = MemoryTracker::getCurrentMemoryUsage();

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < numCycles; ++i) {
            // Simulate object creation and destruction
            auto tempObject = std::make_shared<std::string>("Temporary object " + std::to_string(i));
            std::vector<int> tempVector(100, i);

            // Simulate some processing
            std::this_thread::sleep_for(std::chrono::microseconds(50));

            // Objects go out of scope here
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        size_t finalMemory = MemoryTracker::getCurrentMemoryUsage();
        size_t memoryDelta = finalMemory - initialMemory;

        addResult(createResult("Memory Leak Detection", numCycles, duration,
                              "Memory delta: " + std::to_string(memoryDelta) + " KB"));
    }

    void benchmarkObjectPooling() {
        std::cout << "Running object pooling benchmark...\n";

        const size_t poolSize = 100;
        const size_t numOperations = 5000;

        // Simple object pool implementation for benchmarking
        std::vector<std::unique_ptr<std::string>> pool;
        std::queue<size_t> availableIndices;

        // Initialize pool
        for (size_t i = 0; i < poolSize; ++i) {
            pool.emplace_back(std::make_unique<std::string>("Pooled object " + std::to_string(i)));
            availableIndices.push(i);
        }

        size_t initialMemory = MemoryTracker::getCurrentMemoryUsage();

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < numOperations; ++i) {
            if (!availableIndices.empty()) {
                size_t index = availableIndices.front();
                availableIndices.pop();

                // Use the object
                *pool[index] = "Modified object " + std::to_string(i);

                // Return to pool
                availableIndices.push(index);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        size_t finalMemory = MemoryTracker::getCurrentMemoryUsage();
        size_t memoryDelta = finalMemory - initialMemory;

        addResult(createResult("Object Pooling", numOperations, duration,
                              "Pool size: " + std::to_string(poolSize) +
                              ", Memory delta: " + std::to_string(memoryDelta) + " KB"));
    }

    void benchmarkCacheEfficiency() {
        std::cout << "Running cache efficiency benchmark...\n";

        const size_t cacheSize = 1000;
        const size_t numLookups = 10000;

        std::map<std::string, std::string> cache;
        size_t initialMemory = MemoryTracker::getCurrentMemoryUsage();

        // Populate cache
        for (size_t i = 0; i < cacheSize; ++i) {
            cache["key_" + std::to_string(i)] = "value_" + std::to_string(i);
        }

        auto start = std::chrono::high_resolution_clock::now();

        size_t hits = 0;
        size_t misses = 0;

        for (size_t i = 0; i < numLookups; ++i) {
            std::string key = "key_" + std::to_string(i % (cacheSize * 2)); // Some misses

            if (cache.find(key) != cache.end()) {
                hits++;
            } else {
                misses++;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        size_t finalMemory = MemoryTracker::getCurrentMemoryUsage();
        size_t memoryDelta = finalMemory - initialMemory;

        double hitRate = static_cast<double>(hits) / (hits + misses) * 100.0;

        addResult(createResult("Cache Efficiency", numLookups, duration,
                              "Hit rate: " + std::to_string(hitRate) + "%, " +
                              "Memory: " + std::to_string(memoryDelta) + " KB"));
    }
};
