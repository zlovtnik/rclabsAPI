#include "../include/lock_utils.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

using namespace etl_plus;

// Simulate a simple bank account with thread-safe operations
class BankAccount {
private:
    StateMutex balanceMutex_;
    double balance_;
    std::string accountId_;

public:
    BankAccount(const std::string& id, double initialBalance) 
        : balance_(initialBalance), accountId_(id) {}
    
    bool withdraw(double amount) {
        ScopedTimedLock lock(balanceMutex_, std::chrono::milliseconds(1000), 
                           "withdraw_" + accountId_);
        
        if (balance_ >= amount) {
            // Simulate processing time
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            balance_ -= amount;
            std::cout << "Withdrew $" << amount << " from " << accountId_ 
                     << ". New balance: $" << balance_ << std::endl;
            return true;
        }
        
        std::cout << "Insufficient funds in " << accountId_ 
                 << ". Balance: $" << balance_ << ", Requested: $" << amount << std::endl;
        return false;
    }
    
    void deposit(double amount) {
        ScopedTimedLock lock(balanceMutex_, std::chrono::milliseconds(1000), 
                           "deposit_" + accountId_);
        
        // Simulate processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        balance_ += amount;
        std::cout << "Deposited $" << amount << " to " << accountId_ 
                 << ". New balance: $" << balance_ << std::endl;
    }
    
    double getBalance() const {
        ScopedTimedLock lock(const_cast<StateMutex&>(balanceMutex_), 
                           std::chrono::milliseconds(500), 
                           "balance_check_" + accountId_);
        return balance_;
    }
};

// Simulate a connection pool with proper lock ordering
class ConnectionPool {
private:
    ConfigSharedMutex configMutex_;        // Level 1 - Configuration
    ContainerMutex poolMutex_;             // Level 2 - Pool container
    ResourceMutex connectionMutex_;        // Level 3 - Individual connections
    
    struct Config {
        int maxConnections = 10;
        int timeoutMs = 5000;
    } config_;
    
    std::vector<std::string> availableConnections_;
    std::atomic<int> activeConnections_{0};

public:
    ConnectionPool() {
        // Initialize with some connections
        for (int i = 0; i < 5; ++i) {
            availableConnections_.push_back("conn_" + std::to_string(i));
        }
    }
    
    void updateConfig(int maxConn, int timeout) {
        // Exclusive access to config (writer lock)
        ScopedTimedLock configLock(configMutex_, std::chrono::milliseconds(1000), "config_update");
        
        config_.maxConnections = maxConn;
        config_.timeoutMs = timeout;
        
        std::cout << "Updated config: max=" << maxConn << ", timeout=" << timeout << "ms" << std::endl;
    }
    
    std::string acquireConnection() {
        // Read config (shared lock - multiple readers allowed)
        ScopedTimedSharedLock configLock(configMutex_, std::chrono::milliseconds(500), "config_read");
        
        // Access pool container (exclusive lock)
        ScopedTimedLock poolLock(poolMutex_, std::chrono::milliseconds(1000), "pool_access");
        
        if (availableConnections_.empty()) {
            std::cout << "No connections available" << std::endl;
            return "";
        }
        
        std::string conn = availableConnections_.back();
        availableConnections_.pop_back();
        activeConnections_.fetch_add(1);
        
        std::cout << "Acquired connection: " << conn 
                 << " (Active: " << activeConnections_.load() << ")" << std::endl;
        
        return conn;
    }
    
    void releaseConnection(const std::string& conn) {
        ScopedTimedLock poolLock(poolMutex_, std::chrono::milliseconds(1000), "pool_release");
        
        availableConnections_.push_back(conn);
        activeConnections_.fetch_sub(1);
        
        std::cout << "Released connection: " << conn 
                 << " (Active: " << activeConnections_.load() << ")" << std::endl;
    }
    
    int getActiveCount() const {
        return activeConnections_.load();
    }
};

