#pragma once

#include "job_monitoring_models.hpp"
#include "logger.hpp"
#include "notification_service_recovery.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// Forward declarations
class ConfigManager;

// Notification types
enum class NotificationType {
  JOB_FAILURE,
  JOB_TIMEOUT_WARNING,
  RESOURCE_ALERT,
  SYSTEM_ERROR
};

// Notification priority levels
enum class NotificationPriority { LOW, MEDIUM, HIGH, CRITICAL };

// Notification delivery methods
enum class NotificationMethod { LOG_ONLY, EMAIL, WEBHOOK, SLACK };

// Resource alert types
enum class ResourceAlertType {
  HIGH_MEMORY_USAGE,
  HIGH_CPU_USAGE,
  DISK_SPACE_LOW,
  CONNECTION_LIMIT_REACHED,
  QUEUE_FULL
};

// Resource alert information
struct ResourceAlert {
  ResourceAlertType type;
  std::string description;
  double currentValue;
  double thresholdValue;
  std::string unit;
  std::chrono::system_clock::time_point timestamp;

  std::string toJson() const;
  static ResourceAlert fromJson(const std::string &json);
};

// Notification message structure
struct NotificationMessage {
  std::string id;                // Unique notification ID
  NotificationType type;         // Type of notification
  NotificationPriority priority; // Priority level
  std::string jobId;             // Associated job ID (if applicable)
  std::string subject;           // Notification subject/title
  std::string message;           // Detailed message content
  std::chrono::system_clock::time_point
      timestamp; // When notification was created
  std::chrono::system_clock::time_point
      scheduledFor;                        // When to deliver (for retries)
  int retryCount;                          // Number of retry attempts
  int maxRetries;                          // Maximum retry attempts
  std::vector<NotificationMethod> methods; // Delivery methods to try
  std::unordered_map<std::string, std::string>
      metadata; // Additional context data

  // Generate unique ID
  static std::string generateId();

  // JSON serialization
  std::string toJson() const;
  static NotificationMessage fromJson(const std::string &json);

  // Helper methods
  bool shouldRetry() const;
  std::chrono::milliseconds getRetryDelay() const;
  void incrementRetry();
};

// Notification configuration
struct NotificationConfig {
  bool enabled = true;
  bool jobFailureAlerts = true;
  bool timeoutWarnings = true;
  bool resourceAlerts = true;
  int maxRetryAttempts = 3;
  int baseRetryDelayMs = 5000;  // Base delay for exponential backoff
  int maxRetryDelayMs = 300000; // Maximum retry delay (5 minutes)
  int timeoutWarningThresholdMinutes =
      25;                   // Warn when job runs longer than this
  int queueMaxSize = 10000; // Maximum notification queue size

  // Resource alert thresholds
  double memoryUsageThreshold = 0.85; // 85% memory usage
  double cpuUsageThreshold = 0.90;    // 90% CPU usage
  double diskSpaceThreshold = 0.90;   // 90% disk usage
  int connectionLimitThreshold = 95;  // 95% of max connections

  // Delivery method configuration
  std::vector<NotificationMethod> defaultMethods;
  std::unordered_map<NotificationPriority, std::vector<NotificationMethod>>
      priorityMethods;

  // Method-specific settings
  std::string emailSmtpServer;
  int emailSmtpPort = 587;
  std::string emailUsername;
  std::string emailPassword;
  std::vector<std::string> emailRecipients;

  std::string webhookUrl;
  std::string webhookSecret;
  int webhookTimeoutMs = 30000;

  std::string slackWebhookUrl;
  std::string slackChannel;

  // Load from ConfigManager
  static NotificationConfig fromConfig(const ConfigManager &config);

  // Validation
  bool isValid() const;
};

// Notification delivery interface
class NotificationDelivery {
public:
  virtual ~NotificationDelivery() = default;
  virtual bool deliver(const NotificationMessage &message) = 0;
  virtual NotificationMethod getMethod() const = 0;
  virtual bool isConfigured() const = 0;
};

// Concrete delivery implementations
class LogNotificationDelivery : public NotificationDelivery {
public:
  explicit LogNotificationDelivery(Logger *logger);
  bool deliver(const NotificationMessage &message) override;
  NotificationMethod getMethod() const override {
    return NotificationMethod::LOG_ONLY;
  }
  bool isConfigured() const override { return logger_ != nullptr; }

private:
  Logger *logger_;
};

class EmailNotificationDelivery : public NotificationDelivery {
public:
  explicit EmailNotificationDelivery(const NotificationConfig &config);
  bool deliver(const NotificationMessage &message) override;
  NotificationMethod getMethod() const override {
    return NotificationMethod::EMAIL;
  }
  bool isConfigured() const override;

private:
  NotificationConfig config_;
  bool sendEmail(const std::string &to, const std::string &subject,
                 const std::string &body);
};

class WebhookNotificationDelivery : public NotificationDelivery {
public:
  explicit WebhookNotificationDelivery(const NotificationConfig &config);
  bool deliver(const NotificationMessage &message) override;
  NotificationMethod getMethod() const override {
    return NotificationMethod::WEBHOOK;
  }
  bool isConfigured() const override;

private:
  NotificationConfig config_;
  bool sendWebhook(const std::string &payload);
};

