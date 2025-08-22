#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <future>
#include <cassert>
#include <sstream>
#include <random>

// Include the performance monitor header
#include "../include/performance_monitor.hpp"

/**
 * Test suite for PerformanceMonitor class
 * Tests metrics accuracy, thread safety, and external monitoring interfaces
 */
class PerformanceMonitorTest {
public:
    void runAllTests() {
        std::cout << "=== Performance Monitor Test Suite ===" << std::endl;
        
        testBasicMetrics();
        testRequestTracking();
        testConnectionMetrics();
        testTimeoutTracking();
        testPercentileCalculations();
        testThreadSafety();
        testMetricsReset();
        testJsonExport();
        testPrometheusExport();
        testMetricsAccuracy();
        
        std::cout << "=== All Performance Monitor Tests Passed ===" << std::endl;
    }

private:
    void testBasicMetrics() {
        std::cout << "Testing basic metrics functionality..." << std::endl;
        
        PerformanceMonitor monitor;
        
        // Test initial state
        auto metrics = monitor.getMetrics();
        assert(metrics.totalRequests.load() == 0);
        assert(metrics.activeRequests.load() == 0);
        assert(metrics.averageResponseTime.load() == 0.0);
        assert(metrics.connectionReuses.load() == 0);
        assert(metrics.totalConnections.load() == 0);
        assert(metrics.connectionTimeouts.load() == 0);
        assert(metrics.requestTimeouts.load() == 0);
        
        std::cout << "✓ Basic metrics initialization test passed" << std::endl;
    }
    
    void testRequestTracking() {
        std::cout << "Testing request tracking..." << std::endl;
        
        PerformanceMonitor monitor;
        
        // Test request start/end cycle
        monitor.recordRequestStart();
        auto metrics1 = monitor.getMetrics();
        assert(metrics1.totalRequests.load() == 1);
        assert(metrics1.activeRequests.load() == 1);
        
        // Simulate request processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        monitor.recordRequestEnd(std::chrono::milliseconds(10));
        auto metrics2 = monitor.getMetrics();
        assert(metrics2.totalRequests.load() == 1);
        assert(metrics2.activeRequests.load() == 0);
        assert(metrics2.averageResponseTime.load() > 0.0);
        
        // Test multiple requests
        for (int i = 0; i < 5; ++i) {
            monitor.recordRequestStart();
            monitor.recordRequestEnd(std::chrono::milliseconds(20 + i * 5));
        }
        
        auto metrics3 = monitor.getMetrics();
        assert(metrics3.totalRequests.load() == 6);
        assert(metrics3.activeRequests.load() == 0);
        
        std::cout << "✓ Request tracking test passed" << std::endl;
    }
    
    void testConnectionMetrics() {
        std::cout << "Testing connection metrics..." << std::endl;
        
        PerformanceMonitor monitor;
        
        // Test new connections
        monitor.recordNewConnection();
        monitor.recordNewConnection();
        monitor.recordNewConnection();
        
        auto metrics1 = monitor.getMetrics();
        assert(metrics1.totalConnections.load() == 3);
        assert(metrics1.connectionReuses.load() == 0);
        assert(metrics1.connectionReuseRate == 0.0);
        
        // Test connection reuses
        monitor.recordConnectionReuse();
        monitor.recordConnectionReuse();
        
        auto metrics2 = monitor.getMetrics();
        assert(metrics2.connectionReuses.load() == 2);
        assert(metrics2.connectionReuseRate > 0.0);
        assert(metrics2.connectionReuseRate < 1.0);
        
        std::cout << "✓ Connection metrics test passed" << std::endl;
    }
    
    void testTimeoutTracking() {
        std::cout << "Testing timeout tracking..." << std::endl;
        
        PerformanceMonitor monitor;
        
        // Test connection timeouts
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
        
        auto metrics1 = monitor.getMetrics();
        assert(metrics1.connectionTimeouts.load() == 2);
        assert(metrics1.requestTimeouts.load() == 0);
        
        // Test request timeouts
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
        
        auto metrics2 = monitor.getMetrics();
        assert(metrics2.connectionTimeouts.load() == 2);
        assert(metrics2.requestTimeouts.load() == 1);
        
        std::cout << "✓ Timeout tracking test passed" << std::endl;
    }
    
