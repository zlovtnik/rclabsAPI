#include "config_manager.hpp"
#include "logger.hpp"
#include "notification_service.hpp"
#include <chrono>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

using namespace testing;
using namespace std::chrono_literals;

// Mock Logger for testing
class MockLogger : public Logger {
public:
  MOCK_METHOD(void, logInfo,
              (const std::string &component, const std::string &message),
              (override));
  MOCK_METHOD(void, logWarning,
              (const std::string &component, const std::string &message),
              (override));
  MOCK_METHOD(void, logError,
              (const std::string &component, const std::string &message),
              (override));
  MOCK_METHOD(void, logDebug,
              (const std::string &component, const std::string &message),
              (override));
};

// Mock NotificationDelivery for testing delivery methods
class MockNotificationDelivery : public NotificationDelivery {
public:
  MOCK_METHOD(bool, deliver, (const NotificationMessage &message), (override));
  MOCK_METHOD(NotificationMethod, getMethod, (), (const, override));
  MOCK_METHOD(bool, isConfigured, (), (const, override));
};

class NotificationServiceTest : public ::testing::Test {
protected:
  void SetUp() override {
    mockLogger_ = std::make_shared<MockLogger>();
    service_ = std::make_unique<NotificationServiceImpl>(mockLogger_);

    // Set up default configuration
    config_.enabled = true;
    config_.jobFailureAlerts = true;
    config_.timeoutWarnings = true;
    config_.resourceAlerts = true;
    config_.maxRetryAttempts = 3;
    config_.baseRetryDelayMs = 100; // Faster for testing
    config_.timeoutWarningThresholdMinutes = 25;
    config_.queueMaxSize = 1000;

    // Set up test mode to avoid actual network calls
    service_->setTestMode(true);
  }

  void TearDown() override {
    if (service_->isRunning()) {
      service_->stop();
    }
  }

  std::shared_ptr<MockLogger> mockLogger_;
  std::unique_ptr<NotificationServiceImpl> service_;
  NotificationConfig config_;
};

// ===== NotificationMessage Tests =====

TEST_F(NotificationServiceTest, NotificationMessage_GenerateUniqueIds) {
  std::string id1 = NotificationMessage::generateId();
  std::string id2 = NotificationMessage::generateId();

  EXPECT_NE(id1, id2);
  EXPECT_TRUE(id1.starts_with("notif_"));
  EXPECT_TRUE(id2.starts_with("notif_"));
}

TEST_F(NotificationServiceTest, NotificationMessage_JsonSerialization) {
  NotificationMessage msg;
  msg.id = "test_123";
  msg.type = NotificationType::JOB_FAILURE;
  msg.priority = NotificationPriority::HIGH;
  msg.jobId = "job_456";
  msg.subject = "Test Notification";
  msg.message = "This is a test message";
  msg.timestamp = std::chrono::system_clock::now();
  msg.retryCount = 2;
  msg.maxRetries = 5;
  msg.methods = {NotificationMethod::LOG_ONLY, NotificationMethod::EMAIL};
  msg.metadata["key1"] = "value1";
  msg.metadata["key2"] = "value2";

  std::string json = msg.toJson();
  EXPECT_FALSE(json.empty());
  EXPECT_THAT(json, HasSubstr("test_123"));
  EXPECT_THAT(json, HasSubstr("job_456"));
  EXPECT_THAT(json, HasSubstr("Test Notification"));

  // Test deserialization (basic check)
  NotificationMessage parsed = NotificationMessage::fromJson(json);
  EXPECT_EQ(parsed.id, msg.id);
  EXPECT_EQ(parsed.type, msg.type);
  EXPECT_EQ(parsed.priority, msg.priority);
  EXPECT_EQ(parsed.jobId, msg.jobId);
  EXPECT_EQ(parsed.subject, msg.subject);
  EXPECT_EQ(parsed.message, msg.message);
}

