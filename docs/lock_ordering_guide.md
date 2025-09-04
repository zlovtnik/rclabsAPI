# Lock Ordering and Concurrency Guidelines

## Overview

This document provides comprehensive guidelines for using the RAII lock utilities in the ETL Plus system. Following these guidelines will help prevent deadlocks, improve performance, and maintain thread safety.

## Lock Hierarchy

The system uses a hierarchical lock ordering system to prevent deadlocks. Locks must always be acquired in ascending order of their level:

### Lock Levels

1. **CONFIG (Level 1)** - Configuration and system-wide settings
2. **CONTAINER (Level 2)** - Collections, pools, and container objects  
3. **RESOURCE (Level 3)** - Files, connections, and external resources
4. **STATE (Level 4)** - Individual object state and data

### Lock Types

```cpp
// Exclusive locks
using ConfigMutex = OrderedMutex<LockLevel::CONFIG>;
using ContainerMutex = OrderedMutex<LockLevel::CONTAINER>;
using ResourceMutex = OrderedMutex<LockLevel::RESOURCE>;
using StateMutex = OrderedMutex<LockLevel::STATE>;

// Shared locks (reader-writer)
using ConfigSharedMutex = OrderedSharedMutex<LockLevel::CONFIG>;
using ContainerSharedMutex = OrderedSharedMutex<LockLevel::CONTAINER>;
using ResourceSharedMutex = OrderedSharedMutex<LockLevel::RESOURCE>;
using StateSharedMutex = OrderedSharedMutex<LockLevel::STATE>;
```

## Usage Patterns

### Basic Locking

```cpp
#include "lock_utils.hpp"

class MyComponent {
private:
    StateMutex dataMutex_;
    std::string data_;

public:
    void updateData(const std::string& newData) {
        // Automatic timeout and monitoring
        ScopedTimedLock lock(dataMutex_, std::chrono::milliseconds(1000), "data_update");
        data_ = newData;
        // Lock automatically released when scope ends
    }
};
```

### Convenience Macros

```cpp
void quickUpdate() {
    StateMutex mutex_;
    
    // Simple lock with default 5-second timeout
    SCOPED_LOCK(mutex_);
    
    // Custom timeout
    SCOPED_LOCK_TIMEOUT(mutex_, 2000); // 2 second timeout
}

void readOnlyOperation() {
    StateSharedMutex sharedMutex_;
    
    // Shared lock for readers
    SCOPED_SHARED_LOCK(sharedMutex_);
    
    // Custom timeout for shared lock
    SCOPED_SHARED_LOCK_TIMEOUT(sharedMutex_, 1000);
}
```

### Correct Lock Ordering

```cpp
class SystemManager {
private:
    ConfigMutex configMutex_;           // Level 1
    ContainerMutex connectionsMutex_;   // Level 2
    ResourceMutex fileMutex_;          // Level 3
    StateMutex stateMutex_;            // Level 4

public:
    void complexOperation() {
        // ✅ CORRECT: Acquire locks in ascending order
        ScopedTimedLock configLock(configMutex_, std::chrono::milliseconds(1000), "config");
        ScopedTimedLock containerLock(connectionsMutex_, std::chrono::milliseconds(1000), "connections");
        ScopedTimedLock resourceLock(fileMutex_, std::chrono::milliseconds(1000), "file");
        ScopedTimedLock stateLock(stateMutex_, std::chrono::milliseconds(1000), "state");
        
        // Perform complex operation requiring all locks
    }
    
    void incorrectOperation() {
        // ❌ INCORRECT: This will throw DeadlockException
        ScopedTimedLock stateLock(stateMutex_, std::chrono::milliseconds(1000), "state");
        ScopedTimedLock configLock(configMutex_, std::chrono::milliseconds(1000), "config");
        // DeadlockException thrown here!
    }
};
```

### Reader-Writer Patterns

