#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Forward declarations
class PooledSession;
class RequestHandler;
class WebSocketManager;
class TimeoutManager;

/**
 * ConnectionPoolManager manages a pool of reusable PooledSession connections
 * to reduce connection overhead and improve server performance.
 * 
 * Features:
 * - Configurable minimum and maximum connection limits
 * - Thread-safe connection acquisition and release
 * - Automatic idle connection cleanup
 * - Connection lifecycle management
 * - Performance metrics collection
 */
class ConnectionPoolManager {
public:
    /**
     * Constructor
     * @param ioc IO context for asynchronous operations
     * @param minConnections Minimum number of connections to maintain in pool
     * @param maxConnections Maximum number of connections allowed in pool
     * @param idleTimeout Duration after which idle connections are cleaned up
     * @param handler Request handler for new sessions
     * @param wsManager WebSocket manager for new sessions
     * @param timeoutManager Timeout manager for new sessions
     */
    ConnectionPoolManager(net::io_context& ioc,
                         size_t minConnections,
                         size_t maxConnections,
                         std::chrono::seconds idleTimeout,
                         std::shared_ptr<RequestHandler> handler,
                         std::shared_ptr<WebSocketManager> wsManager,
                         std::shared_ptr<TimeoutManager> timeoutManager);

    /**
     * Destructor - ensures proper cleanup of all connections
     */
    ~ConnectionPoolManager();

    /**
     * Acquire a connection from the pool
     * If no idle connections are available and pool is not at max capacity,
     * creates a new connection. If pool is at capacity, blocks until a
     * connection becomes available.
     * 
     * @param socket TCP socket for the connection
     * @return Shared pointer to a PooledSession ready for use
     */
    std::shared_ptr<PooledSession> acquireConnection(tcp::socket&& socket);

    /**
     * Release a connection back to the pool
     * The connection is reset and marked as idle for reuse.
     * 
     * @param session The session to release back to the pool
     */
    void releaseConnection(std::shared_ptr<PooledSession> session);

    /**
     * Start the cleanup timer for removing idle connections
     * This should be called after construction to begin automatic cleanup.
     */
    void startCleanupTimer();

    /**
     * Stop the cleanup timer and cancel any pending cleanup operations
     */
    void stopCleanupTimer();

    /**
     * Manually trigger cleanup of idle connections
     * Removes connections that have been idle longer than the configured timeout.
     * 
     * @return Number of connections cleaned up
     */
    size_t cleanupIdleConnections();

    /**
     * Shutdown the pool and close all connections
     * This method should be called during server shutdown.
     */
    void shutdown();

    // Metrics and monitoring methods

    /**
     * Get the number of currently active connections
     * @return Number of active connections
     */
    size_t getActiveConnections() const;

    /**
     * Get the number of currently idle connections
     * @return Number of idle connections
     */
    size_t getIdleConnections() const;

    /**
     * Get the total number of connections in the pool
     * @return Total number of connections (active + idle)
     */
    size_t getTotalConnections() const;

    /**
     * Get the maximum number of connections allowed
     * @return Maximum connection limit
     */
    size_t getMaxConnections() const;

    /**
     * Get the minimum number of connections to maintain
     * @return Minimum connection limit
     */
    size_t getMinConnections() const;

    /**
     * Get the idle timeout duration
     * @return Idle timeout duration
     */
    std::chrono::seconds getIdleTimeout() const;

    /**
     * Check if the pool is currently at maximum capacity
     * @return true if pool is at maximum capacity
     */
    bool isAtMaxCapacity() const;

    /**
     * Get statistics about connection reuse
     * @return Number of times connections have been reused
     */
    size_t getConnectionReuseCount() const;

    /**
     * Get the number of connections created since pool initialization
     * @return Total connections created
     */
    size_t getTotalConnectionsCreated() const;

    /**
     * Reset all statistics counters
     */
    void resetStatistics();

private:
    // Configuration
    net::io_context& ioc_;
    size_t minConnections_;
    size_t maxConnections_;
    std::chrono::seconds idleTimeout_;
    
    // Dependencies
    std::shared_ptr<RequestHandler> handler_;
    std::shared_ptr<WebSocketManager> wsManager_;
    std::shared_ptr<TimeoutManager> timeoutManager_;

    // Connection pools
    std::queue<std::shared_ptr<PooledSession>> idleConnections_;
    std::set<std::shared_ptr<PooledSession>> activeConnections_;

    // Thread safety
    mutable std::mutex poolMutex_;
    std::condition_variable connectionAvailable_;

    // Cleanup timer
    std::unique_ptr<net::steady_timer> cleanupTimer_;
    bool shutdownRequested_;

    // Statistics
    size_t connectionReuseCount_;
    size_t totalConnectionsCreated_;

    /**
     * Create a new PooledSession with the given socket
     * @param socket TCP socket for the new session
     * @return Shared pointer to the new PooledSession
     */
    std::shared_ptr<PooledSession> createNewSession(tcp::socket&& socket);

    /**
     * Schedule the next cleanup operation
     */
    void scheduleCleanup();

    /**
     * Handle cleanup timer expiration
     * @param ec Error code from timer operation
     */
    void handleCleanupTimer(const boost::system::error_code& ec);

    /**
     * Remove a connection from the active set (internal use)
     * Must be called with poolMutex_ held
     * @param session The session to remove
     */
    void removeFromActiveSet(std::shared_ptr<PooledSession> session);

    /**
     * Add a connection to the idle queue (internal use)
     * Must be called with poolMutex_ held
     * @param session The session to add to idle queue
     */
    void addToIdleQueue(std::shared_ptr<PooledSession> session);

    /**
     * Check if a session is eligible for cleanup based on idle time
     * @param session The session to check
     * @return true if session should be cleaned up
     */
    bool shouldCleanupSession(std::shared_ptr<PooledSession> session) const;
};