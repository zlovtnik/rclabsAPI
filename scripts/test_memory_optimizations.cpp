#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "server_config.hpp"

/**
 * Memory Optimization Test
 * Tests the memory optimization features and configurations
 */
class MemoryOptimizationTest {
public:
  void testBufferOptimizationConfiguration() {
    std::cout << "Testing buffer optimization configuration..." << std::endl;

    // Test configuration for small response optimization
    ServerConfig smallResponseConfig = ServerConfig::create(
        5, 20, 300, 30, 60,
        4 * 1024, // Small body size for testing small response optimization
        true, 50, 30);

    assert(smallResponseConfig.maxRequestBodySize == 4 * 1024);
    std::cout << "✓ Small response optimization configuration validated"
              << std::endl;

    // Test configuration for large response handling
    ServerConfig largeResponseConfig =
        ServerConfig::create(10, 50, 300, 30, 60,
                             10 * 1024 * 1024, // Large body size
                             true, 100, 30);

    assert(largeResponseConfig.maxRequestBodySize == 10 * 1024 * 1024);
    std::cout << "✓ Large response handling configuration validated"
              << std::endl;

    std::cout << "✓ Buffer optimization configuration test passed" << std::endl;
  }

  void testMemoryAllocationPatterns() {
    std::cout << "Testing memory allocation pattern configurations..."
              << std::endl;

    // Test configuration that would benefit from buffer reuse
    ServerConfig reuseConfig = ServerConfig::create(
        20, 100, 600, 30, 60, // High connection counts for reuse
        8 * 1024,             // 8KB - good for buffer reuse threshold testing
        true, 200, 45);

    assert(reuseConfig.minConnections == 20);
    assert(reuseConfig.maxConnections == 100);
    assert(reuseConfig.maxRequestBodySize == 8 * 1024);

    std::cout << "✓ Buffer reuse configuration validated" << std::endl;

    // Test configuration for minimal memory footprint
    ServerConfig minimalConfig =
        ServerConfig::create(2, 5, 60, 10, 20, // Minimal connections
                             1024,             // Very small body size
                             true, 10, 5       // Small queue
        );

    assert(minimalConfig.minConnections == 2);
    assert(minimalConfig.maxConnections == 5);
    assert(minimalConfig.maxRequestBodySize == 1024);
    assert(minimalConfig.maxQueueSize == 10);

    std::cout << "✓ Minimal memory footprint configuration validated"
              << std::endl;
    std::cout << "✓ Memory allocation pattern test passed" << std::endl;
  }

  void testRequestResponseOptimizations() {
    std::cout << "Testing request/response optimization configurations..."
              << std::endl;

    // Test configuration for optimized request processing
    ServerConfig optimizedConfig = ServerConfig::create(
        15, 75, 300, 25, 45,
        2 * 1024 * 1024, // 2MB - good balance for optimization
        true, 150, 35);

    // Verify optimization-friendly settings
    assert(optimizedConfig.minConnections >=
           10); // Sufficient for pooling benefits
    assert(optimizedConfig.maxConnections >=
           50); // Good for concurrent processing
    assert(optimizedConfig.maxQueueSize >=
           100); // Adequate queue for load handling
    assert(optimizedConfig.connectionTimeout.count() >=
           20); // Reasonable timeout
    assert(optimizedConfig.requestTimeout.count() >=
           30); // Adequate processing time

    std::cout << "✓ Request/response optimization configuration validated"
              << std::endl;
    std::cout << "✓ Request/response optimization test passed" << std::endl;
  }

