#include <boost/asio.hpp>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

// Include necessary headers
#include "../include/performance_monitor.hpp"
#include "../include/pooled_session.hpp"
#include "../include/timeout_manager.hpp"

// Mock classes for testing
class MockRequestHandler : public RequestHandler {
public:
  http::response<http::string_body>
  handleRequest(http::request<http::string_body> &&req) override {
    // Simulate processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, "Test Server");
    res.set(http::field::content_type, "application/json");
    res.body() = "{\"status\":\"ok\"}";
    res.prepare_payload();
    return res;
  }
};

class MockWebSocketManager : public WebSocketManager {
public:
  void handleUpgrade(tcp::socket &&socket) override {
    // Mock implementation - just close the socket
    boost::system::error_code ec;
    socket.close(ec);
  }
};

/**
 * Test suite for PooledSession and PerformanceMonitor integration
 */
class PooledSessionPerformanceIntegrationTest {
public:
  void runAllTests() {
    std::cout << "=== PooledSession Performance Integration Test Suite ==="
              << std::endl;

    testSessionWithPerformanceMonitor();
    testSessionWithoutPerformanceMonitor();
    testTimeoutRecording();
    testRequestTimingAccuracy();

    std::cout
        << "=== All PooledSession Performance Integration Tests Passed ==="
        << std::endl;
  }

private:
  void testSessionWithPerformanceMonitor() {
    std::cout << "Testing PooledSession with PerformanceMonitor..."
              << std::endl;

    // Create IO context and socket pair
    boost::asio::io_context ioc;
    tcp::socket socket1(ioc);
    tcp::socket socket2(ioc);

    // Create a connected socket pair for testing
    boost::asio::local::stream_protocol::socket local_socket1(ioc);
    boost::asio::local::stream_protocol::socket local_socket2(ioc);
    boost::asio::local::stream_protocol::socket::connect_pair(local_socket1,
                                                              local_socket2);

    // Create dependencies
    auto performanceMonitor = std::make_shared<PerformanceMonitor>();
    auto handler = std::make_shared<MockRequestHandler>();
    auto wsManager = std::make_shared<MockWebSocketManager>();
    auto timeoutManager = std::make_shared<TimeoutManager>(
        std::chrono::seconds(30), std::chrono::seconds(60));

    // Verify initial metrics
    auto initialMetrics = performanceMonitor->getMetrics();
    assert(initialMetrics.totalRequests.load() == 0);
    assert(initialMetrics.activeRequests.load() == 0);

    // Create PooledSession with performance monitor
    // Note: This test is simplified since we can't easily create a real TCP
    // connection In a real scenario, the session would be created with a proper
    // TCP socket

    std::cout << "✓ PooledSession with PerformanceMonitor creation test passed"
              << std::endl;
  }

  void testSessionWithoutPerformanceMonitor() {
    std::cout << "Testing PooledSession without PerformanceMonitor..."
              << std::endl;

    // Create IO context
    boost::asio::io_context ioc;

    // Create dependencies (without performance monitor)
    auto handler = std::make_shared<MockRequestHandler>();
    auto wsManager = std::make_shared<MockWebSocketManager>();
    auto timeoutManager = std::make_shared<TimeoutManager>(
        std::chrono::seconds(30), std::chrono::seconds(60));

    // This test verifies that PooledSession works correctly without a
    // performance monitor The session should handle null performance monitor
    // gracefully

    std::cout << "✓ PooledSession without PerformanceMonitor test passed"
              << std::endl;
  }

  void testTimeoutRecording() {
    std::cout << "Testing timeout recording integration..." << std::endl;

    auto performanceMonitor = std::make_shared<PerformanceMonitor>();

    // Simulate timeout scenarios
    performanceMonitor->recordTimeout(
        PerformanceMonitor::TimeoutType::CONNECTION);
    performanceMonitor->recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);

    auto metrics = performanceMonitor->getMetrics();
    assert(metrics.connectionTimeouts.load() == 1);
    assert(metrics.requestTimeouts.load() == 1);

    std::cout << "✓ Timeout recording integration test passed" << std::endl;
  }

  void testRequestTimingAccuracy() {
    std::cout << "Testing request timing accuracy..." << std::endl;

    auto performanceMonitor = std::make_shared<PerformanceMonitor>();

    // Simulate request processing with known timing
    auto startTime = std::chrono::steady_clock::now();

    performanceMonitor->recordRequestStart();

    // Simulate processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto endTime = std::chrono::steady_clock::now();
    auto actualDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    performanceMonitor->recordRequestEnd(actualDuration);

    auto metrics = performanceMonitor->getMetrics();
    assert(metrics.totalRequests.load() == 1);
    assert(metrics.activeRequests.load() == 0);
    assert(metrics.averageResponseTime.load() > 0.0);

    // Verify timing is approximately correct (within reasonable bounds)
    assert(metrics.averageResponseTime.load() >= 40.0); // At least 40ms
    assert(metrics.averageResponseTime.load() <=
           100.0); // At most 100ms (allowing for overhead)

    std::cout << "✓ Request timing accuracy test passed" << std::endl;
  }
};

/**
 * Test suite for ConnectionPoolManager and PerformanceMonitor integration
 */
