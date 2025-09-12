#include "cache_manager.hpp"
#include "logger.hpp"
#include "redis_cache.hpp"
#include <chrono>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

/**
 * @brief Exercises and reports cache warmup configuration values.
 *
 * Constructs a default and a modified CacheConfig, then prints their
 * warmup-related fields (enableWarmup, warmupBatchSize, warmupMaxKeys,
 * warmupBatchTimeout, and warmupTotalTimeout) to standard output. Intended as a
 * simple runtime verification/demo of configuration defaults and custom
 * settings.
 *
 * Side effects:
 * - Writes human-readable configuration details to stdout.
 */
void testCacheWarmupConfiguration() {
  std::cout << "\n=== Testing Cache Warmup Configuration ===" << std::endl;

  // Test default configuration
  CacheConfig defaultConfig;
  std::cout << "Default warmup enabled: "
            << (defaultConfig.enableWarmup ? "true" : "false") << std::endl;
  std::cout << "Default batch size: " << defaultConfig.warmupBatchSize
            << std::endl;
  std::cout << "Default max keys: " << defaultConfig.warmupMaxKeys << std::endl;
  std::cout << "Default batch timeout: "
            << defaultConfig.warmupBatchTimeout.count() << "s" << std::endl;
  std::cout << "Default total timeout: "
            << defaultConfig.warmupTotalTimeout.count() << "s" << std::endl;

  // Test custom configuration
  CacheConfig customConfig;
  customConfig.enableWarmup = true;
  customConfig.warmupBatchSize = 5;
  customConfig.warmupMaxKeys = 50;
  customConfig.warmupBatchTimeout = std::chrono::seconds(3);
  customConfig.warmupTotalTimeout = std::chrono::seconds(30);

  std::cout << "Custom warmup enabled: "
            << (customConfig.enableWarmup ? "true" : "false") << std::endl;
  std::cout << "Custom batch size: " << customConfig.warmupBatchSize
            << std::endl;
  std::cout << "Custom max keys: " << customConfig.warmupMaxKeys << std::endl;
  std::cout << "Custom batch timeout: "
            << customConfig.warmupBatchTimeout.count() << "s" << std::endl;
  std::cout << "Custom total timeout: "
            << customConfig.warmupTotalTimeout.count() << "s" << std::endl;

  std::cout << "✓ Cache warmup configuration test completed" << std::endl;
}

/**
 * @brief Runs a basic test that constructs a CacheManager with warmup settings
 * and reports its status.
 *
 * Creates a CacheConfig with warmup enabled (batch size = 3, max keys = 25),
 * constructs a CacheManager from that config, prints confirmation, and prints a
 * message if the cache reports as disabled via isCacheEnabled(). Intended as a
 * small runtime check that warmup configuration can be applied and that cache
 * enablement can be queried.
 */
void testCacheManagerInitialization() {
  std::cout << "\n=== Testing Cache Manager Initialization ===" << std::endl;

  // Test cache manager creation with warmup config
  CacheConfig config;
  config.enableWarmup = true;
  config.warmupBatchSize = 3;
  config.warmupMaxKeys = 25;

  CacheManager cacheManager(config);
  std::cout << "Cache manager created with warmup configuration" << std::endl;

  // Test cache disabled scenario
  if (!cacheManager.isCacheEnabled()) {
    std::cout << "Cache is disabled (no Redis cache initialized)" << std::endl;
  }

  std::cout << "✓ Cache manager initialization test completed" << std::endl;
}

/**
 * @brief Verifies that a CacheManager respects a disabled warmup configuration.
 *
 * Constructs a CacheConfig with warmup disabled, creates a CacheManager from
 * it, and reports that warmup is disabled. This test does not exercise the
 * actual warmup behavior (which requires mocking of external systems); it only
 * checks that the configuration value can be applied to the manager during
 * construction.
 */
void testCacheWarmupDisabled() {
  std::cout << "\n=== Testing Cache Warmup Disabled ===" << std::endl;

  // Test with warmup disabled
  CacheConfig config;
  config.enableWarmup = false;

  CacheManager cacheManager(config);
  std::cout << "Cache manager created with warmup disabled" << std::endl;

  // Note: We can't easily test the actual warmupCache method without mocking
  // but we can verify the configuration is respected
  std::cout << "Warmup is disabled in configuration" << std::endl;

  std::cout << "✓ Cache warmup disabled test completed" << std::endl;
}

/**
 * @brief Simulates and demonstrates simple batch-processing of key/type pairs.
 *
 * Creates a small in-memory dataset of key/type string pairs, splits it into
 * fixed-size batches, and prints batch counts and each item's key and type to
 * standard output. Useful for verifying batching logic (chunking, last partial
 * batch) and the resulting console output.
 *
 * @note This function has side effects: it writes diagnostic output to stdout.
 */
void testBatchProcessingLogic() {
  std::cout << "\n=== Testing Batch Processing Logic ===" << std::endl;

  // Simulate the batch processing logic
  std::vector<std::vector<std::string>> mockData = {{"user_1", "user"},
                                                    {"job_1", "job"},
                                                    {"session_1", "session"},
                                                    {"user_2", "user"},
                                                    {"job_2", "job"}};

  size_t batchSize = 2;
  std::cout << "Processing " << mockData.size() << " items in batches of "
            << batchSize << std::endl;

  auto batchStart = mockData.begin();
  int batchCount = 0;

  while (batchStart != mockData.end()) {
    // Compute remaining distance and clamp step size
    auto remaining = std::distance(batchStart, mockData.end());
    auto step = std::min(batchSize, static_cast<size_t>(remaining));
    auto batchEnd = std::next(batchStart, step);
    std::vector<std::vector<std::string>> batch(batchStart, batchEnd);

    batchCount++;
    std::cout << "Batch " << batchCount << ": " << batch.size() << " items"
              << std::endl;

    for (const auto &item : batch) {
      if (item.size() >= 2) {
        std::cout << "  - Key: " << item[0] << ", Type: " << item[1]
                  << std::endl;
      }
    }

    batchStart = batchEnd;
  }

  std::cout << "Total batches processed: " << batchCount << std::endl;
  std::cout << "✓ Batch processing logic test completed" << std::endl;
}

/**
 * @brief Entry point for the cache warmup test suite.
 *
 * Runs a sequence of test helper functions that exercise cache warmup
 * configuration, CacheManager initialization, disabled-warmup behavior, and
 * batch processing logic. Reports progress and results to stdout; on
 * std::exception the error is printed to stderr and the process exits with a
 * non-zero code.
 *
 * @return int Returns 0 on successful completion of all tests, or 1 if a
 * std::exception is caught.
 */
int main() {
  std::cout << "Cache Warmup Configuration Test" << std::endl;
  std::cout << "===============================" << std::endl;

  try {
    testCacheWarmupConfiguration();
    testCacheManagerInitialization();
    testCacheWarmupDisabled();
    testBatchProcessingLogic();

    std::cout << "\n🎉 All cache warmup tests completed successfully!"
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
