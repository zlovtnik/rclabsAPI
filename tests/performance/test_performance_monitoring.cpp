#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

// Google Test framework
#include <gtest/gtest.h>

// Include the performance monitor header
#include "../include/performance_monitor.hpp"

/**
 * Test suite for PerformanceMonitor class
 * Tests metrics accuracy, thread safety, and external monitoring interfaces
 */

// Test fixture for PerformanceMonitor tests
class PerformanceMonitorTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Setup code if needed
  }

  void TearDown() override {
    // Cleanup code if needed
  }
};

// Test basic metrics functionality
TEST_F(PerformanceMonitorTest, BasicMetrics) {
  PerformanceMonitor monitor;

  // Test initial state
  auto metrics = monitor.getMetrics();
  EXPECT_EQ(metrics.totalRequests.load(), 0);
  EXPECT_EQ(metrics.activeRequests.load(), 0);
  EXPECT_DOUBLE_EQ(metrics.averageResponseTime.load(), 0.0);
  EXPECT_EQ(metrics.connectionReuses.load(), 0);
  EXPECT_EQ(metrics.totalConnections.load(), 0);
  EXPECT_EQ(metrics.connectionTimeouts.load(), 0);
  EXPECT_EQ(metrics.requestTimeouts.load(), 0);

  std::cout << "✓ Basic metrics initialization test passed" << std::endl;
}

// Test request tracking
TEST_F(PerformanceMonitorTest, RequestTracking) {
  PerformanceMonitor monitor;

  // Test request start/end cycle
  monitor.recordRequestStart();
  auto metrics1 = monitor.getMetrics();
  EXPECT_EQ(metrics1.totalRequests.load(), 1);
  EXPECT_EQ(metrics1.activeRequests.load(), 1);

  // Simulate request processing time
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  monitor.recordRequestEnd(std::chrono::milliseconds(10));
  auto metrics2 = monitor.getMetrics();
  EXPECT_EQ(metrics2.totalRequests.load(), 1);
  EXPECT_EQ(metrics2.activeRequests.load(), 0);
  EXPECT_GT(metrics2.averageResponseTime.load(), 0.0);

  // Test multiple requests
  for (int i = 0; i < 5; ++i) {
    monitor.recordRequestStart();
    monitor.recordRequestEnd(std::chrono::milliseconds(20 + i * 5));
  }

  auto metrics3 = monitor.getMetrics();
  EXPECT_EQ(metrics3.totalRequests.load(), 6);
  EXPECT_EQ(metrics3.activeRequests.load(), 0);
}

// Test connection metrics
TEST_F(PerformanceMonitorTest, ConnectionMetrics) {
  PerformanceMonitor monitor;

  // Test new connections
  monitor.recordNewConnection();
  monitor.recordNewConnection();
  monitor.recordNewConnection();

  auto metrics1 = monitor.getMetrics();
  EXPECT_EQ(metrics1.totalConnections.load(), 3);
  EXPECT_EQ(metrics1.connectionReuses.load(), 0);
  EXPECT_DOUBLE_EQ(metrics1.connectionReuseRate, 0.0);

  // Test connection reuses
  monitor.recordConnectionReuse();
  monitor.recordConnectionReuse();

  auto metrics2 = monitor.getMetrics();
  EXPECT_EQ(metrics2.connectionReuses.load(), 2);
  EXPECT_GT(metrics2.connectionReuseRate, 0.0);
  EXPECT_LT(metrics2.connectionReuseRate, 1.0);
}

// Test timeout tracking
TEST_F(PerformanceMonitorTest, TimeoutTracking) {
  PerformanceMonitor monitor;

  // Test connection timeouts
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);

  auto metrics1 = monitor.getMetrics();
  EXPECT_EQ(metrics1.connectionTimeouts.load(), 2);
  EXPECT_EQ(metrics1.requestTimeouts.load(), 0);

  // Test request timeouts
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);

  auto metrics2 = monitor.getMetrics();
  EXPECT_EQ(metrics2.connectionTimeouts.load(), 2);
  EXPECT_EQ(metrics2.requestTimeouts.load(), 1);
}