    void testPercentileCalculations() {
        std::cout << "Testing percentile calculations..." << std::endl;
        
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
        
        assert(p50.count() >= 40 && p50.count() <= 60); // Should be around median
        assert(p95.count() >= 90); // Should be high percentile
        assert(p99.count() >= 90); // Should be very high percentile
        
        // Test edge cases
        auto p0 = monitor.getPercentileResponseTime(0.0);
        auto p100 = monitor.getPercentileResponseTime(1.0);
        assert(p0.count() == 10); // Minimum value
        assert(p100.count() == 100); // Maximum value
        
        // Test invalid percentiles
        auto invalid1 = monitor.getPercentileResponseTime(-0.1);
        auto invalid2 = monitor.getPercentileResponseTime(1.1);
        assert(invalid1.count() == 0);
        assert(invalid2.count() == 0);
        
        std::cout << "✓ Percentile calculations test passed" << std::endl;
    }
    
    void testThreadSafety() {
        std::cout << "Testing thread safety..." << std::endl;
        
        PerformanceMonitor monitor;
        const int numThreads = 10;
        const int operationsPerThread = 100;
        
        std::vector<std::future<void>> futures;
        
        // Launch multiple threads performing concurrent operations
        for (int i = 0; i < numThreads; ++i) {
            futures.push_back(std::async(std::launch::async, [&monitor, operationsPerThread, i]() {
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
                        monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
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
        for (auto& future : futures) {
            future.wait();
        }
        
        // Verify final state consistency
        auto finalMetrics = monitor.getMetrics();
        assert(finalMetrics.totalRequests.load() == numThreads * operationsPerThread);
        assert(finalMetrics.activeRequests.load() == 0); // All requests should be completed
        assert(finalMetrics.totalConnections.load() == numThreads * operationsPerThread);
        
        std::cout << "✓ Thread safety test passed" << std::endl;
    }
    
    void testMetricsReset() {
        std::cout << "Testing metrics reset..." << std::endl;
        
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
        assert(metrics1.totalRequests.load() > 0);
        assert(metrics1.totalConnections.load() > 0);
        assert(metrics1.connectionReuses.load() > 0);
        assert(metrics1.connectionTimeouts.load() > 0);
        assert(metrics1.requestTimeouts.load() > 0);
        
        // Reset and verify
        monitor.reset();
        auto metrics2 = monitor.getMetrics();
        assert(metrics2.totalRequests.load() == 0);
        assert(metrics2.activeRequests.load() == 0);
        assert(metrics2.averageResponseTime.load() == 0.0);
        assert(metrics2.connectionReuses.load() == 0);
        assert(metrics2.totalConnections.load() == 0);
        assert(metrics2.connectionTimeouts.load() == 0);
        assert(metrics2.requestTimeouts.load() == 0);
        
        auto responseTimes = monitor.getResponseTimes();
        assert(responseTimes.empty());
        
        std::cout << "✓ Metrics reset test passed" << std::endl;
    }
    
    void testJsonExport() {
        std::cout << "Testing JSON export..." << std::endl;
        
        PerformanceMonitor monitor;
        
        // Add some test data
        monitor.recordRequestStart();
        monitor.recordRequestEnd(std::chrono::milliseconds(100));
        monitor.recordNewConnection();
        monitor.recordConnectionReuse();
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
        
        std::string json = monitor.getMetricsAsJson();
        
        // Verify JSON contains expected fields
        assert(json.find("totalRequests") != std::string::npos);
        assert(json.find("activeRequests") != std::string::npos);
        assert(json.find("averageResponseTime") != std::string::npos);
        assert(json.find("connectionReuses") != std::string::npos);
        assert(json.find("totalConnections") != std::string::npos);
        assert(json.find("connectionTimeouts") != std::string::npos);
        assert(json.find("requestTimeouts") != std::string::npos);
        assert(json.find("connectionReuseRate") != std::string::npos);
        assert(json.find("requestsPerSecond") != std::string::npos);
        assert(json.find("p95ResponseTime") != std::string::npos);
        assert(json.find("p99ResponseTime") != std::string::npos);
        
        // Verify it's valid JSON structure
        assert(json.front() == '{');
        assert(json.back() == '}');
        
        std::cout << "✓ JSON export test passed" << std::endl;
    }
    
    void testPrometheusExport() {
        std::cout << "Testing Prometheus export..." << std::endl;
        
        PerformanceMonitor monitor;
        
        // Add some test data
        monitor.recordRequestStart();
        monitor.recordRequestEnd(std::chrono::milliseconds(150));
        monitor.recordNewConnection();
        monitor.recordConnectionReuse();
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
        
        std::string prometheus = monitor.getMetricsAsPrometheus();
        
        // Verify Prometheus format contains expected metrics
        assert(prometheus.find("http_requests_total") != std::string::npos);
        assert(prometheus.find("http_requests_active") != std::string::npos);
        assert(prometheus.find("http_request_duration_ms") != std::string::npos);
        assert(prometheus.find("http_connections_reused_total") != std::string::npos);
        assert(prometheus.find("http_connections_total") != std::string::npos);
        assert(prometheus.find("http_connection_timeouts_total") != std::string::npos);
        assert(prometheus.find("http_request_timeouts_total") != std::string::npos);
        assert(prometheus.find("http_connection_reuse_rate") != std::string::npos);
        assert(prometheus.find("http_requests_per_second") != std::string::npos);
        assert(prometheus.find("http_request_duration_p95_ms") != std::string::npos);
        assert(prometheus.find("http_request_duration_p99_ms") != std::string::npos);
        
        // Verify Prometheus format structure
        assert(prometheus.find("# HELP") != std::string::npos);
        assert(prometheus.find("# TYPE") != std::string::npos);
        
        std::cout << "✓ Prometheus export test passed" << std::endl;
    }
    
    void testMetricsAccuracy() {
        std::cout << "Testing metrics accuracy..." << std::endl;
        
        PerformanceMonitor monitor;
        
        // Test precise request counting
        const int numRequests = 50;
        for (int i = 0; i < numRequests; ++i) {
            monitor.recordRequestStart();
            monitor.recordRequestEnd(std::chrono::milliseconds(10 + i));
        }
        
        auto metrics = monitor.getMetrics();
        assert(metrics.totalRequests.load() == numRequests);
        assert(metrics.activeRequests.load() == 0);
        
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
        assert(std::abs(actualReuseRate - expectedReuseRate) < 0.001);
        
        // Test timeout counting accuracy
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
        monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
        
        auto metrics3 = monitor.getMetrics();
        assert(metrics3.connectionTimeouts.load() == 2);
        assert(metrics3.requestTimeouts.load() == 1);
        
        std::cout << "✓ Metrics accuracy test passed" << std::endl;
    }
};

int main() {
    try {
        PerformanceMonitorTest test;
        test.runAllTests();
        
        std::cout << "\n=== Performance Monitor Integration Test ===" << std::endl;
        
        // Demonstrate real-world usage scenario
        PerformanceMonitor monitor;
        
        std::cout << "Simulating server load..." << std::endl;
        
        // Simulate a realistic server scenario
        for (int i = 0; i < 100; ++i) {
            monitor.recordNewConnection();
            
            // Some connections are reused
            if (i > 10 && i % 3 == 0) {
                monitor.recordConnectionReuse();
            }
            
            // Process requests
            for (int j = 0; j < (i % 5 + 1); ++j) {
                monitor.recordRequestStart();
                
                // Simulate variable response times
                int responseTime = 50 + (i % 200);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                
                monitor.recordRequestEnd(std::chrono::milliseconds(responseTime));
            }
            
            // Occasional timeouts
            if (i % 20 == 0) {
                monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
            }
            if (i % 30 == 0) {
                monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
            }
        }
        
        // Display final metrics
        auto finalMetrics = monitor.getMetrics();
        std::cout << "\nFinal Metrics:" << std::endl;
        std::cout << "Total Requests: " << finalMetrics.totalRequests.load() << std::endl;
        std::cout << "Active Requests: " << finalMetrics.activeRequests.load() << std::endl;
        std::cout << "Average Response Time: " << finalMetrics.averageResponseTime.load() << " ms" << std::endl;
        std::cout << "Total Connections: " << finalMetrics.totalConnections.load() << std::endl;
        std::cout << "Connection Reuses: " << finalMetrics.connectionReuses.load() << std::endl;
        std::cout << "Connection Reuse Rate: " << (finalMetrics.connectionReuseRate * 100) << "%" << std::endl;
        std::cout << "Connection Timeouts: " << finalMetrics.connectionTimeouts.load() << std::endl;
        std::cout << "Request Timeouts: " << finalMetrics.requestTimeouts.load() << std::endl;
        std::cout << "Requests Per Second: " << finalMetrics.requestsPerSecond << std::endl;
        std::cout << "P95 Response Time: " << monitor.getPercentileResponseTime(0.95).count() << " ms" << std::endl;
        std::cout << "P99 Response Time: " << monitor.getPercentileResponseTime(0.99).count() << " ms" << std::endl;
        
        std::cout << "\nJSON Export Sample:" << std::endl;
        std::cout << monitor.getMetricsAsJson() << std::endl;
        
        std::cout << "\n=== All Tests Completed Successfully ===" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}