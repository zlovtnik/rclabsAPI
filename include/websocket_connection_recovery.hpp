#pragma once

#include <chrono>
#include <atomic>
#include <memory>
#include <queue>
#include <mutex>

namespace websocket_recovery {

/**
 * @brief Connection recovery configuration and state management
 */
struct ConnectionRecoveryConfig {
    bool enableAutoReconnect = true;
    int maxReconnectAttempts = 5;
    std::chrono::milliseconds baseReconnectDelay{1000};  // 1 second
    std::chrono::milliseconds maxReconnectDelay{30000};  // 30 seconds
    double backoffMultiplier = 2.0;
    int messageQueueMaxSize = 1000;
    std::chrono::seconds connectionTimeout{30};
    std::chrono::seconds heartbeatInterval{30};
    bool enableHeartbeat = true;
    int maxMissedHeartbeats = 3;
};

/**
 * @brief Connection recovery state tracking
 */
struct ConnectionRecoveryState {
    std::atomic<bool> isRecovering{false};
    std::atomic<int> reconnectAttempts{0};
    std::atomic<int> missedHeartbeats{0};
    std::chrono::system_clock::time_point lastHeartbeat;
    std::chrono::system_clock::time_point lastReconnectAttempt;
    std::queue<std::string> pendingMessages;
    mutable std::mutex pendingMessagesMutex;
    
    // Constructors for atomic types
    ConnectionRecoveryState() = default;
    
    ConnectionRecoveryState(const ConnectionRecoveryState& other) :
        isRecovering(other.isRecovering.load()),
        reconnectAttempts(other.reconnectAttempts.load()),
        missedHeartbeats(other.missedHeartbeats.load()),
        lastHeartbeat(other.lastHeartbeat),
        lastReconnectAttempt(other.lastReconnectAttempt),
        pendingMessages(other.pendingMessages) {}
        
    ConnectionRecoveryState& operator=(const ConnectionRecoveryState& other) {
        if (this != &other) {
            isRecovering.store(other.isRecovering.load());
            reconnectAttempts.store(other.reconnectAttempts.load());
            missedHeartbeats.store(other.missedHeartbeats.load());
            lastHeartbeat = other.lastHeartbeat;
            lastReconnectAttempt = other.lastReconnectAttempt;
            std::lock_guard<std::mutex> lock(pendingMessagesMutex);
            pendingMessages = other.pendingMessages;
        }
        return *this;
    }
    
    void reset() {
        isRecovering.store(false);
        reconnectAttempts.store(0);
        missedHeartbeats.store(0);
        lastHeartbeat = std::chrono::system_clock::now();
        lastReconnectAttempt = std::chrono::system_clock::time_point::min();
        
        std::lock_guard<std::mutex> lock(pendingMessagesMutex);
        while (!pendingMessages.empty()) {
            pendingMessages.pop();
        }
    }
    
    bool shouldAttemptReconnect(const ConnectionRecoveryConfig& config) const {
        if (!config.enableAutoReconnect) return false;
        if (reconnectAttempts.load() >= config.maxReconnectAttempts) return false;
        
        auto now = std::chrono::system_clock::now();
        auto timeSinceLastAttempt = now - lastReconnectAttempt;
        auto requiredDelay = calculateBackoffDelay(config);
        
        return timeSinceLastAttempt >= requiredDelay;
    }
    
    std::chrono::milliseconds calculateBackoffDelay(const ConnectionRecoveryConfig& config) const {
        int attempts = reconnectAttempts.load();
        if (attempts <= 0) return config.baseReconnectDelay;
        
        auto delay = static_cast<long long>(config.baseReconnectDelay.count() * 
                                          std::pow(config.backoffMultiplier, attempts - 1));
        delay = std::min(delay, static_cast<long long>(config.maxReconnectDelay.count()));
        
        return std::chrono::milliseconds(delay);
    }
    
    void addPendingMessage(const std::string& message, const ConnectionRecoveryConfig& config) {
        std::lock_guard<std::mutex> lock(pendingMessagesMutex);
        
        // Drop oldest messages if queue is full
        while (pendingMessages.size() >= static_cast<size_t>(config.messageQueueMaxSize)) {
            pendingMessages.pop();
        }
        
        pendingMessages.push(message);
    }
    
    std::vector<std::string> getPendingMessages() {
        std::lock_guard<std::mutex> lock(pendingMessagesMutex);
        std::vector<std::string> messages;
        
        while (!pendingMessages.empty()) {
            messages.push_back(pendingMessages.front());
            pendingMessages.pop();
        }
        
        return messages;
    }
};

/**
 * @brief Circuit breaker pattern for WebSocket operations
 */
class ConnectionCircuitBreaker {
public:
    enum class State {
        CLOSED,    // Normal operation
        OPEN,      // Failing fast
        HALF_OPEN  // Testing if service recovered
    };
    
    ConnectionCircuitBreaker(
        int failureThreshold = 5,
        std::chrono::seconds timeout = std::chrono::seconds(60),
        int successThreshold = 3
    ) : failureThreshold_(failureThreshold)
      , timeout_(timeout)
      , successThreshold_(successThreshold)
      , state_(State::CLOSED)
      , failureCount_(0)
      , successCount_(0) {}
    
    bool allowOperation() {
        std::lock_guard<std::mutex> lock(mutex_);
        
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
        std::lock_guard<std::mutex> lock(mutex_);
        
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
        std::lock_guard<std::mutex> lock(mutex_);
        
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
                // Already open, just update timestamp
                lastFailureTime_ = std::chrono::steady_clock::now();
                break;
        }
    }
    
    State getState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    
    int getFailureCount() const {
        return failureCount_.load();
    }
    
    int getSuccessCount() const {
        return successCount_.load();
    }

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

} // namespace websocket_recovery
