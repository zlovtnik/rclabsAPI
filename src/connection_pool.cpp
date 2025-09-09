#include "connection_pool.hpp"
#include "logger.hpp"
#include "etl_exceptions.hpp"
#include <algorithm>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <nlohmann/json.hpp>

namespace net = boost::asio;

ConnectionPool::ConnectionPool() : ConnectionPool(ConnectionPoolConfig{}) {}

ConnectionPool::ConnectionPool(const ConnectionPoolConfig& config) : config_(config) {
    WS_LOG_DEBUG("Connection pool created with max connections: " + std::to_string(config_.maxConnections));
}

ConnectionPool::~ConnectionPool() {
    stop();
    WS_LOG_DEBUG("Connection pool destroyed");
}

void ConnectionPool::start() {
    if (running_.load()) {
        WS_LOG_WARN("Connection pool already running");
        return;
    }

    running_.store(true);

    if (config_.enableHealthMonitoring) {
        startHealthMonitoring();
    }

    WS_LOG_INFO("Connection pool started");
}

void ConnectionPool::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    // Stop health monitoring
    stopHealthMonitoring();

    // Close all connections
    SCOPED_LOCK_TIMEOUT(connectionsMutex_, 2000);
    for (auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            connection->close();
        }
    }
    connections_.clear();

    WS_LOG_INFO("Connection pool stopped");
}

void ConnectionPool::addConnection(std::shared_ptr<WebSocketConnection> connection) {
    if (!connection) {
        WS_LOG_ERROR("Attempted to add null WebSocket connection");
        throw etl::ETLException(etl::ErrorCode::INVALID_CONNECTION, "Cannot add null connection to pool");
    }

    if (!running_.load()) {
        WS_LOG_WARN("Connection pool not running, cannot add connection");
        throw etl::ETLException(etl::ErrorCode::POOL_NOT_RUNNING, "Connection pool is not running");
    }

    SCOPED_LOCK_TIMEOUT(connectionsMutex_, 1000);

    // Check connection limit
    if (connections_.size() >= config_.maxConnections) {
        WS_LOG_WARN("Connection pool at maximum capacity: " + std::to_string(config_.maxConnections));
        throw etl::ETLException(etl::ErrorCode::POOL_CAPACITY_EXCEEDED, "Connection pool at maximum capacity");
    }

    const std::string& connectionId = connection->getId();
    connections_[connectionId] = connection;

    WS_LOG_INFO("WebSocket connection added to pool: " + connectionId +
                " (Total connections: " + std::to_string(connections_.size()) + ")");
}

void ConnectionPool::removeConnection(const std::string& connectionId) {
    SCOPED_LOCK_TIMEOUT(connectionsMutex_, 1000);
    removeConnectionInternal(connectionId);
}

void ConnectionPool::removeConnectionInternal(const std::string& connectionId) {
    auto it = connections_.find(connectionId);
    if (it != connections_.end()) {
        connections_.erase(it);
        WS_LOG_INFO("WebSocket connection removed from pool: " + connectionId +
                    " (Total connections: " + std::to_string(connections_.size()) + ")");
    }
}

std::shared_ptr<WebSocketConnection> ConnectionPool::getConnection(const std::string& connectionId) const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    auto it = connections_.find(connectionId);
    return (it != connections_.end()) ? it->second : nullptr;
}

bool ConnectionPool::hasConnection(const std::string& connectionId) const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    return connections_.find(connectionId) != connections_.end();
}

std::vector<std::shared_ptr<WebSocketConnection>> ConnectionPool::getActiveConnections() const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    std::vector<std::shared_ptr<WebSocketConnection>> activeConnections;

    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            activeConnections.push_back(connection);
        }
    }

    return activeConnections;
}

std::vector<std::string> ConnectionPool::getConnectionIds() const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    std::vector<std::string> ids;
    ids.reserve(connections_.size());

    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            ids.push_back(id);
        }
    }

    return ids;
}

void ConnectionPool::removeInactiveConnections() {
    SCOPED_LOCK_TIMEOUT(connectionsMutex_, 1000);

    size_t removedCount = 0;
    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& connection = it->second;
        if (!connection || !connection->isOpen()) {
            WS_LOG_DEBUG("Removing inactive connection from pool: " + it->first);
            it = connections_.erase(it);
            removedCount++;
        } else {
            ++it;
        }
    }

    if (removedCount > 0) {
        WS_LOG_INFO("Removed " + std::to_string(removedCount) + " inactive connections from pool");
    }
}

