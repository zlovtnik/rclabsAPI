#include "config_manager.hpp"
#include "logger.hpp"
#include "lock_utils.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <format>

static std::timed_mutex configMutex;

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& configPath) {
    etl_plus::ScopedTimedLock<std::timed_mutex> lock(configMutex, std::chrono::milliseconds(5000), "configMutex");
    std::cout << "Loading configuration from: " << configPath << std::endl;
    
    configFilePath = configPath;  // Store for reload functionality
    bool result = parseConfigFile(configPath);
    if (result) {
        std::cout << "Configuration loaded successfully with " << configData.size() << " parameters" << std::endl;
    } else {
        std::cerr << "Failed to load configuration from: " << configPath << std::endl;
    }
    return result;
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    if (auto it = configData.find(key); it != configData.end()) {
        return it->second;
    }
    return defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) const {
    if (auto it = configData.find(key); it != configData.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::invalid_argument&) {
            return defaultValue;
        } catch (const std::out_of_range&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

bool ConfigManager::getBool(const std::string& key, bool defaultValue) const {
    if (auto it = configData.find(key); it != configData.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes" || value == "on";
    }
    return defaultValue;
}

double ConfigManager::getDouble(const std::string& key, double defaultValue) const {
    if (auto it = configData.find(key); it != configData.end()) {
        try {
            return std::stod(it->second);
        } catch (const std::invalid_argument&) {
            return defaultValue;
        } catch (const std::out_of_range&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> ConfigManager::getStringSet(const std::string& key) const {
    std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> result;
    if (auto it = configData.find(key); it != configData.end()) {
        std::string value = it->second;
        if (!value.empty() && value.front() == '[' && value.back() == ']') {
            value = value.substr(1, value.length() - 2);
        }
        
        std::stringstream ss(value);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item.erase(0, item.find_first_not_of(" \t\""));
            item.erase(item.find_last_not_of(" \t\"") + 1);
            if (!item.empty()) {
                result.insert(item);
            }
        }
    }
    return result;
}

LogConfig ConfigManager::getLoggingConfig() const {
    LogConfig config;
    
    config.level = parseLogLevel(getString("logging.level", "INFO"));
    config.format = parseLogFormat(getString("logging.format", "TEXT"));
    config.consoleOutput = getBool("logging.console_output", true);
    config.fileOutput = getBool("logging.file_output", true);
    config.asyncLogging = getBool("logging.async_logging", false);
    config.logFile = getString("logging.log_file", "logs/etlplus.log");
    config.maxFileSize = static_cast<size_t>(getInt("logging.max_file_size", 10485760));
    config.maxBackupFiles = getInt("logging.max_backup_files", 5);
    config.enableRotation = getBool("logging.enable_rotation", true);
    config.componentFilter = getStringSet("logging.component_filter");
    config.includeMetrics = getBool("logging.include_metrics", false);
    config.flushInterval = getInt("logging.flush_interval", 1000);
    
    return config;
}

// ===== Monitoring Configuration Implementation =====

MonitoringConfig ConfigManager::getMonitoringConfig() const {
    std::scoped_lock lock(configMutex);
    return MonitoringConfig::fromConfig(*this);
}

WebSocketConfig ConfigManager::getWebSocketConfig() const {
    std::scoped_lock lock(configMutex);
    return WebSocketConfig::fromConfig(*this);
}

JobTrackingConfig ConfigManager::getJobTrackingConfig() const {
    std::scoped_lock lock(configMutex);
    return JobTrackingConfig::fromConfig(*this);
}

ConfigValidationResult ConfigManager::validateMonitoringConfig() const {
    std::scoped_lock lock(configMutex);
    auto config = MonitoringConfig::fromConfig(*this);
    return config.validate();
}

ConfigValidationResult ConfigManager::validateConfiguration() const {
    std::scoped_lock lock(configMutex);
    ConfigValidationResult result;
    
    // Validate monitoring configuration
    auto monitoringResult = validateMonitoringConfig();
    result.isValid = result.isValid && monitoringResult.isValid;
    result.errors.insert(result.errors.end(), monitoringResult.errors.begin(), monitoringResult.errors.end());
    result.warnings.insert(result.warnings.end(), monitoringResult.warnings.begin(), monitoringResult.warnings.end());
    
    // Add other configuration validations here as needed
    
    return result;
}

bool ConfigManager::updateMonitoringConfig(const MonitoringConfig& newConfig) {
    std::scoped_lock lock(configMutex);
    
    // Validate new configuration
    if (auto validationResult = newConfig.validate(); !validationResult.isValid) {
        std::cerr << "Invalid monitoring configuration:" << std::endl;
        for (const auto& error : validationResult.errors) {
            std::cerr << "  Error: " << error << std::endl;
        }
        return false;
    }
    
    // Convert config to map and update
    if (auto configMap = configToMap(newConfig); validateAndUpdateConfigData("monitoring", configMap)) {
        notifyConfigChange("monitoring", newConfig);
        return true;
    }
    
    return false;
}

bool ConfigManager::updateWebSocketConfig(const WebSocketConfig& newConfig) {
    std::scoped_lock lock(configMutex);
    
    // Validate new configuration
    if (auto validationResult = newConfig.validate(); !validationResult.isValid) {
        std::cerr << "Invalid WebSocket configuration:" << std::endl;
        for (const auto& error : validationResult.errors) {
            std::cerr << "  Error: " << error << std::endl;
        }
        return false;
    }
    
    // Convert config to map and update
    if (auto configMap = webSocketConfigToMap(newConfig); validateAndUpdateConfigData("monitoring.websocket", configMap)) {
        MonitoringConfig fullConfig = getMonitoringConfig();
        notifyConfigChange("websocket", fullConfig);
        return true;
    }
    
    return false;
}

bool ConfigManager::updateJobTrackingConfig(const JobTrackingConfig& newConfig) {
    std::scoped_lock lock(configMutex);
    
    // Validate new configuration
    if (auto validationResult = newConfig.validate(); !validationResult.isValid) {
        std::cerr << "Invalid job tracking configuration:" << std::endl;
        for (const auto& error : validationResult.errors) {
            std::cerr << "  Error: " << error << std::endl;
        }
        return false;
    }
    
    // Convert config to map and update
    if (auto configMap = jobTrackingConfigToMap(newConfig); validateAndUpdateConfigData("monitoring.job_tracking", configMap)) {
        MonitoringConfig fullConfig = getMonitoringConfig();
        notifyConfigChange("job_tracking", fullConfig);
        return true;
    }
    
    return false;
}

bool ConfigManager::reloadConfiguration() {
    if (configFilePath.empty()) {
        std::cerr << "No configuration file path available for reload" << std::endl;
        return false;
    }
    
    std::cout << "Reloading configuration from: " << configFilePath << std::endl;
    
    // Store old config for comparison
    auto oldMonitoringConfig = getMonitoringConfig();
    
    // Reload from file
    bool result = loadConfig(configFilePath);
    if (result) {
        // Check if monitoring config changed and notify if so
        auto newMonitoringConfig = getMonitoringConfig();
        if (!(oldMonitoringConfig == newMonitoringConfig)) {
            notifyConfigChange("monitoring", newMonitoringConfig);
        }
    }
    
    return result;
}

void ConfigManager::registerConfigChangeCallback(const std::string& section, const ConfigChangeCallback& callback) {
    std::scoped_lock lock(configMutex);
    changeCallbacks[section] = callback;
}

void ConfigManager::unregisterConfigChangeCallback(const std::string& section) {
    std::scoped_lock lock(configMutex);
    changeCallbacks.erase(section);
}

void ConfigManager::notifyConfigChange(const std::string& section, const MonitoringConfig& newConfig) {
    // Note: Called with configMutex already locked
    auto it = changeCallbacks.find(section);
    if (it != changeCallbacks.end() && it->second) {
        try {
            it->second(section, newConfig);
        } catch (const std::runtime_error& e) {
            std::cerr << "Runtime error in config change callback for section '" << section << "': " << e.what() << std::endl;
        } catch (const std::logic_error& e) {
            std::cerr << "Logic error in config change callback for section '" << section << "': " << e.what() << std::endl;
        }
    }
}

bool ConfigManager::validateAndUpdateConfigData(const std::string& section, 
                                               const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& updates) {
    // Note: Called with configMutex already locked
    try {
        // Update configData with new values
        for (const auto& [key, value] : updates) {
            configData[key] = value;
        }
        return true;
    } catch (const std::runtime_error& e) {
        std::cerr << "Runtime error updating configuration data for section '" << section << "': " << e.what() << std::endl;
        return false;
    } catch (const std::logic_error& e) {
        std::cerr << "Logic error updating configuration data for section '" << section << "': " << e.what() << std::endl;
        return false;
    }
}

std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> ConfigManager::configToMap(const MonitoringConfig& config) const {
    std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> result;
    
    // WebSocket config
    auto wsMap = webSocketConfigToMap(config.websocket);
    result.insert(wsMap.begin(), wsMap.end());
    
    // Job tracking config
    auto jtMap = jobTrackingConfigToMap(config.jobTracking);
    result.insert(jtMap.begin(), jtMap.end());
    
    return result;
}

std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> ConfigManager::webSocketConfigToMap(const WebSocketConfig& config) const {
    return {
        {"monitoring.websocket.enabled", config.enabled ? "true" : "false"},
        {"monitoring.websocket.port", std::to_string(config.port)},
        {"monitoring.websocket.max_connections", std::to_string(config.maxConnections)},
        {"monitoring.websocket.heartbeat_interval", std::to_string(config.heartbeatInterval)},
        {"monitoring.websocket.message_queue_size", std::to_string(config.messageQueueSize)}
    };
}

std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> ConfigManager::jobTrackingConfigToMap(const JobTrackingConfig& config) const {
    return {
        {"monitoring.job_tracking.progress_update_interval", std::to_string(config.progressUpdateInterval)},
        {"monitoring.job_tracking.log_streaming_enabled", config.logStreamingEnabled ? "true" : "false"},
        {"monitoring.job_tracking.metrics_collection_enabled", config.metricsCollectionEnabled ? "true" : "false"},
        {"monitoring.job_tracking.timeout_warning_threshold", std::to_string(config.timeoutWarningThreshold)}
    };
}

LogLevel ConfigManager::parseLogLevel(const std::string& levelStr) const {
    std::string level = levelStr;
    std::transform(level.begin(), level.end(), level.begin(), ::toupper);
    
    if (level == "DEBUG") return LogLevel::DEBUG;
    if (level == "INFO") return LogLevel::INFO;
    if (level == "WARN" || level == "WARNING") return LogLevel::WARN;
    if (level == "ERROR") return LogLevel::ERROR;
    if (level == "FATAL") return LogLevel::FATAL;
    
    return LogLevel::INFO;
}

LogFormat ConfigManager::parseLogFormat(const std::string& formatStr) const {
    std::string format = formatStr;
    std::transform(format.begin(), format.end(), format.begin(), ::toupper);
    
    if (format == "JSON") return LogFormat::JSON;
    return LogFormat::TEXT;
}

bool ConfigManager::parseConfigFile(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "Cannot open config file: " << configPath << std::endl;
        return false;
    }
    
    std::string line;
    std::string currentSection = "";
    int lineNumber = 0;
    
    while (std::getline(file, line)) {
        lineNumber++;
        
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty() || line[0] == '#' || line.substr(0, 2) == "//") {
            continue;
        }
        
        if (line == "{" || line == "}" || line == ",") {
            continue;
        }
        
        if (line.back() == ':' && line.front() == '"') {
            currentSection = line.substr(1, line.length() - 3);
            continue;
        }
        
        if (line.back() == ',') {
            line.pop_back();
        }
        
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            key.erase(0, key.find_first_not_of(" \t\""));
            key.erase(key.find_last_not_of(" \t\"") + 1);
            
            value.erase(0, value.find_first_not_of(" \t\""));
            value.erase(value.find_last_not_of(" \t\",") + 1);
            
            if (!currentSection.empty()) {
                key = currentSection + "." + key;
            }
            
            configData[key] = value;
        }
    }
    
    return true;
}

// ===== WebSocketConfig Implementation =====

WebSocketConfig WebSocketConfig::fromConfig(const ConfigManager& config) {
    WebSocketConfig wsConfig;
    
    wsConfig.enabled = config.getBool("monitoring.websocket.enabled", true);
    wsConfig.port = config.getInt("monitoring.websocket.port", 8081);
    wsConfig.maxConnections = config.getInt("monitoring.websocket.max_connections", 100);
    wsConfig.heartbeatInterval = config.getInt("monitoring.websocket.heartbeat_interval", 30);
    wsConfig.messageQueueSize = config.getInt("monitoring.websocket.message_queue_size", 1000);
    
    return wsConfig;
}

ConfigValidationResult WebSocketConfig::validate() const {
    ConfigValidationResult result;
    
    if (port <= 0 || port > 65535) {
        std::stringstream ss;
        ss << "WebSocket port must be between 1 and 65535, got: " << port;
        result.addError(ss.str());
    }
    
    if (port < 1024) {
        std::stringstream ss;
        ss << "WebSocket port " << port << " is in privileged range (< 1024)";
        result.addWarning(ss.str());
    }
    
    if (maxConnections <= 0) {
        std::stringstream ss;
        ss << "WebSocket max_connections must be positive, got: " << maxConnections;
        result.addError(ss.str());
    }
    
    if (maxConnections > 10000) {
        std::stringstream ss;
        ss << "WebSocket max_connections is very high (" << maxConnections << "), this may cause resource issues";
        result.addWarning(ss.str());
    }
    
    if (heartbeatInterval <= 0) {
        std::stringstream ss;
        ss << "WebSocket heartbeat_interval must be positive, got: " << heartbeatInterval;
        result.addError(ss.str());
    }
    
    if (heartbeatInterval < 5) {
        std::stringstream ss;
        ss << "WebSocket heartbeat_interval is very low (" << heartbeatInterval << " seconds), this may cause unnecessary network traffic";
        result.addWarning(ss.str());
    }
    
    if (messageQueueSize <= 0) {
        std::stringstream ss;
        ss << "WebSocket message_queue_size must be positive, got: " << messageQueueSize;
        result.addError(ss.str());
    }
    
    if (messageQueueSize > 100000) {
        std::stringstream ss;
        ss << "WebSocket message_queue_size is very high (" << messageQueueSize << "), this may cause memory issues";
        result.addWarning(ss.str());
    }
    
    return result;
}

bool WebSocketConfig::operator==(const WebSocketConfig& other) const {
    return enabled == other.enabled &&
           port == other.port &&
           maxConnections == other.maxConnections &&
           heartbeatInterval == other.heartbeatInterval &&
           messageQueueSize == other.messageQueueSize;
}

// ===== JobTrackingConfig Implementation =====

JobTrackingConfig JobTrackingConfig::fromConfig(const ConfigManager& config) {
    JobTrackingConfig jtConfig;
    
    jtConfig.progressUpdateInterval = config.getInt("monitoring.job_tracking.progress_update_interval", 5);
    jtConfig.logStreamingEnabled = config.getBool("monitoring.job_tracking.log_streaming_enabled", true);
    jtConfig.metricsCollectionEnabled = config.getBool("monitoring.job_tracking.metrics_collection_enabled", true);
    jtConfig.timeoutWarningThreshold = config.getInt("monitoring.job_tracking.timeout_warning_threshold", 25);
    
    return jtConfig;
}

ConfigValidationResult JobTrackingConfig::validate() const {
    ConfigValidationResult result;
    
    if (progressUpdateInterval <= 0) {
        std::stringstream ss;
        ss << "Job tracking progress_update_interval must be positive, got: " << progressUpdateInterval;
        result.addError(ss.str());
    }
    
    if (progressUpdateInterval < 1) {
        std::stringstream ss;
        ss << "Job tracking progress_update_interval is very low (" << progressUpdateInterval << " seconds), this may cause performance issues";
        result.addWarning(ss.str());
    }
    
    if (progressUpdateInterval > 300) {
        std::stringstream ss;
        ss << "Job tracking progress_update_interval is very high (" << progressUpdateInterval << " seconds), updates may seem delayed";
        result.addWarning(ss.str());
    }
    
    if (timeoutWarningThreshold <= 0) {
        std::stringstream ss;
        ss << "Job tracking timeout_warning_threshold must be positive, got: " << timeoutWarningThreshold;
        result.addError(ss.str());
    }
    
    if (timeoutWarningThreshold < 5) {
        std::stringstream ss;
        ss << "Job tracking timeout_warning_threshold is very low (" << timeoutWarningThreshold << " minutes), may cause false alarms";
        result.addWarning(ss.str());
    }
    
    return result;
}

bool JobTrackingConfig::operator==(const JobTrackingConfig& other) const {
    return progressUpdateInterval == other.progressUpdateInterval &&
           logStreamingEnabled == other.logStreamingEnabled &&
           metricsCollectionEnabled == other.metricsCollectionEnabled &&
           timeoutWarningThreshold == other.timeoutWarningThreshold;
}

// ===== MonitoringConfig Implementation =====

MonitoringConfig MonitoringConfig::fromConfig(const ConfigManager& config) {
    MonitoringConfig monitoringConfig;
    
    monitoringConfig.websocket = WebSocketConfig::fromConfig(config);
    monitoringConfig.jobTracking = JobTrackingConfig::fromConfig(config);
    
    return monitoringConfig;
}

ConfigValidationResult MonitoringConfig::validate() const {
    ConfigValidationResult result;
    
    // Validate WebSocket configuration
    auto wsResult = websocket.validate();
    result.isValid = result.isValid && wsResult.isValid;
    for (const auto& error : wsResult.errors) {
        result.errors.push_back("WebSocket: " + error);
    }
    for (const auto& warning : wsResult.warnings) {
        result.warnings.push_back("WebSocket: " + warning);
    }
    
    // Validate job tracking configuration
    auto jtResult = jobTracking.validate();
    result.isValid = result.isValid && jtResult.isValid;
    for (const auto& error : jtResult.errors) {
        result.errors.push_back("Job Tracking: " + error);
    }
    for (const auto& warning : jtResult.warnings) {
        result.warnings.push_back("Job Tracking: " + warning);
    }
    
    // Cross-validation checks
    if (websocket.enabled && jobTracking.progressUpdateInterval > websocket.heartbeatInterval) {
        std::stringstream ss;
        ss << "Job progress update interval (" << jobTracking.progressUpdateInterval << "s) is greater than WebSocket heartbeat interval (" << websocket.heartbeatInterval << "s)";
        result.addWarning(ss.str());
    }
    
    return result;
}

bool MonitoringConfig::operator==(const MonitoringConfig& other) const {
    return websocket == other.websocket &&
           jobTracking == other.jobTracking;
}
