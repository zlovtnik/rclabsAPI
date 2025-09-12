#include "log_aggregator.hpp"
#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <syslog.h>
#include <thread>
#include <unistd.h>

#include "logger.hpp"

// CURL callback for writing response data
static size_t writeCallback(void *contents, size_t size, size_t nmemb,
                            void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

// LogAggregator implementation
LogAggregator::LogAggregator(
    const std::vector<LogDestinationConfig> &destinations)
    : destinations_(destinations), stats_() {
  stats_.start_time = std::chrono::steady_clock::now();
}

LogAggregator::~LogAggregator() { shutdown(); }

bool LogAggregator::initialize() {
  if (running_) {
    return true; // Already initialized
  }

  // Initialize CURL global state
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // CURL handle is automatically initialized by CurlHandle RAII wrapper

  // Start processing thread
  running_ = true;
  processing_thread_ = std::thread(&LogAggregator::processingWorker, this);

  return true;
}

void LogAggregator::shutdown() {
  if (!running_) {
    return;
  }

  shutdown_requested_ = true;
  queue_cv_.notify_all();

  if (processing_thread_.joinable()) {
    processing_thread_.join();
  }

  // Close all file streams
  {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (auto &[name, stream] : file_streams_) {
      if (stream.is_open()) {
        stream.close();
      }
    }
    file_streams_.clear();
  }

  // Cleanup CURL
  // CurlHandle automatically cleans up the CURL handle
  curl_global_cleanup();
  running_ = false;
}

void LogAggregator::addLogEntry(const StructuredLogEntry &entry) {
  if (!running_ || shutdown_requested_) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    log_queue_.push(entry);
    stats_.total_entries_processed++;
  }

  queue_cv_.notify_one();
}

void LogAggregator::addDestination(const LogDestinationConfig &config) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  destinations_.push_back(config);
}

void LogAggregator::removeDestination(const std::string &name) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  destinations_.erase(std::remove_if(destinations_.begin(), destinations_.end(),
                                     [&name](const LogDestinationConfig &dest) {
                                       return dest.name == name;
                                     }),
                      destinations_.end());
}

void LogAggregator::processingWorker() {
  std::vector<StructuredLogEntry> current_batch;
  auto last_batch_time = std::chrono::steady_clock::now();

  while (!shutdown_requested_ || !log_queue_.empty()) {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);

      // Wait for entries or timeout
      if (log_queue_.empty()) {
        if (shutdown_requested_) {
          break;
        }

        auto timeout = std::chrono::seconds(1);
        queue_cv_.wait_for(lock, timeout);
        continue;
      }

      // Collect entries for batch processing
      auto now = std::chrono::steady_clock::now();
      bool should_process_batch = false;

      // Check if we should process based on time or size
      for (auto &dest : destinations_) {
        if (!dest.enabled)
          continue;

        if (current_batch.size() >= dest.batch_size ||
            (now - last_batch_time) >= dest.batch_timeout) {
          should_process_batch = true;
          break;
        }
      }

      if (!should_process_batch && !shutdown_requested_) {
        continue;
      }

      // Move entries to current batch
      while (!log_queue_.empty() &&
             current_batch.size() < 1000) { // Max batch size
        current_batch.push_back(std::move(log_queue_.front()));
        log_queue_.pop();
      }
    }

    if (!current_batch.empty()) {
      // Process batch for each destination
      for (const auto &dest : destinations_) {
        if (!dest.enabled)
          continue;

        // Filter entries for this destination
        std::vector<StructuredLogEntry> filtered_batch;
        for (const auto &entry : current_batch) {
          if (shouldShipEntry(dest, entry)) {
            filtered_batch.push_back(entry);
          }
        }

        if (!filtered_batch.empty()) {
          if (shipToDestination(dest, filtered_batch)) {
            stats_.entries_shipped += filtered_batch.size();
            stats_.batches_sent++;
          } else {
            stats_.entries_failed += filtered_batch.size();
          }
        }
      }

      current_batch.clear();
      last_batch_time = std::chrono::steady_clock::now();
    }
  }
}

bool LogAggregator::shipToDestination(
    const LogDestinationConfig &dest,
    const std::vector<StructuredLogEntry> &batch) {
  switch (dest.type) {
  case LogDestinationType::ELASTICSEARCH:
    return shipToElasticsearch(dest, batch);
  case LogDestinationType::HTTP_ENDPOINT:
    return shipToHttpEndpoint(dest, batch);
  case LogDestinationType::FILE:
    return shipToFile(dest, batch);
  case LogDestinationType::SYSLOG:
    return shipToSyslog(dest, batch);
  case LogDestinationType::CLOUDWATCH:
    return shipToCloudWatch(dest, batch);
  case LogDestinationType::SPLUNK:
    return shipToSplunk(dest, batch);
  default:
    return false;
  }
}

