#include "core_logger.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <filesystem>

// === CoreLogger Implementation ===

CoreLogger& CoreLogger::getInstance() {
    static CoreLogger instance;
    return instance;
}

CoreLogger::CoreLogger() {
    initialize();
}

CoreLogger::~CoreLogger() {
    shutdown();
}

void CoreLogger::initialize() {
    isRunning_.store(true);
    
    // Set default configuration
    config_.minLevel = LogLevel::INFO;
    config_.enableAsyncLogging = true;
    config_.asyncQueueSize = 10000;
    config_.flushInterval = std::chrono::milliseconds(1000);
    config_.enableMetrics = true;
    
    // Create default handlers if none exist
    createDefaultHandlers();
    
    // Start async logging if enabled
    if (config_.enableAsyncLogging) {
        startAsyncLogging();
    }
}

void CoreLogger::configure(const Config& config) {
    std::unique_lock lock(configMutex_);
    
    bool restartAsync = (config.enableAsyncLogging != config_.enableAsyncLogging);
    
    config_ = config;
    
    if (restartAsync) {
        lock.unlock();
        if (config_.enableAsyncLogging) {
            startAsyncLogging();
        } else {
            stopAsyncLogging();
        }
    }
}

CoreLogger::Config CoreLogger::getConfig() const {
    std::shared_lock lock(configMutex_);
    return config_;
}

// === Handler Management ===

CoreLogger::HandlerResult CoreLogger::registerHandler(std::shared_ptr<LogHandler> handler) {
    if (!validateHandler(handler)) {
        return HandlerResult::INVALID_HANDLER;
    }
    
    std::unique_lock lock(handlersMutex_);
    
    const std::string& handlerId = handler->getId();
    if (handlers_.find(handlerId) != handlers_.end()) {
        return HandlerResult::ALREADY_EXISTS;
    }
    
    try {
        handlers_[handlerId] = handler;
        return HandlerResult::SUCCESS;
    } catch (const std::exception&) {
        return HandlerResult::REGISTRATION_FAILED;
    }
}

bool CoreLogger::unregisterHandler(const std::string& handlerId) {
    std::unique_lock lock(handlersMutex_);
    
    auto it = handlers_.find(handlerId);
    if (it == handlers_.end()) {
        return false;
    }
    
    // Shutdown the handler gracefully
    try {
        it->second->shutdown();
    } catch (const std::exception&) {
        // Continue with removal even if shutdown fails
    }
    
    handlers_.erase(it);
    return true;
}

std::vector<std::string> CoreLogger::getHandlerIds() const {
    std::shared_lock lock(handlersMutex_);
    
    std::vector<std::string> ids;
    ids.reserve(handlers_.size());
    
    for (const auto& pair : handlers_) {
        ids.push_back(pair.first);
    }
    
    return ids;
}

bool CoreLogger::hasHandler(const std::string& handlerId) const {
    std::shared_lock lock(handlersMutex_);
    return handlers_.find(handlerId) != handlers_.end();
}

std::shared_ptr<LogHandler> CoreLogger::getHandler(const std::string& handlerId) const {
    std::shared_lock lock(handlersMutex_);
    
    auto it = handlers_.find(handlerId);
    return (it != handlers_.end()) ? it->second : nullptr;
}

// === Core Logging Interface ===

void CoreLogger::log(LogLevel level, const std::string& component, const std::string& message,
                     const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    LogEntry entry(level, component, message, "", context);
    
    if (!shouldProcess(entry)) {
        return;
    }
    
    if (config_.enableAsyncLogging && asyncStarted_.load()) {
        // Async logging
        std::unique_lock lock(asyncMutex_);
        
        if (asyncQueue_.size() >= config_.asyncQueueSize) {
            if (config_.enableBackpressure) {
                // Drop oldest message
                asyncQueue_.pop();
                metrics_.droppedMessages.fetch_add(1);
            } else {
                // Drop this message
                metrics_.droppedMessages.fetch_add(1);
                return;
            }
        }
        
        asyncQueue_.push(std::move(entry));
        metrics_.asyncQueueSize.store(asyncQueue_.size());
        lock.unlock();
        
        asyncCondition_.notify_one();
    } else {
        // Synchronous logging
        processLogEntry(entry);
    }
}

