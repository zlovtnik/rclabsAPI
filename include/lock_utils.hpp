#pragma once

#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <type_traits>
#include <stdexcept>

namespace etl_plus {

// Forward declarations
class LockMonitor;
class DeadlockDetector;

/**
 * @brief Exception thrown when lock acquisition times out
 */
class LockTimeoutException : public std::runtime_error {
public:
    explicit LockTimeoutException(const std::string& message)
        : std::runtime_error(message) {}
};

/**
 * @brief Exception thrown when potential deadlock is detected
 */
class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(const std::string& message)
        : std::runtime_error(message) {}
};

/**
 * @brief Lock ordering levels to prevent deadlocks
 * 
 * Locks must be acquired in ascending order of their level.
 * This prevents circular wait conditions that lead to deadlocks.
 */
enum class LockLevel : int {
    CONFIG = 1,     // Configuration locks (highest priority)
    CONTAINER = 2,  // Container locks (collections, pools)
    RESOURCE = 3,   // Resource locks (files, connections)
    STATE = 4       // State locks (individual object state)
};

/**
 * @brief Ordered mutex that enforces lock ordering to prevent deadlocks
 */
template<LockLevel Level>
class OrderedMutex : public std::timed_mutex {
public:
    static constexpr LockLevel level = Level;
    
    OrderedMutex() : id_(generateId()) {}
    
    const std::string& getId() const { return id_; }
    LockLevel getLevel() const { return Level; }
    
private:
    std::string id_;
    static std::atomic<uint64_t> counter_;
    
    static std::string generateId() {
        return "mutex_" + std::to_string(counter_.fetch_add(1));
    }
};

template<LockLevel Level>
std::atomic<uint64_t> OrderedMutex<Level>::counter_{0};

/**
 * @brief Ordered shared mutex for reader-writer scenarios
 */
template<LockLevel Level>
class OrderedSharedMutex : public std::shared_timed_mutex {
public:
    static constexpr LockLevel level = Level;
    
    OrderedSharedMutex() : id_(generateId()) {}
    
    const std::string& getId() const { return id_; }
    LockLevel getLevel() const { return Level; }
    
private:
    std::string id_;
    static std::atomic<uint64_t> counter_;
    
    static std::string generateId() {
        return "shared_mutex_" + std::to_string(counter_.fetch_add(1));
    }
};

template<LockLevel Level>
std::atomic<uint64_t> OrderedSharedMutex<Level>::counter_{0};

// Convenience type aliases for common lock types
using ConfigMutex = OrderedMutex<LockLevel::CONFIG>;
using ContainerMutex = OrderedMutex<LockLevel::CONTAINER>;
using ResourceMutex = OrderedMutex<LockLevel::RESOURCE>;
using StateMutex = OrderedMutex<LockLevel::STATE>;

using ConfigSharedMutex = OrderedSharedMutex<LockLevel::CONFIG>;
using ContainerSharedMutex = OrderedSharedMutex<LockLevel::CONTAINER>;
using ResourceSharedMutex = OrderedSharedMutex<LockLevel::RESOURCE>;
using StateSharedMutex = OrderedSharedMutex<LockLevel::STATE>;

/**
 * @brief RAII lock helper with timeout and deadlock detection
 */
template<typename Mutex>
class ScopedTimedLock {
public:
    using mutex_type = Mutex;
    
