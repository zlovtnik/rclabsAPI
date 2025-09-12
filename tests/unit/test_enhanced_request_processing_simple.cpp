#include <cassert>
#include <chrono>
#include <iostream>

#include "server_config.hpp"

/**
 * Enhanced Request Processing Configuration Test
 * This test validates the enhanced configuration options and optimization
 * features
 */
class EnhancedRequestProcessingTest {
public:
  /**
   * @brief Verifies that ServerConfig correctly stores request queue
   * parameters.
   *
   * Creates a ServerConfig with specific queue settings (maxQueueSize and
   * maxQueueWaitTime) and asserts those values are preserved on the resulting
   * configuration object.
   *
   * This is a unit test helper; it will abort the process if assertions fail.
   */
  void testQueueConfiguration() {
    std::cout << "Testing request queue configuration..." << std::endl;

    // Test queue configuration with various settings
    ServerConfig config = ServerConfig::create(5,           // minConnections
                                               10,          // maxConnections
                                               60,          // idleTimeoutSec
                                               10,          // connTimeoutSec
                                               30,          // reqTimeoutSec
                                               1024 * 1024, // maxBodySize (1MB)
                                               true,        // metricsEnabled
                                               50,          // maxQueueSize
                                               15 // maxQueueWaitTimeSec
    );

    // Verify queue configuration
    assert(config.maxQueueSize == 50);
    assert(config.maxQueueWaitTime.count() == 15);

    std::cout << "✓ Queue configuration test passed" << std::endl;
  }

  /**
   * @brief Tests server configuration parameters intended to exercise
   * connection-pool exhaustion scenarios.
   *
   * Creates a ServerConfig with a deliberately small connection pool and small
   * queue settings, then validates that the produced configuration exposes the
   * expected limits for maxConnections, maxQueueSize, and maxQueueWaitTime.
   *
   * The test uses assertions to verify the values and will terminate the
   * process if any assertion fails.
   */
  void testPoolExhaustionConfiguration() {
    std::cout << "Testing pool exhaustion configuration..." << std::endl;

    // Test configuration for pool exhaustion scenarios
    ServerConfig config =
        ServerConfig::create(1,  // minConnections (small)
                             2,  // maxConnections (small to force exhaustion)
                             30, // idleTimeoutSec
                             5,  // connTimeoutSec
                             10, // reqTimeoutSec
                             512 * 1024, // maxBodySize
                             true,       // metricsEnabled
                             5,          // maxQueueSize (small)
                             3           // maxQueueWaitTimeSec (short)
        );

    // Verify exhaustion handling configuration
    assert(config.maxConnections == 2);
    assert(config.maxQueueSize == 5);
    assert(config.maxQueueWaitTime.count() == 3);

    std::cout << "✓ Pool exhaustion configuration test passed" << std::endl;
  }

  /**
   * @brief Tests that ServerConfig supports memory-optimized settings.
   *
   * Creates a ServerConfig with a small maxRequestBodySize and typical
   * connection/queue parameters, then asserts that key memory-related fields
   * match the expected values. Assertion failures indicate the configuration
   * did not apply memory-optimization parameters correctly.
   */
  void testMemoryOptimizationConfiguration() {
    std::cout << "Testing memory optimization configuration..." << std::endl;

    // Test configuration for memory optimization
    ServerConfig config = ServerConfig::create(
        10,       // minConnections
        50,       // maxConnections
        120,      // idleTimeoutSec
        20,       // connTimeoutSec
        40,       // reqTimeoutSec
        4 * 1024, // maxBodySize (small for testing small response optimization)
        true,     // metricsEnabled
        100,      // maxQueueSize
        30        // maxQueueWaitTimeSec
    );

    // Verify memory optimization settings
    assert(config.maxRequestBodySize == 4 * 1024);
    assert(config.maxConnections == 50);
    assert(config.maxQueueSize == 100);

    std::cout << "✓ Memory optimization configuration test passed" << std::endl;
  }

  /**
   * @brief Validates ServerConfig validation, defaulting, and warning behavior.
   *
   * Exercises invalid, defaulting, and warning code paths of ServerConfig:
   * - Constructs an invalid configuration (zero/negative queue values) and
   * asserts validation fails and that error messages mention `maxQueueSize` and
   * `maxQueueWaitTime`.
   * - Calls `applyDefaults()` on the invalid config and asserts defaults are
   * applied (positive queue size and wait time).
   * - Constructs a configuration with excessively large queue settings,
   * validates it, and asserts the configuration is considered valid but
   * produces warnings.
   *
   * This test uses assertions to enforce expected outcomes.
   */
  void testConfigurationValidation() {
    std::cout << "Testing enhanced configuration validation..." << std::endl;

    // Test invalid queue configuration
    ServerConfig invalidConfig;
    invalidConfig.maxQueueSize = 0;                            // Invalid
    invalidConfig.maxQueueWaitTime = std::chrono::seconds{-1}; // Invalid

    auto validation = invalidConfig.validate();
    assert(!validation.isValid);

    // Check that we have errors for queue settings
    bool hasQueueSizeError = false;
    bool hasQueueWaitTimeError = false;
    for (const auto &error : validation.errors) {
      if (error.find("maxQueueSize") != std::string::npos) {
        hasQueueSizeError = true;
      }
      if (error.find("maxQueueWaitTime") != std::string::npos) {
        hasQueueWaitTimeError = true;
      }
    }
    assert(hasQueueSizeError);
    assert(hasQueueWaitTimeError);

    std::cout << "✓ Invalid configuration detection passed" << std::endl;

    // Test configuration defaults
    invalidConfig.applyDefaults();
    assert(invalidConfig.maxQueueSize > 0);
    assert(invalidConfig.maxQueueWaitTime.count() > 0);

    std::cout << "✓ Configuration defaults application passed" << std::endl;

    // Test warning conditions
    ServerConfig warningConfig =
        ServerConfig::create(10, 100, 300, 30, 60, 10 * 1024 * 1024, true,
                             2000, // Very large queue size
                             400   // Very long wait time
        );

    auto warningValidation = warningConfig.validate();
    assert(warningValidation.isValid); // Should be valid but have warnings
    assert(!warningValidation.warnings.empty()); // Should have warnings

    std::cout << "✓ Configuration warning detection passed" << std::endl;
    std::cout << "✓ Enhanced configuration validation test passed" << std::endl;
  }