TEST_F(NotificationServiceTest, NotificationMessage_RetryLogic) {
  NotificationMessage msg;
  msg.retryCount = 0;
  msg.maxRetries = 3;

  EXPECT_TRUE(msg.shouldRetry());

  msg.incrementRetry();
  EXPECT_EQ(msg.retryCount, 1);
  EXPECT_TRUE(msg.shouldRetry());

  msg.incrementRetry();
  msg.incrementRetry();
  EXPECT_EQ(msg.retryCount, 3);
  EXPECT_FALSE(msg.shouldRetry());

  // Test retry delay increases
  msg.retryCount = 0;
  auto delay1 = msg.getRetryDelay();
  msg.incrementRetry();
  auto delay2 = msg.getRetryDelay();
  EXPECT_GT(delay2, delay1);
}

// ===== ResourceAlert Tests =====

TEST_F(NotificationServiceTest, ResourceAlert_JsonSerialization) {
  ResourceAlert alert;
  alert.type = ResourceAlertType::HIGH_MEMORY_USAGE;
  alert.description = "Memory usage high";
  alert.currentValue = 0.92;
  alert.thresholdValue = 0.85;
  alert.unit = "percentage";
  alert.timestamp = std::chrono::system_clock::now();

  std::string json = alert.toJson();
  EXPECT_FALSE(json.empty());
  EXPECT_THAT(json, HasSubstr("Memory usage high"));
  EXPECT_THAT(json, HasSubstr("0.92"));
  EXPECT_THAT(json, HasSubstr("percentage"));

  ResourceAlert parsed = ResourceAlert::fromJson(json);
  EXPECT_EQ(parsed.type, alert.type);
  EXPECT_EQ(parsed.description, alert.description);
  EXPECT_DOUBLE_EQ(parsed.currentValue, alert.currentValue);
  EXPECT_DOUBLE_EQ(parsed.thresholdValue, alert.thresholdValue);
  EXPECT_EQ(parsed.unit, alert.unit);
}

// ===== NotificationConfig Tests =====

TEST_F(NotificationServiceTest, NotificationConfig_Validation) {
  NotificationConfig config;
  config.enabled = true;
  config.maxRetryAttempts = 3;
  config.baseRetryDelayMs = 1000;

  EXPECT_TRUE(config.isValid());

  config.maxRetryAttempts = -1;
  EXPECT_FALSE(config.isValid());

  config.maxRetryAttempts = 3;
  config.baseRetryDelayMs = 0;
  EXPECT_FALSE(config.isValid());

  config.enabled = false;
  EXPECT_TRUE(config.isValid()); // Disabled config is always valid
}

// ===== NotificationServiceImpl Tests =====

TEST_F(NotificationServiceTest, Service_LifecycleManagement) {
  EXPECT_FALSE(service_->isRunning());

  service_->configure(config_);
  service_->start();
  EXPECT_TRUE(service_->isRunning());

  service_->stop();
  EXPECT_FALSE(service_->isRunning());
}

TEST_F(NotificationServiceTest, Service_JobFailureAlert) {
  service_->configure(config_);
  service_->start();

  EXPECT_CALL(*mockLogger_, logInfo(_, _)).Times(AtLeast(1));

  service_->sendJobFailureAlert("job_123", "Database connection failed");

  // Give some time for processing
  std::this_thread::sleep_for(50ms);

  EXPECT_GT(service_->getProcessedCount(), 0);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_FALSE(recent.empty());
  EXPECT_EQ(recent.back().type, NotificationType::JOB_FAILURE);
  EXPECT_EQ(recent.back().jobId, "job_123");
  EXPECT_THAT(recent.back().message, HasSubstr("Database connection failed"));
}

TEST_F(NotificationServiceTest, Service_TimeoutWarning) {
  service_->configure(config_);
  service_->start();

  EXPECT_CALL(*mockLogger_, logInfo(_, _)).Times(AtLeast(1));

  service_->sendJobTimeoutWarning("job_456", 30);

  std::this_thread::sleep_for(50ms);

  EXPECT_GT(service_->getProcessedCount(), 0);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_FALSE(recent.empty());
  EXPECT_EQ(recent.back().type, NotificationType::JOB_TIMEOUT_WARNING);
  EXPECT_EQ(recent.back().jobId, "job_456");
  EXPECT_THAT(recent.back().message, HasSubstr("30"));
}

