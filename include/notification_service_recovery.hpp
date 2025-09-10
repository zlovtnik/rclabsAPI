#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

// Forward declarations - actual definitions in notification_service.hpp
enum class NotificationMethod;
enum class NotificationPriority;
enum class NotificationStatus;
struct NotificationMessage;

namespace notification_recovery {

/**
 * @brief Notification delivery retry configuration
 */
struct RetryConfig {
  bool enableRetry = true;
  int maxRetryAttempts = 3;
  std::chrono::milliseconds baseRetryDelay{5000};  // 5 seconds
  std::chrono::milliseconds maxRetryDelay{300000}; // 5 minutes
  double backoffMultiplier = 2.0;
  std::chrono::milliseconds deliveryTimeout{30000}; // 30 seconds
  int maxConcurrentRetries = 5;
  bool enableBulkRetry = true;
  std::chrono::minutes bulkRetryInterval{10};
};

/**
 * @brief Notification service recovery state
 */
struct ServiceRecoveryState {
  std::atomic<bool> isHealthy{true};
  std::atomic<bool> isRecovering{false};
  std::atomic<int> failedDeliveries{0};
  std::atomic<int> successfulDeliveries{0};
  std::atomic<int> activeRetries{0};
  std::chrono::system_clock::time_point lastSuccessfulDelivery;
  std::chrono::system_clock::time_point lastFailedDelivery;

  // Constructors and assignment operators for atomic types
  ServiceRecoveryState() = default;

  ServiceRecoveryState(const ServiceRecoveryState &other)
      : isHealthy(other.isHealthy.load()),
        isRecovering(other.isRecovering.load()),
        failedDeliveries(other.failedDeliveries.load()),
        successfulDeliveries(other.successfulDeliveries.load()),
        activeRetries(other.activeRetries.load()),
        lastSuccessfulDelivery(other.lastSuccessfulDelivery),
        lastFailedDelivery(other.lastFailedDelivery) {}

  ServiceRecoveryState &operator=(const ServiceRecoveryState &other) {
    if (this != &other) {
      isHealthy.store(other.isHealthy.load());
      isRecovering.store(other.isRecovering.load());
      failedDeliveries.store(other.failedDeliveries.load());
      successfulDeliveries.store(other.successfulDeliveries.load());
      activeRetries.store(other.activeRetries.load());
      lastSuccessfulDelivery = other.lastSuccessfulDelivery;
      lastFailedDelivery = other.lastFailedDelivery;
    }
    return *this;
  }

  void reset() {
    isHealthy.store(true);
    isRecovering.store(false);
    failedDeliveries.store(0);
    successfulDeliveries.store(0);
    activeRetries.store(0);
    lastSuccessfulDelivery = std::chrono::system_clock::now();
    lastFailedDelivery = std::chrono::system_clock::time_point::min();
  }

  double getFailureRate() const {
    int total = failedDeliveries.load() + successfulDeliveries.load();
    if (total == 0)
      return 0.0;
    return static_cast<double>(failedDeliveries.load()) / total;
  }
};

/**
 * @brief Failed notification for retry queue
 */
struct FailedNotification {
  std::string notificationId;
  std::string recipient;
  std::string content;
  std::chrono::system_clock::time_point nextRetryTime;
  std::string failureReason;
  int retryCount;
  int failedMethodIndex; // Index of failed method

  FailedNotification(const std::string &id, const std::string &recip,
                     const std::string &cont,
                     std::chrono::system_clock::time_point retryTime,
                     const std::string &reason, int retries, int methodIdx)
      : notificationId(id), recipient(recip), content(cont),
        nextRetryTime(retryTime), failureReason(reason), retryCount(retries),
        failedMethodIndex(methodIdx) {}

  bool isReadyForRetry() const {
    return std::chrono::system_clock::now() >= nextRetryTime;
  }
};

/**
 * @brief Notification delivery circuit breaker
 */
class NotificationCircuitBreaker {
public:
  enum class State {
    CLOSED,   // Normal operation
    OPEN,     // Failing fast
    HALF_OPEN // Testing recovery
  };

  NotificationCircuitBreaker(
      int failureThreshold = 10,
      std::chrono::minutes timeout = std::chrono::minutes(5),
      int successThreshold = 3)
      : failureThreshold_(failureThreshold), timeout_(timeout),
        successThreshold_(successThreshold), state_(State::CLOSED),
        failureCount_(0), successCount_(0) {}

