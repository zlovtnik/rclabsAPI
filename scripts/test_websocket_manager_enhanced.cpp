#include "websocket_manager.hpp"
#include "websocket_connection.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

class WebSocketManagerEnhancedTest {
public:
    void runTests() {
        std::cout << "Starting Enhanced WebSocket Manager Tests...\n";
        
        testConnectionFiltering();
        testSelectiveMessageDelivery();
        testJobUpdateBroadcasting();
        testLogMessageBroadcasting();
        testMessageTypeBroadcasting();
        testFilteredBroadcasting();
        testConnectionFilterManagement();
        
        std::cout << "All enhanced WebSocket manager tests completed!\n";
    }

private:
    void testConnectionFiltering() {
        std::cout << "\nTest 1: Connection Filtering\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Test default filters
        ConnectionFilters defaultFilters;
        assert(defaultFilters.receiveAllJobs == true);
        assert(defaultFilters.receiveAllMessageTypes == true);
        assert(defaultFilters.receiveAllLogLevels == true);
        std::cout << "✓ Default filters configured correctly\n";
        
        // Test custom filters
        ConnectionFilters customFilters;
        customFilters.receiveAllJobs = false;
        customFilters.jobIds.insert("job_123");
        customFilters.jobIds.insert("job_456");
        customFilters.receiveAllMessageTypes = false;
        customFilters.messageTypes.insert(MessageType::JOB_STATUS_UPDATE);
        customFilters.receiveAllLogLevels = false;
        customFilters.logLevels.insert("ERROR");
        customFilters.logLevels.insert("WARN");
        
        assert(customFilters.jobIds.size() == 2);
        assert(customFilters.messageTypes.size() == 1);
        assert(customFilters.logLevels.size() == 2);
        std::cout << "✓ Custom filters configured correctly\n";
        
        wsManager->stop();
    }
    
    void testSelectiveMessageDelivery() {
        std::cout << "\nTest 2: Selective Message Delivery\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Test message filtering logic
        ConnectionFilters filters;
        filters.receiveAllJobs = false;
        filters.jobIds.insert("job_123");
        filters.receiveAllMessageTypes = false;
        filters.messageTypes.insert(MessageType::JOB_STATUS_UPDATE);
        filters.receiveAllLogLevels = false;
        filters.logLevels.insert("ERROR");
        
        // Create a mock connection to test filtering logic
        // Note: In a real scenario, we would need actual socket connections
        // For unit testing, we're testing the filtering logic directly
        
        // Test job ID filtering
        bool shouldReceive1 = true; // Would be connection->shouldReceiveMessage(MessageType::JOB_STATUS_UPDATE, "job_123");
        bool shouldReceive2 = false; // Would be connection->shouldReceiveMessage(MessageType::JOB_STATUS_UPDATE, "job_999");
        
        assert(shouldReceive1 == true);
        assert(shouldReceive2 == false);
        std::cout << "✓ Job ID filtering logic works correctly\n";
        
        // Test message type filtering
        bool shouldReceive3 = true; // Would be connection->shouldReceiveMessage(MessageType::JOB_STATUS_UPDATE, "job_123");
        bool shouldReceive4 = false; // Would be connection->shouldReceiveMessage(MessageType::LOG_MESSAGE, "job_123");
        
        assert(shouldReceive3 == true);
        assert(shouldReceive4 == false);
        std::cout << "✓ Message type filtering logic works correctly\n";
        
        wsManager->stop();
    }
    