void ConnectionPool::performHealthCheck() {
    if (!running_.load()) {
        return;
    }

    SCOPED_LOCK_TIMEOUT(connectionsMutex_, 1000);

    size_t healthyCount = 0;
    size_t unhealthyCount = 0;

    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& connection = it->second;
        if (connection) {
            if (connection->isOpen() && connection->isHealthy()) {
                healthyCount++;
                ++it;
            } else {
                WS_LOG_DEBUG("Removing unhealthy connection: " + it->first);
                it = connections_.erase(it);
                unhealthyCount++;
            }
        } else {
            WS_LOG_DEBUG("Removing null connection reference");
            it = connections_.erase(it);
            unhealthyCount++;
        }
    }

    WS_LOG_DEBUG("Health check completed - Healthy: " + std::to_string(healthyCount) +
                 ", Unhealthy: " + std::to_string(unhealthyCount));
}

void ConnectionPool::startHealthMonitoring() {
    if (!config_.enableHealthMonitoring || healthMonitoringActive_.load()) {
        return;
    }

    healthMonitoringActive_.store(true);
    scheduleHealthCheck();

    if (config_.enableConnectionCleanup) {
        scheduleCleanup();
    }

    WS_LOG_INFO("Connection pool health monitoring started");
}

void ConnectionPool::stopHealthMonitoring() {
    healthMonitoringActive_.store(false);

    if (healthCheckTimer_) {
        healthCheckTimer_->cancel();
    }
    if (cleanupTimer_) {
        cleanupTimer_->cancel();
    }

    WS_LOG_INFO("Connection pool health monitoring stopped");
}

void ConnectionPool::scheduleHealthCheck() {
    if (!healthMonitoringActive_.load()) {
        return;
    }

    // Create timer if it doesn't exist
    if (!healthCheckTimer_) {
        // We need an io_context - this is a limitation, but for now we'll skip automatic scheduling
        // In a real implementation, this would be passed in or managed externally
        WS_LOG_DEBUG("Health check timer not available - manual health checks only");
        return;
    }

    healthCheckTimer_->expires_after(config_.healthCheckInterval);
    healthCheckTimer_->async_wait([self = shared_from_this()](const boost::system::error_code& ec) {
        if (!ec && self->healthMonitoringActive_.load()) {
            self->performHealthCheck();
            self->scheduleHealthCheck(); // Schedule next check
        }
    });
}

void ConnectionPool::scheduleCleanup() {
    if (!healthMonitoringActive_.load() || !config_.enableConnectionCleanup) {
        return;
    }

    // Similar to health check - would need io_context for automatic scheduling
    WS_LOG_DEBUG("Cleanup timer not available - manual cleanup only");
}

bool ConnectionPool::isConnectionHealthy(const std::string& connectionId) const {
    auto connection = getConnection(connectionId);
    return connection && connection->isOpen() && connection->isHealthy();
}

std::vector<std::string> ConnectionPool::getUnhealthyConnections() const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    std::vector<std::string> unhealthyIds;

    for (const auto& [id, connection] : connections_) {
        if (!connection || !connection->isOpen() || !connection->isHealthy()) {
            unhealthyIds.push_back(id);
        }
    }

    return unhealthyIds;
}

std::vector<std::shared_ptr<WebSocketConnection>> ConnectionPool::getConnectionsByFilter(
    std::function<bool(const std::shared_ptr<WebSocketConnection>&)> filter) const {

    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    std::vector<std::shared_ptr<WebSocketConnection>> matchingConnections;

    for (const auto& [id, connection] : connections_) {
        if (connection && filter(connection)) {
            matchingConnections.push_back(connection);
        }
    }

    return matchingConnections;
}

std::vector<std::string> ConnectionPool::getConnectionIdsByFilter(
    std::function<bool(const std::shared_ptr<WebSocketConnection>&)> filter) const {

    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    std::vector<std::string> matchingIds;

    for (const auto& [id, connection] : connections_) {
        if (connection && filter(connection)) {
            matchingIds.push_back(id);
        }
    }

    return matchingIds;
}

size_t ConnectionPool::getTotalConnectionCount() const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    return connections_.size();
}

size_t ConnectionPool::getActiveConnectionCount() const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    size_t activeCount = 0;

    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            activeCount++;
        }
    }

    return activeCount;
}

