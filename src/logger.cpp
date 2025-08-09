#include "logger.hpp"
#include "job_monitoring_models.hpp"
#include "websocket_manager.hpp"
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <sstream>

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    shutdown();
}

void Logger::configure(const LogConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    
    // Apply configuration directly without additional locks (we already have configMutex_)
    // Set log level directly
    config_.level = config.level;
    
    // Set log format directly  
    config_.format = config.format;
    
    // Set log file with file operations
    {
        std::lock_guard<std::mutex> fileLock(fileMutex_);
        
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
        
        config_.logFile = config.logFile;
        currentLogFile_ = config.logFile;
        
        if (config_.fileOutput) {
            // Create directory if it doesn't exist
            std::filesystem::path logPath(config.logFile);
            std::filesystem::create_directories(logPath.parent_path());
            
            fileStream_.open(currentLogFile_, std::ios::app);
            
            if (!fileStream_.is_open()) {
                std::cerr << "Failed to open log file: " << config.logFile << std::endl;
                config_.fileOutput = false;
            } else {
                // Get current file size
                if (std::filesystem::exists(currentLogFile_)) {
                    currentFileSize_ = std::filesystem::file_size(currentLogFile_);
                } else {
                    currentFileSize_ = 0;
                }
                
                // Write simple startup message
                std::string startupMsg = "[" + formatTimestamp() + "] [INFO ] [Logger] Enhanced logger initialized";
                fileStream_ << startupMsg << std::endl;
                fileStream_.flush();
                currentFileSize_ += startupMsg.length() + 1;
            }
        }
    }
    
    // Set other options directly
    config_.consoleOutput = config.consoleOutput;
    config_.fileOutput = config.fileOutput;
    config_.enableRotation = config.enableRotation;
    config_.maxFileSize = config.maxFileSize;
    config_.maxBackupFiles = config.maxBackupFiles;
    config_.componentFilter = config.componentFilter;
    config_.includeMetrics = config.includeMetrics;
    config_.flushInterval = config.flushInterval;
    
    // Handle async logging initialization
    if (config.asyncLogging && !config_.asyncLogging && !asyncStarted_) {
        config_.asyncLogging = true;
        stopAsync_ = false;
        asyncStarted_ = true;
        asyncThread_ = std::thread(&Logger::asyncWorker, this);
    }
    
    // Handle real-time streaming initialization
    config_.enableRealTimeStreaming = config.enableRealTimeStreaming;
    config_.streamingQueueSize = config.streamingQueueSize;
    config_.streamAllLevels = config.streamAllLevels;
    config_.streamingJobFilter = config.streamingJobFilter;
    
    if (config.enableRealTimeStreaming && !streamingStarted_) {
        enableRealTimeStreaming(true);
    }
}

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.level = level;
}

void Logger::setLogFormat(LogFormat format) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.format = format;
}

void Logger::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
    
    config_.logFile = filename;
    currentLogFile_ = filename;
    
    // Create directory if it doesn't exist
    std::filesystem::path logPath(filename);
    std::filesystem::create_directories(logPath.parent_path());
    
    fileStream_.open(currentLogFile_, std::ios::app);
    
    if (!fileStream_.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
        config_.fileOutput = false;
    } else {
        config_.fileOutput = true;
        // Get current file size
        if (std::filesystem::exists(currentLogFile_)) {
            currentFileSize_ = std::filesystem::file_size(currentLogFile_);
        } else {
            currentFileSize_ = 0;
        }
        
        // Write simple startup message to avoid recursion during initialization
        std::string startupMsg = "[" + formatTimestamp() + "] [INFO ] [Logger] Enhanced logger initialized";
        fileStream_ << startupMsg << std::endl;
        fileStream_.flush();
        currentFileSize_ += startupMsg.length() + 1;
    }
}

void Logger::enableConsoleOutput(bool enable) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.consoleOutput = enable;
}

void Logger::enableFileOutput(bool enable) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.fileOutput = enable;
}

void Logger::enableAsyncLogging(bool enable) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    if (enable && !config_.asyncLogging && !asyncStarted_) {
        config_.asyncLogging = true;
        stopAsync_ = false;
        asyncStarted_ = true;
        asyncThread_ = std::thread(&Logger::asyncWorker, this);
    } else if (!enable && config_.asyncLogging && asyncStarted_) {
        config_.asyncLogging = false;
        stopAsync_ = true;
        asyncCondition_.notify_all();
        if (asyncThread_.joinable()) {
            asyncThread_.join();
        }
        asyncStarted_ = false;
    }
}

void Logger::setComponentFilter(const std::unordered_set<std::string>& components) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.componentFilter = components;
}