// Test percentile calculations
TEST_F(PerformanceMonitorTest, PercentileCalculations) {
  PerformanceMonitor monitor;

  // Add response times in a known pattern
  std::vector<int> responseTimes = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

  for (int time : responseTimes) {
    monitor.recordRequestStart();
    monitor.recordRequestEnd(std::chrono::milliseconds(time));
  }

  // Test percentile calculations
  auto p50 = monitor.getPercentileResponseTime(0.5);
  auto p95 = monitor.getPercentileResponseTime(0.95);
  auto p99 = monitor.getPercentileResponseTime(0.99);

  EXPECT_GE(p50.count(), 40);
  EXPECT_LE(p50.count(), 60); // Should be around median
  EXPECT_GE(p95.count(), 90); // Should be high percentile
  EXPECT_GE(p99.count(), 90); // Should be very high percentile

  // Test edge cases
  auto p0 = monitor.getPercentileResponseTime(0.0);
  auto p100 = monitor.getPercentileResponseTime(1.0);
  EXPECT_EQ(p0.count(), 10);    // Minimum value
  EXPECT_EQ(p100.count(), 100); // Maximum value

  // Test invalid percentiles
  auto invalid1 = monitor.getPercentileResponseTime(-0.1);
  auto invalid2 = monitor.getPercentileResponseTime(1.1);
  EXPECT_EQ(invalid1.count(), 0);
  EXPECT_EQ(invalid2.count(), 0);
}

// Test thread safety
TEST_F(PerformanceMonitorTest, ThreadSafety) {
  PerformanceMonitor monitor;
  const int numThreads = 10;
  const int operationsPerThread = 100;

  std::vector<std::future<void>> futures;

  // Launch multiple threads performing concurrent operations
  for (int i = 0; i < numThreads; ++i) {
    futures.push_back(
        std::async(std::launch::async, [&monitor, operationsPerThread, i]() {
          std::random_device rd;
          std::mt19937 gen(rd());
          std::uniform_int_distribution<> dis(1, 50);

          for (int j = 0; j < operationsPerThread; ++j) {
            // Mix different operations
            monitor.recordRequestStart();
            monitor.recordNewConnection();

            if (j % 3 == 0) {
              monitor.recordConnectionReuse();
            }

            if (j % 7 == 0) {
              monitor.recordTimeout(
                  PerformanceMonitor::TimeoutType::CONNECTION);
            }

            if (j % 11 == 0) {
              monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
            }

            // Simulate some processing time
            std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));

            monitor.recordRequestEnd(std::chrono::milliseconds(dis(gen)));

            // Occasionally get metrics to test concurrent reads
            if (j % 10 == 0) {
              auto metrics = monitor.getMetrics();
              auto responseTimes = monitor.getResponseTimes();
              auto p95 = monitor.getPercentileResponseTime(0.95);
            }
          }
        }));
  }

  // Wait for all threads to complete
  for (auto &future : futures) {
    future.wait();
  }

  // Verify final state consistency
  auto finalMetrics = monitor.getMetrics();
  EXPECT_EQ(finalMetrics.totalRequests.load(),
            numThreads * operationsPerThread);
  EXPECT_EQ(finalMetrics.activeRequests.load(),
            0); // All requests should be completed
  EXPECT_EQ(finalMetrics.totalConnections.load(),
            numThreads * operationsPerThread);
}

// Test metrics reset
TEST_F(PerformanceMonitorTest, MetricsReset) {
  PerformanceMonitor monitor;

  // Add some data
  monitor.recordRequestStart();
  monitor.recordRequestEnd(std::chrono::milliseconds(50));
  monitor.recordNewConnection();
  monitor.recordConnectionReuse();
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);

  // Verify data exists
  auto metrics1 = monitor.getMetrics();
  EXPECT_GT(metrics1.totalRequests.load(), 0);
  EXPECT_GT(metrics1.totalConnections.load(), 0);
  EXPECT_GT(metrics1.connectionReuses.load(), 0);
  EXPECT_GT(metrics1.connectionTimeouts.load(), 0);
  EXPECT_GT(metrics1.requestTimeouts.load(), 0);

  // Reset and verify
  monitor.reset();
  auto metrics2 = monitor.getMetrics();
  EXPECT_EQ(metrics2.totalRequests.load(), 0);
  EXPECT_EQ(metrics2.activeRequests.load(), 0);
  EXPECT_DOUBLE_EQ(metrics2.averageResponseTime.load(), 0.0);
  EXPECT_EQ(metrics2.connectionReuses.load(), 0);
  EXPECT_EQ(metrics2.totalConnections.load(), 0);
  EXPECT_EQ(metrics2.connectionTimeouts.load(), 0);
  EXPECT_EQ(metrics2.requestTimeouts.load(), 0);

  auto responseTimes = monitor.getResponseTimes();
  EXPECT_TRUE(responseTimes.empty());
}

