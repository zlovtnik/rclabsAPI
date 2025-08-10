#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <regex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "transparent_string_hash.hpp"
#include "log_handler.hpp" // For LogLevel enum

// Forward declarations

struct HistoricalLogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string component;
    std::string jobId;
    std::string threadId;
    std::string message;
    std::string filename;
    size_t lineNumber;

    HistoricalLogEntry() = default;
    HistoricalLogEntry(std::chrono::system_clock::time_point ts, LogLevel lvl,
                      const std::string& comp, const std::string& job,
                      const std::string& thread, const std::string& msg,
                      const std::string& file = "", size_t line = 0)
        : timestamp(ts), level(lvl), component(comp), jobId(job),
          threadId(thread), message(msg), filename(file), lineNumber(line) {}
};

struct LogQueryParams {
    std::optional<std::chrono::system_clock::time_point> startTime;
    std::optional<std::chrono::system_clock::time_point> endTime;
    std::optional<LogLevel> minLevel;
    std::optional<LogLevel> maxLevel;
    std::optional<std::string> component;
    std::optional<std::string> jobId;
    std::optional<std::string> threadId;
    std::optional<std::string> searchText;
    bool useRegex = false;
    size_t maxResults = 1000;
    size_t offset = 0;
    std::string sortBy = "timestamp"; // timestamp, level, component
    bool ascending = true;

    LogQueryParams() = default;
};

struct LogFileInfo;

/**
 * @brief Rotation trigger types for log files
 */
enum class RotationTrigger {
    SIZE_BASED,     ///< Rotate when file reaches maximum size
    TIME_BASED,     ///< Rotate based on time intervals
    COMBINED,       ///< Rotate on either size or time limit
    EXTERNAL        ///< Rotation triggered externally
};

/**
 * @brief Compression algorithms supported for log archiving
 */
enum class CompressionType {
    NONE,           ///< No compression
    GZIP,           ///< GNU zip compression (.gz)
    ZIP,            ///< ZIP archive compression (.zip)
    BZIP2,          ///< Bzip2 compression (.bz2)
    LZ4,            ///< LZ4 compression (.lz4)
    ZSTD            ///< Zstandard compression (.zst)
};

/**
 * @brief Log file archiving strategies
 */
enum class ArchiveStrategy {
    DISABLED,       ///< No archiving
    SIZE_BASED,     ///< Archive when directory reaches size limit
    AGE_BASED,      ///< Archive files older than specified age
    COUNT_BASED,    ///< Archive when file count exceeds limit
    COMBINED,       ///< Archive based on multiple criteria
    SMART           ///< AI-driven archiving based on access patterns
};

/**
 * @brief File integrity verification methods
 */
enum class IntegrityMethod {
    NONE,           ///< No integrity checking
    CRC32,          ///< CRC32 checksum
    MD5,            ///< MD5 hash
    SHA256,         ///< SHA-256 hash
    SHA512          ///< SHA-512 hash
};

/**
 * @brief Advanced log file rotation configuration
 */
struct LogRotationPolicy {
    bool enabled = true;
    RotationTrigger trigger = RotationTrigger::SIZE_BASED;
    
    // Size-based rotation
    size_t maxFileSize = 10 * 1024 * 1024; // 10MB
    int maxBackupFiles = 5;
    
    // Time-based rotation
    std::chrono::hours rotationInterval = std::chrono::hours(24);
    std::chrono::system_clock::time_point nextRotationTime = std::chrono::system_clock::now();
    
    // Advanced settings
    bool compressRotatedFiles = false;
    CompressionType compressionType = CompressionType::GZIP;
    bool preserveFilePermissions = true;
    bool atomicRotation = true; // Use atomic operations for rotation
    
    // Custom rotation naming
    std::string backupFilePattern = "{basename}.{index}"; // {basename}.{timestamp}, {basename}.{index}
    bool useTimestampInBackup = false;
    std::string timestampFormat = "%Y%m%d_%H%M%S";
    
    // Rotation triggers
    std::vector<std::chrono::hours> rotationSchedule; // Specific times for rotation
    bool rotateOnStartup = false;
    bool rotateOnShutdown = false;
    
    // Performance optimization
    size_t rotationBufferSize = 64 * 1024; // 64KB buffer for rotation
    bool useMemoryMapping = false; // Use memory-mapped files for large rotations
};

/**
 * @brief Comprehensive log file archiving configuration
 */
struct LogArchivePolicy {
    bool enabled = true;
    ArchiveStrategy strategy = ArchiveStrategy::AGE_BASED;
    
    std::string archiveDirectory = "logs/archive";
    std::string archiveSubdirectoryPattern = "{year}/{month}"; // Directory structure for archives
    
    // Age-based archiving
    std::chrono::hours maxAge = std::chrono::hours(24 * 7); // 7 days
    
    // Size-based archiving
    size_t maxDirectorySize = 1000 * 1024 * 1024; // 1GB
    
    // Count-based archiving
    size_t maxFileCount = 50;
    
    // Smart archiving (access pattern based)
    std::chrono::hours accessThreshold = std::chrono::hours(24 * 3); // Archive if not accessed for 3 days
    size_t accessCountThreshold = 10; // Archive if accessed less than 10 times
    
    // Compression settings
    bool compressOnArchive = true;
    CompressionType compressionType = CompressionType::GZIP;
    int compressionLevel = 6; // 1-9 for gzip/bzip2
    
    // Integrity verification
    IntegrityMethod integrityMethod = IntegrityMethod::SHA256;
    bool verifyIntegrityOnArchive = true;
    bool verifyIntegrityOnRestore = true;
    
    // Metadata preservation
    bool preserveMetadata = true;
    bool preserveAccessTimes = false;
    bool createManifest = true; // Create manifest file with archive contents
    
    // Cleanup settings
    bool enableAutoCleanup = true;
    std::chrono::hours cleanupInterval = std::chrono::hours(24);
    std::chrono::hours archiveRetentionPeriod = std::chrono::hours(24 * 30); // 30 days
    
    // Deduplication
    bool enableDeduplication = false;
    IntegrityMethod deduplicationMethod = IntegrityMethod::SHA256;
    
    // Encryption (future enhancement)
    bool enableEncryption = false;
    std::string encryptionAlgorithm = "AES-256";
    std::string encryptionKeyPath = "";
};