bool LogAggregator::shipToElasticsearch(
    const LogDestinationConfig &dest,
    const std::vector<StructuredLogEntry> &batch) {
  if (!curl_handle_.get())
    return false;

  // Create bulk request body
  std::string bulk_data;
  std::string index_name = generateIndexName(dest.index_pattern);

  for (const auto &entry : batch) {
    // Elasticsearch bulk API format
    nlohmann::json action = {{"index", {{"_index", index_name}}}};
    if (!dest.pipeline.empty()) {
      action["index"]["pipeline"] = dest.pipeline;
    }

    bulk_data += action.dump() + "\n";
    bulk_data += entry.toJson().dump() + "\n";
  }

  std::string url = dest.endpoint + "/_bulk";
  std::string response;

  std::unordered_map<std::string, std::string> headers = dest.headers;
  headers["Content-Type"] = "application/x-ndjson";

  return makeHttpRequest(url, "POST", bulk_data, headers, response);
}

bool LogAggregator::shipToHttpEndpoint(
    const LogDestinationConfig &dest,
    const std::vector<StructuredLogEntry> &batch) {
  if (!curl_handle_.get())
    return false;

  // Create JSON array of log entries
  nlohmann::json json_batch = nlohmann::json::array();
  for (const auto &entry : batch) {
    json_batch.push_back(entry.toJson());
  }

  std::string data = json_batch.dump();
  std::string response;

  return makeHttpRequest(dest.endpoint, "POST", data, dest.headers, response);
}

bool LogAggregator::shipToFile(const LogDestinationConfig &dest,
                               const std::vector<StructuredLogEntry> &batch) {
  std::lock_guard<std::mutex> lock(file_mutex_);

  // Get or create file stream
  auto &stream = file_streams_[dest.name];
  if (!stream.is_open()) {
    std::filesystem::path file_path(dest.file_path);
    std::filesystem::create_directories(file_path.parent_path());

    stream.open(dest.file_path, std::ios::app);
    if (!stream.is_open()) {
      return false;
    }
  }

  // Check if file needs rotation
  if (dest.rotate_files) {
    // Flush any buffered data to ensure accurate size calculation
    stream.flush();

    // Get current file size including any buffered data
    std::streampos current_pos = stream.tellp();
    if (current_pos >= static_cast<std::streampos>(dest.max_file_size)) {
      rotateLogFile(dest.file_path);
      stream.close();
      stream.open(dest.file_path, std::ios::app);
      if (!stream.is_open()) {
        return false;
      }
    }
  }

  // Write batch to file
  for (const auto &entry : batch) {
    stream << entry.toJson().dump() << std::endl;
  }

  stream.flush();
  return true;
}

bool LogAggregator::shipToSyslog(const LogDestinationConfig &dest,
                                 const std::vector<StructuredLogEntry> &batch) {
  // Open syslog connection
  openlog("etlplus", LOG_PID | LOG_CONS, LOG_USER);

  for (const auto &entry : batch) {
    int syslog_level;
    switch (entry.level) {
    case LogLevel::DEBUG:
      syslog_level = LOG_DEBUG;
      break;
    case LogLevel::INFO:
      syslog_level = LOG_INFO;
      break;
    case LogLevel::WARN:
      syslog_level = LOG_WARNING;
      break;
    case LogLevel::ERROR:
      syslog_level = LOG_ERR;
      break;
    case LogLevel::FATAL:
      syslog_level = LOG_CRIT;
      break;
    default:
      syslog_level = LOG_INFO;
      break;
    }

    std::string message = "[" + entry.component + "] " + entry.message;
    syslog(syslog_level, "%s", message.c_str());
  }

  closelog();
  return true;
}

bool LogAggregator::shipToCloudWatch(
    const LogDestinationConfig &dest,
    const std::vector<StructuredLogEntry> &batch) {
  // Note: This is a simplified implementation
  // In a real implementation, you'd use AWS SDK for CloudWatch Logs
  if (!curl_handle_.get())
    return false;

  nlohmann::json cloudwatch_batch = {{"logGroupName", "etlplus-logs"},
                                     {"logStreamName", "application-logs"},
                                     {"logEvents", nlohmann::json::array()}};

  for (const auto &entry : batch) {
    nlohmann::json log_event = {
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count()},
        {"message", entry.toJson().dump()}};
    cloudwatch_batch["logEvents"].push_back(log_event);
  }

  std::string data = cloudwatch_batch.dump();
  std::string response;

  std::unordered_map<std::string, std::string> headers = dest.headers;
  headers["X-Amz-Target"] = "Logs_20140328.PutLogEvents";
  headers["Content-Type"] = "application/x-amz-json-1.1";

  return makeHttpRequest(dest.endpoint, "POST", data, headers, response);
}

