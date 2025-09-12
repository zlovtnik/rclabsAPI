#include "../include/config_manager.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

class ConfigManagerMonitoringTest : public ::testing::Test {
protected:
  /**
   * @brief Test fixture setup: prepares a temporary config environment and
   * loads it.
   *
   * Creates a temporary directory and writes a test configuration file
   * (test_config.json), then obtains the ConfigManager singleton and loads the
   * written config file so tests start with a known configuration state.
   *
   * Side effects:
   * - Creates the directory referenced by `testDir`.
   * - Writes `testConfigFile` into that directory.
   * - Initializes `configManager` by loading the configuration into the
   * ConfigManager singleton.
   */
  void SetUp() override {
    // Create a temporary directory for test files
    testDir = std::filesystem::temp_directory_path() / "etlplus_config_test";
    std::filesystem::create_directories(testDir);

    // Create test config file
    testConfigFile = testDir / "test_config.json";
    createTestConfigFile();

    // Load config
    configManager = &ConfigManager::getInstance();
    configManager->loadConfig(testConfigFile.string());
  }

  void TearDown() override {
    // Clean up test files
    std::filesystem::remove_all(testDir);
  }

  /**
   * @brief Creates a standard test configuration file at testConfigFile.
   *
   * Writes a JSON configuration used by the test suite containing server,
   * database, and monitoring sections (websocket, job_tracking, notifications).
   * Existing file at testConfigFile will be overwritten.
   */
  void createTestConfigFile() {
    std::ofstream file(testConfigFile);
    file << R"({
  "server": {
    "address": "0.0.0.0",
    "port": 8080,
    "threads": 4
  },
  "database": {
    "host": "localhost",
    "port": 1521,
    "name": "FREE"
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

  /**
   * @brief Writes a deliberately invalid monitoring JSON configuration to the
   * testConfigFile.
   *
   * The file contains invalid numeric values for websocket and job_tracking
   * sections (e.g., negative port, zero/negative intervals and sizes) intended
   * to trigger validation errors in tests that load and validate configuration
   * data.
   */
  void createInvalidConfigFile() {
    std::ofstream file(testConfigFile);
    file << R"({
  "monitoring": {
    "websocket": {
      "enabled": true,
      "port": -1,
      "max_connections": 0,
      "heartbeat_interval": -5,
      "message_queue_size": 0
    },
    "job_tracking": {
      "progress_update_interval": 0,
      "log_streaming_enabled": true,
      "metrics_collection_enabled": true,
      "timeout_warning_threshold": -10
    }
  }
})";
    file.close();
  }

  std::filesystem::path testDir;
  std::filesystem::path testConfigFile;
  ConfigManager *configManager;
};

// ===== WebSocketConfig Tests =====

TEST_F(ConfigManagerMonitoringTest,
       WebSocketConfig_FromConfig_ValidConfiguration) {
  auto wsConfig = configManager->getWebSocketConfig();

  EXPECT_TRUE(wsConfig.enabled);
  EXPECT_EQ(wsConfig.port, 8081);
  EXPECT_EQ(wsConfig.maxConnections, 100);
  EXPECT_EQ(wsConfig.heartbeatInterval, 30);
  EXPECT_EQ(wsConfig.messageQueueSize, 1000);
}

TEST_F(ConfigManagerMonitoringTest, WebSocketConfig_FromConfig_DefaultValues) {
  // Create config with minimal settings
  std::ofstream file(testConfigFile);
  file << R"({"monitoring": {"websocket": {}}})";
  file.close();

  configManager->loadConfig(testConfigFile.string());
  auto wsConfig = configManager->getWebSocketConfig();

  EXPECT_TRUE(wsConfig.enabled);              // default
  EXPECT_EQ(wsConfig.port, 8081);             // default
  EXPECT_EQ(wsConfig.maxConnections, 100);    // default
  EXPECT_EQ(wsConfig.heartbeatInterval, 30);  // default
  EXPECT_EQ(wsConfig.messageQueueSize, 1000); // default
}

TEST_F(ConfigManagerMonitoringTest,
       WebSocketConfig_Validate_ValidConfiguration) {
  auto wsConfig = configManager->getWebSocketConfig();
  auto result = wsConfig.validate();

  EXPECT_TRUE(result.isValid);
  EXPECT_TRUE(result.errors.empty());
}

TEST_F(ConfigManagerMonitoringTest, WebSocketConfig_Validate_InvalidPort) {
  WebSocketConfig config;
  config.port = -1;

  auto result = config.validate();

  EXPECT_FALSE(result.isValid);
  EXPECT_GE(result.errors.size(), 1);
  EXPECT_THAT(result.errors[0],
              testing::HasSubstr("port must be between 1 and 65535"));
}

