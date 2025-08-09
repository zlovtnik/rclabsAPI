#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <string_view>
#include "transparent_string_hash.hpp"

// Forward declarations for real-time log streaming
class WebSocketManager;
struct LogMessage;

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

enum class LogFormat { TEXT = 0, JSON = 1 };

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
  std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> componentFilter; // Empty = all components
  bool includeMetrics = false;
  int flushInterval = 1000; // milliseconds
  
  // Real-time streaming configuration
  bool enableRealTimeStreaming = false;
  size_t streamingQueueSize = 1000;
  bool streamAllLevels = true;
  std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> streamingJobFilter; // Empty = all jobs
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
  void setComponentFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> &components);
  void enableRotation(bool enable, size_t maxFileSize = 10 * 1024 * 1024,
                      int maxBackupFiles = 5);

  // Logging methods
  void log(LogLevel level, const std::string &component,
           const std::string &message,
           const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context = {});
  void debug(const std::string &component, const std::string &message,
             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context = {});
  void info(const std::string &component, const std::string &message,
            const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context = {});
  void warn(const std::string &component, const std::string &message,
            const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context = {});
  void error(const std::string &component, const std::string &message,
             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context = {});
  void fatal(const std::string &component, const std::string &message,
             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context = {});

  // Metrics and performance logging
  void logMetric(const std::string &name, double value,
                 const std::string &unit = "");
  void logPerformance(
      const std::string &operation, double durationMs,
      const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context = {});
  LogMetrics getMetrics() const;

  // Control methods
  void flush();
  void shutdown();

  // Real-time log streaming methods
  void setWebSocketManager(std::shared_ptr<WebSocketManager> wsManager);
  void enableRealTimeStreaming(bool enable);
  void setStreamingJobFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& jobIds);
  void addStreamingJobFilter(const std::string& jobId);
  void removeStreamingJobFilter(const std::string& jobId);
  void clearStreamingJobFilter();
  
  // Job-specific logging methods
  void logForJob(LogLevel level, const std::string& component, 
                 const std::string& message, const std::string& jobId,
                 const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
  void debugForJob(const std::string& component, const std::string& message, 
                   const std::string& jobId,
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
  void infoForJob(const std::string& component, const std::string& message, 
                  const std::string& jobId,
                  const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
  void warnForJob(const std::string& component, const std::string& message, 
                  const std::string& jobId,
                  const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
  void errorForJob(const std::string& component, const std::string& message, 
                   const std::string& jobId,
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
  void fatalForJob(const std::string& component, const std::string& message, 
                   const std::string& jobId,
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});

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
  std::string
  formatMessage(LogLevel level, const std::string &component,
                const std::string &message,
                const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context);
  std::string formatTextMessage(
      LogLevel level, const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context);
  std::string formatJsonMessage(
      LogLevel level, const std::string &component, const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> &context);
  void writeLog(const std::string &formattedMessage);
  void writeLogSync(const std::string &formattedMessage);
  void writeLogAsync(const std::string &formattedMessage);
  void asyncWorker();
  void rotateLogFile();
  bool shouldLog(LogLevel level, const std::string &component);
  std::string escapeJson(const std::string &str);
  
  // Real-time streaming helpers
  void streamingWorker();
  void broadcastLogMessage(const std::shared_ptr<LogMessage>& logMsg);
  bool shouldStreamLog(LogLevel level, const std::string& jobId);
  std::shared_ptr<LogMessage> createLogMessage(LogLevel level, const std::string& component,
                                              const std::string& message, const std::string& jobId,
                                              const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context);
};

// Convenience macros for logging (backward compatible)
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

// Component-specific logging macros (backward compatible)
#define CONFIG_LOG_DEBUG(message, ...)                                         \
  LOG_DEBUG("ConfigManager", message, ##__VA_ARGS__)
#define CONFIG_LOG_INFO(message, ...)                                          \
  LOG_INFO("ConfigManager", message, ##__VA_ARGS__)
#define CONFIG_LOG_WARN(message, ...)                                          \
  LOG_WARN("ConfigManager", message, ##__VA_ARGS__)
#define CONFIG_LOG_ERROR(message, ...)                                         \
  LOG_ERROR("ConfigManager", message, ##__VA_ARGS__)

#define DB_LOG_DEBUG(message, ...)                                             \
  LOG_DEBUG("DatabaseManager", message, ##__VA_ARGS__)
#define DB_LOG_INFO(message, ...)                                              \
  LOG_INFO("DatabaseManager", message, ##__VA_ARGS__)
#define DB_LOG_WARN(message, ...)                                              \
  LOG_WARN("DatabaseManager", message, ##__VA_ARGS__)
#define DB_LOG_ERROR(message, ...)                                             \
  LOG_ERROR("DatabaseManager", message, ##__VA_ARGS__)

#define HTTP_LOG_DEBUG(message, ...)                                           \
  LOG_DEBUG("HttpServer", message, ##__VA_ARGS__)
#define HTTP_LOG_INFO(message, ...)                                            \
  LOG_INFO("HttpServer", message, ##__VA_ARGS__)
#define HTTP_LOG_WARN(message, ...)                                            \
  LOG_WARN("HttpServer", message, ##__VA_ARGS__)
#define HTTP_LOG_ERROR(message, ...)                                           \
  LOG_ERROR("HttpServer", message, ##__VA_ARGS__)

#define AUTH_LOG_DEBUG(message, ...)                                           \
  LOG_DEBUG("AuthManager", message, ##__VA_ARGS__)
#define AUTH_LOG_INFO(message, ...)                                            \
  LOG_INFO("AuthManager", message, ##__VA_ARGS__)
#define AUTH_LOG_WARN(message, ...)                                            \
  LOG_WARN("AuthManager", message, ##__VA_ARGS__)
#define AUTH_LOG_ERROR(message, ...)                                           \
  LOG_ERROR("AuthManager", message, ##__VA_ARGS__)

#define ETL_LOG_DEBUG(message, ...)                                            \
  LOG_DEBUG("ETLJobManager", message, ##__VA_ARGS__)
#define ETL_LOG_INFO(message, ...)                                             \
  LOG_INFO("ETLJobManager", message, ##__VA_ARGS__)
#define ETL_LOG_WARN(message, ...)                                             \
  LOG_WARN("ETLJobManager", message, ##__VA_ARGS__)
#define ETL_LOG_ERROR(message, ...)                                            \
  LOG_ERROR("ETLJobManager", message, ##__VA_ARGS__)

#define REQ_LOG_DEBUG(message, ...)                                            \
  LOG_DEBUG("RequestHandler", message, ##__VA_ARGS__)
#define REQ_LOG_INFO(message, ...)                                             \
  LOG_INFO("RequestHandler", message, ##__VA_ARGS__)
#define REQ_LOG_WARN(message, ...)                                             \
  LOG_WARN("RequestHandler", message, ##__VA_ARGS__)
#define REQ_LOG_ERROR(message, ...)                                            \
  LOG_ERROR("RequestHandler", message, ##__VA_ARGS__)

#define TRANSFORM_LOG_DEBUG(message, ...)                                      \
  LOG_DEBUG("DataTransformer", message, ##__VA_ARGS__)
#define TRANSFORM_LOG_INFO(message, ...)                                       \
  LOG_INFO("DataTransformer", message, ##__VA_ARGS__)
#define TRANSFORM_LOG_WARN(message, ...)                                       \
  LOG_WARN("DataTransformer", message, ##__VA_ARGS__)
#define TRANSFORM_LOG_ERROR(message, ...)                                      \
  LOG_ERROR("DataTransformer", message, ##__VA_ARGS__)

#define REQUEST_LOG_DEBUG(message, ...)                                        \
  LOG_DEBUG("RequestHandler", message, ##__VA_ARGS__)
#define REQUEST_LOG_INFO(message, ...)                                         \
  LOG_INFO("RequestHandler", message, ##__VA_ARGS__)
#define REQUEST_LOG_WARN(message, ...)                                         \
  LOG_WARN("RequestHandler", message, ##__VA_ARGS__)
#define REQUEST_LOG_ERROR(message, ...)                                        \
  LOG_ERROR("RequestHandler", message, ##__VA_ARGS__)

#define WS_LOG_DEBUG(message, ...)                                             \
  LOG_DEBUG("WebSocket", message, ##__VA_ARGS__)
#define WS_LOG_INFO(message, ...)                                              \
  LOG_INFO("WebSocket", message, ##__VA_ARGS__)
#define WS_LOG_WARN(message, ...)                                              \
  LOG_WARN("WebSocket", message, ##__VA_ARGS__)
#define WS_LOG_ERROR(message, ...)                                             \
  LOG_ERROR("WebSocket", message, ##__VA_ARGS__)

#define JOB_LOG_DEBUG(message, ...)                                            \
  LOG_DEBUG("JobMonitorService", message, ##__VA_ARGS__)
#define JOB_LOG_INFO(message, ...)                                             \
  LOG_INFO("JobMonitorService", message, ##__VA_ARGS__)
#define JOB_LOG_WARN(message, ...)                                             \
  LOG_WARN("JobMonitorService", message, ##__VA_ARGS__)
#define JOB_LOG_ERROR(message, ...)                                            \
  LOG_ERROR("JobMonitorService", message, ##__VA_ARGS__)

// Job-specific logging macros for real-time streaming
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

// ETL Job-specific logging macros
#define ETL_LOG_DEBUG_JOB(message, jobId, ...)                                 \
  LOG_DEBUG_JOB("ETLJobManager", message, jobId, ##__VA_ARGS__)
#define ETL_LOG_INFO_JOB(message, jobId, ...)                                  \
  LOG_INFO_JOB("ETLJobManager", message, jobId, ##__VA_ARGS__)
#define ETL_LOG_WARN_JOB(message, jobId, ...)                                  \
  LOG_WARN_JOB("ETLJobManager", message, jobId, ##__VA_ARGS__)
#define ETL_LOG_ERROR_JOB(message, jobId, ...)                                 \
  LOG_ERROR_JOB("ETLJobManager", message, jobId, ##__VA_ARGS__)