bool LogAggregator::shipToSplunk(const LogDestinationConfig &dest,
                                 const std::vector<StructuredLogEntry> &batch) {
  if (!curl_handle_.get())
    return false;

  // Create JSON array for Splunk HEC
  nlohmann::json splunk_batch = nlohmann::json::array();
  for (const auto &entry : batch) {
    nlohmann::json splunk_event = {{"event", entry.toJson()},
                                   {"sourcetype", "etlplus:log"},
                                   {"index", "main"}};
    splunk_batch.push_back(splunk_event);
  }

  std::string data = splunk_batch.dump();
  std::string response;

  std::unordered_map<std::string, std::string> headers = dest.headers;
  headers["Content-Type"] = "application/json";

  return makeHttpRequest(dest.endpoint, "POST", data, headers, response);
}

bool LogAggregator::makeHttpRequest(
    const std::string &url, const std::string &method, const std::string &data,
    const std::unordered_map<std::string, std::string> &headers,
    std::string &response) {
  if (!curl_handle_.get())
    return false;

  curl_easy_reset(curl_handle_.get());

  // Set URL
  curl_easy_setopt(curl_handle_.get(), CURLOPT_URL, url.c_str());

  // Set method
  if (method == "POST") {
    curl_easy_setopt(curl_handle_.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle_.get(), CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl_handle_.get(), CURLOPT_POSTFIELDSIZE, data.length());
  }

  // Set headers
  struct curl_slist *header_list = nullptr;
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_guard(
      nullptr, curl_slist_free_all);
  for (const auto &[key, value] : headers) {
    std::string header = key + ": " + value;
    header_list = curl_slist_append(header_list, header.c_str());
  }
  header_guard.reset(header_list);
  curl_easy_setopt(curl_handle_.get(), CURLOPT_HTTPHEADER, header_list);

  // Set response callback
  curl_easy_setopt(curl_handle_.get(), CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl_handle_.get(), CURLOPT_WRITEDATA, &response);

  // Set timeouts
  curl_easy_setopt(curl_handle_.get(), CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl_handle_.get(), CURLOPT_CONNECTTIMEOUT, 10L);

  // Perform request
  CURLcode res = curl_easy_perform(curl_handle_.get());

  // Cleanup
  if (header_list) {
    curl_slist_free_all(header_list);
  }

  return (res == CURLE_OK);
}

void LogAggregator::rotateLogFile(const std::string &file_path) {
  std::filesystem::path path(file_path);
  std::string stem = path.stem().string();
  std::string extension = path.extension().string();

  // Find next available rotation number
  int rotation_num = 1;
  std::string rotated_path;

  do {
    rotated_path = path.parent_path() /
                   (stem + "." + std::to_string(rotation_num) + extension);
    rotation_num++;
  } while (std::filesystem::exists(rotated_path) && rotation_num < 100);

  // Rotate the file
  std::filesystem::rename(file_path, rotated_path);
}

bool LogAggregator::shouldShipEntry(const LogDestinationConfig &dest,
                                    const StructuredLogEntry &entry) {
  // Check log level
  if (dest.allowed_levels.find(entry.level) == dest.allowed_levels.end()) {
    return false;
  }

  // Check component filter
  if (!dest.allowed_components.empty() &&
      dest.allowed_components.find(entry.component) ==
          dest.allowed_components.end()) {
    return false;
  }

  return true;
}

std::string LogAggregator::generateIndexName(const std::string &pattern) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};

#ifdef _WIN32
  localtime_s(&tm_buf, &time_t);
#else
  localtime_r(&time_t, &tm_buf);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm_buf, pattern.c_str());
  return oss.str();
}

// StructuredLogger implementation
StructuredLogger &StructuredLogger::getInstance() {
  static StructuredLogger instance;
  return instance;
}

StructuredLogger::StructuredLogger()
    : default_component_("system"), json_format_enabled_(false),
      aggregation_enabled_(false) {}

StructuredLogger::~StructuredLogger() {
  if (aggregator_) {
    aggregator_->shutdown();
  }
}

void StructuredLogger::configureStructuredLogging(
    bool enable_json, const std::string &default_component) {
  json_format_enabled_ = enable_json;
  default_component_ = default_component;
}

