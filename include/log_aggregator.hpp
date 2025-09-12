#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "logger.hpp"
#include "nlohmann/json.hpp"

// Log shipping destination types
enum class LogDestinationType {
  ELASTICSEARCH,
  HTTP_ENDPOINT,
  FILE,
  SYSLOG,
  CLOUDWATCH,
  SPLUNK
};

// Configuration for log shipping destinations
struct LogDestinationConfig {
  LogDestinationType type;
  std::string name;
  bool enabled = true;

  // Common settings
  std::string endpoint;
  std::string auth_token;
  std::unordered_map<std::string, std::string> headers;

  // Elasticsearch specific
  std::string index_pattern = "logs-%Y.%m.%d";
  std::string pipeline;

  // File specific
  std::string file_path;
  bool rotate_files = true;
  size_t max_file_size = 100 * 1024 * 1024; // 100MB

  // Batch settings
  size_t batch_size = 100;
  std::chrono::seconds batch_timeout{30};
  size_t max_retries = 3;
  std::chrono::seconds retry_delay{5};

  // Filtering
  std::unordered_set<LogLevel> allowed_levels = {
      LogLevel::DEBUG, LogLevel::INFO, LogLevel::WARN, LogLevel::ERROR,
      LogLevel::FATAL};
  std::unordered_set<std::string> allowed_components;
};

// Structured log entry for aggregation
struct StructuredLogEntry {
  std::string timestamp;
  LogLevel level;
  std::string component;
  std::string message;
  std::string thread_id;
  std::string process_id;
  std::unordered_map<std::string, std::string> metadata;
  nlohmann::json structured_data;

  /**
   * @brief Serialize the structured log entry into a JSON object suitable for
   * shipping.
   *
   * Produces a JSON object containing the canonical fields used by downstream
   * sinks:
   * - "@timestamp": timestamp string
   * - "level": human-readable level (via levelToString)
   * - "component", "message", "thread_id", "process_id"
   *
   * The "metadata" field is included only if metadata is non-empty. The "data"
   * field is included only if structured_data is non-empty.
   *
   * @return nlohmann::json JSON representation of this StructuredLogEntry.
   */
  [[nodiscard]] nlohmann::json toJson() const {
    nlohmann::json json_entry;
    json_entry["@timestamp"] = timestamp;
    json_entry["level"] = levelToString(level);
    json_entry["component"] = component;
    json_entry["message"] = message;
    json_entry["thread_id"] = thread_id;
    json_entry["process_id"] = process_id;

    if (!metadata.empty()) {
      json_entry["metadata"] = metadata;
    }

    if (!structured_data.empty()) {
      json_entry["data"] = structured_data;
    }

    return json_entry;
  }

private:
  /**
   * @brief Convert a LogLevel enum value to its uppercase string
   * representation.
   *
   * Converts LogLevel::DEBUG/INFO/WARN/ERROR/FATAL to
   * "DEBUG"/"INFO"/"WARN"/"ERROR"/"FATAL".
   *
   * @param level Log level to convert.
   * @return std::string Uppercase textual name of the level; returns "UNKNOWN"
   * for unrecognized values.
   */
  std::string levelToString(LogLevel level) const {
    switch (level) {
    case LogLevel::DEBUG:
      return "DEBUG";
    case LogLevel::INFO:
      return "INFO";
    case LogLevel::WARN:
      return "WARN";
    case LogLevel::ERROR:
      return "ERROR";
    case LogLevel::FATAL:
      return "FATAL";
    default:
      return "UNKNOWN";
    }
  }
};

// RAII wrapper for CURL handles
class CurlHandle {
public:
  CurlHandle() : handle_(curl_easy_init()) {
    if (!handle_) {
      throw std::runtime_error("Failed to initialize CURL handle");
    }
  }

  CurlHandle(const CurlHandle &) = delete;
  CurlHandle &operator=(const CurlHandle &) = delete;