/**
 * @brief Advanced log file indexing configuration
 */
struct LogIndexingPolicy {
    bool enabled = true;
    
    std::string indexDirectory = "logs/index";
    std::string indexFileExtension = ".idx";
    std::string metadataFileExtension = ".meta";
    
    // Indexing strategy
    bool indexByTimestamp = true;
    bool indexByComponent = true;
    bool indexByLogLevel = true;
    bool indexByJobId = true;
    bool indexByThreadId = false;
    bool indexByProcessId = false;
    
    // Full-text search
    bool enableFullTextIndex = false;
    size_t minWordLength = 3;
    std::vector<std::string> excludePatterns; // Regex patterns to exclude from indexing
    std::vector<std::string> stopWords; // Words to exclude from full-text index
    
    // Performance optimization
    size_t indexFlushInterval = 100; // Flush index every N entries
    bool compressIndex = true;
    size_t indexCacheSize = 1024 * 1024; // 1MB index cache
    bool useBloomFilter = true; // Bloom filter for faster negative lookups
    
    // Index maintenance
    std::chrono::hours indexMaintenanceInterval = std::chrono::hours(24);
    bool rebuildCorruptedIndex = true;
    bool optimizeIndexOnStartup = false;
    bool defragmentIndex = true;
    
    // Backup and recovery
    bool createIndexBackups = true;
    int maxIndexBackups = 3;
    std::chrono::hours indexBackupInterval = std::chrono::hours(24 * 7); // Weekly
};

/**
 * @brief Performance and behavior configuration
 */
struct LogPerformanceConfig {
    // Write buffering
    size_t writeBufferSize = 64 * 1024; // 64KB buffer
    bool enableAsyncFlush = true;
    std::chrono::milliseconds flushInterval = std::chrono::milliseconds(1000);
    std::chrono::milliseconds maxFlushDelay = std::chrono::milliseconds(5000);
    
    // Concurrency
    size_t maxConcurrentOperations = 10;
    bool enableOperationQueuing = true;
    size_t operationQueueSize = 1000;
    
    // Memory management
    size_t maxMemoryUsage = 100 * 1024 * 1024; // 100MB
    bool enableMemoryPressureHandling = true;
    double memoryPressureThreshold = 0.8; // 80% of max memory
    
    // I/O optimization
    bool useDirectIO = false; // Bypass OS cache for large files
    bool enableReadAhead = true;
    size_t readAheadSize = 128 * 1024; // 128KB
    
    // Error resilience
    size_t maxRetryAttempts = 3;
    std::chrono::milliseconds retryDelay = std::chrono::milliseconds(100);
    double retryBackoffMultiplier = 2.0;
    bool fallbackToConsole = true;
    bool enableCorruptionRecovery = true;
};

/**
 * @brief Complete configuration for LogFileManager
 */
struct LogFileManagerConfig {
    // Basic file settings
    std::string logDirectory = "logs";
    std::string defaultLogFile = "etlplus.log";
    bool createDirectories = true;
    
    // Policies
    LogRotationPolicy rotation;
    LogArchivePolicy archive;
    LogIndexingPolicy indexing;
    LogPerformanceConfig performance;
    
    // Monitoring and health checks
    bool enableFileMonitoring = true;
    bool reportMetrics = true;
    std::chrono::seconds healthCheckInterval = std::chrono::seconds(30);
    
    // File system settings
    bool enableFileSystemWatcher = false; // Watch for external file changes
    bool enableSpaceMonitoring = true;
    size_t minFreeSpaceBytes = 100 * 1024 * 1024; // 100MB minimum free space
    
    // Security settings
    bool enableFilePermissionChecks = true;
    std::string filePermissions = "644"; // Default file permissions
    std::string directoryPermissions = "755"; // Default directory permissions
    
    // Debugging and diagnostics
    bool enableDetailedLogging = false; // Log file manager operations
    bool enablePerformanceTracing = false;
    std::string diagnosticsLogFile = "logs/file_manager.log";
};

/**
 * @brief Comprehensive statistics for log file operations
 */
struct LogFileMetrics {
    // File operation counters
    std::atomic<uint64_t> totalFilesCreated{0};
    std::atomic<uint64_t> totalFilesRotated{0};
    std::atomic<uint64_t> totalFilesArchived{0};
    std::atomic<uint64_t> totalFilesCompressed{0};
    std::atomic<uint64_t> totalFilesDecompressed{0};
    std::atomic<uint64_t> totalFilesDeleted{0};
    std::atomic<uint64_t> totalFilesRestored{0};
    
    // Data volume counters
    std::atomic<uint64_t> totalBytesWritten{0};
    std::atomic<uint64_t> totalBytesRead{0};
    std::atomic<uint64_t> totalBytesCompressed{0};
    std::atomic<uint64_t> totalBytesArchived{0};
    std::atomic<uint64_t> totalBytesRecovered{0};
    
    // Performance metrics
    std::atomic<uint64_t> totalWriteOperations{0};
    std::atomic<uint64_t> totalReadOperations{0};
    std::atomic<uint64_t> totalFlushOperations{0};
    std::atomic<double> averageWriteLatency{0.0}; // microseconds
    std::atomic<double> averageReadLatency{0.0};  // microseconds
    std::atomic<double> averageFlushLatency{0.0}; // microseconds
    
    // Error counters
    std::atomic<uint64_t> rotationErrors{0};
    std::atomic<uint64_t> archiveErrors{0};
    std::atomic<uint64_t> compressionErrors{0};
    std::atomic<uint64_t> indexingErrors{0};
    std::atomic<uint64_t> writeErrors{0};
    std::atomic<uint64_t> readErrors{0};
    std::atomic<uint64_t> corruptionDetected{0};
    std::atomic<uint64_t> recoveryAttempts{0};
    
    // Timing information
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastRotation = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastArchive = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastMaintenance = std::chrono::steady_clock::now();
    
    // Cache and buffer statistics
    std::atomic<uint64_t> cacheHits{0};
    std::atomic<uint64_t> cacheMisses{0};
    std::atomic<uint64_t> bufferFlushes{0};
    std::atomic<uint64_t> bufferOverflows{0};
    
    // Compression statistics
    std::atomic<double> averageCompressionRatio{0.0};
    std::atomic<uint64_t> compressionTime{0}; // microseconds
    std::atomic<uint64_t> decompressionTime{0}; // microseconds
    
