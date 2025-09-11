#ifndef DATABASE_CONNECTION_POOL_HPP
#define DATABASE_CONNECTION_POOL_HPP

#include <pqxx/pqxx>
#include <memory>
#include <queue>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include "logger.hpp"

struct DatabaseConnectionConfig {
    std::string host = "localhost";
    int port = 5432;
    std::string database = "etl_db";
    std::string username = "etl_user";
    std::vector<char> password; // Secure password storage
    int maxConnections = 10;
    int minConnections = 2;
    std::chrono::seconds connectionTimeout = std::chrono::seconds(30);
    std::chrono::seconds healthCheckInterval = std::chrono::seconds(60);
    bool enableHealthChecks = true;
    int maxRetries = 3;
    std::chrono::milliseconds retryDelay = std::chrono::milliseconds(1000);

    // Helper methods for password management
    void setPassword(const std::string& pwd) {
        password.assign(pwd.begin(), pwd.end());
    }

    std::string getPassword() const {
        return std::string(password.begin(), password.end());
    }

    void clearPassword() {
        std::fill(password.begin(), password.end(), 0);
        password.clear();
    }
};

class DatabaseConnectionPool {
public:
    explicit DatabaseConnectionPool(const DatabaseConnectionConfig& config);
    ~DatabaseConnectionPool();

    // Delete copy and move operations
    DatabaseConnectionPool(const DatabaseConnectionPool&) = delete;
    DatabaseConnectionPool& operator=(const DatabaseConnectionPool&) = delete;
    DatabaseConnectionPool(DatabaseConnectionPool&&) = delete;
    DatabaseConnectionPool& operator=(DatabaseConnectionPool&&) = delete;

    // Connection management
    std::shared_ptr<pqxx::connection> acquireConnection();
    void releaseConnection(std::shared_ptr<pqxx::connection> conn);
    void closeAll();
    bool gracefulShutdown(std::chrono::milliseconds timeout);

    // Health monitoring
    void startHealthMonitoring();
    void stopHealthMonitoring();
    bool isHealthy() const;

    // Metrics
    struct PoolMetrics {
        size_t activeConnections = 0;
        size_t idleConnections = 0;
        size_t totalConnections = 0;
        size_t connectionsCreated = 0;
        size_t connectionsDestroyed = 0;
        size_t connectionTimeouts = 0;
        size_t healthCheckFailures = 0;
        double averageWaitTimeMs = 0.0;
        std::chrono::steady_clock::time_point lastHealthCheck;
    };

    PoolMetrics getMetrics() const;

    // Configuration
    void updateConfig(const DatabaseConnectionConfig& newConfig);

private:
    struct PooledConnection {
        std::shared_ptr<pqxx::connection> connection;
        std::chrono::steady_clock::time_point createdTime;
        std::chrono::steady_clock::time_point lastUsedTime;
        bool isHealthy = true;

        PooledConnection(std::shared_ptr<pqxx::connection> conn)
            : connection(std::move(conn)),
              createdTime(std::chrono::steady_clock::now()),
              lastUsedTime(std::chrono::steady_clock::now()) {
            if (!connection) {
                throw std::invalid_argument("PooledConnection requires a non-null connection");
            }
        }
    };

    DatabaseConnectionConfig config_;
    std::queue<std::shared_ptr<PooledConnection>> idleConnections_;
    std::vector<std::shared_ptr<PooledConnection>> activeConnections_;
    mutable std::mutex poolMutex_;
    std::condition_variable poolCondition_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_{false};
    std::thread healthCheckThread_;

    // Metrics
    mutable std::mutex metricsMutex_;
    PoolMetrics metrics_;
    static constexpr size_t MAX_WAIT_TIMES = 100;
    std::deque<double> waitTimes_;

    // Private methods
    std::shared_ptr<pqxx::connection> createConnection();
    bool validateConnection(std::shared_ptr<pqxx::connection> conn);
    void performHealthCheck();
    void cleanupExpiredConnections();
    void adjustPoolSize();
    std::string buildConnectionString() const;
};

#endif // DATABASE_CONNECTION_POOL_HPP
