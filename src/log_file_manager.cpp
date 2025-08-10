#include "log_file_manager.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <regex>
#include <iomanip>
#include <sstream>

// LogFileManager implementation

LogFileManager::LogFileManager(const LogFileManagerConfig& config)
    : config_(config), maxCacheSize_(config.performance.maxMemoryUsage / 10) {
    // Initialize directory structure
    createDirectoryStructure(config_.logDirectory);
    createDirectoryStructure(config_.archive.archiveDirectory);

    // Initialize utility components
    archiver_ = std::make_unique<LogFileArchiver>(config_.archive);
    indexer_ = std::make_unique<LogFileIndexer>(config_.indexing);
    compressor_ = std::make_unique<LogFileCompressor>();
    validator_ = std::make_unique<LogFileValidator>();

    // Start background maintenance if enabled
    if (config_.enableFileMonitoring) {
        startBackgroundMaintenance();
    }
}

LogFileManager::~LogFileManager() {
    stopBackgroundMaintenance();
    closeAllFiles();
}

bool LogFileManager::updateConfig(const LogFileManagerConfig& config) {
    auto [isValid, errorMessage] = validateConfig(config);
    if (!isValid) {
        return false;
    }

    std::unique_lock lock(configMutex_);
    config_ = config;
    return true;
}

LogFileManagerConfig LogFileManager::getConfig() const {
    std::shared_lock lock(configMutex_);
    return config_;
}

bool LogFileManager::initializeLogFile(const std::string& filename) {
    std::string fullPath = filename;
    if (!std::filesystem::path(filename).is_absolute()) {
        fullPath = config_.logDirectory + "/" + filename;
    }

    if (!createDirectoryStructure(fullPath)) {
        return false;
    }
    
    std::unique_lock lock(filesMutex_);

    auto stream = std::make_unique<std::ofstream>(fullPath, std::ios::app);
    if (!stream->is_open()) {
        return false;
    }

    openFiles_[filename] = std::move(stream);
    fileSizes_[filename] = getFileSize(fullPath);
    fileCreationTimes_[filename] = std::chrono::system_clock::now();
    lastRotationTimes_[filename] = std::chrono::system_clock::now();

    {
        std::unique_lock currentLock(currentFileMutex_);
        if (currentLogFile_.empty()) {
            currentLogFile_ = filename;
        }
    }

    metrics_.totalFilesCreated++;
    return true;
}

size_t LogFileManager::writeToFile(const std::string& data, bool forceFlush) {
    std::shared_lock currentLock(currentFileMutex_);
    if (currentLogFile_.empty()) {
        return 0;
    }
    return writeToFile(currentLogFile_, data, forceFlush);
}

size_t LogFileManager::writeToFile(const std::string& filename, const std::string& data, bool forceFlush) {
    auto start = std::chrono::steady_clock::now();

    // Check if rotation is needed
    if (needsRotation(filename)) {
        rotateLogFile(filename);
    }

    std::unique_lock lock(filesMutex_);

    auto it = openFiles_.find(filename);
    if (it == openFiles_.end()) {
        if (!initializeLogFile(filename)) {
            metrics_.writeErrors++;
            return 0;
        }
        it = openFiles_.find(filename);
    }

    if (!it->second || !it->second->is_open()) {
        metrics_.writeErrors++;
        return 0;
    }

    it->second->write(data.c_str(), data.size());
    if (it->second->fail()) {
        metrics_.writeErrors++;
        return 0;
    }

    // Update metrics and tracking
    fileSizes_[filename] += data.size();
    lastAccessTimes_[filename] = std::chrono::system_clock::now();

    if (forceFlush) {
        it->second->flush();
        metrics_.totalFlushOperations++;
    }

    auto end = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    metrics_.totalWriteOperations++;
    metrics_.totalBytesWritten += data.size();
    updateLatencyMetric("write", latency);

    return data.size();
}

bool LogFileManager::flush() {
    std::shared_lock lock(filesMutex_);

    for (auto& [filename, stream] : openFiles_) {
        if (stream && stream->is_open()) {
            stream->flush();
        }
    }

    metrics_.totalFlushOperations++;
    return true;
}

