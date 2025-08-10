#include "log_handler.hpp"
#include <iomanip>
#include <sstream>
#include <filesystem>

// Forward declaration to avoid circular dependency
class WebSocketManager;

namespace {
    // ANSI color codes for console output
    const std::string RESET = "\033[0m";
    const std::string RED = "\033[31m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string GREEN = "\033[32m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    const std::string BOLD = "\033[1m";
}

// LogHandler base class implementation
std::string LogHandler::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

std::string LogHandler::formatTimestamp(const std::chrono::system_clock::time_point& timestamp) const {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;
    
    // Use thread-safe localtime alternative
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string LogHandler::escapeJson(const std::string& str) const {
    std::string escaped;
    escaped.reserve(str.length() + str.length() / 4); // Reserve some extra space
    
    for (char c : str) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c < 0x20) {
                    std::ostringstream oss;
                    oss << "\\u" << std::setfill('0') << std::setw(4) << std::hex << static_cast<int>(c);
                    escaped += oss.str();
                } else {
                    escaped += c;
                }
                break;
        }
    }
    return escaped;
}

// FileLogHandler implementation
FileLogHandler::FileLogHandler(const std::string& id, const std::string& filename,
                               Format format, LogLevel minLevel)
    : id_(id), filename_(filename), format_(format), minLevel_(minLevel), fileSize_(0) {
    
    // Create directory if it doesn't exist
    std::filesystem::path filePath(filename);
    std::filesystem::create_directories(filePath.parent_path());
    
    // Open file in append mode
    fileStream_.open(filename_, std::ios::app);
    if (fileStream_.is_open()) {
        // Get current file size
        fileStream_.seekp(0, std::ios::end);
        fileSize_ = fileStream_.tellp();
    }
}

FileLogHandler::~FileLogHandler() {
    shutdown();
}

void FileLogHandler::handle(const LogEntry& entry) {
    if (!shouldHandle(entry) || !isOpen()) {
        return;
    }
    
    std::string formattedMessage;
    if (format_ == Format::JSON) {
        formattedMessage = formatAsJson(entry);
    } else {
        formattedMessage = formatAsText(entry);
    }
    
    writeToFile(formattedMessage);
}

bool FileLogHandler::shouldHandle(const LogEntry& entry) const {
    return entry.level >= minLevel_;
}

void FileLogHandler::flush() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (fileStream_.is_open()) {
        fileStream_.flush();
    }
}

void FileLogHandler::shutdown() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

bool FileLogHandler::isOpen() const {
    std::lock_guard<std::mutex> lock(fileMutex_);
    return fileStream_.is_open();
}

size_t FileLogHandler::getFileSize() const {
    std::lock_guard<std::mutex> lock(fileMutex_);
    return fileSize_;
}

std::string FileLogHandler::formatAsText(const LogEntry& entry) const {
    std::stringstream ss;
    ss << "[" << formatTimestamp(entry.timestamp) << "] "
       << "[" << levelToString(entry.level) << "] "
       << "[" << entry.component << "] ";
    
    if (!entry.jobId.empty()) {
        ss << "[Job: " << entry.jobId << "] ";
    }
    
    ss << entry.message;
    
    if (!entry.context.empty()) {
        ss << " {";
        bool first = true;
        for (const auto& [key, value] : entry.context) {
            if (!first) ss << ", ";
            ss << key << "=" << value;
            first = false;
        }
        ss << "}";
    }
    
    ss << "\n";
    return ss.str();
}

std::string FileLogHandler::formatAsJson(const LogEntry& entry) const {
    std::stringstream ss;
    ss << "{";
    ss << "\"timestamp\":\"" << formatTimestamp(entry.timestamp) << "\",";
    ss << "\"level\":\"" << levelToString(entry.level) << "\",";
    ss << "\"component\":\"" << escapeJson(entry.component) << "\",";
    ss << "\"message\":\"" << escapeJson(entry.message) << "\"";
    
    if (!entry.jobId.empty()) {
        ss << ",\"jobId\":\"" << escapeJson(entry.jobId) << "\"";
    }
    
    if (!entry.context.empty()) {
        ss << ",\"context\":{";
        bool first = true;
        for (const auto& [key, value] : entry.context) {
            if (!first) ss << ",";
            ss << "\"" << escapeJson(key) << "\":\"" << escapeJson(value) << "\"";
            first = false;
        }
        ss << "}";
    }
    
    ss << "}\n";
    return ss.str();
}

void FileLogHandler::writeToFile(const std::string& message) {
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (fileStream_.is_open()) {
        fileStream_ << message;
        fileSize_ += message.length();
    }
}

// ConsoleLogHandler implementation
ConsoleLogHandler::ConsoleLogHandler(const std::string& id, bool useColors,
                                   bool errorToStderr, LogLevel minLevel)
    : id_(id), useColors_(useColors), errorToStderr_(errorToStderr), minLevel_(minLevel) {
}

void ConsoleLogHandler::handle(const LogEntry& entry) {
    if (!shouldHandle(entry)) {
        return;
    }
    
    std::string formattedMessage = formatForConsole(entry);
    std::ostream& output = getOutputStream(entry.level);
    
    std::lock_guard<std::mutex> lock(consoleMutex_);
    output << formattedMessage;
}

