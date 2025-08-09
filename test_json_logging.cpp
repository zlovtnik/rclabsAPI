#include "include/logger.hpp"
#include "include/config_manager.hpp"
#include <iostream>
#include <unordered_map>

int main() {
    std::cout << "Testing JSON logging format..." << std::endl;
    
    // Create a specific JSON configuration
    LogConfig jsonConfig;
    jsonConfig.level = LogLevel::DEBUG;
    jsonConfig.format = LogFormat::JSON;
    jsonConfig.consoleOutput = true;
    jsonConfig.fileOutput = false;
    jsonConfig.asyncLogging = false;
    
    Logger& logger = Logger::getInstance();
    logger.configure(jsonConfig);
    
    std::cout << "\nLogging with JSON format:" << std::endl;
    
    // Test basic logging
    logger.info("TestComponent", "This is a test message");
    
    // Test logging with context
    std::unordered_map<std::string, std::string> context = {
        {"user_id", "12345"},
        {"session_id", "abcdef"},
        {"operation", "test_json"}
    };
    logger.info("TestComponent", "Message with context", context);
    
    // Test different log levels
    logger.debug("TestComponent", "Debug message");
    logger.warn("TestComponent", "Warning message");
    logger.error("TestComponent", "Error message");
    
    std::cout << "\nJSON logging test completed!" << std::endl;
    
    return 0;
}