bool LogFileManager::needsRotation() const {
    std::shared_lock currentLock(currentFileMutex_);
    if (currentLogFile_.empty()) {
        return false;
    }
    return needsRotation(currentLogFile_);
}

bool LogFileManager::needsRotation(const std::string& filename) const {
    return shouldRotateBySize(filename) || shouldRotateByTime(filename);
}

bool LogFileManager::rotateLogFile() {
    std::shared_lock currentLock(currentFileMutex_);
    if (currentLogFile_.empty()) {
        return false;
    }
    return rotateLogFile(currentLogFile_);
}

bool LogFileManager::rotateLogFile(const std::string& filename) {
    std::unique_lock lock(filesMutex_);

    auto it = openFiles_.find(filename);
    if (it == openFiles_.end() || !it->second) {
        return false;
    }
    
    // Close current file
    it->second->close();

    // Generate backup filename
    std::string backupName = generateBackupFileName(filename, 1);

    // Move current file to backup
    std::string fullPath = config_.logDirectory + "/" + filename;
    std::string backupPath = config_.logDirectory + "/" + backupName;

    try {
        std::filesystem::rename(fullPath, backupPath);

        // Create new file
        auto newStream = std::make_unique<std::ofstream>(fullPath, std::ios::out);
        if (!newStream->is_open()) {
            return false;
        }
        
        openFiles_[filename] = std::move(newStream);
        fileSizes_[filename] = 0;
        lastRotationTimes_[filename] = std::chrono::system_clock::now();

        metrics_.totalFilesRotated++;
        return true;

    } catch (const std::exception& e) {
        handleFileError("rotation", filename, e);
        return false;
    }
}

std::vector<LogFileInfo> LogFileManager::listLogFiles(bool includeArchived, bool includeCompressed, const std::string& sortBy) const {
    std::vector<LogFileInfo> files;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(config_.logDirectory)) {
            if (entry.is_regular_file()) {
                LogFileInfo info;
                info.filename = entry.path().filename().string();
                info.fullPath = entry.path().string();
                info.fileSize = entry.file_size();
                info.lastModified = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    std::chrono::system_clock::now()); // Simplified for C++17 compatibility
                info.isCompressed = info.isCompressedFile();

                if (!includeCompressed && info.isCompressed) continue;

                files.push_back(info);
            }
        }
        
        if (includeArchived && std::filesystem::exists(config_.archive.archiveDirectory)) {
            for (const auto& entry : std::filesystem::directory_iterator(config_.archive.archiveDirectory)) {
                if (entry.is_regular_file()) {
                    LogFileInfo info;
                    info.filename = entry.path().filename().string();
                    info.fullPath = entry.path().string();
                    info.fileSize = entry.file_size();
                    info.lastModified = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        std::chrono::system_clock::now()); // Simplified for C++17 compatibility
                    info.isArchived = true;
                    info.isCompressed = info.isCompressedFile();

                    files.push_back(info);
                }
            }
        }
    } catch (const std::exception& e) {
        // Log error but return what we have
    }
    
    return files;
}

size_t LogFileManager::getCurrentFileSize() const {
    std::shared_lock currentLock(currentFileMutex_);
    if (currentLogFile_.empty()) {
        return 0;
    }
    return getFileSize(currentLogFile_);
}

size_t LogFileManager::getFileSize(const std::string& filename) const {
    std::shared_lock lock(filesMutex_);

    auto it = fileSizes_.find(filename);
    if (it != fileSizes_.end()) {
        return it->second;
    }

    // Try to get actual file size
    std::string fullPath = filename;
    if (!std::filesystem::path(filename).is_absolute()) {
        fullPath = config_.logDirectory + "/" + filename;
    }

    try {
        if (std::filesystem::exists(fullPath)) {
            return std::filesystem::file_size(fullPath);
        }
    } catch (const std::exception&) {
        // Return 0 on error
    }

    return 0;
}

bool LogFileManager::closeLogFile() {
    std::shared_lock currentLock(currentFileMutex_);
    if (currentLogFile_.empty()) {
        return false;
    }
    return closeLogFile(currentLogFile_);
}