void CoreLogger::logForJob(LogLevel level, const std::string& component, const std::string& message,
                           const std::string& jobId,
                           const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    LogEntry entry(level, component, message, jobId, context);
    
    if (!shouldProcess(entry)) {
        return;
    }
    
    if (config_.enableAsyncLogging && asyncStarted_.load()) {
        std::unique_lock lock(asyncMutex_);
        
        if (asyncQueue_.size() >= config_.asyncQueueSize) {
            if (config_.enableBackpressure) {
                asyncQueue_.pop();
                metrics_.droppedMessages.fetch_add(1);
            } else {
                metrics_.droppedMessages.fetch_add(1);
                return;
            }
        }
        
        asyncQueue_.push(std::move(entry));
        metrics_.asyncQueueSize.store(asyncQueue_.size());
        lock.unlock();
        
        asyncCondition_.notify_one();
    } else {
        processLogEntry(entry);
    }
}

// Convenience methods
void CoreLogger::debug(const std::string& component, const std::string& message,
                       const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    log(LogLevel::DEBUG, component, message, context);
}

void CoreLogger::info(const std::string& component, const std::string& message,
                      const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    log(LogLevel::INFO, component, message, context);
}

void CoreLogger::warn(const std::string& component, const std::string& message,
                      const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    log(LogLevel::WARN, component, message, context);
}

void CoreLogger::error(const std::string& component, const std::string& message,
                       const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    log(LogLevel::ERROR, component, message, context);
}

void CoreLogger::fatal(const std::string& component, const std::string& message,
                       const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    log(LogLevel::FATAL, component, message, context);
}

// Job-specific convenience methods
void CoreLogger::debugForJob(const std::string& component, const std::string& message, const std::string& jobId,
                             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    logForJob(LogLevel::DEBUG, component, message, jobId, context);
}

void CoreLogger::infoForJob(const std::string& component, const std::string& message, const std::string& jobId,
                            const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    logForJob(LogLevel::INFO, component, message, jobId, context);
}

void CoreLogger::warnForJob(const std::string& component, const std::string& message, const std::string& jobId,
                            const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    logForJob(LogLevel::WARN, component, message, jobId, context);
}

void CoreLogger::errorForJob(const std::string& component, const std::string& message, const std::string& jobId,
                             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    logForJob(LogLevel::ERROR, component, message, jobId, context);
}

void CoreLogger::fatalForJob(const std::string& component, const std::string& message, const std::string& jobId,
                             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    logForJob(LogLevel::FATAL, component, message, jobId, context);
}

// === Performance and Metrics ===

void CoreLogger::logMetric(const std::string& name, double value, const std::string& unit,
                           const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    auto metricContext = context;
    metricContext["metric_name"] = name;
    metricContext["metric_value"] = std::to_string(value);
    if (!unit.empty()) {
        metricContext["metric_unit"] = unit;
    }
    
    info("Metrics", "Metric recorded: " + name + " = " + std::to_string(value) + 
         (unit.empty() ? "" : " " + unit), metricContext);
}

void CoreLogger::logPerformance(const std::string& operation, double durationMs,
                                const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    auto perfContext = context;
    perfContext["operation"] = operation;
    perfContext["duration_ms"] = std::to_string(durationMs);
    
    info("Performance", "Operation completed: " + operation + " took " + 
         std::to_string(durationMs) + "ms", perfContext);
}

CoreLogger::LoggerMetrics CoreLogger::getMetrics() const {
    std::lock_guard lock(metricsMutex_);
    return metrics_;
}

void CoreLogger::resetMetrics() {
    std::lock_guard lock(metricsMutex_);
    metrics_ = LoggerMetrics{};
}

// === Control Operations ===

void CoreLogger::flush() {
    // Flush async queue if enabled
    if (config_.enableAsyncLogging && asyncStarted_.load()) {
        std::unique_lock lock(asyncMutex_);
        asyncCondition_.wait(lock, [this] { return asyncQueue_.empty() || stopAsync_.load(); });
    }
    
    // Flush all handlers
    std::shared_lock lock(handlersMutex_);
    for (const auto& pair : handlers_) {
        try {
            pair.second->flush();
        } catch (const std::exception&) {
            // Continue flushing other handlers
        }
    }
}