// Base NotificationService interface (matches existing interface in
// job_monitor_service.hpp)
class NotificationService {
public:
  virtual ~NotificationService() = default;
  virtual void sendJobFailureAlert(const std::string &jobId,
                                   const std::string &error) = 0;
  virtual void sendJobTimeoutWarning(const std::string &jobId,
                                     int executionTimeMinutes) = 0;
  virtual bool isRunning() const = 0;
};

/**
 * NotificationServiceImpl provides comprehensive notification capabilities for
 * the ETL Plus system. It handles job failure alerts, timeout warnings,
 * resource alerts, and other critical system events.
 *
 * Features:
 * - Multiple delivery methods (log, email, webhook, slack)
 * - Retry logic with exponential backoff
 * - Priority-based notification routing
 * - Configurable thresholds and settings
 * - Asynchronous notification processing
 * - Resource monitoring and alerting
 */
class NotificationServiceImpl : public NotificationService {
public:
  NotificationServiceImpl();
  explicit NotificationServiceImpl(Logger *logger);
  ~NotificationServiceImpl();

  // Configuration and lifecycle
  void configure(const NotificationConfig &config);
  void start();
  void stop();
  bool isRunning() const override;

  // Error handling and recovery
  bool isHealthy() const;
  void setRetryConfig(const notification_recovery::RetryConfig &config);
  const notification_recovery::RetryConfig &getRetryConfig() const {
    return retryConfig_;
  }
  const notification_recovery::ServiceRecoveryState &getRecoveryState() const {
    return recoveryState_;
  }
  void performHealthCheck();
  void attemptRecovery();

  // NotificationService interface implementation
  void sendJobFailureAlert(const std::string &jobId,
                           const std::string &error) override;
  void sendJobTimeoutWarning(const std::string &jobId,
                             int executionTimeMinutes) override;

  // Extended notification methods
  void sendResourceAlert(const ResourceAlert &alert);
  void sendSystemErrorAlert(const std::string &component,
                            const std::string &error);
  void sendCustomNotification(const NotificationMessage &message);

  // Queue and delivery management
  void queueNotification(const NotificationMessage &message);
  size_t getQueueSize() const;
  size_t getProcessedCount() const;
  size_t getFailedCount() const;

  // Resource monitoring (to be called by monitoring components)
  void checkMemoryUsage(double currentUsage);
  void checkCpuUsage(double currentUsage);
  void checkDiskSpace(double currentUsage);
  void checkConnectionLimit(int currentConnections, int maxConnections);

  // Testing and debugging support
  std::vector<NotificationMessage>
  getRecentNotifications(size_t limit = 100) const;
  void clearQueue();
  void setTestMode(bool enabled);

private:
  // Configuration and state
  NotificationConfig config_;
  Logger *logger_;
  std::atomic<bool> running_;
  std::atomic<bool> testMode_;

  // Error handling and recovery
  notification_recovery::RetryConfig retryConfig_;
  notification_recovery::ServiceRecoveryState recoveryState_;
  notification_recovery::NotificationCircuitBreaker circuitBreaker_;
  notification_recovery::RetryQueueManager retryManager_;

  // Statistics
  std::atomic<size_t> processedCount_;
  std::atomic<size_t> failedCount_;

  // Notification queue and processing
  std::queue<NotificationMessage> notificationQueue_;
  mutable std::mutex queueMutex_;
  std::condition_variable queueCondition_;
  std::thread processingThread_;
  std::thread retryThread_;

  // Recent notifications for debugging
  std::vector<NotificationMessage> recentNotifications_;
  mutable std::mutex recentMutex_;

  // Delivery methods
  std::vector<std::unique_ptr<NotificationDelivery>> deliveryMethods_;

  // Resource alert tracking (to prevent spam)
  std::unordered_map<ResourceAlertType, std::chrono::system_clock::time_point>
      lastAlertTime_;
  std::mutex alertTrackingMutex_;

  // Private methods
  void processNotifications();
  void processRetries();
  bool deliverNotification(const NotificationMessage &message);
  void handleDeliveryFailure(const NotificationMessage &message,
                             const std::string &reason,
                             NotificationMethod failedMethod);
  void scheduleRetry(const NotificationMessage &message,
                     const std::string &reason,
                     NotificationMethod failedMethod);
  void addToRecentNotifications(const NotificationMessage &message);
  bool shouldSendResourceAlert(ResourceAlertType type);
  void recordResourceAlert(ResourceAlertType type);

  // Error handling methods
  void handleServiceError(const std::string &operation,
                          const std::exception &e);
  bool tryDeliveryWithCircuitBreaker(const NotificationMessage &message,
                                     NotificationMethod method);
  void onDeliverySuccess();
  void onDeliveryFailure();

  // Notification creation helpers
  NotificationMessage createJobFailureNotification(const std::string &jobId,
                                                   const std::string &error);
  NotificationMessage
  createTimeoutWarningNotification(const std::string &jobId,
                                   int executionTimeMinutes);
  NotificationMessage
  createResourceAlertNotification(const ResourceAlert &alert);
  NotificationMessage
  createSystemErrorNotification(const std::string &component,
                                const std::string &error);

  // Delivery method setup
  void setupDeliveryMethods();
  std::vector<NotificationMethod>
  getMethodsForPriority(NotificationPriority priority);

  // Utility methods
  std::string formatJobUrl(const std::string &jobId);
  std::string formatDuration(int minutes);
  std::string getNotificationTypeString(NotificationType type);
  std::string getPriorityString(NotificationPriority priority);
};