bool LogFileManager::closeLogFile(const std::string& filename) {
    std::unique_lock lock(filesMutex_);

    auto it = openFiles_.find(filename);
    if (it == openFiles_.end()) {
        return false;
    }

    if (it->second && it->second->is_open()) {
        it->second->close();
    }

    openFiles_.erase(it);
    return true;
}

size_t LogFileManager::closeAllFiles() {
    std::unique_lock lock(filesMutex_);

    size_t closedCount = 0;
    for (auto& [filename, stream] : openFiles_) {
        if (stream && stream->is_open()) {
            stream->close();
            closedCount++;
        }
    }

    openFiles_.clear();
    return closedCount;
}

LogFileMetrics LogFileManager::getMetrics() const {
    std::lock_guard lock(metricsMutex_);
    return metrics_;
}

void LogFileManager::resetMetrics() {
    std::lock_guard lock(metricsMutex_);
    metrics_ = LogFileMetrics{};
}

std::string LogFileManager::getStatus() const {
    std::ostringstream status;
    status << "{"
           << "\"currentFile\":\"" << getCurrentLogFile() << "\","
           << "\"fileCount\":" << listLogFiles().size() << ","
           << "\"totalSize\":" << getTotalLogSize() << ","
           << "\"maintenanceRunning\":" << (maintenanceRunning_.load() ? "true" : "false") << ","
           << "\"healthy\":" << (isHealthy() ? "true" : "false")
           << "}";
    return status.str();
}

bool LogFileManager::isHealthy() const {
    // Basic health checks
    return std::filesystem::exists(config_.logDirectory) &&
           hasRequiredPermissions(config_.logDirectory);
}

std::string LogFileManager::getCurrentLogFile() const {
    std::shared_lock lock(currentFileMutex_);
    return currentLogFile_;
}

bool LogFileManager::startBackgroundMaintenance() {
    if (maintenanceRunning_.load()) {
        return false;
    }
    
    stopMaintenance_.store(false);
    maintenanceThread_ = std::thread(&LogFileManager::maintenanceWorker, this);
    maintenanceRunning_.store(true);
    return true;
}

bool LogFileManager::stopBackgroundMaintenance(std::chrono::seconds timeout) {
    if (!maintenanceRunning_.load()) {
        return true;
    }
    
    stopMaintenance_.store(true);
    maintenanceCondition_.notify_all();

    if (maintenanceThread_.joinable()) {
        maintenanceThread_.join();
    }
    
    maintenanceRunning_.store(false);
    return true;
}

bool LogFileManager::isBackgroundMaintenanceRunning() const {
    return maintenanceRunning_.load();
}

// Private helper methods

bool LogFileManager::createDirectoryStructure(const std::string& filePath) {
    try {
        std::filesystem::path path(filePath);
        if (path.has_filename()) {
            path = path.parent_path();
        }
        return std::filesystem::create_directories(path);
    } catch (const std::exception&) {
        return false;
    }
}

std::string LogFileManager::generateBackupFileName(const std::string& baseFilename, int index) const {
    std::filesystem::path path(baseFilename);
    std::string stem = path.stem().string();
    std::string extension = path.extension().string();

    return stem + "." + std::to_string(index) + extension;
}

bool LogFileManager::shouldRotateBySize(const std::string& filename) const {
    if (!config_.rotation.enabled || config_.rotation.trigger == RotationTrigger::TIME_BASED) {
        return false;
    }

    size_t currentSize = getFileSize(filename);
    return currentSize >= config_.rotation.maxFileSize;
}