void CoreLogger::shutdown() {
    if (isShutdown_.exchange(true)) {
        return; // Already shutdown
    }
    
    isRunning_.store(false);
    
    // Stop async logging
    stopAsyncLogging();
    
    // Shutdown all handlers
    std::unique_lock lock(handlersMutex_);
    for (auto& pair : handlers_) {
        try {
            pair.second->shutdown();
        } catch (const std::exception&) {
            // Continue shutting down other handlers
        }
    }
    handlers_.clear();
}

bool CoreLogger::isRunning() const {
    return isRunning_.load() && !isShutdown_.load();
}

// === Configuration Helpers ===

void CoreLogger::setLogLevel(LogLevel level) {
    std::unique_lock lock(configMutex_);
    config_.minLevel = level;
}

LogLevel CoreLogger::getLogLevel() const {
    std::shared_lock lock(configMutex_);
    return config_.minLevel;
}

void CoreLogger::setAsyncLogging(bool enable) {
    std::unique_lock lock(configMutex_);
    if (config_.enableAsyncLogging != enable) {
        config_.enableAsyncLogging = enable;
        lock.unlock();
        
        if (enable) {
            startAsyncLogging();
        } else {
            stopAsyncLogging();
        }
    }
}

bool CoreLogger::isAsyncLogging() const {
    std::shared_lock lock(configMutex_);
    return config_.enableAsyncLogging;
}

void CoreLogger::setComponentFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& components, bool whitelist) {
    std::unique_lock lock(configMutex_);
    config_.componentFilter = components;
    config_.filterMode = whitelist;
}

void CoreLogger::clearComponentFilter() {
    std::unique_lock lock(configMutex_);
    config_.componentFilter.clear();
}

void CoreLogger::setJobFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& jobs, bool whitelist) {
    std::unique_lock lock(configMutex_);
    config_.jobFilter = jobs;
    config_.jobFilterMode = whitelist;
}

void CoreLogger::clearJobFilter() {
    std::unique_lock lock(configMutex_);
    config_.jobFilter.clear();
}

// === File Management Integration ===

void CoreLogger::setFileManager(std::shared_ptr<LogFileManager> fileManager) {
    std::unique_lock lock(fileManagerMutex_);
    fileManager_ = fileManager;
}

std::shared_ptr<LogFileManager> CoreLogger::getFileManager() const {
    std::shared_lock lock(fileManagerMutex_);
    return fileManager_;
}

// === Internal Methods ===

void CoreLogger::processLogEntry(const LogEntry& entry) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::shared_lock lock(handlersMutex_);
    
    size_t handledCount = 0;
    for (const auto& pair : handlers_) {
        try {
            if (pair.second->shouldHandle(entry)) {
                pair.second->handle(entry);
                handledCount++;
            }
        } catch (const std::exception&) {
            // Continue processing with other handlers
        }
    }
    
    lock.unlock();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto processingTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    updateMetrics(entry, processingTime);
}

bool CoreLogger::shouldProcess(const LogEntry& entry) const {
    // Check log level
    if (entry.level < config_.minLevel) {
        return false;
    }
    
    // Check component filter
    if (!passesComponentFilter(entry.component)) {
        return false;
    }
    
    // Check job filter
    if (!passesJobFilter(entry.jobId)) {
        return false;
    }
    
    return true;
}

void CoreLogger::asyncWorkerFunction() {
    while (!stopAsync_.load()) {
        std::unique_lock lock(asyncMutex_);
        
        if (asyncCondition_.wait_for(lock, config_.flushInterval, 
                                     [this] { return !asyncQueue_.empty() || stopAsync_.load(); })) {
            
            // Process all queued entries
            while (!asyncQueue_.empty() && !stopAsync_.load()) {
                LogEntry entry = std::move(asyncQueue_.front());
                asyncQueue_.pop();
                metrics_.asyncQueueSize.store(asyncQueue_.size());
                
                lock.unlock();
                processLogEntry(entry);
                lock.lock();
            }
        }
    }
    
    // Process remaining entries during shutdown
    std::unique_lock lock(asyncMutex_);
    while (!asyncQueue_.empty()) {
        LogEntry entry = std::move(asyncQueue_.front());
        asyncQueue_.pop();
        
        lock.unlock();
        processLogEntry(entry);
        lock.lock();
    }
    metrics_.asyncQueueSize.store(0);
}

