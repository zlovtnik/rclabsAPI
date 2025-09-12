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
  /**
   * @brief Handles an incoming HTTP request for the concurrent-load test handler.
   *
   * Processes the request by recording start/end timestamps, incrementing/decrementing
   * internal concurrent/request counters, simulating work with a small random delay
   * (10–50 ms), and recording the per-request response time into an internal, thread-safe
   * vector. Builds and returns an HTTP 200 JSON response containing a brief message,
   * the total processed request count, and the observed concurrent count for that request.
   *
   * Thread-safety: updates to concurrency counters use atomics; response times are
   * appended under a mutex to protect the vector.
   *
   * Side effects:
   * - Increments/decrements request and concurrency counters.
   * - Appends a measured response time to the handler's responseTimes_ vector.
   * - Performs a blocking sleep to simulate work.
   *
   * @return http::response<http::string_body> HTTP 200 OK response with a JSON body
   *         containing "message", "count", and "concurrent" fields.
   */
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

  /**
 * @brief Returns the total number of requests this handler has processed.
 *
 * This is a thread-safe snapshot of the request counter.
 *
 * @return int Total processed request count.
 */
int getRequestCount() const { return requestCount_.load(); }
  /**
 * @brief Returns the highest number of concurrent in-flight requests observed.
 *
 * This value is maintained atomically by the handler and represents the peak
 * number of concurrent requests seen since the last reset.
 *
 * @return int Peak concurrent request count.
 */
