#include "lock_utils.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cassert>

namespace etl_plus {

// LockMonitor implementation
LockMonitor& LockMonitor::getInstance() {
    static LockMonitor instance;
    return instance;
}

void LockMonitor::recordAcquisition(const std::string& lockName, std::chrono::microseconds waitTime) {
    std::shared_lock<std::shared_mutex> lock(statsMutex_);
    
    auto& stats = lockStats_[lockName];
    stats.acquisitions.fetch_add(1);
    stats.totalWaitTime.fetch_add(waitTime.count());
    
    // Update max wait time atomically
    uint64_t currentMax = stats.maxWaitTime.load();
    uint64_t newWaitTime = waitTime.count();
    while (newWaitTime > currentMax && 
           !stats.maxWaitTime.compare_exchange_weak(currentMax, newWaitTime)) {
        // Keep trying until we successfully update or find a larger value
    }
    
    // Record contention if wait time is significant (> 1ms)
    if (waitTime.count() > 1000) {
        stats.contentions.fetch_add(1);
    }
    
    if (detailedLogging_.load()) {
        std::cout << "[LockMonitor] Acquired lock '" << lockName 
                  << "' after " << waitTime.count() << "μs" << std::endl;
    }
}

void LockMonitor::recordFailure(const std::string& lockName, 
                               std::chrono::milliseconds timeout, 
                               std::chrono::microseconds waitTime) {
    std::shared_lock<std::shared_mutex> lock(statsMutex_);
    
    auto& stats = lockStats_[lockName];
    stats.failures.fetch_add(1);
    
    if (detailedLogging_.load()) {
        std::cout << "[LockMonitor] Failed to acquire lock '" << lockName 
                  << "' after " << waitTime.count() << "μs (timeout: " 
                  << timeout.count() << "ms)" << std::endl;
    }
}

void LockMonitor::recordRelease(const std::string& lockName) {
    if (detailedLogging_.load()) {
        std::cout << "[LockMonitor] Released lock '" << lockName << "'" << std::endl;
    }
}

LockMonitor::LockStats LockMonitor::getLockStats(const std::string& lockName) const {
    std::shared_lock<std::shared_mutex> lock(statsMutex_);
    
    auto it = lockStats_.find(lockName);
    if (it != lockStats_.end()) {
        return it->second;
    }
    
    return LockStats{}; // Return default-constructed stats
}

std::vector<std::pair<std::string, LockMonitor::LockStats>> LockMonitor::getAllStats() const {
    std::shared_lock<std::shared_mutex> lock(statsMutex_);
    
    std::vector<std::pair<std::string, LockStats>> result;
    result.reserve(lockStats_.size());
    
    for (const auto& [name, stats] : lockStats_) {
        result.emplace_back(name, stats);
    }
    
    return result;
}

void LockMonitor::reset() {
    std::unique_lock<std::shared_mutex> lock(statsMutex_);
    lockStats_.clear();
}

// DeadlockDetector implementation
DeadlockDetector& DeadlockDetector::getInstance() {
    static DeadlockDetector instance;
    return instance;
}

void DeadlockDetector::registerLockAcquisition(std::thread::id threadId, 
                                              LockLevel level, 
                                              const std::string& mutexId) {
    if (!enabled_.load()) return;
    
    std::unique_lock<std::shared_mutex> lock(lockInfoMutex_);
    
    // Validate lock ordering before acquiring
    validateLockOrdering(threadId, level);
    
    auto& threadInfo = threadLocks_[threadId];
    threadInfo.exclusiveLocks.emplace_back(level, mutexId);
    
    // Sort to maintain ordering
    std::sort(threadInfo.exclusiveLocks.begin(), threadInfo.exclusiveLocks.end());
}

void DeadlockDetector::unregisterLockAcquisition(std::thread::id threadId, 
                                                LockLevel level, 
                                                const std::string& mutexId) {
    if (!enabled_.load()) return;
    
    std::unique_lock<std::shared_mutex> lock(lockInfoMutex_);
    
    auto it = threadLocks_.find(threadId);
    if (it != threadLocks_.end()) {
        auto& locks = it->second.exclusiveLocks;
        locks.erase(
            std::remove_if(locks.begin(), locks.end(),
                [level, &mutexId](const auto& pair) {
                    return pair.first == level && pair.second == mutexId;
                }),
            locks.end()
        );
        
        // Clean up empty thread entries
        if (locks.empty() && it->second.sharedLocks.empty()) {
            threadLocks_.erase(it);
        }
    }
}

void DeadlockDetector::registerSharedLockAcquisition(std::thread::id threadId, 
                                                    LockLevel level, 
                                                    const std::string& mutexId) {
    if (!enabled_.load()) return;
    
    std::unique_lock<std::shared_mutex> lock(lockInfoMutex_);
    
    // Validate lock ordering
    validateLockOrdering(threadId, level);
    
    auto& threadInfo = threadLocks_[threadId];
    threadInfo.sharedLocks.emplace_back(level, mutexId);
    
    // Sort to maintain ordering
    std::sort(threadInfo.sharedLocks.begin(), threadInfo.sharedLocks.end());
}

void DeadlockDetector::unregisterSharedLockAcquisition(std::thread::id threadId, 
                                                      LockLevel level, 
                                                      const std::string& mutexId) {
    if (!enabled_.load()) return;
    
    std::unique_lock<std::shared_mutex> lock(lockInfoMutex_);
    
    auto it = threadLocks_.find(threadId);
    if (it != threadLocks_.end()) {
        auto& locks = it->second.sharedLocks;
        locks.erase(
            std::remove_if(locks.begin(), locks.end(),
                [level, &mutexId](const auto& pair) {
                    return pair.first == level && pair.second == mutexId;
                }),
            locks.end()
        );
        
        // Clean up empty thread entries
        if (locks.empty() && it->second.exclusiveLocks.empty()) {
            threadLocks_.erase(it);
        }
    }
}

void DeadlockDetector::validateLockOrdering(std::thread::id threadId, LockLevel newLevel) {
    auto it = threadLocks_.find(threadId);
    if (it == threadLocks_.end()) {
        return; // No existing locks, ordering is fine
    }
    
    const auto& threadInfo = it->second;
    
    // Check exclusive locks
    for (const auto& [level, mutexId] : threadInfo.exclusiveLocks) {
        if (static_cast<int>(level) > static_cast<int>(newLevel)) {
            std::ostringstream oss;
            oss << "Lock ordering violation detected! Thread " << threadId 
                << " already holds lock at level " << static_cast<int>(level)
                << " (mutex: " << mutexId << ") and is trying to acquire lock at level "
                << static_cast<int>(newLevel) << ". This may cause deadlock.";
            throw DeadlockException(oss.str());
        }
    }
    
    // Check shared locks
    for (const auto& [level, mutexId] : threadInfo.sharedLocks) {
        if (static_cast<int>(level) > static_cast<int>(newLevel)) {
            std::ostringstream oss;
            oss << "Lock ordering violation detected! Thread " << threadId 
                << " already holds shared lock at level " << static_cast<int>(level)
                << " (mutex: " << mutexId << ") and is trying to acquire lock at level "
                << static_cast<int>(newLevel) << ". This may cause deadlock.";
            throw DeadlockException(oss.str());
        }
    }
}

void DeadlockDetector::checkForDeadlocks() {
    if (!enabled_.load()) return;
    
    std::shared_lock<std::shared_mutex> lock(lockInfoMutex_);
    
    // Simple deadlock detection: look for circular wait patterns
    // This is a basic implementation - more sophisticated algorithms exist
    
    for (const auto& [threadId1, info1] : threadLocks_) {
        for (const auto& [threadId2, info2] : threadLocks_) {
            if (threadId1 >= threadId2) continue; // Avoid duplicate checks
            
            // Check if thread1 holds locks that thread2 might be waiting for
            // and vice versa (simplified check)
            bool potential_deadlock = false;
            
            // This is a simplified check - in practice, you'd need more
            // sophisticated cycle detection in the wait-for graph
            if (!info1.exclusiveLocks.empty() && !info2.exclusiveLocks.empty()) {
                // Check for potential circular dependencies
                auto max1 = std::max_element(info1.exclusiveLocks.begin(), info1.exclusiveLocks.end());
                auto min2 = std::min_element(info2.exclusiveLocks.begin(), info2.exclusiveLocks.end());
                
                if (max1 != info1.exclusiveLocks.end() && min2 != info2.exclusiveLocks.end()) {
                    if (max1->first > min2->first) {
                        potential_deadlock = true;
                    }
                }
            }
            
            if (potential_deadlock) {
                std::ostringstream oss;
                oss << "Potential deadlock detected between threads " << threadId1 
                    << " and " << threadId2 << ". Review lock acquisition order.";
                
                // For now, just log the warning. In production, you might want to
                // take more aggressive action or provide more detailed analysis.
                std::cerr << "[DeadlockDetector] WARNING: " << oss.str() << std::endl;
            }
        }
    }
}

std::unordered_map<std::thread::id, DeadlockDetector::ThreadLockInfo> 
DeadlockDetector::getThreadLockInfo() const {
    std::shared_lock<std::shared_mutex> lock(lockInfoMutex_);
    return threadLocks_;
}



} // namespace etl_plus