void CoreLogger::startAsyncLogging() {
    if (asyncStarted_.exchange(true)) {
        return; // Already started
    }
    
    stopAsync_.store(false);
    asyncWorker_ = std::thread(&CoreLogger::asyncWorkerFunction, this);
}

void CoreLogger::stopAsyncLogging() {
    if (!asyncStarted_.exchange(false)) {
        return; // Already stopped
    }
    
    stopAsync_.store(true);
    asyncCondition_.notify_all();
    
    if (asyncWorker_.joinable()) {
        asyncWorker_.join();
    }
}

void CoreLogger::updateMetrics(const LogEntry& entry, std::chrono::microseconds processingTime) {
    if (!config_.enableMetrics) {
        return;
    }
    
    std::lock_guard lock(metricsMutex_);
    
    metrics_.totalMessages.fetch_add(1);
    
    if (entry.level == LogLevel::ERROR || entry.level == LogLevel::FATAL) {
        metrics_.errorCount.fetch_add(1);
    } else if (entry.level == LogLevel::WARN) {
        metrics_.warningCount.fetch_add(1);
    }
    
    // Update average processing time (simple moving average)
    double currentAvg = metrics_.avgProcessingTime.load();
    double newTime = static_cast<double>(processingTime.count());
    double newAvg = (currentAvg == 0.0) ? newTime : (currentAvg + newTime) / 2.0;
    metrics_.avgProcessingTime.store(newAvg);
}

bool CoreLogger::validateHandler(const std::shared_ptr<LogHandler>& handler) const {
    return handler != nullptr && !handler->getId().empty();
}

void CoreLogger::createDefaultHandlers() {
    // This will be implemented when default handlers are needed
    // For now, handlers must be explicitly registered
}

bool CoreLogger::passesComponentFilter(const std::string& component) const {
    if (config_.componentFilter.empty()) {
        return true; // No filter = allow all
    }
    
    bool inFilter = config_.componentFilter.find(component) != config_.componentFilter.end();
    
    if (config_.filterMode) {
        // Whitelist mode: component must be in filter
        return inFilter;
    } else {
        // Blacklist mode: component must NOT be in filter
        return !inFilter;
    }
}

bool CoreLogger::passesJobFilter(const std::string& jobId) const {
    if (config_.jobFilter.empty()) {
        return true; // No filter = allow all
    }
    
    // Empty jobId always passes job filter
    if (jobId.empty()) {
        return true;
    }
    
    bool inFilter = config_.jobFilter.find(jobId) != config_.jobFilter.end();
    
    if (config_.jobFilterMode) {
        // Whitelist mode: job must be in filter
        return inFilter;
    } else {
        // Blacklist mode: job must NOT be in filter
        return !inFilter;
    }
}

// === Backward Compatibility Layer ===

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::configure(const LogConfig& config) {
    // Convert old config to new config format
    CoreLogger::Config newConfig;
    newConfig.minLevel = config.level;
    newConfig.enableAsyncLogging = config.asyncLogging;
    newConfig.componentFilter = config.componentFilter;
    newConfig.enableMetrics = config.includeMetrics;
    
    CoreLogger::getInstance().configure(newConfig);
    
    // Store settings for handler management
    currentFormat_ = config.format;
    currentLogFile_ = config.logFile;
    consoleEnabled_ = config.consoleOutput;
    fileEnabled_ = config.fileOutput;
    
    updateHandlers();
}

void Logger::updateHandlers() {
    auto& core = CoreLogger::getInstance();
    
    // Manage console handler
    if (consoleEnabled_) {
        ensureConsoleHandler();
    } else {
        core.unregisterHandler("console");
    }
    
    // Manage file handler
    if (fileEnabled_) {
        ensureFileHandler();
    } else {
        core.unregisterHandler("file");
    }
}

