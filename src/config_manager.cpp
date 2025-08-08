#include "config_manager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& configPath) {
    return parseConfigFile(configPath);
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = configData.find(key);
    return (it != configData.end()) ? it->second : defaultValue;
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
        std::cerr << "Failed to open config file: " << configPath << std::endl;
        return false;
    }
    
    // Simple JSON-like parser (basic implementation)
    std::string line;
    std::string currentSection;
    
    while (std::getline(file, line)) {
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
        }
    }
    
    return true;
}
