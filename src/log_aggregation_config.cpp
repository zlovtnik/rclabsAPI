#include "log_aggregation_config.hpp"
#include "log_aggregator.hpp"
#include <algorithm>
#include <iostream>

StructuredLoggingConfig LogAggregationConfigLoader::loadStructuredLoggingConfig(
    const nlohmann::json &config) {
  StructuredLoggingConfig structured_config;

  if (config.contains("structured_logging")) {
    const auto &structured = config["structured_logging"];

    if (structured.contains("enabled")) {
      structured_config.enabled = structured["enabled"];
    }

    if (structured.contains("default_component")) {
      structured_config.default_component = structured["default_component"];
    }
  }

  return structured_config;
}

AggregationConfig LogAggregationConfigLoader::loadAggregationConfig(
    const nlohmann::json &config) {
  AggregationConfig agg_config;

  if (config.contains("structured_logging") &&
      config["structured_logging"].contains("aggregation")) {
    const auto &aggregation = config["structured_logging"]["aggregation"];

    if (aggregation.contains("enabled")) {
      agg_config.enabled = aggregation["enabled"];
    }

    if (aggregation.contains("destinations") &&
        aggregation["destinations"].is_array()) {
      for (const auto &dest_json : aggregation["destinations"]) {
        try {
          auto dest_config = loadDestinationConfig(dest_json);
          agg_config.destinations.push_back(dest_config);
        } catch (const std::exception &e) {
          std::cerr << "Failed to load destination config: " << e.what()
                    << std::endl;
        }
      }
    }
  }

  return agg_config;
}

LogDestinationConfig LogAggregationConfigLoader::loadDestinationConfig(
    const nlohmann::json &dest_config) {
  LogDestinationConfig config;

  // Required fields
  if (dest_config.contains("type")) {
    config.type = parseDestinationType(dest_config["type"]);
  } else {
    throw std::runtime_error("Destination type is required");
  }

  if (dest_config.contains("name")) {
    config.name = dest_config["name"];
  } else {
    throw std::runtime_error("Destination name is required");
  }

  // Optional fields
  if (dest_config.contains("enabled")) {
    config.enabled = dest_config["enabled"];
  }

  if (dest_config.contains("endpoint")) {
    config.endpoint = dest_config["endpoint"];
  }

  if (dest_config.contains("auth_token")) {
    config.auth_token = dest_config["auth_token"];
  }

  if (dest_config.contains("headers") && dest_config["headers"].is_object()) {
    for (const auto &[key, value] : dest_config["headers"].items()) {
      if (value.is_string()) {
        config.headers[key] = value.get<std::string>();
      } else {
        std::cerr << "Warning: Non-string header value for key '" << key << "', skipping" << std::endl;
      }
    }
  }

  // Elasticsearch specific
  if (dest_config.contains("index_pattern")) {
    config.index_pattern = dest_config["index_pattern"];
  }

  if (dest_config.contains("pipeline")) {
    config.pipeline = dest_config["pipeline"];
  }

  // File specific
  if (dest_config.contains("file_path")) {
    config.file_path = dest_config["file_path"];
  }

  if (dest_config.contains("rotate_files")) {
    config.rotate_files = dest_config["rotate_files"];
  }

  if (dest_config.contains("max_file_size")) {
    config.max_file_size = dest_config["max_file_size"];
  }

  // Batch settings
  if (dest_config.contains("batch_size")) {
    config.batch_size = dest_config["batch_size"];
  }

  if (dest_config.contains("batch_timeout")) {
    config.batch_timeout = std::chrono::seconds{dest_config["batch_timeout"].get<int>()};
  }

  if (dest_config.contains("max_retries")) {
    config.max_retries = dest_config["max_retries"];
  }

  if (dest_config.contains("retry_delay")) {
    config.retry_delay = std::chrono::seconds{dest_config["retry_delay"].get<int>()};
  }

  // Filtering
  if (dest_config.contains("allowed_levels")) {
    config.allowed_levels = parseAllowedLevels(dest_config["allowed_levels"]);
  }

  if (dest_config.contains("allowed_components") &&
      dest_config["allowed_components"].is_array()) {
    for (const auto &component : dest_config["allowed_components"]) {
      if (component.is_string()) {
        config.allowed_components.insert(component.get<std::string>());
      } else {
        std::cerr << "Warning: Non-string component in allowed_components, skipping" << std::endl;
      }
    }
  }

  return config;
}

LogDestinationType
LogAggregationConfigLoader::parseDestinationType(const std::string &type_str) {
  if (type_str == "ELASTICSEARCH")
    return LogDestinationType::ELASTICSEARCH;
  if (type_str == "HTTP_ENDPOINT")
    return LogDestinationType::HTTP_ENDPOINT;
  if (type_str == "FILE")
    return LogDestinationType::FILE;
  if (type_str == "SYSLOG")
    return LogDestinationType::SYSLOG;
  if (type_str == "CLOUDWATCH")
    return LogDestinationType::CLOUDWATCH;
  if (type_str == "SPLUNK")
    return LogDestinationType::SPLUNK;

  throw std::runtime_error("Unknown destination type: " + type_str);
}

LogLevel
LogAggregationConfigLoader::parseLogLevel(const std::string &level_str) {
  if (level_str == "DEBUG")
    return LogLevel::DEBUG;
  if (level_str == "INFO")
    return LogLevel::INFO;
  if (level_str == "WARN")
    return LogLevel::WARN;
  if (level_str == "ERROR")
    return LogLevel::ERROR;
  if (level_str == "FATAL")
    return LogLevel::FATAL;

  throw std::runtime_error("Unknown log level: " + level_str);
}

std::unordered_set<LogLevel> LogAggregationConfigLoader::parseAllowedLevels(
    const nlohmann::json &levels_array) {
  std::unordered_set<LogLevel> levels;

  if (!levels_array.is_array()) {
    return levels;
  }

  for (const auto &level_json : levels_array) {
    if (level_json.is_string()) {
      try {
        levels.insert(parseLogLevel(level_json));
      } catch (const std::exception &e) {
        std::cerr << "Failed to parse log level: " << e.what() << std::endl;
      }
    }
  }

  return levels;
}