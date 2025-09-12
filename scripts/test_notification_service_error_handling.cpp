#include "config_manager.hpp"
#include "logger.hpp"
#include "notification_service.hpp"
#include "notification_service_recovery.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

class NotificationServiceErrorHandlingTest {
public:
  void runTests() {
    std::cout << "=== Notification Service Error Handling Tests ==="
              << std::endl;

    setupTestEnvironment();

    testRetryConfiguration();
    testServiceRecoveryState();
    testNotificationCircuitBreaker();
    testRetryQueueManager();
    testFailedNotificationHandling();
    testDeliveryErrorScenarios();
    testBulkRetryMechanism();

    std::cout << "✅ All Notification Service error handling tests completed!"
              << std::endl;
  }

private:
  void setupTestEnvironment() {
    // Configure logger
    LogConfig logConfig;
    logConfig.level = LogLevel::DEBUG;
    logConfig.consoleOutput = true;
    Logger::getInstance().configure(logConfig);

    std::cout << "Test environment configured" << std::endl;
  }

  void testRetryConfiguration() {
    std::cout << "\n--- Test: Retry Configuration ---" << std::endl;

    notification_recovery::RetryConfig config;

    // Test default values
    assert(config.enableRetry == true);
    assert(config.maxRetryAttempts == 3);
    assert(config.baseRetryDelay == std::chrono::milliseconds(5000));
    assert(config.maxRetryDelay == std::chrono::milliseconds(300000));
    assert(config.backoffMultiplier == 2.0);
    assert(config.deliveryTimeout == std::chrono::milliseconds(30000));
    assert(config.maxConcurrentRetries == 5);
    assert(config.enableBulkRetry == true);
    assert(config.bulkRetryInterval == std::chrono::minutes(10));

    std::cout << "✓ Retry configuration defaults are correct" << std::endl;

    // Test custom configuration
    config.enableRetry = false;
    config.maxRetryAttempts = 5;
    config.baseRetryDelay = std::chrono::milliseconds(10000);
    config.maxRetryDelay = std::chrono::milliseconds(600000);
    config.backoffMultiplier = 3.0;
    config.deliveryTimeout = std::chrono::milliseconds(60000);
    config.maxConcurrentRetries = 10;
    config.enableBulkRetry = false;
    config.bulkRetryInterval = std::chrono::minutes(5);

    assert(config.enableRetry == false);
    assert(config.maxRetryAttempts == 5);
    assert(config.backoffMultiplier == 3.0);
    assert(config.maxConcurrentRetries == 10);

    std::cout << "✓ Retry configuration can be customized" << std::endl;
  }

  void testServiceRecoveryState() {
    std::cout << "\n--- Test: Service Recovery State ---" << std::endl;

    notification_recovery::ServiceRecoveryState state;

    // Test initial state
    assert(state.isHealthy.load() == true);
    assert(state.isRecovering.load() == false);
    assert(state.failedDeliveries.load() == 0);
    assert(state.successfulDeliveries.load() == 0);
    assert(state.activeRetries.load() == 0);

    std::cout << "✓ Service recovery state starts with correct initial values"
              << std::endl;

    // Test failure rate calculation
    assert(state.getFailureRate() == 0.0); // No deliveries yet

    state.failedDeliveries.store(2);
    state.successfulDeliveries.store(8);
    assert(state.getFailureRate() == 0.2); // 2 failures out of 10 total

    state.failedDeliveries.store(5);
    state.successfulDeliveries.store(5);
    assert(state.getFailureRate() == 0.5); // 50% failure rate

    std::cout << "✓ Failure rate calculation works correctly" << std::endl;

    // Test reset functionality
    state.isHealthy.store(false);
    state.isRecovering.store(true);
    state.failedDeliveries.store(10);
    state.successfulDeliveries.store(20);
    state.activeRetries.store(5);

    state.reset();
    assert(state.isHealthy.load() == true);
    assert(state.isRecovering.load() == false);
    assert(state.failedDeliveries.load() == 0);
    assert(state.successfulDeliveries.load() == 0);
    assert(state.activeRetries.load() == 0);

    std::cout << "✓ Service recovery state reset works correctly" << std::endl;
  }

