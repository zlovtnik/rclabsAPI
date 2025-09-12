/*
 * Example integration of NotificationService with the main application
 * This shows the additional lines needed in main.cpp to set up notifications
 */

// Add this include at the top of main.cpp
#include "job_monitor_service.hpp"
#include "notification_service.hpp"

// Add this code after initializing the ETL Job Manager and WebSocket Manager
// (around line 95 in the current main.cpp)

/*
// Initialize Notification Service
LOG_INFO("Main", "Initializing notification service...");
auto notificationService =
    std::make_shared<NotificationServiceImpl>(&logger);

// Load notification configuration
auto notificationConfig = NotificationConfig::fromConfig(config);
notificationService->configure(notificationConfig);

// Start notification service
notificationService->start();
LOG_INFO("Main", "Notification service started successfully");

// Initialize Job Monitor Service
LOG_INFO("Main", "Initializing job monitor service...");
auto jobMonitorService = std::make_shared<JobMonitorService>();

// Initialize job monitor service with dependencies
jobMonitorService->initialize(etlManager, wsManager, notificationService);

// Start job monitor service
jobMonitorService->start();
LOG_INFO("Main", "Job monitor service started successfully");

// The JobMonitorService will now automatically:
// 1. Listen for job status changes from ETLJobManager
// 2. Send real-time updates via WebSocket
// 3. Trigger notifications for critical events
*/

/*
 * Configuration changes needed in config.json:
 *
 * Add to "monitoring" section:
 *
 * "notifications": {
 *   "enabled": true,
 *   "job_failure_alerts": true,
 *   "timeout_warnings": true,
 *   "resource_alerts": true,
 *   "retry_attempts": 3,
 *   "retry_delay": 5000,
 *   "memory_threshold": 0.85,
 *   "cpu_threshold": 0.90,
 *   "disk_threshold": 0.90,
 *   "email": {
 *     "smtp_server": "smtp.gmail.com",
 *     "smtp_port": 587,
 *     "username": "your-email@gmail.com",
 *     "password": "your-app-password",
 *     "recipients": ["admin@company.com", "ops@company.com"]
 *   },
 *   "webhook": {
 *     "url": "https://hooks.slack.com/services/YOUR/SLACK/WEBHOOK",
 *     "secret": "your-webhook-secret",
 *     "timeout": 30000
 *   }
 * }
 */

/*
 * Shutdown changes needed in main.cpp:
 *
 * Add before server shutdown:
 *
 * if (jobMonitorService) {
 *     LOG_INFO("Main", "Stopping job monitor service...");
 *     jobMonitorService->stop();
 * }
 *
 * if (notificationService) {
 *     LOG_INFO("Main", "Stopping notification service...");
 *     notificationService->stop();
 * }
 */

/**
 * @brief Example snippets showing how other components can trigger
 * notifications.
 *
 * This function contains commented example calls illustrating typical usages of
 * the NotificationService from different parts of the system:
 * - Sending job-failure alerts from an ETL job manager.
 * - Performing resource checks (memory/CPU) from monitoring components.
 * - Sending system error alerts from any component.
 * - Constructing and sending a custom NotificationMessage with fields such as
 *   id, type, priority, subject, message, timestamps, retry counts, and
 * methods.
 *
 * The examples are illustrative and intentionally commented out; the function
 * itself performs no runtime actions.
 */

void exampleNotificationUsage() {
  // From ETLJobManager when a job fails:
  // notificationService->sendJobFailureAlert("job_123", "Database connection failed");

  // From a monitoring component checking system resources:
  // notificationService->checkMemoryUsage(getCurrentMemoryUsage());
  // notificationService->checkCpuUsage(getCurrentCpuUsage());

  // From any component when a critical error occurs:
  // notificationService->sendSystemErrorAlert("DatabaseManager", "Connection pool exhausted");

  // Custom notifications for business logic:
  // NotificationMessage custom;
  // custom.id = NotificationMessage::generateId();
  // custom.type = NotificationType::SYSTEM_ERROR;
  // custom.priority = NotificationPriority::HIGH;
  // custom.subject = "Data Quality Issue";
  // custom.message = "Detected anomalous data patterns in latest ETL batch";
  // custom.timestamp = std::chrono::system_clock::now();
  // custom.scheduledFor = custom.timestamp;
  // custom.retryCount = 0;
  // custom.maxRetries = 3;
  // custom.methods = {NotificationMethod::LOG_ONLY, NotificationMethod::EMAIL};
  // notificationService->sendCustomNotification(custom);
}
