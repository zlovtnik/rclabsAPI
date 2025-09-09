#pragma once

#include "transparent_string_hash.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations for real-time log streaming
class WebSocketManager;
struct LogMessage;

// Forward declarations for shared types from log_file_manager.hpp
struct LogQueryParams;
struct HistoricalLogEntry;
struct LogFileInfo;

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

enum class LogFormat { TEXT = 0, JSON = 1 };

// Enhanced log rotation and retention configuration
struct LogRotationConfig {
  bool enableRotation = true;
  size_t maxFileSize = 10 * 1024 * 1024; // 10MB
  int maxBackupFiles = 5;

  // Enhanced retention policies
  bool enableTimeBasedRotation = false;
  std::chrono::hours rotationInterval = std::chrono::hours(24);
  std::chrono::hours retentionPeriod = std::chrono::hours(24 * 7); // 7 days

  // Compression settings
  bool compressOldLogs = false;
  std::string compressionFormat = "gzip"; // gzip, zip, none

  // Cleanup settings
  bool enableAutoCleanup = true;
  std::chrono::minutes cleanupInterval = std::chrono::minutes(60);
};

struct LogConfig {
  LogLevel level = LogLevel::INFO;
  LogFormat format = LogFormat::TEXT;
  bool consoleOutput = true;
  bool fileOutput = false;
  bool asyncLogging = false;
  std::string logFile = "logs/etlplus.log";
  size_t maxFileSize = 10 * 1024 * 1024; // 10MB
  int maxBackupFiles = 5;
  bool enableRotation = true;
  std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>
      componentFilter; // Empty = all components
  bool includeMetrics = false;
  int flushInterval = 1000; // milliseconds

  // Real-time streaming configuration
  bool enableRealTimeStreaming = false;
  size_t streamingQueueSize = 1000;
  bool streamAllLevels = true;
  std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>
      streamingJobFilter; // Empty = all jobs

  // Enhanced rotation and retention
  LogRotationConfig rotation;

  // Historical access configuration
  bool enableHistoricalAccess = true;
  std::string archiveDirectory = "logs/archive";
  size_t maxQueryResults = 10000;
  bool enableLogIndexing = true;
};

struct LogMetrics {
  std::atomic<uint64_t> totalMessages{0};
  std::atomic<uint64_t> errorCount{0};
  std::atomic<uint64_t> warningCount{0};
  std::atomic<uint64_t> droppedMessages{0};
  std::chrono::steady_clock::time_point startTime;

  LogMetrics() : startTime(std::chrono::steady_clock::now()) {}

  // Copy constructor - can't copy atomics directly, so copy their values
  LogMetrics(const LogMetrics &other)
      : totalMessages(other.totalMessages.load()),
        errorCount(other.errorCount.load()),
        warningCount(other.warningCount.load()),
        droppedMessages(other.droppedMessages.load()),
        startTime(other.startTime) {}

  // Assignment operator
  LogMetrics &operator=(const LogMetrics &other) {
    if (this != &other) {
      totalMessages.store(other.totalMessages.load());
      errorCount.store(other.errorCount.load());
      warningCount.store(other.warningCount.load());
      droppedMessages.store(other.droppedMessages.load());
      startTime = other.startTime;
    }
    return *this;
  }
};

class Logger {
public:
  static Logger &getInstance();

  // Configuration methods
  void configure(const LogConfig &config);
  void setLogLevel(LogLevel level);
  void setLogFormat(LogFormat format);
  void setLogFile(const std::string &filename);
  void enableConsoleOutput(bool enable);
  void enableFileOutput(bool enable);
  void enableAsyncLogging(bool enable);
  void setComponentFilter(
      const std::unordered_set<std::string, TransparentStringHash,
                               std::equal_to<>> &components);
  void enableRotation(bool enable, size_t maxFileSize = 10 * 1024 * 1024,
                      int maxBackupFiles = 5);