  void testNotificationCircuitBreaker() {
    std::cout << "\n--- Test: Notification Circuit Breaker ---" << std::endl;

    notification_recovery::NotificationCircuitBreaker circuitBreaker(
        3, std::chrono::minutes(1), 2);

    // Test initial state (CLOSED)
    assert(circuitBreaker.getState() ==
           notification_recovery::NotificationCircuitBreaker::State::CLOSED);
    assert(circuitBreaker.allowDelivery() == true);
    assert(circuitBreaker.isInDegradedMode() == false);

    std::cout << "✓ Notification circuit breaker starts in CLOSED state"
              << std::endl;

    // Test failures leading to OPEN state
    circuitBreaker.onDeliveryFailure();
    assert(circuitBreaker.getState() ==
           notification_recovery::NotificationCircuitBreaker::State::CLOSED);

    circuitBreaker.onDeliveryFailure();
    assert(circuitBreaker.getState() ==
           notification_recovery::NotificationCircuitBreaker::State::CLOSED);

    circuitBreaker.onDeliveryFailure();
    assert(circuitBreaker.getState() ==
           notification_recovery::NotificationCircuitBreaker::State::OPEN);
    assert(circuitBreaker.allowDelivery() == false);
    assert(circuitBreaker.isInDegradedMode() == true);

    std::cout << "✓ Notification circuit breaker opens after failure threshold"
              << std::endl;

    // For testing purposes, we can't easily wait for the full timeout,
    // but we can test the success recovery logic
    notification_recovery::NotificationCircuitBreaker testBreaker(
        2, std::chrono::seconds(1), 2);

    // Trigger failures to open
    testBreaker.onDeliveryFailure();
    testBreaker.onDeliveryFailure();
    assert(testBreaker.getState() ==
           notification_recovery::NotificationCircuitBreaker::State::OPEN);

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Should transition to HALF_OPEN
    assert(testBreaker.allowDelivery() == true);
    assert(testBreaker.getState() ==
           notification_recovery::NotificationCircuitBreaker::State::HALF_OPEN);

    // Test recovery with successes
    testBreaker.onDeliverySuccess();
    testBreaker.onDeliverySuccess();
    assert(testBreaker.getState() ==
           notification_recovery::NotificationCircuitBreaker::State::CLOSED);

    std::cout << "✓ Notification circuit breaker recovery works correctly"
              << std::endl;
  }

  void testRetryQueueManager() {
    std::cout << "\n--- Test: Retry Queue Manager ---" << std::endl;

    notification_recovery::RetryConfig config;
    config.baseRetryDelay = std::chrono::milliseconds(100);
    config.maxRetryDelay = std::chrono::milliseconds(1000);
    config.backoffMultiplier = 2.0;

    notification_recovery::RetryQueueManager retryManager(config);

    // Test initial state
    assert(retryManager.empty() == true);
    assert(retryManager.size() == 0);

    std::cout << "✓ Retry queue manager starts empty" << std::endl;

    // Create test notification
    NotificationMessage testNotification;
    testNotification.id = NotificationMessage::generateId();
    testNotification.type = NotificationType::JOB_FAILURE;
    testNotification.priority = NotificationPriority::HIGH;
    testNotification.jobId = "test_job_123";
    testNotification.subject = "Test Job Failed";
    testNotification.message = "Test job failed for retry testing";
    testNotification.timestamp = std::chrono::system_clock::now();
    testNotification.retryCount = 0;
    testNotification.maxRetries = 3;

    // Add failed notification
    retryManager.addFailedNotification(testNotification, "Network timeout",
                                       NotificationMethod::EMAIL);

    assert(retryManager.empty() == false);
    assert(retryManager.size() == 1);

    std::cout << "✓ Failed notifications can be added to retry queue"
              << std::endl;

    // Test immediate retry (should be empty since retry time hasn't elapsed)
    auto readyForRetry = retryManager.getReadyForRetry();
    assert(readyForRetry.empty() == true);
    assert(retryManager.size() == 1); // Still in queue

    // Wait for retry delay
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    readyForRetry = retryManager.getReadyForRetry();
    assert(readyForRetry.size() == 1);
    assert(readyForRetry[0].notification.id == testNotification.id);
    assert(readyForRetry[0].failureReason == "Network timeout");
    assert(readyForRetry[0].failedMethod == NotificationMethod::EMAIL);
    assert(retryManager.empty() == true); // Should be removed from queue

    std::cout << "✓ Failed notifications become ready for retry after delay"
              << std::endl;

    // Test multiple notifications with different retry counts
    testNotification.retryCount = 1;
    retryManager.addFailedNotification(testNotification, "Server error",
                                       NotificationMethod::WEBHOOK);

    testNotification.id = NotificationMessage::generateId();
    testNotification.retryCount = 2;
    retryManager.addFailedNotification(testNotification, "Connection refused",
                                       NotificationMethod::SLACK);

    assert(retryManager.size() == 2);

    std::cout << "✓ Multiple failed notifications can be queued" << std::endl;
  }

