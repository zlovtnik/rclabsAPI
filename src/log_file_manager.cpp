#include "log_file_manager.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <regex>
#include <iomanip>
#include <sstream>

// RotationPolicy implementations

bool SizeBasedRotationPolicy::shouldRotate(size_t currentSize, 
                                          const std::chrono::system_clock::time_point& lastRotation) const {
    return currentSize >= maxFileSize_;
}

bool TimeBasedRotationPolicy::shouldRotate(size_t currentSize, 
                                          const std::chrono::system_clock::time_point& lastRotation) const {
    auto now = std::chrono::system_clock::now();
    auto timeSinceRotation = std::chrono::duration_cast<std::chrono::hours>(now - lastRotation);
    return timeSinceRotation >= rotationInterval_;
}

bool CombinedRotationPolicy::shouldRotate(size_t currentSize, 
                                         const std::chrono::system_clock::time_point& lastRotation) const {
    return sizePolicy_.shouldRotate(currentSize, lastRotation) || 
           timePolicy_.shouldRotate(currentSize, lastRotation);
}

// LogFileArchiver implementation

LogFileArchiver::LogFileArchiver(const FileConfig& config) : config_(config) {}

bool LogFileArchiver::archiveFile(const std::string& filename, const std::string& archiveDir) {
    if (!std::filesystem::exists(filename)) {
        std::cerr << "File does not exist: " << filename << std::endl;
        return false;
    }
    
    try {
        // Create archive directory if it doesn't exist
        std::filesystem::create_directories(archiveDir);
        
        // Generate archived filename with timestamp
        std::filesystem::path filePath(filename);
        std::string baseName = filePath.filename().string();
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << archiveDir << "/" << baseName << "_" 
           << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        std::string archivedPath = ss.str();
        
        // Move file to archive
        std::filesystem::rename(filename, archivedPath);
        
        // Compress if enabled
        if (config_.compressOldLogs) {
            return compressFile(archivedPath, config_.compressionFormat);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to archive file " << filename << ": " << e.what() << std::endl;
        return false;
    }
}

bool LogFileArchiver::restoreFile(const std::string& filename, const std::string& targetDir) {
    if (!std::filesystem::exists(filename)) {
        std::cerr << "Archived file does not exist: " << filename << std::endl;
        return false;
    }
    
    try {
        std::filesystem::path filePath(filename);
        std::string baseName = filePath.filename().string();
        
        // Remove timestamp suffix if present
        std::regex timestampRegex("_\\d{8}_\\d{6}");
        baseName = std::regex_replace(baseName, timestampRegex, "");
        
        std::string targetPath = targetDir + "/" + baseName;
        
        // Decompress if necessary
        if (isCompressedFile(filename)) {
            std::string tempFile = filename + ".temp";
            if (!decompressFile(filename, tempFile)) {
                return false;
            }
            std::filesystem::rename(tempFile, targetPath);
        } else {
            std::filesystem::copy_file(filename, targetPath);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to restore file " << filename << ": " << e.what() << std::endl;
        return false;
    }
}

bool LogFileArchiver::deleteFile(const std::string& filename) {
    try {
        return std::filesystem::remove(filename);
    } catch (const std::exception& e) {
        std::cerr << "Failed to delete file " << filename << ": " << e.what() << std::endl;
        return false;
    }
}

bool LogFileArchiver::compressFile(const std::string& filename, const std::string& format) {
    if (format == "gzip") {
        return compressWithGzip(filename, filename + ".gz");
    } else if (format == "none") {
        return true; // No compression requested
    }
    
    std::cerr << "Unsupported compression format: " << format << std::endl;
    return false;
}

bool LogFileArchiver::decompressFile(const std::string& inputFile, const std::string& outputFile) {
    if (inputFile.ends_with(".gz")) {
        return decompressWithGzip(inputFile, outputFile);
    }
    
    std::cerr << "Unknown compression format for file: " << inputFile << std::endl;
    return false;
}

std::vector<LogFileInfo> LogFileArchiver::listArchivedFiles(const std::string& archiveDir) const {
    std::vector<LogFileInfo> files;
    
    try {
        if (!std::filesystem::exists(archiveDir)) {
            return files;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(archiveDir)) {
            if (entry.is_regular_file()) {
                LogFileInfo info;
                info.filename = entry.path().filename().string();
                info.fullPath = entry.path().string();
                info.fileSize = entry.file_size();
                
                auto ftime = entry.last_write_time();
                info.lastModified = std::chrono::system_clock::from_time_t(
                    std::chrono::system_clock::to_time_t(
                        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - std::filesystem::file_time_type::clock::now() + 
                            std::chrono::system_clock::now())));
                info.createdTime = info.lastModified;
                info.isCompressed = isCompressedFile(info.filename);
                info.isArchived = true;
                
                files.push_back(info);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error listing archived files: " << e.what() << std::endl;
    }
    
    return files;
}

void LogFileArchiver::cleanupArchivedFiles(const std::string& archiveDir, 
                                          const std::chrono::hours& retentionPeriod) {
    try {
        if (!std::filesystem::exists(archiveDir)) {
            return;
        }
        
        auto now = std::chrono::system_clock::now();
        
        for (const auto& entry : std::filesystem::directory_iterator(archiveDir)) {
            if (entry.is_regular_file()) {
                auto ftime = entry.last_write_time();
                auto fileTime = std::chrono::system_clock::from_time_t(
                    std::chrono::system_clock::to_time_t(
                        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - std::filesystem::file_time_type::clock::now() + 
                            std::chrono::system_clock::now())));
                
                auto age = std::chrono::duration_cast<std::chrono::hours>(now - fileTime);
                
                if (age > retentionPeriod) {
                    std::filesystem::remove(entry);
                    std::cout << "Removed old archived file: " << entry.path() << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error cleaning up archived files: " << e.what() << std::endl;
    }
}

bool LogFileArchiver::compressWithGzip(const std::string& inputFile, const std::string& outputFile) {
    // This is a simplified implementation - in a real system you might want to use
    // a compression library like zlib or call external gzip command
    std::ifstream input(inputFile, std::ios::binary);
    std::ofstream output(outputFile, std::ios::binary);
    
    if (!input.is_open() || !output.is_open()) {
        return false;
    }
    
    // For now, just copy the file and add .gz extension
    // In a real implementation, you would use proper gzip compression
    output << input.rdbuf();
    
    input.close();
    output.close();
    
    // Remove original file after successful compression
    std::filesystem::remove(inputFile);
    
    return true;
}

bool LogFileArchiver::decompressWithGzip(const std::string& inputFile, const std::string& outputFile) {
    // This is a simplified implementation - in a real system you would use
    // a decompression library like zlib
    std::ifstream input(inputFile, std::ios::binary);
    std::ofstream output(outputFile, std::ios::binary);
    
    if (!input.is_open() || !output.is_open()) {
        return false;
    }
    
    // For now, just copy the content
    // In a real implementation, you would use proper gzip decompression
    output << input.rdbuf();
    
    input.close();
    output.close();
    
    return true;
}

std::string LogFileArchiver::getCompressionExtension(const std::string& format) const {
    if (format == "gzip") return ".gz";
    if (format == "zip") return ".zip";
    return "";
}

bool LogFileArchiver::isCompressedFile(const std::string& filename) const {
    return filename.ends_with(".gz") || filename.ends_with(".zip");
}

// LogFileIndexer implementation

LogFileIndexer::LogFileIndexer(const FileConfig& config) 
    : config_(config)
    , indexFilePath_(config.archiveDirectory + "/log_index.txt") {}

void LogFileIndexer::indexFile(const std::string& filename) {
    if (!config_.enableLogIndexing) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(indexMutex_);
    writeIndexEntry(filename);
}

void LogFileIndexer::removeFileIndex(const std::string& filename) {
    if (!config_.enableLogIndexing) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(indexMutex_);
    removeIndexEntry(filename);
}

void LogFileIndexer::rebuildIndex(const std::string& logDir) {
    if (!config_.enableLogIndexing) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(indexMutex_);
    
    std::vector<std::string> entries;
    
    try {
        // Scan log directory
        if (std::filesystem::exists(logDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(logDir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().string();
                    auto time_t = std::chrono::system_clock::to_time_t(
                        std::chrono::system_clock::now());
                    entries.push_back(filename + " " + std::to_string(time_t));
                }
            }
        }
        
        // Scan archive directory
        std::string archiveDir = config_.archiveDirectory;
        if (std::filesystem::exists(archiveDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(archiveDir)) {
                if (entry.is_regular_file() && entry.path().filename() != "log_index.txt") {
                    std::string filename = entry.path().string();
                    auto ftime = entry.last_write_time();
                    auto time_t = std::chrono::system_clock::to_time_t(
                        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - std::filesystem::file_time_type::clock::now() + 
                            std::chrono::system_clock::now()));
                    entries.push_back(filename + " " + std::to_string(time_t));
                }
            }
        }
        
        writeIndexFile(entries);
    } catch (const std::exception& e) {
        std::cerr << "Error rebuilding index: " << e.what() << std::endl;
    }
}

void LogFileIndexer::writeIndexEntry(const std::string& filename) {
    try {
        // Create archive directory if it doesn't exist
        std::filesystem::create_directories(config_.archiveDirectory);
        
        std::ofstream indexFile(indexFilePath_, std::ios::app);
        if (indexFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            indexFile << filename << " " << time_t << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to write index entry: " << e.what() << std::endl;
    }
}

void LogFileIndexer::removeIndexEntry(const std::string& filename) {
    try {
        auto entries = readIndexFile();
        
        // Remove entries containing the filename
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                          [&filename](const std::string& entry) {
                              return entry.find(filename) != std::string::npos;
                          }),
            entries.end());
        
        writeIndexFile(entries);
    } catch (const std::exception& e) {
        std::cerr << "Failed to remove index entry: " << e.what() << std::endl;
    }
}

std::vector<std::string> LogFileIndexer::readIndexFile() const {
    std::vector<std::string> entries;
    
    try {
        std::ifstream indexFile(indexFilePath_);
        if (indexFile.is_open()) {
            std::string line;
            while (std::getline(indexFile, line)) {
                if (!line.empty()) {
                    entries.push_back(line);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to read index file: " << e.what() << std::endl;
    }
    
    return entries;
}

void LogFileIndexer::writeIndexFile(const std::vector<std::string>& entries) const {
    try {
        std::ofstream indexFile(indexFilePath_, std::ios::out);
        if (indexFile.is_open()) {
            for (const auto& entry : entries) {
                indexFile << entry << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to write index file: " << e.what() << std::endl;
    }
}

// LogFileManager implementation

LogFileManager::LogFileManager(const FileConfig& config) 
    : config_(config)
    , currentLogFile_(config.logFile)
    , lastRotationTime_(std::chrono::system_clock::now()) {
    
    // Create default rotation policy based on configuration
    if (config_.enableTimeBasedRotation) {
        setRotationPolicy(std::make_unique<CombinedRotationPolicy>(
            config_.maxFileSize, config_.rotationInterval));
    } else {
        setRotationPolicy(std::make_unique<SizeBasedRotationPolicy>(config_.maxFileSize));
    }
    
    // Initialize utility components
    archiver_ = std::make_unique<LogFileArchiver>(config_);
    indexer_ = std::make_unique<LogFileIndexer>(config_);
    
    // Initialize file if needed
    if (config_.enableFileOutput) {
        initializeFile();
    }
}

LogFileManager::~LogFileManager() {
    close();
}

bool LogFileManager::initializeFile() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    // Create directories if they don't exist
    if (!createDirectories()) {
        std::cerr << "Failed to create log directories" << std::endl;
        return false;
    }
    
    // Close existing stream if open
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
    
    // Open log file for appending
    fileStream_.open(currentLogFile_, std::ios::app);
    if (!fileStream_.is_open()) {
        std::cerr << "Failed to open log file: " << currentLogFile_ << std::endl;
        return false;
    }
    
    // Get current file size
    if (fileExists(currentLogFile_)) {
        currentFileSize_ = getFileSize(currentLogFile_);
    } else {
        currentFileSize_ = 0;
    }
    
    // Write initialization message
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") 
       << "] [INFO ] [LogFileManager] Log file manager initialized: " << currentLogFile_ << std::endl;
    
    std::string initMsg = ss.str();
    fileStream_ << initMsg;
    fileStream_.flush();
    currentFileSize_ += initMsg.length();
    
    // Index the file if indexing is enabled
    if (config_.enableLogIndexing) {
        indexer_->indexFile(currentLogFile_);
    }
    
    return true;
}

void LogFileManager::writeToFile(const std::string& formattedMessage) {
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    if (!fileStream_.is_open()) {
        return;
    }
    
    fileStream_ << formattedMessage << std::endl;
    fileStream_.flush();
    currentFileSize_ += formattedMessage.length() + 1;
    
    // Check if rotation is needed
    if (isRotationNeeded()) {
        performRotation();
    }
}

void LogFileManager::flush() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (fileStream_.is_open()) {
        fileStream_.flush();
    }
}

void LogFileManager::close() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

void LogFileManager::updateConfig(const FileConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    
    // Update rotation policy if needed
    if (config_.enableTimeBasedRotation) {
        setRotationPolicy(std::make_unique<CombinedRotationPolicy>(
            config_.maxFileSize, config_.rotationInterval));
    } else {
        setRotationPolicy(std::make_unique<SizeBasedRotationPolicy>(config_.maxFileSize));
    }
}

void LogFileManager::rotateIfNeeded() {
    if (isRotationNeeded()) {
        std::lock_guard<std::mutex> lock(fileMutex_);
        performRotation();
    }
}

void LogFileManager::setRotationPolicy(std::unique_ptr<RotationPolicy> policy) {
    rotationPolicy_ = std::move(policy);
}

bool LogFileManager::isRotationNeeded() const {
    if (!config_.enableRotation || !rotationPolicy_) {
        return false;
    }
    
    return rotationPolicy_->shouldRotate(currentFileSize_.load(), lastRotationTime_);
}

std::vector<LogFileInfo> LogFileManager::listLogFiles(bool includeArchived) const {
    std::vector<LogFileInfo> files;
    
    // Get current and backup log files
    std::filesystem::path logPath(currentLogFile_);
    std::string logDir = logPath.parent_path().string();
    std::string baseName = logPath.filename().string();
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(logDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                
                // Check if it's a log file (current or backup)
                if (filename == baseName || filename.find(baseName + ".") == 0) {
                    LogFileInfo info = getFileInfo(entry.path().string());
                    files.push_back(info);
                }
            }
        }
        
        // Include archived files if requested
        if (includeArchived && config_.enableHistoricalAccess) {
            auto archivedFiles = archiver_->listArchivedFiles(config_.archiveDirectory);
            files.insert(files.end(), archivedFiles.begin(), archivedFiles.end());
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error listing log files: " << e.what() << std::endl;
    }
    
    // Sort files by creation time (newest first)
    std::sort(files.begin(), files.end(), 
              [](const LogFileInfo& a, const LogFileInfo& b) {
                  return a.createdTime > b.createdTime;
              });
    
    return files;
}

LogFileInfo LogFileManager::getFileInfo(const std::string& filename) const {
    LogFileInfo info;
    info.filename = std::filesystem::path(filename).filename().string();
    info.fullPath = std::filesystem::absolute(filename).string();
    
    if (fileExists(filename)) {
        info.fileSize = getFileSize(filename);
        
        try {
            auto ftime = std::filesystem::last_write_time(filename);
            info.lastModified = std::chrono::system_clock::from_time_t(
                std::chrono::system_clock::to_time_t(
                    std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - std::filesystem::file_time_type::clock::now() + 
                        std::chrono::system_clock::now())));
            info.createdTime = info.lastModified; // Approximation
        } catch (const std::exception&) {
            info.lastModified = std::chrono::system_clock::now();
            info.createdTime = info.lastModified;
        }
        
        // Check if file is compressed
        info.isCompressed = (filename.find(".gz") != std::string::npos) ||
                           (filename.find(".zip") != std::string::npos);
        
        // Check if file is archived
        info.isArchived = filename.find(config_.archiveDirectory) != std::string::npos;
    } else {
        info.fileSize = 0;
        info.lastModified = std::chrono::system_clock::now();
        info.createdTime = info.lastModified;
        info.isCompressed = false;
        info.isArchived = false;
    }
    
    return info;
}

bool LogFileManager::archiveFile(const std::string& filename) {
    return archiver_->archiveFile(filename, config_.archiveDirectory);
}

bool LogFileManager::restoreFile(const std::string& filename) {
    std::filesystem::path logPath(currentLogFile_);
    std::string logDir = logPath.parent_path().string();
    return archiver_->restoreFile(filename, logDir);
}

bool LogFileManager::deleteFile(const std::string& filename) {
    return archiver_->deleteFile(filename);
}

void LogFileManager::cleanupOldFiles() {
    if (!config_.enableAutoCleanup) {
        return;
    }
    
    auto files = listLogFiles(false); // Only current log files, not archived
    
    // Remove old backup files if we have too many
    std::vector<LogFileInfo> backupFiles;
    std::copy_if(files.begin(), files.end(), std::back_inserter(backupFiles),
                 [this](const LogFileInfo& file) {
                     return file.filename.find(std::filesystem::path(currentLogFile_).filename().string() + ".") == 0;
                 });
    
    // Sort by creation time and remove oldest files
    std::sort(backupFiles.begin(), backupFiles.end(),
              [](const LogFileInfo& a, const LogFileInfo& b) {
                  return a.createdTime > b.createdTime;
              });
    
    // Remove files beyond maxBackupFiles
    for (size_t i = config_.maxBackupFiles; i < backupFiles.size(); ++i) {
        try {
            std::filesystem::remove(backupFiles[i].fullPath);
        } catch (const std::exception& e) {
            std::cerr << "Failed to remove old backup file " << backupFiles[i].fullPath 
                      << ": " << e.what() << std::endl;
        }
    }
    
    // Cleanup archived files
    if (config_.enableHistoricalAccess) {
        archiver_->cleanupArchivedFiles(config_.archiveDirectory, config_.retentionPeriod);
    }
}

void LogFileManager::archiveOldLogs() {
    if (!config_.enableHistoricalAccess) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    // Close current file
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
    
    // Archive the current log file
    std::string archivedFile = config_.archiveDirectory + "/" + 
                              std::filesystem::path(currentLogFile_).filename().string() + 
                              "_" + std::to_string(std::chrono::system_clock::to_time_t(
                                  std::chrono::system_clock::now()));
    
    try {
        std::filesystem::rename(currentLogFile_, archivedFile);
        
        // Index the archived file
        if (config_.enableLogIndexing) {
            indexer_->indexFile(archivedFile);
        }
        
        // Compress if enabled
        if (config_.compressOldLogs) {
            archiver_->compressFile(archivedFile, config_.compressionFormat);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to archive log file: " << e.what() << std::endl;
    }
    
    // Reopen log file
    fileStream_.open(currentLogFile_, std::ios::out);
    currentFileSize_ = 0;
    
    if (!fileStream_.is_open()) {
        std::cerr << "Failed to create new log file after archiving: " << currentLogFile_ << std::endl;
    }
}

std::string LogFileManager::getCurrentLogFile() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return currentLogFile_;
}

bool LogFileManager::isFileOpen() const {
    std::lock_guard<std::mutex> lock(fileMutex_);
    return fileStream_.is_open();
}

// Private helper methods

void LogFileManager::performRotation() {
    if (!config_.enableRotation) {
        return;
    }
    
    // Close current file
    fileStream_.close();
    
    // Move backup files
    moveBackupFiles();
    
    // Move current log to .1
    std::string firstBackup = generateBackupFilename(1);
    try {
        if (fileExists(currentLogFile_)) {
            std::filesystem::rename(currentLogFile_, firstBackup);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to move current log file during rotation: " << e.what() << std::endl;
    }
    
    // Create new log file
    fileStream_.open(currentLogFile_, std::ios::out);
    currentFileSize_ = 0;
    lastRotationTime_ = std::chrono::system_clock::now();
    
    if (!fileStream_.is_open()) {
        std::cerr << "Failed to create new log file after rotation: " << currentLogFile_ << std::endl;
    }
    
    // Index the rotated file if needed
    if (config_.enableLogIndexing && fileExists(firstBackup)) {
        indexer_->indexFile(firstBackup);
    }
}

void LogFileManager::moveBackupFiles() {
    // Move existing backup files (from highest to lowest number)
    for (int i = config_.maxBackupFiles - 1; i > 0; i--) {
        std::string oldFile = generateBackupFilename(i);
        std::string newFile = generateBackupFilename(i + 1);
        
        if (fileExists(oldFile)) {
            try {
                if (i == config_.maxBackupFiles - 1) {
                    // Remove the oldest file
                    std::filesystem::remove(newFile);
                }
                std::filesystem::rename(oldFile, newFile);
            } catch (const std::exception& e) {
                std::cerr << "Failed to move backup file during rotation: " << e.what() << std::endl;
            }
        }
    }
}

std::string LogFileManager::generateBackupFilename(int backupNumber) const {
    return currentLogFile_ + "." + std::to_string(backupNumber);
}

bool LogFileManager::fileExists(const std::string& path) const {
    return std::filesystem::exists(path);
}

size_t LogFileManager::getFileSize(const std::string& path) const {
    try {
        return std::filesystem::file_size(path);
    } catch (const std::exception&) {
        return 0;
    }
}

bool LogFileManager::createDirectories() const {
    try {
        // Create log file directory
        std::filesystem::path logPath(currentLogFile_);
        std::filesystem::create_directories(logPath.parent_path());
        
        // Create archive directory if historical access is enabled
        if (config_.enableHistoricalAccess) {
            std::filesystem::create_directories(config_.archiveDirectory);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create directories: " << e.what() << std::endl;
        return false;
    }
}