    /**
     * @brief Construct lock with timeout
     * @param mutex The mutex to lock
     * @param timeout Maximum time to wait for lock acquisition
     * @param lockName Optional name for debugging and monitoring
     */
    explicit ScopedTimedLock(
        Mutex& mutex, 
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000),
        const std::string& lockName = ""
    ) : mutex_(mutex), locked_(false), lockName_(lockName.empty() ? generateLockName() : lockName) {
        
        // Check lock ordering if this is an ordered mutex
        if constexpr (hasLockLevel<Mutex>()) {
            checkLockOrdering(mutex.getLevel());
        }
        
        auto startTime = std::chrono::steady_clock::now();
        
        // Attempt to acquire lock with timeout
        if constexpr (std::is_same_v<Mutex, std::mutex> || std::is_same_v<Mutex, std::recursive_mutex>) {
            // Standard mutex doesn't have try_lock_for, use try_lock with polling
            auto endTime = startTime + timeout;
            while (std::chrono::steady_clock::now() < endTime) {
                if (mutex_.try_lock()) {
                    locked_ = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            // Timed mutex types have try_lock_for
            locked_ = mutex_.try_lock_for(timeout);
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto waitTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        if (!locked_) {
            recordLockFailure(lockName_, timeout, waitTime);
            throw LockTimeoutException("Failed to acquire lock '" + lockName_ + 
                                     "' within " + std::to_string(timeout.count()) + "ms");
        }
        
        // Record successful lock acquisition
        recordLockAcquisition(lockName_, waitTime);
        
        // Register with deadlock detector if available
        if constexpr (hasLockLevel<Mutex>()) {
            registerLockAcquisition(mutex.getLevel(), mutex.getId());
        }
    }
    
    /**
     * @brief Destructor - automatically releases lock
     */
    ~ScopedTimedLock() {
        if (locked_) {
            // Unregister from deadlock detector
            if constexpr (hasLockLevel<Mutex>()) {
                unregisterLockAcquisition(mutex_.getLevel(), getMutexId());
            }
            
            recordLockRelease(lockName_);
            mutex_.unlock();
        }
    }
    
    // Non-copyable, non-movable for safety
    ScopedTimedLock(const ScopedTimedLock&) = delete;
    ScopedTimedLock& operator=(const ScopedTimedLock&) = delete;
    ScopedTimedLock(ScopedTimedLock&&) = delete;
    ScopedTimedLock& operator=(ScopedTimedLock&&) = delete;
    
    /**
     * @brief Check if lock is currently held
     */
    bool owns_lock() const { return locked_; }
    
    /**
     * @brief Get lock name for debugging
     */
    const std::string& getLockName() const { return lockName_; }
    
private:
    Mutex& mutex_;
    bool locked_;
    std::string lockName_;
    
    template<typename T>
    static constexpr bool hasLockLevel() {
        return std::is_same_v<T, OrderedMutex<LockLevel::CONFIG>> ||
               std::is_same_v<T, OrderedMutex<LockLevel::CONTAINER>> ||
               std::is_same_v<T, OrderedMutex<LockLevel::RESOURCE>> ||
               std::is_same_v<T, OrderedMutex<LockLevel::STATE>> ||
               std::is_same_v<T, OrderedSharedMutex<LockLevel::CONFIG>> ||
               std::is_same_v<T, OrderedSharedMutex<LockLevel::CONTAINER>> ||
               std::is_same_v<T, OrderedSharedMutex<LockLevel::RESOURCE>> ||
               std::is_same_v<T, OrderedSharedMutex<LockLevel::STATE>>;
    }
    
    std::string getMutexId() const {
        if constexpr (hasLockLevel<Mutex>()) {
            return mutex_.getId();
        }
        return "unknown";
    }
    
    std::string generateLockName() const {
        return "lock_" + std::to_string(reinterpret_cast<uintptr_t>(&mutex_));
    }
    
    void checkLockOrdering(LockLevel level);
    void registerLockAcquisition(LockLevel level, const std::string& mutexId);
    void unregisterLockAcquisition(LockLevel level, const std::string& mutexId);
    void recordLockAcquisition(const std::string& lockName, std::chrono::microseconds waitTime);
    void recordLockFailure(const std::string& lockName, std::chrono::milliseconds timeout, std::chrono::microseconds waitTime);
    void recordLockRelease(const std::string& lockName);
};

/**
 * @brief RAII shared lock helper for reader scenarios
 */
template<typename SharedMutex>
class ScopedTimedSharedLock {
public:
    using mutex_type = SharedMutex;
    
    explicit ScopedTimedSharedLock(
        SharedMutex& mutex,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000),
        const std::string& lockName = ""
    ) : mutex_(mutex), locked_(false), lockName_(lockName.empty() ? generateLockName() : lockName) {
        
        if constexpr (hasLockLevel<SharedMutex>()) {
            checkLockOrdering(mutex.getLevel());
        }
        
        auto startTime = std::chrono::steady_clock::now();
        
        if constexpr (std::is_same_v<SharedMutex, std::shared_mutex>) {
            // shared_mutex doesn't have try_lock_shared_for, use try_lock_shared with polling
            auto endTime = startTime + timeout;
            while (std::chrono::steady_clock::now() < endTime) {
                if (mutex_.try_lock_shared()) {
                    locked_ = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            // shared_timed_mutex has try_lock_shared_for
            locked_ = mutex_.try_lock_shared_for(timeout);
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto waitTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        if (!locked_) {
            recordLockFailure(lockName_, timeout, waitTime);
            throw LockTimeoutException("Failed to acquire shared lock '" + lockName_ + 
                                     "' within " + std::to_string(timeout.count()) + "ms");
        }
        
        recordLockAcquisition(lockName_, waitTime);
        
        if constexpr (hasLockLevel<SharedMutex>()) {
            registerSharedLockAcquisition(mutex.getLevel(), mutex.getId());
        }
    }
    
    ~ScopedTimedSharedLock() {
        if (locked_) {
            if constexpr (hasLockLevel<SharedMutex>()) {
                unregisterSharedLockAcquisition(mutex_.getLevel(), getMutexId());
            }
            
            recordLockRelease(lockName_);
            mutex_.unlock_shared();
        }
    }
    
    // Non-copyable, non-movable
    ScopedTimedSharedLock(const ScopedTimedSharedLock&) = delete;
    ScopedTimedSharedLock& operator=(const ScopedTimedSharedLock&) = delete;
    ScopedTimedSharedLock(ScopedTimedSharedLock&&) = delete;
    ScopedTimedSharedLock& operator=(ScopedTimedSharedLock&&) = delete;
    
    bool owns_lock() const { return locked_; }
    const std::string& getLockName() const { return lockName_; }
    
private:
    SharedMutex& mutex_;
    bool locked_;
    std::string lockName_;
    
    template<typename T>
    static constexpr bool hasLockLevel() {
        return std::is_same_v<T, OrderedMutex<LockLevel::CONFIG>> ||
               std::is_same_v<T, OrderedMutex<LockLevel::CONTAINER>> ||
               std::is_same_v<T, OrderedMutex<LockLevel::RESOURCE>> ||
               std::is_same_v<T, OrderedMutex<LockLevel::STATE>> ||
               std::is_same_v<T, OrderedSharedMutex<LockLevel::CONFIG>> ||
               std::is_same_v<T, OrderedSharedMutex<LockLevel::CONTAINER>> ||
               std::is_same_v<T, OrderedSharedMutex<LockLevel::RESOURCE>> ||
               std::is_same_v<T, OrderedSharedMutex<LockLevel::STATE>>;
    }
    
    std::string getMutexId() const {
        if constexpr (hasLockLevel<SharedMutex>()) {
            return mutex_.getId();
        }
        return "unknown";
    }
    
    std::string generateLockName() const {
        return "shared_lock_" + std::to_string(reinterpret_cast<uintptr_t>(&mutex_));
    }
    
    void checkLockOrdering(LockLevel level);
    void registerSharedLockAcquisition(LockLevel level, const std::string& mutexId);
    void unregisterSharedLockAcquisition(LockLevel level, const std::string& mutexId);
    void recordLockAcquisition(const std::string& lockName, std::chrono::microseconds waitTime);
    void recordLockFailure(const std::string& lockName, std::chrono::milliseconds timeout, std::chrono::microseconds waitTime);
    void recordLockRelease(const std::string& lockName);
};

/**
 * @brief Lock performance monitoring and statistics
 */
class LockMonitor {
public:
    struct LockStats {
        std::atomic<uint64_t> acquisitions{0};
        std::atomic<uint64_t> failures{0};
        std::atomic<uint64_t> totalWaitTime{0};  // microseconds
        std::atomic<uint64_t> maxWaitTime{0};    // microseconds
        std::atomic<uint64_t> contentions{0};
        
        // Copy constructor and assignment operator
        LockStats() = default;
        LockStats(const LockStats& other) 
            : acquisitions(other.acquisitions.load())
            , failures(other.failures.load())
            , totalWaitTime(other.totalWaitTime.load())
            , maxWaitTime(other.maxWaitTime.load())
            , contentions(other.contentions.load()) {}
        
        LockStats& operator=(const LockStats& other) {
            if (this != &other) {
                acquisitions.store(other.acquisitions.load());
                failures.store(other.failures.load());
                totalWaitTime.store(other.totalWaitTime.load());
                maxWaitTime.store(other.maxWaitTime.load());
                contentions.store(other.contentions.load());
            }
            return *this;
        }
        
        double getAverageWaitTime() const {
            auto acq = acquisitions.load();
            return acq > 0 ? static_cast<double>(totalWaitTime.load()) / acq : 0.0;
        }
        
        double getFailureRate() const {
            auto total = acquisitions.load() + failures.load();
            return total > 0 ? static_cast<double>(failures.load()) / total : 0.0;
        }
    };
    
    static LockMonitor& getInstance();
    
    void recordAcquisition(const std::string& lockName, std::chrono::microseconds waitTime);
    void recordFailure(const std::string& lockName, std::chrono::milliseconds timeout, std::chrono::microseconds waitTime);
    void recordRelease(const std::string& lockName);
    
    LockStats getLockStats(const std::string& lockName) const;
    std::vector<std::pair<std::string, LockStats>> getAllStats() const;
    
    void reset();
    void enableDetailedLogging(bool enable) { detailedLogging_ = enable; }
    
private:
    LockMonitor() = default;
    
    mutable std::shared_mutex statsMutex_;
    std::unordered_map<std::string, LockStats> lockStats_;
    std::atomic<bool> detailedLogging_{false};
};

/**
 * @brief Deadlock detection and prevention
 */
class DeadlockDetector {
public:
    static DeadlockDetector& getInstance();
    
    void registerLockAcquisition(std::thread::id threadId, LockLevel level, const std::string& mutexId);
    void unregisterLockAcquisition(std::thread::id threadId, LockLevel level, const std::string& mutexId);
    
    void registerSharedLockAcquisition(std::thread::id threadId, LockLevel level, const std::string& mutexId);
    void unregisterSharedLockAcquisition(std::thread::id threadId, LockLevel level, const std::string& mutexId);
    
    void checkForDeadlocks();
    void enableDeadlockDetection(bool enable) { enabled_ = enable; }
    
    struct ThreadLockInfo {
        std::vector<std::pair<LockLevel, std::string>> exclusiveLocks;
        std::vector<std::pair<LockLevel, std::string>> sharedLocks;
    };
    
    std::unordered_map<std::thread::id, ThreadLockInfo> getThreadLockInfo() const;
    
private:
    DeadlockDetector() = default;
    
    mutable std::shared_mutex lockInfoMutex_;
    std::unordered_map<std::thread::id, ThreadLockInfo> threadLocks_;
    std::atomic<bool> enabled_{true};
    
    void validateLockOrdering(std::thread::id threadId, LockLevel newLevel);
};

// Convenience macros for common locking patterns
#define SCOPED_LOCK(mutex) \
    etl_plus::ScopedTimedLock lock(mutex, std::chrono::milliseconds(5000), #mutex)

#define SCOPED_LOCK_TIMEOUT(mutex, timeout_ms) \
    etl_plus::ScopedTimedLock lock(mutex, std::chrono::milliseconds(timeout_ms), #mutex)

#define SCOPED_SHARED_LOCK(mutex) \
    etl_plus::ScopedTimedSharedLock lock(mutex, std::chrono::milliseconds(5000), #mutex)

#define SCOPED_SHARED_LOCK_TIMEOUT(mutex, timeout_ms) \
    etl_plus::ScopedTimedSharedLock lock(mutex, std::chrono::milliseconds(timeout_ms), #mutex)

// Template method implementations (must be after class definitions)
template<typename Mutex>
void ScopedTimedLock<Mutex>::checkLockOrdering(LockLevel level) {
    DeadlockDetector::getInstance().registerLockAcquisition(
        std::this_thread::get_id(), level, getMutexId()
    );
}

template<typename Mutex>
void ScopedTimedLock<Mutex>::registerLockAcquisition(LockLevel level, const std::string& mutexId) {
    // This is handled in checkLockOrdering for simplicity
}

template<typename Mutex>
void ScopedTimedLock<Mutex>::unregisterLockAcquisition(LockLevel level, const std::string& mutexId) {
    DeadlockDetector::getInstance().unregisterLockAcquisition(
        std::this_thread::get_id(), level, mutexId
    );
}

template<typename Mutex>
void ScopedTimedLock<Mutex>::recordLockAcquisition(const std::string& lockName, std::chrono::microseconds waitTime) {
    LockMonitor::getInstance().recordAcquisition(lockName, waitTime);
}

template<typename Mutex>
void ScopedTimedLock<Mutex>::recordLockFailure(const std::string& lockName, std::chrono::milliseconds timeout, std::chrono::microseconds waitTime) {
    LockMonitor::getInstance().recordFailure(lockName, timeout, waitTime);
}

template<typename Mutex>
void ScopedTimedLock<Mutex>::recordLockRelease(const std::string& lockName) {
    LockMonitor::getInstance().recordRelease(lockName);
}

template<typename SharedMutex>
void ScopedTimedSharedLock<SharedMutex>::checkLockOrdering(LockLevel level) {
    DeadlockDetector::getInstance().registerSharedLockAcquisition(
        std::this_thread::get_id(), level, getMutexId()
    );
}

template<typename SharedMutex>
void ScopedTimedSharedLock<SharedMutex>::registerSharedLockAcquisition(LockLevel level, const std::string& mutexId) {
    // This is handled in checkLockOrdering for simplicity
}

template<typename SharedMutex>
void ScopedTimedSharedLock<SharedMutex>::unregisterSharedLockAcquisition(LockLevel level, const std::string& mutexId) {
    DeadlockDetector::getInstance().unregisterSharedLockAcquisition(
        std::this_thread::get_id(), level, mutexId
    );
}

template<typename SharedMutex>
void ScopedTimedSharedLock<SharedMutex>::recordLockAcquisition(const std::string& lockName, std::chrono::microseconds waitTime) {
    LockMonitor::getInstance().recordAcquisition(lockName, waitTime);
}

template<typename SharedMutex>
void ScopedTimedSharedLock<SharedMutex>::recordLockFailure(const std::string& lockName, std::chrono::milliseconds timeout, std::chrono::microseconds waitTime) {
    LockMonitor::getInstance().recordFailure(lockName, timeout, waitTime);
}

template<typename SharedMutex>
void ScopedTimedSharedLock<SharedMutex>::recordLockRelease(const std::string& lockName) {
    LockMonitor::getInstance().recordRelease(lockName);
}

} // namespace etl_plus