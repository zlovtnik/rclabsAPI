#include "performance_benchmark.hpp"
#include "connection_pool_manager.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

// Connection pool performance benchmark
class ConnectionPoolBenchmark : public BenchmarkBase {
public:
    ConnectionPoolBenchmark() : BenchmarkBase("Connection Pool") {}

    void run() override {
        benchmarkConnectionAcquisition();
        benchmarkConcurrentConnections();
        benchmarkConnectionReuse();
        benchmarkPoolScaling();
    }

private:
    void benchmarkConnectionAcquisition() {
        std::cout << "Running connection acquisition benchmark...\n";

        etl_plus::ConnectionPoolManager poolManager;
        poolManager.initialize(10, "test_db", "localhost", 5432);

        const size_t numConnections = 1000;

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < numConnections; ++i) {
            auto connection = poolManager.acquireConnection();
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            poolManager.releaseConnection(connection);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        addResult(createResult("Connection Acquisition", numConnections, duration,
                              "Sequential connection acquire/release cycles"));
    }

    void benchmarkConcurrentConnections() {
        std::cout << "Running concurrent connections benchmark...\n";

        etl_plus::ConnectionPoolManager poolManager;
        poolManager.initialize(20, "test_db", "localhost", 5432);

        const size_t numThreads = 10;
        const size_t connectionsPerThread = 100;
        const size_t totalConnections = numThreads * connectionsPerThread;

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;
        std::atomic<size_t> completedThreads{0};

        for (size_t i = 0; i < numThreads; ++i) {
            threads.emplace_back([&, i]() {
                for (size_t j = 0; j < connectionsPerThread; ++j) {
                    auto connection = poolManager.acquireConnection();
                    // Simulate database work
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    poolManager.releaseConnection(connection);
                }
                completedThreads++;
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        addResult(createResult("Concurrent Connections", totalConnections, duration,
                              std::to_string(numThreads) + " concurrent threads"));
    }

    void benchmarkConnectionReuse() {
        std::cout << "Running connection reuse benchmark...\n";

        etl_plus::ConnectionPoolManager poolManager;
        poolManager.initialize(5, "test_db", "localhost", 5432);

        const size_t numReuses = 5000;
        auto connection = poolManager.acquireConnection();

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < numReuses; ++i) {
            // Simulate query execution
            std::this_thread::sleep_for(std::chrono::microseconds(5));
            // Connection stays active, just reused
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        poolManager.releaseConnection(connection);

        addResult(createResult("Connection Reuse", numReuses, duration,
                              "Reusing single connection for multiple operations"));
    }

    void benchmarkPoolScaling() {
        std::cout << "Running pool scaling benchmark...\n";

        const size_t maxPoolSize = 50;
        const size_t stepSize = 10;
        const size_t operationsPerPoolSize = 500;

        for (size_t poolSize = stepSize; poolSize <= maxPoolSize; poolSize += stepSize) {
            etl_plus::ConnectionPoolManager poolManager;
            poolManager.initialize(poolSize, "test_db", "localhost", 5432);

            auto start = std::chrono::high_resolution_clock::now();

            std::vector<std::thread> threads;
            for (size_t i = 0; i < poolSize; ++i) {
                threads.emplace_back([&, i]() {
                    for (size_t j = 0; j < operationsPerPoolSize / poolSize; ++j) {
                        auto connection = poolManager.acquireConnection();
                        std::this_thread::sleep_for(std::chrono::microseconds(20));
                        poolManager.releaseConnection(connection);
                    }
                });
            }

            for (auto& thread : threads) {
                thread.join();
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            addResult(createResult("Pool Scaling " + std::to_string(poolSize),
                                  operationsPerPoolSize, duration,
                                  "Pool size: " + std::to_string(poolSize)));
        }
    }
};
