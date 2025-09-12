#include "performance_benchmark.hpp"
#include "websocket_manager_enhanced.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// WebSocket performance benchmark
class WebSocketBenchmark : public BenchmarkBase {
public:
  WebSocketBenchmark() : BenchmarkBase("WebSocket") {}

  void run() override {
    benchmarkMessageThroughput();
    benchmarkConcurrentClients();
    benchmarkMessageLatency();
    benchmarkConnectionHandling();
  }

private:
  void benchmarkMessageThroughput() {
    std::cout << "Running WebSocket message throughput benchmark...\n";

    etl_plus::WebSocketManagerEnhanced wsManager;
    wsManager.initialize(8080);

    const size_t numMessages = 10000;
    const std::string testMessage =
        R"({"type":"status","data":{"progress":50}})";

    auto start = std::chrono::high_resolution_clock::now();

    // Simulate sending messages to connected clients
    for (size_t i = 0; i < numMessages; ++i) {
      wsManager.broadcastMessage(testMessage);
      // Small delay to simulate realistic message spacing
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    addResult(createResult("Message Throughput", numMessages, duration,
                           "Broadcasting messages to connected clients"));
  }

  void benchmarkConcurrentClients() {
    std::cout << "Running concurrent WebSocket clients benchmark...\n";

    etl_plus::WebSocketManagerEnhanced wsManager;
    wsManager.initialize(8080);

    const size_t numClients = 100;
    const size_t messagesPerClient = 50;
    const size_t totalMessages = numClients * messagesPerClient;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> clientThreads;
    std::atomic<size_t> activeClients{0};

    for (size_t i = 0; i < numClients; ++i) {
      clientThreads.emplace_back([&, i]() {
        activeClients++;

        // Simulate client connection and message handling
        for (size_t j = 0; j < messagesPerClient; ++j) {
          std::string clientMessage = R"({"client":)" + std::to_string(i) +
                                      R"(,"message":)" + std::to_string(j) +
                                      "}";
          wsManager.handleIncomingMessage(i, clientMessage);
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        activeClients--;
      });
    }

    for (auto &thread : clientThreads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    addResult(
        createResult("Concurrent Clients", totalMessages, duration,
                     std::to_string(numClients) + " simultaneous clients"));
  }

  void benchmarkMessageLatency() {
    std::cout << "Running WebSocket message latency benchmark...\n";

    etl_plus::WebSocketManagerEnhanced wsManager;
    wsManager.initialize(8080);

    const size_t numLatencyTests = 1000;
    std::vector<long long> latencies;

    for (size_t i = 0; i < numLatencyTests; ++i) {
      auto sendTime = std::chrono::high_resolution_clock::now();

      std::string pingMessage =
          R"({"type":"ping","timestamp":)" +
          std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                             sendTime.time_since_epoch())
                             .count()) +
          "}";

      wsManager.broadcastMessage(pingMessage);

      // Simulate round-trip delay
      std::this_thread::sleep_for(std::chrono::microseconds(500));

      auto receiveTime = std::chrono::high_resolution_clock::now();
      auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                         receiveTime - sendTime)
                         .count();

      latencies.push_back(latency);
    }

    // Calculate average latency
    double avgLatency =
        std::accumulate(latencies.begin(), latencies.end(), 0.0) /
        latencies.size();

    auto totalDuration =
        std::chrono::microseconds(static_cast<long long>(avgLatency));
    addResult(
        createResult("Message Latency", numLatencyTests, totalDuration,
                     "Average latency: " +
                         std::to_string(static_cast<long long>(avgLatency)) +
                         " microseconds"));
  }

  void benchmarkConnectionHandling() {
    std::cout << "Running WebSocket connection handling benchmark...\n";

    etl_plus::WebSocketManagerEnhanced wsManager;
    wsManager.initialize(8080);

    const size_t numConnections = 500;
    const size_t connectionDurationMs = 100;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> connectionThreads;

    for (size_t i = 0; i < numConnections; ++i) {
      connectionThreads.emplace_back([&, i]() {
        // Simulate connection establishment
        wsManager.handleNewConnection(i);

        // Simulate connection activity
        std::this_thread::sleep_for(
            std::chrono::milliseconds(connectionDurationMs));

        // Simulate connection closure
        wsManager.handleConnectionClose(i);
      });
    }

    for (auto &thread : connectionThreads) {
      thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    addResult(createResult("Connection Handling", numConnections, duration,
                           "Connection establishment and cleanup"));
  }
};
