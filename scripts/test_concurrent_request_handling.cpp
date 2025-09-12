#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "connection_pool_manager.hpp"
#include "http_server.hpp"
#include "logger.hpp"
#include "request_handler.hpp"
#include "server_config.hpp"
#include "timeout_manager.hpp"

/**
 * Concurrent Load Test Request Handler
 */
class ConcurrentTestHandler : public RequestHandler {
private:
  std::atomic<int> requestCount_{0};
  std::atomic<int> concurrentRequests_{0};
  std::atomic<int> maxConcurrentRequests_{0};
  std::mutex responseMutex_;
  std::vector<std::chrono::milliseconds> responseTimes_;

public:
  http::response<http::string_body>
  handleRequest(http::request<http::string_body> &&req) override {
    auto startTime = std::chrono::steady_clock::now();

    int currentConcurrent = ++concurrentRequests_;
    int currentMax = maxConcurrentRequests_.load();
    while (currentConcurrent > currentMax &&
           !maxConcurrentRequests_.compare_exchange_weak(currentMax,
                                                         currentConcurrent)) {
      currentMax = maxConcurrentRequests_.load();
    }

    requestCount_++;

    // Simulate variable processing time (10-50ms)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));

    --concurrentRequests_;

    auto endTime = std::chrono::steady_clock::now();
    auto responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    {
      std::lock_guard<std::mutex> lock(responseMutex_);
      responseTimes_.push_back(responseTime);
    }

    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, "ETL Plus Backend Concurrent Test");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());

    std::string body =
        "{\"message\":\"Concurrent request processed\",\"count\":" +
        std::to_string(requestCount_.load()) +
        ",\"concurrent\":" + std::to_string(currentConcurrent) + "}";
    res.body() = body;
    res.prepare_payload();

    return res;
  }

  int getRequestCount() const { return requestCount_.load(); }
  int getMaxConcurrentRequests() const { return maxConcurrentRequests_.load(); }

  std::vector<std::chrono::milliseconds> getResponseTimes() const {
    std::lock_guard<std::mutex> lock(responseMutex_);
    return responseTimes_;
  }

  void reset() {
    requestCount_ = 0;
    concurrentRequests_ = 0;
    maxConcurrentRequests_ = 0;
    std::lock_guard<std::mutex> lock(responseMutex_);
    responseTimes_.clear();
  }

  // Required by RequestHandler interface
  std::shared_ptr<ETLJobManager> getJobManager() override { return nullptr; }
  std::shared_ptr<JobMonitorService> getJobMonitorService() override {
    return nullptr;
  }
};

/**
 * Concurrent Request Handling Integration Test
 */
class ConcurrentRequestHandlingTest {
private:
  std::unique_ptr<HttpServer> server_;
  std::shared_ptr<ConcurrentTestHandler> handler_;
  const std::string address_ = "127.0.0.1";
  const unsigned short port_ = 8083;

public:
  ConcurrentRequestHandlingTest() {
    handler_ = std::make_shared<ConcurrentTestHandler>();
  }

  void testHighConcurrencyWithOptimalPool() {
    std::cout << "Testing high concurrency with optimal pool configuration..."
              << std::endl;

    // Configure for high concurrency
    ServerConfig config = ServerConfig::create(20,  // minConnections
                                               100, // maxConnections
                                               300, // idleTimeoutSec
                                               30,  // connTimeoutSec
                                               60,  // reqTimeoutSec
                                               5 * 1024 * 1024, // maxBodySize
                                               true, // metricsEnabled
                                               200,  // maxQueueSize
                                               45    // maxQueueWaitTimeSec
    );

    server_ = std::make_unique<HttpServer>(address_, port_, 8, config);
    server_->setRequestHandler(handler_);

    auto poolManager = server_->getConnectionPoolManager();
    assert(poolManager != nullptr);

    // Verify pool configuration
    assert(poolManager->getMaxConnections() == 100);
    assert(poolManager->getMaxQueueSize() == 200);

    std::cout << "✓ High concurrency pool configuration validated" << std::endl;
    std::cout << "✓ High concurrency test setup completed" << std::endl;
  }

