#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

// Include the performance monitor header
#include "../include/performance_monitor.hpp"

/**
 * Simplified test suite for PerformanceMonitor class
 * Tests core functionality without external dependencies
 */
class SimplePerformanceMonitorTest {
public:
  void runAllTests() {
    std::cout << "=== Simple Performance Monitor Test Suite ===" << std::endl;

    testBasicFunctionality();
    testMetricsAccuracy();
    testThreadSafety();
    testExportFormats();
    testRealWorldScenario();

    std::cout << "=== All Simple Performance Monitor Tests Passed ==="
              << std::endl;
  }

private:
  void testBasicFunctionality() {
    std::cout << "Testing basic functionality..." << std::endl;

    PerformanceMonitor monitor;

    // Test request lifecycle
    monitor.recordRequestStart();
    auto metrics1 = monitor.getMetrics();
    assert(metrics1.totalRequests.load() == 1);
    assert(metrics1.activeRequests.load() == 1);

    monitor.recordRequestEnd(std::chrono::milliseconds(100));
    auto metrics2 = monitor.getMetrics();
    assert(metrics2.totalRequests.load() == 1);
    assert(metrics2.activeRequests.load() == 0);
    assert(metrics2.averageResponseTime.load() > 0.0);

    // Test connection tracking
    monitor.recordNewConnection();
    monitor.recordConnectionReuse();
    auto metrics3 = monitor.getMetrics();
    assert(metrics3.totalConnections.load() == 1);
    assert(metrics3.connectionReuses.load() == 1);

    // Test timeout tracking
    monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
    monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
    auto metrics4 = monitor.getMetrics();
    assert(metrics4.connectionTimeouts.load() == 1);
    assert(metrics4.requestTimeouts.load() == 1);

    std::cout << "✓ Basic functionality test passed" << std::endl;
  }

  void testMetricsAccuracy() {
    std::cout << "Testing metrics accuracy..." << std::endl;

    PerformanceMonitor monitor;

    // Test precise counting
    const int numRequests = 25;
    for (int i = 0; i < numRequests; ++i) {
      monitor.recordRequestStart();
      monitor.recordRequestEnd(std::chrono::milliseconds(50 + i * 2));
    }

    auto metrics = monitor.getMetrics();
    assert(metrics.totalRequests.load() == numRequests);
    assert(metrics.activeRequests.load() == 0);

    // Test connection reuse rate
    monitor.reset();
    monitor.recordNewConnection();
    monitor.recordNewConnection();
    monitor.recordConnectionReuse();

    auto metrics2 = monitor.getMetrics();
    assert(metrics2.connectionReuseRate ==
           0.5); // 1 reuse, 2 new connections (50% reuse rate)

    std::cout << "✓ Metrics accuracy test passed" << std::endl;
  }

  void testThreadSafety() {
    std::cout << "Testing thread safety..." << std::endl;

    PerformanceMonitor monitor;
    const int numThreads = 4;
    const int operationsPerThread = 50;

    std::vector<std::future<void>> futures;

    for (int i = 0; i < numThreads; ++i) {
      futures.push_back(
          std::async(std::launch::async, [&monitor, operationsPerThread]() {
            for (int j = 0; j < operationsPerThread; ++j) {
              monitor.recordRequestStart();
              monitor.recordNewConnection();

              if (j % 2 == 0) {
                monitor.recordConnectionReuse();
              }

              monitor.recordRequestEnd(std::chrono::milliseconds(10 + j));

              if (j % 10 == 0) {
                auto metrics = monitor.getMetrics();
                // Just access metrics to test concurrent reads
              }
            }
          }));
    }

    for (auto &future : futures) {
      future.wait();
    }

    auto finalMetrics = monitor.getMetrics();
    assert(finalMetrics.totalRequests.load() ==
           numThreads * operationsPerThread);
    assert(finalMetrics.activeRequests.load() == 0);

    std::cout << "✓ Thread safety test passed" << std::endl;
  }

