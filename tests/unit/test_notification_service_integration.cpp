#include "config_manager.hpp"
#include "job_monitor_service.hpp"
#include "logger.hpp"
#include "notification_service.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

/**
 * Simple integration demo showing how to set up and use the NotificationService
 * with the JobMonitorService for real-time job event notifications.
 */

int main() {
  std::cout << "=== NotificationService Integration Demo ===" << std::endl;

  try {
    // 1. Initialize core components
    auto &logger = Logger::getInstance();
    auto &configManager = ConfigManager::getInstance();

    // Load configuration
    if (!configManager.loadConfig("config.json")) {
      std::cerr << "Failed to load configuration!" << std::endl;
      return 1;
    }

    // 2. Create and configure the NotificationService
    auto notificationService =
        std::make_shared<NotificationServiceImpl>(&logger);

    // Load configuration from config manager
    auto notificationConfig = NotificationConfig::fromConfig(configManager);
    notificationService->configure(notificationConfig);

    // Start the notification service
    notificationService->start();
    std::cout << "NotificationService started successfully" << std::endl;

    // 3. Test different types of notifications
    std::cout << "\n--- Testing Job Failure Alert ---" << std::endl;
    notificationService->sendJobFailureAlert("demo_job_001",
                                             "Database connection timeout");

    std::cout << "\n--- Testing Job Timeout Warning ---" << std::endl;
    notificationService->sendJobTimeoutWarning("demo_job_002", 30);

    std::cout << "\n--- Testing Resource Alerts ---" << std::endl;

    // Memory usage alert
    ResourceAlert memoryAlert;
    memoryAlert.type = ResourceAlertType::HIGH_MEMORY_USAGE;
    memoryAlert.description = "Memory usage exceeded threshold";
    memoryAlert.currentValue = 0.92;
    memoryAlert.thresholdValue = 0.85;
    memoryAlert.unit = "percentage";
    memoryAlert.timestamp = std::chrono::system_clock::now();
    notificationService->sendResourceAlert(memoryAlert);

    // CPU usage alert
    ResourceAlert cpuAlert;
    cpuAlert.type = ResourceAlertType::HIGH_CPU_USAGE;
    cpuAlert.description = "CPU usage exceeded threshold";
    cpuAlert.currentValue = 0.95;
    cpuAlert.thresholdValue = 0.90;
    cpuAlert.unit = "percentage";
    cpuAlert.timestamp = std::chrono::system_clock::now();
    notificationService->sendResourceAlert(cpuAlert);

    std::cout << "\n--- Testing System Error Alert ---" << std::endl;
    notificationService->sendSystemErrorAlert("DatabaseManager",
                                              "Connection pool exhausted");

    std::cout << "\n--- Testing Resource Monitoring Methods ---" << std::endl;
    // These would typically be called by monitoring components
    notificationService->checkMemoryUsage(0.88);        // Above threshold
    notificationService->checkCpuUsage(0.95);           // Above threshold
    notificationService->checkDiskSpace(0.92);          // Above threshold
    notificationService->checkConnectionLimit(98, 100); // Above threshold

    // 4. Wait for notifications to be processed
    std::cout << "\nWaiting for notifications to be processed..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 5. Display statistics
    std::cout << "\n--- Notification Statistics ---" << std::endl;
    std::cout << "Queue size: " << notificationService->getQueueSize()
              << std::endl;
    std::cout << "Processed count: " << notificationService->getProcessedCount()
              << std::endl;
    std::cout << "Failed count: " << notificationService->getFailedCount()
              << std::endl;

    // 6. Show recent notifications
    std::cout << "\n--- Recent Notifications ---" << std::endl;
    auto recentNotifications = notificationService->getRecentNotifications(10);
    for (size_t i = 0; i < recentNotifications.size(); ++i) {
      const auto &notif = recentNotifications[i];
      std::cout << (i + 1) << ". [" << notif.id << "] " << notif.subject
                << " - " << notif.message << std::endl;
    }

    // 7. Test custom notification
    std::cout << "\n--- Testing Custom Notification ---" << std::endl;
    NotificationMessage customNotif;
    customNotif.id = NotificationMessage::generateId();
    customNotif.type = NotificationType::SYSTEM_ERROR;
    customNotif.priority = NotificationPriority::MEDIUM;
    customNotif.subject = "Demo Custom Notification";
    customNotif.message =
        "This is a custom notification created for demonstration purposes";
    customNotif.timestamp = std::chrono::system_clock::now();
    customNotif.scheduledFor = customNotif.timestamp;
    customNotif.retryCount = 0;
    customNotif.maxRetries = 3;
    customNotif.methods = {NotificationMethod::LOG_ONLY};
    customNotif.metadata["demo"] = "true";
    customNotif.metadata["source"] = "integration_demo";

    notificationService->sendCustomNotification(customNotif);

    // 8. Test notification JSON serialization
    std::cout << "\n--- Testing JSON Serialization ---" << std::endl;
    std::string jsonStr = customNotif.toJson();
    std::cout << "Notification JSON: " << jsonStr << std::endl;

    // 9. Test retry logic simulation
    std::cout << "\n--- Testing Retry Logic ---" << std::endl;
    NotificationMessage retryTest;
    retryTest.retryCount = 0;
    retryTest.maxRetries = 3;

    std::cout << "Initial retry count: " << retryTest.retryCount << std::endl;
    std::cout << "Should retry: " << (retryTest.shouldRetry() ? "yes" : "no")
              << std::endl;

    retryTest.incrementRetry();
    std::cout << "After increment - retry count: " << retryTest.retryCount
              << std::endl;
    std::cout << "Retry delay: " << retryTest.getRetryDelay().count() << "ms"
              << std::endl;

    // 10. Integration with JobMonitorService simulation
    std::cout << "\n--- JobMonitorService Integration Simulation ---"
              << std::endl;
    std::cout << "In a real scenario, JobMonitorService would:" << std::endl;
    std::cout << "1. Be initialized with the NotificationService instance"
              << std::endl;
    std::cout << "2. Call notification methods when job events occur"
              << std::endl;
    std::cout << "3. Example: onJobStatusChanged() -> sendJobFailureAlert()"
              << std::endl;
    std::cout << "4. Example: timeout detection -> sendJobTimeoutWarning()"
              << std::endl;

    // Simulate what JobMonitorService would do
    std::cout << "\nSimulating JobMonitorService calling notification methods:"
              << std::endl;
    notificationService->sendJobFailureAlert(
        "simulated_job_123", "Simulated failure from JobMonitorService");

    // 11. Wait for final processing
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 12. Final statistics
    std::cout << "\n--- Final Statistics ---" << std::endl;
    std::cout << "Total processed: " << notificationService->getProcessedCount()
              << std::endl;
    std::cout << "Total failed: " << notificationService->getFailedCount()
              << std::endl;
    std::cout << "Queue size: " << notificationService->getQueueSize()
              << std::endl;

    // 13. Stop the service
    notificationService->stop();
    std::cout << "\nNotificationService stopped successfully" << std::endl;

    std::cout << "\n=== Demo completed successfully ===" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Demo failed with error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}

/*
Expected Output:
=== NotificationService Integration Demo ===
NotificationService started successfully

--- Testing Job Failure Alert ---

--- Testing Job Timeout Warning ---

--- Testing Resource Alerts ---

--- Testing System Error Alert ---

--- Testing Resource Monitoring Methods ---

Waiting for notifications to be processed...

--- Notification Statistics ---
Queue size: 0
Processed count: 8
Failed count: 0

--- Recent Notifications ---
1. [notif_66b6d2a0_abcd1234] ETL Job Failed: demo_job_001 - Job demo_job_001 has
failed with error: Database connection timeout
2. [notif_66b6d2a1_abcd1235] ETL Job Timeout Warning: demo_job_002 - Job
demo_job_002 has been running for 30 minutes, which exceeds the warning
threshold of 25 minutes
3. [notif_66b6d2a2_abcd1236] Resource Alert: Memory usage exceeded threshold -
Memory usage exceeded threshold. Current value: 0.92 percentage, threshold: 0.85
percentage
4. [notif_66b6d2a3_abcd1237] Resource Alert: CPU usage exceeded threshold - CPU
usage exceeded threshold. Current value: 0.95 percentage, threshold: 0.90
percentage
5. [notif_66b6d2a4_abcd1238] System Error in DatabaseManager - A critical error
occurred in component DatabaseManager: Connection pool exhausted
6. [notif_66b6d2a5_abcd1239] Resource Alert: Memory usage exceeded threshold -
Memory usage exceeded threshold. Current value: 0.88 percentage, threshold: 0.85
percentage
7. [notif_66b6d2a6_abcd123a] Resource Alert: CPU usage exceeded threshold - CPU
usage exceeded threshold. Current value: 0.95 percentage, threshold: 0.90
percentage
8. [notif_66b6d2a7_abcd123b] Resource Alert: Disk space usage is above threshold
- Disk space usage is above threshold. Current value: 0.92 percentage,
threshold: 0.90 percentage

--- Testing Custom Notification ---

--- Testing JSON Serialization ---
Notification JSON:
{"id":"notif_66b6d2a8_abcd123c","type":3,"priority":1,"jobId":"","subject":"Demo
Custom Notification"...}

--- Testing Retry Logic ---
Initial retry count: 0
Should retry: yes
After increment - retry count: 1
Retry delay: 10000ms

--- JobMonitorService Integration Simulation ---
In a real scenario, JobMonitorService would:
1. Be initialized with the NotificationService instance
2. Call notification methods when job events occur
3. Example: onJobStatusChanged() -> sendJobFailureAlert()
4. Example: timeout detection -> sendJobTimeoutWarning()

Simulating JobMonitorService calling notification methods:

--- Final Statistics ---
Total processed: 10
Total failed: 0
Queue size: 0

NotificationService stopped successfully

=== Demo completed successfully ===
*/
