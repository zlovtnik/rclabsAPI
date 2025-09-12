#include "../include/config_manager.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

class ConfigMonitoringDemo {
private:
  std::filesystem::path testDir;
  std::filesystem::path testConfigFile;
  ConfigManager *configManager;

  /**
   * @brief Create a JSON test configuration file at the path stored in testConfigFile.
   *
   * Writes a predefined JSON configuration (server and monitoring sections including
   * websocket, job_tracking, and notifications) to the member path testConfigFile.
   *
   * @note This will overwrite any existing file at testConfigFile.
   */
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
  /**
   * @brief Prepare the temporary environment and load the test configuration.
   *
   * Creates a temporary directory (temp_directory_path()/etlplus_config_demo), writes the test
   * JSON configuration file (demo_config.json) via createTestConfigFile(), obtains the
   * ConfigManager singleton, and loads the configuration from that file.
   *
   * Side effects:
   * - Sets the member variables `testDir` and `testConfigFile`.
   * - Creates filesystem directories and writes the test config file.
   * - Initializes `configManager` to point at ConfigManager::getInstance().
   *
   * Any exceptions thrown during setup are caught; an error is printed and the function
   * returns false in that case.
   *
   * @return true if the configuration was successfully loaded; false on failure or exception.
   */
  bool setUp() {
    try {
      testDir = std::filesystem::temp_directory_path() / "etlplus_config_demo";
      std::filesystem::create_directories(testDir);

      testConfigFile = testDir / "demo_config.json";
      createTestConfigFile();

      configManager = &ConfigManager::getInstance();
      return configManager->loadConfig(testConfigFile.string());
    } catch (const std::exception &e) {
      std::cerr << "Setup failed: " << e.what() << std::endl;
      return false;
    }
  }

  /**
   * @brief Clean up the temporary test directory used by the demo.
   *
   * Attempts to remove the directory referenced by `testDir` and all of its contents.
   * Any exceptions thrown during removal are caught; on failure an error message is
   * written to standard error but the exception is not propagated.
   */
  void tearDown() {
    try {
      std::filesystem::remove_all(testDir);
    } catch (const std::exception &e) {
      std::cerr << "Cleanup failed: " << e.what() << std::endl;
    }
  }

  /**
   * @brief Retrieve the WebSocket configuration and verify it matches expected test values.
   *
   * Retrieves the current WebSocket configuration from the global ConfigManager and validates
   * that the fields match the test fixture's expected values.
   *
   * Expected values checked:
   * - enabled == true
   * - port == 8081
   * - maxConnections == 100
   * - heartbeatInterval == 30
   * - messageQueueSize == 1000
   *
   * @return true if all fields equal the expected values; false otherwise.
   */
  bool testWebSocketConfigRetrieval() {
    std::cout << "\n=== Testing WebSocket Configuration Retrieval ===\n";

    auto wsConfig = configManager->getWebSocketConfig();

    std::cout << "WebSocket Config:\n";
    std::cout << "  Enabled: " << (wsConfig.enabled ? "true" : "false") << "\n";
    std::cout << "  Port: " << wsConfig.port << "\n";
    std::cout << "  Max Connections: " << wsConfig.maxConnections << "\n";
    std::cout << "  Heartbeat Interval: " << wsConfig.heartbeatInterval
              << "s\n";
    std::cout << "  Message Queue Size: " << wsConfig.messageQueueSize << "\n";

    // Validate values
    bool success = wsConfig.enabled == true && wsConfig.port == 8081 &&
                   wsConfig.maxConnections == 100 &&
                   wsConfig.heartbeatInterval == 30 &&
                   wsConfig.messageQueueSize == 1000;

    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
    return success;
  }

  /**
   * @brief Retrieves the Job Tracking configuration from the ConfigManager, validates it against expected values, and reports the result.
   *
   * Retrieves the current job tracking configuration, prints key fields (progress update interval, log streaming enabled,
   * metrics collection enabled, and timeout warning threshold), and checks that they match the expected values:
   * progressUpdateInterval == 5, logStreamingEnabled == true, metricsCollectionEnabled == true, timeoutWarningThreshold == 25.
   *
   * Side effects:
   * - Prints diagnostic information and a PASS/FAIL summary to standard output.
   *
   * @return true if the retrieved configuration matches the expected values; false otherwise.
   */
  bool testJobTrackingConfigRetrieval() {
    std::cout << "\n=== Testing Job Tracking Configuration Retrieval ===\n";

    auto jtConfig = configManager->getJobTrackingConfig();

    std::cout << "Job Tracking Config:\n";
    std::cout << "  Progress Update Interval: "
              << jtConfig.progressUpdateInterval << "s\n";
    std::cout << "  Log Streaming Enabled: "
              << (jtConfig.logStreamingEnabled ? "true" : "false") << "\n";
    std::cout << "  Metrics Collection Enabled: "
              << (jtConfig.metricsCollectionEnabled ? "true" : "false") << "\n";
    std::cout << "  Timeout Warning Threshold: "
              << jtConfig.timeoutWarningThreshold << " minutes\n";

    // Validate values
    bool success = jtConfig.progressUpdateInterval == 5 &&
                   jtConfig.logStreamingEnabled == true &&
                   jtConfig.metricsCollectionEnabled == true &&
                   jtConfig.timeoutWarningThreshold == 25;

    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
    return success;
  }

