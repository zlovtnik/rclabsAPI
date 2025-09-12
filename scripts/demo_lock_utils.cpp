#include "../include/lock_utils.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// Explicit using declarations for needed symbols
using etl_plus::ConfigMutex;
using etl_plus::ConfigSharedMutex;
using etl_plus::ContainerMutex;
using etl_plus::DeadlockDetector;
using etl_plus::LockMonitor;
using etl_plus::ResourceMutex;
using etl_plus::ScopedTimedLock;
using etl_plus::ScopedTimedSharedLock;
using etl_plus::StateMutex;

// Simulate a simple bank account with thread-safe operations
class BankAccount {
private:
  mutable StateMutex balanceMutex_;
  double balance_;
  std::string accountId_;

public:
  /**
   * @brief Construct a BankAccount with the given identifier and starting
   * balance.
   *
   * @param id Account identifier (string).
   * @param initialBalance Starting balance for the account.
   */
  BankAccount(const std::string &id, double initialBalance)
      : balance_(initialBalance), accountId_(id) {}

  /**
   * @brief Attempt to withdraw an amount from the account.
   *
   * Acquires the account's balance mutex with a 1-second timed lock, simulates
   * a short processing delay (1 ms), and if the current balance is sufficient
   * deducts the amount and returns true. If the balance is insufficient the
   * balance is unchanged and the function returns false. The function prints
   * a message describing the outcome.
   *
   * @param amount Withdrawal amount in the account currency.
   * @return true if the withdrawal succeeded and the balance was decreased.
   * @return false if the withdrawal failed due to insufficient funds.
   */
  bool withdraw(double amount) {
    ScopedTimedLock lock(balanceMutex_, std::chrono::milliseconds(1000),
                         "withdraw_" + accountId_);

    if (balance_ >= amount) {
      // Simulate processing time
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      balance_ -= amount;
      std::cout << "Withdrew $" << amount << " from " << accountId_
                << ". New balance: $" << balance_ << '\n';
      return true;
    }

    std::cout << "Insufficient funds in " << accountId_ << ". Balance: $"
              << balance_ << ", Requested: $" << amount << '\n';
    return false;
  }

  /**
   * @brief Atomically adds funds to the account balance.
   *
   * Acquires the account's timed mutex (up to 1 second) to protect the balance,
   * simulates a short processing delay, increments the stored balance by the
   * given amount, and prints the updated balance to stdout.
   *
   * @param amount Amount to deposit; added to the account's balance.
   */
  void deposit(double amount) {
    ScopedTimedLock lock(balanceMutex_, std::chrono::milliseconds(1000),
                         "deposit_" + accountId_);

    // Simulate processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    balance_ += amount;
    std::cout << "Deposited $" << amount << " to " << accountId_
              << ". New balance: $" << balance_ << '\n';
  }

  /**
   * @brief Returns the current account balance.
   *
   * Attempts to acquire the account's balance mutex with a 500 ms timed lock
   * and then returns the protected balance value. This const accessor uses a
   * `const_cast` to obtain the non-const mutex so it may block (up to the
   * timeout) while synchronizing access with other threads.
   *
   * @return double Current balance.
   */
  double getBalance() const {
    ScopedTimedLock lock(balanceMutex_, std::chrono::milliseconds(500),
                         "balance_check_" + accountId_);
    return balance_;
  }
};

// Simulate a connection pool with proper lock ordering
class ConnectionPool {
private:
  ConfigSharedMutex configMutex_; // Level 1 - Configuration
  ContainerMutex poolMutex_;      // Level 2 - Pool container
  ResourceMutex connectionMutex_; // Level 3 - Individual connections

  struct Config {
    int maxConnections = 10;
    int timeoutMs = 5000;
  } config_;

  std::vector<std::string> availableConnections_;
  std::atomic<int> activeConnections_{0};

public:
  /**
   * @brief Constructs a ConnectionPool and populates the pool with initial
   * connections.
   *
   * Initializes the availableConnections_ container with five connection
   * identifiers
   * ("conn_0" through "conn_4"). Other members (config_ defaults and
   * activeConnections_) remain at their default-initialized values.
   */
  ConnectionPool() {
    // Initialize with some connections
    for (int i = 0; i < 5; ++i) {
      availableConnections_.push_back("conn_" + std::to_string(i));
    }
  }