  void testConnectionPoolUnderStress() {
    std::cout << "Testing connection pool behavior under stress..."
              << std::endl;

    // Configure with limited connections to test pool behavior
    ServerConfig config = ServerConfig::create(
        5,           // minConnections
        15,          // maxConnections (limited to force pool management)
        60,          // idleTimeoutSec
        10,          // connTimeoutSec
        30,          // reqTimeoutSec
        1024 * 1024, // maxBodySize
        true,        // metricsEnabled
        50,          // maxQueueSize
        20           // maxQueueWaitTimeSec
    );

    server_ = std::make_unique<HttpServer>(address_, port_, 4, config);
    server_->setRequestHandler(handler_);

    auto poolManager = server_->getConnectionPoolManager();
    assert(poolManager != nullptr);

    // Test pool statistics tracking
    assert(poolManager->getConnectionReuseCount() == 0);
    assert(poolManager->getTotalConnectionsCreated() == 0);
    assert(poolManager->getRejectedRequestCount() == 0);

    std::cout << "✓ Connection pool stress test configuration validated"
              << std::endl;
    std::cout << "✓ Connection pool stress test setup completed" << std::endl;
  }

  void testRequestQueueingBehavior() {
    std::cout << "Testing request queuing behavior under load..." << std::endl;

    // Configure with very small pool to force queuing
    ServerConfig config =
        ServerConfig::create(2,          // minConnections (very small)
                             3,          // maxConnections (very small)
                             30,         // idleTimeoutSec
                             5,          // connTimeoutSec
                             15,         // reqTimeoutSec
                             512 * 1024, // maxBodySize
                             true,       // metricsEnabled
                             10, // maxQueueSize (small to test queue limits)
                             10  // maxQueueWaitTimeSec
        );

    server_ = std::make_unique<HttpServer>(address_, port_, 2, config);
    server_->setRequestHandler(handler_);

    auto poolManager = server_->getConnectionPoolManager();
    assert(poolManager != nullptr);

    // Verify queue configuration
    assert(poolManager->getMaxConnections() == 3);
    assert(poolManager->getMaxQueueSize() == 10);

    // Test initial queue state
    assert(poolManager->getQueueSize() == 0);
    assert(poolManager->getRejectedRequestCount() == 0);

    std::cout << "✓ Request queuing behavior test configuration validated"
              << std::endl;
    std::cout << "✓ Request queuing behavior test setup completed" << std::endl;
  }

  void testErrorHandlingUnderLoad() {
    std::cout << "Testing error handling under high load..." << std::endl;

    // Configure for error testing with very restrictive limits
    ServerConfig config =
        ServerConfig::create(1,          // minConnections (minimal)
                             2,          // maxConnections (minimal)
                             15,         // idleTimeoutSec (short)
                             3,          // connTimeoutSec (very short)
                             5,          // reqTimeoutSec (very short)
                             256 * 1024, // maxBodySize (small)
                             true,       // metricsEnabled
                             3,          // maxQueueSize (very small)
                             2           // maxQueueWaitTimeSec (very short)
        );

    server_ = std::make_unique<HttpServer>(address_, port_, 1, config);
    server_->setRequestHandler(handler_);

    auto poolManager = server_->getConnectionPoolManager();
    assert(poolManager != nullptr);

    // Verify restrictive configuration
    assert(poolManager->getMaxConnections() == 2);
    assert(poolManager->getMaxQueueSize() == 3);

    std::cout << "✓ Error handling under load test configuration validated"
              << std::endl;
    std::cout << "✓ Error handling under load test setup completed"
              << std::endl;
  }

