#include "../include/config_manager.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>

class ConfigMonitoringDemo {
private:
    std::filesystem::path testDir;
    std::filesystem::path testConfigFile;
    ConfigManager* configManager;

    void createTestConfigFile() {
        std::ofstream file(testConfigFile);
        file << R"({
  "server": {
    "address": "0.0.0.0",
    "port": 8080,
    "threads": 4
  },
  "monitoring": {
    "websocket": {
      "enabled": true,
      "port": 8081,
      "max_connections": 100,
      "heartbeat_interval": 30,
      "message_queue_size": 1000
    },
    "job_tracking": {
      "progress_update_interval": 5,
      "log_streaming_enabled": true,
      "metrics_collection_enabled": true,
      "timeout_warning_threshold": 25
    },
    "notifications": {
      "enabled": true,
      "job_failure_alerts": true,
      "timeout_warnings": true,
      "resource_alerts": true,
      "retry_attempts": 3,
      "retry_delay": 5000
    }
  }
})";
        file.close();
    }

public:
    bool setUp() {
        try {
            testDir = std::filesystem::temp_directory_path() / "etlplus_config_demo";
            std::filesystem::create_directories(testDir);
            
            testConfigFile = testDir / "demo_config.json";
            createTestConfigFile();
            
            configManager = &ConfigManager::getInstance();
            return configManager->loadConfig(testConfigFile.string());
        } catch (const std::exception& e) {
            std::cerr << "Setup failed: " << e.what() << std::endl;
            return false;
        }
    }

    void tearDown() {
        try {
            std::filesystem::remove_all(testDir);
        } catch (const std::exception& e) {
            std::cerr << "Cleanup failed: " << e.what() << std::endl;
        }
    }

    bool testWebSocketConfigRetrieval() {
        std::cout << "\n=== Testing WebSocket Configuration Retrieval ===\n";
        
        auto wsConfig = configManager->getWebSocketConfig();
        
        std::cout << "WebSocket Config:\n";
        std::cout << "  Enabled: " << (wsConfig.enabled ? "true" : "false") << "\n";
        std::cout << "  Port: " << wsConfig.port << "\n";
        std::cout << "  Max Connections: " << wsConfig.maxConnections << "\n";
        std::cout << "  Heartbeat Interval: " << wsConfig.heartbeatInterval << "s\n";
        std::cout << "  Message Queue Size: " << wsConfig.messageQueueSize << "\n";
        
        // Validate values
        bool success = wsConfig.enabled == true &&
                      wsConfig.port == 8081 &&
                      wsConfig.maxConnections == 100 &&
                      wsConfig.heartbeatInterval == 30 &&
                      wsConfig.messageQueueSize == 1000;
        
        std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
        return success;
    }

    bool testJobTrackingConfigRetrieval() {
        std::cout << "\n=== Testing Job Tracking Configuration Retrieval ===\n";
        
        auto jtConfig = configManager->getJobTrackingConfig();
        
        std::cout << "Job Tracking Config:\n";
        std::cout << "  Progress Update Interval: " << jtConfig.progressUpdateInterval << "s\n";
        std::cout << "  Log Streaming Enabled: " << (jtConfig.logStreamingEnabled ? "true" : "false") << "\n";
        std::cout << "  Metrics Collection Enabled: " << (jtConfig.metricsCollectionEnabled ? "true" : "false") << "\n";
        std::cout << "  Timeout Warning Threshold: " << jtConfig.timeoutWarningThreshold << " minutes\n";
        
        // Validate values
        bool success = jtConfig.progressUpdateInterval == 5 &&
                      jtConfig.logStreamingEnabled == true &&
                      jtConfig.metricsCollectionEnabled == true &&
                      jtConfig.timeoutWarningThreshold == 25;
        
        std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
        return success;
    }

    bool testMonitoringConfigRetrieval() {
        std::cout << "\n=== Testing Full Monitoring Configuration Retrieval ===\n";
        
        auto monitoringConfig = configManager->getMonitoringConfig();
        
        std::cout << "Full Monitoring Config Retrieved Successfully\n";
        std::cout << "  WebSocket Port: " << monitoringConfig.websocket.port << "\n";
        std::cout << "  Job Tracking Interval: " << monitoringConfig.jobTracking.progressUpdateInterval << "s\n";
        
        bool success = monitoringConfig.websocket.port == 8081 &&
                      monitoringConfig.jobTracking.progressUpdateInterval == 5;
        
        std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
        return success;
    }

    bool testConfigurationValidation() {
        std::cout << "\n=== Testing Configuration Validation ===\n";
        
        auto result = configManager->validateMonitoringConfig();
        
        std::cout << "Validation Result:\n";
        std::cout << "  Valid: " << (result.isValid ? "true" : "false") << "\n";
        std::cout << "  Errors: " << result.errors.size() << "\n";
        std::cout << "  Warnings: " << result.warnings.size() << "\n";
        
        if (!result.errors.empty()) {
            std::cout << "  Error Details:\n";
            for (const auto& error : result.errors) {
                std::cout << "    - " << error << "\n";
            }
        }
        
        if (!result.warnings.empty()) {
            std::cout << "  Warning Details:\n";
            for (const auto& warning : result.warnings) {
                std::cout << "    - " << warning << "\n";
            }
        }
        
        std::cout << "Result: " << (result.isValid ? "PASS" : "FAIL") << "\n";
        return result.isValid;
    }

    bool testInvalidConfigurationValidation() {
        std::cout << "\n=== Testing Invalid Configuration Validation ===\n";
        
        // Test WebSocket config with invalid values
        WebSocketConfig invalidWsConfig;
        invalidWsConfig.port = -1;
        invalidWsConfig.maxConnections = 0;
        invalidWsConfig.heartbeatInterval = -5;
        
        auto wsResult = invalidWsConfig.validate();
        std::cout << "Invalid WebSocket Config Validation:\n";
        std::cout << "  Valid: " << (wsResult.isValid ? "true" : "false") << "\n";
        std::cout << "  Errors: " << wsResult.errors.size() << "\n";
        
        // Test Job Tracking config with invalid values
        JobTrackingConfig invalidJtConfig;
        invalidJtConfig.progressUpdateInterval = 0;
        invalidJtConfig.timeoutWarningThreshold = -10;
        
        auto jtResult = invalidJtConfig.validate();
        std::cout << "Invalid Job Tracking Config Validation:\n";
        std::cout << "  Valid: " << (jtResult.isValid ? "true" : "false") << "\n";
        std::cout << "  Errors: " << jtResult.errors.size() << "\n";
        
        bool success = !wsResult.isValid && !jtResult.isValid &&
                      wsResult.errors.size() >= 3 && jtResult.errors.size() >= 2;
        
        std::cout << "Result: " << (success ? "PASS (correctly detected invalid configs)" : "FAIL") << "\n";
        return success;
    }

    bool testDynamicConfigurationUpdate() {
        std::cout << "\n=== Testing Dynamic Configuration Updates ===\n";
        
        // Get original config
        auto originalConfig = configManager->getWebSocketConfig();
        std::cout << "Original WebSocket Port: " << originalConfig.port << "\n";
        
        // Update WebSocket config
        WebSocketConfig newWsConfig = originalConfig;
        newWsConfig.port = 9090;
        newWsConfig.maxConnections = 200;
        
        bool updateResult = configManager->updateWebSocketConfig(newWsConfig);
        std::cout << "Update Result: " << (updateResult ? "SUCCESS" : "FAILED") << "\n";
        
        if (updateResult) {
            auto updatedConfig = configManager->getWebSocketConfig();
            std::cout << "Updated WebSocket Port: " << updatedConfig.port << "\n";
            std::cout << "Updated Max Connections: " << updatedConfig.maxConnections << "\n";
            
            bool success = updatedConfig.port == 9090 && updatedConfig.maxConnections == 200;
            std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
            return success;
        }
        
        std::cout << "Result: FAIL (update failed)\n";
        return false;
    }

    bool testInvalidDynamicConfigurationUpdate() {
        std::cout << "\n=== Testing Invalid Dynamic Configuration Updates ===\n";
        
        // Try to update with invalid config
        WebSocketConfig invalidConfig;
        invalidConfig.port = -1;  // Invalid port
        
        bool updateResult = configManager->updateWebSocketConfig(invalidConfig);
        std::cout << "Invalid Update Result: " << (updateResult ? "ACCEPTED (BAD)" : "REJECTED (GOOD)") << "\n";
        
        // Config should remain unchanged
        auto currentConfig = configManager->getWebSocketConfig();
        bool success = !updateResult && currentConfig.port != -1;
        
        std::cout << "Current Port (should be unchanged): " << currentConfig.port << "\n";
        std::cout << "Result: " << (success ? "PASS (correctly rejected invalid config)" : "FAIL") << "\n";
        return success;
    }

    bool testConfigurationChangeCallback() {
        std::cout << "\n=== Testing Configuration Change Callbacks ===\n";
        
        bool callbackInvoked = false;
        std::string receivedSection;
        
        // Register callback
        ConfigChangeCallback callback = [&](const std::string& section, const MonitoringConfig& config) {
            callbackInvoked = true;
            receivedSection = section;
            std::cout << "Callback invoked for section: " << section << "\n";
        };
        
        configManager->registerConfigChangeCallback("test_callback", callback);
        
        // Update config to trigger callback
        JobTrackingConfig newJtConfig;
        newJtConfig.progressUpdateInterval = 15;
        newJtConfig.logStreamingEnabled = false;
        newJtConfig.metricsCollectionEnabled = true;
        newJtConfig.timeoutWarningThreshold = 30;
        
        configManager->updateJobTrackingConfig(newJtConfig);
        
        // Cleanup
        configManager->unregisterConfigChangeCallback("test_callback");
        
        bool success = callbackInvoked && receivedSection == "job_tracking";
        std::cout << "Callback Invoked: " << (callbackInvoked ? "true" : "false") << "\n";
        std::cout << "Received Section: " << receivedSection << "\n";
        std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
        return success;
    }

    bool testTemplateValidation() {
        std::cout << "\n=== Testing Template-based Validated Value Retrieval ===\n";
        
        // Test validated int retrieval
        auto port = configManager->getValidatedValue<int>("monitoring.websocket.port", 8080, 
            [](int p) { return p > 0 && p <= 65535; });
        std::cout << "Validated port retrieval: " << port << "\n";
        
        // Test validated bool retrieval
        auto enabled = configManager->getValidatedValue<bool>("monitoring.websocket.enabled", false);
        std::cout << "Validated enabled retrieval: " << (enabled ? "true" : "false") << "\n";
        
        // Test validated string retrieval
        auto address = configManager->getValidatedValue<std::string>("server.address", "localhost");
        std::cout << "Validated address retrieval: " << address << "\n";
        
        bool success = port > 0 && enabled == true && !address.empty();
        std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
        return success;
    }

    bool runAllTests() {
        std::cout << "==========================================\n";
        std::cout << "Configuration Management Demo & Testing\n";
        std::cout << "==========================================\n";
        
        bool allPassed = true;
        
        allPassed &= testWebSocketConfigRetrieval();
        allPassed &= testJobTrackingConfigRetrieval();
        allPassed &= testMonitoringConfigRetrieval();
        allPassed &= testConfigurationValidation();
        allPassed &= testInvalidConfigurationValidation();
        allPassed &= testDynamicConfigurationUpdate();
        allPassed &= testInvalidDynamicConfigurationUpdate();
        allPassed &= testConfigurationChangeCallback();
        allPassed &= testTemplateValidation();
        
        std::cout << "\n==========================================\n";
        std::cout << "Overall Result: " << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
        std::cout << "==========================================\n";
        
        return allPassed;
    }
};

int main() {
    ConfigMonitoringDemo demo;
    
    if (!demo.setUp()) {
        std::cerr << "Failed to set up test environment" << std::endl;
        return 1;
    }
    
    bool success = demo.runAllTests();
    
    demo.tearDown();
    
    return success ? 0 : 1;
}