  /**
   * @brief Atomically update the connection pool configuration.
   *
   * Acquires an exclusive timed lock on the configuration mutex (1 second)
   * and updates the stored maxConnections and timeoutMs values. The new
   * configuration is also written to standard output.
   *
   * @param maxConn Maximum number of connections.
   * @param timeout Connection timeout in milliseconds.
   */
  void updateConfig(int maxConn, int timeout) {
    // Exclusive access to config (writer lock)
    ScopedTimedLock configLock(configMutex_, std::chrono::milliseconds(1000),
                               "config_update");

    config_.maxConnections = maxConn;
    config_.timeoutMs = timeout;

    std::cout << "Updated config: max=" << maxConn << ", timeout=" << timeout
              << "ms" << std::endl;
  }

  /**
   * @brief Acquire a connection identifier from the pool.
   *
   * Attempts to read the current configuration (shared timed read) and then
   * obtains exclusive access to the pool container to remove and return one
   * available connection identifier.
   *
   * If a connection is successfully acquired, it is removed from the pool and
   * the internal active connection counter is incremented; the connection id is
   * returned. If no connections are available, an empty string is returned.
   *
   * The method uses timed locks when accessing configuration (500 ms) and the
   * pool container (1000 ms).
   *
   * @return std::string The acquired connection identifier, or an empty string
   * if none are available.
   */
  std::string acquireConnection() {
    // Read config under a bounded scope; release before taking the pool lock.
    {
      ScopedTimedSharedLock configLock(
          configMutex_, std::chrono::milliseconds(500), "config_read");
      // Copy any needed fields from config_ here.
      // auto max = config_.maxConnections; auto to = config_.timeoutMs;
    }

    // Access pool container (exclusive lock)
    ScopedTimedLock poolLock(poolMutex_, std::chrono::milliseconds(1000),
                             "pool_access");

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

  /**
   * @brief Releases a previously acquired connection back into the pool.
   *
   * Acquires an exclusive timed lock on the pool container (up to 1000 ms) and
   * returns the connection identifier to the list of available connections.
   * The function also decrements the pool's active-connection counter.
   *
   * @param conn Connection identifier to release back into the pool.
   */
  void releaseConnection(const std::string &conn) {
    ScopedTimedLock poolLock(poolMutex_, std::chrono::milliseconds(1000),
                             "pool_release");

    availableConnections_.push_back(conn);
    activeConnections_.fetch_sub(1);

    std::cout << "Released connection: " << conn
              << " (Active: " << activeConnections_.load() << ")" << std::endl;
  }

  /**
   * @brief Returns the current number of active (in-use) connections.
   *
   * This is a snapshot read of an internal atomic counter and is safe to call
   * concurrently. The returned value may immediately become stale as
   * connections are acquired or released by other threads.
   *
   * @return int Current number of active connections.
   */
  int getActiveCount() const { return activeConnections_.load(); }
};

/**
 * @brief Demonstrates timed-lock behavior and timeout handling.
 *
 * Runs two threads contending on a std::timed_mutex: a "slow" thread that
 * holds the lock for ~2 seconds using ScopedTimedLock and a "fast" thread
 * that attempts to acquire the same lock with a short timeout. Shows the
 * expected LockTimeoutException when the fast thread times out and prints
 * progress and results to stdout.
 *
 * Side effects:
 * - Spawns two threads and writes diagnostic messages to stdout.
 * - Waits for both threads to join before returning.
 */
void demonstrateLockTimeout() {
  std::cout << "\n=== Lock Timeout Demonstration ===" << std::endl;

  std::timed_mutex slowMutex;

  // Thread that holds lock for a long time
  std::thread slowThread([&slowMutex]() {
    ScopedTimedLock lock(slowMutex, std::chrono::milliseconds(5000),
                         "slow_operation");
    std::cout << "Slow thread acquired lock, sleeping for 2 seconds..."
              << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Slow thread releasing lock" << std::endl;
  });

  // Give slow thread time to acquire lock
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Thread that tries to acquire with short timeout
  std::thread fastThread([&slowMutex]() {
    try {
      ScopedTimedLock lock(slowMutex, std::chrono::milliseconds(500),
                           "fast_operation");
      std::cout << "Fast thread acquired lock (this shouldn't happen)"
                << std::endl;
    } catch (const LockTimeoutException &e) {
      std::cout << "Fast thread timed out as expected: " << e.what()
                << std::endl;
    }
  });

  slowThread.join();
  fastThread.join();
}

/**
 * @brief Demonstrates deadlock prevention via consistent lock ordering.
 *
 * Attempts two locking sequences on a pair of mutex types (configuration-level
 * and container-level) to show the effect of a global lock ordering rule:
 * the correct ordering (config then container) acquires both locks
 * successfully, while the reversed ordering triggers the deadlock detector and
 * throws DeadlockException.
 *
 * The function prints status messages and catches DeadlockException for the
 * failing scenario to confirm the prevention behavior.
 */
void demonstrateDeadlockPrevention() {
  std::cout << "\n=== Deadlock Prevention Demonstration ===" << std::endl;

  ConfigMutex configMutex;
  ContainerMutex containerMutex;

  std::cout << "Attempting correct lock ordering (should succeed)..."
            << std::endl;
  {
    ScopedTimedLock configLock(configMutex, std::chrono::milliseconds(1000),
                               "config_first");
    ScopedTimedLock containerLock(
        containerMutex, std::chrono::milliseconds(1000), "container_second");
    std::cout << "✓ Correct ordering succeeded" << std::endl;
  }

  std::cout << "Attempting incorrect lock ordering (should throw)..."
            << std::endl;
  try {
    ScopedTimedLock containerLock(
        containerMutex, std::chrono::milliseconds(1000), "container_first");
    ScopedTimedLock configLock(configMutex, std::chrono::milliseconds(1000),
                               "config_second");
    std::cout << "❌ This should not be reached!" << std::endl;
  } catch (const DeadlockException &e) {
    std::cout << "✓ Deadlock prevention worked: " << e.what() << std::endl;
  }
}

/**
 * @brief Entry point for the Lock Utils demonstration program.
 *
 * Runs four demonstrations that exercise the locking utilities and
 * instrumentation: 1) Concurrent BankAccount deposits/withdrawals. 2)
 * ConnectionPool usage with proper lock ordering and a config update. 3) Lock
 * timeout behavior. 4) Deadlock-prevention demonstration.
 *
 * Also enables LockMonitor and DeadlockDetector, prints runtime statistics
 * gathered by LockMonitor, and outputs progress/results to stdout.
 *
 * @return int Program exit status (returns 0 on successful completion).
 */
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
      try {
        account.deposit(100.0 + i * 10);
        account.withdraw(50.0 + i * 5);
      } catch (const LockTimeoutException &e) {
        std::cerr << "Thread " << i
                  << " failed due to lock timeout: " << e.what() << std::endl;
        // Continue with other operations or abort gracefully
      } catch (const std::exception &e) {
        std::cerr << "Thread " << i
                  << " failed with unexpected error: " << e.what() << std::endl;
      }
    });
  }

  for (auto &t : transactions) {
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
      try {
        std::string conn = pool.acquireConnection();
        if (!conn.empty()) {
          // Simulate work with connection
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          pool.releaseConnection(conn);
        }
      } catch (const LockTimeoutException &e) {
        std::cerr << "Worker thread " << i
                  << " failed due to lock timeout: " << e.what() << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "Worker thread " << i
                  << " failed with unexpected error: " << e.what() << std::endl;
      }
    });
  }

  // Configuration update thread
  workers.emplace_back([&pool]() {
    try {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      pool.updateConfig(15, 3000);
    } catch (const LockTimeoutException &e) {
      std::cerr << "Config update thread failed due to lock timeout: "
                << e.what() << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "Config update thread failed with unexpected error: "
                << e.what() << std::endl;
    }
  });

  for (auto &w : workers) {
    w.join();
  }

  // Demo 3: Lock timeout handling
  demonstrateLockTimeout();

  // Demo 4: Deadlock prevention
  demonstrateDeadlockPrevention();

  // Show final statistics
  std::cout << "\n=== Lock Statistics ===" << std::endl;
  auto stats = LockMonitor::getInstance().getAllStats();

  for (const auto &[lockName, lockStats] : stats) {
    std::cout << "Lock '" << lockName << "':" << std::endl;
    std::cout << "  Acquisitions: " << lockStats.acquisitions.load()
              << std::endl;
    std::cout << "  Failures: " << lockStats.failures.load() << std::endl;
    std::cout << "  Avg wait time: " << lockStats.getAverageWaitTime() << "μs"
              << std::endl;
    std::cout << "  Max wait time: " << lockStats.maxWaitTime.load() << "μs"
              << std::endl;
    std::cout << "  Contentions: " << lockStats.contentions.load() << std::endl;

    if (lockStats.getFailureRate() > 0) {
      std::cout << "  Failure rate: " << (lockStats.getFailureRate() * 100)
                << "%" << std::endl;
    }
  }

  std::cout << "\n🎉 Demo completed successfully!" << std::endl;
  return 0;
}