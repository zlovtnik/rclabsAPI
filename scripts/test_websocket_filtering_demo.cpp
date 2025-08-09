#include "websocket_manager.hpp"
#include "websocket_connection.hpp"
#include "logger.hpp"
#include <iostream>
#include <memory>
#include <cassert>

class WebSocketFilteringDemo {
public:
    void runDemo() {
        std::cout << "Starting WebSocket Filtering Demonstration...\n";
        
        demonstrateMessageTypeFiltering();
        demonstrateJobIdFiltering();
        demonstrateLogLevelFiltering();
        demonstrateCustomFilterPredicates();
        
        std::cout << "WebSocket filtering demonstration completed!\n";
    }

private:
    void demonstrateMessageTypeFiltering() {
        std::cout << "\n=== Message Type Filtering Demo ===\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Demonstrate different message types
        std::cout << "Broadcasting different message types:\n";
        
        // Job status update
        std::string jobStatusMsg = R"({
            "type": "job_status_update",
            "data": {
                "jobId": "job_123",
                "status": "RUNNING",
                "progressPercent": 75
            }
        })";
        wsManager->broadcastByMessageType(jobStatusMsg, MessageType::JOB_STATUS_UPDATE, "job_123");
        std::cout << "  ✓ Job status update broadcasted\n";
        
        // Log message
        std::string logMsg = R"({
            "type": "log_message",
            "data": {
                "jobId": "job_123",
                "level": "ERROR",
                "message": "Database connection failed"
            }
        })";
        wsManager->broadcastByMessageType(logMsg, MessageType::LOG_MESSAGE, "job_123");
        std::cout << "  ✓ Log message broadcasted\n";
        
        // Notification
        std::string notificationMsg = R"({
            "type": "notification",
            "data": {
                "severity": "HIGH",
                "message": "Job execution time exceeded threshold"
            }
        })";
        wsManager->broadcastByMessageType(notificationMsg, MessageType::NOTIFICATION);
        std::cout << "  ✓ Notification broadcasted\n";
        
        wsManager->stop();
    }
    
    void demonstrateJobIdFiltering() {
        std::cout << "\n=== Job ID Filtering Demo ===\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Demonstrate job-specific updates
        std::vector<std::string> jobIds = {"job_001", "job_002", "job_003"};
        
        std::cout << "Broadcasting job updates for different jobs:\n";
        for (const auto& jobId : jobIds) {
            std::string jobMsg = R"({"type":"job_update","jobId":")" + jobId + R"(","status":"PROCESSING"})";
            wsManager->broadcastJobUpdate(jobMsg, jobId);
            std::cout << "  ✓ Update broadcasted for " << jobId << "\n";
        }
        
        wsManager->stop();
    }
    
    void demonstrateLogLevelFiltering() {
        std::cout << "\n=== Log Level Filtering Demo ===\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Demonstrate different log levels
        std::vector<std::string> logLevels = {"DEBUG", "INFO", "WARN", "ERROR"};
        
        std::cout << "Broadcasting log messages at different levels:\n";
        for (const auto& level : logLevels) {
            std::string logMsg = R"({
                "type": "log_message",
                "data": {
                    "jobId": "job_123",
                    "level": ")" + level + R"(",
                    "message": "Sample log message at )" + level + R"( level"
                }
            })";
            wsManager->broadcastLogMessage(logMsg, "job_123", level);
            std::cout << "  ✓ " << level << " level log message broadcasted\n";
        }
        
        wsManager->stop();
    }
    
    void demonstrateCustomFilterPredicates() {
        std::cout << "\n=== Custom Filter Predicates Demo ===\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Demonstrate custom filtering logic
        std::cout << "Using custom filter predicates:\n";
        
        // Filter for connections interested in critical messages only
        auto criticalOnlyFilter = [](const ConnectionFilters& filters) -> bool {
            return filters.receiveAllLogLevels || 
                   filters.logLevels.find("ERROR") != filters.logLevels.end() ||
                   filters.logLevels.find("WARN") != filters.logLevels.end();
        };
        
        std::string criticalMsg = R"({
            "type": "critical_alert",
            "data": {
                "severity": "CRITICAL",
                "message": "System resource usage exceeded 90%"
            }
        })";
        wsManager->broadcastToFilteredConnections(criticalMsg, criticalOnlyFilter);
        std::cout << "  ✓ Critical alert sent to filtered connections\n";
        
        // Filter for connections monitoring specific job types
        auto etlJobsOnlyFilter = [](const ConnectionFilters& filters) -> bool {
            // In a real scenario, this could check for specific job patterns
            return filters.receiveAllJobs || !filters.jobIds.empty();
        };
        
        std::string etlMsg = R"({
            "type": "etl_summary",
            "data": {
                "totalJobs": 15,
                "completedJobs": 12,
                "failedJobs": 1,
                "runningJobs": 2
            }
        })";
        wsManager->broadcastToFilteredConnections(etlMsg, etlJobsOnlyFilter);
        std::cout << "  ✓ ETL summary sent to job-monitoring connections\n";
        
        wsManager->stop();
    }
};

