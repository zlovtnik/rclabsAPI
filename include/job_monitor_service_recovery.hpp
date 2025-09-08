#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <queue>

namespace job_monitoring_recovery {

/**
 * @brief Service recovery configuration
 */
struct ServiceRecoveryConfig {
  bool enableGracefulDegradation = true;
  bool enableAutoRecovery = true;
  int maxRecoveryAttempts = 3;
  std::chrono::milliseconds baseRecoveryDelay{5000}; // 5 seconds
  std::chrono::milliseconds maxRecoveryDelay{60000}; // 60 seconds
  double backoffMultiplier = 2.0;
  int eventQueueMaxSize = 10000;
  std::chrono::seconds healthCheckInterval{30};
  bool enableHealthChecks = true;
  int maxFailedHealthChecks = 3;
};

/**
 * @brief Service recovery state
 */
struct ServiceRecoveryState {
  std::atomic<bool> isHealthy{true};
  std::atomic<bool> isRecovering{false};
  std::atomic<int> recoveryAttempts{0};
  std::atomic<int> failedHealthChecks{0};
  std::chrono::system_clock::time_point lastRecoveryAttempt;
  std::chrono::system_clock::time_point lastHealthCheck;

  // Constructors for atomic types
  ServiceRecoveryState() = default;

  ServiceRecoveryState(const ServiceRecoveryState &other)
      : isHealthy(other.isHealthy.load()),
        isRecovering(other.isRecovering.load()),
        recoveryAttempts(other.recoveryAttempts.load()),
        failedHealthChecks(other.failedHealthChecks.load()),
        lastRecoveryAttempt(other.lastRecoveryAttempt),
        lastHealthCheck(other.lastHealthCheck) {}

  ServiceRecoveryState &operator=(const ServiceRecoveryState &other) {
    if (this != &other) {
      isHealthy.store(other.isHealthy.load());
      isRecovering.store(other.isRecovering.load());
      recoveryAttempts.store(other.recoveryAttempts.load());
      failedHealthChecks.store(other.failedHealthChecks.load());
      lastRecoveryAttempt = other.lastRecoveryAttempt;
      lastHealthCheck = other.lastHealthCheck;
    }
    return *this;
  }

  void reset() {
    isHealthy.store(true);
    isRecovering.store(false);
    recoveryAttempts.store(0);
    failedHealthChecks.store(0);
    lastRecoveryAttempt = std::chrono::system_clock::time_point::min();
    lastHealthCheck = std::chrono::system_clock::now();
  }

  bool shouldAttemptRecovery(const ServiceRecoveryConfig &config) const {
    if (!config.enableAutoRecovery)
      return false;
    if (recoveryAttempts.load() >= config.maxRecoveryAttempts)
      return false;

    auto now = std::chrono::system_clock::now();
    auto timeSinceLastAttempt = now - lastRecoveryAttempt;
    auto requiredDelay = calculateBackoffDelay(config);

    return timeSinceLastAttempt >= requiredDelay;
  }

  std::chrono::milliseconds
  calculateBackoffDelay(const ServiceRecoveryConfig &config) const {
    int attempts = recoveryAttempts.load();
    if (attempts <= 0)
      return config.baseRecoveryDelay;

    auto delay = static_cast<long long>(
        config.baseRecoveryDelay.count() *
        std::pow(config.backoffMultiplier, attempts - 1));
    delay = std::min(delay,
                     static_cast<long long>(config.maxRecoveryDelay.count()));

    return std::chrono::milliseconds(delay);
  }
};

/**
 * @brief Event queue for degraded mode operation
 */
template <typename EventType> class DegradedModeEventQueue {
public:
  explicit DegradedModeEventQueue(size_t maxSize = 10000) : maxSize_(maxSize) {}

  void enqueue(const EventType &event) {
    std::scoped_lock lock(mutex_);

    // Drop oldest events if queue is full
    while (queue_.size() >= maxSize_) {
      queue_.pop();
    }

    queue_.push(event);
  }

  std::vector<EventType> dequeueAll() {
    std::scoped_lock lock(mutex_);
    std::vector<EventType> events;

    while (!queue_.empty()) {
      events.push_back(queue_.front());
      queue_.pop();
    }

    return events;
  }

  size_t size() const {
    std::scoped_lock lock(mutex_);
    return queue_.size();
  }

  bool empty() const {
    std::scoped_lock lock(mutex_);
    return queue_.empty();
  }

private:
  mutable std::mutex mutex_;
  std::queue<EventType> queue_;
  size_t maxSize_;
};

/**
 * @brief Circuit breaker for service operations
 */
class ServiceCircuitBreaker {
public:
  enum class State {
    CLOSED,   // Normal operation
    OPEN,     // Failing fast (degraded mode)
    HALF_OPEN // Testing if service recovered
  };

  ServiceCircuitBreaker(int failureThreshold = 5,
                        std::chrono::seconds timeout = std::chrono::seconds(60),
                        int successThreshold = 3)
      : failureThreshold_(failureThreshold), timeout_(timeout),
        successThreshold_(successThreshold), state_(State::CLOSED),
        failureCount_(0), successCount_(0) {}

  bool allowOperation() {
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

  void onSuccess() {
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

  void onFailure() {
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
      // Already open
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
  const std::chrono::seconds timeout_;
  const int successThreshold_;

  mutable std::mutex mutex_;
  State state_;
  std::atomic<int> failureCount_{0};
  std::atomic<int> successCount_{0};
  std::chrono::steady_clock::time_point lastFailureTime_;
};

} // namespace job_monitoring_recovery