```cpp
class DataCache {
private:
    ResourceSharedMutex cacheMutex_;
    std::unordered_map<std::string, std::string> cache_;

public:
    std::string getValue(const std::string& key) {
        // Multiple readers can access simultaneously
        ScopedTimedSharedLock lock(cacheMutex_, std::chrono::milliseconds(500), "cache_read");
        
        auto it = cache_.find(key);
        return it != cache_.end() ? it->second : "";
    }
    
    void setValue(const std::string& key, const std::string& value) {
        // Exclusive access for writers
        ScopedTimedLock lock(cacheMutex_, std::chrono::milliseconds(1000), "cache_write");
        cache_[key] = value;
    }
};
```

## Component-Specific Guidelines

### Logger Components

```cpp
class LogFileManager {
private:
    ResourceMutex fileMutex_;      // File operations
    StateMutex rotationMutex_;     // Rotation state

public:
    void writeLog(const std::string& message) {
        // File operations at resource level
        ScopedTimedLock lock(fileMutex_, std::chrono::milliseconds(2000), "log_write");
        // Write to file
    }
    
    void rotateFile() {
        // Need both file and rotation locks
        ScopedTimedLock fileLock(fileMutex_, std::chrono::milliseconds(5000), "file_rotation");
        ScopedTimedLock rotationLock(rotationMutex_, std::chrono::milliseconds(1000), "rotation_state");
        // Perform rotation
    }
};
```

### WebSocket Manager

```cpp
class ConnectionPool {
private:
    ContainerMutex connectionsMutex_;  // Connection container
    StateMutex metricsMutex_;         // Metrics state

public:
    void addConnection(std::shared_ptr<Connection> conn) {
        ScopedTimedLock connectionsLock(connectionsMutex_, std::chrono::milliseconds(1000), "add_connection");
        ScopedTimedLock metricsLock(metricsMutex_, std::chrono::milliseconds(500), "update_metrics");
        
        // Add connection and update metrics
    }
};
```

### Request Handler

```cpp
class RequestValidator {
private:
    ConfigSharedMutex configMutex_;    // Read-only config access
    StateMutex validationMutex_;       // Validation state

public:
    bool validateRequest(const Request& req) {
        // Read configuration (shared lock)
        ScopedTimedSharedLock configLock(configMutex_, std::chrono::milliseconds(500), "config_read");
        
        // Update validation state (exclusive lock)
        ScopedTimedLock stateLock(validationMutex_, std::chrono::milliseconds(200), "validation_state");
        
        // Perform validation
        return true;
    }
};
```

## Performance Considerations

### Lock Granularity

```cpp
// ❌ Too coarse - locks entire operation
class BadExample {
private:
    StateMutex bigMutex_;
    std::vector<Data> items_;

public:
    void processAllItems() {
        ScopedTimedLock lock(bigMutex_, std::chrono::milliseconds(10000), "process_all");
        for (auto& item : items_) {
            // Long processing while holding lock
            processItem(item);
        }
    }
};

// ✅ Better - fine-grained locking
class GoodExample {
private:
    StateMutex itemsMutex_;
    std::vector<Data> items_;

public:
    void processAllItems() {
        std::vector<Data> localItems;
        
        // Copy items under lock
        {
            ScopedTimedLock lock(itemsMutex_, std::chrono::milliseconds(100), "copy_items");
            localItems = items_;
        }
        
        // Process without holding lock
        for (auto& item : localItems) {
            processItem(item);
        }
    }
};
```

### Lock Contention Reduction

```cpp
class OptimizedCache {
private:
    // Use multiple locks to reduce contention
    static constexpr size_t NUM_SHARDS = 16;
    std::array<StateMutex, NUM_SHARDS> shardMutexes_;
    std::array<std::unordered_map<std::string, std::string>, NUM_SHARDS> shards_;

    size_t getShardIndex(const std::string& key) {
        return std::hash<std::string>{}(key) % NUM_SHARDS;
    }

public:
    void setValue(const std::string& key, const std::string& value) {
        size_t shard = getShardIndex(key);
        ScopedTimedLock lock(shardMutexes_[shard], std::chrono::milliseconds(500), 
                           "cache_shard_" + std::to_string(shard));
        shards_[shard][key] = value;
    }
};
```

## Monitoring and Debugging

### Enable Lock Monitoring