    LogFileMetrics() = default;
    
    // Copy constructor for atomics
    LogFileMetrics(const LogFileMetrics& other) 
        : totalFilesCreated(other.totalFilesCreated.load())
        , totalFilesRotated(other.totalFilesRotated.load())
        , totalFilesArchived(other.totalFilesArchived.load())
        , totalFilesCompressed(other.totalFilesCompressed.load())
        , totalFilesDecompressed(other.totalFilesDecompressed.load())
        , totalFilesDeleted(other.totalFilesDeleted.load())
        , totalFilesRestored(other.totalFilesRestored.load())
        , totalBytesWritten(other.totalBytesWritten.load())
        , totalBytesRead(other.totalBytesRead.load())
        , totalBytesCompressed(other.totalBytesCompressed.load())
        , totalBytesArchived(other.totalBytesArchived.load())
        , totalBytesRecovered(other.totalBytesRecovered.load())
        , totalWriteOperations(other.totalWriteOperations.load())
        , totalReadOperations(other.totalReadOperations.load())
        , totalFlushOperations(other.totalFlushOperations.load())
        , averageWriteLatency(other.averageWriteLatency.load())
        , averageReadLatency(other.averageReadLatency.load())
        , averageFlushLatency(other.averageFlushLatency.load())
        , rotationErrors(other.rotationErrors.load())
        , archiveErrors(other.archiveErrors.load())
        , compressionErrors(other.compressionErrors.load())
        , indexingErrors(other.indexingErrors.load())
        , writeErrors(other.writeErrors.load())
        , readErrors(other.readErrors.load())
        , corruptionDetected(other.corruptionDetected.load())
        , recoveryAttempts(other.recoveryAttempts.load())
        , startTime(other.startTime)
        , lastRotation(other.lastRotation)
        , lastArchive(other.lastArchive)
        , lastMaintenance(other.lastMaintenance)
        , cacheHits(other.cacheHits.load())
        , cacheMisses(other.cacheMisses.load())
        , bufferFlushes(other.bufferFlushes.load())
        , bufferOverflows(other.bufferOverflows.load())
        , averageCompressionRatio(other.averageCompressionRatio.load())
        , compressionTime(other.compressionTime.load())
        , decompressionTime(other.decompressionTime.load()) {}
    
    // Assignment operator for atomics
    LogFileMetrics& operator=(const LogFileMetrics& other) {
        if (this != &other) {
            totalFilesCreated.store(other.totalFilesCreated.load());
            totalFilesRotated.store(other.totalFilesRotated.load());
            totalFilesArchived.store(other.totalFilesArchived.load());
            totalFilesCompressed.store(other.totalFilesCompressed.load());
            totalFilesDecompressed.store(other.totalFilesDecompressed.load());
            totalFilesDeleted.store(other.totalFilesDeleted.load());
            totalFilesRestored.store(other.totalFilesRestored.load());
            totalBytesWritten.store(other.totalBytesWritten.load());
            totalBytesRead.store(other.totalBytesRead.load());
            totalBytesCompressed.store(other.totalBytesCompressed.load());
            totalBytesArchived.store(other.totalBytesArchived.load());
            totalBytesRecovered.store(other.totalBytesRecovered.load());
            totalWriteOperations.store(other.totalWriteOperations.load());
            totalReadOperations.store(other.totalReadOperations.load());
            totalFlushOperations.store(other.totalFlushOperations.load());
            averageWriteLatency.store(other.averageWriteLatency.load());
            averageReadLatency.store(other.averageReadLatency.load());
            averageFlushLatency.store(other.averageFlushLatency.load());
            rotationErrors.store(other.rotationErrors.load());
            archiveErrors.store(other.archiveErrors.load());
            compressionErrors.store(other.compressionErrors.load());
            indexingErrors.store(other.indexingErrors.load());
            writeErrors.store(other.writeErrors.load());
            readErrors.store(other.readErrors.load());
            corruptionDetected.store(other.corruptionDetected.load());
            recoveryAttempts.store(other.recoveryAttempts.load());
            startTime = other.startTime;
            lastRotation = other.lastRotation;
            lastArchive = other.lastArchive;
            lastMaintenance = other.lastMaintenance;
            cacheHits.store(other.cacheHits.load());
            cacheMisses.store(other.cacheMisses.load());
            bufferFlushes.store(other.bufferFlushes.load());
            bufferOverflows.store(other.bufferOverflows.load());
            averageCompressionRatio.store(other.averageCompressionRatio.load());
            compressionTime.store(other.compressionTime.load());
            decompressionTime.store(other.decompressionTime.load());
        }
        return *this;
    }
    
    // Helper methods for calculations
    double getCompressionRatio() const {
        auto compressed = totalBytesCompressed.load();
        auto written = totalBytesWritten.load();
        return written > 0 ? static_cast<double>(compressed) / written : 0.0;
    }
    
    double getErrorRate() const {
        auto totalOps = totalWriteOperations.load() + totalReadOperations.load();
        auto totalErrors = writeErrors.load() + readErrors.load();
        return totalOps > 0 ? static_cast<double>(totalErrors) / totalOps : 0.0;
    }
    
    double getCacheHitRate() const {
        auto hits = cacheHits.load();
        auto total = hits + cacheMisses.load();
        return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }
    
    std::chrono::duration<double> getUptime() const {
        return std::chrono::steady_clock::now() - startTime;
    }
};

// Forward declarations for utility classes
class LogFileArchiver;
class LogFileIndexer;
class LogFileCompressor;
class LogFileValidator;

/**
 * @brief Extended log file information structure
 */
struct LogFileInfo {
    std::string filename;
    std::string fullPath;
    size_t fileSize = 0;
    std::chrono::system_clock::time_point createdTime;
    std::chrono::system_clock::time_point lastModified;
    std::chrono::system_clock::time_point lastAccessed;
    bool isCompressed = false;
    bool isArchived = false;
    bool isIndexed = false;
    bool isCorrupted = false;
    CompressionType compressionType = CompressionType::NONE;
    std::string checksum;
    IntegrityMethod checksumMethod = IntegrityMethod::NONE;
    size_t accessCount = 0;
    std::string permissions;
    std::string owner;
    std::string group;
    
