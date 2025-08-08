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
    CONFIG_LOG_INFO("Loading configuration from: " + configPath);
    bool result = parseConfigFile(configPath);
    if (result) {
        CONFIG_LOG_INFO("Configuration loaded successfully with " + std::to_string(configData.size()) + " parameters");
    } else {
        CONFIG_LOG_ERROR("Failed to load configuration from: " + configPath);
    }
    return result;
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = configData.find(key);
    if (it != configData.end()) {
        CONFIG_LOG_DEBUG("Retrieved config value for key '" + key + "': " + it->second);
        return it->second;
    }
    CONFIG_LOG_DEBUG("Using default value for key '" + key + "': " + defaultValue);
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
        return (value == "true" || value == "1" || value == "yes");
    }
    return defaultValue;
}

bool ConfigManager::parseConfigFile(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        CONFIG_LOG_ERROR("Failed to open config file: " + configPath);
        return false;
    }
    
    CONFIG_LOG_DEBUG("Parsing configuration file: " + configPath);
    
    // Simple JSON-like parser (basic implementation)
    std::string line;
    std::string currentSection;
    int lineNumber = 0;
    
    while (std::getline(file, line)) {
        lineNumber++;
        
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == '/' || line == "{" || line == "}") {
            continue;
        }
        
        // Handle sections (simple nested object support)
        if (line.back() == '{') {
            currentSection = line.substr(0, line.find('"', 1));
            currentSection = currentSection.substr(1); // Remove opening quote
            CONFIG_LOG_DEBUG("Found configuration section: " + currentSection);
            continue;
        }
        
        // Parse key-value pairs
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // Clean up key
            key.erase(0, key.find_first_not_of(" \t\""));
            key.erase(key.find_last_not_of(" \t\"") + 1);
            
            // Clean up value
            value.erase(0, value.find_first_not_of(" \t\""));
            value.erase(value.find_last_not_of(" \t\",") + 1);
            
            // Add section prefix if we're in a section
            if (!currentSection.empty()) {
                key = currentSection + "." + key;
            }
            
            configData[key] = value;
            CONFIG_LOG_DEBUG("Parsed config parameter: " + key + " = " + value);
        } else {
            CONFIG_LOG_WARN("Invalid config line " + std::to_string(lineNumber) + ": " + line);
        }
    }
    
    CONFIG_LOG_INFO("Configuration parsing completed. Loaded " + std::to_string(configData.size()) + " parameters");
    return true;
}