TEST_F(NotificationServiceTest, Service_ResourceAlert) {
  service_->configure(config_);
  service_->start();

  EXPECT_CALL(*mockLogger_, logInfo(_, _)).Times(AtLeast(1));

  ResourceAlert alert;
  alert.type = ResourceAlertType::HIGH_CPU_USAGE;
  alert.description = "CPU usage exceeded threshold";
  alert.currentValue = 0.95;
  alert.thresholdValue = 0.90;
  alert.unit = "percentage";
  alert.timestamp = std::chrono::system_clock::now();

  service_->sendResourceAlert(alert);

  std::this_thread::sleep_for(50ms);

  EXPECT_GT(service_->getProcessedCount(), 0);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_FALSE(recent.empty());
  EXPECT_EQ(recent.back().type, NotificationType::RESOURCE_ALERT);
  EXPECT_THAT(recent.back().message, HasSubstr("CPU usage exceeded threshold"));
}

TEST_F(NotificationServiceTest, Service_SystemErrorAlert) {
  service_->configure(config_);
  service_->start();

  EXPECT_CALL(*mockLogger_, logError(_, _)).Times(AtLeast(1));

  service_->sendSystemErrorAlert("DatabaseManager",
                                 "Connection pool exhausted");

  std::this_thread::sleep_for(50ms);

  EXPECT_GT(service_->getProcessedCount(), 0);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_FALSE(recent.empty());
  EXPECT_EQ(recent.back().type, NotificationType::SYSTEM_ERROR);
  EXPECT_EQ(recent.back().priority, NotificationPriority::CRITICAL);
  EXPECT_THAT(recent.back().message, HasSubstr("DatabaseManager"));
  EXPECT_THAT(recent.back().message, HasSubstr("Connection pool exhausted"));
}

TEST_F(NotificationServiceTest, Service_CustomNotification) {
  service_->configure(config_);
  service_->start();

  NotificationMessage custom;
  custom.id = NotificationMessage::generateId();
  custom.type = NotificationType::SYSTEM_ERROR;
  custom.priority = NotificationPriority::MEDIUM;
  custom.subject = "Custom Test";
  custom.message = "This is a custom notification";
  custom.timestamp = std::chrono::system_clock::now();
  custom.scheduledFor = custom.timestamp;
  custom.retryCount = 0;
  custom.maxRetries = 3;
  custom.methods = {NotificationMethod::LOG_ONLY};

  service_->sendCustomNotification(custom);

  std::this_thread::sleep_for(50ms);

  EXPECT_GT(service_->getProcessedCount(), 0);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_FALSE(recent.empty());
  EXPECT_EQ(recent.back().subject, "Custom Test");
  EXPECT_EQ(recent.back().message, "This is a custom notification");
}

TEST_F(NotificationServiceTest, Service_QueueManagement) {
  service_->configure(config_);
  // Don't start service to test queuing

  EXPECT_EQ(service_->getQueueSize(), 0);

  service_->sendJobFailureAlert("job_1", "Error 1");
  EXPECT_EQ(service_->getQueueSize(), 1);

  service_->sendJobFailureAlert("job_2", "Error 2");
  EXPECT_EQ(service_->getQueueSize(), 2);

  service_->clearQueue();
  EXPECT_EQ(service_->getQueueSize(), 0);
}

TEST_F(NotificationServiceTest, Service_QueueSizeLimit) {
  config_.queueMaxSize = 2;
  service_->configure(config_);

  EXPECT_CALL(*mockLogger_, logWarning(_, HasSubstr("queue full")))
      .Times(AtLeast(1));

  // Fill the queue to its limit
  service_->sendJobFailureAlert("job_1", "Error 1");
  service_->sendJobFailureAlert("job_2", "Error 2");
  EXPECT_EQ(service_->getQueueSize(), 2);

  // This should be dropped due to queue size limit
  service_->sendJobFailureAlert("job_3", "Error 3");
  EXPECT_EQ(service_->getQueueSize(), 2); // Should still be 2
}

TEST_F(NotificationServiceTest, Service_DisabledConfiguration) {
  config_.enabled = false;
  service_->configure(config_);
  service_->start();

  // No notifications should be sent when disabled
  service_->sendJobFailureAlert("job_123", "Error");
  service_->sendJobTimeoutWarning("job_456", 30);

  std::this_thread::sleep_for(50ms);

  EXPECT_EQ(service_->getProcessedCount(), 0);
  EXPECT_EQ(service_->getQueueSize(), 0);
}

