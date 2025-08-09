#include "logger.hpp"
#include <iostream>

int main() {
    std::cout << "Simple Enhanced Logging Test" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    // Test basic logging
    LOG_INFO("Test", "Basic logging test");
    
    // Test with context
    std::unordered_map<std::string, std::string> context = {
        {"test_key", "test_value"}
    };
    logger.info("Test", "Context logging test", context);
    
    // Test metrics
    LogMetrics metrics = logger.getMetrics();
    std::cout << "Total messages: " << metrics.totalMessages.load() << std::endl;
    
    std::cout << "✅ Simple test completed successfully!" << std::endl;
    return 0;
}