  void testExportFormats() {
    std::cout << "Testing export formats..." << std::endl;

    PerformanceMonitor monitor;

    // Add some data
    monitor.recordRequestStart();
    monitor.recordRequestEnd(std::chrono::milliseconds(75));
    monitor.recordNewConnection();
    monitor.recordConnectionReuse();

    // Test JSON export
    std::string json = monitor.getMetricsAsJson();
    assert(!json.empty());
    assert(json.find("totalRequests") != std::string::npos);
    assert(json.find("averageResponseTime") != std::string::npos);
    assert(json.find("connectionReuseRate") != std::string::npos);

    // Test Prometheus export
    std::string prometheus = monitor.getMetricsAsPrometheus();
    assert(!prometheus.empty());
    assert(prometheus.find("http_requests_total") != std::string::npos);
    assert(prometheus.find("# HELP") != std::string::npos);
    assert(prometheus.find("# TYPE") != std::string::npos);

    std::cout << "✓ Export formats test passed" << std::endl;
  }

  void testRealWorldScenario() {
    std::cout << "Testing real-world scenario..." << std::endl;

    PerformanceMonitor monitor;

    // Simulate realistic server load
    for (int i = 0; i < 100; ++i) {
      // Connection management
      if (i < 30) {
        monitor.recordNewConnection();
      } else if (i % 2 == 0) {
        monitor.recordConnectionReuse();
      }

      // Request processing
      int requestsThisCycle = (i % 3) + 1;
      for (int j = 0; j < requestsThisCycle; ++j) {
        monitor.recordRequestStart();

        // Variable response times
        int responseTime = 25 + (i + j) % 150;
        monitor.recordRequestEnd(std::chrono::milliseconds(responseTime));
      }

      // Occasional timeouts
      if (i % 20 == 0) {
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
      }
      if (i % 25 == 0) {
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
      }
    }

    auto metrics = monitor.getMetrics();

    // Verify realistic metrics
    assert(metrics.totalRequests.load() > 100);
    assert(metrics.activeRequests.load() == 0);
    assert(metrics.averageResponseTime.load() > 0.0);
    assert(metrics.totalConnections.load() > 0);
    assert(metrics.connectionReuses.load() > 0);
    assert(metrics.connectionReuseRate > 0.0);
    assert(metrics.requestTimeouts.load() > 0);
    assert(metrics.connectionTimeouts.load() > 0);

    // Test percentiles
    auto p95 = monitor.getPercentileResponseTime(0.95);
    auto p99 = monitor.getPercentileResponseTime(0.99);
    assert(p95.count() > 0);
    assert(p99.count() >= p95.count());

    std::cout << "✓ Real-world scenario test passed" << std::endl;
  }
};

int main() {
  try {
    SimplePerformanceMonitorTest test;
    test.runAllTests();

    std::cout << "\n=== Performance Monitoring Demonstration ===" << std::endl;

    PerformanceMonitor monitor;

    std::cout << "Simulating HTTP server with performance monitoring..."
              << std::endl;

    // Simulate server operations
    for (int i = 0; i < 200; ++i) {
      // Connection pool behavior
      if (i < 50) {
        monitor.recordNewConnection();
      } else {
        // 70% connection reuse rate
        if (i % 10 < 7) {
          monitor.recordConnectionReuse();
        } else {
          monitor.recordNewConnection();
        }
      }

      // Request processing
      monitor.recordRequestStart();

      // Simulate realistic response times (20-200ms)
      int baseTime = 20;
      int variableTime = i % 180;
      int responseTime = baseTime + variableTime;

      monitor.recordRequestEnd(std::chrono::milliseconds(responseTime));

      // Simulate timeouts (5% request timeout rate, 2% connection timeout rate)
      if (i % 20 == 0) {
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
      }
      if (i % 50 == 0) {
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
      }
    }

    // Display comprehensive results
    auto finalMetrics = monitor.getMetrics();

    std::cout << "\nPerformance Monitoring Results:" << std::endl;
    std::cout << "===============================" << std::endl;
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
    std::cout << "Connection Timeouts: "
              << finalMetrics.connectionTimeouts.load() << std::endl;
    std::cout << "Request Timeouts: " << finalMetrics.requestTimeouts.load()
              << std::endl;
    std::cout << "P95 Response Time: "
              << monitor.getPercentileResponseTime(0.95).count() << " ms"
              << std::endl;
    std::cout << "P99 Response Time: "
              << monitor.getPercentileResponseTime(0.99).count() << " ms"
              << std::endl;

    std::cout << "\nJSON Metrics Export:" << std::endl;
    std::cout << "===================" << std::endl;
    std::cout << monitor.getMetricsAsJson() << std::endl;

    std::cout << "\n=== Performance Monitoring Implementation Complete ==="
              << std::endl;

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Test failed with unknown exception" << std::endl;
    return 1;
  }
}