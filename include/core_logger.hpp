#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <string_view>
#include <shared_mutex>

#include "log_handler.hpp"
#include "log_file_manager.hpp"
#include "transparent_string_hash.hpp"

// Forward declarations for backward compatibility
class WebSocketManager;
enum class LogFormat { TEXT = 0, JSON = 1 };

// Backward compatibility types
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
    std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> componentFilter;
    bool includeMetrics = false;
    int flushInterval = 1000; // milliseconds
    bool enableRealTimeStreaming = false;
    size_t streamingQueueSize = 1000;
    bool streamAllLevels = true;
    std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> streamingJobFilter;
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
    std::chrono::steady_clock::time_point startTime{std::chrono::steady_clock::now()};
    
    LogMetrics() = default;
    ~LogMetrics() = default;
    
    LogMetrics(const LogMetrics& other)
        : totalMessages(other.totalMessages.load())
        , errorCount(other.errorCount.load())
        , warningCount(other.warningCount.load())
        , droppedMessages(other.droppedMessages.load())
        , startTime(other.startTime) {}
    
    LogMetrics& operator=(const LogMetrics& other) {
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

/**
 * @brief Core Logger configuration
 */
struct CoreLoggerConfig {
    LogLevel minLevel = LogLevel::INFO;
    bool enableAsyncLogging = true;
    size_t asyncQueueSize = 10000;
    std::chrono::milliseconds flushInterval{1000};
    bool enableMetrics = true;
    
    // Component filtering
    std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> componentFilter;
    bool filterMode = false; // false = blacklist, true = whitelist
    
    // Job-specific filtering
    std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> jobFilter;
    bool jobFilterMode = false; // false = blacklist, true = whitelist
    
    // Performance settings
    size_t maxMemoryUsage = 50 * 1024 * 1024; // 50MB
    std::chrono::milliseconds maxLatency{100};
    bool enableBackpressure = true;
};

/**
 * @brief Core Logger implementing the handler pattern
 * 
 * This is the new core logging system that replaces the monolithic Logger class.
 * It uses a handler-based architecture for pluggable output destinations and
 * leverages LogFileManager for file operations.
 * 
 * Key features:
 * - Handler pattern for extensible output destinations
 * - Asynchronous logging support
 * - Thread-safe operations
 * - Performance metrics and monitoring
 * - Backward compatibility with existing Logger interface
 * - Integration with LogFileManager for file operations
 */
class CoreLogger {
public:
    // Use type alias to maintain compatibility
    using Config = CoreLoggerConfig;
    
/**
 * @brief Performance and operational metrics
 */
struct LoggerMetrics {
    std::atomic<uint64_t> totalMessages{0};
    std::atomic<uint64_t> droppedMessages{0};
    std::atomic<uint64_t> errorCount{0};
    std::atomic<uint64_t> warningCount{0};
    std::atomic<uint64_t> asyncQueueSize{0};
    std::atomic<double> avgProcessingTime{0.0};
    std::chrono::steady_clock::time_point startTime{std::chrono::steady_clock::now()};
    
    LoggerMetrics() = default;
    ~LoggerMetrics() = default;
    
    LoggerMetrics(const LoggerMetrics& other) 
        : totalMessages(other.totalMessages.load())
        , droppedMessages(other.droppedMessages.load())
        , errorCount(other.errorCount.load())
        , warningCount(other.warningCount.load())
        , asyncQueueSize(other.asyncQueueSize.load())
        , avgProcessingTime(other.avgProcessingTime.load())
        , startTime(other.startTime) {}
        
    LoggerMetrics& operator=(const LoggerMetrics& other) {
        if (this != &other) {
            totalMessages.store(other.totalMessages.load());
            droppedMessages.store(other.droppedMessages.load());
            errorCount.store(other.errorCount.load());
            warningCount.store(other.warningCount.load());
            asyncQueueSize.store(other.asyncQueueSize.load());
            avgProcessingTime.store(other.avgProcessingTime.load());
            startTime = other.startTime;
        }
        return *this;
    }
};

    // Handler registration result
    enum class HandlerResult {
        SUCCESS,
        ALREADY_EXISTS,
        INVALID_HANDLER,
        REGISTRATION_FAILED
    };

public:
    /**
     * @brief Get the singleton instance of CoreLogger
     * @return Reference to the singleton instance
     */
    static CoreLogger& getInstance();
    
    /**
     * @brief Configure the logger with new settings
     * @param config New configuration to apply
     */
    void configure(const Config& config);
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    Config getConfig() const;
    
    // === Handler Management ===
    
    /**
     * @brief Register a new log handler
     * @param handler Shared pointer to the handler to register
     * @return Result of the registration operation
     */
    HandlerResult registerHandler(std::shared_ptr<LogHandler> handler);
    
    /**
     * @brief Unregister a handler by ID
     * @param handlerId ID of the handler to remove
     * @return true if handler was found and removed
     */
    bool unregisterHandler(const std::string& handlerId);
    
    /**
     * @brief Get all registered handler IDs
     * @return Vector of handler IDs
     */
    std::vector<std::string> getHandlerIds() const;
    
    /**
     * @brief Check if a handler is registered
     * @param handlerId ID of the handler to check
     * @return true if handler is registered
     */
    bool hasHandler(const std::string& handlerId) const;
    
    /**
     * @brief Get a registered handler by ID
     * @param handlerId ID of the handler to retrieve
     * @return Shared pointer to handler or nullptr if not found
     */
    std::shared_ptr<LogHandler> getHandler(const std::string& handlerId) const;
    
    // === Core Logging Interface ===
    
    /**
     * @brief Log a message with full context
     * @param level Log level
     * @param component Component generating the log
     * @param message Log message
     * @param context Additional context information
     */
    void log(LogLevel level, const std::string& component, const std::string& message,
             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    
    /**
     * @brief Log a message for a specific job
     * @param level Log level
     * @param component Component generating the log
     * @param message Log message
     * @param jobId Job identifier
     * @param context Additional context information
     */
    void logForJob(LogLevel level, const std::string& component, const std::string& message,
                   const std::string& jobId,
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    
    // Convenience methods for different log levels
    void debug(const std::string& component, const std::string& message,
               const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void info(const std::string& component, const std::string& message,
              const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void warn(const std::string& component, const std::string& message,
              const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void error(const std::string& component, const std::string& message,
               const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void fatal(const std::string& component, const std::string& message,
               const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    
    // Job-specific convenience methods
    void debugForJob(const std::string& component, const std::string& message, const std::string& jobId,
                     const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void infoForJob(const std::string& component, const std::string& message, const std::string& jobId,
                    const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void warnForJob(const std::string& component, const std::string& message, const std::string& jobId,
                    const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void errorForJob(const std::string& component, const std::string& message, const std::string& jobId,
                     const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void fatalForJob(const std::string& component, const std::string& message, const std::string& jobId,
                     const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    
    // === Performance and Metrics ===
    
    /**
     * @brief Log a performance metric
     * @param name Metric name
     * @param value Metric value
     * @param unit Metric unit (optional)
     * @param context Additional context
     */
    void logMetric(const std::string& name, double value, const std::string& unit = "",
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    
    /**
     * @brief Log performance timing information
     * @param operation Operation name
     * @param durationMs Duration in milliseconds
     * @param context Additional context
     */
    void logPerformance(const std::string& operation, double durationMs,
                        const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    
    /**
     * @brief Get current logger metrics
     * @return Current metrics
     */
    LoggerMetrics getMetrics() const;
    
    /**
     * @brief Reset metrics counters
     */
    void resetMetrics();
    
    // === Control Operations ===
    
    /**
     * @brief Flush all handlers and pending operations
     */
    void flush();
    
    /**
     * @brief Shutdown the logger gracefully
     */
    void shutdown();
    
    /**
     * @brief Check if logger is currently running
     * @return true if logger is active
     */
    bool isRunning() const;
    
    // === Configuration Helpers ===
    
    /**
     * @brief Set minimum log level
     * @param level New minimum log level
     */
    void setLogLevel(LogLevel level);
    
    /**
     * @brief Get current minimum log level
     * @return Current minimum log level
     */
    LogLevel getLogLevel() const;
    
    /**
     * @brief Enable or disable asynchronous logging
     * @param enable true to enable async logging
     */
    void setAsyncLogging(bool enable);
    
    /**
     * @brief Check if async logging is enabled
     * @return true if async logging is enabled
     */
    bool isAsyncLogging() const;
    
    /**
     * @brief Set component filter
     * @param components Set of component names to filter
     * @param whitelist true for whitelist mode, false for blacklist
     */
    void setComponentFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& components, bool whitelist = false);
    
    /**
     * @brief Clear component filter
     */
    void clearComponentFilter();
    
    /**
     * @brief Set job filter
     * @param jobs Set of job IDs to filter
     * @param whitelist true for whitelist mode, false for blacklist
     */
    void setJobFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& jobs, bool whitelist = false);
    
    /**
     * @brief Clear job filter
     */
    void clearJobFilter();
    
    // === File Management Integration ===
    
    /**
     * @brief Set LogFileManager for file operations
     * @param fileManager Shared pointer to LogFileManager instance
     */
    void setFileManager(std::shared_ptr<LogFileManager> fileManager);
    
    /**
     * @brief Get current LogFileManager
     * @return Shared pointer to LogFileManager or nullptr if not set
     */
    std::shared_ptr<LogFileManager> getFileManager() const;

private:
    // Private constructor for singleton
    CoreLogger();
    
    // Destructor
    ~CoreLogger();
    
    // Disable copy and assignment
    CoreLogger(const CoreLogger&) = delete;
    CoreLogger& operator=(const CoreLogger&) = delete;
    
    // === Internal State ===
    
    // Configuration
    Config config_;
    mutable std::shared_mutex configMutex_;
    
    // Handler management
    std::unordered_map<std::string, std::shared_ptr<LogHandler>, TransparentStringHash, std::equal_to<>> handlers_;
    mutable std::shared_mutex handlersMutex_;
    
    // Async logging
    std::queue<LogEntry> asyncQueue_;
    std::thread asyncWorker_; // NOLINT: std::jthread not available in current C++ standard
    std::condition_variable asyncCondition_;
    std::mutex asyncMutex_;
    std::atomic<bool> stopAsync_{false};
    std::atomic<bool> asyncStarted_{false};
    
    // Metrics
    mutable LoggerMetrics metrics_;
    mutable std::mutex metricsMutex_;
    
    // File manager integration
    std::shared_ptr<LogFileManager> fileManager_;
    mutable std::shared_mutex fileManagerMutex_;
    
    // System state
    std::atomic<bool> isShutdown_{false};
    std::atomic<bool> isRunning_{false};
    
    // === Internal Methods ===
    
    /**
     * @brief Initialize the logger system
     */
    void initialize();
    
    /**
     * @brief Process a log entry through all handlers
     * @param entry Log entry to process
     */
    void processLogEntry(const LogEntry& entry);
    
    /**
     * @brief Check if a log entry should be processed
     * @param entry Log entry to check
     * @return true if entry should be processed
     */
    bool shouldProcess(const LogEntry& entry) const;
    
    /**
     * @brief Worker function for async logging thread
     */
    void asyncWorkerFunction();
    
    /**
     * @brief Start async logging if configured
     */
    void startAsyncLogging();
    
    /**
     * @brief Stop async logging
     */
    void stopAsyncLogging();
    
    /**
     * @brief Update processing time metrics
     * @param processingTime Time taken to process a log entry
     */
    void updateMetrics(const LogEntry& entry, std::chrono::microseconds processingTime);
    
    /**
     * @brief Validate handler before registration
     * @param handler Handler to validate
     * @return true if handler is valid
     */
    bool validateHandler(const std::shared_ptr<LogHandler>& handler) const;
    
    /**
     * @brief Create default handlers if none are registered
     */
    void createDefaultHandlers();
    
    /**
     * @brief Check component filter
     * @param component Component name to check
     * @return true if component should be logged
     */
    bool passesComponentFilter(const std::string& component) const;
    
    /**
     * @brief Check job filter
     * @param jobId Job ID to check
     * @return true if job should be logged
     */
    bool passesJobFilter(const std::string& jobId) const;
};

// === Backward Compatibility Layer ===

/**
 * @brief Backward compatibility wrapper for the old Logger interface
 * 
 * This class provides the same interface as the original Logger class
 * but delegates all operations to the new CoreLogger with handler pattern.
 * This ensures existing code continues to work without modification.
 */
class Logger {
public:
    static Logger& getInstance();
    
    // Configuration methods (delegate to CoreLogger)
    void configure(const LogConfig& config);
    void setLogLevel(LogLevel level);
    void setLogFormat(LogFormat format);
    void setLogFile(const std::string& filename);
    void enableConsoleOutput(bool enable);
    void enableFileOutput(bool enable);
    void enableAsyncLogging(bool enable);
    void setComponentFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& components);
    void enableRotation(bool enable, size_t maxFileSize = 10 * 1024 * 1024, int maxBackupFiles = 5);
    
    // Logging methods (delegate to CoreLogger)
    void log(LogLevel level, const std::string& component, const std::string& message,
             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void debug(const std::string& component, const std::string& message,
               const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void info(const std::string& component, const std::string& message,
              const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void warn(const std::string& component, const std::string& message,
              const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void error(const std::string& component, const std::string& message,
               const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void fatal(const std::string& component, const std::string& message,
               const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    
    // Job-specific logging methods (delegate to CoreLogger)
    void logForJob(LogLevel level, const std::string& component, const std::string& message,
                   const std::string& jobId,
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void debugForJob(const std::string& component, const std::string& message, const std::string& jobId,
                     const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void infoForJob(const std::string& component, const std::string& message, const std::string& jobId,
                    const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void warnForJob(const std::string& component, const std::string& message, const std::string& jobId,
                    const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void errorForJob(const std::string& component, const std::string& message, const std::string& jobId,
                     const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    void fatalForJob(const std::string& component, const std::string& message, const std::string& jobId,
                     const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    
    // Metrics and performance methods (delegate to CoreLogger)
    void logMetric(const std::string& name, double value, const std::string& unit = "");
    void logPerformance(const std::string& operation, double durationMs,
                        const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context = {});
    LogMetrics getMetrics() const;
    
    // Control methods (delegate to CoreLogger)
    void flush();
    void shutdown();
    
    // Real-time streaming methods (delegate to handlers)
    void setWebSocketManager(std::shared_ptr<WebSocketManager> wsManager);
    void enableRealTimeStreaming(bool enable);
    void setStreamingJobFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& jobIds);
    void addStreamingJobFilter(const std::string& jobId);
    void removeStreamingJobFilter(const std::string& jobId);
    void clearStreamingJobFilter();
    
    // Historical access methods (delegate to LogFileManager)
    void enableHistoricalAccess(bool enable);
    bool isHistoricalAccessEnabled() const;
    void setArchiveDirectory(const std::string& directory);
    std::string getArchiveDirectory() const;
    void setMaxQueryResults(size_t maxResults);
    size_t getMaxQueryResults() const;
    void enableLogIndexing(bool enable);
    bool isLogIndexingEnabled() const;
    
    // Query methods (delegate to LogFileManager)
    std::vector<HistoricalLogEntry> queryLogs(const LogQueryParams& params);
    std::vector<LogFileInfo> listLogFiles(bool includeArchived = false);
    bool archiveLogFile(const std::string& filename);
    bool restoreLogFile(const std::string& filename);
    bool deleteLogFile(const std::string& filename);
    bool compressLogFile(const std::string& filename, const std::string& format = "gzip");
    bool decompressLogFile(const std::string& filename);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    // Internal state for backward compatibility
    LogFormat currentFormat_ = LogFormat::TEXT;
    std::string currentLogFile_;
    bool consoleEnabled_ = true;
    bool fileEnabled_ = false;
    
    // Helper methods for compatibility layer
    void ensureFileHandler();
    void ensureConsoleHandler();
    void updateHandlers();
    std::shared_ptr<LogFileManager> getOrCreateFileManager();
};
