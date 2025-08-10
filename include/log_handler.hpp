#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <mutex>
#include "transparent_string_hash.hpp"

// Forward declaration
class WebSocketManager;

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

/**
 * Structure representing a single log entry with all necessary information
 * for processing and formatting by log handlers.
 */
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string component;
    std::string message;
    std::string jobId;  // Optional job ID for job-specific logging
    std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> context;
    
    LogEntry() = default;
    
    LogEntry(LogLevel lvl, const std::string& comp, const std::string& msg)
        : timestamp(std::chrono::system_clock::now())
        , level(lvl)
        , component(comp)
        , message(msg) {}
        
    LogEntry(LogLevel lvl, const std::string& comp, const std::string& msg, 
             const std::string& job_id,
             const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& ctx = {})
        : timestamp(std::chrono::system_clock::now())
        , level(lvl)
        , component(comp)
        , message(msg)
        , jobId(job_id)
        , context(ctx) {}
};

/**
 * Abstract base class for all log handlers.
 * Defines the interface for polymorphic log output destinations.
 */
class LogHandler {
public:
    virtual ~LogHandler() = default;
    
    /**
     * Process and output a log entry.
     * @param entry The log entry to handle
     */
    virtual void handle(const LogEntry& entry) = 0;
    
    /**
     * Get a unique identifier for this handler.
     * @return Handler identifier string
     */
    virtual std::string getId() const = 0;
    
    /**
     * Determine if this handler should process the given log entry.
     * @param entry The log entry to evaluate
     * @return true if the handler should process this entry
     */
    virtual bool shouldHandle(const LogEntry& entry) const = 0;
    
    /**
     * Flush any buffered output.
     * Default implementation does nothing - derived classes can override.
     */
    virtual void flush() { /* No buffering by default */ }
    
    /**
     * Shutdown the handler and clean up resources.
     * Default implementation does nothing - derived classes can override.
     */
    virtual void shutdown() { /* No cleanup needed by default */ }

protected:
    /**
     * Convert log level to string representation.
     * @param level The log level to convert
     * @return String representation of the log level
     */
    std::string levelToString(LogLevel level) const;
    
    /**
     * Format timestamp to string.
     * @param timestamp The timestamp to format
     * @return Formatted timestamp string
     */
    std::string formatTimestamp(const std::chrono::system_clock::time_point& timestamp) const;
    
    /**
     * Escape special characters for JSON formatting.
     * @param str The string to escape
     * @return Escaped string
     */
    std::string escapeJson(const std::string& str) const;
};

/**
 * Log handler that outputs to a file.
 * Supports both text and JSON formats.
 */
class FileLogHandler : public LogHandler {
public:
    enum class Format { TEXT, JSON };
    
    /**
     * Constructor.
     * @param id Unique identifier for this handler
     * @param filename Path to the log file
     * @param format Output format (TEXT or JSON)
     * @param minLevel Minimum log level to handle
     */
    FileLogHandler(const std::string& id, const std::string& filename, 
                   Format format = Format::TEXT, LogLevel minLevel = LogLevel::DEBUG);
    
    /**
     * Destructor - ensures file is properly closed.
     */
    ~FileLogHandler() override;
    
    void handle(const LogEntry& entry) override;
    std::string getId() const override { return id_; }
    bool shouldHandle(const LogEntry& entry) const override;
    void flush() override;
    void shutdown() override;
    
    /**
     * Check if the file is successfully opened.
     * @return true if file is open and ready for writing
     */
    bool isOpen() const;
    
    /**
     * Get the current file size.
     * @return File size in bytes
     */
    size_t getFileSize() const;

private:
    std::string id_;
    std::string filename_;
    Format format_;
    LogLevel minLevel_;
    std::ofstream fileStream_;
    mutable std::mutex fileMutex_;
    size_t fileSize_;
    
    /**
     * Format log entry as text.
     * @param entry The log entry to format
     * @return Formatted text string
     */
    std::string formatAsText(const LogEntry& entry) const;
    
    /**
     * Format log entry as JSON.
     * @param entry The log entry to format
     * @return Formatted JSON string
     */
    std::string formatAsJson(const LogEntry& entry) const;
    
    /**
     * Write formatted message to file.
     * @param message The message to write
     */
    void writeToFile(const std::string& message);
};

/**
 * Log handler that outputs to console (stdout/stderr).
 * Supports colored output and different streams for different log levels.
 */
class ConsoleLogHandler : public LogHandler {
public:
    /**
     * Constructor.
     * @param id Unique identifier for this handler
     * @param useColors Whether to use ANSI color codes
     * @param errorToStderr Whether to send ERROR and FATAL to stderr
     * @param minLevel Minimum log level to handle
     */
    explicit ConsoleLogHandler(const std::string& id, bool useColors = true, 
                             bool errorToStderr = true, LogLevel minLevel = LogLevel::DEBUG);
    
    void handle(const LogEntry& entry) override;
    std::string getId() const override { return id_; }
    bool shouldHandle(const LogEntry& entry) const override;
    void flush() override;

private:
    std::string id_;
    bool useColors_;
    bool errorToStderr_;
    LogLevel minLevel_;
    mutable std::mutex consoleMutex_;
    
    /**
     * Format log entry for console output.
     * @param entry The log entry to format
     * @return Formatted string
     */
    std::string formatForConsole(const LogEntry& entry) const;
    
    /**
     * Get ANSI color code for log level.
     * @param level The log level
     * @return ANSI color code string
     */
    std::string getColorCode(LogLevel level) const;
    
    /**
     * Get appropriate output stream for log level.
     * @param level The log level
     * @return Reference to output stream
     */
    std::ostream& getOutputStream(LogLevel level) const;
};

/**
 * Log handler that streams log entries to WebSocket connections.
 * Supports filtering by job ID and log level.
 */
class StreamingLogHandler : public LogHandler {
public:
    /**
     * Constructor.
     * @param id Unique identifier for this handler
     * @param wsManager Shared pointer to WebSocket manager
     * @param minLevel Minimum log level to handle
     */
    StreamingLogHandler(const std::string& id, std::shared_ptr<WebSocketManager> wsManager,
                       LogLevel minLevel = LogLevel::DEBUG);
    
    ~StreamingLogHandler() override = default;
    
    void handle(const LogEntry& entry) override;
    std::string getId() const override { return id_; }
    bool shouldHandle(const LogEntry& entry) const override;
    void flush() override;
    void shutdown() override;
    
    /**
     * Set job ID filter for streaming.
     * @param jobIds Set of job IDs to stream (empty = all jobs)
     */
    void setJobFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& jobIds);
    
    /**
     * Add job ID to filter.
     * @param jobId Job ID to add to filter
     */
    void addJobFilter(const std::string& jobId);
    
    /**
     * Remove job ID from filter.
     * @param jobId Job ID to remove from filter
     */
    void removeJobFilter(const std::string& jobId);
    
    /**
     * Clear job filter (stream all jobs).
     */
    void clearJobFilter();

private:
    std::string id_;
    std::shared_ptr<WebSocketManager> wsManager_;
    LogLevel minLevel_;
    std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> jobFilter_;
    mutable std::mutex filterMutex_;
    
    /**
     * Format log entry for streaming.
     * @param entry The log entry to format
     * @return Formatted JSON string for streaming
     */
    std::string formatForStreaming(const LogEntry& entry) const;
    
    /**
     * Check if entry should be streamed based on job filter.
     * @param entry The log entry to check
     * @return true if entry should be streamed
     */
    bool shouldStreamEntry(const LogEntry& entry) const;
};
