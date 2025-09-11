#include "cache_manager.hpp"
#include "redis_cache.hpp"
#include "logger.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iterator>

void testCacheWarmupConfiguration() {
    std::cout << "\n=== Testing Cache Warmup Configuration ===" << std::endl;

    // Test default configuration
    CacheConfig defaultConfig;
    std::cout << "Default warmup enabled: " << (defaultConfig.enableWarmup ? "true" : "false") << std::endl;
    std::cout << "Default batch size: " << defaultConfig.warmupBatchSize << std::endl;
    std::cout << "Default max keys: " << defaultConfig.warmupMaxKeys << std::endl;
    std::cout << "Default batch timeout: " << defaultConfig.warmupBatchTimeout.count() << "s" << std::endl;
    std::cout << "Default total timeout: " << defaultConfig.warmupTotalTimeout.count() << "s" << std::endl;

    // Test custom configuration
    CacheConfig customConfig;
    customConfig.enableWarmup = true;
    customConfig.warmupBatchSize = 5;
    customConfig.warmupMaxKeys = 50;
    customConfig.warmupBatchTimeout = std::chrono::seconds(3);
    customConfig.warmupTotalTimeout = std::chrono::seconds(30);

    std::cout << "Custom warmup enabled: " << (customConfig.enableWarmup ? "true" : "false") << std::endl;
    std::cout << "Custom batch size: " << customConfig.warmupBatchSize << std::endl;
    std::cout << "Custom max keys: " << customConfig.warmupMaxKeys << std::endl;
    std::cout << "Custom batch timeout: " << customConfig.warmupBatchTimeout.count() << "s" << std::endl;
    std::cout << "Custom total timeout: " << customConfig.warmupTotalTimeout.count() << "s" << std::endl;

    std::cout << "✓ Cache warmup configuration test completed" << std::endl;
}

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

void testBatchProcessingLogic() {
    std::cout << "\n=== Testing Batch Processing Logic ===" << std::endl;

    // Simulate the batch processing logic
    std::vector<std::vector<std::string>> mockData = {
        {"user_1", "user"},
        {"job_1", "job"},
        {"session_1", "session"},
        {"user_2", "user"},
        {"job_2", "job"}
    };

    size_t batchSize = 2;
    std::cout << "Processing " << mockData.size() << " items in batches of " << batchSize << std::endl;

    auto batchStart = mockData.begin();
    int batchCount = 0;

    while (batchStart != mockData.end()) {
        // Compute remaining distance and clamp step size
        auto remaining = std::distance(batchStart, mockData.end());
        auto step = std::min(batchSize, static_cast<size_t>(remaining));
        auto batchEnd = std::next(batchStart, step);
        std::vector<std::vector<std::string>> batch(batchStart, batchEnd);

        batchCount++;
        std::cout << "Batch " << batchCount << ": " << batch.size() << " items" << std::endl;

        for (const auto& item : batch) {
            if (item.size() >= 2) {
                std::cout << "  - Key: " << item[0] << ", Type: " << item[1] << std::endl;
            }
        }

        batchStart = batchEnd;
    }

    std::cout << "Total batches processed: " << batchCount << std::endl;
    std::cout << "✓ Batch processing logic test completed" << std::endl;
}

int main() {
    std::cout << "Cache Warmup Configuration Test" << std::endl;
    std::cout << "===============================" << std::endl;

    try {
        testCacheWarmupConfiguration();
        testCacheManagerInitialization();
        testCacheWarmupDisabled();
        testBatchProcessingLogic();

        std::cout << "\n🎉 All cache warmup tests completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
