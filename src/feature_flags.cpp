#include "feature_flags.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>

// Singleton instance
FeatureFlags &FeatureFlags::getInstance() {
  static FeatureFlags instance;
  return instance;
}

FeatureFlags::FeatureFlags() { initializeDefaults(); }

void FeatureFlags::initializeDefaults() {
  // All features disabled by default for safety
  flags_.clear();
  rolloutPercentages_.clear();
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

void FeatureFlags::setFlag(const std::string &flag, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  flags_[flag] = enabled;
}

bool FeatureFlags::isEnabled(const std::string &flag) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = flags_.find(flag);
  return it != flags_.end() ? it->second : false;
}

void FeatureFlags::setRolloutPercentage(const std::string &flag,
                                        double percentage) {
  std::lock_guard<std::mutex> lock(mutex_);
  rolloutPercentages_[flag] = std::clamp(percentage, 0.0, 100.0);
}

double FeatureFlags::getRolloutPercentage(const std::string &flag) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = rolloutPercentages_.find(flag);
  return it != rolloutPercentages_.end() ? it->second : 0.0;
}

bool FeatureFlags::shouldEnableForUser(const std::string &flag,
                                       const std::string &userId) const {
  double percentage = getRolloutPercentage(flag);
  if (percentage >= 100.0)
    return true;
  if (percentage <= 0.0)
    return false;

  // Use stable FNV-1a hash with finer granularity (10,000 buckets for 0.01% precision)
  auto fnv1a64 = [](std::string_view s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
  };
  const uint64_t h = fnv1a64(userId);
  const double bucket = static_cast<double>(h % 10000) / 100.0; // [0,100)
  return bucket < percentage;
}

std::unordered_map<std::string, bool> FeatureFlags::getAllFlags() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return flags_;
}

void FeatureFlags::resetToDefaults() {
  std::lock_guard<std::mutex> lock(mutex_);
  initializeDefaults();
}

void FeatureFlags::loadFromConfig(const std::string &configFile) {
  try {
    std::ifstream file(configFile);
    if (!file.is_open()) {
      std::cerr << "Warning: Could not open feature flags config file: "
                << configFile << '\n';
      return;
    }

    nlohmann::json config;
    file >> config;

    // Prepare merged snapshots
    std::unordered_map<std::string, bool> newFlags;
    std::unordered_map<std::string, double> newRollouts;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      newFlags = flags_;
      newRollouts = rolloutPercentages_;
    }
    if (config.contains("flags") && config["flags"].is_object()) {
      for (const auto &[key, value] : config["flags"].items()) {
        if (value.is_boolean()) newFlags[key] = value.get<bool>();
      }
    }
    if (config.contains("rollout_percentages") && config["rollout_percentages"].is_object()) {
      for (const auto &[key, value] : config["rollout_percentages"].items()) {
        if (value.is_number()) {
          const double val = value.get<double>();
          newRollouts[key] = std::clamp(val, 0.0, 100.0);
        }
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      flags_.swap(newFlags);
      rolloutPercentages_.swap(newRollouts);
    }
    std::cout << "Feature flags loaded from: " << configFile << '\n';
  } catch (const std::exception &e) {
    std::cerr << "Error loading feature flags config: " << e.what() << '\n';
  }
}

void FeatureFlags::saveToConfig(const std::string &configFile) const {
  try {
    nlohmann::json config;

    std::lock_guard<std::mutex> lock(mutex_);

    config["flags"] = flags_;
    config["rollout_percentages"] = rolloutPercentages_;

    // Atomic-ish save: write temp then rename
    const std::string tmpFile = configFile + ".tmp";
    std::ofstream file(tmpFile);
    if (!file.is_open()) {
      std::cerr << "Error: Could not open temp file for writing: "
                << tmpFile << '\n';
      return;
    }

    file << config.dump(4);
    file.close();
    // If you add <filesystem>, prefer std::filesystem::rename with error_code.
    if (std::rename(tmpFile.c_str(), configFile.c_str()) != 0) {
      std::cerr << "Error: Could not replace config file: "
                << configFile << '\n';
      std::remove(tmpFile.c_str());
      return;
    }
    std::cout << "Feature flags saved to: " << configFile << '\n';
  } catch (const std::exception &e) {
    std::cerr << "Error saving feature flags config: " << e.what() << '\n';
  }
}