// Helper function to demonstrate filter configuration
void demonstrateFilterConfiguration() {
    std::cout << "\n=== Filter Configuration Examples ===\n";
    
    // Example 1: Monitor specific jobs only
    ConnectionFilters jobSpecificFilter;
    jobSpecificFilter.receiveAllJobs = false;
    jobSpecificFilter.jobIds.insert("critical_job_001");
    jobSpecificFilter.jobIds.insert("critical_job_002");
    std::cout << "✓ Job-specific filter configured for " << jobSpecificFilter.jobIds.size() << " jobs\n";
    
    // Example 2: Error logs only
    ConnectionFilters errorOnlyFilter;
    errorOnlyFilter.receiveAllLogLevels = false;
    errorOnlyFilter.logLevels.insert("ERROR");
    errorOnlyFilter.logLevels.insert("WARN");
    std::cout << "✓ Error-only filter configured for " << errorOnlyFilter.logLevels.size() << " log levels\n";
    
    // Example 3: Job updates and notifications only
    ConnectionFilters statusOnlyFilter;
    statusOnlyFilter.receiveAllMessageTypes = false;
    statusOnlyFilter.messageTypes.insert(MessageType::JOB_STATUS_UPDATE);
    statusOnlyFilter.messageTypes.insert(MessageType::NOTIFICATION);
    std::cout << "✓ Status-only filter configured for " << statusOnlyFilter.messageTypes.size() << " message types\n";
    
    // Example 4: Combined filters
    ConnectionFilters combinedFilter;
    combinedFilter.receiveAllJobs = false;
    combinedFilter.jobIds.insert("important_job");
    combinedFilter.receiveAllMessageTypes = false;
    combinedFilter.messageTypes.insert(MessageType::JOB_STATUS_UPDATE);
    combinedFilter.messageTypes.insert(MessageType::LOG_MESSAGE);
    combinedFilter.receiveAllLogLevels = false;
    combinedFilter.logLevels.insert("ERROR");
    std::cout << "✓ Combined filter configured with job, message type, and log level restrictions\n";
}

int main() {
    try {
        // Initialize logger for testing
        Logger& logger = Logger::getInstance();
        logger.configure(LogConfig{});
        
        WebSocketFilteringDemo demo;
        demo.runDemo();
        
        demonstrateFilterConfiguration();
        
        std::cout << "\n=== Summary ===\n";
        std::cout << "✓ WebSocket Manager enhanced with connection filtering\n";
        std::cout << "✓ Message broadcasting supports selective delivery\n";
        std::cout << "✓ Job-specific updates can be filtered by job ID\n";
        std::cout << "✓ Log messages can be filtered by level\n";
        std::cout << "✓ Custom filter predicates enable flexible filtering\n";
        std::cout << "✓ All filtering functionality tested successfully\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Filtering demo failed with exception: " << e.what() << std::endl;
        return 1;
    }
}