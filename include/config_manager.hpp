#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include "logger.hpp"

class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    bool loadConfig(const std::string& configPath);
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    double getDouble(const std::string& key, double defaultValue = 0.0) const;
    std::unordered_set<std::string> getStringSet(const std::string& key) const;
    
    // Logging configuration helpers
    LogConfig getLoggingConfig() const;
    
private:
    ConfigManager() = default;
    std::unordered_map<std::string, std::string> configData;
    bool parseConfigFile(const std::string& configPath);
    LogLevel parseLogLevel(const std::string& levelStr) const;
    LogFormat parseLogFormat(const std::string& formatStr) const;
};
