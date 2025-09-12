#include "job_monitoring_models.hpp"
#include "logger.hpp"
#include "websocket_connection.hpp"
#include "websocket_filter_manager.hpp"
#include "websocket_manager.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

class WebSocketFilteringUnitTest {
public:
  void runTests() {
    std::cout << "Starting WebSocket Message Filtering Unit Tests...\n";

    initializeLogger();

    testConnectionFiltersBasics();
    testConnectionFiltersEnhanced();
    testWebSocketConnectionFilterMethods();
    testWebSocketManagerFilterMethods();
    testWebSocketFilterManagerBasics();
    testWebSocketFilterManagerAdvanced();
    testMessageRoutingLogic();
    testFilterTemplates();
    testFilterStatistics();
    testBatchOperations();
    testErrorHandling();
    testPerformance();

    std::cout << "All WebSocket filtering unit tests completed successfully!\n";
  }

private:
  void initializeLogger() {
    Logger &logger = Logger::getInstance();
    // Configure logger for test environment
    LogConfig config;
    config.level = LogLevel::ERROR; // Only show errors
    config.consoleOutput = false;   // Reduce noise during tests
    config.fileOutput = false;
    Logger::getInstance().configure(config);
  }

  void testConnectionFiltersBasics() {
    std::cout << "\nTest 1: ConnectionFilters Basic Functionality\n";

    // Test default state
    ConnectionFilters filters;
    assert(filters.jobIds.empty());
    assert(filters.messageTypes.empty());
    assert(filters.logLevels.empty());
    assert(filters.includeSystemNotifications == true);
    assert(filters.hasFilters() == false);
    std::cout << "  ✓ Default ConnectionFilters state correct\n";

    // Test adding filters
    filters.addJobId("job_123");
    filters.addJobId("job_456");
    filters.addMessageType(MessageType::JOB_STATUS_UPDATE);
    filters.addLogLevel("ERROR");

    assert(filters.hasFilters() == true);
    assert(filters.hasJobFilters() == true);
    assert(filters.hasMessageTypeFilters() == true);
    assert(filters.hasLogLevelFilters() == true);
    assert(filters.getTotalFilterCount() == 4);
    std::cout << "  ✓ Adding filters works correctly\n";

    // Test shouldReceive methods
    assert(filters.shouldReceiveJob("job_123") == true);
    assert(filters.shouldReceiveJob("job_789") == false);
    assert(filters.shouldReceiveMessageType(MessageType::JOB_STATUS_UPDATE) ==
           true);
    assert(filters.shouldReceiveMessageType(MessageType::JOB_LOG_MESSAGE) ==
           false);
    assert(filters.shouldReceiveLogLevel("ERROR") == true);
    assert(filters.shouldReceiveLogLevel("DEBUG") == false);
    std::cout << "  ✓ shouldReceive methods work correctly\n";

    // Test removing filters
    filters.removeJobId("job_123");
    filters.removeMessageType(MessageType::JOB_STATUS_UPDATE);
    filters.removeLogLevel("ERROR");

    assert(filters.shouldReceiveJob("job_123") ==
           false); // No longer has job_123, only job_456
    assert(filters.shouldReceiveJob("job_456") == true); // Still has job_456
    assert(filters.shouldReceiveMessageType(MessageType::JOB_STATUS_UPDATE) ==
           true);                                           // Empty = all
    assert(filters.shouldReceiveLogLevel("ERROR") == true); // Empty = all
    std::cout << "  ✓ Removing filters works correctly\n";

    // Test clear
    filters.clear();
    assert(filters.hasFilters() == false);
    assert(filters.getTotalFilterCount() == 0);
    std::cout << "  ✓ Clear filters works correctly\n";
  }

  void testConnectionFiltersEnhanced() {
    std::cout << "\nTest 2: ConnectionFilters Enhanced Functionality\n";

    ConnectionFilters filters;

    // Test duplicate handling
    filters.addJobId("job_123");
    filters.addJobId("job_123"); // Should not add duplicate
    assert(filters.jobIds.size() == 1);
    std::cout << "  ✓ Duplicate job ID handling works\n";

    filters.addMessageType(MessageType::JOB_STATUS_UPDATE);
    filters.addMessageType(
        MessageType::JOB_STATUS_UPDATE); // Should not add duplicate
    assert(filters.messageTypes.size() == 1);
    std::cout << "  ✓ Duplicate message type handling works\n";

    filters.addLogLevel("ERROR");
    filters.addLogLevel("ERROR"); // Should not add duplicate
    assert(filters.logLevels.size() == 1);
    std::cout << "  ✓ Duplicate log level handling works\n";

    // Test validation
    assert(filters.isValid() == true);
    std::cout << "  ✓ Filter validation works\n";

    // Test JSON serialization/deserialization
    std::string json = filters.toJson();
    ConnectionFilters parsedFilters = ConnectionFilters::fromJson(json);

    assert(parsedFilters.jobIds.size() == filters.jobIds.size());
    assert(parsedFilters.messageTypes.size() == filters.messageTypes.size());
    assert(parsedFilters.logLevels.size() == filters.logLevels.size());
    assert(parsedFilters.includeSystemNotifications ==
           filters.includeSystemNotifications);
    std::cout << "  ✓ JSON serialization/deserialization works\n";
  }

