#pragma once

#include "logger.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <type_traits>
#include "transparent_string_hash.hpp"

// Forward declarations
class ConfigManager;

// Configuration validation result
struct ConfigValidationResult {
    bool isValid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    void addError(const std::string& error) {
        isValid = false;
        errors.push_back(error);
    }
    
    void addWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
};

// WebSocket configuration structure
struct WebSocketConfig {
    bool enabled = true;
    int port = 8081;
    int maxConnections = 100;
    int heartbeatInterval = 30;  // seconds
    int messageQueueSize = 1000;
    
    static WebSocketConfig fromConfig(const ConfigManager& config);
    ConfigValidationResult validate() const;
    bool operator==(const WebSocketConfig& other) const;
};

// Job tracking configuration structure
struct JobTrackingConfig {
    int progressUpdateInterval = 5;  // seconds
    bool logStreamingEnabled = true;
    bool metricsCollectionEnabled = true;
    int timeoutWarningThreshold = 25;  // minutes
    
    static JobTrackingConfig fromConfig(const ConfigManager& config);
    ConfigValidationResult validate() const;
    bool operator==(const JobTrackingConfig& other) const;
};

// Monitoring configuration structure
struct MonitoringConfig {
    WebSocketConfig websocket;
    JobTrackingConfig jobTracking;
    // Note: NotificationConfig is defined in notification_service.hpp to avoid circular dependency
    
    static MonitoringConfig fromConfig(const ConfigManager& config);
    ConfigValidationResult validate() const;
    bool operator==(const MonitoringConfig& other) const;
};

// Configuration change callback type
using ConfigChangeCallback = std::function<void(const std::string& section, const MonitoringConfig& newConfig)>;

class ConfigManager {
public:
  static ConfigManager &getInstance();

  bool loadConfig(const std::string &configPath);
  std::string getString(const std::string &key,
                        const std::string &defaultValue = "") const;
  int getInt(const std::string &key, int defaultValue = 0) const;
  bool getBool(const std::string &key, bool defaultValue = false) const;
  double getDouble(const std::string &key, double defaultValue = 0.0) const;
  std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> getStringSet(const std::string &key) const;

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
  bool updateMonitoringConfig(const MonitoringConfig& newConfig);
  bool updateWebSocketConfig(const WebSocketConfig& newConfig);
  bool updateJobTrackingConfig(const JobTrackingConfig& newConfig);
  bool reloadConfiguration();
  
  // Configuration change notifications
  void registerConfigChangeCallback(const std::string& section, const ConfigChangeCallback& callback);
  void unregisterConfigChangeCallback(const std::string& section);
  
  // Configuration access with validation
  template<typename T>
  T getValidatedValue(const std::string& key, const T& defaultValue, 
                     std::function<bool(const T&)> validator = nullptr) const;

private:
  ConfigManager() = default;
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> configData;
  std::unordered_map<std::string, ConfigChangeCallback, TransparentStringHash, std::equal_to<>> changeCallbacks;
  std::string configFilePath;
  
  bool parseConfigFile(const std::string &configPath);
  LogLevel parseLogLevel(const std::string &levelStr) const;
  LogFormat parseLogFormat(const std::string &formatStr) const;
  
  // Helper methods for configuration updates
  void notifyConfigChange(const std::string& section, const MonitoringConfig& newConfig);
  bool validateAndUpdateConfigData(const std::string& section, const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& updates);
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> configToMap(const MonitoringConfig& config) const;
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> webSocketConfigToMap(const WebSocketConfig& config) const;
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> jobTrackingConfigToMap(const JobTrackingConfig& config) const;
};

// Template method implementation
template<typename T>
T ConfigManager::getValidatedValue(const std::string& key, const T& defaultValue, 
                                  std::function<bool(const T&)> validator) const {
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
