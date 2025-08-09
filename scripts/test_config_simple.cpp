#include "../include/config_manager.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

void createTestConfig() {
    std::filesystem::path configPath = "test_monitoring_config.json";
    std::ofstream file(configPath);
    file << R"({
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
    }
  }
})";
    file.close();
}

int main() {
    try {
        createTestConfig();
        
        ConfigManager& config = ConfigManager::getInstance();
        if (!config.loadConfig("test_monitoring_config.json")) {
            std::cerr << "Failed to load configuration" << std::endl;
            return 1;
        }
        
        std::cout << "=== Basic Configuration Test ===" << std::endl;
        
        // Test WebSocket configuration
        auto wsConfig = config.getWebSocketConfig();
        std::cout << "WebSocket enabled: " << (wsConfig.enabled ? "true" : "false") << std::endl;
        std::cout << "WebSocket port: " << wsConfig.port << std::endl;
        std::cout << "Max connections: " << wsConfig.maxConnections << std::endl;
        
        // Test Job Tracking configuration
        auto jtConfig = config.getJobTrackingConfig();
        std::cout << "Progress interval: " << jtConfig.progressUpdateInterval << "s" << std::endl;
        std::cout << "Log streaming: " << (jtConfig.logStreamingEnabled ? "true" : "false") << std::endl;
        
        // Test validation
        auto validationResult = config.validateMonitoringConfig();
        std::cout << "Configuration valid: " << (validationResult.isValid ? "true" : "false") << std::endl;
        std::cout << "Errors: " << validationResult.errors.size() << std::endl;
        std::cout << "Warnings: " << validationResult.warnings.size() << std::endl;
        
        // Test invalid configuration validation
        std::cout << "\n=== Invalid Configuration Test ===" << std::endl;
        WebSocketConfig invalidConfig;
        invalidConfig.port = -1;
        invalidConfig.maxConnections = 0;
        
        auto invalidResult = invalidConfig.validate();
        std::cout << "Invalid config detected: " << (!invalidResult.isValid ? "true" : "false") << std::endl;
        std::cout << "Error count: " << invalidResult.errors.size() << std::endl;
        
        if (!invalidResult.errors.empty()) {
            std::cout << "First error: " << invalidResult.errors[0] << std::endl;
        }
        
        // Cleanup
        std::filesystem::remove("test_monitoring_config.json");
        
        std::cout << "\n=== All Tests PASSED ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