void Logger::enableRotation(bool enable, size_t maxFileSize, int maxBackupFiles) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.enableRotation = enable;
    config_.maxFileSize = maxFileSize;
    config_.maxBackupFiles = maxBackupFiles;
}

void Logger::log(LogLevel level, const std::string& component, const std::string& message, 
                 const std::unordered_map<std::string, std::string>& context) {
    if (!shouldLog(level, component)) {
        return;
    }
    
    // Update metrics
    metrics_.totalMessages++;
    if (level == LogLevel::ERROR || level == LogLevel::FATAL) {
        metrics_.errorCount++;
    } else if (level == LogLevel::WARN) {
        metrics_.warningCount++;
    }
    
    std::string formattedMessage = formatMessage(level, component, message, context);
    writeLog(formattedMessage);
}

void Logger::debug(const std::string& component, const std::string& message, 
                   const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::DEBUG, component, message, context);
}

void Logger::info(const std::string& component, const std::string& message, 
                  const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::INFO, component, message, context);
}

void Logger::warn(const std::string& component, const std::string& message, 
                  const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::WARN, component, message, context);
}

void Logger::error(const std::string& component, const std::string& message, 
                   const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::ERROR, component, message, context);
}

void Logger::fatal(const std::string& component, const std::string& message, 
                   const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::FATAL, component, message, context);
}

void Logger::logMetric(const std::string& name, double value, const std::string& unit) {
    if (!config_.includeMetrics) return;
    
    std::unordered_map<std::string, std::string> context = {
        {"metric_name", name},
        {"metric_value", std::to_string(value)},
        {"metric_unit", unit},
        {"metric_type", "gauge"}
    };
    
    log(LogLevel::INFO, "Metrics", "Metric recorded: " + name, context);
}

void Logger::logPerformance(const std::string& operation, double durationMs, 
                           const std::unordered_map<std::string, std::string>& context) {
    auto perfContext = context;
    perfContext["operation"] = operation;
    perfContext["duration_ms"] = std::to_string(durationMs);
    perfContext["performance_log"] = "true";
    
    log(LogLevel::INFO, "Performance", "Operation completed: " + operation, perfContext);
}

LogMetrics Logger::getMetrics() const {
    return metrics_;
}

void Logger::flush() {
    if (config_.asyncLogging) {
        // For async logging, notify worker to flush
        std::unique_lock<std::mutex> lock(asyncMutex_);
        asyncCondition_.notify_all();
    }
    
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (fileStream_.is_open()) {
        fileStream_.flush();
    }
}

void Logger::shutdown() {
    if (asyncStarted_) {
        enableAsyncLogging(false);
    }
    
    if (streamingStarted_) {
        enableRealTimeStreaming(false);
    }
    
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

std::string Logger::formatTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

std::string Logger::formatMessage(LogLevel level, const std::string& component, 
                                 const std::string& message, 
                                 const std::unordered_map<std::string, std::string>& context) {
    return config_.format == LogFormat::JSON 
        ? formatJsonMessage(level, component, message, context)
        : formatTextMessage(level, component, message, context);
}

std::string Logger::formatTextMessage(LogLevel level, const std::string& component, 
                                     const std::string& message, 
                                     const std::unordered_map<std::string, std::string>& context) {
    std::ostringstream oss;
    oss << "[" << formatTimestamp() << "] "
        << "[" << levelToString(level) << "] "
        << "[" << component << "] "
        << message;
    
    if (!context.empty()) {
        oss << " |";
        for (const auto& [key, value] : context) {
            oss << " " << key << "=" << value;
        }
    }
    
    return oss.str();
}

std::string Logger::formatJsonMessage(LogLevel level, const std::string& component, 
                                     const std::string& message, 
                                     const std::unordered_map<std::string, std::string>& context) {
    std::ostringstream oss;
    oss << "{"
        << "\"timestamp\":\"" << formatTimestamp() << "\","
        << "\"level\":\"" << levelToString(level) << "\","
        << "\"component\":\"" << escapeJson(component) << "\","
        << "\"message\":\"" << escapeJson(message) << "\"";
    
    if (!context.empty()) {
        oss << ",\"context\":{";
        bool first = true;
        for (const auto& [key, value] : context) {
            if (!first) oss << ",";
            oss << "\"" << escapeJson(key) << "\":\"" << escapeJson(value) << "\"";
            first = false;
        }
        oss << "}";
    }
    
    oss << "}";
    return oss.str();
}

std::string Logger::escapeJson(const std::string& str) {
    std::string result;
    result.reserve(str.length() + 20); // Reserve some extra space for escapes
    
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20) {
                    std::ostringstream oss;
                    oss << "\\u" << std::setfill('0') << std::setw(4) << std::hex << static_cast<int>(c);
                    result += oss.str();
                } else {
                    result += c;
                }
                break;
        }
    }
    
    return result;
}