TEST_F(ConfigManagerMonitoringTest,
       WebSocketConfig_Validate_PrivilegedPortWarning) {
  WebSocketConfig config;
  config.port = 80;

  auto result = config.validate();

  EXPECT_TRUE(result.isValid);
  EXPECT_GE(result.warnings.size(), 1);
  EXPECT_THAT(result.warnings[0], testing::HasSubstr("privileged range"));
}

TEST_F(ConfigManagerMonitoringTest,
       WebSocketConfig_Validate_InvalidMaxConnections) {
  WebSocketConfig config;
  config.maxConnections = 0;

  auto result = config.validate();

  EXPECT_FALSE(result.isValid);
  EXPECT_GE(result.errors.size(), 1);
  EXPECT_THAT(result.errors[0],
              testing::HasSubstr("max_connections must be positive"));
}

TEST_F(ConfigManagerMonitoringTest,
       WebSocketConfig_Validate_HighMaxConnectionsWarning) {
  WebSocketConfig config;
  config.maxConnections = 20000;

  auto result = config.validate();

  EXPECT_TRUE(result.isValid);
  EXPECT_GE(result.warnings.size(), 1);
  EXPECT_THAT(result.warnings[0], testing::HasSubstr("very high"));
}

TEST_F(ConfigManagerMonitoringTest, WebSocketConfig_EqualityOperator) {
  WebSocketConfig config1;
  WebSocketConfig config2;

  EXPECT_TRUE(config1 == config2);

  config2.port = 9090;
  EXPECT_FALSE(config1 == config2);
}

// ===== JobTrackingConfig Tests =====

TEST_F(ConfigManagerMonitoringTest,
       JobTrackingConfig_FromConfig_ValidConfiguration) {
  auto jtConfig = configManager->getJobTrackingConfig();

  EXPECT_EQ(jtConfig.progressUpdateInterval, 5);
  EXPECT_TRUE(jtConfig.logStreamingEnabled);
  EXPECT_TRUE(jtConfig.metricsCollectionEnabled);
  EXPECT_EQ(jtConfig.timeoutWarningThreshold, 25);
}

TEST_F(ConfigManagerMonitoringTest,
       JobTrackingConfig_FromConfig_DefaultValues) {
  // Create config with minimal settings
  std::ofstream file(testConfigFile);
  file << R"({"monitoring": {"job_tracking": {}}})";
  file.close();

  configManager->loadConfig(testConfigFile.string());
  auto jtConfig = configManager->getJobTrackingConfig();

  EXPECT_EQ(jtConfig.progressUpdateInterval, 5);   // default
  EXPECT_TRUE(jtConfig.logStreamingEnabled);       // default
  EXPECT_TRUE(jtConfig.metricsCollectionEnabled);  // default
  EXPECT_EQ(jtConfig.timeoutWarningThreshold, 25); // default
}

TEST_F(ConfigManagerMonitoringTest,
       JobTrackingConfig_Validate_ValidConfiguration) {
  auto jtConfig = configManager->getJobTrackingConfig();
  auto result = jtConfig.validate();

  EXPECT_TRUE(result.isValid);
  EXPECT_TRUE(result.errors.empty());
}

TEST_F(ConfigManagerMonitoringTest,
       JobTrackingConfig_Validate_InvalidProgressUpdateInterval) {
  JobTrackingConfig config;
  config.progressUpdateInterval = 0;

  auto result = config.validate();

  EXPECT_FALSE(result.isValid);
  EXPECT_GE(result.errors.size(), 1);
  EXPECT_THAT(result.errors[0],
              testing::HasSubstr("progress_update_interval must be positive"));
}

TEST_F(ConfigManagerMonitoringTest,
       JobTrackingConfig_Validate_HighProgressUpdateIntervalWarning) {
  JobTrackingConfig config;
  config.progressUpdateInterval = 500;

  auto result = config.validate();

  EXPECT_TRUE(result.isValid);
  EXPECT_GE(result.warnings.size(), 1);
  EXPECT_THAT(result.warnings[0], testing::HasSubstr("very high"));
}

TEST_F(ConfigManagerMonitoringTest,
       JobTrackingConfig_Validate_InvalidTimeoutThreshold) {
  JobTrackingConfig config;
  config.timeoutWarningThreshold = -1;

  auto result = config.validate();

  EXPECT_FALSE(result.isValid);
  EXPECT_GE(result.errors.size(), 1);
  EXPECT_THAT(result.errors[0],
              testing::HasSubstr("timeout_warning_threshold must be positive"));
}

TEST_F(ConfigManagerMonitoringTest, JobTrackingConfig_EqualityOperator) {
  JobTrackingConfig config1;
  JobTrackingConfig config2;

  EXPECT_TRUE(config1 == config2);

  config2.progressUpdateInterval = 10;
  EXPECT_FALSE(config1 == config2);
}