  void testWebSocketConnectionFilterMethods() {
    std::cout << "\nTest 3: WebSocket Connection Filter Methods\n";

    // Note: This test would require mock WebSocket connections in a real
    // scenario For now, we'll test the logic without actual network connections

    ConnectionFilters filters;
    filters.addJobId("job_123");
    filters.addMessageType(MessageType::JOB_STATUS_UPDATE);
    filters.addLogLevel("ERROR");

    // Test shouldReceiveMessage with WebSocketMessage
    WebSocketMessage message;
    message.type = MessageType::JOB_STATUS_UPDATE;
    message.targetJobId = "job_123";
    message.targetLevel = "ERROR";

    assert(filters.shouldReceiveMessage(message) == true);
    std::cout << "  ✓ Should receive matching message\n";

    message.targetJobId = "job_999";
    assert(filters.shouldReceiveMessage(message) == false);
    std::cout << "  ✓ Should not receive non-matching job ID\n";

    message.targetJobId = "job_123";
    message.type = MessageType::JOB_LOG_MESSAGE;
    assert(filters.shouldReceiveMessage(message) == false);
    std::cout << "  ✓ Should not receive non-matching message type\n";

    message.type = MessageType::JOB_STATUS_UPDATE;
    message.targetLevel = "DEBUG";
    assert(filters.shouldReceiveMessage(message) == false);
    std::cout << "  ✓ Should not receive non-matching log level\n";
  }

  void testWebSocketManagerFilterMethods() {
    std::cout << "\nTest 4: WebSocket Manager Filter Methods\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    // Test methods that don't require actual connections
    assert(wsManager->getConnectionCount() == 0);
    assert(wsManager->getFilteredConnectionCount() == 0);
    assert(wsManager->getUnfilteredConnectionCount() == 0);
    std::cout << "  ✓ Initial connection counts correct\n";

    auto jobConnections = wsManager->getConnectionsForJob("job_123");
    assert(jobConnections.empty());
    std::cout << "  ✓ getConnectionsForJob returns empty for no connections\n";

    auto typeConnections =
        wsManager->getConnectionsForMessageType(MessageType::JOB_STATUS_UPDATE);
    assert(typeConnections.empty());
    std::cout << "  ✓ getConnectionsForMessageType returns empty for no "
                 "connections\n";

    auto levelConnections = wsManager->getConnectionsForLogLevel("ERROR");
    assert(levelConnections.empty());
    std::cout
        << "  ✓ getConnectionsForLogLevel returns empty for no connections\n";

    wsManager->stop();
  }

  void testWebSocketFilterManagerBasics() {
    std::cout << "\nTest 5: WebSocket Filter Manager Basic Operations\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    auto filterManager = std::make_shared<WebSocketFilterManager>(wsManager);

    // Test statistics with no connections
    auto stats = filterManager->getFilterStatistics();
    assert(stats.totalConnections == 0);
    assert(stats.filteredConnections == 0);
    assert(stats.unfilteredConnections == 0);
    assert(stats.averageFiltersPerConnection == 0.0);
    std::cout << "  ✓ Initial statistics correct\n";

    // Test filter templates
    auto templates = filterManager->getAvailableFilterTemplates();
    assert(!templates.empty());
    std::cout << "  ✓ Default filter templates available\n";

    // Test loading a template
    ConnectionFilters templateFilters;
    bool loaded =
        filterManager->loadFilterTemplate("error-only", templateFilters);
    assert(loaded == true);
    assert(!templateFilters.logLevels.empty());
    std::cout << "  ✓ Filter template loading works\n";

    wsManager->stop();
  }

