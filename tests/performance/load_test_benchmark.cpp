#include "component_logger.hpp"
#include "connection_pool_manager.hpp"
#include "http_server.hpp"
#include "log_handler.hpp"
#include "performance_benchmark.hpp"
#include "websocket_manager_enhanced.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

// Load testing benchmark - comprehensive stress testing
class LoadTestBenchmark : public BenchmarkBase {
public:
  LoadTestBenchmark() : BenchmarkBase("Load Test") {}

  void run() override {
    benchmarkConcurrentRequests();
    benchmarkMixedWorkload();
    benchmarkSpikeLoad();
    benchmarkSustainedLoad();
  }

private:
  void benchmarkConcurrentRequests() {
    std::cout << "Running concurrent requests benchmark...\n";

    etl_plus::HttpServer server;
    server.initialize(8080);

    etl_plus::ConnectionPoolManager poolManager;
    poolManager.initialize(20, "test_db", "localhost", 5432);

    const size_t numClients = 50;
    const size_t requestsPerClient = 100;
    const size_t totalRequests = numClients * requestsPerClient;

    std::atomic<size_t> completedRequests{0};
    std::atomic<size_t> failedRequests{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> clientThreads;
    for (size_t i = 0; i < numClients; ++i) {
      clientThreads.emplace_back([&, i]() {
        for (size_t j = 0; j < requestsPerClient; ++j) {
          try {
            // Simulate HTTP request processing
            std::string request =
                "GET /api/data/" + std::to_string(j) + " HTTP/1.1";
            auto connection = poolManager.acquireConnection();

            // Simulate request processing
            std::this_thread::sleep_for(std::chrono::microseconds(200));

            poolManager.releaseConnection(connection);
            completedRequests++;
          } catch (const std::exception &e) {
            failedRequests++;
          }
        }
      });
    }

    for (auto &thread : clientThreads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double successRate =
        static_cast<double>(completedRequests) / totalRequests * 100.0;

    addResult(createResult("Concurrent Requests", totalRequests, duration,
                           "Success rate: " + std::to_string(successRate) +
                               "%, " + std::to_string(numClients) +
                               " clients"));
  }

  void benchmarkMixedWorkload() {
    std::cout << "Running mixed workload benchmark...\n";

    etl_plus::HttpServer server;
    server.initialize(8080);

    etl_plus::ConnectionPoolManager poolManager;
    poolManager.initialize(15, "test_db", "localhost", 5432);

    etl_plus::WebSocketManagerEnhanced wsManager;
    wsManager.initialize(8081);

    auto consoleHandler = std::make_shared<etl_plus::ConsoleLogHandler>();
    etl_plus::ComponentLogger<etl_plus::AuthManager> logger(consoleHandler);

    const size_t numOperations = 2000;
    const size_t numThreads = 20;
    const size_t opsPerThread = numOperations / numThreads;

    std::atomic<size_t> httpRequests{0};
    std::atomic<size_t> dbOperations{0};
    std::atomic<size_t> wsMessages{0};
    std::atomic<size_t> logEntries{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workerThreads;
    for (size_t i = 0; i < numThreads; ++i) {
      workerThreads.emplace_back([&, i]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> operationType(0, 3);

        for (size_t j = 0; j < opsPerThread; ++j) {
          int op = operationType(gen);

          switch (op) {
          case 0: { // HTTP request
            auto connection = poolManager.acquireConnection();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            poolManager.releaseConnection(connection);
            httpRequests++;
            break;
          }
          case 1: { // Database operation
            auto connection = poolManager.acquireConnection();
            std::this_thread::sleep_for(std::chrono::microseconds(150));
            poolManager.releaseConnection(connection);
            dbOperations++;
            break;
          }
          case 2: { // WebSocket message
            std::string message = R"({"type":"update","thread":)" +
                                  std::to_string(i) + R"(,"op":)" +
                                  std::to_string(j) + "}";
            wsManager.broadcastMessage(message);
            wsMessages++;
            break;
          }
          case 3: { // Logging
            logger.info("LoadTest", "Thread " + std::to_string(i) +
                                        " operation " + std::to_string(j));
            logEntries++;
            break;
          }
          }
        }
      });
    }

    for (auto &thread : workerThreads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    addResult(createResult("Mixed Workload", numOperations, duration,
                           "HTTP: " + std::to_string(httpRequests) +
                               ", DB: " + std::to_string(dbOperations) +
                               ", WS: " + std::to_string(wsMessages) +
                               ", Logs: " + std::to_string(logEntries)));
  }

  void benchmarkSpikeLoad() {
    std::cout << "Running spike load benchmark...\n";

    etl_plus::HttpServer server;
    server.initialize(8080);

    etl_plus::ConnectionPoolManager poolManager;
    poolManager.initialize(30, "test_db", "localhost", 5432);

    const size_t spikeDurationMs = 5000; // 5 seconds
    const size_t baseLoad = 10;
    const size_t spikeLoad = 100;

    std::atomic<size_t> totalRequests{0};
    std::atomic<size_t> spikeRequests{0};

    auto start = std::chrono::high_resolution_clock::now();

    // Phase 1: Base load
    std::vector<std::thread> baseThreads;
    for (size_t i = 0; i < baseLoad; ++i) {
      baseThreads.emplace_back([&, i]() {
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - start)
                   .count() < spikeDurationMs / 2) {
          auto connection = poolManager.acquireConnection();
          std::this_thread::sleep_for(std::chrono::microseconds(200));
          poolManager.releaseConnection(connection);
          totalRequests++;
        }
      });
    }

    // Wait for base load to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(spikeDurationMs / 2));

    // Phase 2: Spike load
    std::vector<std::thread> spikeThreads;
    for (size_t i = 0; i < spikeLoad; ++i) {
      spikeThreads.emplace_back([&, i]() {
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - start)
                   .count() < spikeDurationMs) {
          auto connection = poolManager.acquireConnection();
          std::this_thread::sleep_for(std::chrono::microseconds(100));
          poolManager.releaseConnection(connection);
          totalRequests++;
          spikeRequests++;
        }
      });
    }