  bool allowDelivery() {
    std::scoped_lock lock(mutex_);

    switch (state_) {
    case State::CLOSED:
      return true;

    case State::OPEN:
      if (isTimeoutExpired()) {
        state_ = State::HALF_OPEN;
        successCount_.store(0);
        return true;
      }
      return false;

    case State::HALF_OPEN:
      return true;
    }

    return false;
  }

  void onDeliverySuccess() {
    std::scoped_lock lock(mutex_);

    switch (state_) {
    case State::CLOSED:
      failureCount_.store(0);
      break;

    case State::HALF_OPEN:
      successCount_++;
      if (successCount_.load() >= successThreshold_) {
        state_ = State::CLOSED;
        failureCount_.store(0);
      }
      break;

    case State::OPEN:
      // Should not happen
      break;
    }
  }

  void onDeliveryFailure() {
    std::scoped_lock lock(mutex_);

    switch (state_) {
    case State::CLOSED:
      failureCount_++;
      if (failureCount_.load() >= failureThreshold_) {
        state_ = State::OPEN;
        lastFailureTime_ = std::chrono::steady_clock::now();
      }
      break;

    case State::HALF_OPEN:
      state_ = State::OPEN;
      lastFailureTime_ = std::chrono::steady_clock::now();
      break;

    case State::OPEN:
      lastFailureTime_ = std::chrono::steady_clock::now();
      break;
    }
  }

  State getState() const {
    std::scoped_lock lock(mutex_);
    return state_;
  }

  bool isInDegradedMode() const { return getState() == State::OPEN; }

private:
  bool isTimeoutExpired() const {
    auto now = std::chrono::steady_clock::now();
    return (now - lastFailureTime_) >= timeout_;
  }

  const int failureThreshold_;
  const std::chrono::minutes timeout_;
  const int successThreshold_;

  mutable std::mutex mutex_;
  State state_;
  std::atomic<int> failureCount_{0};
  std::atomic<int> successCount_{0};
  std::chrono::steady_clock::time_point lastFailureTime_;
};

/**
 * @brief Retry queue manager for failed notifications
 */
class RetryQueueManager {
public:
  RetryQueueManager() : config_(defaultConfig_) {}
  explicit RetryQueueManager(const RetryConfig &config) : config_(config) {}

  void addFailedNotification(const std::string &notificationId,
                             const std::string &recipient,
                             const std::string &content,
                             const std::string &reason, int failedMethodIndex) {
    std::scoped_lock lock(queueMutex_);

    auto retryDelay = calculateRetryDelay(0); // Start with 0 retries
    auto nextRetryTime = std::chrono::system_clock::now() + retryDelay;

    retryQueue_.emplace(notificationId, recipient, content, nextRetryTime,
                        reason, 0, failedMethodIndex);
  }

  size_t getQueueSize() const {
    std::scoped_lock lock(queueMutex_);
    return retryQueue_.size();
  }

  void clearQueue() {
    std::scoped_lock lock(queueMutex_);
    while (!retryQueue_.empty()) {
      retryQueue_.pop();
    }
  }

  std::vector<FailedNotification> getReadyForRetry() {
    std::scoped_lock lock(queueMutex_);
    std::vector<FailedNotification> ready;

    while (!retryQueue_.empty() && retryQueue_.front().isReadyForRetry()) {
      ready.push_back(retryQueue_.front());
      retryQueue_.pop();
    }

    return ready;
  }

  size_t size() const {
    std::scoped_lock lock(queueMutex_);
    return retryQueue_.size();
  }

  bool empty() const {
    std::scoped_lock lock(queueMutex_);
    return retryQueue_.empty();
  }

private:
  std::chrono::milliseconds calculateRetryDelay(int retryCount) const {
    if (retryCount <= 0)
      return config_.baseRetryDelay;

    auto delay =
        static_cast<long long>(config_.baseRetryDelay.count() *
                               std::pow(config_.backoffMultiplier, retryCount));
    delay =
        std::min(delay, static_cast<long long>(config_.maxRetryDelay.count()));

    return std::chrono::milliseconds(delay);
  }

private:
  const RetryConfig &config_;
  static const RetryConfig defaultConfig_;
  mutable std::mutex queueMutex_;
  std::queue<FailedNotification> retryQueue_;
};

} // namespace notification_recovery
