#include "config_manager.hpp"
#include "logger.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& configPath) {
    std::cout << "Loading configuration from: " << configPath << std::endl;
    
    bool result = parseConfigFile(configPath);
    if (result) {
        std::cout << "Configuration loaded successfully with " << configData.size() << " parameters" << std::endl;
    } else {
        std::cerr << "Failed to load configuration from: " << configPath << std::endl;
    }
    return result;
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = configData.find(key);
    if (it != configData.end()) {
        return it->second;
    }
    return defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) const {
    auto it = configData.find(key);
    if (it != configData.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

bool ConfigManager::getBool(const std::string& key, bool defaultValue) const {
    auto it = configData.find(key);
    if (it != configData.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes" || value == "on";
    }
    return defaultValue;
}

double ConfigManager::getDouble(const std::string& key, double defaultValue) const {
    auto it = configData.find(key);
    if (it != configData.end()) {
        try {
            return std::stod(it->second);
        } catch (const std::exception&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

std::unordered_set<std::string> ConfigManager::getStringSet(const std::string& key) const {
    std::unordered_set<std::string> result;
    auto it = configData.find(key);
    if (it != configData.end()) {
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