    // Compression statistics
    size_t originalSize = 0;
    double compressionRatio = 0.0;
    
    // File type detection
    bool isRotatedFile() const {
        return filename.find('.') != std::string::npos && 
               (filename.find(".1") != std::string::npos || 
                filename.find(".2") != std::string::npos ||
                std::regex_match(filename, std::regex(R"(.*\.\d+$)")));
    }
    
    bool isCompressedFile() const {
        return (filename.size() >= 3 && filename.substr(filename.size() - 3) == ".gz") ||
               (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".zip") ||
               (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".bz2") ||
               (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".lz4") ||
               (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".zst");
    }
    
    std::string getFileType() const {
        if (isCompressedFile()) {
            if (filename.size() >= 3 && filename.substr(filename.size() - 3) == ".gz") return "GZIP";
            if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".zip") return "ZIP";
            if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".bz2") return "BZIP2";
            if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".lz4") return "LZ4";
            if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".zst") return "ZSTD";
        }
        if (isRotatedFile()) return "ROTATED";
        if (isArchived) return "ARCHIVED";
        return "ACTIVE";
    }
};

/**
 * @brief Enterprise-grade log file management system
 * 
 * The LogFileManager is a comprehensive, thread-safe logging infrastructure component
 * that handles all file operations for the logging system. It provides:
 * 
 * Core Features:
 * - High-performance buffered file I/O with async flushing
 * - Multiple rotation strategies (size, time, combined, external)
 * - Advanced archiving with compression and deduplication
 * - Full-text indexing for rapid log searches
 * - Integrity verification and corruption recovery
 * - Comprehensive metrics and performance monitoring
 * - Memory pressure handling and resource management
 * 
 * Advanced Features:
 * - Smart archiving based on access patterns
 * - Multi-level compression with different algorithms
 * - Atomic operations for reliability
 * - Background maintenance with configurable scheduling
 * - File system monitoring and health checks
 * - Encryption support (future enhancement)
 * - Distributed log management (future enhancement)
 * 
 * Thread Safety:
 * All operations are thread-safe and designed for high-concurrency environments.
 * Uses a combination of shared_mutex, atomic operations, and lock-free algorithms
 * for optimal performance under load.
 * 
 * Error Handling:
 * Comprehensive error handling with automatic recovery, retry mechanisms,
 * and graceful degradation. Maintains operation logs for troubleshooting.
 */
class LogFileManager {
public:
    /**
     * @brief Constructor with comprehensive configuration
     * @param config Configuration for file management operations
     */
    explicit LogFileManager(const LogFileManagerConfig& config = LogFileManagerConfig{});
    
    /**
     * @brief Destructor - ensures proper cleanup and resource release
     */
    ~LogFileManager();
    
    // Disable copy constructor and assignment (resource management)
    LogFileManager(const LogFileManager&) = delete;
    LogFileManager& operator=(const LogFileManager&) = delete;
    
    // Disable move constructor and assignment (due to std::shared_mutex)
    LogFileManager(LogFileManager&&) = delete;
    LogFileManager& operator=(LogFileManager&&) = delete;

    // ========================================================================
    // Configuration Management
    // ========================================================================
    
    /**
     * @brief Update configuration with validation and hot-reload support
     * @param config New configuration to apply
     * @return true if configuration was successfully applied
     */
    bool updateConfig(const LogFileManagerConfig& config);
    
    /**
     * @brief Get current configuration (thread-safe copy)
     * @return Current configuration
     */
    LogFileManagerConfig getConfig() const;
    
    /**
     * @brief Update rotation policy with immediate effect
     * @param policy New rotation policy
     * @return true if policy was successfully applied
     */
    bool updateRotationPolicy(const LogRotationPolicy& policy);
    
    /**
     * @brief Update archive policy with validation
     * @param policy New archive policy
     * @return true if policy was successfully applied
     */
    bool updateArchivePolicy(const LogArchivePolicy& policy);
    
    /**
     * @brief Update indexing policy with index rebuilding if needed
     * @param policy New indexing policy
     * @return true if policy was successfully applied
     */
    bool updateIndexingPolicy(const LogIndexingPolicy& policy);
    
    /**
     * @brief Update performance configuration
     * @param config New performance configuration
     * @return true if configuration was successfully applied
     */
    bool updatePerformanceConfig(const LogPerformanceConfig& config);
    
    /**
     * @brief Validate configuration before applying
     * @param config Configuration to validate
     * @return pair<bool, string> - success flag and error message if any
     */
    std::pair<bool, std::string> validateConfig(const LogFileManagerConfig& config) const;
    
    // ========================================================================
    // File Operations
    // ========================================================================
    
    /**
     * @brief Initialize log file for writing with full setup
     * @param filename Path to the log file
     * @return true if file was successfully initialized
     */
    bool initializeLogFile(const std::string& filename);
    
    /**
     * @brief High-performance buffered write to current log file
     * @param data Data to write
     * @param forceFlush Whether to flush immediately
     * @return Number of bytes written, or 0 on failure
     */
    size_t writeToFile(const std::string& data, bool forceFlush = false);
    
    /**
     * @brief Write data to specific file with automatic file management
     * @param filename Target file
     * @param data Data to write
     * @param forceFlush Whether to flush immediately
     * @return Number of bytes written, or 0 on failure
     */
    size_t writeToFile(const std::string& filename, const std::string& data, bool forceFlush = false);
    
    /**
     * @brief Batch write multiple entries for optimal performance
     * @param entries Vector of data entries to write
     * @param forceFlush Whether to flush after batch
     * @return Number of bytes written total
     */
    size_t writeBatch(const std::vector<std::string>& entries, bool forceFlush = false);
    
    /**
     * @brief Read data from log file with caching
     * @param filename File to read from
     * @param offset Starting offset
     * @param length Number of bytes to read (0 = read all)
     * @return Data read, empty string on failure
     */
    std::string readFromFile(const std::string& filename, size_t offset = 0, size_t length = 0);
    
    /**
     * @brief Stream read data for large files
     * @param filename File to read from
     * @param callback Function called for each chunk
     * @param chunkSize Size of each chunk to read
     * @return true if read completed successfully
     */
    bool streamReadFile(const std::string& filename, 
                       std::function<bool(const std::string&)> callback,
                       size_t chunkSize = 64 * 1024);
    
