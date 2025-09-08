#include "feature_flags.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <random>
#include <functional>
#include <nlohmann/json.hpp>

// Singleton instance
FeatureFlags& FeatureFlags::getInstance() {
    static FeatureFlags instance;
    return instance;
}

FeatureFlags::FeatureFlags() {
    initializeDefaults();
}

void FeatureFlags::initializeDefaults() {
    // All features disabled by default for safety
    flags_[NEW_LOGGER_SYSTEM] = false;
    flags_[NEW_EXCEPTION_SYSTEM] = false;
    flags_[NEW_REQUEST_HANDLER] = false;
    flags_[NEW_WEBSOCKET_MANAGER] = false;
    flags_[NEW_CONCURRENCY_PATTERNS] = false;
    flags_[NEW_TYPE_SYSTEM] = false;

    // Default rollout percentages (0% = disabled, 100% = fully enabled)
    rolloutPercentages_[NEW_LOGGER_SYSTEM] = 0.0;
    rolloutPercentages_[NEW_EXCEPTION_SYSTEM] = 0.0;
    rolloutPercentages_[NEW_REQUEST_HANDLER] = 0.0;
    rolloutPercentages_[NEW_WEBSOCKET_MANAGER] = 0.0;
    rolloutPercentages_[NEW_CONCURRENCY_PATTERNS] = 0.0;
    rolloutPercentages_[NEW_TYPE_SYSTEM] = 0.0;
}

void FeatureFlags::setFlag(const std::string& flag, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    flags_[flag] = enabled;
}

bool FeatureFlags::isEnabled(const std::string& flag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = flags_.find(flag);
    return it != flags_.end() ? it->second : false;
}

void FeatureFlags::setRolloutPercentage(const std::string& flag, double percentage) {
    std::lock_guard<std::mutex> lock(mutex_);
    rolloutPercentages_[flag] = std::max(0.0, std::min(100.0, percentage));
}

double FeatureFlags::getRolloutPercentage(const std::string& flag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rolloutPercentages_.find(flag);
    return it != rolloutPercentages_.end() ? it->second : 0.0;
}

bool FeatureFlags::shouldEnableForUser(const std::string& flag, const std::string& userId) const {
    double percentage = getRolloutPercentage(flag);
    if (percentage >= 100.0) return true;
    if (percentage <= 0.0) return false;

    // Use user ID hash to determine if user should get the feature
    std::hash<std::string> hasher;
    size_t hash = hasher(userId);
    double userPercentage = static_cast<double>(hash % 100);

    return userPercentage < percentage;
}

std::unordered_map<std::string, bool> FeatureFlags::getAllFlags() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return flags_;
}

void FeatureFlags::resetToDefaults() {
    std::lock_guard<std::mutex> lock(mutex_);
    initializeDefaults();
}

void FeatureFlags::loadFromConfig(const std::string& configFile) {
    try {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open feature flags config file: " << configFile << std::endl;
            return;
        }

        nlohmann::json config;
        file >> config;

        std::lock_guard<std::mutex> lock(mutex_);

        if (config.contains("flags")) {
            for (const auto& [key, value] : config["flags"].items()) {
                if (value.is_boolean()) {
                    flags_[key] = value;
                }
            }
        }

        if (config.contains("rollout_percentages")) {
            for (const auto& [key, value] : config["rollout_percentages"].items()) {
                if (value.is_number()) {
                    rolloutPercentages_[key] = value;
                }
            }
        }

        std::cout << "Feature flags loaded from: " << configFile << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading feature flags config: " << e.what() << std::endl;
    }
}

void FeatureFlags::saveToConfig(const std::string& configFile) const {
    try {
        nlohmann::json config;

        std::lock_guard<std::mutex> lock(mutex_);

        config["flags"] = flags_;
        config["rollout_percentages"] = rolloutPercentages_;

        std::ofstream file(configFile);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open feature flags config file for writing: " << configFile << std::endl;
            return;
        }

        file << config.dump(4);
        std::cout << "Feature flags saved to: " << configFile << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error saving feature flags config: " << e.what() << std::endl;
    }
}
