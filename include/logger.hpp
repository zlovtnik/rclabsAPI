#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <memory>
#include <chrono>
#include <iomanip>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

class Logger {
public:
    static Logger& getInstance();
    
    void setLogLevel(LogLevel level);
    void setLogFile(const std::string& filename);
    void enableConsoleOutput(bool enable);
    void enableFileOutput(bool enable);
    
    void log(LogLevel level, const std::string& component, const std::string& message);
    void debug(const std::string& component, const std::string& message);
    void info(const std::string& component, const std::string& message);
    void warn(const std::string& component, const std::string& message);
    void error(const std::string& component, const std::string& message);
    void fatal(const std::string& component, const std::string& message);
    
private:
    Logger() = default;
    
    LogLevel currentLevel_ = LogLevel::INFO;
    bool consoleOutput_ = true;
    bool fileOutput_ = false;
    std::string logFile_;
    std::ofstream fileStream_;
    std::mutex logMutex_;
    
    std::string formatTimestamp();
    std::string levelToString(LogLevel level);
    void writeLog(const std::string& formattedMessage);
};

// Convenience macros for logging
#define LOG_DEBUG(component, message) Logger::getInstance().debug(component, message)
#define LOG_INFO(component, message) Logger::getInstance().info(component, message)
#define LOG_WARN(component, message) Logger::getInstance().warn(component, message)
#define LOG_ERROR(component, message) Logger::getInstance().error(component, message)
#define LOG_FATAL(component, message) Logger::getInstance().fatal(component, message)

// Component-specific logging macros
#define CONFIG_LOG_DEBUG(message) LOG_DEBUG("ConfigManager", message)
#define CONFIG_LOG_INFO(message) LOG_INFO("ConfigManager", message)
#define CONFIG_LOG_WARN(message) LOG_WARN("ConfigManager", message)
#define CONFIG_LOG_ERROR(message) LOG_ERROR("ConfigManager", message)

#define DB_LOG_DEBUG(message) LOG_DEBUG("DatabaseManager", message)
#define DB_LOG_INFO(message) LOG_INFO("DatabaseManager", message)
#define DB_LOG_WARN(message) LOG_WARN("DatabaseManager", message)
#define DB_LOG_ERROR(message) LOG_ERROR("DatabaseManager", message)

#define HTTP_LOG_DEBUG(message) LOG_DEBUG("HttpServer", message)
#define HTTP_LOG_INFO(message) LOG_INFO("HttpServer", message)
#define HTTP_LOG_WARN(message) LOG_WARN("HttpServer", message)
#define HTTP_LOG_ERROR(message) LOG_ERROR("HttpServer", message)

#define AUTH_LOG_DEBUG(message) LOG_DEBUG("AuthManager", message)
#define AUTH_LOG_INFO(message) LOG_INFO("AuthManager", message)
#define AUTH_LOG_WARN(message) LOG_WARN("AuthManager", message)
#define AUTH_LOG_ERROR(message) LOG_ERROR("AuthManager", message)

#define ETL_LOG_DEBUG(message) LOG_DEBUG("ETLJobManager", message)
#define ETL_LOG_INFO(message) LOG_INFO("ETLJobManager", message)
#define ETL_LOG_WARN(message) LOG_WARN("ETLJobManager", message)
#define ETL_LOG_ERROR(message) LOG_ERROR("ETLJobManager", message)

#define TRANSFORM_LOG_DEBUG(message) LOG_DEBUG("DataTransformer", message)
#define TRANSFORM_LOG_INFO(message) LOG_INFO("DataTransformer", message)
#define TRANSFORM_LOG_WARN(message) LOG_WARN("DataTransformer", message)
#define TRANSFORM_LOG_ERROR(message) LOG_ERROR("DataTransformer", message)

#define REQUEST_LOG_DEBUG(message) LOG_DEBUG("RequestHandler", message)
#define REQUEST_LOG_INFO(message) LOG_INFO("RequestHandler", message)
#define REQUEST_LOG_WARN(message) LOG_WARN("RequestHandler", message)
#define REQUEST_LOG_ERROR(message) LOG_ERROR("RequestHandler", message)