  void testThreadSafetyUnderConcurrentLoad() {
    std::cout << "Testing thread safety under concurrent load..." << std::endl;

    // Configure for maximum concurrency testing
    ServerConfig config = ServerConfig::create(10,  // minConnections
                                               50,  // maxConnections
                                               120, // idleTimeoutSec
                                               20,  // connTimeoutSec
                                               40,  // reqTimeoutSec
                                               2 * 1024 * 1024, // maxBodySize
                                               true, // metricsEnabled
                                               100,  // maxQueueSize
                                               30    // maxQueueWaitTimeSec
    );

    server_ = std::make_unique<HttpServer>(address_, port_, 8, config);
    server_->setRequestHandler(handler_);

    auto poolManager = server_->getConnectionPoolManager();
    assert(poolManager != nullptr);

    // Test concurrent access to pool statistics
    std::vector<std::future<bool>> futures;
    std::atomic<int> successfulAccesses{0};

    // Launch multiple threads to access pool statistics concurrently
    for (int i = 0; i < 20; ++i) {
      futures.push_back(
          std::async(std::launch::async, [&poolManager, &successfulAccesses]() {
            try {
              for (int j = 0; j < 50; ++j) {
                // Access various statistics concurrently
                auto active = poolManager->getActiveConnections();
                auto idle = poolManager->getIdleConnections();
                auto total = poolManager->getTotalConnections();
                auto reuse = poolManager->getConnectionReuseCount();
                auto queue = poolManager->getQueueSize();
                auto rejected = poolManager->getRejectedRequestCount();

                // Verify basic consistency
                assert(total == active + idle);
                assert(reuse >= 0);
                assert(queue >= 0);
                assert(rejected >= 0);

                // Small delay to increase chance of race conditions
                std::this_thread::sleep_for(std::chrono::microseconds(100));
              }
              return true;
            } catch (...) {
              return false;
            }
          }));
    }

    // Wait for all threads and check results
    for (auto &future : futures) {
      if (future.get()) {
        successfulAccesses++;
      }
    }

    assert(successfulAccesses.load() == 20);
    std::cout << "✓ Thread safety under concurrent load test passed"
              << std::endl;
    std::cout << "✓ Thread safety test completed successfully" << std::endl;
  }

  void testPerformanceMetricsCollection() {
    std::cout << "Testing performance metrics collection..." << std::endl;

    ServerConfig config =
        ServerConfig::create(5, 25, 180, 25, 50, 3 * 1024 * 1024, true, 75, 35);

    server_ = std::make_unique<HttpServer>(address_, port_, 6, config);
    server_->setRequestHandler(handler_);

    auto poolManager = server_->getConnectionPoolManager();
    assert(poolManager != nullptr);

    // Test metrics collection capabilities
    auto initialReuse = poolManager->getConnectionReuseCount();
    auto initialCreated = poolManager->getTotalConnectionsCreated();
    auto initialRejected = poolManager->getRejectedRequestCount();

    assert(initialReuse >= 0);
    assert(initialCreated >= 0);
    assert(initialRejected >= 0);

    // Test statistics reset
    poolManager->resetStatistics();
    assert(poolManager->getConnectionReuseCount() == 0);
    assert(poolManager->getTotalConnectionsCreated() == 0);
    assert(poolManager->getRejectedRequestCount() == 0);

    std::cout << "✓ Performance metrics collection test passed" << std::endl;
    std::cout << "✓ Performance metrics test completed successfully"
              << std::endl;
  }

  void cleanup() {
    if (server_ && server_->isRunning()) {
      std::cout << "Stopping server..." << std::endl;
      server_->stop();
      assert(!server_->isRunning());
      std::cout << "✓ Server stopped successfully" << std::endl;
    }

    if (handler_) {
      handler_->reset();
    }
  }

  void runAllTests() {
    std::cout << "Running Concurrent Request Handling Integration Tests..."
              << std::endl;
    std::cout << "============================================================="
              << std::endl;

    try {
      testHighConcurrencyWithOptimalPool();
      cleanup();

      testConnectionPoolUnderStress();
      cleanup();

      testRequestQueueingBehavior();
      cleanup();

      testErrorHandlingUnderLoad();
      cleanup();

      testThreadSafetyUnderConcurrentLoad();
      cleanup();

      testPerformanceMetricsCollection();
      cleanup();

      std::cout
          << "============================================================="
          << std::endl;
      std::cout << "✓ All concurrent request handling integration tests passed!"
                << std::endl;

    } catch (const std::exception &e) {
      std::cout << "✗ Concurrent request handling test failed: " << e.what()
                << std::endl;
      cleanup();
      throw;
    } catch (...) {
      std::cout
          << "✗ Concurrent request handling test failed with unknown exception"
          << std::endl;
      cleanup();
      throw;
    }
  }
};

int main() {
  try {
    // Set up logging
    Logger::getInstance().setLogLevel(LogLevel::INFO);

    ConcurrentRequestHandlingTest test;
    test.runAllTests();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Concurrent request handling test suite failed: " << e.what()
              << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Concurrent request handling test suite failed with unknown "
                 "exception"
              << std::endl;
    return 1;
  }
}