```cpp
void enableLockDebugging() {
    // Enable detailed logging
    LockMonitor::getInstance().enableDetailedLogging(true);
    
    // Enable deadlock detection
    DeadlockDetector::getInstance().enableDeadlockDetection(true);
}

void checkLockStatistics() {
    auto stats = LockMonitor::getInstance().getAllStats();
    
    for (const auto& [lockName, lockStats] : stats) {
        std::cout << "Lock: " << lockName << std::endl;
        std::cout << "  Acquisitions: " << lockStats.acquisitions.load() << std::endl;
        std::cout << "  Failures: " << lockStats.failures.load() << std::endl;
        std::cout << "  Avg wait time: " << lockStats.getAverageWaitTime() << "μs" << std::endl;
        std::cout << "  Failure rate: " << (lockStats.getFailureRate() * 100) << "%" << std::endl;
        
        // Alert on high contention
        if (lockStats.getFailureRate() > 0.1) { // > 10% failure rate
            std::cout << "  ⚠️  HIGH CONTENTION DETECTED!" << std::endl;
        }
    }
}
```

### Deadlock Detection

```cpp
void checkForDeadlocks() {
    try {
        DeadlockDetector::getInstance().checkForDeadlocks();
    } catch (const DeadlockException& e) {
        std::cerr << "Deadlock detected: " << e.what() << std::endl;
        
        // Get detailed thread lock information
        auto threadInfo = DeadlockDetector::getInstance().getThreadLockInfo();
        for (const auto& [threadId, info] : threadInfo) {
            std::cerr << "Thread " << threadId << ":" << std::endl;
            
            for (const auto& [level, mutexId] : info.exclusiveLocks) {
                std::cerr << "  Exclusive lock: Level " << static_cast<int>(level) 
                         << " (" << mutexId << ")" << std::endl;
            }
            
            for (const auto& [level, mutexId] : info.sharedLocks) {
                std::cerr << "  Shared lock: Level " << static_cast<int>(level) 
                         << " (" << mutexId << ")" << std::endl;
            }
        }
    }
}
```

## Common Pitfalls and Solutions

### 1. Lock Ordering Violations

**Problem**: Acquiring locks in different orders across different code paths.

**Solution**: Always follow the hierarchy: CONFIG → CONTAINER → RESOURCE → STATE

### 2. Holding Locks Too Long

**Problem**: Performing I/O or expensive operations while holding locks.

**Solution**: Minimize critical sections, copy data out of locked regions.

### 3. Recursive Locking

**Problem**: Same thread trying to acquire the same lock multiple times.

**Solution**: Use `std::recursive_mutex` or redesign to avoid recursion.

### 4. Exception Safety

**Problem**: Exceptions causing locks to remain held.

**Solution**: RAII locks automatically release on exceptions.

### 5. Lock Contention

**Problem**: Multiple threads frequently competing for the same lock.

**Solution**: Use lock sharding, reader-writer locks, or lock-free data structures.

## Best Practices Summary

1. **Always use RAII locks** - Never use raw `lock()`/`unlock()` calls
2. **Follow lock hierarchy** - Acquire locks in ascending order of levels
3. **Use appropriate timeouts** - Don't wait indefinitely for locks
4. **Minimize critical sections** - Hold locks for the shortest time possible
5. **Use shared locks for readers** - Allow concurrent read access when possible
6. **Monitor lock performance** - Watch for high contention and failures
7. **Name your locks** - Use descriptive names for debugging
8. **Handle timeouts gracefully** - Have fallback strategies for lock failures
9. **Test under load** - Verify lock behavior with concurrent access
10. **Document lock dependencies** - Make lock relationships clear in code

## Migration from Legacy Locking

When migrating existing code to use the new lock utilities:

1. **Identify existing mutexes** and classify them by level
2. **Replace raw mutexes** with ordered mutexes of appropriate level
3. **Replace lock_guard/unique_lock** with ScopedTimedLock
4. **Add timeout handling** for lock acquisition failures
5. **Add monitoring** to identify performance issues
6. **Test thoroughly** with concurrent workloads

This systematic approach ensures thread safety while providing better debugging and monitoring capabilities.