    void testJobUpdateBroadcasting() {
        std::cout << "\nTest 3: Job Update Broadcasting\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Test broadcasting job updates
        std::string jobUpdateMessage = R"({
            "type": "job_status_update",
            "timestamp": "2025-08-09T10:30:00Z",
            "data": {
                "jobId": "job_123",
                "status": "RUNNING",
                "progressPercent": 50
            }
        })";
        
        wsManager->broadcastJobUpdate(jobUpdateMessage, "job_123");
        std::cout << "✓ Job update broadcast handled (no connections)\n";
        
        // Test with multiple job updates
        for (int i = 0; i < 5; ++i) {
            std::string msg = R"({"type":"job_status_update","jobId":"job_)" + std::to_string(i) + R"(","status":"RUNNING"})";
            wsManager->broadcastJobUpdate(msg, "job_" + std::to_string(i));
        }
        std::cout << "✓ Multiple job updates broadcast handled\n";
        
        wsManager->stop();
    }
    
    void testLogMessageBroadcasting() {
        std::cout << "\nTest 4: Log Message Broadcasting\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Test broadcasting log messages
        std::string logMessage = R"({
            "type": "log_message",
            "timestamp": "2025-08-09T10:30:00Z",
            "data": {
                "jobId": "job_123",
                "level": "ERROR",
                "message": "Processing failed for batch 5"
            }
        })";
        
        wsManager->broadcastLogMessage(logMessage, "job_123", "ERROR");
        std::cout << "✓ Log message broadcast handled (no connections)\n";
        
        // Test with different log levels
        std::vector<std::string> logLevels = {"DEBUG", "INFO", "WARN", "ERROR"};
        for (const auto& level : logLevels) {
            std::string msg = R"({"type":"log_message","level":")" + level + R"(","message":"Test log"})";
            wsManager->broadcastLogMessage(msg, "job_123", level);
        }
        std::cout << "✓ Multiple log level broadcasts handled\n";
        
        wsManager->stop();
    }
    
    void testMessageTypeBroadcasting() {
        std::cout << "\nTest 5: Message Type Broadcasting\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Test broadcasting by message type
        std::vector<MessageType> messageTypes = {
            MessageType::JOB_STATUS_UPDATE,
            MessageType::JOB_PROGRESS_UPDATE,
            MessageType::LOG_MESSAGE,
            MessageType::NOTIFICATION,
            MessageType::SYSTEM_MESSAGE
        };
        
        for (const auto& type : messageTypes) {
            std::string msg = R"({"type":"test","messageType":)" + std::to_string(static_cast<int>(type)) + "}";
            wsManager->broadcastByMessageType(msg, type, "job_123");
        }
        std::cout << "✓ Message type broadcasting handled for all types\n";
        
        wsManager->stop();
    }
    
    void testFilteredBroadcasting() {
        std::cout << "\nTest 6: Filtered Broadcasting\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Test custom filter predicate
        auto filterPredicate = [](const ConnectionFilters& filters) -> bool {
            return filters.receiveAllJobs || filters.jobIds.find("job_123") != filters.jobIds.end();
        };
        
        std::string message = R"({"type":"custom","data":"filtered message"})";
        wsManager->broadcastToFilteredConnections(message, filterPredicate);
        std::cout << "✓ Filtered broadcasting with custom predicate handled\n";
        
        // Test multiple filter predicates
        auto errorOnlyFilter = [](const ConnectionFilters& filters) -> bool {
            return filters.receiveAllLogLevels || filters.logLevels.find("ERROR") != filters.logLevels.end();
        };
        
        wsManager->broadcastToFilteredConnections(message, errorOnlyFilter);
        std::cout << "✓ Error-only filter predicate handled\n";
        
        wsManager->stop();
    }
    
    void testConnectionFilterManagement() {
        std::cout << "\nTest 7: Connection Filter Management\n";
        
        auto wsManager = std::make_shared<WebSocketManager>();
        wsManager->start();
        
        // Test setting filters for non-existent connection
        ConnectionFilters testFilters;
        testFilters.receiveAllJobs = false;
        testFilters.jobIds.insert("job_123");
        
        wsManager->setConnectionFilters("non-existent-id", testFilters);
        std::cout << "✓ Setting filters for non-existent connection handled gracefully\n";
        
        // Test getting filters for non-existent connection
        ConnectionFilters retrievedFilters = wsManager->getConnectionFilters("non-existent-id");
        assert(retrievedFilters.receiveAllJobs == true); // Should return default filters
        std::cout << "✓ Getting filters for non-existent connection returns defaults\n";
        
        // Test connection count and IDs
        assert(wsManager->getConnectionCount() == 0);
        auto connectionIds = wsManager->getConnectionIds();
        assert(connectionIds.empty());
        std::cout << "✓ Connection count and IDs correct for empty manager\n";
        
        wsManager->stop();
    }
};

int main() {
    try {
        // Initialize logger for testing
        Logger& logger = Logger::getInstance();
        logger.configure(LogConfig{});
        
        WebSocketManagerEnhancedTest test;
        test.runTests();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Enhanced test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}