  void testWebSocketFilterManagerAdvanced() {
    std::cout << "\nTest 6: WebSocket Filter Manager Advanced Features\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    auto filterManager = std::make_shared<WebSocketFilterManager>(wsManager);

    // Test preference management
    ConnectionFilters testFilters;
    testFilters.addJobId("job_123");
    testFilters.addLogLevel("ERROR");

    filterManager->saveConnectionPreferences("test_connection", testFilters);

    ConnectionFilters loadedFilters;
    bool loaded = filterManager->loadConnectionPreferences("test_connection",
                                                           loadedFilters);
    assert(loaded == true);
    assert(loadedFilters.jobIds.size() == 1);
    assert(loadedFilters.logLevels.size() == 1);
    std::cout << "  ✓ Connection preference management works\n";

    // Test clearing preferences
    filterManager->clearStoredPreferences("test_connection");
    loaded = filterManager->loadConnectionPreferences("test_connection",
                                                      loadedFilters);
    assert(loaded == false);
    std::cout << "  ✓ Clearing preferences works\n";

    // Test custom filter template
    ConnectionFilters customTemplate;
    customTemplate.addJobId("custom_job");
    customTemplate.addMessageType(MessageType::JOB_METRICS_UPDATE);

    filterManager->saveFilterTemplate("custom-template", customTemplate);

    ConnectionFilters loadedTemplate;
    loaded =
        filterManager->loadFilterTemplate("custom-template", loadedTemplate);
    assert(loaded == true);
    assert(loadedTemplate.jobIds.size() == 1);
    assert(loadedTemplate.messageTypes.size() == 1);
    std::cout << "  ✓ Custom filter template management works\n";

    wsManager->stop();
  }

  void testMessageRoutingLogic() {
    std::cout << "\nTest 7: Message Routing Logic\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    auto filterManager = std::make_shared<WebSocketFilterManager>(wsManager);

    // Test finding connections with custom predicates
    auto errorOnlyConnections = filterManager->findConnectionsMatchingFilter(
        [](const ConnectionFilters &filters) -> bool {
          return filters.hasLogLevelFilters() &&
                 std::find(filters.logLevels.begin(), filters.logLevels.end(),
                           "ERROR") != filters.logLevels.end();
        });

    assert(errorOnlyConnections.empty()); // No connections exist yet
    std::cout << "  ✓ Custom filter predicate works with no connections\n";

    // Test message creation and routing
    WebSocketMessage testMessage = WebSocketMessage::createJobStatusUpdate(
        JobStatusUpdate{.jobId = "job_123",
                        .status = JobStatus::RUNNING,
                        .previousStatus = JobStatus::PENDING,
                        .timestamp = std::chrono::system_clock::now(),
                        .progressPercent = 50,
                        .currentStep = "Processing data"});

    assert(testMessage.type == MessageType::JOB_STATUS_UPDATE);
    assert(testMessage.targetJobId.has_value());
    assert(testMessage.targetJobId.value() == "job_123");
    std::cout << "  ✓ WebSocket message creation works\n";

    // Test broadcasting with advanced routing (no connections)
    wsManager->broadcastWithAdvancedRouting(testMessage);
    std::cout << "  ✓ Advanced routing works with no connections\n";

    wsManager->stop();
  }

  void testFilterTemplates() {
    std::cout << "\nTest 8: Filter Templates\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    auto filterManager = std::make_shared<WebSocketFilterManager>(wsManager);

    // Test all default templates
    std::vector<std::string> expectedTemplates = {
        "error-only", "job-status", "system-notifications", "verbose"};

    auto availableTemplates = filterManager->getAvailableFilterTemplates();
    for (const auto &expectedTemplate : expectedTemplates) {
      bool found =
          std::find(availableTemplates.begin(), availableTemplates.end(),
                    expectedTemplate) != availableTemplates.end();
      assert(found == true);
    }
    std::cout << "  ✓ All default templates available\n";

    // Test loading each template
    for (const auto &templateName : expectedTemplates) {
      ConnectionFilters filters;
      bool loaded = filterManager->loadFilterTemplate(templateName, filters);
      assert(loaded == true);

      if (templateName == "error-only") {
        assert(filters.hasLogLevelFilters() == true);
      } else if (templateName == "job-status") {
        assert(filters.hasMessageTypeFilters() == true);
      } else if (templateName == "system-notifications") {
        assert(filters.hasMessageTypeFilters() == true);
        assert(filters.includeSystemNotifications == true);
      } else if (templateName == "verbose") {
        assert(filters.hasLogLevelFilters() == true);
        assert(filters.logLevels.size() >= 4); // DEBUG, INFO, WARN, ERROR
      }
    }
    std::cout
        << "  ✓ All default templates load correctly with expected content\n";

    wsManager->stop();
  }

