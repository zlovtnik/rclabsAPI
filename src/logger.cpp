#include "logger.hpp"
#include <iostream>
#include <iomanip>

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(logMutex_);
    currentLevel_ = level;
}

void Logger::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(logMutex_);
    
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
    
    logFile_ = filename;
    fileStream_.open(logFile_, std::ios::app);
    
    if (!fileStream_.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
        fileOutput_ = false;
    } else {
        fileOutput_ = true;
        // Write startup message
        fileStream_ << "\n=== Logger initialized at " << formatTimestamp() << " ===" << std::endl;
        fileStream_.flush();
    }
}

void Logger::enableConsoleOutput(bool enable) {
    std::lock_guard<std::mutex> lock(logMutex_);
    consoleOutput_ = enable;
}

void Logger::enableFileOutput(bool enable) {
    std::lock_guard<std::mutex> lock(logMutex_);
    fileOutput_ = enable;
}

void Logger::log(LogLevel level, const std::string& component, const std::string& message) {
    if (level < currentLevel_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(logMutex_);
    
    std::ostringstream oss;
    oss << "[" << formatTimestamp() << "] "
        << "[" << levelToString(level) << "] "
        << "[" << component << "] "
        << message;
    
    writeLog(oss.str());
}

void Logger::debug(const std::string& component, const std::string& message) {
    log(LogLevel::DEBUG, component, message);
}

void Logger::info(const std::string& component, const std::string& message) {
    log(LogLevel::INFO, component, message);
}

void Logger::warn(const std::string& component, const std::string& message) {
    log(LogLevel::WARN, component, message);
}

void Logger::error(const std::string& component, const std::string& message) {
    log(LogLevel::ERROR, component, message);
}

void Logger::fatal(const std::string& component, const std::string& message) {
    log(LogLevel::FATAL, component, message);
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

void Logger::writeLog(const std::string& formattedMessage) {
    if (consoleOutput_) {
        std::cout << formattedMessage << std::endl;
    }
    
    if (fileOutput_ && fileStream_.is_open()) {
        fileStream_ << formattedMessage << std::endl;
        fileStream_.flush();
    }
}
