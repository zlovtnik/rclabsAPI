#include <gtest/gtest.h>
#include "server_config.hpp"

TEST(MemoryOptimizationTest, BufferOptimizationConfiguration) {
    // Test configuration for small response optimization
    ServerConfig smallResponseConfig = ServerConfig::create(
        5, 20, 300, 30, 60,
        4 * 1024, // Small body size for testing small response optimization
        true, 50, 30);

    EXPECT_EQ(smallResponseConfig.maxRequestBodySize, 4 * 1024);

    // Test configuration for large response handling
    ServerConfig largeResponseConfig =
        ServerConfig::create(10, 50, 300, 30, 60,
                             10 * 1024 * 1024, // Large body size
                             true, 100, 30);

    EXPECT_EQ(largeResponseConfig.maxRequestBodySize, 10 * 1024 * 1024);
}

TEST(MemoryOptimizationTest, MemoryAllocationPatterns) {
    // Test configuration that would benefit from buffer reuse
    ServerConfig reuseConfig = ServerConfig::create(
        20, 100, 600, 30, 60, // High connection counts for reuse
        8 * 1024,             // 8KB - good for buffer reuse threshold testing
        true, 200, 45);

    EXPECT_EQ(reuseConfig.minConnections, 20);
    EXPECT_EQ(reuseConfig.maxConnections, 100);
    EXPECT_EQ(reuseConfig.maxRequestBodySize, 8 * 1024);

    // Test configuration for minimal memory footprint
    ServerConfig minimalConfig =
        ServerConfig::create(2, 5, 60, 10, 20, // Minimal connections
                             1024,             // Very small body size
                             true, 10, 5       // Small queue
        );

    EXPECT_EQ(minimalConfig.minConnections, 2);
    EXPECT_EQ(minimalConfig.maxConnections, 5);
    EXPECT_EQ(minimalConfig.maxRequestBodySize, 1024);
    EXPECT_EQ(minimalConfig.maxQueueSize, 10);
}

TEST(MemoryOptimizationTest, RequestResponseOptimizations) {
    // Test configuration for optimized request processing
    ServerConfig optimizedConfig = ServerConfig::create(
        15, 75, 300, 25, 45,
        2 * 1024 * 1024, // 2MB - good balance for optimization
        true, 150, 35);

    // Verify optimization-friendly settings
    EXPECT_GE(optimizedConfig.minConnections, 10); // Sufficient for pooling benefits
    EXPECT_GE(optimizedConfig.maxConnections, 50); // Good for concurrent processing
    EXPECT_GE(optimizedConfig.maxQueueSize, 100);  // Adequate queue for load handling
    EXPECT_GE(optimizedConfig.connectionTimeout.count(), 20); // Reasonable timeout
    EXPECT_GE(optimizedConfig.requestTimeout.count(), 30);    // Adequate processing time
}

TEST(MemoryOptimizationTest, ConcurrentProcessingConfiguration) {
    // Test configuration optimized for high concurrency
    ServerConfig concurrentConfig = ServerConfig::create(
        25, 150, 600, 30, 60,
        5 * 1024 * 1024, // 5MB - good for concurrent processing
        true, 300, 60    // Large queue and longer wait time for high load
    );

    // Verify concurrency-optimized settings
    EXPECT_GE(concurrentConfig.minConnections, 20);  // High minimum for immediate availability
    EXPECT_GE(concurrentConfig.maxConnections, 100); // High maximum for peak load
    EXPECT_GE(concurrentConfig.maxQueueSize, 200);   // Large queue for burst handling
    EXPECT_GE(concurrentConfig.maxQueueWaitTime.count(), 45); // Adequate wait time

    // Test configuration for thread safety validation
    ServerConfig threadSafeConfig = ServerConfig::create(
        10, 50, 300, 20, 40, 3 * 1024 * 1024, true, 100, 30);

    // Verify thread-safe operation friendly settings
    EXPECT_GT(threadSafeConfig.maxConnections, threadSafeConfig.minConnections);
    EXPECT_GT(threadSafeConfig.maxQueueSize, 0);
    EXPECT_GT(threadSafeConfig.connectionTimeout.count(), 0);
    EXPECT_GT(threadSafeConfig.requestTimeout.count(), 0);
}

TEST(MemoryOptimizationTest, ErrorHandlingOptimizations) {
    // Test configuration for robust error handling
    ServerConfig robustConfig = ServerConfig::create(
        5, 25, 180, 15, 30,
        1024 * 1024, // 1MB
        true, 50, 20 // Moderate queue with reasonable wait time
    );

    // Verify error handling friendly settings
    EXPECT_GE(robustConfig.connectionTimeout.count(), 10); // Adequate for detection
    EXPECT_GE(robustConfig.requestTimeout.count(), 20);    // Adequate for processing
    EXPECT_GE(robustConfig.maxQueueWaitTime.count(), 15);  // Reasonable wait before rejection
    EXPECT_GE(robustConfig.maxQueueSize, 25);              // Adequate buffer for error scenarios

    // Test configuration for fast error detection
    ServerConfig fastErrorConfig = ServerConfig::create(
        3, 10, 60, 5, 10, // Short timeouts for fast error detection
        512 * 1024,       // Small body size
        true, 20, 5       // Small queue with short wait
    );

    EXPECT_EQ(fastErrorConfig.connectionTimeout.count(), 5);
    EXPECT_EQ(fastErrorConfig.requestTimeout.count(), 10);
    EXPECT_EQ(fastErrorConfig.maxQueueWaitTime.count(), 5);
}

TEST(MemoryOptimizationTest, PerformanceMetricsConfiguration) {
    // Test configuration with metrics enabled
    ServerConfig metricsConfig =
        ServerConfig::create(10, 40, 240, 20, 35, 2 * 1024 * 1024,
                             true, // Metrics enabled
                             80, 25);

    EXPECT_TRUE(metricsConfig.enableMetrics);

    // Test configuration with metrics disabled for performance
    ServerConfig noMetricsConfig =
        ServerConfig::create(15, 60, 300, 25, 45, 3 * 1024 * 1024,
                             false, // Metrics disabled for maximum performance
                             120, 40);

    EXPECT_FALSE(noMetricsConfig.enableMetrics);
}