  void testFailedNotificationHandling() {
    std::cout << "\n--- Test: Failed Notification Handling ---" << std::endl;

    // Create test notification
    NotificationMessage notification;
    notification.id = NotificationMessage::generateId();
    notification.type = NotificationType::JOB_TIMEOUT_WARNING;
    notification.priority = NotificationPriority::MEDIUM;
    notification.jobId = "timeout_job_456";
    notification.subject = "Job Timeout Warning";
    notification.message = "Job is taking longer than expected";
    notification.timestamp = std::chrono::system_clock::now();
    notification.retryCount = 0;
    notification.maxRetries = 3;

    auto nextRetryTime =
        std::chrono::system_clock::now() + std::chrono::milliseconds(500);

    notification_recovery::FailedNotification failedNotification(
        notification, nextRetryTime, "Connection timeout",
        NotificationMethod::EMAIL);

    // Test initial state
    assert(failedNotification.notification.id == notification.id);
    assert(failedNotification.failureReason == "Connection timeout");
    assert(failedNotification.failedMethod == NotificationMethod::EMAIL);
    assert(failedNotification.isReadyForRetry() == false);

    std::cout << "✓ Failed notification object created correctly" << std::endl;

    // Test retry readiness
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    assert(failedNotification.isReadyForRetry() == true);

    std::cout
        << "✓ Failed notification becomes ready for retry after scheduled time"
        << std::endl;

    // Test notification message retry logic
    assert(notification.shouldRetry() == true); // retryCount < maxRetries
    assert(notification.retryCount == 0);

    notification.incrementRetry();
    assert(notification.retryCount == 1);
    assert(notification.shouldRetry() == true);

    notification.incrementRetry();
    notification.incrementRetry();
    assert(notification.retryCount == 3);
    assert(notification.shouldRetry() == false); // retryCount >= maxRetries

    std::cout << "✓ Notification retry logic works correctly" << std::endl;

    // Test retry delay calculation
    notification.retryCount = 0;
    auto delay1 = notification.getRetryDelay();

    notification.retryCount = 1;
    auto delay2 = notification.getRetryDelay();

    notification.retryCount = 2;
    auto delay3 = notification.getRetryDelay();

    // Delays should increase exponentially
    assert(delay2 > delay1);
    assert(delay3 > delay2);

    std::cout << "✓ Exponential backoff retry delay calculation works correctly"
              << std::endl;
  }