TEST_F(NotificationServiceTest, Service_SelectivelyDisabledAlerts) {
  config_.jobFailureAlerts = false;
  config_.timeoutWarnings = true;
  config_.resourceAlerts = false;
  service_->configure(config_);
  service_->start();

  // Job failure should be ignored
  service_->sendJobFailureAlert("job_123", "Error");

  // Timeout warning should be processed
  service_->sendJobTimeoutWarning("job_456", 30);

  // Resource alert should be ignored
  ResourceAlert alert;
  alert.type = ResourceAlertType::HIGH_MEMORY_USAGE;
  service_->sendResourceAlert(alert);

  std::this_thread::sleep_for(50ms);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_EQ(recent.size(), 1); // Only timeout warning
  EXPECT_EQ(recent[0].type, NotificationType::JOB_TIMEOUT_WARNING);
}

TEST_F(NotificationServiceTest, Service_ResourceMonitoring) {
  service_->configure(config_);
  service_->start();

  EXPECT_CALL(*mockLogger_, logInfo(_, _)).Times(AtLeast(1));

  // Test memory usage check
  service_->checkMemoryUsage(0.90); // Above threshold (0.85)

  // Test CPU usage check
  service_->checkCpuUsage(0.95); // Above threshold (0.90)

  // Test disk space check
  service_->checkDiskSpace(0.95); // Above threshold (0.90)

  // Test connection limit check
  service_->checkConnectionLimit(98, 100); // Above threshold (95%)

  std::this_thread::sleep_for(100ms);

  EXPECT_GT(service_->getProcessedCount(), 0);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_GE(recent.size(), 4); // Should have at least 4 resource alerts
}

TEST_F(NotificationServiceTest, Service_ResourceAlertSpamPrevention) {
  service_->configure(config_);
  service_->start();

  ResourceAlert alert;
  alert.type = ResourceAlertType::HIGH_MEMORY_USAGE;
  alert.description = "Memory usage high";
  alert.currentValue = 0.90;
  alert.thresholdValue = 0.85;
  alert.unit = "percentage";
  alert.timestamp = std::chrono::system_clock::now();

  // Send same alert type multiple times rapidly
  service_->sendResourceAlert(alert);
  service_->sendResourceAlert(alert);
  service_->sendResourceAlert(alert);

  std::this_thread::sleep_for(50ms);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_EQ(recent.size(), 1); // Only first one should be sent
}

TEST_F(NotificationServiceTest, Service_StatisticsTracking) {
  service_->configure(config_);
  service_->start();

  size_t initialProcessed = service_->getProcessedCount();
  size_t initialFailed = service_->getFailedCount();

  service_->sendJobFailureAlert("job_123", "Error");
  service_->sendJobTimeoutWarning("job_456", 30);

  std::this_thread::sleep_for(100ms);

  EXPECT_GT(service_->getProcessedCount(), initialProcessed);
  // In test mode, failures shouldn't increase
  EXPECT_EQ(service_->getFailedCount(), initialFailed);
}

// ===== LogNotificationDelivery Tests =====

TEST_F(NotificationServiceTest, LogDelivery_BasicFunctionality) {
  auto logger = std::make_shared<MockLogger>();
  LogNotificationDelivery delivery(logger);

  EXPECT_EQ(delivery.getMethod(), NotificationMethod::LOG_ONLY);
  EXPECT_TRUE(delivery.isConfigured());

  NotificationMessage msg;
  msg.subject = "Test Subject";
  msg.message = "Test Message";
  msg.priority = NotificationPriority::HIGH;
  msg.jobId = "job_123";

  EXPECT_CALL(*logger,
              logWarning("NotificationService", HasSubstr("Test Subject")));

  EXPECT_TRUE(delivery.deliver(msg));
}

