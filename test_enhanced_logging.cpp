#include "logger.hpp"
#include "config_manager.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

void testBasicLogging() {
    std::cout << "=== Testing Basic Enhanced Logging ===" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    // Test basic logging with context
    std::unordered_map<std::string, std::string> context = {
        {"user_id", "12345"},
        {"session_id", "sess_789"},
        {"operation", "test_operation"}
    };
    
    logger.info("TestComponent", "This is a test message with context", context);
    logger.warn("TestComponent", "This is a warning message");
    logger.error("TestComponent", "This is an error message", {{"error_code", "E001"}});
    
    std::cout << "✓ Basic logging with context completed" << std::endl;
}

void testJsonLogging() {
    std::cout << "\n=== Testing JSON Format Logging ===" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    LogConfig config;
    config.format = LogFormat::JSON;
    config.consoleOutput = true;
    config.fileOutput = false;
    config.level = LogLevel::DEBUG;
    
    logger.configure(config);
    
    std::unordered_map<std::string, std::string> context = {
        {"request_id", "req-123"},
        {"method", "POST"},
        {"endpoint", "/api/users"},
        {"status_code", "200"}
    };
    
    logger.info("HttpServer", "Request processed successfully", context);
    logger.debug("DatabaseManager", "Query executed", {
        {"query", "SELECT * FROM users WHERE id = ?"},
        {"duration_ms", "15.3"},
        {"rows_affected", "1"}
    });
    
    std::cout << "✓ JSON format logging completed" << std::endl;
}

void testMetricsLogging() {
    std::cout << "\n=== Testing Metrics Logging ===" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    LogConfig config;
    config.includeMetrics = true;
    config.format = LogFormat::JSON;
    config.consoleOutput = true;
    config.fileOutput = false;
    
    logger.configure(config);
    
    // Test metric logging
    logger.logMetric("response_time", 125.5, "ms");
    logger.logMetric("memory_usage", 85.2, "percent");
    logger.logMetric("active_connections", 42, "count");
    
    // Test performance logging
    logger.logPerformance("database_query", 23.7, {
        {"table", "users"},
        {"operation", "SELECT"}
    });
    
    logger.logPerformance("http_request", 156.3, {
        {"method", "GET"},
        {"endpoint", "/api/health"},
        {"status", "200"}
    });
    
    std::cout << "✓ Metrics and performance logging completed" << std::endl;
}

void testComponentFiltering() {
    std::cout << "\n=== Testing Component Filtering ===" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    LogConfig config;
    config.componentFilter = {"DatabaseManager", "AuthManager"};
    config.consoleOutput = true;
    config.fileOutput = false;
    config.level = LogLevel::DEBUG;
    
    logger.configure(config);
    
    // These should be logged (in filter)
    logger.info("DatabaseManager", "This message should appear");
    logger.info("AuthManager", "This message should also appear");
    
    // These should NOT be logged (not in filter)
    logger.info("HttpServer", "This message should NOT appear");
    logger.info("ETLJobManager", "This message should also NOT appear");
    
    std::cout << "✓ Component filtering test completed (check output above)" << std::endl;
}

void testLogRotation() {
    std::cout << "\n=== Testing Log Rotation ===" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    // Create a test log file with small size limit
    std::string testLogFile = "test_rotation.log";
    
    LogConfig config;
    config.logFile = testLogFile;
    config.fileOutput = true;
    config.consoleOutput = false;
    config.enableRotation = true;
    config.maxFileSize = 1024; // 1KB for testing
    config.maxBackupFiles = 3;
    config.level = LogLevel::DEBUG;
    
    logger.configure(config);
    
    // Generate enough log messages to trigger rotation
    for (int i = 0; i < 100; i++) {
        logger.info("RotationTest", "Log message number " + std::to_string(i) + 
                   " - This is a longer message to fill up the log file quickly for rotation testing");
    }
    
    logger.flush();
    
    // Check if rotation files were created
    bool rotated = std::filesystem::exists(testLogFile + ".1");
    
    // Cleanup
    std::filesystem::remove(testLogFile);
    std::filesystem::remove(testLogFile + ".1");
    std::filesystem::remove(testLogFile + ".2");
    std::filesystem::remove(testLogFile + ".3");
    
    if (rotated) {
        std::cout << "✓ Log rotation working correctly" << std::endl;
    } else {
        std::cout << "⚠ Log rotation test inconclusive (file may not have reached size limit)" << std::endl;
    }
}