  /**
   * @brief Tests ServerConfig settings intended for
   * high-concurrency/thread-safety scenarios.
   *
   * Constructs a ServerConfig with high min/max connection counts, a large
   * request queue and extended timeouts, then asserts those fields match the
   * expected values.
   *
   * The function uses assertions to validate: minConnections == 20,
   * maxConnections == 100, maxQueueSize == 200 and maxQueueWaitTime == 45
   * seconds. Assertion failures will terminate the test; on success it prints a
   * short success message to stdout.
   */
  void testThreadSafetyConfiguration() {
    std::cout << "Testing thread safety configuration..." << std::endl;

    // Test configuration for high concurrency
    ServerConfig config =
        ServerConfig::create(20,  // minConnections (high for concurrency)
                             100, // maxConnections (high for concurrency)
                             180, // idleTimeoutSec
                             25,  // connTimeoutSec
                             50,  // reqTimeoutSec
                             2 * 1024 * 1024, // maxBodySize
                             true,            // metricsEnabled
                             200, // maxQueueSize (large for high load)
                             45   // maxQueueWaitTimeSec
        );

    // Verify high concurrency configuration
    assert(config.minConnections == 20);
    assert(config.maxConnections == 100);
    assert(config.maxQueueSize == 200);
    assert(config.maxQueueWaitTime.count() == 45);

    std::cout << "✓ Thread safety configuration test passed" << std::endl;
  }

  /**
   * @brief Verifies ServerConfig behavior for tight error-handling scenarios.
   *
   * Constructs a ServerConfig with minimal connections, very short timeouts,
   * a small request body limit, and a very small request queue, then asserts
   * that the resulting configuration fields match the expected values.
   *
   * Side effects:
   * - Prints progress to stdout.
   * - Uses assert; a failed assertion will terminate the test.
   */
  void testErrorHandlingConfiguration() {
    std::cout << "Testing error handling configuration..." << std::endl;

    // Test configuration for error handling scenarios
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

    // Verify error handling configuration
    assert(config.maxConnections == 2);
    assert(config.maxQueueSize == 3);
    assert(config.connectionTimeout.count() == 3);
    assert(config.requestTimeout.count() == 5);
    assert(config.maxQueueWaitTime.count() == 2);

    std::cout << "✓ Error handling configuration test passed" << std::endl;
  }

  /**
   * @brief Placeholder cleanup hook for the test suite.
   *
   * No resources require explicit teardown for these configuration tests; the
   * function exists to satisfy the test framework's lifecycle and may be
   * extended if future tests allocate resources that need cleanup.
   */
  void cleanup() {
    // No cleanup needed for configuration tests
  }

  /**
   * @brief Run the suite of enhanced request processing configuration tests.
   *
   * Executes each test in sequence: queue configuration, pool exhaustion,
   * memory optimization, configuration validation, thread-safety, and
   * error-handling.
   *
   * @details
   * Reports progress to stdout. If any test throws, this function calls
   * cleanup() to perform teardown and then rethrows the exception to the
   * caller.
   */
  void runAllTests() {
    std::cout << "Running Enhanced Request Processing Configuration Tests..."
              << std::endl;
    std::cout << "============================================================="
              << std::endl;

    try {
      testQueueConfiguration();
      testPoolExhaustionConfiguration();
      testMemoryOptimizationConfiguration();
      testConfigurationValidation();
      testThreadSafetyConfiguration();
      testErrorHandlingConfiguration();

      std::cout
          << "============================================================="
          << std::endl;
      std::cout
          << "✓ All enhanced request processing configuration tests passed!"
          << std::endl;

    } catch (const std::exception &e) {
      std::cout << "✗ Configuration test failed with exception: " << e.what()
                << std::endl;
      cleanup();
      throw;
    } catch (...) {
      std::cout << "✗ Configuration test failed with unknown exception"
                << std::endl;
      cleanup();
      throw;
    }
  }
};

/**
 * @brief Entry point for the enhanced request processing configuration test
 * suite.
 *
 * Runs the EnhancedRequestProcessingTest::runAllTests() sequence and maps
 * outcomes to process exit codes. On successful completion the process exits
 * with 0. If any std::exception is thrown the exception message is written to
 * stderr and the process exits with 1. Any other unexpected exception also
 * results in an error message to stderr and exit code 1.
 *
 * @return int 0 on success, 1 on failure.
 */
int main() {
  try {
    // Configuration tests don't require logging setup

    EnhancedRequestProcessingTest test;
    test.runAllTests();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Enhanced request processing configuration test suite failed: "
              << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Enhanced request processing configuration test suite failed "
                 "with unknown exception"
              << std::endl;
    return 1;
  }
}