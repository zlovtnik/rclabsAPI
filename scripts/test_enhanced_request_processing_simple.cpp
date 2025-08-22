#include <iostream>
#include <cassert>
#include <chrono>

#include "server_config.hpp"

/**
 * Enhanced Request Processing Configuration Test
 * This test validates the enhanced configuration options and optimization features
 */
class EnhancedRequestProcessingTest {
public:
    void testQueueConfiguration() {
        std::cout << "Testing request queue configuration..." << std::endl;
        
        // Test queue configuration with various settings
        ServerConfig config = ServerConfig::create(
            5,   // minConnections
            10,  // maxConnections
            60,  // idleTimeoutSec
            10,  // connTimeoutSec
            30,  // reqTimeoutSec
            1024 * 1024, // maxBodySize (1MB)
            true, // metricsEnabled
            50,   // maxQueueSize
            15    // maxQueueWaitTimeSec
        );
        
        // Verify queue configuration
        assert(config.maxQueueSize == 50);
        assert(config.maxQueueWaitTime.count() == 15);
        
        std::cout << "✓ Queue configuration test passed" << std::endl;
    }

    void testPoolExhaustionConfiguration() {
        std::cout << "Testing pool exhaustion configuration..." << std::endl;
        
        // Test configuration for pool exhaustion scenarios
        ServerConfig config = ServerConfig::create(
            1,   // minConnections (small)
            2,   // maxConnections (small to force exhaustion)
            30,  // idleTimeoutSec
            5,   // connTimeoutSec
            10,  // reqTimeoutSec
            512 * 1024, // maxBodySize
            true, // metricsEnabled
            5,    // maxQueueSize (small)
            3     // maxQueueWaitTimeSec (short)
        );
        
        // Verify exhaustion handling configuration
        assert(config.maxConnections == 2);
        assert(config.maxQueueSize == 5);
        assert(config.maxQueueWaitTime.count() == 3);
        
        std::cout << "✓ Pool exhaustion configuration test passed" << std::endl;
    }

    void testMemoryOptimizationConfiguration() {
        std::cout << "Testing memory optimization configuration..." << std::endl;
        
        // Test configuration for memory optimization
        ServerConfig config = ServerConfig::create(
            10,  // minConnections
            50,  // maxConnections
            120, // idleTimeoutSec
            20,  // connTimeoutSec
            40,  // reqTimeoutSec
            4 * 1024, // maxBodySize (small for testing small response optimization)
            true, // metricsEnabled
            100,  // maxQueueSize
            30    // maxQueueWaitTimeSec
        );
        
        // Verify memory optimization settings
        assert(config.maxRequestBodySize == 4 * 1024);
        assert(config.maxConnections == 50);
        assert(config.maxQueueSize == 100);
        
        std::cout << "✓ Memory optimization configuration test passed" << std::endl;
    }

    void testConfigurationValidation() {
        std::cout << "Testing enhanced configuration validation..." << std::endl;
        
        // Test invalid queue configuration
        ServerConfig invalidConfig;
        invalidConfig.maxQueueSize = 0; // Invalid
        invalidConfig.maxQueueWaitTime = std::chrono::seconds{-1}; // Invalid
        
        auto validation = invalidConfig.validate();
        assert(!validation.isValid);
        
        // Check that we have errors for queue settings
        bool hasQueueSizeError = false;
        bool hasQueueWaitTimeError = false;
        for (const auto& error : validation.errors) {
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
        ServerConfig warningConfig = ServerConfig::create(
            10, 100, 300, 30, 60, 10*1024*1024, true,
            2000, // Very large queue size
            400   // Very long wait time
        );
        
        auto warningValidation = warningConfig.validate();
        assert(warningValidation.isValid); // Should be valid but have warnings
        assert(!warningValidation.warnings.empty()); // Should have warnings
        
        std::cout << "✓ Configuration warning detection passed" << std::endl;
        std::cout << "✓ Enhanced configuration validation test passed" << std::endl;
    }

    void testThreadSafetyConfiguration() {
        std::cout << "Testing thread safety configuration..." << std::endl;
        
        // Test configuration for high concurrency
        ServerConfig config = ServerConfig::create(
            20,  // minConnections (high for concurrency)
            100, // maxConnections (high for concurrency)
            180, // idleTimeoutSec
            25,  // connTimeoutSec
            50,  // reqTimeoutSec
            2 * 1024 * 1024, // maxBodySize
            true, // metricsEnabled
            200,  // maxQueueSize (large for high load)
            45    // maxQueueWaitTimeSec
        );
        
        // Verify high concurrency configuration
        assert(config.minConnections == 20);
        assert(config.maxConnections == 100);
        assert(config.maxQueueSize == 200);
        assert(config.maxQueueWaitTime.count() == 45);
        
        std::cout << "✓ Thread safety configuration test passed" << std::endl;
    }

    void testErrorHandlingConfiguration() {
        std::cout << "Testing error handling configuration..." << std::endl;
        
        // Test configuration for error handling scenarios
        ServerConfig config = ServerConfig::create(
            1,   // minConnections (minimal)
            2,   // maxConnections (minimal)
            15,  // idleTimeoutSec (short)
            3,   // connTimeoutSec (very short)
            5,   // reqTimeoutSec (very short)
            256 * 1024, // maxBodySize (small)
            true, // metricsEnabled
            3,    // maxQueueSize (very small)
            2     // maxQueueWaitTimeSec (very short)
        );
        
        // Verify error handling configuration
        assert(config.maxConnections == 2);
        assert(config.maxQueueSize == 3);
        assert(config.connectionTimeout.count() == 3);
        assert(config.requestTimeout.count() == 5);
        assert(config.maxQueueWaitTime.count() == 2);
        
        std::cout << "✓ Error handling configuration test passed" << std::endl;
    }

    void cleanup() {
        // No cleanup needed for configuration tests
    }

    void runAllTests() {
        std::cout << "Running Enhanced Request Processing Configuration Tests..." << std::endl;
        std::cout << "=============================================================" << std::endl;
        
        try {
            testQueueConfiguration();
            testPoolExhaustionConfiguration();
            testMemoryOptimizationConfiguration();
            testConfigurationValidation();
            testThreadSafetyConfiguration();
            testErrorHandlingConfiguration();
            
            std::cout << "=============================================================" << std::endl;
            std::cout << "✓ All enhanced request processing configuration tests passed!" << std::endl;
            
        } catch (const std::exception& e) {
            std::cout << "✗ Configuration test failed with exception: " << e.what() << std::endl;
            cleanup();
            throw;
        } catch (...) {
            std::cout << "✗ Configuration test failed with unknown exception" << std::endl;
            cleanup();
            throw;
        }
    }
};

int main() {
    try {
        // Configuration tests don't require logging setup
        
        EnhancedRequestProcessingTest test;
        test.runAllTests();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Enhanced request processing configuration test suite failed: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Enhanced request processing configuration test suite failed with unknown exception" << std::endl;
        return 1;
    }
}