#ifndef FEATURE_FLAGS_HPP
#define FEATURE_FLAGS_HPP

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Feature flags for gradual rollout of refactored components
 *
 * This system allows enabling/disabling new features during deployment
 * to minimize risk and enable gradual rollout strategies.
 */
class FeatureFlags {
public:
  // Feature flag keys for refactored components
  static constexpr const char *NEW_LOGGER_SYSTEM = "new_logger_system";
  static constexpr const char *NEW_EXCEPTION_SYSTEM = "new_exception_system";
  static constexpr const char *NEW_REQUEST_HANDLER = "new_request_handler";
  static constexpr const char *NEW_WEBSOCKET_MANAGER = "new_websocket_manager";
  static constexpr const char *NEW_CONCURRENCY_PATTERNS =
      "new_concurrency_patterns";
  static constexpr const char *NEW_TYPE_SYSTEM = "new_type_system";

  // Singleton instance
  static FeatureFlags &getInstance();

  // Feature flag management
  void setFlag(const std::string &flag, bool enabled);
  bool isEnabled(const std::string &flag) const;
  void loadFromConfig(const std::string &configFile);
  void saveToConfig(const std::string &configFile) const;

  // Rollout control
  void setRolloutPercentage(const std::string &flag, double percentage);
  double getRolloutPercentage(const std::string &flag) const;
  bool shouldEnableForUser(const std::string &flag,
                           const std::string &userId) const;

  // Feature flag status
  std::unordered_map<std::string, bool> getAllFlags() const;
  void resetToDefaults();

private:
  FeatureFlags();
  ~FeatureFlags() = default;
  FeatureFlags(const FeatureFlags &) = delete;
  FeatureFlags &operator=(const FeatureFlags &) = delete;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, bool> flags_;
  std::unordered_map<std::string, double> rolloutPercentages_;

  // Default feature flag values (all disabled by default for safety)
  void initializeDefaults();
};

#endif // FEATURE_FLAGS_HPP