// Test JSON export
TEST_F(PerformanceMonitorTest, JsonExport) {
  PerformanceMonitor monitor;

  // Add some test data
  monitor.recordRequestStart();
  monitor.recordRequestEnd(std::chrono::milliseconds(100));
  monitor.recordNewConnection();
  monitor.recordConnectionReuse();
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);

  std::string json = monitor.getMetricsAsJson();

  // Verify JSON contains expected fields
  EXPECT_NE(json.find("totalRequests"), std::string::npos);
  EXPECT_NE(json.find("activeRequests"), std::string::npos);
  EXPECT_NE(json.find("averageResponseTime"), std::string::npos);
  EXPECT_NE(json.find("connectionReuses"), std::string::npos);
  EXPECT_NE(json.find("totalConnections"), std::string::npos);
  EXPECT_NE(json.find("connectionTimeouts"), std::string::npos);
  EXPECT_NE(json.find("requestTimeouts"), std::string::npos);
  EXPECT_NE(json.find("connectionReuseRate"), std::string::npos);
  EXPECT_NE(json.find("requestsPerSecond"), std::string::npos);
  EXPECT_NE(json.find("p95ResponseTime"), std::string::npos);
  EXPECT_NE(json.find("p99ResponseTime"), std::string::npos);

  // Verify it's valid JSON structure
  EXPECT_EQ(json.front(), '{');
  EXPECT_EQ(json.back(), '}');
}

// Test Prometheus export
TEST_F(PerformanceMonitorTest, PrometheusExport) {
  PerformanceMonitor monitor;

  // Add some test data
  monitor.recordRequestStart();
  monitor.recordRequestEnd(std::chrono::milliseconds(150));
  monitor.recordNewConnection();
  monitor.recordConnectionReuse();
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);

  std::string prometheus = monitor.getMetricsAsPrometheus();

  // Verify Prometheus format contains expected metrics
  EXPECT_NE(prometheus.find("http_requests_total"), std::string::npos);
  EXPECT_NE(prometheus.find("http_requests_active"), std::string::npos);
  EXPECT_NE(prometheus.find("http_request_duration_ms"), std::string::npos);
  EXPECT_NE(prometheus.find("http_connections_reused_total"),
            std::string::npos);
  EXPECT_NE(prometheus.find("http_connections_total"), std::string::npos);
  EXPECT_NE(prometheus.find("http_connection_timeouts_total"),
            std::string::npos);
  EXPECT_NE(prometheus.find("http_request_timeouts_total"), std::string::npos);
  EXPECT_NE(prometheus.find("http_connection_reuse_rate"), std::string::npos);
  EXPECT_NE(prometheus.find("http_requests_per_second"), std::string::npos);
  EXPECT_NE(prometheus.find("http_request_duration_p95_ms"), std::string::npos);
  EXPECT_NE(prometheus.find("http_request_duration_p99_ms"), std::string::npos);

  // Verify Prometheus format structure
  EXPECT_NE(prometheus.find("# HELP"), std::string::npos);
  EXPECT_NE(prometheus.find("# TYPE"), std::string::npos);
}

// Test metrics accuracy
TEST_F(PerformanceMonitorTest, MetricsAccuracy) {
  PerformanceMonitor monitor;

  // Test precise request counting
  const int numRequests = 50;
  for (int i = 0; i < numRequests; ++i) {
    monitor.recordRequestStart();
    monitor.recordRequestEnd(std::chrono::milliseconds(10 + i));
  }

  auto metrics = monitor.getMetrics();
  EXPECT_EQ(metrics.totalRequests.load(), numRequests);
  EXPECT_EQ(metrics.activeRequests.load(), 0);

  // Test connection reuse rate calculation
  const int numConnections = 20;
  const int numReuses = 15;

  monitor.reset();
  for (int i = 0; i < numConnections; ++i) {
    monitor.recordNewConnection();
  }
  for (int i = 0; i < numReuses; ++i) {
    monitor.recordConnectionReuse();
  }

  auto metrics2 = monitor.getMetrics();
  double expectedReuseRate = static_cast<double>(numReuses) / numConnections;
  double actualReuseRate = metrics2.connectionReuseRate;

  // Allow for small floating point differences
  EXPECT_LT(std::abs(actualReuseRate - expectedReuseRate), 0.001);

  // Test timeout counting accuracy
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);

  auto metrics3 = monitor.getMetrics();
  EXPECT_EQ(metrics3.connectionTimeouts.load(), 2);
  EXPECT_EQ(metrics3.requestTimeouts.load(), 1);
}