bool ConsoleLogHandler::shouldHandle(const LogEntry& entry) const {
    return entry.level >= minLevel_;
}

void ConsoleLogHandler::flush() {
    std::lock_guard<std::mutex> lock(consoleMutex_);
    std::cout.flush();
    std::cerr.flush();
}

std::string ConsoleLogHandler::formatForConsole(const LogEntry& entry) const {
    std::stringstream ss;
    
    if (useColors_) {
        ss << getColorCode(entry.level);
    }
    
    ss << "[" << formatTimestamp(entry.timestamp) << "] "
       << "[" << levelToString(entry.level) << "] "
       << "[" << entry.component << "] ";
    
    if (!entry.jobId.empty()) {
        ss << "[Job: " << entry.jobId << "] ";
    }
    
    ss << entry.message;
    
    if (!entry.context.empty()) {
        ss << " {";
        bool first = true;
        for (const auto& [key, value] : entry.context) {
            if (!first) ss << ", ";
            ss << key << "=" << value;
            first = false;
        }
        ss << "}";
    }
    
    if (useColors_) {
        ss << RESET;
    }
    
    ss << "\n";
    return ss.str();
}

std::string ConsoleLogHandler::getColorCode(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return CYAN;
        case LogLevel::INFO:  return GREEN;
        case LogLevel::WARN:  return YELLOW;
        case LogLevel::ERROR: return RED;
        case LogLevel::FATAL: return BOLD + RED;
        default: return RESET;
    }
}

std::ostream& ConsoleLogHandler::getOutputStream(LogLevel level) const {
    if (errorToStderr_ && (level >= LogLevel::ERROR)) {
        return std::cerr;
    }
    return std::cout;
}

// StreamingLogHandler implementation
StreamingLogHandler::StreamingLogHandler(const std::string& id, 
                                       std::shared_ptr<WebSocketManager> wsManager,
                                       LogLevel minLevel)
    : id_(id), wsManager_(wsManager), minLevel_(minLevel) {
}

void StreamingLogHandler::handle(const LogEntry& entry) {
    if (!shouldHandle(entry) || !wsManager_) {
        return;
    }
    
    if (!shouldStreamEntry(entry)) {
        return;
    }
    
    std::string formattedMessage = formatForStreaming(entry);
    
    // Note: The actual WebSocket broadcasting will be implemented
    // when we integrate with the existing WebSocketManager
    // For now, we'll add a placeholder that can be implemented later
    // wsManager_->broadcastMessage(formattedMessage);
}

bool StreamingLogHandler::shouldHandle(const LogEntry& entry) const {
    return entry.level >= minLevel_;
}

void StreamingLogHandler::flush() {
    // WebSocket connections don't typically need explicit flushing
    // since messages are sent immediately
}

void StreamingLogHandler::shutdown() {
    // Clear the WebSocket manager reference
    wsManager_.reset();
}

void StreamingLogHandler::setJobFilter(const std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>& jobIds) {
    std::lock_guard<std::mutex> lock(filterMutex_);
    jobFilter_ = jobIds;
}

void StreamingLogHandler::addJobFilter(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(filterMutex_);
    jobFilter_.insert(jobId);
}

void StreamingLogHandler::removeJobFilter(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(filterMutex_);
    jobFilter_.erase(jobId);
}

void StreamingLogHandler::clearJobFilter() {
    std::lock_guard<std::mutex> lock(filterMutex_);
    jobFilter_.clear();
}

std::string StreamingLogHandler::formatForStreaming(const LogEntry& entry) const {
    std::stringstream ss;
    ss << "{";
    ss << "\"timestamp\":\"" << formatTimestamp(entry.timestamp) << "\",";
    ss << "\"level\":\"" << levelToString(entry.level) << "\",";
    ss << "\"component\":\"" << escapeJson(entry.component) << "\",";
    ss << "\"message\":\"" << escapeJson(entry.message) << "\"";
    
    if (!entry.jobId.empty()) {
        ss << ",\"jobId\":\"" << escapeJson(entry.jobId) << "\"";
    }
    
    if (!entry.context.empty()) {
        ss << ",\"context\":{";
        bool first = true;
        for (const auto& [key, value] : entry.context) {
            if (!first) ss << ",";
            ss << "\"" << escapeJson(key) << "\":\"" << escapeJson(value) << "\"";
            first = false;
        }
        ss << "}";
    }
    
    ss << "}";
    return ss.str();
}

bool StreamingLogHandler::shouldStreamEntry(const LogEntry& entry) const {
    std::lock_guard<std::mutex> lock(filterMutex_);
    
    // If no job filter is set, stream all entries
    if (jobFilter_.empty()) {
        return true;
    }
    
    // If entry has no job ID but filter is set, don't stream
    if (entry.jobId.empty()) {
        return false;
    }
    
    // Check if entry's job ID is in the filter
    return jobFilter_.find(entry.jobId) != jobFilter_.end();
}
