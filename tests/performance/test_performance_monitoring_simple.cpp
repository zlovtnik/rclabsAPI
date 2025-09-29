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
#include "../../include/performance_monitor.hpp"

/**
 * Simplified test suite for PerformanceMonitor class
 * Tests core functionality without external dependencies
 */

// Test fixture for SimplePerformanceMonitor tests
class SimplePerformanceMonitorTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Setup code if needed
  }

  void TearDown() override {
    // Cleanup code if needed
  }
};

// Test basic functionality
TEST_F(SimplePerformanceMonitorTest, BasicFunctionality) {
  PerformanceMonitor monitor;

  // Test request lifecycle
  monitor.recordRequestStart();
  auto metrics1 = monitor.getMetrics();
  EXPECT_EQ(metrics1.totalRequests.load(), 1);
  EXPECT_EQ(metrics1.activeRequests.load(), 1);

  monitor.recordRequestEnd(std::chrono::milliseconds(100));
  auto metrics2 = monitor.getMetrics();
  EXPECT_EQ(metrics2.totalRequests.load(), 1);
  EXPECT_EQ(metrics2.activeRequests.load(), 0);
  EXPECT_GT(metrics2.averageResponseTime.load(), 0.0);

  // Test connection tracking
  monitor.recordNewConnection();
  monitor.recordConnectionReuse();
  auto metrics3 = monitor.getMetrics();
  EXPECT_EQ(metrics3.totalConnections.load(), 1);
  EXPECT_EQ(metrics3.connectionReuses.load(), 1);

  // Test timeout tracking
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
  monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
  auto metrics4 = monitor.getMetrics();
  EXPECT_EQ(metrics4.connectionTimeouts.load(), 1);
  EXPECT_EQ(metrics4.requestTimeouts.load(), 1);
}

// Test metrics accuracy
TEST_F(SimplePerformanceMonitorTest, MetricsAccuracy) {
  PerformanceMonitor monitor;

  // Test precise counting
  const int numRequests = 25;
  for (int i = 0; i < numRequests; ++i) {
    monitor.recordRequestStart();
    monitor.recordRequestEnd(std::chrono::milliseconds(50 + i * 2));
  }

  auto metrics = monitor.getMetrics();
  EXPECT_EQ(metrics.totalRequests.load(), numRequests);
  EXPECT_EQ(metrics.activeRequests.load(), 0);

  // Test connection reuse rate
  monitor.reset();
  monitor.recordNewConnection();
  monitor.recordNewConnection();
  monitor.recordConnectionReuse();

  auto metrics2 = monitor.getMetrics();
  EXPECT_DOUBLE_EQ(metrics2.connectionReuseRate,
                   0.5); // 1 reuse, 2 new connections (50% reuse rate)
}

// Test thread safety
TEST_F(SimplePerformanceMonitorTest, ThreadSafety) {
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
  EXPECT_EQ(finalMetrics.totalRequests.load(),
            numThreads * operationsPerThread);
  EXPECT_EQ(finalMetrics.activeRequests.load(), 0);
}

// Test export formats
TEST_F(SimplePerformanceMonitorTest, ExportFormats) {
  PerformanceMonitor monitor;

  // Add some data
  monitor.recordRequestStart();
  monitor.recordRequestEnd(std::chrono::milliseconds(75));
  monitor.recordNewConnection();
  monitor.recordConnectionReuse();

  // Test JSON export
  std::string json = monitor.getMetricsAsJson();
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("totalRequests"), std::string::npos);
  EXPECT_NE(json.find("averageResponseTime"), std::string::npos);
  EXPECT_NE(json.find("connectionReuseRate"), std::string::npos);

  // Test Prometheus export
  std::string prometheus = monitor.getMetricsAsPrometheus();
  EXPECT_FALSE(prometheus.empty());
  EXPECT_NE(prometheus.find("http_requests_total"), std::string::npos);
  EXPECT_NE(prometheus.find("# HELP"), std::string::npos);
  EXPECT_NE(prometheus.find("# TYPE"), std::string::npos);
}

// Test real-world scenario
TEST_F(SimplePerformanceMonitorTest, RealWorldScenario) {
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
  EXPECT_GT(metrics.totalRequests.load(), 100);
  EXPECT_EQ(metrics.activeRequests.load(), 0);
  EXPECT_GT(metrics.averageResponseTime.load(), 0.0);
  EXPECT_GT(metrics.totalConnections.load(), 0);
  EXPECT_GT(metrics.connectionReuses.load(), 0);
  EXPECT_GT(metrics.connectionReuseRate, 0.0);
  EXPECT_GT(metrics.requestTimeouts.load(), 0);
  EXPECT_GT(metrics.connectionTimeouts.load(), 0);

  // Test percentiles
  auto p95 = monitor.getPercentileResponseTime(0.95);
  auto p99 = monitor.getPercentileResponseTime(0.99);
  EXPECT_GT(p95.count(), 0);
  EXPECT_GE(p99.count(), p95.count());
}