    // Wait for spike to complete
    for (auto &thread : spikeThreads) {
      thread.join();
    }
    for (auto &thread : baseThreads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    addResult(createResult("Spike Load", totalRequests.load(), duration,
                           "Spike requests: " + std::to_string(spikeRequests) +
                               ", Base load: " + std::to_string(baseLoad) +
                               ", Spike load: " + std::to_string(spikeLoad)));
  }

  void benchmarkSustainedLoad() {
    std::cout << "Running sustained load benchmark...\n";

    etl_plus::HttpServer server;
    server.initialize(8080);

    etl_plus::ConnectionPoolManager poolManager;
    poolManager.initialize(25, "test_db", "localhost", 5432);

    const size_t testDurationMs = 10000; // 10 seconds
    const size_t numWorkerThreads = 15;

    std::atomic<size_t> totalOperations{0};
    std::atomic<size_t> successfulOperations{0};
    std::atomic<size_t> failedOperations{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workerThreads;
    for (size_t i = 0; i < numWorkerThreads; ++i) {
      workerThreads.emplace_back([&, i]() {
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - start)
                   .count() < testDurationMs) {

          try {
            // Simulate sustained workload
            auto connection = poolManager.acquireConnection();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
            poolManager.releaseConnection(connection);

            successfulOperations++;
          } catch (const std::exception &e) {
            failedOperations++;
          }

          totalOperations++;
        }
      });
    }

    for (auto &thread : workerThreads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double successRate =
        static_cast<double>(successfulOperations) / totalOperations * 100.0;
    double opsPerSecond =
        static_cast<double>(totalOperations) / (duration.count() / 1000.0);

    addResult(createResult("Sustained Load", totalOperations.load(), duration,
                           "Success rate: " + std::to_string(successRate) +
                               "%, " +
                               "Ops/sec: " + std::to_string(opsPerSecond)));
  }
};