void StructuredLogger::logStructured(
    LogLevel level, const std::string &component, const std::string &message,
    const std::unordered_map<std::string, std::string> &metadata,
    const nlohmann::json &structured_data) {
  auto entry =
      createLogEntry(level, component, message, metadata, structured_data);

  // Log to standard logger
  Logger::getInstance().log(level, component, message);

  // Send to aggregator if enabled
  if (aggregation_enabled_ && aggregator_) {
    aggregator_->addLogEntry(entry);
  }
}

void StructuredLogger::logWithContext(
    LogLevel level, const std::string &component, const std::string &operation,
    const std::string &message,
    const std::unordered_map<std::string, std::string> &context) {
  std::unordered_map<std::string, std::string> metadata = context;
  metadata["operation"] = operation;

  nlohmann::json structured_data;
  structured_data["operation"] = operation;
  for (const auto &[key, value] : context) {
    structured_data[key] = value;
  }

  logStructured(level, component, message, metadata, structured_data);
}

void StructuredLogger::setDefaultComponent(const std::string &component) {
  default_component_ = component;
}

void StructuredLogger::setAggregationEnabled(bool enabled) {
  aggregation_enabled_ = enabled;

  if (enabled && !aggregator_) {
    // Initialize aggregator with default destinations
    std::vector<LogDestinationConfig> destinations;

    // Add file destination as default
    LogDestinationConfig file_dest;
    file_dest.type = LogDestinationType::FILE;
    file_dest.name = "default_file";
    file_dest.file_path = "logs/aggregated.log";
    destinations.push_back(file_dest);

    aggregator_ = std::make_unique<LogAggregator>(destinations);
    aggregator_->initialize();
  } else if (!enabled && aggregator_) {
    aggregator_->shutdown();
    aggregator_.reset();
  }
}

bool StructuredLogger::isAggregationEnabled() const {
  return aggregation_enabled_;
}

StructuredLogEntry StructuredLogger::createLogEntry(
    LogLevel level, const std::string &component, const std::string &message,
    const std::unordered_map<std::string, std::string> &metadata,
    const nlohmann::json &structured_data) {
  StructuredLogEntry entry;

  // Get current timestamp
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  std::tm tm_buf{};
#ifdef _WIN32
  localtime_s(&tm_buf, &time_t);
#else
  localtime_r(&time_t, &tm_buf);
#endif

  std::ostringstream timestamp_oss;
  timestamp_oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << "."
                << std::setfill('0') << std::setw(3) << ms.count() << "Z";
  entry.timestamp = timestamp_oss.str();

  entry.level = level;
  entry.component = component.empty() ? default_component_ : component;
  entry.message = message;
  entry.metadata = metadata;
  entry.structured_data = structured_data;

  // Get thread and process IDs
  std::ostringstream thread_oss;
  thread_oss << std::this_thread::get_id();
  entry.thread_id = thread_oss.str();

  entry.process_id = std::to_string(getpid());

  return entry;
}

// Global logging functions
namespace logging {

void logStructured(LogLevel level, const std::string &component,
                   const std::string &message,
                   const std::unordered_map<std::string, std::string> &metadata,
                   const nlohmann::json &structured_data) {
  StructuredLogger::getInstance().logStructured(level, component, message,
                                                metadata, structured_data);
}

void logWithContext(
    LogLevel level, const std::string &component, const std::string &operation,
    const std::string &message,
    const std::unordered_map<std::string, std::string> &context) {
  StructuredLogger::getInstance().logWithContext(level, component, operation,
                                                 message, context);
}

void logDatabase(LogLevel level, const std::string &operation,
                 const std::string &message,
                 const std::unordered_map<std::string, std::string> &context) {
  logWithContext(level, "database", operation, message, context);
}

void logApi(LogLevel level, const std::string &operation,
            const std::string &message,
            const std::unordered_map<std::string, std::string> &context) {
  logWithContext(level, "api", operation, message, context);
}

void logJob(LogLevel level, const std::string &job_id,
            const std::string &operation, const std::string &message,
            const std::unordered_map<std::string, std::string> &context) {
  std::unordered_map<std::string, std::string> job_context = context;
  job_context["job_id"] = job_id;
  logWithContext(level, "job", operation, message, job_context);
}

void logSecurity(LogLevel level, const std::string &event_type,
                 const std::string &message,
                 const std::unordered_map<std::string, std::string> &context) {
  std::unordered_map<std::string, std::string> security_context = context;
  security_context["event_type"] = event_type;
  security_context["security_event"] = "true";
  logWithContext(level, "security", "security_event", message,
                 security_context);
}

} // namespace logging