size_t ConnectionPool::getInactiveConnectionCount() const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);
    size_t total = connections_.size();
    size_t active = 0;
    for (const auto& [id, connection] : connections_) {
        if (connection && connection->isOpen()) {
            active++;
        }
    }
    return total - active;
}

ConnectionPoolStats ConnectionPool::getStats() const {
    ConnectionPoolStats stats;
    updateStats(stats);
    return stats;
}

void ConnectionPool::updateStats(ConnectionPoolStats& stats) const {
    SCOPED_SHARED_LOCK_TIMEOUT(connectionsMutex_, 500);

    stats.totalConnections = connections_.size();
    stats.activeConnections = 0;
    stats.inactiveConnections = 0;
    stats.healthyConnections = 0;
    stats.unhealthyConnections = 0;

    for (const auto& [id, connection] : connections_) {
        if (connection) {
            if (connection->isOpen()) {
                stats.activeConnections++;
                if (connection->isHealthy()) {
                    stats.healthyConnections++;
                } else {
                    stats.unhealthyConnections++;
                }
            } else {
                stats.inactiveConnections++;
                stats.unhealthyConnections++;
            }
        } else {
            stats.inactiveConnections++;
            stats.unhealthyConnections++;
        }
    }

    stats.lastHealthCheck = std::chrono::system_clock::now();
}

void ConnectionPool::updateConfig(const ConnectionPoolConfig& newConfig) {
    SCOPED_LOCK_TIMEOUT(connectionsMutex_, 1000);
    config_ = newConfig;

    // If we're over the new limit, we might need to remove some connections
    if (connections_.size() > config_.maxConnections) {
        WS_LOG_WARN("Configuration update would exceed max connections. Current: " +
                    std::to_string(connections_.size()) + ", New max: " +
                    std::to_string(config_.maxConnections));
    }

    WS_LOG_INFO("Connection pool configuration updated");
}

void ConnectionPool::cleanupStaleConnections() {
    if (!running_.load()) {
        return;
    }

    SCOPED_LOCK_TIMEOUT(connectionsMutex_, 1000);

    size_t cleanedCount = 0;
    size_t batchSize = 0;

    for (auto it = connections_.begin(); it != connections_.end() && batchSize < config_.cleanupBatchSize;) {
        auto& connection = it->second;
        if (isConnectionStale(connection)) {
            WS_LOG_DEBUG("Cleaning up stale connection: " + it->first);
            it = connections_.erase(it);
            cleanedCount++;
            batchSize++;
        } else {
            ++it;
        }
    }

    if (cleanedCount > 0) {
        WS_LOG_INFO("Cleaned up " + std::to_string(cleanedCount) + " stale connections");
    }
}

void ConnectionPool::forceCleanup(size_t maxToRemove) {
    SCOPED_LOCK_TIMEOUT(connectionsMutex_, 1000);

    size_t removedCount = 0;
    size_t targetRemovals = maxToRemove > 0 ? maxToRemove : connections_.size();

    for (auto it = connections_.begin(); it != connections_.end() && removedCount < targetRemovals;) {
        WS_LOG_DEBUG("Force removing connection: " + it->first);
        it = connections_.erase(it);
        removedCount++;
    }

    if (removedCount > 0) {
        WS_LOG_INFO("Force cleaned up " + std::to_string(removedCount) + " connections");
    }
}

bool ConnectionPool::isConnectionStale(const std::shared_ptr<WebSocketConnection>& connection) const {
    if (!connection || !connection->isOpen()) {
        return true;
    }

    // Check if connection hasn't received heartbeat within timeout
    auto lastHeartbeat = connection->getLastHeartbeat();
    auto now = std::chrono::system_clock::now();
    auto timeSinceHeartbeat = now - lastHeartbeat;

    return timeSinceHeartbeat > config_.connectionTimeout;
}

std::string ConnectionPoolStats::toJson() const {
    nlohmann::json json;
    json["totalConnections"] = totalConnections;
    json["activeConnections"] = activeConnections;
    json["inactiveConnections"] = inactiveConnections;
    json["healthyConnections"] = healthyConnections;
    json["unhealthyConnections"] = unhealthyConnections;
    json["lastHealthCheck"] = formatTimestamp(lastHealthCheck);
    json["lastCleanup"] = formatTimestamp(lastCleanup);
    return json.dump();
}
