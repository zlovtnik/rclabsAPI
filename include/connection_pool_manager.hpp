#pragma once

#include "lock_utils.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
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
class PerformanceMonitor;

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
  struct QueueConfig {
    size_t maxSize;
    std::chrono::seconds maxWait;
  };
  struct MonitorConfig {
    std::shared_ptr<PerformanceMonitor> perf;
  };

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
   * @param performanceMonitor Performance monitor for metrics collection
   * @param maxQueueSize Maximum number of requests to queue when pool is at
   * capacity
   * @param maxQueueWaitTime Maximum time a request can wait in queue
   */
  ConnectionPoolManager(net::io_context &ioc, size_t minConnections,
                        size_t maxConnections, std::chrono::seconds idleTimeout,
                        std::shared_ptr<RequestHandler> handler,
                        std::shared_ptr<WebSocketManager> wsManager,
                        std::shared_ptr<TimeoutManager> timeoutManager,
                        MonitorConfig monitor, QueueConfig queue);

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
  std::shared_ptr<PooledSession> acquireConnection(tcp::socket &&socket);

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
  [[nodiscard]] size_t getActiveConnections() const noexcept;

  /**
   * Get the number of currently idle connections
   * @return Number of idle connections
   */
  [[nodiscard]] size_t getIdleConnections() const noexcept;

  /**
   * Get the total number of connections in the pool
   * @return Total number of connections (active + idle)
   */
  [[nodiscard]] size_t getTotalConnections() const noexcept;

  /**
   * Get the maximum number of connections allowed
   * @return Maximum connection limit
   */
  [[nodiscard]] size_t getMaxConnections() const noexcept;

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
   * Get the current size of the request queue
   * @return Number of requests waiting in queue
   */
  size_t getQueueSize() const;

  /**
   * Get the maximum queue size
   * @return Maximum number of requests that can be queued
   */
  [[nodiscard]] size_t getMaxQueueSize() const noexcept;

  /**
   * Get the number of requests that have been rejected due to queue overflow
   * @return Number of rejected requests
   */
  [[nodiscard]] size_t getRejectedRequestCount() const noexcept;

  /**
   * Reset all statistics counters
   */
  void resetStatistics();

private:
  // Configuration
  net::io_context &ioc_;
  size_t minConnections_;
  size_t maxConnections_;
  std::chrono::seconds idleTimeout_;

  // Dependencies
  std::shared_ptr<RequestHandler> handler_;
  std::shared_ptr<WebSocketManager> wsManager_;
  std::shared_ptr<TimeoutManager> timeoutManager_;
  std::shared_ptr<PerformanceMonitor> performanceMonitor_;

  // Connection pools
  std::queue<std::shared_ptr<PooledSession>> idleConnections_;
  std::set<std::shared_ptr<PooledSession>> activeConnections_;

  // Request queuing for pool exhaustion scenarios
  struct QueuedRequest {
    tcp::socket socket;
    std::chrono::steady_clock::time_point queueTime;

    QueuedRequest(tcp::socket &&s)
        : socket(std::move(s)), queueTime(std::chrono::steady_clock::now()) {}
  };
  std::queue<QueuedRequest, std::deque<QueuedRequest>> requestQueue_;
  size_t maxQueueSize_;
  std::chrono::seconds maxQueueWaitTime_;

  // Thread safety
  mutable etl_plus::ContainerMutex poolMutex_;
  std::condition_variable_any connectionAvailable_;
  std::condition_variable_any requestQueued_;

  // Cleanup timer
  std::optional<net::steady_timer> cleanupTimer_;
  bool shutdownRequested_;

  // Statistics
  size_t connectionReuseCount_;
  size_t totalConnectionsCreated_;
  size_t rejectedRequestCount_;

  /**
   * Create a new PooledSession with the given socket
   * @param socket TCP socket for the new session
   * @return Shared pointer to the new PooledSession
   */
  std::shared_ptr<PooledSession> createNewSession(tcp::socket &&socket);

  /**
   * Schedule the next cleanup operation
   */
  void scheduleCleanup();

  /**
   * Handle cleanup timer expiration
   * @param ec Error code from timer operation
   */
  void handleCleanupTimer(const boost::system::error_code &ec);

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

  /**
   * Process queued requests when connections become available
   * Must be called with poolMutex_ held
   */
  void processQueuedRequests();

  /**
   * Clean up expired requests from the queue
   * Must be called with poolMutex_ held
   * @return Number of expired requests removed
   */
  size_t cleanupExpiredQueuedRequests();

  /**
   * Send error response for rejected request
   * @param socket The socket to send error response to
   * @param errorMessage The error message to send
   */
  void sendErrorResponse(tcp::socket &socket, const std::string &errorMessage);
};