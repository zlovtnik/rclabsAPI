#pragma once

#include "log_aggregator.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Configuration structures for log aggregation
struct StructuredLoggingConfig {
  bool enabled = false;
  std::string default_component = "system";
};

struct AggregationConfig {
  bool enabled = false;
  std::vector<LogDestinationConfig> destinations;
};

// Load log aggregation configuration from JSON
class LogAggregationConfigLoader {
public:
  static StructuredLoggingConfig
  loadStructuredLoggingConfig(const nlohmann::json &config);
  static AggregationConfig loadAggregationConfig(const nlohmann::json &config);
  static LogDestinationConfig
  loadDestinationConfig(const nlohmann::json &dest_config);

private:
  static LogDestinationType parseDestinationType(const std::string &type_str);
  static LogLevel parseLogLevel(const std::string &level_str);
  static std::unordered_set<LogLevel>
  parseAllowedLevels(const nlohmann::json &levels_array);
};