    /**
     * @brief Flush all pending writes to disk
     * @return true if flush was successful
     */
    bool flush();
    
    /**
     * @brief Flush specific file
     * @param filename File to flush
     * @return true if flush was successful
     */
    bool flush(const std::string& filename);
    
    /**
     * @brief Sync file to disk (fsync)
     * @param filename File to sync
     * @return true if sync was successful
     */
    bool sync(const std::string& filename);
    
    /**
     * @brief Close current log file gracefully
     * @return true if file was successfully closed
     */
    bool closeLogFile();
    
    /**
     * @brief Close specific log file
     * @param filename File to close
     * @return true if file was successfully closed
     */
    bool closeLogFile(const std::string& filename);
    
    /**
     * @brief Close all open files
     * @return Number of files successfully closed
     */
    size_t closeAllFiles();
    
    // ========================================================================
    // Rotation Operations
    // ========================================================================
    
    /**
     * @brief Check if rotation is needed for current file
     * @return true if rotation should be performed
     */
    bool needsRotation() const;
    
    /**
     * @brief Check if rotation is needed for specific file
     * @param filename File to check
     * @return true if rotation should be performed
     */
    bool needsRotation(const std::string& filename) const;
    
    /**
     * @brief Perform atomic log rotation for current file
     * @return true if rotation was successful
     */
    bool rotateLogFile();
    
    /**
     * @brief Perform atomic log rotation for specific file
     * @param filename File to rotate
     * @return true if rotation was successful
     */
    bool rotateLogFile(const std::string& filename);
    
    /**
     * @brief Force immediate rotation regardless of policies
     * @return true if rotation was successful
     */
    bool forceRotation();
    
    /**
     * @brief Force immediate rotation for specific file
     * @param filename File to rotate
     * @return true if rotation was successful
     */
    bool forceRotation(const std::string& filename);
    
    /**
     * @brief Schedule rotation at specific time
     * @param filename File to rotate
     * @param rotationTime When to perform rotation
     * @return true if rotation was scheduled
     */
    bool scheduleRotation(const std::string& filename, 
                         const std::chrono::system_clock::time_point& rotationTime);
    
    /**
     * @brief Cancel scheduled rotation
     * @param filename File rotation to cancel
     * @return true if cancellation was successful
     */
    bool cancelScheduledRotation(const std::string& filename);
    
    // ========================================================================
    // Archive Operations
    // ========================================================================
    
    /**
     * @brief Check if archiving is needed based on policies
     * @return true if archiving should be performed
     */
    bool needsArchiving() const;
    
    /**
     * @brief Archive specified log file with integrity verification
     * @param filename File to archive
     * @return true if archiving was successful
     */
    bool archiveLogFile(const std::string& filename);
    
    /**
     * @brief Archive multiple files in batch for efficiency
     * @param filenames Files to archive
     * @return Number of files successfully archived
     */
    size_t archiveFiles(const std::vector<std::string>& filenames);
    
    /**
     * @brief Archive all eligible log files based on policies
     * @return Number of files successfully archived
     */
    size_t archiveEligibleFiles();
    
    /**
     * @brief Restore archived file back to active logs
     * @param archivedFilename Name of archived file
     * @param targetFilename Target filename (optional)
     * @return true if restore was successful
     */
    bool restoreArchivedFile(const std::string& archivedFilename, 
                           const std::string& targetFilename = "");
    
    /**
     * @brief Create archive snapshot of current state
     * @param snapshotName Name for the snapshot
     * @return true if snapshot was created
     */
    bool createArchiveSnapshot(const std::string& snapshotName);
    
    /**
     * @brief Restore from archive snapshot
     * @param snapshotName Name of snapshot to restore
     * @return true if restore was successful
     */
    bool restoreFromSnapshot(const std::string& snapshotName);
    
    // ========================================================================
    // Compression Operations
    // ========================================================================
    
    /**
     * @brief Compress log file with specified algorithm
     * @param filename File to compress
     * @param compressionType Type of compression to use
     * @param compressionLevel Compression level (1-9)
     * @return true if compression was successful
     */
    bool compressLogFile(const std::string& filename, 
                        CompressionType compressionType = CompressionType::GZIP,
                        int compressionLevel = 6);
    
    /**
     * @brief Decompress log file
     * @param compressedFilename Compressed file to decompress
     * @param outputFilename Output filename (optional)
     * @return true if decompression was successful
     */
    bool decompressLogFile(const std::string& compressedFilename,
                          const std::string& outputFilename = "");
    
    /**
     * @brief Compress all eligible files based on policies
     * @return Number of files successfully compressed
     */
    size_t compressEligibleFiles();
    
    /**
     * @brief Estimate compression ratio for file
     * @param filename File to analyze
     * @param compressionType Compression type to estimate for
     * @return Estimated compression ratio
     */
    double estimateCompressionRatio(const std::string& filename, 
                                   CompressionType compressionType) const;
    
    
    // ========================================================================
    // File Listing and Information
    // ========================================================================
    
    /**
     * @brief List all log files with comprehensive information
     * @param includeArchived Whether to include archived files
     * @param includeCompressed Whether to include compressed files
     * @param sortBy Sort criteria ("name", "size", "date", "type")
     * @return Vector of detailed log file information
     */
    std::vector<LogFileInfo> listLogFiles(bool includeArchived = false, 
                                         bool includeCompressed = false,
                                         const std::string& sortBy = "date") const;
    
    /**
     * @brief Get comprehensive information about specific log file
     * @param filename File to get information about
     * @return File information, or nullopt if file not found
     */
    std::optional<LogFileInfo> getLogFileInfo(const std::string& filename) const;
    
    /**
     * @brief Get current log file size with caching
     * @return Size in bytes
     */
    size_t getCurrentFileSize() const;
    
    /**
     * @brief Get size of specific log file
     * @param filename File to check
     * @return Size in bytes, or 0 if file doesn't exist
     */
    size_t getFileSize(const std::string& filename) const;
    
    /**
     * @brief Get total size of all log files
     * @param includeArchived Whether to include archived files
     * @param includeCompressed Whether to include compressed files
     * @return Total size in bytes
     */
    size_t getTotalLogSize(bool includeArchived = false, bool includeCompressed = false) const;
    