// ===== MonitoringConfig Tests =====

TEST_F(ConfigManagerMonitoringTest,
       MonitoringConfig_FromConfig_ValidConfiguration) {
  auto monitoringConfig = configManager->getMonitoringConfig();

  EXPECT_TRUE(monitoringConfig.websocket.enabled);
  EXPECT_EQ(monitoringConfig.websocket.port, 8081);
  EXPECT_EQ(monitoringConfig.jobTracking.progressUpdateInterval, 5);
  EXPECT_TRUE(monitoringConfig.jobTracking.logStreamingEnabled);
}

TEST_F(ConfigManagerMonitoringTest,
       MonitoringConfig_Validate_ValidConfiguration) {
  auto monitoringConfig = configManager->getMonitoringConfig();
  auto result = monitoringConfig.validate();

  EXPECT_TRUE(result.isValid);
  EXPECT_TRUE(result.errors.empty());
}

TEST_F(ConfigManagerMonitoringTest,
       MonitoringConfig_Validate_InvalidWebSocketConfiguration) {
  createInvalidConfigFile();
  configManager->loadConfig(testConfigFile.string());

  auto monitoringConfig = configManager->getMonitoringConfig();
  auto result = monitoringConfig.validate();

  EXPECT_FALSE(result.isValid);
  EXPECT_GT(result.errors.size(), 0);

  // Check that errors are prefixed with component names
  bool hasWebSocketError = false;
  bool hasJobTrackingError = false;
  for (const auto &error : result.errors) {
    if (error.find("WebSocket:") != std::string::npos) {
      hasWebSocketError = true;
    }
    if (error.find("Job Tracking:") != std::string::npos) {
      hasJobTrackingError = true;
    }
  }

  EXPECT_TRUE(hasWebSocketError);
  EXPECT_TRUE(hasJobTrackingError);
}

TEST_F(ConfigManagerMonitoringTest,
       MonitoringConfig_Validate_CrossValidationWarning) {
  MonitoringConfig config;
  config.websocket.heartbeatInterval = 10;
  config.jobTracking.progressUpdateInterval = 20;

  auto result = config.validate();

  EXPECT_TRUE(result.isValid);
  EXPECT_GE(result.warnings.size(), 1);
  EXPECT_THAT(result.warnings[0],
              testing::HasSubstr("progress update interval"));
}

TEST_F(ConfigManagerMonitoringTest, MonitoringConfig_EqualityOperator) {
  MonitoringConfig config1;
  MonitoringConfig config2;

  EXPECT_TRUE(config1 == config2);

  config2.websocket.port = 9090;
  EXPECT_FALSE(config1 == config2);
}

// ===== ConfigManager Dynamic Updates Tests =====

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_UpdateWebSocketConfig_ValidUpdate) {
  WebSocketConfig newConfig;
  newConfig.port = 9090;
  newConfig.maxConnections = 200;

  bool result = configManager->updateWebSocketConfig(newConfig);

  EXPECT_TRUE(result);

  auto updatedConfig = configManager->getWebSocketConfig();
  EXPECT_EQ(updatedConfig.port, 9090);
  EXPECT_EQ(updatedConfig.maxConnections, 200);
}

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_UpdateWebSocketConfig_InvalidUpdate) {
  WebSocketConfig newConfig;
  newConfig.port = -1; // Invalid port

  bool result = configManager->updateWebSocketConfig(newConfig);

  EXPECT_FALSE(result);

  // Original config should remain unchanged
  auto originalConfig = configManager->getWebSocketConfig();
  EXPECT_EQ(originalConfig.port, 8081);
}

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_UpdateJobTrackingConfig_ValidUpdate) {
  JobTrackingConfig newConfig;
  newConfig.progressUpdateInterval = 10;
  newConfig.logStreamingEnabled = false;

  bool result = configManager->updateJobTrackingConfig(newConfig);

  EXPECT_TRUE(result);

  auto updatedConfig = configManager->getJobTrackingConfig();
  EXPECT_EQ(updatedConfig.progressUpdateInterval, 10);
  EXPECT_FALSE(updatedConfig.logStreamingEnabled);
}

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_UpdateJobTrackingConfig_InvalidUpdate) {
  JobTrackingConfig newConfig;
  newConfig.progressUpdateInterval = 0; // Invalid interval

  bool result = configManager->updateJobTrackingConfig(newConfig);

  EXPECT_FALSE(result);

  // Original config should remain unchanged
  auto originalConfig = configManager->getJobTrackingConfig();
  EXPECT_EQ(originalConfig.progressUpdateInterval, 5);
}

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_UpdateMonitoringConfig_ValidUpdate) {
  MonitoringConfig newConfig = configManager->getMonitoringConfig();
  newConfig.websocket.port = 9090;
  newConfig.jobTracking.progressUpdateInterval = 10;

  bool result = configManager->updateMonitoringConfig(newConfig);

  EXPECT_TRUE(result);

  auto updatedConfig = configManager->getMonitoringConfig();
  EXPECT_EQ(updatedConfig.websocket.port, 9090);
  EXPECT_EQ(updatedConfig.jobTracking.progressUpdateInterval, 10);
}