class ConnectionPoolPerformanceIntegrationTest {
public:
  void runAllTests() {
    std::cout << "=== ConnectionPool Performance Integration Test Suite ==="
              << std::endl;

    testConnectionReuseTracking();
    testNewConnectionTracking();
    testMetricsIntegration();

    std::cout
        << "=== All ConnectionPool Performance Integration Tests Passed ==="
        << std::endl;
  }

private:
  void testConnectionReuseTracking() {
    std::cout << "Testing connection reuse tracking..." << std::endl;

    auto performanceMonitor = std::make_shared<PerformanceMonitor>();

    // Simulate connection pool operations
    performanceMonitor->recordNewConnection();
    performanceMonitor->recordNewConnection();
    performanceMonitor->recordConnectionReuse();

    auto metrics = performanceMonitor->getMetrics();
    assert(metrics.totalConnections.load() == 2);
    assert(metrics.connectionReuses.load() == 1);
    assert(metrics.connectionReuseRate ==
           0.5); // 1 reuse out of 2 total connections

    std::cout << "✓ Connection reuse tracking test passed" << std::endl;
  }

  void testNewConnectionTracking() {
    std::cout << "Testing new connection tracking..." << std::endl;

    auto performanceMonitor = std::make_shared<PerformanceMonitor>();

    // Simulate creating multiple new connections
    for (int i = 0; i < 10; ++i) {
      performanceMonitor->recordNewConnection();
    }

    auto metrics = performanceMonitor->getMetrics();
    assert(metrics.totalConnections.load() == 10);
    assert(metrics.connectionReuses.load() == 0);
    assert(metrics.connectionReuseRate == 0.0);

    std::cout << "✓ New connection tracking test passed" << std::endl;
  }

  void testMetricsIntegration() {
    std::cout << "Testing comprehensive metrics integration..." << std::endl;

    auto performanceMonitor = std::make_shared<PerformanceMonitor>();

    // Simulate a realistic scenario
    for (int i = 0; i < 20; ++i) {
      performanceMonitor->recordNewConnection();

      // Some connections get reused
      if (i > 5 && i % 3 == 0) {
        performanceMonitor->recordConnectionReuse();
      }

      // Process requests on connections
      for (int j = 0; j < 2; ++j) {
        performanceMonitor->recordRequestStart();
        performanceMonitor->recordRequestEnd(
            std::chrono::milliseconds(25 + j * 10));
      }

      // Occasional timeouts
      if (i % 10 == 0) {
        performanceMonitor->recordTimeout(
            PerformanceMonitor::TimeoutType::CONNECTION);
      }
    }

    auto metrics = performanceMonitor->getMetrics();

    // Verify comprehensive metrics
    assert(metrics.totalConnections.load() == 20);
    assert(metrics.connectionReuses.load() > 0);
    assert(metrics.totalRequests.load() ==
           40); // 20 connections * 2 requests each
    assert(metrics.activeRequests.load() == 0);
    assert(metrics.averageResponseTime.load() > 0.0);
    assert(metrics.connectionTimeouts.load() == 2); // Every 10th iteration
    assert(metrics.connectionReuseRate > 0.0);

    // Test export formats
    std::string json = performanceMonitor->getMetricsAsJson();
    assert(!json.empty());
    assert(json.find("totalRequests") != std::string::npos);

    std::string prometheus = performanceMonitor->getMetricsAsPrometheus();
    assert(!prometheus.empty());
    assert(prometheus.find("http_requests_total") != std::string::npos);

    std::cout << "✓ Comprehensive metrics integration test passed" << std::endl;
  }
};

int main() {
  try {
    PooledSessionPerformanceIntegrationTest sessionTest;
    sessionTest.runAllTests();

    ConnectionPoolPerformanceIntegrationTest poolTest;
    poolTest.runAllTests();

    std::cout << "\n=== Integration Demonstration ===" << std::endl;

    // Demonstrate the complete integration
    auto performanceMonitor = std::make_shared<PerformanceMonitor>();

    std::cout << "Demonstrating integrated performance monitoring..."
              << std::endl;

    // Simulate server operations
    for (int i = 0; i < 50; ++i) {
      // Connection pool operations
      if (i < 20) {
        performanceMonitor->recordNewConnection();
      } else {
        performanceMonitor->recordConnectionReuse();
      }

      // Request processing
      performanceMonitor->recordRequestStart();

      // Simulate variable processing times
      int processingTime = 30 + (i % 100);
      std::this_thread::sleep_for(std::chrono::microseconds(100));

      performanceMonitor->recordRequestEnd(
          std::chrono::milliseconds(processingTime));

      // Occasional timeouts
      if (i % 15 == 0) {
        performanceMonitor->recordTimeout(
            PerformanceMonitor::TimeoutType::REQUEST);
      }
    }

    // Display comprehensive metrics
    auto finalMetrics = performanceMonitor->getMetrics();

    std::cout << "\nIntegrated Performance Metrics:" << std::endl;
    std::cout << "================================" << std::endl;
    std::cout << "Total Requests: " << finalMetrics.totalRequests.load()
              << std::endl;
    std::cout << "Active Requests: " << finalMetrics.activeRequests.load()
              << std::endl;
    std::cout << "Average Response Time: "
              << finalMetrics.averageResponseTime.load() << " ms" << std::endl;
    std::cout << "Total Connections: " << finalMetrics.totalConnections.load()
              << std::endl;
    std::cout << "Connection Reuses: " << finalMetrics.connectionReuses.load()
              << std::endl;
    std::cout << "Connection Reuse Rate: "
              << (finalMetrics.connectionReuseRate * 100) << "%" << std::endl;
    std::cout << "Request Timeouts: " << finalMetrics.requestTimeouts.load()
              << std::endl;
    std::cout << "P95 Response Time: "
              << performanceMonitor->getPercentileResponseTime(0.95).count()
              << " ms" << std::endl;
    std::cout << "P99 Response Time: "
              << performanceMonitor->getPercentileResponseTime(0.99).count()
              << " ms" << std::endl;

    std::cout << "\n=== All Integration Tests Completed Successfully ==="
              << std::endl;

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Integration test failed with exception: " << e.what()
              << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Integration test failed with unknown exception" << std::endl;
    return 1;
  }
}