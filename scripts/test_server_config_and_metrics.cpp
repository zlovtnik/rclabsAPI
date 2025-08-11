#include "../include/server_config.hpp"
#include "../include/performance_monitor.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

/**
 * @brief Test program for ServerConfig and PerformanceMonitor functionality
 * 
 * This program validates the configuration validation, default value handling,
 * and thread-safe metrics collection capabilities.
 */

void testServerConfig() {
    std::cout << "Testing ServerConfig functionality...\n";
    
    // Test default configuration
    auto defaultConfig = ServerConfig::create();
    auto validation = defaultConfig.validate();
    assert(validation.isValid);
    assert(defaultConfig.minConnections == 10);
    assert(defaultConfig.maxConnections == 100);
    assert(defaultConfig.enableMetrics == true);
    std::cout << "✓ Default configuration validation passed\n";
    
    // Test custom configuration
    auto customConfig = ServerConfig::create(5, 50, 600, 15, 30, 5 * 1024 * 1024, false);
    validation = customConfig.validate();
    assert(validation.isValid);
    assert(customConfig.minConnections == 5);
    assert(customConfig.maxConnections == 50);
    assert(customConfig.idleTimeout == std::chrono::seconds{600});
    assert(customConfig.connectionTimeout == std::chrono::seconds{15});
    assert(customConfig.requestTimeout == std::chrono::seconds{30});
    assert(customConfig.maxRequestBodySize == 5 * 1024 * 1024);
    assert(customConfig.enableMetrics == false);
    std::cout << "✓ Custom configuration validation passed\n";
    
    // Test invalid configuration
    ServerConfig invalidConfig;
    invalidConfig.minConnections = 0;
    invalidConfig.maxConnections = 0;
    invalidConfig.connectionTimeout = std::chrono::seconds{-1};
    validation = invalidConfig.validate();
    assert(!validation.isValid);
    assert(validation.errors.size() >= 3); // Should have multiple errors
    std::cout << "✓ Invalid configuration properly detected\n";
    
    // Test configuration with warnings
    ServerConfig warningConfig;
    warningConfig.maxConnections = 2000; // Very high
    warningConfig.idleTimeout = std::chrono::seconds{30}; // Very low
    warningConfig.maxRequestBodySize = 200 * 1024 * 1024; // Very large
    validation = warningConfig.validate();
    assert(validation.isValid); // Should be valid but have warnings
    assert(validation.warnings.size() >= 3); // Should have multiple warnings
    std::cout << "✓ Configuration warnings properly generated\n";
    
    // Test applyDefaults functionality
    ServerConfig brokenConfig;
    brokenConfig.minConnections = 0;
    brokenConfig.maxConnections = 0;
    brokenConfig.connectionTimeout = std::chrono::seconds{0};
    brokenConfig.applyDefaults();
    validation = brokenConfig.validate();
    assert(validation.isValid);
    assert(brokenConfig.minConnections > 0);
    assert(brokenConfig.maxConnections > 0);
    assert(brokenConfig.connectionTimeout.count() > 0);
    std::cout << "✓ Default value application works correctly\n";
    
    // Test equality operators
    auto config1 = ServerConfig::create();
    auto config2 = ServerConfig::create();
    assert(config1 == config2);
    config2.minConnections = 20;
    assert(config1 != config2);
    std::cout << "✓ Equality operators work correctly\n";
    
    std::cout << "ServerConfig tests completed successfully!\n\n";
}