  // Logging methods
  void
  log(LogLevel level, const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  void debug(
      const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  void
  info(const std::string &component, const std::string &message,
       const std::unordered_map<std::string, std::string, TransparentStringHash,
                                std::equal_to<>> &context = {});
  void
  warn(const std::string &component, const std::string &message,
       const std::unordered_map<std::string, std::string, TransparentStringHash,
                                std::equal_to<>> &context = {});
  void error(
      const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  void fatal(
      const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});

  // Metrics and performance logging
  void logMetric(const std::string &name, double value,
                 const std::string &unit = "");
  void logPerformance(
      const std::string &operation, double durationMs,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  LogMetrics getMetrics() const;

  // Control methods
  void flush();
  void shutdown();

  // Real-time log streaming methods
  void setWebSocketManager(std::shared_ptr<WebSocketManager> wsManager);
  void enableRealTimeStreaming(bool enable);
  void setStreamingJobFilter(
      const std::unordered_set<std::string, TransparentStringHash,
                               std::equal_to<>> &jobIds);
  void addStreamingJobFilter(const std::string &jobId);
  void removeStreamingJobFilter(const std::string &jobId);
  void clearStreamingJobFilter();

  // Job-specific logging methods
  void logForJob(
      LogLevel level, const std::string &component, const std::string &message,
      const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  void debugForJob(
      const std::string &component, const std::string &message,
      const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  void infoForJob(
      const std::string &component, const std::string &message,
      const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  void warnForJob(
      const std::string &component, const std::string &message,
      const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  void errorForJob(
      const std::string &component, const std::string &message,
      const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});
  void fatalForJob(
      const std::string &component, const std::string &message,
      const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {});

  // Historical log access methods
  void enableHistoricalAccess(bool enable);
  bool isHistoricalAccessEnabled() const;
  void setArchiveDirectory(const std::string &directory);
  std::string getArchiveDirectory() const;
  void setMaxQueryResults(size_t maxResults);
  size_t getMaxQueryResults() const;
  void enableLogIndexing(bool enable);
  bool isLogIndexingEnabled() const;

  // Log querying methods
  std::vector<HistoricalLogEntry> queryLogs(const LogQueryParams &params);
  std::vector<LogFileInfo> listLogFiles(bool includeArchived = false);
  bool archiveLogFile(const std::string &filename);
  bool restoreLogFile(const std::string &filename);
  bool deleteLogFile(const std::string &filename);
  bool compressLogFile(const std::string &filename,
                       const std::string &format = "gzip");
  bool decompressLogFile(const std::string &filename);

private:
  Logger() = default;
  ~Logger();

  // Configuration
  LogConfig config_;
  mutable std::mutex configMutex_;

  // File handling
  std::ofstream fileStream_;
  std::string currentLogFile_;
  size_t currentFileSize_ = 0;
  mutable std::mutex fileMutex_;

  // Async logging
  std::queue<std::string> messageQueue_;
  std::thread asyncThread_;
  std::condition_variable asyncCondition_;
  std::mutex asyncMutex_;
  std::atomic<bool> stopAsync_{false};
  std::atomic<bool> asyncStarted_{false};

  // Metrics
  LogMetrics metrics_;

  // Real-time streaming
  std::shared_ptr<WebSocketManager> wsManager_;
  std::queue<std::shared_ptr<LogMessage>> streamingQueue_;
  std::thread streamingThread_;
  std::condition_variable streamingCondition_;
  std::mutex streamingMutex_;
  std::atomic<bool> stopStreaming_{false};
  std::atomic<bool> streamingStarted_{false};

  // Helper methods
  std::string formatTimestamp();
  std::string levelToString(LogLevel level);
  std::string formatMessage(
      LogLevel level, const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context);
  std::string formatTextMessage(
      LogLevel level, const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context);
  std::string formatJsonMessage(
      LogLevel level, const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context);
  void writeLog(const std::string &formattedMessage);
  void writeLogSync(const std::string &formattedMessage);
  void writeLogAsync(const std::string &formattedMessage);
  void asyncWorker();
  void rotateLogFile();
  bool shouldLog(LogLevel level, const std::string &component) const;
  std::string escapeJson(const std::string &str);

  // Real-time streaming helpers
  void streamingWorker();
  void broadcastLogMessage(const std::shared_ptr<LogMessage> &logMsg);
  bool shouldStreamLog(LogLevel level, const std::string &jobId);
  std::shared_ptr<LogMessage> createLogMessage(
      LogLevel level, const std::string &component, const std::string &message,
      const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context);

  // Historical log access helpers
  mutable std::mutex indexMutex_;
  void archiveOldLogs();
  void cleanupArchivedLogs();
  void indexLogFile(const std::string &filename);
  void removeLogFileIndex(const std::string &filename);
  std::vector<HistoricalLogEntry> searchLogs(const std::string &query,
                                             const std::string &jobId = "");
  void sortLogEntries(std::vector<HistoricalLogEntry> &entries,
                      bool descending);

  // Log parsing helpers
  std::optional<HistoricalLogEntry> parseLogLine(const std::string &line,
                                                 const std::string &filename,
                                                 size_t lineNumber);
  std::optional<HistoricalLogEntry> parseTextLogLine(const std::string &line,
                                                     HistoricalLogEntry &entry);
  std::optional<HistoricalLogEntry> parseJsonLogLine(const std::string &line,
                                                     HistoricalLogEntry &entry);
  std::chrono::system_clock::time_point
  parseTimestamp(const std::string &timestampStr);
  LogLevel stringToLogLevel(const std::string &levelStr);
  void
  parseContextString(const std::string &contextStr,
                     std::unordered_map<std::string, std::string> &context);
  void parseJsonContext(const std::string &contextStr,
                        std::unordered_map<std::string, std::string> &context);
  bool matchesQuery(const HistoricalLogEntry &entry,
                    const LogQueryParams &params);
};

// Standard logging macros (backward compatible)
#define LOG_DEBUG(component, message, ...)                                     \
  Logger::getInstance().debug(component, message, ##__VA_ARGS__)
#define LOG_INFO(component, message, ...)                                      \
  Logger::getInstance().info(component, message, ##__VA_ARGS__)
#define LOG_WARN(component, message, ...)                                      \
  Logger::getInstance().warn(component, message, ##__VA_ARGS__)
#define LOG_ERROR(component, message, ...)                                     \
  Logger::getInstance().error(component, message, ##__VA_ARGS__)
#define LOG_FATAL(component, message, ...)                                     \
  Logger::getInstance().fatal(component, message, ##__VA_ARGS__)

// Job-specific logging macros
#define LOG_DEBUG_JOB(component, message, jobId, ...)                          \
  Logger::getInstance().debugForJob(component, message, jobId, ##__VA_ARGS__)
#define LOG_INFO_JOB(component, message, jobId, ...)                           \
  Logger::getInstance().infoForJob(component, message, jobId, ##__VA_ARGS__)
#define LOG_WARN_JOB(component, message, jobId, ...)                           \
  Logger::getInstance().warnForJob(component, message, jobId, ##__VA_ARGS__)
#define LOG_ERROR_JOB(component, message, jobId, ...)                          \
  Logger::getInstance().errorForJob(component, message, jobId, ##__VA_ARGS__)
#define LOG_FATAL_JOB(component, message, jobId, ...)                          \
  Logger::getInstance().fatalForJob(component, message, jobId, ##__VA_ARGS__)

// Include the new ComponentLogger template system
#include "component_logger.hpp"

// Backward compatibility - redirect old macros to new ComponentLogger system
// Note: These are deprecated and should be replaced with ComponentLogger
// template calls

#define CONFIG_LOG_DEBUG(message, ...)                                         \
  etl::ConfigLogger::debug(message, ##__VA_ARGS__)
#define CONFIG_LOG_INFO(message, ...)                                          \
  etl::ConfigLogger::info(message, ##__VA_ARGS__)
#define CONFIG_LOG_WARN(message, ...)                                          \
  etl::ConfigLogger::warn(message, ##__VA_ARGS__)
#define CONFIG_LOG_ERROR(message, ...)                                         \
  etl::ConfigLogger::error(message, ##__VA_ARGS__)

#define DB_LOG_DEBUG(message, ...)                                             \
  etl::DatabaseLogger::debug(message, ##__VA_ARGS__)
#define DB_LOG_INFO(message, ...)                                              \
  etl::DatabaseLogger::info(message, ##__VA_ARGS__)
#define DB_LOG_WARN(message, ...)                                              \
  etl::DatabaseLogger::warn(message, ##__VA_ARGS__)
#define DB_LOG_ERROR(message, ...)                                             \
  etl::DatabaseLogger::error(message, ##__VA_ARGS__)

#define ETL_LOG_DEBUG(message, ...)                                            \
  etl::ETLJobLogger::debug(message, ##__VA_ARGS__)
#define ETL_LOG_INFO(message, ...)                                             \
  etl::ETLJobLogger::info(message, ##__VA_ARGS__)
#define ETL_LOG_WARN(message, ...)                                             \
  etl::ETLJobLogger::warn(message, ##__VA_ARGS__)
#define ETL_LOG_ERROR(message, ...)                                            \
  etl::ETLJobLogger::error(message, ##__VA_ARGS__)

#define ETL_LOG_DEBUG_JOB(message, jobId, ...)                                 \
  etl::ETLJobLogger::debugJob(message, jobId, ##__VA_ARGS__)
#define ETL_LOG_INFO_JOB(message, jobId, ...)                                  \
  etl::ETLJobLogger::infoJob(message, jobId, ##__VA_ARGS__)
#define ETL_LOG_WARN_JOB(message, jobId, ...)                                  \
  etl::ETLJobLogger::warnJob(message, jobId, ##__VA_ARGS__)
#define ETL_LOG_ERROR_JOB(message, jobId, ...)                                 \
  etl::ETLJobLogger::errorJob(message, jobId, ##__VA_ARGS__)

#define WS_LOG_DEBUG(message, ...)                                             \
  etl::WebSocketLogger::debug(message, ##__VA_ARGS__)
#define WS_LOG_INFO(message, ...)                                              \
  etl::WebSocketLogger::info(message, ##__VA_ARGS__)
#define WS_LOG_WARN(message, ...)                                              \
  etl::WebSocketLogger::warn(message, ##__VA_ARGS__)
#define WS_LOG_ERROR(message, ...)                                             \
  etl::WebSocketLogger::error(message, ##__VA_ARGS__)

#define AUTH_LOG_DEBUG(message, ...)                                           \
  etl::AuthLogger::debug(message, ##__VA_ARGS__)
#define AUTH_LOG_INFO(message, ...)                                            \
  etl::AuthLogger::info(message, ##__VA_ARGS__)
#define AUTH_LOG_WARN(message, ...)                                            \
  etl::AuthLogger::warn(message, ##__VA_ARGS__)
#define AUTH_LOG_ERROR(message, ...)                                           \
  etl::AuthLogger::error(message, ##__VA_ARGS__)

#define HTTP_LOG_DEBUG(message, ...)                                           \
  etl::HttpLogger::debug(message, ##__VA_ARGS__)
#define HTTP_LOG_INFO(message, ...)                                            \
  etl::HttpLogger::info(message, ##__VA_ARGS__)
#define HTTP_LOG_WARN(message, ...)                                            \
  etl::HttpLogger::warn(message, ##__VA_ARGS__)
#define HTTP_LOG_ERROR(message, ...)                                           \
  etl::HttpLogger::error(message, ##__VA_ARGS__)

#define REQ_LOG_DEBUG(message, ...)                                            \
  etl::ComponentLogger<etl::RequestHandler>::debug(message, ##__VA_ARGS__)
#define REQ_LOG_INFO(message, ...)                                             \
  etl::ComponentLogger<etl::RequestHandler>::info(message, ##__VA_ARGS__)
#define REQ_LOG_WARN(message, ...)                                             \
  etl::ComponentLogger<etl::RequestHandler>::warn(message, ##__VA_ARGS__)
#define REQ_LOG_ERROR(message, ...)                                            \
  etl::ComponentLogger<etl::RequestHandler>::error(message, ##__VA_ARGS__)

#define TRANSFORM_LOG_DEBUG(message, ...)                                      \
  etl::DataTransformerLogger::debug(message, ##__VA_ARGS__)
#define TRANSFORM_LOG_INFO(message, ...)                                       \
  etl::DataTransformerLogger::info(message, ##__VA_ARGS__)
#define TRANSFORM_LOG_WARN(message, ...)                                       \
  etl::DataTransformerLogger::warn(message, ##__VA_ARGS__)
#define TRANSFORM_LOG_ERROR(message, ...)                                      \
  etl::DataTransformerLogger::error(message, ##__VA_ARGS__)

#define JOB_LOG_DEBUG(message, ...)                                            \
  etl::JobMonitorLogger::debug(message, ##__VA_ARGS__)
#define JOB_LOG_INFO(message, ...)                                             \
  etl::JobMonitorLogger::info(message, ##__VA_ARGS__)
#define JOB_LOG_WARN(message, ...)                                             \
  etl::JobMonitorLogger::warn(message, ##__VA_ARGS__)
#define JOB_LOG_ERROR(message, ...)                                            \
  etl::JobMonitorLogger::error(message, ##__VA_ARGS__)