  void testFilterStatistics() {
    std::cout << "\nTest 9: Filter Statistics\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    auto filterManager = std::make_shared<WebSocketFilterManager>(wsManager);

    // Test initial statistics
    auto stats = filterManager->getFilterStatistics();
    assert(stats.totalConnections == 0);
    assert(stats.filteredConnections == 0);
    assert(stats.unfilteredConnections == 0);
    assert(stats.jobFilterCounts.empty());
    assert(stats.messageTypeFilterCounts.empty());
    assert(stats.logLevelFilterCounts.empty());
    assert(stats.averageFiltersPerConnection == 0.0);
    std::cout << "  ✓ Initial statistics are correct\n";

    // Test statistics JSON serialization
    http::response<http::string_body> response =
        filterManager->handleGetFilterStatistics();
    assert(response.result() == http::status::ok);
    assert(!response.body().empty());
    std::cout << "  ✓ Statistics JSON serialization works\n";

    wsManager->stop();
  }

  void testBatchOperations() {
    std::cout << "\nTest 10: Batch Operations\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    auto filterManager = std::make_shared<WebSocketFilterManager>(wsManager);

    // Test batch filter application (with non-existent connections)
    std::vector<std::string> connectionIds = {"conn1", "conn2", "conn3"};
    ConnectionFilters batchFilters;
    batchFilters.addJobId("batch_job");
    batchFilters.addLogLevel("ERROR");

    // This should not throw even with non-existent connections
    filterManager->applyFiltersToMultipleConnections(connectionIds,
                                                     batchFilters);
    std::cout
        << "  ✓ Batch filter application handles non-existent connections\n";

    // Test batch filter clearing
    filterManager->clearFiltersFromMultipleConnections(connectionIds);
    std::cout << "  ✓ Batch filter clearing handles non-existent connections\n";

    wsManager->stop();
  }

  void testErrorHandling() {
    std::cout << "\nTest 11: Error Handling\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    auto filterManager = std::make_shared<WebSocketFilterManager>(wsManager);

    // Test handling non-existent connection
    auto response =
        filterManager->handleGetConnectionFilters("non_existent_connection");
    assert(response.result() == http::status::not_found);
    std::cout << "  ✓ Non-existent connection returns 404\n";

    // Test invalid filter data
    response = filterManager->handleSetConnectionFilters("test_connection",
                                                         "invalid_json");
    assert(response.result() == http::status::not_found ||
           response.result() == http::status::bad_request);
    std::cout << "  ✓ Invalid JSON returns appropriate error\n";

    // Test loading non-existent template
    ConnectionFilters filters;
    bool loaded =
        filterManager->loadFilterTemplate("non_existent_template", filters);
    assert(loaded == false);
    std::cout << "  ✓ Non-existent template returns false\n";

    // Test null WebSocket manager
    try {
      auto badFilterManager = std::make_shared<WebSocketFilterManager>(nullptr);
      assert(false); // Should not reach here
    } catch (const std::invalid_argument &) {
      std::cout << "  ✓ Null WebSocket manager throws exception\n";
    }

    wsManager->stop();
  }

  void testPerformance() {
    std::cout << "\nTest 12: Performance Testing\n";

    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();

    auto filterManager = std::make_shared<WebSocketFilterManager>(wsManager);

    // Test many filter templates
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; ++i) {
      ConnectionFilters filters;
      filters.addJobId("job_" + std::to_string(i));
      filters.addLogLevel("ERROR");
      filterManager->saveFilterTemplate("template_" + std::to_string(i),
                                        filters);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    assert(duration.count() < 1000); // Should complete in less than 1 second
    std::cout << "  ✓ Saving 1000 filter templates completed in "
              << duration.count() << "ms\n";

    // Test loading performance
    start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; ++i) {
      ConnectionFilters filters;
      filterManager->loadFilterTemplate("template_" + std::to_string(i),
                                        filters);
    }

    end = std::chrono::high_resolution_clock::now();
    duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    assert(duration.count() < 500); // Should complete in less than 0.5 seconds
    std::cout << "  ✓ Loading 1000 filter templates completed in "
              << duration.count() << "ms\n";

    // Test filter matching performance
    ConnectionFilters testFilters;
    for (int i = 0; i < 100; ++i) {
      testFilters.addJobId("job_" + std::to_string(i));
    }

    start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10000; ++i) {
      WebSocketMessage message;
      message.type = MessageType::JOB_STATUS_UPDATE;
      message.targetJobId = "job_" + std::to_string(i % 100);

      testFilters.shouldReceiveMessage(message);
    }

    end = std::chrono::high_resolution_clock::now();
    duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    assert(duration.count() < 100); // Should complete in less than 0.1 seconds
    std::cout << "  ✓ 10000 filter matches completed in " << duration.count()
              << "ms\n";

    wsManager->stop();
  }
};

int main() {
  WebSocketFilteringUnitTest test;
  test.runTests();
  return 0;
}