TEST_F(ConfigManagerMonitoringTest, ConfigManager_ValidateMonitoringConfig) {
  auto result = configManager->validateMonitoringConfig();

  EXPECT_TRUE(result.isValid);
  EXPECT_TRUE(result.errors.empty());
}

TEST_F(ConfigManagerMonitoringTest, ConfigManager_ValidateConfiguration) {
  auto result = configManager->validateConfiguration();

  EXPECT_TRUE(result.isValid);
  EXPECT_TRUE(result.errors.empty());
}

// ===== Configuration Change Callbacks Tests =====

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_ConfigChangeCallback_Registration) {
  bool callbackInvoked = false;
  std::string receivedSection;
  MonitoringConfig receivedConfig;

  ConfigChangeCallback callback = [&](const std::string &section,
                                      const MonitoringConfig &config) {
    callbackInvoked = true;
    receivedSection = section;
    receivedConfig = config;
  };

  configManager->registerConfigChangeCallback("test_section", callback);

  // Trigger a config change
  WebSocketConfig newConfig;
  newConfig.port = 9090;
  configManager->updateWebSocketConfig(newConfig);

  EXPECT_TRUE(callbackInvoked);
  EXPECT_EQ(receivedSection, "websocket");
  EXPECT_EQ(receivedConfig.websocket.port, 9090);

  // Cleanup
  configManager->unregisterConfigChangeCallback("test_section");
}

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_ConfigChangeCallback_Unregistration) {
  bool callbackInvoked = false;

  ConfigChangeCallback callback = [&](const std::string &section,
                                      const MonitoringConfig &config) {
    callbackInvoked = true;
  };

  configManager->registerConfigChangeCallback("test_section", callback);
  configManager->unregisterConfigChangeCallback("test_section");

  // Trigger a config change
  WebSocketConfig newConfig;
  newConfig.port = 9090;
  configManager->updateWebSocketConfig(newConfig);

  EXPECT_FALSE(callbackInvoked);
}

// ===== Configuration Reload Tests =====

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_ReloadConfiguration_ValidFile) {
  // Modify the config file
  std::ofstream file(testConfigFile);
  file << R"({
  "monitoring": {
    "websocket": {
      "enabled": false,
      "port": 9999,
      "max_connections": 50
    },
    "job_tracking": {
      "progress_update_interval": 15,
      "log_streaming_enabled": false
    }
  }
})";
  file.close();

  bool result = configManager->reloadConfiguration();

  EXPECT_TRUE(result);

  auto config = configManager->getMonitoringConfig();
  EXPECT_FALSE(config.websocket.enabled);
  EXPECT_EQ(config.websocket.port, 9999);
  EXPECT_EQ(config.websocket.maxConnections, 50);
  EXPECT_EQ(config.jobTracking.progressUpdateInterval, 15);
  EXPECT_FALSE(config.jobTracking.logStreamingEnabled);
}

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_ReloadConfiguration_InvalidFile) {
  // Delete the config file to cause a reload failure
  std::filesystem::remove(testConfigFile);

  bool result = configManager->reloadConfiguration();

  EXPECT_FALSE(result);
}

// ===== Configuration Validation with Templates Tests =====

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_GetValidatedValue_IntWithValidator) {
  auto value = configManager->getValidatedValue<int>(
      "monitoring.websocket.port", 8080,
      [](int port) { return port > 0 && port <= 65535; });

  EXPECT_EQ(value, 8081); // From config file
}

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_GetValidatedValue_IntWithValidatorFailure) {
  auto value = configManager->getValidatedValue<int>(
      "monitoring.websocket.port", 8080,
      [](int port) { return port > 10000; }); // This should fail

  EXPECT_EQ(value, 8080); // Should return default value
}

TEST_F(ConfigManagerMonitoringTest,
       ConfigManager_GetValidatedValue_StringWithValidator) {
  auto value = configManager->getValidatedValue<std::string>(
      "monitoring.websocket.enabled", "false",
      [](const std::string &val) { return val == "true" || val == "false"; });

  EXPECT_EQ(value, "true"); // From config file
}

/**
 * @brief Entry point for the Google Test runner.
 *
 * Initializes Google Test with command-line arguments and runs all registered
 * tests.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return int Test runner exit code (0 on success, non-zero on failure).
 */
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