    /**
     * @brief Get directory usage statistics
     * @param directory Directory to analyze
     * @return Usage statistics (total size, file count, free space)
     */
    std::tuple<size_t, size_t, size_t> getDirectoryUsage(const std::string& directory) const;
    
    // ========================================================================
    // File Management Operations
    // ========================================================================
    
    /**
     * @brief Delete specific log file with optional secure deletion
     * @param filename File to delete
     * @param secureDelete Whether to securely overwrite before deletion
     * @return true if deletion was successful
     */
    bool deleteLogFile(const std::string& filename, bool secureDelete = false);
    
    /**
     * @brief Delete multiple files in batch
     * @param filenames Files to delete
     * @param secureDelete Whether to securely overwrite before deletion
     * @return Number of files successfully deleted
     */
    size_t deleteLogFiles(const std::vector<std::string>& filenames, bool secureDelete = false);
    
    /**
     * @brief Delete old log files based on retention policy
     * @return Number of files deleted
     */
    size_t deleteOldLogFiles();
    
    /**
     * @brief Clean up temporary and backup files
     * @return Number of files cleaned up
     */
    size_t cleanupTempFiles();
    
    /**
     * @brief Perform comprehensive maintenance operation
     * This includes rotation, archiving, compression, cleanup, and optimization
     * @return true if maintenance completed successfully
     */
    bool performMaintenance();
    
    /**
     * @brief Verify integrity of log files
     * @param filenames Files to verify (empty = all files)
     * @return Map of filename to verification result
     */
    std::unordered_map<std::string, bool> verifyFileIntegrity(
        const std::vector<std::string>& filenames = {}) const;
    
    /**
     * @brief Repair corrupted log files if possible
     * @param filename File to repair
     * @return true if repair was successful or file was not corrupted
     */
    bool repairCorruptedFile(const std::string& filename);
    
    // ========================================================================
    // Search and Query Operations
    // ========================================================================
    
    /**
     * @brief Search for log entries matching comprehensive criteria
     * @param params Detailed query parameters
     * @return Vector of matching log entries
     */
    std::vector<HistoricalLogEntry> searchLogEntries(const LogQueryParams& params) const;
    
    /**
     * @brief High-performance text search across log files
     * @param searchText Text to search for (supports regex)
     * @param maxResults Maximum number of results
     * @param includeArchived Whether to search archived files
     * @param useRegex Whether to treat searchText as regex
     * @return Vector of matching log entries
     */
    std::vector<HistoricalLogEntry> searchText(const std::string& searchText, 
                                             size_t maxResults = 100,
                                             bool includeArchived = false,
                                             bool useRegex = false) const;
    
    /**
     * @brief Get log entries from specific time range with pagination
     * @param startTime Start of time range
     * @param endTime End of time range
     * @param maxResults Maximum number of results per page
     * @param offset Offset for pagination
     * @return Vector of log entries in time range
     */
    std::vector<HistoricalLogEntry> getLogEntriesInTimeRange(
        const std::chrono::system_clock::time_point& startTime,
        const std::chrono::system_clock::time_point& endTime,
        size_t maxResults = 1000,
        size_t offset = 0) const;
    
    /**
     * @brief Get log statistics for analysis
     * @param startTime Start time for analysis
     * @param endTime End time for analysis
     * @return Map of component/level to count statistics
     */
    std::unordered_map<std::string, std::unordered_map<std::string, uint64_t>> 
        getLogStatistics(const std::chrono::system_clock::time_point& startTime,
                        const std::chrono::system_clock::time_point& endTime) const;
    
    // ========================================================================
    // Indexing Operations
    // ========================================================================
    
    /**
     * @brief Rebuild index for specific file with progress tracking
     * @param filename File to index
     * @param progressCallback Optional progress callback (percent complete)
     * @return true if indexing was successful
     */
    bool rebuildIndex(const std::string& filename, 
                     std::function<void(double)> progressCallback = nullptr);
    
    /**
     * @brief Rebuild all log file indexes
     * @param progressCallback Optional progress callback (percent complete)
     * @return Number of files successfully indexed
     */
    size_t rebuildAllIndexes(std::function<void(double)> progressCallback = nullptr);
    
    /**
     * @brief Optimize indexes for better search performance
     * @return true if optimization was successful
     */
    bool optimizeIndexes();
    
    /**
     * @brief Get index statistics and health information
     * @return Map of filename to index statistics
     */
    std::unordered_map<std::string, std::unordered_map<std::string, uint64_t>> getIndexStatistics() const;
    
    // ========================================================================
    // Status and Monitoring
    // ========================================================================
    
    /**
     * @brief Get comprehensive status of file manager
     * @return Detailed status information in JSON-like format
     */
    std::string getStatus() const;
    
    /**
     * @brief Get comprehensive metrics with performance data
     * @return Current metrics structure
     */
    LogFileMetrics getMetrics() const;
    
    /**
     * @brief Reset metrics counters and timers
     */
    void resetMetrics();
    
    /**
     * @brief Comprehensive health check of all systems
     * @return Health status with detailed information
     */
    std::pair<bool, std::string> getHealthStatus() const;
    
    /**
     * @brief Check if file manager is operating normally
     * @return true if all systems are healthy
     */
    bool isHealthy() const;
    
    /**
     * @brief Get current active log file path
     * @return Path to current active log file
     */
    std::string getCurrentLogFile() const;
    
    /**
     * @brief Get next scheduled rotation time
     * @return Time point when next rotation will occur
     */
    std::chrono::system_clock::time_point getNextRotationTime() const;
    
    /**
     * @brief Get current memory usage of file manager
     * @return Memory usage in bytes
     */
    size_t getMemoryUsage() const;
    
    /**
     * @brief Get performance statistics for monitoring
     * @return Map of metric names to values
     */
    std::unordered_map<std::string, double> getPerformanceStats() const;
    
    // ========================================================================
    // Async Operations Control
    // ========================================================================
    
    /**
     * @brief Start background maintenance thread with full monitoring
     * @return true if thread started successfully
     */
    bool startBackgroundMaintenance();
    
    /**
     * @brief Stop background maintenance thread gracefully
     * @param timeout Maximum time to wait for shutdown
     * @return true if thread stopped successfully
     */
    bool stopBackgroundMaintenance(std::chrono::seconds timeout = std::chrono::seconds(30));
    
