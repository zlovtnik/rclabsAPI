#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <filesystem>
#include "transparent_string_hash.hpp"

// Forward declarations and type definitions
enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

/**
 * Configuration for file management operations
 */
struct FileConfig {
    std::string logFile = "logs/etlplus.log";
    std::string archiveDirectory = "logs/archive";
    bool enableFileOutput = false;
    size_t maxFileSize = 10 * 1024 * 1024; // 10MB
    int maxBackupFiles = 5;
    bool enableRotation = true;
    
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
    
    // Historical access
    bool enableHistoricalAccess = true;
    bool enableLogIndexing = true;
    size_t maxQueryResults = 10000;
};

/**
 * Log file information structure
 */
struct LogFileInfo {
    std::string filename;
    std::string fullPath;
    size_t fileSize;
    std::chrono::system_clock::time_point createdTime;
    std::chrono::system_clock::time_point lastModified;
    bool isCompressed;
    bool isArchived;
};

/**
 * Abstract base class for rotation policies
 */
class RotationPolicy {
public:
    virtual ~RotationPolicy() = default;
    virtual bool shouldRotate(size_t currentSize, 
                             const std::chrono::system_clock::time_point& lastRotation) const = 0;
    virtual std::string getPolicyName() const = 0;
};

/**
 * Size-based rotation policy
 */
class SizeBasedRotationPolicy : public RotationPolicy {
public:
    explicit SizeBasedRotationPolicy(size_t maxSize) : maxFileSize_(maxSize) {}
    
    bool shouldRotate(size_t currentSize, 
                     const std::chrono::system_clock::time_point& lastRotation) const override;
    std::string getPolicyName() const override { return "SizeBased"; }
    
private:
    size_t maxFileSize_;
};

/**
 * Time-based rotation policy
 */
class TimeBasedRotationPolicy : public RotationPolicy {
public:
    explicit TimeBasedRotationPolicy(std::chrono::hours interval) 
        : rotationInterval_(interval) {}
    
    bool shouldRotate(size_t currentSize, 
                     const std::chrono::system_clock::time_point& lastRotation) const override;
    std::string getPolicyName() const override { return "TimeBased"; }
    
private:
    std::chrono::hours rotationInterval_;
};

/**
 * Combined rotation policy (size OR time)
 */
class CombinedRotationPolicy : public RotationPolicy {
public:
    CombinedRotationPolicy(size_t maxSize, std::chrono::hours interval)
        : sizePolicy_(maxSize), timePolicy_(interval) {}
    
    bool shouldRotate(size_t currentSize, 
                     const std::chrono::system_clock::time_point& lastRotation) const override;
    std::string getPolicyName() const override { return "Combined"; }
    
private:
    SizeBasedRotationPolicy sizePolicy_;
    TimeBasedRotationPolicy timePolicy_;
};

/**
 * Forward declarations for utility classes
 */
class LogFileArchiver;
class LogFileIndexer;

/**
 * LogFileManager handles core file operations for the logging system.
 * 
 * Responsibilities:
 * - File creation, writing, and rotation
 * - Basic file management operations
 * - Coordination with archiver and indexer components
 */
class LogFileManager {
public:
    explicit LogFileManager(const FileConfig& config);
    ~LogFileManager();
    
    // Core file operations
    bool initializeFile();
    void writeToFile(const std::string& formattedMessage);
    void flush();
    void close();
    
    // Configuration management
    void updateConfig(const FileConfig& config);
    const FileConfig& getConfig() const { return config_; }
    
    // Rotation management
    void rotateIfNeeded();
    void setRotationPolicy(std::unique_ptr<RotationPolicy> policy);
    bool isRotationNeeded() const;
    
    // File listing and information
    std::vector<LogFileInfo> listLogFiles(bool includeArchived = false) const;
    LogFileInfo getFileInfo(const std::string& filename) const;
    
    // Archive operations (delegated to archiver)
    bool archiveFile(const std::string& filename);
    bool restoreFile(const std::string& filename);
    bool deleteFile(const std::string& filename);
    
    // Cleanup operations
    void cleanupOldFiles();
    void archiveOldLogs();
    
    // Status and metrics
    size_t getCurrentFileSize() const { return currentFileSize_.load(); }
    std::string getCurrentLogFile() const;
    bool isFileOpen() const;
    std::chrono::system_clock::time_point getLastRotationTime() const { return lastRotationTime_; }
    
private:
    // Configuration
    FileConfig config_;
    mutable std::mutex configMutex_;
    
    // File handling
    std::ofstream fileStream_;
    std::string currentLogFile_;
    std::atomic<size_t> currentFileSize_{0};
    mutable std::mutex fileMutex_;
    
    // Rotation management
    std::unique_ptr<RotationPolicy> rotationPolicy_;
    std::chrono::system_clock::time_point lastRotationTime_;
    
    // Utility components
    std::unique_ptr<LogFileArchiver> archiver_;
    std::unique_ptr<LogFileIndexer> indexer_;
    
    // Helper methods
    void performRotation();
    void moveBackupFiles();
    std::string generateBackupFilename(int backupNumber) const;
    bool fileExists(const std::string& path) const;
    size_t getFileSize(const std::string& path) const;
    bool createDirectories() const;
};

/**
 * LogFileArchiver handles archive and compression operations
 */
class LogFileArchiver {
public:
    explicit LogFileArchiver(const FileConfig& config);
    
    // Archive operations
    bool archiveFile(const std::string& filename, const std::string& archiveDir);
    bool restoreFile(const std::string& filename, const std::string& targetDir);
    bool deleteFile(const std::string& filename);
    
    // Compression operations
    bool compressFile(const std::string& filename, const std::string& format = "gzip");
    bool decompressFile(const std::string& inputFile, const std::string& outputFile);
    
    // Listing operations
    std::vector<LogFileInfo> listArchivedFiles(const std::string& archiveDir) const;
    
    // Cleanup operations
    void cleanupArchivedFiles(const std::string& archiveDir, 
                             const std::chrono::hours& retentionPeriod);

private:
    FileConfig config_;
    
    // Compression utilities
    bool compressWithGzip(const std::string& inputFile, const std::string& outputFile);
    bool decompressWithGzip(const std::string& inputFile, const std::string& outputFile);
    std::string getCompressionExtension(const std::string& format) const;
    bool isCompressedFile(const std::string& filename) const;
};

/**
 * LogFileIndexer handles index management for historical log access
 */
class LogFileIndexer {
public:
    explicit LogFileIndexer(const FileConfig& config);
    
    // Index management
    void indexFile(const std::string& filename);
    void removeFileIndex(const std::string& filename);
    void rebuildIndex(const std::string& logDir);
    
private:
    FileConfig config_;
    std::string indexFilePath_;
    mutable std::mutex indexMutex_;
    
    // Index utilities
    void writeIndexEntry(const std::string& filename);
    void removeIndexEntry(const std::string& filename);
    std::vector<std::string> readIndexFile() const;
    void writeIndexFile(const std::vector<std::string>& entries) const;
};