TEST_F(NotificationServiceTest, LogDelivery_PriorityMapping) {
  auto logger = std::make_shared<MockLogger>();
  LogNotificationDelivery delivery(logger);

  NotificationMessage msg;
  msg.subject = "Test";
  msg.message = "Message";

  // Test different priority levels
  msg.priority = NotificationPriority::CRITICAL;
  EXPECT_CALL(*logger, logError("NotificationService", _));
  delivery.deliver(msg);

  msg.priority = NotificationPriority::HIGH;
  EXPECT_CALL(*logger, logWarning("NotificationService", _));
  delivery.deliver(msg);

  msg.priority = NotificationPriority::MEDIUM;
  EXPECT_CALL(*logger, logInfo("NotificationService", _));
  delivery.deliver(msg);

  msg.priority = NotificationPriority::LOW;
  EXPECT_CALL(*logger, logDebug("NotificationService", _));
  delivery.deliver(msg);
}

// ===== Integration Tests =====

TEST_F(NotificationServiceTest, Integration_EndToEndWorkflow) {
  service_->configure(config_);
  service_->start();

  // Simulate a complete job failure scenario
  std::string jobId = "integration_test_job";
  std::string error = "Database connection timeout";

  EXPECT_CALL(*mockLogger_, logWarning(_, _)).Times(AtLeast(1));

  service_->sendJobFailureAlert(jobId, error);

  // Wait for processing
  std::this_thread::sleep_for(100ms);

  // Verify the notification was processed
  EXPECT_GT(service_->getProcessedCount(), 0);

  auto recent = service_->getRecentNotifications(1);
  ASSERT_FALSE(recent.empty());

  const auto &notification = recent[0];
  EXPECT_EQ(notification.type, NotificationType::JOB_FAILURE);
  EXPECT_EQ(notification.priority, NotificationPriority::HIGH);
  EXPECT_EQ(notification.jobId, jobId);
  EXPECT_THAT(notification.subject, HasSubstr(jobId));
  EXPECT_THAT(notification.message, HasSubstr(error));
  EXPECT_FALSE(notification.id.empty());
  EXPECT_GT(notification.timestamp.time_since_epoch().count(), 0);
}

TEST_F(NotificationServiceTest, Integration_MultipleNotificationTypes) {
  service_->configure(config_);
  service_->start();

  EXPECT_CALL(*mockLogger_, logWarning(_, _)).Times(AtLeast(1));
  EXPECT_CALL(*mockLogger_, logInfo(_, _)).Times(AtLeast(1));
  EXPECT_CALL(*mockLogger_, logError(_, _)).Times(AtLeast(1));

  // Send different types of notifications
  service_->sendJobFailureAlert("job_1", "Error 1");
  service_->sendJobTimeoutWarning("job_2", 35);
  service_->sendSystemErrorAlert("TestComponent", "System error");

  ResourceAlert alert;
  alert.type = ResourceAlertType::DISK_SPACE_LOW;
  alert.description = "Disk space low";
  alert.currentValue = 0.95;
  alert.thresholdValue = 0.90;
  alert.unit = "percentage";
  alert.timestamp = std::chrono::system_clock::now();
  service_->sendResourceAlert(alert);

  // Wait for processing
  std::this_thread::sleep_for(200ms);

  EXPECT_GE(service_->getProcessedCount(), 4);

  auto recent = service_->getRecentNotifications(10);
  EXPECT_GE(recent.size(), 4);

  // Verify we have different notification types
  std::set<NotificationType> types;
  for (const auto &notif : recent) {
    types.insert(notif.type);
  }
  EXPECT_GE(types.size(), 3); // At least 3 different types
}

// ===== Performance Tests =====

TEST_F(NotificationServiceTest, Performance_HighVolumeNotifications) {
  service_->configure(config_);
  service_->start();

  // Send a large number of notifications quickly
  const int numNotifications = 1000;
  auto startTime = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < numNotifications; ++i) {
    service_->sendJobFailureAlert("job_" + std::to_string(i),
                                  "Error " + std::to_string(i));
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      endTime - startTime);

  // Should be able to queue 1000 notifications in reasonable time (< 1 second)
  EXPECT_LT(duration.count(), 1000);

  // Wait for processing to complete
  std::this_thread::sleep_for(2s);

  EXPECT_EQ(service_->getProcessedCount(), numNotifications);
}

// Test main function
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