void Logger::writeLog(const std::string& formattedMessage) {
    if (config_.asyncLogging) {
        writeLogAsync(formattedMessage);
    } else {
        writeLogSync(formattedMessage);
    }
}

void Logger::writeLogSync(const std::string& formattedMessage) {
    if (config_.consoleOutput) {
        std::cout << formattedMessage << std::endl;
    }
    
    if (config_.fileOutput) {
        std::lock_guard<std::mutex> lock(fileMutex_);
        if (fileStream_.is_open()) {
            // Check if rotation is needed
            if (config_.enableRotation && 
                currentFileSize_ + formattedMessage.length() > config_.maxFileSize) {
                rotateLogFile();
            }
            
            fileStream_ << formattedMessage << std::endl;
            fileStream_.flush();
            currentFileSize_ += formattedMessage.length() + 1;
        }
    }
}

void Logger::writeLogAsync(const std::string& formattedMessage) {
    std::lock_guard<std::mutex> lock(asyncMutex_);
    
    // Check queue size to prevent memory issues
    if (messageQueue_.size() > 10000) {
        metrics_.droppedMessages++;
        return;
    }
    
    messageQueue_.push(formattedMessage);
    asyncCondition_.notify_one();
}

void Logger::asyncWorker() {
    while (!stopAsync_) {
        std::unique_lock<std::mutex> lock(asyncMutex_);
        
        asyncCondition_.wait(lock, [this] { 
            return !messageQueue_.empty() || stopAsync_; 
        });
        
        // Process all messages in queue
        while (!messageQueue_.empty()) {
            std::string message = messageQueue_.front();
            messageQueue_.pop();
            lock.unlock();
            
            writeLogSync(message);
            
            lock.lock();
        }
    }
    
    // Process remaining messages on shutdown
    std::lock_guard<std::mutex> lock(asyncMutex_);
    while (!messageQueue_.empty()) {
        writeLogSync(messageQueue_.front());
        messageQueue_.pop();
    }
}

void Logger::rotateLogFile() {
    if (!config_.enableRotation) return;
    
    fileStream_.close();
    
    // Move existing backup files
    for (int i = config_.maxBackupFiles - 1; i > 0; i--) {
        std::string oldFile = currentLogFile_ + "." + std::to_string(i);
        std::string newFile = currentLogFile_ + "." + std::to_string(i + 1);
        
        if (std::filesystem::exists(oldFile)) {
            if (i == config_.maxBackupFiles - 1) {
                std::filesystem::remove(newFile); // Remove oldest
            }
            std::filesystem::rename(oldFile, newFile);
        }
    }
    
    // Move current log to .1
    std::string firstBackup = currentLogFile_ + ".1";
    if (std::filesystem::exists(currentLogFile_)) {
        std::filesystem::rename(currentLogFile_, firstBackup);
    }
    
    // Create new log file
    fileStream_.open(currentLogFile_, std::ios::out);
    currentFileSize_ = 0;
    
    if (!fileStream_.is_open()) {
        std::cerr << "Failed to create new log file after rotation: " << currentLogFile_ << std::endl;
        config_.fileOutput = false;
    }
}

bool Logger::shouldLog(LogLevel level, const std::string& component) {
    if (level < config_.level) {
        return false;
    }
    
    if (!config_.componentFilter.empty() && 
        config_.componentFilter.find(component) == config_.componentFilter.end()) {
        return false;
    }
    
    return true;
}

// Real-time streaming methods implementation
void Logger::setWebSocketManager(std::shared_ptr<WebSocketManager> wsManager) {
    std::lock_guard<std::mutex> lock(configMutex_);
    wsManager_ = wsManager;
}

void Logger::enableRealTimeStreaming(bool enable) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    if (enable && !config_.enableRealTimeStreaming && !streamingStarted_) {
        config_.enableRealTimeStreaming = true;
        stopStreaming_ = false;
        streamingStarted_ = true;
        streamingThread_ = std::thread(&Logger::streamingWorker, this);
    } else if (!enable && config_.enableRealTimeStreaming && streamingStarted_) {
        config_.enableRealTimeStreaming = false;
        stopStreaming_ = true;
        streamingCondition_.notify_all();
        if (streamingThread_.joinable()) {
            streamingThread_.join();
        }
        streamingStarted_ = false;
    }
}

void Logger::setStreamingJobFilter(const std::unordered_set<std::string>& jobIds) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.streamingJobFilter = jobIds;
}