// Demonstrate lock timeout handling
void demonstrateLockTimeout() {
    std::cout << "\n=== Lock Timeout Demonstration ===" << std::endl;
    
    std::timed_mutex slowMutex;
    
    // Thread that holds lock for a long time
    std::thread slowThread([&slowMutex]() {
        ScopedTimedLock lock(slowMutex, std::chrono::milliseconds(5000), "slow_operation");
        std::cout << "Slow thread acquired lock, sleeping for 2 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "Slow thread releasing lock" << std::endl;
    });
    
    // Give slow thread time to acquire lock
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Thread that tries to acquire with short timeout
    std::thread fastThread([&slowMutex]() {
        try {
            ScopedTimedLock lock(slowMutex, std::chrono::milliseconds(500), "fast_operation");
            std::cout << "Fast thread acquired lock (this shouldn't happen)" << std::endl;
        } catch (const LockTimeoutException& e) {
            std::cout << "Fast thread timed out as expected: " << e.what() << std::endl;
        }
    });
    
    slowThread.join();
    fastThread.join();
}

// Demonstrate deadlock prevention
void demonstrateDeadlockPrevention() {
    std::cout << "\n=== Deadlock Prevention Demonstration ===" << std::endl;
    
    ConfigMutex configMutex;
    ContainerMutex containerMutex;
    
    std::cout << "Attempting correct lock ordering (should succeed)..." << std::endl;
    {
        ScopedTimedLock configLock(configMutex, std::chrono::milliseconds(1000), "config_first");
        ScopedTimedLock containerLock(containerMutex, std::chrono::milliseconds(1000), "container_second");
        std::cout << "✓ Correct ordering succeeded" << std::endl;
    }
    
    std::cout << "Attempting incorrect lock ordering (should throw)..." << std::endl;
    try {
        ScopedTimedLock containerLock(containerMutex, std::chrono::milliseconds(1000), "container_first");
        ScopedTimedLock configLock(configMutex, std::chrono::milliseconds(1000), "config_second");
        std::cout << "❌ This should not be reached!" << std::endl;
    } catch (const DeadlockException& e) {
        std::cout << "✓ Deadlock prevention worked: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "Lock Utils Demo" << std::endl;
    std::cout << "===============" << std::endl;
    
    // Enable monitoring for demonstration
    LockMonitor::getInstance().enableDetailedLogging(true);
    DeadlockDetector::getInstance().enableDeadlockDetection(true);
    
    // Demo 1: Bank account with concurrent transactions
    std::cout << "\n=== Bank Account Demo ===" << std::endl;
    BankAccount account("ACC001", 1000.0);
    
    std::vector<std::thread> transactions;
    
    // Create multiple concurrent transactions
    for (int i = 0; i < 3; ++i) {
        transactions.emplace_back([&account, i]() {
            account.deposit(100.0 + i * 10);
            account.withdraw(50.0 + i * 5);
        });
    }
    
    for (auto& t : transactions) {
        t.join();
    }
    
    std::cout << "Final balance: $" << account.getBalance() << std::endl;
    
    // Demo 2: Connection pool with proper lock ordering
    std::cout << "\n=== Connection Pool Demo ===" << std::endl;
    ConnectionPool pool;
    
    std::vector<std::thread> workers;
    
    // Worker threads that acquire and release connections
    for (int i = 0; i < 3; ++i) {
        workers.emplace_back([&pool, i]() {
            std::string conn = pool.acquireConnection();
            if (!conn.empty()) {
                // Simulate work with connection
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                pool.releaseConnection(conn);
            }
        });
    }
    
    // Configuration update thread
    workers.emplace_back([&pool]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        pool.updateConfig(15, 3000);
    });
    
    for (auto& w : workers) {
        w.join();
    }
    
    // Demo 3: Lock timeout handling
    demonstrateLockTimeout();
    
    // Demo 4: Deadlock prevention
    demonstrateDeadlockPrevention();
    
    // Show final statistics
    std::cout << "\n=== Lock Statistics ===" << std::endl;
    auto stats = LockMonitor::getInstance().getAllStats();
    
    for (const auto& [lockName, lockStats] : stats) {
        std::cout << "Lock '" << lockName << "':" << std::endl;
        std::cout << "  Acquisitions: " << lockStats.acquisitions.load() << std::endl;
        std::cout << "  Failures: " << lockStats.failures.load() << std::endl;
        std::cout << "  Avg wait time: " << lockStats.getAverageWaitTime() << "μs" << std::endl;
        std::cout << "  Max wait time: " << lockStats.maxWaitTime.load() << "μs" << std::endl;
        std::cout << "  Contentions: " << lockStats.contentions.load() << std::endl;
        
        if (lockStats.getFailureRate() > 0) {
            std::cout << "  Failure rate: " << (lockStats.getFailureRate() * 100) << "%" << std::endl;
        }
    }
    
    std::cout << "\n🎉 Demo completed successfully!" << std::endl;
    return 0;
}