void Logger::ensureConsoleHandler() {
    // This would create and register a console handler
    // Implementation depends on available handler types
}

void Logger::ensureFileHandler() {
    // This would create and register a file handler
    // Implementation depends on available handler types
}

std::shared_ptr<LogFileManager> Logger::getOrCreateFileManager() {
    auto& core = CoreLogger::getInstance();
    auto fileManager = core.getFileManager();
    
    if (!fileManager) {
        // Create default file manager
        LogFileManagerConfig config;
        config.logDirectory = std::filesystem::path(currentLogFile_).parent_path();
        fileManager = std::make_shared<LogFileManager>(config);
        core.setFileManager(fileManager);
    }
    
    return fileManager;
}

// Delegate all logging methods to CoreLogger
void Logger::log(LogLevel level, const std::string& component, const std::string& message,
                 const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().log(level, component, message, context);
}

void Logger::debug(const std::string& component, const std::string& message,
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().debug(component, message, context);
}

void Logger::info(const std::string& component, const std::string& message,
                  const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().info(component, message, context);
}

void Logger::warn(const std::string& component, const std::string& message,
                  const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().warn(component, message, context);
}

void Logger::error(const std::string& component, const std::string& message,
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().error(component, message, context);
}

void Logger::fatal(const std::string& component, const std::string& message,
                   const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().fatal(component, message, context);
}

// Job-specific methods
void Logger::logForJob(LogLevel level, const std::string& component, const std::string& message,
                       const std::string& jobId,
                       const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().logForJob(level, component, message, jobId, context);
}

void Logger::debugForJob(const std::string& component, const std::string& message, const std::string& jobId,
                         const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().debugForJob(component, message, jobId, context);
}

void Logger::infoForJob(const std::string& component, const std::string& message, const std::string& jobId,
                        const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().infoForJob(component, message, jobId, context);
}

void Logger::warnForJob(const std::string& component, const std::string& message, const std::string& jobId,
                        const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().warnForJob(component, message, jobId, context);
}

void Logger::errorForJob(const std::string& component, const std::string& message, const std::string& jobId,
                         const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().errorForJob(component, message, jobId, context);
}

void Logger::fatalForJob(const std::string& component, const std::string& message, const std::string& jobId,
                         const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().fatalForJob(component, message, jobId, context);
}

// Metrics methods
void Logger::logMetric(const std::string& name, double value, const std::string& unit) {
    CoreLogger::getInstance().logMetric(name, value, unit);
}

void Logger::logPerformance(const std::string& operation, double durationMs,
                            const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& context) {
    CoreLogger::getInstance().logPerformance(operation, durationMs, context);
}

LogMetrics Logger::getMetrics() const {
    auto coreMetrics = CoreLogger::getInstance().getMetrics();
    
    // Convert to old format
    LogMetrics metrics;
    metrics.totalMessages.store(coreMetrics.totalMessages.load());
    metrics.errorCount.store(coreMetrics.errorCount.load());
    metrics.warningCount.store(coreMetrics.warningCount.load());
    metrics.droppedMessages.store(coreMetrics.droppedMessages.load());
    metrics.startTime = coreMetrics.startTime;
    
    return metrics;
}

// Control methods
void Logger::flush() {
    CoreLogger::getInstance().flush();
}

void Logger::shutdown() {
    CoreLogger::getInstance().shutdown();
}

// Configuration helpers
void Logger::setLogLevel(LogLevel level) {
    CoreLogger::getInstance().setLogLevel(level);
}

void Logger::setLogFormat(LogFormat format) {
    currentFormat_ = format;
    // Format is handled by specific handlers
}

void Logger::setLogFile(const std::string& filename) {
    currentLogFile_ = filename;
    if (fileEnabled_) {
        ensureFileHandler();
    }
}

void Logger::enableConsoleOutput(bool enable) {
    consoleEnabled_ = enable;
    updateHandlers();
}

void Logger::enableFileOutput(bool enable) {
    fileEnabled_ = enable;
    updateHandlers();
}

