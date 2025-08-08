#pragma once

#include <string>
#include <unordered_map>
#include <memory>

class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    bool loadConfig(const std::string& configPath);
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    
private:
    ConfigManager() = default;
    std::unordered_map<std::string, std::string> configData;
    bool parseConfigFile(const std::string& configPath);
};