void testPerformanceMonitor() {
    std::cout << "Testing PerformanceMonitor functionality...\n";
    
    PerformanceMonitor monitor;
    
    // Test initial state
    auto metrics = monitor.getMetrics();
    assert(metrics.totalRequests == 0);
    assert(metrics.activeRequests == 0);
    assert(metrics.averageResponseTime == 0.0);
    assert(metrics.connectionReuses == 0);
    assert(metrics.totalConnections == 0);
    assert(metrics.connectionTimeouts == 0);
    assert(metrics.requestTimeouts == 0);
    std::cout << "✓ Initial metrics state is correct\n";
    
    // Test request tracking
    monitor.recordRequestStart();
    metrics = monitor.getMetrics();
    assert(metrics.totalRequests == 1);
    assert(metrics.activeRequests == 1);
    std::cout << "✓ Request start tracking works\n";
    
    monitor.recordRequestEnd(std::chrono::milliseconds{100});
    metrics = monitor.getMetrics();
    assert(metrics.totalRequests == 1);
    assert(metrics.activeRequests == 0);
    assert(metrics.averageResponseTime > 0.0);
    std::cout << "✓ Request end tracking works\n";
    
    // Test connection tracking
    monitor.recordNewConnection();
    monitor.recordConnectionReuse();
    metrics = monitor.getMetrics();
    assert(metrics.totalConnections == 1);
    assert(metrics.connectionReuses == 1);
    assert(metrics.connectionReuseRate == 1.0); // 100% reuse rate
    std::cout << "✓ Connection tracking works\n";
    
    // Test timeout tracking
    monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
    monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
    metrics = monitor.getMetrics();
    assert(metrics.connectionTimeouts == 1);
    assert(metrics.requestTimeouts == 1);
    std::cout << "✓ Timeout tracking works\n";
    
    // Test multiple requests for average calculation
    for (int i = 0; i < 10; ++i) {
        monitor.recordRequestStart();
        monitor.recordRequestEnd(std::chrono::milliseconds{50 + i * 10});
    }
    metrics = monitor.getMetrics();
    assert(metrics.totalRequests == 11); // 1 + 10
    assert(metrics.activeRequests == 0);
    assert(metrics.averageResponseTime > 0.0);
    std::cout << "✓ Multiple request tracking and averaging works\n";
    
    // Test response time percentiles
    auto responseTimes = monitor.getResponseTimes();
    assert(responseTimes.size() == 11); // All recorded response times
    
    auto p50 = monitor.getPercentileResponseTime(0.5);
    auto p95 = monitor.getPercentileResponseTime(0.95);
    assert(p50.count() > 0);
    assert(p95.count() >= p50.count());
    std::cout << "✓ Response time percentile calculation works\n";
    
    // Test reset functionality
    monitor.reset();
    metrics = monitor.getMetrics();
    assert(metrics.totalRequests == 0);
    assert(metrics.activeRequests == 0);
    assert(metrics.averageResponseTime == 0.0);
    assert(metrics.connectionReuses == 0);
    assert(metrics.totalConnections == 0);
    assert(metrics.connectionTimeouts == 0);
    assert(metrics.requestTimeouts == 0);
    responseTimes = monitor.getResponseTimes();
    assert(responseTimes.empty());
    std::cout << "✓ Reset functionality works\n";
    
    std::cout << "PerformanceMonitor tests completed successfully!\n\n";
}

void testThreadSafety() {
    std::cout << "Testing thread safety...\n";
    
    PerformanceMonitor monitor;
    const int numThreads = 4;
    const int operationsPerThread = 1000;
    
    std::vector<std::thread> threads;
    
    // Launch threads that perform concurrent operations
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&monitor, operationsPerThread]() {
            for (int i = 0; i < operationsPerThread; ++i) {
                monitor.recordRequestStart();
                monitor.recordRequestEnd(std::chrono::milliseconds{10 + (i % 100)});
                monitor.recordNewConnection();
                monitor.recordConnectionReuse();
                
                if (i % 10 == 0) {
                    monitor.recordTimeout(PerformanceMonitor::TimeoutType::CONNECTION);
                }
                if (i % 15 == 0) {
                    monitor.recordTimeout(PerformanceMonitor::TimeoutType::REQUEST);
                }
                
                // Occasionally get metrics to test concurrent read access
                if (i % 100 == 0) {
                    auto metrics = monitor.getMetrics();
                    (void)metrics; // Suppress unused variable warning
                }
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify final metrics
    auto metrics = monitor.getMetrics();
    assert(metrics.totalRequests == numThreads * operationsPerThread);
    assert(metrics.activeRequests == 0);
    assert(metrics.totalConnections == numThreads * operationsPerThread);
    assert(metrics.connectionReuses == numThreads * operationsPerThread);
    
    // For timeout counts, calculate expected values more carefully
    size_t expectedConnectionTimeouts = 0;
    size_t expectedRequestTimeouts = 0;
    for (int i = 0; i < operationsPerThread; ++i) {
        if (i % 10 == 0) expectedConnectionTimeouts++;
        if (i % 15 == 0) expectedRequestTimeouts++;
    }
    expectedConnectionTimeouts *= numThreads;
    expectedRequestTimeouts *= numThreads;
    
    assert(metrics.connectionTimeouts == expectedConnectionTimeouts);
    assert(metrics.requestTimeouts == expectedRequestTimeouts);
    
    std::cout << "✓ Thread safety test completed successfully\n";
    std::cout << "  Total requests: " << metrics.totalRequests << "\n";
    std::cout << "  Average response time: " << metrics.averageResponseTime << "ms\n";
    std::cout << "  Connection reuse rate: " << (metrics.connectionReuseRate * 100) << "%\n";
    std::cout << "  Connection timeouts: " << metrics.connectionTimeouts << "\n";
    std::cout << "  Request timeouts: " << metrics.requestTimeouts << "\n\n";
}

int main() {
    std::cout << "Starting ServerConfig and PerformanceMonitor tests...\n\n";
    
    try {
        testServerConfig();
        testPerformanceMonitor();
        testThreadSafety();
        
        std::cout << "All tests passed successfully! ✓\n";
        std::cout << "Configuration and metrics infrastructure is ready for use.\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}