  CurlHandle(CurlHandle &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  CurlHandle &operator=(CurlHandle &&other) noexcept {
    if (this != &other) {
      if (handle_) {
        curl_easy_cleanup(handle_);
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  ~CurlHandle() {
    if (handle_) {
      curl_easy_cleanup(handle_);
    }
  }

  CURL *get() const { return handle_; }

private:
  CURL *handle_;
};

// Main log aggregator class
class LogAggregator {
public:
  explicit LogAggregator(const std::vector<LogDestinationConfig> &destinations);
  ~LogAggregator();

  // Initialize the aggregator
  bool initialize();

  // Shutdown the aggregator
  void shutdown();

  // Add a log entry for aggregation
  void addLogEntry(const StructuredLogEntry &entry);

  // Add destination configuration
  void addDestination(const LogDestinationConfig &config);

  // Remove destination
  void removeDestination(const std::string &name);

  // Get aggregator statistics
  struct AggregatorStats {
    std::atomic<uint64_t> total_entries_processed{0};
    std::atomic<uint64_t> entries_shipped{0};
    std::atomic<uint64_t> entries_failed{0};
    std::atomic<uint64_t> batches_sent{0};
    std::chrono::steady_clock::time_point start_time;
  };

  /**
   * @brief Access the aggregator's runtime statistics.
   *
   * Returns a const reference to the internal AggregatorStats structure which
   * contains atomic counters (total_entries_processed, entries_shipped,
   * entries_failed, batches_sent) and the start_time. The reference refers to
   * the aggregator's internal state and remains valid for the lifetime of the
   * LogAggregator instance.
   *
   * @return const AggregatorStats& Reference to the current aggregator
   * statistics.
   */
  const AggregatorStats &getStats() const { return stats_; }

private:
  // Worker thread for processing log batches
  void processingWorker();

  // Ship logs to a specific destination
  bool shipToDestination(const LogDestinationConfig &dest,
                         const std::vector<StructuredLogEntry> &batch);

  // Ship to Elasticsearch
  bool shipToElasticsearch(const LogDestinationConfig &dest,
                           const std::vector<StructuredLogEntry> &batch);

  // Ship to HTTP endpoint
  bool shipToHttpEndpoint(const LogDestinationConfig &dest,
                          const std::vector<StructuredLogEntry> &batch);

  // Ship to file
  bool shipToFile(const LogDestinationConfig &dest,
                  const std::vector<StructuredLogEntry> &batch);

  // Ship to syslog
  bool shipToSyslog(const LogDestinationConfig &dest,
                    const std::vector<StructuredLogEntry> &batch);

  // Ship to AWS CloudWatch
  bool shipToCloudWatch(const LogDestinationConfig &dest,
                        const std::vector<StructuredLogEntry> &batch);

  // Ship to Splunk
  bool shipToSplunk(const LogDestinationConfig &dest,
                    const std::vector<StructuredLogEntry> &batch);

  // HTTP request helper
  bool
  makeHttpRequest(const std::string &url, const std::string &method,
                  const std::string &data,
                  const std::unordered_map<std::string, std::string> &headers,
                  std::string &response);

  // File rotation helper
  void rotateLogFile(const std::string &file_path);

  // Filter entries based on destination config
  bool shouldShipEntry(const LogDestinationConfig &dest,
                       const StructuredLogEntry &entry);

  // Generate index name for Elasticsearch
  std::string generateIndexName(const std::string &pattern);

  // Member variables
  std::vector<LogDestinationConfig> destinations_;
  std::queue<StructuredLogEntry> log_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::thread processing_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> shutdown_requested_{false};

  AggregatorStats stats_;

  // CURL handle for HTTP requests
  // CURL handle for HTTP requests (protected by curl_mutex_)
  // CURL handle for HTTP requests (protected by curl_mutex_)
  CurlHandle curl_handle_;
  mutable std::mutex curl_mutex_;

  // File streams for file destinations
  std::unordered_map<std::string, std::ofstream> file_streams_;
  std::mutex file_mutex_;
};

// Enhanced logger with structured logging support
class StructuredLogger {
public:
  static StructuredLogger &getInstance();

  // Configure structured logging
  void
  configureStructuredLogging(bool enable_json = true,
                             const std::string &default_component = "system");

  // Log with structured data
  void logStructured(
      LogLevel level, const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string> &metadata = {},
      const nlohmann::json &structured_data = nlohmann::json());

  // Log with context (component, operation, etc.)
  void logWithContext(
      LogLevel level, const std::string &component,
      const std::string &operation, const std::string &message,
      const std::unordered_map<std::string, std::string> &context = {});

  // Set default component for this logger instance
  void setDefaultComponent(const std::string &component);

  // Enable/disable aggregation
  void setAggregationEnabled(bool enabled);
  bool isAggregationEnabled() const;

private:
  StructuredLogger();
  ~StructuredLogger();

  // Create structured log entry
  StructuredLogEntry
  createLogEntry(LogLevel level, const std::string &component,
                 const std::string &message,
                 const std::unordered_map<std::string, std::string> &metadata,
                 const nlohmann::json &structured_data);

  std::string default_component_;
  bool json_format_enabled_;
  bool aggregation_enabled_;
  std::unique_ptr<LogAggregator> aggregator_;
};

// Global functions for easy logging
namespace logging {

// Structured logging functions
void logStructured(
    LogLevel level, const std::string &component, const std::string &message,
    const std::unordered_map<std::string, std::string> &metadata = {},
    const nlohmann::json &structured_data = nlohmann::json());

void logWithContext(
    LogLevel level, const std::string &component, const std::string &operation,
    const std::string &message,
    const std::unordered_map<std::string, std::string> &context = {});

// Component-specific loggers
void logDatabase(
    LogLevel level, const std::string &operation, const std::string &message,
    const std::unordered_map<std::string, std::string> &context = {});

void logApi(LogLevel level, const std::string &operation,
            const std::string &message,
            const std::unordered_map<std::string, std::string> &context = {});

void logJob(LogLevel level, const std::string &job_id,
            const std::string &operation, const std::string &message,
            const std::unordered_map<std::string, std::string> &context = {});

void logSecurity(
    LogLevel level, const std::string &event_type, const std::string &message,
    const std::unordered_map<std::string, std::string> &context = {});

} // namespace logging