void Logger::addStreamingJobFilter(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.streamingJobFilter.insert(jobId);
}

void Logger::removeStreamingJobFilter(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.streamingJobFilter.erase(jobId);
}

void Logger::clearStreamingJobFilter() {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.streamingJobFilter.clear();
}

void Logger::logForJob(LogLevel level, const std::string& component, 
                       const std::string& message, const std::string& jobId,
                       const std::unordered_map<std::string, std::string>& context) {
    // Regular logging
    log(level, component, message, context);
    
    // Real-time streaming if enabled
    if (config_.enableRealTimeStreaming && shouldStreamLog(level, jobId)) {
        auto logMsg = createLogMessage(level, component, message, jobId, context);
        
        std::lock_guard<std::mutex> lock(streamingMutex_);
        
        // Check queue size to prevent memory issues
        if (streamingQueue_.size() >= config_.streamingQueueSize) {
            metrics_.droppedMessages++;
            return;
        }
        
        streamingQueue_.push(logMsg);
        streamingCondition_.notify_one();
    }
}

void Logger::debugForJob(const std::string& component, const std::string& message, 
                         const std::string& jobId,
                         const std::unordered_map<std::string, std::string>& context) {
    logForJob(LogLevel::DEBUG, component, message, jobId, context);
}

void Logger::infoForJob(const std::string& component, const std::string& message, 
                        const std::string& jobId,
                        const std::unordered_map<std::string, std::string>& context) {
    logForJob(LogLevel::INFO, component, message, jobId, context);
}

void Logger::warnForJob(const std::string& component, const std::string& message, 
                        const std::string& jobId,
                        const std::unordered_map<std::string, std::string>& context) {
    logForJob(LogLevel::WARN, component, message, jobId, context);
}

void Logger::errorForJob(const std::string& component, const std::string& message, 
                         const std::string& jobId,
                         const std::unordered_map<std::string, std::string>& context) {
    logForJob(LogLevel::ERROR, component, message, jobId, context);
}

void Logger::fatalForJob(const std::string& component, const std::string& message, 
                         const std::string& jobId,
                         const std::unordered_map<std::string, std::string>& context) {
    logForJob(LogLevel::FATAL, component, message, jobId, context);
}

void Logger::streamingWorker() {
    while (!stopStreaming_) {
        std::unique_lock<std::mutex> lock(streamingMutex_);
        
        streamingCondition_.wait(lock, [this] { 
            return !streamingQueue_.empty() || stopStreaming_; 
        });
        
        // Process all messages in queue
        while (!streamingQueue_.empty()) {
            auto logMsg = streamingQueue_.front();
            streamingQueue_.pop();
            lock.unlock();
            
            broadcastLogMessage(logMsg);
            
            lock.lock();
        }
    }
    
    // Process remaining messages on shutdown
    std::lock_guard<std::mutex> lock(streamingMutex_);
    while (!streamingQueue_.empty()) {
        broadcastLogMessage(streamingQueue_.front());
        streamingQueue_.pop();
    }
}

void Logger::broadcastLogMessage(const std::shared_ptr<LogMessage>& logMsg) {
    if (!wsManager_ || !logMsg) {
        return;
    }
    
    try {
        // Broadcast to WebSocket clients with filtering
        wsManager_->broadcastLogMessage(logMsg->toJson(), logMsg->jobId, logMsg->level);
    } catch (const std::exception& e) {
        // Log error without causing recursion
        std::cerr << "Failed to broadcast log message: " << e.what() << std::endl;
    }
}

bool Logger::shouldStreamLog(LogLevel level, const std::string& jobId) {
    // Check if streaming is enabled
    if (!config_.enableRealTimeStreaming) {
        return false;
    }
    
    // Check log level filtering
    if (!config_.streamAllLevels && level < config_.level) {
        return false;
    }
    
    // Check job ID filtering
    if (!config_.streamingJobFilter.empty() && 
        config_.streamingJobFilter.find(jobId) == config_.streamingJobFilter.end()) {
        return false;
    }
    
    return true;
}

std::shared_ptr<LogMessage> Logger::createLogMessage(LogLevel level, const std::string& component,
                                                    const std::string& message, const std::string& jobId,
                                                    const std::unordered_map<std::string, std::string>& context) {
    auto logMsg = std::make_shared<LogMessage>();
    logMsg->jobId = jobId;
    logMsg->level = levelToString(level);
    logMsg->component = component;
    logMsg->message = message;
    logMsg->timestamp = std::chrono::system_clock::now();
    logMsg->context = context;
    
    return logMsg;
}