bool LogFileManager::shouldRotateByTime(const std::string& filename) const {
    if (!config_.rotation.enabled || config_.rotation.trigger == RotationTrigger::SIZE_BASED) {
        return false;
    }
    
    std::shared_lock lock(filesMutex_);
    auto it = lastRotationTimes_.find(filename);
    if (it == lastRotationTimes_.end()) {
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    auto timeSinceRotation = std::chrono::duration_cast<std::chrono::hours>(now - it->second);
    return timeSinceRotation >= config_.rotation.rotationInterval;
}

size_t LogFileManager::getTotalLogSize(bool includeArchived, bool includeCompressed) const {
    size_t totalSize = 0;
    auto files = listLogFiles(includeArchived, includeCompressed);

    for (const auto& file : files) {
        totalSize += file.fileSize;
    }

    return totalSize;
}

bool LogFileManager::hasRequiredPermissions(const std::string& directory) const {
    try {
        std::filesystem::perms perms = std::filesystem::status(directory).permissions();
        return (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
    } catch (const std::exception&) {
        return false;
    }
}

void LogFileManager::handleFileError(const std::string& operation, const std::string& filename, const std::exception& error) {
    incrementErrorMetric(operation);
    // Log error internally
}

void LogFileManager::incrementErrorMetric(const std::string& errorType) {
    if (errorType == "write") {
        metrics_.writeErrors++;
    } else if (errorType == "rotation") {
        metrics_.rotationErrors++;
    } else if (errorType == "archive") {
        metrics_.archiveErrors++;
    }
}

void LogFileManager::updateLatencyMetric(const std::string& operation, std::chrono::microseconds latency) {
    double latencyMs = latency.count();

    if (operation == "write") {
        uint64_t count = metrics_.totalWriteOperations.load();
        double currentAvg = metrics_.averageWriteLatency.load();
        metrics_.averageWriteLatency.store(calculateMovingAverage(currentAvg, latencyMs, count));
    }
}

double LogFileManager::calculateMovingAverage(double currentAvg, double newValue, uint64_t count) {
    if (count == 0) return newValue;
    return ((currentAvg * (count - 1)) + newValue) / count;
}

void LogFileManager::maintenanceWorker() {
    while (!stopMaintenance_.load()) {
        std::unique_lock lock(maintenanceMutex_);
        maintenanceCondition_.wait_for(lock, std::chrono::seconds(60));

        if (stopMaintenance_.load()) {
            break;
        }
        
        // Perform basic maintenance
        performRotationMaintenance();
        performCleanupMaintenance();
    }
}

void LogFileManager::performRotationMaintenance() {
    auto files = listLogFiles();
    for (const auto& file : files) {
        if (needsRotation(file.filename)) {
            rotateLogFile(file.filename);
        }
    }
}

void LogFileManager::performCleanupMaintenance() {
    // Basic cleanup implementation
    cleanupTempFiles();
}

size_t LogFileManager::cleanupTempFiles() {
    // Simple implementation - remove .tmp files
    size_t cleaned = 0;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(config_.logDirectory)) {
            if (entry.path().extension() == ".tmp") {
                std::filesystem::remove(entry.path());
                cleaned++;
            }
        }
    } catch (const std::exception&) {
        // Ignore errors during cleanup
    }
    return cleaned;
}

std::pair<bool, std::string> LogFileManager::validateConfig(const LogFileManagerConfig& config) const {
    if (config.logDirectory.empty()) {
        return {false, "Log directory cannot be empty"};
    }

    if (config.rotation.maxFileSize == 0) {
        return {false, "Max file size must be greater than 0"};
    }

    return {true, ""};
}

// Utility class implementations

LogFileArchiver::LogFileArchiver(const LogArchivePolicy& policy) : policy_(policy) {}

bool LogFileArchiver::archiveFile(const std::string& sourceFile, const std::string& archiveDir) {
    try {
        std::filesystem::create_directories(archiveDir);
        std::filesystem::path sourcePath(sourceFile);
        std::filesystem::path targetPath = std::filesystem::path(archiveDir) / sourcePath.filename();

        std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

LogFileIndexer::LogFileIndexer(const LogIndexingPolicy& policy) : policy_(policy) {}

bool LogFileIndexer::indexFile(const std::string& logFile) {
    // Basic indexing implementation
    return true;
}

std::vector<HistoricalLogEntry> LogFileIndexer::searchIndex(const LogQueryParams& params) const {
    // Basic search implementation
    return {};
}

bool LogFileManager::archiveLogFile(const std::string& filename) {
    if (!archiver_) {
        return false;
    }

    std::string fullPath = filename;
    if (!std::filesystem::path(filename).is_absolute()) {
        fullPath = config_.logDirectory + "/" + filename;
    }

    if (!std::filesystem::exists(fullPath)) {
        return false;
    }

    try {
        return archiver_->archiveFile(fullPath, config_.archive.archiveDirectory);
    } catch (const std::exception& e) {
        handleFileError("archive", filename, e);
        return false;
    }
}