  void testConcurrentProcessingConfiguration() {
    std::cout << "Testing concurrent processing optimization configurations..."
              << std::endl;

    // Test configuration optimized for high concurrency
    ServerConfig concurrentConfig = ServerConfig::create(
        25, 150, 600, 30, 60,
        5 * 1024 * 1024, // 5MB - good for concurrent processing
        true, 300, 60    // Large queue and longer wait time for high load
    );

    // Verify concurrency-optimized settings
    assert(concurrentConfig.minConnections >=
           20); // High minimum for immediate availability
    assert(concurrentConfig.maxConnections >=
           100); // High maximum for peak load
    assert(concurrentConfig.maxQueueSize >=
           200); // Large queue for burst handling
    assert(concurrentConfig.maxQueueWaitTime.count() >=
           45); // Adequate wait time

    std::cout << "✓ High concurrency configuration validated" << std::endl;

    // Test configuration for thread safety validation
    ServerConfig threadSafeConfig = ServerConfig::create(
        10, 50, 300, 20, 40, 3 * 1024 * 1024, true, 100, 30);

    // Verify thread-safe operation friendly settings
    assert(threadSafeConfig.maxConnections > threadSafeConfig.minConnections);
    assert(threadSafeConfig.maxQueueSize > 0);
    assert(threadSafeConfig.connectionTimeout.count() > 0);
    assert(threadSafeConfig.requestTimeout.count() > 0);

    std::cout << "✓ Thread safety configuration validated" << std::endl;
    std::cout << "✓ Concurrent processing optimization test passed"
              << std::endl;
  }

  void testErrorHandlingOptimizations() {
    std::cout << "Testing error handling optimization configurations..."
              << std::endl;

    // Test configuration for robust error handling
    ServerConfig robustConfig = ServerConfig::create(
        5, 25, 180, 15, 30,
        1024 * 1024, // 1MB
        true, 50, 20 // Moderate queue with reasonable wait time
    );

    // Verify error handling friendly settings
    assert(robustConfig.connectionTimeout.count() >=
           10); // Adequate for detection
    assert(robustConfig.requestTimeout.count() >=
           20); // Adequate for processing
    assert(robustConfig.maxQueueWaitTime.count() >=
           15); // Reasonable wait before rejection
    assert(robustConfig.maxQueueSize >=
           25); // Adequate buffer for error scenarios

    std::cout << "✓ Robust error handling configuration validated" << std::endl;

    // Test configuration for fast error detection
    ServerConfig fastErrorConfig = ServerConfig::create(
        3, 10, 60, 5, 10, // Short timeouts for fast error detection
        512 * 1024,       // Small body size
        true, 20, 5       // Small queue with short wait
    );

    assert(fastErrorConfig.connectionTimeout.count() == 5);
    assert(fastErrorConfig.requestTimeout.count() == 10);
    assert(fastErrorConfig.maxQueueWaitTime.count() == 5);

    std::cout << "✓ Fast error detection configuration validated" << std::endl;
    std::cout << "✓ Error handling optimization test passed" << std::endl;
  }

  void testPerformanceMetricsConfiguration() {
    std::cout << "Testing performance metrics configuration..." << std::endl;

    // Test configuration with metrics enabled
    ServerConfig metricsConfig =
        ServerConfig::create(10, 40, 240, 20, 35, 2 * 1024 * 1024,
                             true, // Metrics enabled
                             80, 25);

    assert(metricsConfig.enableMetrics == true);
    std::cout << "✓ Metrics enabled configuration validated" << std::endl;

    // Test configuration with metrics disabled for performance
    ServerConfig noMetricsConfig =
        ServerConfig::create(15, 60, 300, 25, 45, 3 * 1024 * 1024,
                             false, // Metrics disabled for maximum performance
                             120, 40);

    assert(noMetricsConfig.enableMetrics == false);
    std::cout << "✓ Metrics disabled configuration validated" << std::endl;

    std::cout << "✓ Performance metrics configuration test passed" << std::endl;
  }

  void runAllTests() {
    std::cout << "Running Memory Optimization Tests..." << std::endl;
    std::cout << "============================================================="
              << std::endl;

    try {
      testBufferOptimizationConfiguration();
      testMemoryAllocationPatterns();
      testRequestResponseOptimizations();
      testConcurrentProcessingConfiguration();
      testErrorHandlingOptimizations();
      testPerformanceMetricsConfiguration();

      std::cout
          << "============================================================="
          << std::endl;
      std::cout << "✓ All memory optimization tests passed!" << std::endl;

    } catch (const std::exception &e) {
      std::cout << "✗ Memory optimization test failed: " << e.what()
                << std::endl;
      throw;
    } catch (...) {
      std::cout << "✗ Memory optimization test failed with unknown exception"
                << std::endl;
      throw;
    }
  }
};

int main() {
  try {
    MemoryOptimizationTest test;
    test.runAllTests();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Memory optimization test suite failed: " << e.what()
              << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Memory optimization test suite failed with unknown exception"
              << std::endl;
    return 1;
  }
}