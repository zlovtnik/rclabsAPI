#pragma once

#include "lock_utils.hpp"
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace net = boost::asio;

// Forward declaration for PooledSession (will be implemented in task 3)
class PooledSession;

enum class TimeoutType { CONNECTION, REQUEST };

/**
 * TimeoutManager provides centralized timeout handling for HTTP connections and
 * requests. It manages Boost.Asio steady_timer instances for connection and
 * request timeouts, with proper cleanup and callback mechanisms.
 */
class TimeoutManager {
public:
  using TimeoutCallback =
      std::function<void(std::shared_ptr<PooledSession>, TimeoutType)>;

  /**
   * Constructor
   * @param ioc IO context for timer operations
   * @param connectionTimeout Default connection timeout duration
   * @param requestTimeout Default request timeout duration
   */
  TimeoutManager(
      net::io_context &ioc,
      std::chrono::seconds connectionTimeout = std::chrono::seconds(30),
      std::chrono::seconds requestTimeout = std::chrono::seconds(60));

  /**
   * Destructor - cancels all active timers
   */
  ~TimeoutManager();

  /**
   * Start a connection timeout for the given session
   * @param session The session to monitor
   * @param callback Optional callback to invoke on timeout (uses default if not
   * provided)
   * @param timeout Optional custom timeout duration (uses default if not
   * provided)
   */
  void startConnectionTimeout(
      std::shared_ptr<PooledSession> session,
      TimeoutCallback callback = nullptr,
      std::chrono::seconds timeout = std::chrono::seconds(0));

  /**
   * Start a request timeout for the given session
   * @param session The session to monitor
   * @param callback Optional callback to invoke on timeout (uses default if not
   * provided)
   * @param timeout Optional custom timeout duration (uses default if not
   * provided)
   */
  void
  startRequestTimeout(std::shared_ptr<PooledSession> session,
                      TimeoutCallback callback = nullptr,
                      std::chrono::seconds timeout = std::chrono::seconds(0));

  /**
   * Cancel all timeouts for the given session
   * @param session The session to cancel timeouts for
   */
  void cancelTimeouts(std::shared_ptr<PooledSession> session);

  /**
   * Cancel connection timeout for the given session
   * @param session The session to cancel connection timeout for
   */
  void cancelConnectionTimeout(std::shared_ptr<PooledSession> session);

  /**
   * Cancel request timeout for the given session
   * @param session The session to cancel request timeout for
   */
  void cancelRequestTimeout(std::shared_ptr<PooledSession> session);

  /**
   * Set the default connection timeout duration
   * @param timeout New connection timeout duration
   */
  void setConnectionTimeout(std::chrono::seconds timeout);

  /**
   * Set the default request timeout duration
   * @param timeout New request timeout duration
   */
  void setRequestTimeout(std::chrono::seconds timeout);

  /**
   * Get the current default connection timeout
   * @return Current connection timeout duration
   */
  std::chrono::seconds getConnectionTimeout() const;

  /**
   * Get the current default request timeout
   * @return Current request timeout duration
   */
  std::chrono::seconds getRequestTimeout() const;

  /**
   * Set the default timeout callback
   * @param callback Callback to invoke when timeouts occur
   */
  void setDefaultTimeoutCallback(TimeoutCallback callback);

  /**
   * Get the number of active connection timers
   * @return Number of active connection timers
   */
  size_t getActiveConnectionTimers() const;

  /**
   * Get the number of active request timers
   * @return Number of active request timers
   */
  size_t getActiveRequestTimers() const;

  /**
   * Cancel all active timers (used during shutdown)
   */
  void cancelAllTimers();

private:
  struct TimerInfo {
    std::unique_ptr<net::steady_timer> timer;
    TimeoutCallback callback;
    TimeoutType type;
    std::chrono::seconds duration;
  };

  net::io_context &ioc_;
  std::chrono::seconds connectionTimeout_;
  std::chrono::seconds requestTimeout_;
  TimeoutCallback defaultCallback_;

  // Maps to track active timers by session
  std::map<std::shared_ptr<PooledSession>, std::unique_ptr<TimerInfo>>
      connectionTimers_;
  std::map<std::shared_ptr<PooledSession>, std::unique_ptr<TimerInfo>>
      requestTimers_;

  mutable etl_plus::ResourceMutex timerMutex_;

  /**
   * Handle timeout event
   * @param session The session that timed out
   * @param type The type of timeout
   * @param callback The callback to invoke
   * @param ec Error code from timer operation
   * @param firedTimer The timer that fired
   */
  void handleTimeout(std::shared_ptr<PooledSession> session, TimeoutType type,
                     TimeoutCallback callback,
                     const boost::system::error_code &ec,
                     net::steady_timer *firedTimer);

  /**
   * Default timeout handler - logs timeout events
   * @param session The session that timed out
   * @param type The type of timeout
   */
  void defaultTimeoutHandler(std::shared_ptr<PooledSession> session,
                             TimeoutType type);

  /**
   * Remove timer from the appropriate map
   * @param session The session to remove timer for
   * @param type The type of timer to remove
   */
  void removeTimer(std::shared_ptr<PooledSession> session, TimeoutType type);
};