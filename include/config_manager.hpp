#pragma once

#include "logger.hpp"
#include "transparent_string_hash.hpp"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations
class ConfigManager;

// Configuration validation result
struct ConfigValidationResult {
  bool isValid = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;

  void addError(const std::string &error) {
    isValid = false;
    errors.push_back(error);
  }

  void addWarning(const std::string &warning) { warnings.push_back(warning); }
};

// WebSocket configuration structure
struct WebSocketConfig {
  bool enabled = true;
  int port = 8081;
  int maxConnections = 100;
  int heartbeatInterval = 30; // seconds
  int messageQueueSize = 1000;

  static WebSocketConfig fromConfig(const ConfigManager &config);
  ConfigValidationResult validate() const;
  bool operator==(const WebSocketConfig &other) const;
};

// Job tracking configuration structure
struct JobTrackingConfig {
  int progressUpdateInterval = 5; // seconds
  bool logStreamingEnabled = true;
  bool metricsCollectionEnabled = true;
  int timeoutWarningThreshold = 25; // minutes

  static JobTrackingConfig fromConfig(const ConfigManager &config);
  ConfigValidationResult validate() const;
  bool operator==(const JobTrackingConfig &other) const;
};

// Monitoring configuration structure
struct MonitoringConfig {
  WebSocketConfig websocket;
  JobTrackingConfig jobTracking;
  // Note: NotificationConfig is defined in notification_service.hpp to avoid
  // circular dependency

  static MonitoringConfig fromConfig(const ConfigManager &config);
  ConfigValidationResult validate() const;
  bool operator==(const MonitoringConfig &other) const;
};

// Configuration change callback type
using ConfigChangeCallback = std::function<void(
    const std::string &section, const MonitoringConfig &newConfig)>;

class ConfigManager {
public:
  static ConfigManager &getInstance();

  bool loadConfig(const std::string &configPath);
  std::string getString(const std::string &key,
                        const std::string &defaultValue = "") const;
  int getInt(const std::string &key, int defaultValue = 0) const;
  bool getBool(const std::string &key, bool defaultValue = false) const;
  double getDouble(const std::string &key, double defaultValue = 0.0) const;
  std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>
  getStringSet(const std::string &key) const;

  // Logging configuration helpers
  LogConfig getLoggingConfig() const;

  // Monitoring configuration helpers
  MonitoringConfig getMonitoringConfig() const;
  WebSocketConfig getWebSocketConfig() const;
  JobTrackingConfig getJobTrackingConfig() const;

  // Configuration validation
  ConfigValidationResult validateMonitoringConfig() const;
  ConfigValidationResult validateConfiguration() const;

  // Dynamic configuration updates
  bool updateMonitoringConfig(const MonitoringConfig &newConfig);
  bool updateWebSocketConfig(const WebSocketConfig &newConfig);
  bool updateJobTrackingConfig(const JobTrackingConfig &newConfig);
  bool reloadConfiguration();

  // Configuration change notifications
  void registerConfigChangeCallback(const std::string &section,
                                    const ConfigChangeCallback &callback);
  void unregisterConfigChangeCallback(const std::string &section);

  // Get raw JSON configuration
  const nlohmann::json &getJsonConfig() const;

  // Configuration access with validation
  template <typename T>
  T getValidatedValue(
      const std::string &key, const T &defaultValue,
      const std::function<bool(const T &)> &validator = nullptr) const;

private:
  /**
   * @brief Default constructor.
   *
   * Private and defaulted to enforce the singleton pattern; use
   * ConfigManager::getInstance() to obtain the single shared instance.
   */
  ConfigManager() = default;
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
      configData;
  std::unordered_map<std::string, ConfigChangeCallback, TransparentStringHash,
                     std::equal_to<>>
      changeCallbacks;
  std::string configFilePath;
  nlohmann::json rawConfig_; // Store the raw JSON configuration

  bool parseConfigFile(const std::string &configPath);
  void flattenJson(const nlohmann::json &json, const std::string &prefix,
                   int currentDepth, int maxDepth,
                   std::unordered_set<const nlohmann::json *> &visited);
  LogLevel parseLogLevel(const std::string &levelStr) const;
  LogFormat parseLogFormat(const std::string &formatStr) const;

  // Helper methods for configuration updates
  void notifyConfigChange(const std::string &section,
                          const MonitoringConfig &newConfig);
  bool updateConfigData(
      const std::string &section,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &updates);
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
  configToMap(const MonitoringConfig &config) const;
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
  webSocketConfigToMap(const WebSocketConfig &config) const;
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
  jobTrackingConfigToMap(const JobTrackingConfig &config) const;
};

// Template method implementation
template <typename T>
/**
 * @brief Retrieve a typed configuration value for a key with optional runtime
 * validation.
 *
 * Retrieves the configuration value for @p key, using the overload-specific
 * getter for the requested template type T (supported: std::string, int, bool,
 * double). If the key is absent the provided @p defaultValue is returned. If a
 * @p validator is supplied and returns false for the retrieved value, @p
 * defaultValue is returned instead.
 *
 * @tparam T The requested return type. Only `std::string`, `int`, `bool`, and
 * `double` are supported; requesting any other type triggers a compile-time
 * error.
 * @param key Configuration key to look up.
 * @param defaultValue Value returned when the key is missing or validation
 * fails.
 * @param validator Optional predicate to validate the retrieved value; called
 * only if the value was successfully retrieved. If `validator` returns false,
 * @p defaultValue is returned.
 * @return T The configuration value (or @p defaultValue when missing or
 * invalid).
 */
T ConfigManager::getValidatedValue(
    const std::string &key, const T &defaultValue,
    const std::function<bool(const T &)> &validator) const {
  T value;

  // Get value based on type
  if constexpr (std::is_same_v<T, std::string>) {
    value = getString(key, defaultValue);
  } else if constexpr (std::is_same_v<T, int>) {
    value = getInt(key, defaultValue);
  } else if constexpr (std::is_same_v<T, bool>) {
    value = getBool(key, defaultValue);
  } else if constexpr (std::is_same_v<T, double>) {
    value = getDouble(key, defaultValue);
  } else {
    static_assert(std::is_same_v<T, std::string> || std::is_same_v<T, int> ||
                      std::is_same_v<T, bool> || std::is_same_v<T, double>,
                  "Unsupported type for getValidatedValue");
    return defaultValue;
  }

  // Apply validation if provided
  if (validator && !validator(value)) {
    return defaultValue;
  }

  return value;
}