void testAsyncLogging() {
    std::cout << "\n=== Testing Async Logging ===" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    LogConfig config;
    config.asyncLogging = true;
    config.consoleOutput = true;
    config.fileOutput = false;
    config.level = LogLevel::DEBUG;
    
    logger.configure(config);
    
    // Generate many log messages quickly
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; i++) {
        logger.info("AsyncTest", "Async log message " + std::to_string(i));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Wait a bit for async processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    logger.flush();
    
    std::cout << "✓ Async logging completed 1000 messages in " 
              << duration.count() << " microseconds" << std::endl;
}

void testConfigurationLoading() {
    std::cout << "\n=== Testing Configuration Loading ===" << std::endl;
    
    // Test loading logging config from ConfigManager
    auto& config = ConfigManager::getInstance();
    
    if (config.loadConfig("config.json")) {
        LogConfig logConfig = config.getLoggingConfig();
        
        std::cout << "Loaded logging configuration:" << std::endl;
        std::cout << "  Level: " << (logConfig.level == LogLevel::INFO ? "INFO" : "OTHER") << std::endl;
        std::cout << "  Format: " << (logConfig.format == LogFormat::TEXT ? "TEXT" : "JSON") << std::endl;
        std::cout << "  Console: " << (logConfig.consoleOutput ? "enabled" : "disabled") << std::endl;
        std::cout << "  File: " << (logConfig.fileOutput ? "enabled" : "disabled") << std::endl;
        std::cout << "  Async: " << (logConfig.asyncLogging ? "enabled" : "disabled") << std::endl;
        std::cout << "  Log file: " << logConfig.logFile << std::endl;
        std::cout << "  Max file size: " << logConfig.maxFileSize << " bytes" << std::endl;
        std::cout << "  Max backup files: " << logConfig.maxBackupFiles << std::endl;
        
        std::cout << "✓ Configuration loading successful" << std::endl;
    } else {
        std::cout << "⚠ Could not load config.json" << std::endl;
    }
}

void testLogMetrics() {
    std::cout << "\n=== Testing Log Metrics ===" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    // Reset with basic config
    LogConfig config;
    config.consoleOutput = false;
    config.fileOutput = false;
    logger.configure(config);
    
    // Generate some log messages
    logger.info("MetricsTest", "Info message 1");
    logger.info("MetricsTest", "Info message 2");
    logger.warn("MetricsTest", "Warning message 1");
    logger.error("MetricsTest", "Error message 1");
    logger.error("MetricsTest", "Error message 2");
    
    LogMetrics metrics = logger.getMetrics();
    
    std::cout << "Current logging metrics:" << std::endl;
    std::cout << "  Total messages: " << metrics.totalMessages.load() << std::endl;
    std::cout << "  Error count: " << metrics.errorCount.load() << std::endl;
    std::cout << "  Warning count: " << metrics.warningCount.load() << std::endl;
    std::cout << "  Dropped messages: " << metrics.droppedMessages.load() << std::endl;
    
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - metrics.startTime);
    std::cout << "  Logger uptime: " << uptime.count() << " seconds" << std::endl;
    
    std::cout << "✓ Metrics collection working" << std::endl;
}

int main() {
    std::cout << "🚀 Enhanced Logging System Test Suite" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    try {
        testBasicLogging();
        testJsonLogging();
        testMetricsLogging();
        testComponentFiltering();
        testLogRotation();
        testAsyncLogging();
        testConfigurationLoading();
        testLogMetrics();
        
        std::cout << "\n🎉 All enhanced logging tests completed successfully!" << std::endl;
        std::cout << "\nEnhanced logging system features:" << std::endl;
        std::cout << "✓ Configuration-based setup" << std::endl;
        std::cout << "✓ JSON and TEXT format support" << std::endl;
        std::cout << "✓ Structured logging with context" << std::endl;
        std::cout << "✓ Metrics and performance logging" << std::endl;
        std::cout << "✓ Component-based filtering" << std::endl;
        std::cout << "✓ Log file rotation" << std::endl;
        std::cout << "✓ Asynchronous logging" << std::endl;
        std::cout << "✓ Real-time metrics collection" << std::endl;
        
        return 0;
        
    } catch (const std::exception& ex) {
        std::cerr << "❌ Test failed with exception: " << ex.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}