  /**
   * @brief Retrieve the full monitoring configuration and validate key fields.
   *
   * Retrieves the monitoring configuration from the global ConfigManager, prints
   * selected fields (WebSocket port and Job Tracking progress update interval),
   * and verifies they match expected test values.
   *
   * The test expects:
   * - websocket.port == 8081
   * - jobTracking.progressUpdateInterval == 5
   *
   * @return true if the retrieved configuration matches the expected values; false otherwise.
   */
  bool testMonitoringConfigRetrieval() {
    std::cout << "\n=== Testing Full Monitoring Configuration Retrieval ===\n";

    auto monitoringConfig = configManager->getMonitoringConfig();

    std::cout << "Full Monitoring Config Retrieved Successfully\n";
    std::cout << "  WebSocket Port: " << monitoringConfig.websocket.port
              << "\n";
    std::cout << "  Job Tracking Interval: "
              << monitoringConfig.jobTracking.progressUpdateInterval << "s\n";

    bool success = monitoringConfig.websocket.port == 8081 &&
                   monitoringConfig.jobTracking.progressUpdateInterval == 5;

    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
    return success;
  }

  /**
   * @brief Runs validation for the monitoring configuration and reports results.
   *
   * Calls the ConfigManager's validateMonitoringConfig(), prints a summary of
   * the validation (validity, counts of errors and warnings, and their messages),
   * and returns whether the configuration is valid.
   *
   * Side effects:
   *  - Writes validation output to stdout.
   *
   * @return true if the monitoring configuration is valid; otherwise false.
   */
  bool testConfigurationValidation() {
    std::cout << "\n=== Testing Configuration Validation ===\n";

    auto result = configManager->validateMonitoringConfig();

    std::cout << "Validation Result:\n";
    std::cout << "  Valid: " << (result.isValid ? "true" : "false") << "\n";
    std::cout << "  Errors: " << result.errors.size() << "\n";
    std::cout << "  Warnings: " << result.warnings.size() << "\n";

    if (!result.errors.empty()) {
      std::cout << "  Error Details:\n";
      for (const auto &error : result.errors) {
        std::cout << "    - " << error << "\n";
      }
    }

    if (!result.warnings.empty()) {
      std::cout << "  Warning Details:\n";
      for (const auto &warning : result.warnings) {
        std::cout << "    - " << warning << "\n";
      }
    }

    std::cout << "Result: " << (result.isValid ? "PASS" : "FAIL") << "\n";
    return result.isValid;
  }

  /**
   * @brief Verify that invalid monitoring configurations are detected.
   *
   * Constructs invalid WebSocketConfig and JobTrackingConfig instances, invokes
   * their validate() methods, and checks that each validation result is marked
   * invalid and contains a sufficient number of errors (at least 3 errors for
   * the WebSocket config and at least 2 errors for the Job Tracking config).
   *
   * @return true if both validations are invalid and meet the minimum error
   * counts; false otherwise.
   */
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