    /**
     * @brief Check if background maintenance is running
     * @return true if maintenance thread is active
     */
    bool isBackgroundMaintenanceRunning() const;
    
    /**
     * @brief Force immediate maintenance cycle
     * @return true if maintenance completed successfully
     */
    bool triggerImmediateMaintenance();
    
    /**
     * @brief Set maintenance schedule
     * @param schedule Map of operation types to intervals
     * @return true if schedule was set successfully
     */
    bool setMaintenanceSchedule(const std::unordered_map<std::string, std::chrono::seconds>& schedule);

private:
    // ========================================================================
    // Configuration and State Management
    // ========================================================================
    
    LogFileManagerConfig config_;
    mutable std::shared_mutex configMutex_;
    
    // File management state
    std::unordered_map<std::string, std::unique_ptr<std::ofstream>> openFiles_;
    std::unordered_map<std::string, size_t> fileSizes_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> fileCreationTimes_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> lastAccessTimes_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> lastRotationTimes_;
    mutable std::shared_mutex filesMutex_;
    
    std::string currentLogFile_;
    mutable std::shared_mutex currentFileMutex_;

    // ========================================================================
    // Utility Components
    // ========================================================================
    
    std::unique_ptr<LogFileArchiver> archiver_;
    std::unique_ptr<LogFileIndexer> indexer_;
    std::unique_ptr<LogFileCompressor> compressor_;
    std::unique_ptr<LogFileValidator> validator_;
    
    // ========================================================================
    // Background Operations and Threading
    // ========================================================================
    
    std::thread maintenanceThread_;
    std::condition_variable maintenanceCondition_;
    std::mutex maintenanceMutex_;
    std::atomic<bool> stopMaintenance_{false};
    std::atomic<bool> maintenanceRunning_{false};
    
    // Operation scheduling
    std::priority_queue<std::pair<std::chrono::system_clock::time_point, std::function<void()>>> scheduledOperations_;
    std::mutex scheduleMutex_;
    
    // ========================================================================
    // Performance and Metrics
    // ========================================================================
    
    LogFileMetrics metrics_;
    mutable std::mutex metricsMutex_;
    
    // Write buffering and caching
    std::unordered_map<std::string, std::string> writeBuffers_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastFlushTimes_;
    mutable std::mutex bufferMutex_;
    
    // Read cache
    struct CacheEntry {
        std::string data;
        std::chrono::steady_clock::time_point timestamp;
        size_t accessCount;
    };
    std::unordered_map<std::string, CacheEntry> readCache_;
    mutable std::mutex cacheMutex_;
    size_t maxCacheSize_;
    
    // ========================================================================
    // Helper Methods - File Operations
    // ========================================================================
    
    bool createDirectoryStructure(const std::string& filePath);
    bool validateFilePath(const std::string& filePath) const;
    std::string generateBackupFileName(const std::string& baseFilename, int index) const;
    std::string generateTimestampedFileName(const std::string& baseFilename) const;
    bool ensureFilePermissions(const std::string& filePath) const;
    bool lockFile(const std::string& filePath);
    bool unlockFile(const std::string& filePath);
    
    // ========================================================================
    // Helper Methods - Rotation
    // ========================================================================
    
    bool shouldRotateBySize(const std::string& filename) const;
    bool shouldRotateByTime(const std::string& filename) const;
    bool performSizeBasedRotation(const std::string& filename);
    bool performTimeBasedRotation(const std::string& filename);
    bool performAtomicRotation(const std::string& filename);
    void updateNextRotationTime();
    void scheduleNextRotation(const std::string& filename);
    
    // ========================================================================
    // Helper Methods - Archive
    // ========================================================================
    
    bool shouldArchiveByAge(const std::string& filename) const;
    bool shouldArchiveBySize() const;
    bool shouldArchiveByCount() const;
    bool shouldArchiveByAccessPattern(const std::string& filename) const;
    std::vector<std::string> findEligibleFilesForArchive() const;
    bool createArchiveManifest(const std::vector<std::string>& archivedFiles);
    
    // ========================================================================
    // Helper Methods - Maintenance and Background Operations
    // ========================================================================
    
    void maintenanceWorker();
    void performRotationMaintenance();
    void performArchiveMaintenance();
    void performCleanupMaintenance();
    void performIndexMaintenance();
    void performIntegrityChecks();
    void performCacheOptimization();
    void executeScheduledOperations();
    
    // ========================================================================
    // Helper Methods - Error Handling and Recovery
    // ========================================================================
    
    bool retryOperation(std::function<bool()> operation, const std::string& operationName);
    void handleFileError(const std::string& operation, const std::string& filename, 
                        const std::exception& error);
    bool recoverFromCorruption(const std::string& filename);
    void logInternalError(const std::string& message, const std::string& context = "");
    
    // ========================================================================
    // Helper Methods - Buffer and Cache Management
    // ========================================================================
    
    void flushBuffer(const std::string& filename);
    void flushAllBuffers();
    bool shouldFlushBuffer(const std::string& filename) const;
    void evictOldCacheEntries();
    void updateCacheEntry(const std::string& key, const std::string& data);
    std::optional<std::string> getCachedData(const std::string& key);
    
    // ========================================================================
    // Helper Methods - Metrics and Performance
    // ========================================================================
    
    void updateMetrics(const std::string& operation, size_t bytesProcessed = 0, 
                      std::chrono::microseconds latency = std::chrono::microseconds(0));
    void incrementErrorMetric(const std::string& errorType);
    void updateLatencyMetric(const std::string& operation, std::chrono::microseconds latency);
    double calculateMovingAverage(double currentAvg, double newValue, uint64_t count);
    
    // ========================================================================
    // Helper Methods - Validation and Security
    // ========================================================================
    
    bool isValidLogFile(const std::string& filename) const;
    bool hasRequiredPermissions(const std::string& directory) const;
    std::string sanitizeFilename(const std::string& filename) const;
    bool validateFileIntegrity(const std::string& filename, IntegrityMethod method) const;
    std::string calculateChecksum(const std::string& filename, IntegrityMethod method) const;
    bool verifyChecksum(const std::string& filename, const std::string& expectedChecksum, 
                       IntegrityMethod method) const;
    
    // ========================================================================
    // Helper Methods - System Integration
    // ========================================================================
    