void Logger::enableAsyncLogging(bool enable) {
    CoreLogger::getInstance().setAsyncLogging(enable);
}

void Logger::setComponentFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& components) {
    CoreLogger::getInstance().setComponentFilter(components, false); // Blacklist mode for backward compatibility
}

void Logger::enableRotation(bool enable, size_t maxFileSize, int maxBackupFiles) {
    // This would configure the file manager for rotation
    auto fileManager = getOrCreateFileManager();
    // Configure rotation settings
}

// Streaming methods (placeholder implementations)
void Logger::setWebSocketManager(std::shared_ptr<WebSocketManager> wsManager) {
    // This would set up streaming handlers
}

void Logger::enableRealTimeStreaming(bool enable) {
    // This would enable/disable streaming handlers
}

void Logger::setStreamingJobFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& jobIds) {
    CoreLogger::getInstance().setJobFilter(jobIds, true); // Whitelist mode for streaming
}

void Logger::addStreamingJobFilter(const std::string& jobId) {
    // Add to job filter
}

void Logger::removeStreamingJobFilter(const std::string& jobId) {
    // Remove from job filter
}

void Logger::clearStreamingJobFilter() {
    CoreLogger::getInstance().clearJobFilter();
}

// Historical access methods (delegate to file manager)
void Logger::enableHistoricalAccess(bool enable) {
    // Configure file manager for historical access
}

bool Logger::isHistoricalAccessEnabled() const {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    return fileManager != nullptr; // Simplified check
}

void Logger::setArchiveDirectory(const std::string& directory) {
    auto fileManager = getOrCreateFileManager();
    // Configure archive directory
}

std::string Logger::getArchiveDirectory() const {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    if (fileManager) {
        auto config = fileManager->getConfig();
        return config.archive.archiveDirectory;
    }
    return "logs/archive";
}

void Logger::setMaxQueryResults(size_t maxResults) {
    // Configure query limits
}

size_t Logger::getMaxQueryResults() const {
    return 10000; // Default value
}

void Logger::enableLogIndexing(bool enable) {
    // Configure indexing
}

bool Logger::isLogIndexingEnabled() const {
    return true; // Default value
}

// Helper function to convert string compression format to enum
CompressionType stringToCompressionType(const std::string& format) {
    if (format == "gzip") return CompressionType::GZIP;
    if (format == "zip") return CompressionType::ZIP;
    if (format == "bzip2") return CompressionType::BZIP2;
    if (format == "lz4") return CompressionType::LZ4;
    if (format == "zstd") return CompressionType::ZSTD;
    return CompressionType::GZIP; // Default
}

// Query methods (delegate to file manager)
std::vector<HistoricalLogEntry> Logger::queryLogs(const LogQueryParams& params) {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    if (fileManager) {
        // For now, return empty vector since queryLogs method needs to be implemented
        // This maintains API compatibility while the method is being developed
        return {};
    }
    return {};
}

std::vector<LogFileInfo> Logger::listLogFiles(bool includeArchived) {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    if (fileManager) {
        return fileManager->listLogFiles(includeArchived);
    }
    return {};
}

bool Logger::archiveLogFile(const std::string& filename) {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    if (fileManager) {
        return fileManager->archiveLogFile(filename);
    }
    return false;
}

bool Logger::restoreLogFile(const std::string& filename) {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    if (fileManager) {
        // For now, return false since this method needs to be implemented
        // This maintains API compatibility while the method is being developed
        return false;
    }
    return false;
}

bool Logger::deleteLogFile(const std::string& filename) {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    if (fileManager) {
        // For now, return false since this method needs proper implementation
        // This maintains API compatibility while the method is being developed
        return false;
    }
    return false;
}

bool Logger::compressLogFile(const std::string& filename, const std::string& format) {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    if (fileManager) {
        // For now, return false since this method needs proper implementation
        // This maintains API compatibility while the method is being developed
        return false;
    }
    return false;
}

bool Logger::decompressLogFile(const std::string& filename) {
    auto fileManager = CoreLogger::getInstance().getFileManager();
    if (fileManager) {
        // For now, return false since this method needs proper implementation
        // This maintains API compatibility while the method is being developed
        return false;
    }
    return false;
}