int getMaxConcurrentRequests() const { return maxConcurrentRequests_.load(); }

  /**
   * @brief Returns a snapshot of per-request processing durations recorded by the handler.
   *
   * The vector is a thread-safe copy captured under a mutex to avoid races with concurrent
   * request handling. Each element is the measured elapsed time for a single handled request.
   *
   * @return std::vector<std::chrono::milliseconds> A copy of the recorded response times.
   */
  std::vector<std::chrono::milliseconds> getResponseTimes() const {
    std::lock_guard<std::mutex> lock(responseMutex_);
    return responseTimes_;
  }

  /**
   * @brief Reset all recorded request counters and timing data.
   *
   * Clears the per-request timing history and sets the request, current-concurrency,
   * and observed-maximum-concurrency counters back to zero. The operation is
   * thread-safe: the response times vector is cleared while holding the internal
   * mutex; atomic counters are updated without additional locking.
   */
  void reset() {
    requestCount_ = 0;
    concurrentRequests_ = 0;
    maxConcurrentRequests_ = 0;
    std::lock_guard<std::mutex> lock(responseMutex_);
    responseTimes_.clear();
  }

  /**
 * @brief Returns the job manager used by the handler.
 *
 * This test handler does not use a job manager; always returns `nullptr`.
 *
 * @return std::shared_ptr<ETLJobManager> Always `nullptr`.
 */
  std::shared_ptr<ETLJobManager> getJobManager() override { return nullptr; }
  /**
   * @brief Returns the job monitor service used by the handler.
   *
   * This test handler does not provide a JobMonitorService; callers will receive
   * a null shared_ptr.
   *
   * @return std::shared_ptr<JobMonitorService> Always `nullptr` for this handler.
   */
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
  /**
   * @brief Constructs the test harness.
   *
   * Initializes the test fixture by creating the shared ConcurrentTestHandler instance
   * used by the server tests.
   */
  ConcurrentRequestHandlingTest() {
    handler_ = std::make_shared<ConcurrentTestHandler>();
  }

  /**
   * @brief Sets up an HttpServer with an "optimal" high-concurrency connection pool and validates its configuration.
   *
   * Configures a ServerConfig tuned for high concurrency, constructs the server (stored in member `server_`),
   * installs the shared `handler_`, and asserts that the connection pool manager is present and that
   * its max-connections and max-queue-size match the configured values. Uses assertions for validation;
   * any assertion failure will terminate the test.
   */
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

  /**
   * @brief Sets up an HttpServer with a constrained connection pool and validates initial pool statistics.
   *
   * Configures a ServerConfig with limited max connections and a bounded request queue, creates an
   * HttpServer bound to the test address/port, installs the ConcurrentTestHandler, and obtains the
   * ConnectionPoolManager. Asserts that the pool manager exists and that its initial metrics for
   * connection reuse, total created connections, and rejected requests are zero.
   *
   * Side effects:
   * - Constructs and stores a server instance in `server_`.
   * - Installs `handler_` as the server's request handler.
   * - Uses assertions to validate pool manager presence and initial counters.
   */
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

  /**
   * @brief Validates connection-pool queueing configuration and initial state by creating a small-pool server.
   *
   * Creates an HttpServer configured with a deliberately tiny connection pool (min 2, max 3)
   * and a small request queue (maxQueueSize 10), installs the ConcurrentTestHandler, and
   * asserts the pool manager reports the expected limits and an empty initial queue.
   *
   * Side effects:
   * - Constructs the server instance and assigns the request handler (stored in `server_` and `handler_`).
   * - Uses runtime assertions to validate pool configuration and initial statistics; a failed assertion will terminate the test run.
   */
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

  /**
   * @brief Configure the server with highly restrictive connection-pool limits and validate error-handling-related settings.
   *
   * Sets up an HttpServer using a minimal/maximal connection configuration and tiny queue/timeout limits intended to
   * provoke queueing/rejection and exercise error-handling paths under load. The function creates the server instance,
   * installs the test request handler, retrieves the ConnectionPoolManager, and asserts that the pool's maximum
   * connections and queue size match the restrictive configuration. Progress is emitted to stdout.
   *
   * Side effects:
   * - Initializes server_ (unique_ptr<HttpServer>) and assigns its request handler to handler_.
   * - Performs assertions that terminate the test process if the pool manager is missing or configuration differs.
   */
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

  /**
   * @brief Verifies thread safety of the connection pool by exercising concurrent reads of its statistics.
   *
   * Starts an HttpServer configured for high concurrency, sets the test request handler, then spawns
   * multiple asynchronous threads that repeatedly read various pool metrics (active, idle, total,
   * reuse count, queue size, rejected count) and assert basic invariants (e.g., total == active + idle,
   * non-negative counters). Waits for all workers to complete and asserts all succeeded.
   *
   * Side effects:
   * - Creates and assigns to the member server_.
   * - Calls server_->setRequestHandler(handler_).
   * - Uses assertions to enforce invariants; a failing assertion will terminate the test.
   */
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

  /**
   * @brief Verifies that the connection pool exposes and resets performance metrics.
   *
   * Starts an HttpServer with a medium-sized configuration, installs the test handler,
   * reads initial pool metrics (connection reuse count, total connections created,
   * rejected request count) and asserts they are non-negative, then calls
   * resetStatistics() and asserts those metrics are reset to zero.
   *
   * Side effects:
   * - Assigns and starts server_ and configures its request handler.
   * - Uses assertions to validate metric values and reset behavior.
   */
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

  /**
   * @brief Stop the test server if it's running and reset the test handler state.
   *
   * If an internal HttpServer instance exists and reports running, this method
   * stops it and asserts that it is no longer running. After server shutdown (or
   * if no server was running), the associated ConcurrentTestHandler, if present,
   * is reset to clear counters and recorded response times.
   *
   * @note Uses an assertion to verify the server stopped successfully.
   */
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

  /**
   * @brief Run the full suite of concurrent request handling integration tests.
   *
   * Executes each test case in sequence and performs cleanup between tests to
   * ensure isolation. Progress and results are written to standard output.
   *
   * The sequence of tests invoked:
   * - testHighConcurrencyWithOptimalPool
   * - testConnectionPoolUnderStress
   * - testRequestQueueingBehavior
   * - testErrorHandlingUnderLoad
   * - testThreadSafetyUnderConcurrentLoad
   * - testPerformanceMetricsCollection
   *
   * If any test throws, this function calls cleanup() and then rethrows the exception,
   * allowing callers to handle test failures.
   */
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

/**
 * @brief Entry point for the concurrent request handling test suite.
 *
 * Configures logging, constructs the test harness (ConcurrentRequestHandlingTest),
 * and runs the full suite of integration tests via runAllTests().
 *
 * Exits with 0 on success. If a std::exception is thrown, prints the exception
 * message to stderr and returns 1. For any other unexpected exception, prints
 * a generic error message to stderr and returns 1.
 *
 * @return int Exit code (0 = success, 1 = failure).
 */
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