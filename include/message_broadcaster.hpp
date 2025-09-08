#pragma once

#include "websocket_connection.hpp"
#include "connection_pool.hpp"
#include "job_monitoring_models.hpp"
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

/**
 * @brief Configuration for message broadcaster behavior
 */
struct MessageBroadcasterConfig {
    size_t maxQueueSize = 10000;                    // Maximum message queue size
    size_t batchSize = 50;                          // Messages to process per batch
    std::chrono::milliseconds processingInterval = std::chrono::milliseconds(10); // Processing interval
    bool enableAsyncProcessing = true;              // Enable asynchronous message processing
    bool enableMessagePrioritization = false;       // Enable message prioritization
    size_t maxConcurrentBroadcasts = 10;            // Maximum concurrent broadcast operations
};

/**
 * @brief Message broadcaster statistics
 */
struct MessageBroadcasterStats {
    size_t totalMessagesSent = 0;
    size_t totalMessagesQueued = 0;
    size_t totalMessagesDropped = 0;
    size_t currentQueueSize = 0;
    size_t activeBroadcasts = 0;
    std::chrono::system_clock::time_point lastMessageSent;
    double messagesPerSecond = 0.0;

    std::string toJson() const;
};

/**
 * @brief Internal message structure for queuing
 */
struct QueuedMessage {
    std::string message;
    MessageType type;
    std::string jobId;
    std::string logLevel;
    std::chrono::system_clock::time_point timestamp;
    int priority = 0; // Higher priority = processed first

    bool operator<(const QueuedMessage& other) const {
        return priority < other.priority;
    }
};

/**
 * @brief Handles message distribution and broadcasting to WebSocket connections
 *
 * This component is responsible for:
 * - Broadcasting messages to multiple connections
 * - Message filtering and selective broadcasting
 * - Message queuing and delivery guarantees
 * - Performance optimizations for high-throughput
 * - Thread-safe message processing
 */
class MessageBroadcaster : public std::enable_shared_from_this<MessageBroadcaster> {
public:
    explicit MessageBroadcaster(std::shared_ptr<ConnectionPool> connectionPool);
    MessageBroadcaster(std::shared_ptr<ConnectionPool> connectionPool, const MessageBroadcasterConfig& config);
    ~MessageBroadcaster();

    // Broadcaster lifecycle
    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // Basic broadcasting
    void broadcastMessage(const std::string& message);
    void sendToConnection(const std::string& connectionId, const std::string& message);

    // Enhanced broadcasting with filtering
    void broadcastJobUpdate(const std::string& message, const std::string& jobId);
    void broadcastLogMessage(const std::string& message, const std::string& jobId, const std::string& logLevel);
    void broadcastByMessageType(const std::string& message, MessageType messageType, const std::string& jobId = "");
    void broadcastToFilteredConnections(const std::string& message,
                                      std::function<bool(const ConnectionFilters&)> filterPredicate);

    // Advanced message routing
    void broadcastWithAdvancedRouting(const WebSocketMessage& message);
    void sendToMatchingConnections(const WebSocketMessage& message,
                                 std::function<bool(const ConnectionFilters&, const WebSocketMessage&)> customMatcher);
    bool testConnectionFilter(const std::string& connectionId, const WebSocketMessage& testMessage) const;

    // Connection filter management
    void setConnectionFilters(const std::string& connectionId, const ConnectionFilters& filters);
    ConnectionFilters getConnectionFilters(const std::string& connectionId) const;
    void updateConnectionFilters(const std::string& connectionId, const ConnectionFilters& filters);

    // Enhanced filter management
    void addJobFilterToConnection(const std::string& connectionId, const std::string& jobId);
    void removeJobFilterFromConnection(const std::string& connectionId, const std::string& jobId);
    void addMessageTypeFilterToConnection(const std::string& connectionId, MessageType messageType);
    void removeMessageTypeFilterFromConnection(const std::string& connectionId, MessageType messageType);
    void addLogLevelFilterToConnection(const std::string& connectionId, const std::string& logLevel);
    void removeLogLevelFilterFromConnection(const std::string& connectionId, const std::string& logLevel);
    void clearConnectionFilters(const std::string& connectionId);

    // Connection analysis and statistics
    std::vector<std::string> getConnectionsForJob(const std::string& jobId) const;
    std::vector<std::string> getConnectionsForMessageType(MessageType messageType) const;
    std::vector<std::string> getConnectionsForLogLevel(const std::string& logLevel) const;
    size_t getFilteredConnectionCount() const;
    size_t getUnfilteredConnectionCount() const;

    // Statistics and metrics
    MessageBroadcasterStats getStats() const;

    // Configuration
    void updateConfig(const MessageBroadcasterConfig& newConfig);
    const MessageBroadcasterConfig& getConfig() const { return config_; }

    // Queue management
    void flushQueue();
    void clearQueue();
    bool isQueueFull() const;
    size_t getQueueSize() const;

private:
    // Dependencies and configuration
    std::shared_ptr<ConnectionPool> connectionPool_;
    MessageBroadcasterConfig config_;
    std::atomic<bool> running_{false};

    // Message queuing
    mutable std::mutex queueMutex_;
    std::priority_queue<QueuedMessage> messageQueue_;
    std::condition_variable queueCondition_;
    std::atomic<size_t> activeBroadcasts_{0};

    // Statistics
    mutable std::mutex statsMutex_;
    MessageBroadcasterStats stats_;

    // Internal helper methods
    void processMessageQueue();
    void processQueuedMessage(const QueuedMessage& msg);
    void broadcastToConnections(const std::string& message,
                               const std::vector<std::shared_ptr<WebSocketConnection>>& connections);
    void sendMessageToConnection(const std::shared_ptr<WebSocketConnection>& connection,
                                const std::string& message);
    void updateStats(size_t messagesSent, size_t messagesDropped = 0);
    bool shouldProcessMessage(const std::shared_ptr<WebSocketConnection>& connection,
                             MessageType type, const std::string& jobId = "", const std::string& logLevel = "") const;
    bool shouldProcessMessage(const std::shared_ptr<WebSocketConnection>& connection,
                             const WebSocketMessage& message) const;

    // Async processing
    void startAsyncProcessing();
    void stopAsyncProcessing();
    std::vector<std::thread> processingThreads_;
};
