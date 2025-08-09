#include "include/logger.hpp"
#include "include/config_manager.hpp"
#include <iostream>
#include <unordered_map>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Testing async logging performance..." << std::endl;
    
    // Test async logging
    LogConfig asyncConfig;
    asyncConfig.level = LogLevel::DEBUG;
    asyncConfig.format = LogFormat::TEXT;
    asyncConfig.consoleOutput = true;
    asyncConfig.fileOutput = false;
    asyncConfig.asyncLogging = true;
    
    Logger& logger = Logger::getInstance();
    logger.configure(asyncConfig);
    
    std::cout << "\nTesting async logging with 1000 messages:" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Log 1000 messages to test async performance
    for (int i = 0; i < 1000; ++i) {
        std::unordered_map<std::string, std::string> context = {
            {"iteration", std::to_string(i)},
            {"batch", std::to_string(i / 100)}
        };
        logger.info("AsyncTest", "Message " + std::to_string(i), context);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Async logging of 1000 messages took: " << duration.count() << "ms" << std::endl;
    
    // Get metrics
    LogMetrics metrics = logger.getMetrics();
    std::cout << "Total messages: " << metrics.totalMessages << std::endl;
    std::cout << "Error count: " << metrics.errorCount << std::endl;
    std::cout << "Warning count: " << metrics.warningCount << std::endl;
    
    // Wait a bit for async processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    logger.flush();
    
    std::cout << "\nAsync logging test completed!" << std::endl;
    
    return 0;
}