    std::cout << "Result: "
              << (success ? "PASS (correctly detected invalid configs)"
                          : "FAIL")
              << "\n";
    return success;
  }

  /**
   * @brief Tests dynamic update of the WebSocket configuration via the ConfigManager.
   *
   * Performs an in-process update of the WebSocket configuration (changes port and
   * maxConnections), attempts to persist the change through the ConfigManager,
   * then reads back the configuration to verify the new values were applied.
   *
   * The function has a side effect on the ConfigManager state by calling
   * updateWebSocketConfig().
   *
   * @return true if the update was accepted and the persisted configuration
   *         reflects the new port and maxConnections; false if the update was
   *         rejected or the persisted configuration does not match the expected values.
   */
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
    std::cout << "Update Result: " << (updateResult ? "SUCCESS" : "FAILED")
              << "\n";

    if (updateResult) {
      auto updatedConfig = configManager->getWebSocketConfig();
      std::cout << "Updated WebSocket Port: " << updatedConfig.port << "\n";
      std::cout << "Updated Max Connections: " << updatedConfig.maxConnections
                << "\n";

      bool success =
          updatedConfig.port == 9090 && updatedConfig.maxConnections == 200;
      std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
      return success;
    }

    std::cout << "Result: FAIL (update failed)\n";
    return false;
  }

  /**
   * @brief Verifies that attempting to apply an invalid dynamic WebSocket configuration is rejected
   *        and does not mutate the active configuration.
   *
   * Attempts to update the singleton ConfigManager's WebSocket configuration with an obviously
   * invalid entry (port set to -1). The test checks that the update call returns false
   * (rejection) and that the currently active WebSocket configuration remains unchanged
   * (its port is not -1).
   *
   * @return true if the invalid update was rejected and the active configuration remained unchanged;
   *         false otherwise.
   */
  bool testInvalidDynamicConfigurationUpdate() {
    std::cout << "\n=== Testing Invalid Dynamic Configuration Updates ===\n";

    // Try to update with invalid config
    WebSocketConfig invalidConfig;
    invalidConfig.port = -1; // Invalid port

    bool updateResult = configManager->updateWebSocketConfig(invalidConfig);
    std::cout << "Invalid Update Result: "
              << (updateResult ? "ACCEPTED (BAD)" : "REJECTED (GOOD)") << "\n";

    // Config should remain unchanged
    auto currentConfig = configManager->getWebSocketConfig();
    bool success = !updateResult && currentConfig.port != -1;

    std::cout << "Current Port (should be unchanged): " << currentConfig.port
              << "\n";
    std::cout << "Result: "
              << (success ? "PASS (correctly rejected invalid config)" : "FAIL")
              << "\n";
    return success;
  }

  /**
   * @brief Tests that registered configuration-change callbacks are invoked when a config section is updated.
   *
   * Registers a temporary callback named "test_callback" with the ConfigManager, updates the Job Tracking
   * configuration to trigger the callback, then unregisters the callback. The test passes if the callback
   * was invoked and the reported section name equals "job_tracking".
   *
   * @return true if the callback was invoked and the received section equals "job_tracking"; false otherwise.
   */
  bool testConfigurationChangeCallback() {
    std::cout << "\n=== Testing Configuration Change Callbacks ===\n";

    bool callbackInvoked = false;
    std::string receivedSection;

    // Register callback
    ConfigChangeCallback callback = [&](const std::string &section,
                                        const MonitoringConfig &config) {
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
    std::cout << "Callback Invoked: " << (callbackInvoked ? "true" : "false")
              << "\n";
    std::cout << "Received Section: " << receivedSection << "\n";
    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
    return success;
  }

  /**
   * @brief Exercises template-based validated value retrieval from the ConfigManager.
   *
   * Retrieves three configuration values using the templated getValidatedValue:
   * - an integer port at "monitoring.websocket.port" (uses 8080 default and a
   *   predicate that accepts 1..65535),
   * - a boolean enabled flag at "monitoring.websocket.enabled" (default false),
   * - a string address at "server.address" (default "localhost").
   *
   * Each retrieved value is printed to stdout. The function considers the test
   * successful if the resolved port is > 0, the enabled flag is true, and the
   * address is non-empty.
   *
   * @return true if all retrieved values pass the basic checks (port > 0,
   *         enabled == true, non-empty address); otherwise false.
   */
  bool testTemplateValidation() {
    std::cout << "\n=== Testing Template-based Validated Value Retrieval ===\n";

    // Test validated int retrieval
    auto port = configManager->getValidatedValue<int>(
        "monitoring.websocket.port", 8080,
        [](int p) { return p > 0 && p <= 65535; });
    std::cout << "Validated port retrieval: " << port << "\n";

    // Test validated bool retrieval
    auto enabled = configManager->getValidatedValue<bool>(
        "monitoring.websocket.enabled", false);
    std::cout << "Validated enabled retrieval: " << (enabled ? "true" : "false")
              << "\n";

    // Test validated string retrieval
    auto address = configManager->getValidatedValue<std::string>(
        "server.address", "localhost");
    std::cout << "Validated address retrieval: " << address << "\n";

    bool success = port > 0 && enabled == true && !address.empty();
    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
    return success;
  }

  /**
   * @brief Runs the full suite of configuration-related tests and reports the aggregate result.
   *
   * Executes each individual test case in a fixed sequence:
   * - WebSocket configuration retrieval
   * - Job tracking configuration retrieval
   * - Full monitoring configuration retrieval
   * - Configuration validation
   * - Invalid-configuration detection
   * - Dynamic configuration updates (valid and invalid)
   * - Configuration change callback handling
   * - Template-based validated value retrieval
   *
   * The function prints a header, per-suite summary lines and a final overall result to stdout.
   *
   * @return true if all tests passed; false if any test failed.
   */
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
    std::cout << "Overall Result: "
              << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    std::cout << "==========================================\n";

    return allPassed;
  }
};

/**
 * @brief Test harness entry point for the ConfigMonitoringDemo suite.
 *
 * Initializes a ConfigMonitoringDemo instance, sets up the test environment,
 * runs the full set of configuration tests, performs cleanup, and returns an
 * appropriate process exit code.
 *
 * The function returns 0 when all tests pass and 1 on any failure (including
 * setup failure).
 *
 * @return int Process exit code: 0 on success, 1 on failure.
 */
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
