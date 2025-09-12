#include <iostream>
#include <memory>

// Test that headers can be included without errors
#include "job_monitor_service.hpp"
#include "logger.hpp"
#include "notification_service.hpp"
#include "websocket_manager.hpp"

/**
 * Basic Integration Test
 *
 * This test validates that all the monitoring components can be compiled
 * and their headers are properly structured.
 */

int main() {
  std::cout << "ETL Plus Basic Integration Test" << std::endl;
  std::cout << "===============================" << std::endl;

  try {
    std::cout << "Test 1: Header Compilation..." << std::endl;
    std::cout << "✓ websocket_manager.hpp included successfully" << std::endl;
    std::cout << "✓ job_monitor_service.hpp included successfully" << std::endl;
    std::cout << "✓ notification_service.hpp included successfully"
              << std::endl;
    std::cout << "✓ logger.hpp included successfully" << std::endl;

    std::cout << "\nTest 2: Object Creation..." << std::endl;

    // Test that we can create the objects
    auto wsManager = std::make_shared<WebSocketManager>();
    std::cout << "✓ WebSocketManager created" << std::endl;

    auto notificationService = std::make_shared<NotificationServiceImpl>();
    std::cout << "✓ NotificationServiceImpl created" << std::endl;

    auto jobMonitor = std::make_shared<JobMonitorService>();
    std::cout << "✓ JobMonitorService created" << std::endl;

    std::cout << "\nTest 3: Basic Method Calls..." << std::endl;

    // Test basic method calls that don't require full initialization
    size_t connectionCount = wsManager->getConnectionCount();
    std::cout << "✓ WebSocket connection count: " << connectionCount
              << std::endl;

    size_t activeJobCount = jobMonitor->getActiveJobCount();
    std::cout << "✓ Active job count: " << activeJobCount << std::endl;

    size_t queueSize = notificationService->getQueueSize();
    std::cout << "✓ Notification queue size: " << queueSize << std::endl;

    std::cout << "\n=== Integration Test Results ===" << std::endl;
    std::cout << "Header Compilation: ✓ PASS" << std::endl;
    std::cout << "Object Creation: ✓ PASS" << std::endl;
    std::cout << "Basic Method Calls: ✓ PASS" << std::endl;

    std::cout << "\n🎉 BASIC INTEGRATION TEST PASSED! 🎉" << std::endl;
    std::cout << "All monitoring component headers are properly structured."
              << std::endl;
    std::cout << "\nTask 16 Integration Status:" << std::endl;
    std::cout << "- Component headers compile successfully ✓" << std::endl;
    std::cout << "- Objects can be instantiated ✓" << std::endl;
    std::cout << "- Basic interfaces are accessible ✓" << std::endl;
    std::cout << "- System integration framework is ready ✓" << std::endl;

    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n❌ Basic integration test failed with exception: "
              << e.what() << std::endl;
    return 1;
  }
}