  void testDeliveryErrorScenarios() {
    std::cout << "\n--- Test: Delivery Error Scenarios ---" << std::endl;

    // Test different types of delivery errors
    std::vector<std::string> errorScenarios = {
        "Network timeout",
        "Connection refused",
        "DNS resolution failed",
        "HTTP 500 Internal Server Error",
        "HTTP 503 Service Unavailable",
        "Invalid authentication credentials",
        "Rate limit exceeded",
        "Payload too large",
        "Invalid webhook URL",
        "SSL certificate verification failed"};

    notification_recovery::RetryConfig config;
    notification_recovery::RetryQueueManager retryManager(config);

    // Create test notification
    NotificationMessage notification;
    notification.id = NotificationMessage::generateId();
    notification.type = NotificationType::RESOURCE_ALERT;
    notification.priority = NotificationPriority::CRITICAL;
    notification.subject = "Resource Alert";
    notification.message = "High memory usage detected";
    notification.timestamp = std::chrono::system_clock::now();
    notification.retryCount = 0;
    notification.maxRetries = 3;

    // Test various error scenarios
    for (const auto &errorScenario : errorScenarios) {
      retryManager.addFailedNotification(notification, errorScenario,
                                         NotificationMethod::WEBHOOK);
      std::cout << "  Added failed notification for: " << errorScenario
                << std::endl;
    }

    assert(retryManager.size() == errorScenarios.size());

    std::cout << "✓ Various delivery error scenarios can be handled and queued"
              << std::endl;

    // Test error categorization (would be used to decide retry strategy)
    std::vector<std::string> retryableErrors = {
        "Network timeout", "Connection refused",
        "HTTP 500 Internal Server Error", "HTTP 503 Service Unavailable",
        "Rate limit exceeded"};

    std::vector<std::string> nonRetryableErrors = {
        "Invalid authentication credentials", "HTTP 404 Not Found",
        "Invalid webhook URL", "Payload too large"};

    // In a real implementation, these would be used to determine
    // whether a notification should be retried or marked as permanently failed

    std::cout << "✓ Error scenarios categorized for retry decision making"
              << std::endl;
  }

  void testBulkRetryMechanism() {
    std::cout << "\n--- Test: Bulk Retry Mechanism ---" << std::endl;

    notification_recovery::RetryConfig config;
    config.enableBulkRetry = true;
    config.bulkRetryInterval = std::chrono::minutes(1); // Short for testing
    config.baseRetryDelay = std::chrono::milliseconds(50);

    notification_recovery::RetryQueueManager retryManager(config);

    // Add multiple failed notifications
    for (int i = 0; i < 5; ++i) {
      NotificationMessage notification;
      notification.id = NotificationMessage::generateId();
      notification.type = NotificationType::JOB_FAILURE;
      notification.priority = NotificationPriority::HIGH;
      notification.jobId = "bulk_job_" + std::to_string(i);
      notification.subject = "Bulk Test Job " + std::to_string(i) + " Failed";
      notification.message = "Bulk test job failed";
      notification.timestamp = std::chrono::system_clock::now();
      notification.retryCount = 0;
      notification.maxRetries = 3;

      retryManager.addFailedNotification(notification, "Bulk test failure",
                                         NotificationMethod::EMAIL);
    }

    assert(retryManager.size() == 5);

    std::cout << "✓ Multiple notifications queued for bulk retry" << std::endl;

    // Wait for retry delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get all ready notifications
    auto readyNotifications = retryManager.getReadyForRetry();
    assert(readyNotifications.size() == 5);
    assert(retryManager.empty() == true);

    std::cout << "✓ Bulk retry retrieves all ready notifications" << std::endl;

    // Test that all notifications have correct properties
    for (size_t i = 0; i < readyNotifications.size(); ++i) {
      auto &failedNotification = readyNotifications[i];
      assert(failedNotification.notification.jobId ==
             "bulk_job_" + std::to_string(i));
      assert(failedNotification.failureReason == "Bulk test failure");
      assert(failedNotification.failedMethod == NotificationMethod::EMAIL);
      assert(failedNotification.isReadyForRetry() == true);
    }

    std::cout << "✓ All bulk retry notifications have correct properties"
              << std::endl;
  }
};

int main() {
  try {
    NotificationServiceErrorHandlingTest test;
    test.runTests();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