    size_t getAvailableDiskSpace(const std::string& path) const;
    bool checkMemoryPressure() const;
    void handleMemoryPressure();
    std::string formatBytes(size_t bytes) const;
    std::string formatDuration(std::chrono::seconds duration) const;
    std::chrono::system_clock::time_point parseTimeString(const std::string& timeStr) const;
};

// ============================================================================
// Utility Classes
// ============================================================================

/**
 * @brief Advanced log file archiving operations
 */
class LogFileArchiver {
public:
    explicit LogFileArchiver(const LogArchivePolicy& policy);
    ~LogFileArchiver() = default;
    
    // Core archiving operations
    bool archiveFile(const std::string& sourceFile, const std::string& archiveDir);
    bool archiveFiles(const std::vector<std::string>& sourceFiles, const std::string& archiveDir);
    bool restoreFile(const std::string& archivedFile, const std::string& targetFile);
    
    // Policy-based operations
    std::vector<std::string> findEligibleFiles(const std::string& logDir, 
                                              const LogArchivePolicy& policy) const;
    bool cleanupOldArchives(const std::string& archiveDir, const LogArchivePolicy& policy);
    
    // Manifest operations
    bool createManifest(const std::vector<std::string>& archivedFiles, const std::string& manifestPath);
    std::vector<std::string> readManifest(const std::string& manifestPath);
    
    // Integrity operations
    bool verifyArchiveIntegrity(const std::string& archiveFile);
    std::string calculateArchiveChecksum(const std::string& archiveFile, IntegrityMethod method);
    
private:
    LogArchivePolicy policy_;
    std::mutex operationMutex_;
    
    bool createArchiveDirectory(const std::string& archiveDir);
    std::string generateArchivePath(const std::string& sourceFile, const std::string& archiveDir);
    bool preserveFileMetadata(const std::string& sourceFile, const std::string& archiveFile);
};

/**
 * @brief High-performance log file indexing operations
 */
class LogFileIndexer {
public:
    explicit LogFileIndexer(const LogIndexingPolicy& policy);
    ~LogFileIndexer() = default;
    
    // Core indexing operations
    bool indexFile(const std::string& logFile);
    bool removeIndex(const std::string& logFile);
    std::vector<HistoricalLogEntry> searchIndex(const LogQueryParams& params) const;
    
    // Index maintenance
    bool optimizeIndex();
    bool rebuildIndex(const std::string& logFile);
    bool rebuildAllIndexes(const std::string& logDirectory);
    
    // Index statistics
    std::unordered_map<std::string, uint64_t> getIndexStatistics() const;
    bool verifyIndexIntegrity(const std::string& indexFile) const;
    
private:
    LogIndexingPolicy policy_;
    mutable std::shared_mutex indexMutex_;
    
    struct IndexEntry {
        std::chrono::system_clock::time_point timestamp;
        LogLevel level;
        std::string component;
        std::string jobId;
        std::string filename;
        size_t lineNumber;
        size_t fileOffset;
        uint32_t checksum;
    };
    
    std::vector<IndexEntry> loadIndex(const std::string& indexFile) const;
    bool saveIndex(const std::string& indexFile, const std::vector<IndexEntry>& entries);
    std::string getIndexFilePath(const std::string& logFile) const;
    bool createFullTextIndex(const std::string& logFile);
    std::vector<std::string> tokenizeText(const std::string& text) const;
};

/**
 * @brief Multi-algorithm log file compression operations
 */
class LogFileCompressor {
public:
    LogFileCompressor() = default;
    ~LogFileCompressor() = default;
    
    // Core compression operations
    bool compressFile(const std::string& sourceFile, const std::string& targetFile, 
                     CompressionType type, int level = 6);
    bool decompressFile(const std::string& compressedFile, const std::string& targetFile);
    
    // Utility operations
    CompressionType detectCompressionType(const std::string& filename) const;
    std::string getCompressedExtension(CompressionType type) const;
    double getCompressionRatio(const std::string& originalFile, const std::string& compressedFile) const;
    size_t estimateCompressedSize(const std::string& filename, CompressionType type) const;
    
    // Performance operations
    bool compressInMemory(const std::string& data, std::string& compressedData, CompressionType type);
    bool decompressInMemory(const std::string& compressedData, std::string& data, CompressionType type);
    
private:
    mutable std::mutex compressionMutex_;
    
    // Algorithm-specific implementations
    bool compressGzip(const std::string& sourceFile, const std::string& targetFile, int level);
    bool compressZip(const std::string& sourceFile, const std::string& targetFile, int level);
    bool compressBzip2(const std::string& sourceFile, const std::string& targetFile, int level);
    bool compressLZ4(const std::string& sourceFile, const std::string& targetFile);
    bool compressZstd(const std::string& sourceFile, const std::string& targetFile, int level);
    
    bool decompressGzip(const std::string& sourceFile, const std::string& targetFile);
    bool decompressZip(const std::string& sourceFile, const std::string& targetFile);
    bool decompressBzip2(const std::string& sourceFile, const std::string& targetFile);
    bool decompressLZ4(const std::string& sourceFile, const std::string& targetFile);
    bool decompressZstd(const std::string& sourceFile, const std::string& targetFile);
};

/**
 * @brief Log file validation and integrity checking
 */
class LogFileValidator {
public:
    LogFileValidator() = default;
    ~LogFileValidator() = default;
    
    // Validation operations
    bool validateFile(const std::string& filename) const;
    bool validateFormat(const std::string& filename) const;
    bool validateIntegrity(const std::string& filename, IntegrityMethod method) const;
    
    // Repair operations
    bool repairFile(const std::string& filename);
    bool recoverPartialFile(const std::string& corruptedFile, const std::string& recoveredFile);
    
    // Checksum operations
    std::string calculateChecksum(const std::string& filename, IntegrityMethod method) const;
    bool verifyChecksum(const std::string& filename, const std::string& expectedChecksum, 
                       IntegrityMethod method) const;
    
private:
    std::string calculateCRC32(const std::string& filename) const;
    std::string calculateMD5(const std::string& filename) const;
    std::string calculateSHA256(const std::string& filename) const;
    std::string calculateSHA512(const std::string& filename) const;
    
    bool isValidLogLine(const std::string& line) const;
    bool isRecoverableLine(const std::string& line) const;
};
