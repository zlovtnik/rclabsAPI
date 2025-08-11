#include "timeout_manager.hpp"
#include "pooled_session.hpp"
#include "logger.hpp"
#include "transparent_string_hash.hpp"
#include <boost/system/error_code.hpp>

TimeoutManager::TimeoutManager(net::io_context& ioc,
                               std::chrono::seconds connectionTimeout,
                               std::chrono::seconds requestTimeout)
    : ioc_(ioc)
    , connectionTimeout_(connectionTimeout)
    , requestTimeout_(requestTimeout)
    , defaultCallback_(std::bind(&TimeoutManager::defaultTimeoutHandler, this,
                                std::placeholders::_1, std::placeholders::_2)) {
    HTTP_LOG_INFO("TimeoutManager created with connection timeout: " + 
                  std::to_string(connectionTimeout.count()) + "s, request timeout: " + 
                  std::to_string(requestTimeout.count()) + "s");
}

TimeoutManager::~TimeoutManager() {
    HTTP_LOG_DEBUG("TimeoutManager destructor - cancelling all timers");
    cancelAllTimers();
}

void TimeoutManager::startConnectionTimeout(std::shared_ptr<PooledSession> session,
                                           TimeoutCallback callback,
                                           std::chrono::seconds timeout) {
    if (!session) {
        HTTP_LOG_ERROR("TimeoutManager::startConnectionTimeout - null session provided");
        return;
    }

    std::lock_guard<std::mutex> lock(timerMutex_);
    
    // Use provided timeout or default
    auto actualTimeout = (timeout.count() > 0) ? timeout : connectionTimeout_;
    auto actualCallback = callback ? callback : defaultCallback_;

    // Cancel existing connection timer if any
    auto it = connectionTimers_.find(session);
    if (it != connectionTimers_.end()) {
        HTTP_LOG_DEBUG("TimeoutManager::startConnectionTimeout - cancelling existing connection timer");
        it->second->timer->cancel();
        connectionTimers_.erase(it);
    }

    // Create new timer
    auto timerInfo = std::make_unique<TimerInfo>();
    timerInfo->timer = std::make_unique<net::steady_timer>(ioc_);
    timerInfo->callback = actualCallback;
    timerInfo->type = TimeoutType::CONNECTION;
    timerInfo->duration = actualTimeout;

    // Set timer expiration
    timerInfo->timer->expires_after(actualTimeout);

    // Start async wait
    timerInfo->timer->async_wait([this, session, actualCallback](const boost::system::error_code& ec) {
        handleTimeout(session, TimeoutType::CONNECTION, actualCallback, ec);
    });

    connectionTimers_[session] = std::move(timerInfo);
    
    HTTP_LOG_DEBUG("TimeoutManager::startConnectionTimeout - started connection timer for " + 
                   std::to_string(actualTimeout.count()) + " seconds");
}

void TimeoutManager::startRequestTimeout(std::shared_ptr<PooledSession> session,
                                        TimeoutCallback callback,
                                        std::chrono::seconds timeout) {
    if (!session) {
        HTTP_LOG_ERROR("TimeoutManager::startRequestTimeout - null session provided");
        return;
    }

    std::lock_guard<std::mutex> lock(timerMutex_);
    
    // Use provided timeout or default
    auto actualTimeout = (timeout.count() > 0) ? timeout : requestTimeout_;
    auto actualCallback = callback ? callback : defaultCallback_;

    // Cancel existing request timer if any
    auto it = requestTimers_.find(session);
    if (it != requestTimers_.end()) {
        HTTP_LOG_DEBUG("TimeoutManager::startRequestTimeout - cancelling existing request timer");
        it->second->timer->cancel();
        requestTimers_.erase(it);
    }

    // Create new timer
    auto timerInfo = std::make_unique<TimerInfo>();
    timerInfo->timer = std::make_unique<net::steady_timer>(ioc_);
    timerInfo->callback = actualCallback;
    timerInfo->type = TimeoutType::REQUEST;
    timerInfo->duration = actualTimeout;

    // Set timer expiration
    timerInfo->timer->expires_after(actualTimeout);

    // Start async wait
    timerInfo->timer->async_wait([this, session, actualCallback](const boost::system::error_code& ec) {
        handleTimeout(session, TimeoutType::REQUEST, actualCallback, ec);
    });

    requestTimers_[session] = std::move(timerInfo);
    
    HTTP_LOG_DEBUG("TimeoutManager::startRequestTimeout - started request timer for " + 
                   std::to_string(actualTimeout.count()) + " seconds");
}

void TimeoutManager::cancelTimeouts(std::shared_ptr<PooledSession> session) {
    if (!session) {
        return;
    }

    std::lock_guard<std::mutex> lock(timerMutex_);
    
    // Cancel connection timer
    auto connIt = connectionTimers_.find(session);
    if (connIt != connectionTimers_.end()) {
        HTTP_LOG_DEBUG("TimeoutManager::cancelTimeouts - cancelling connection timer");
        connIt->second->timer->cancel();
        connectionTimers_.erase(connIt);
    }

    // Cancel request timer
    auto reqIt = requestTimers_.find(session);
    if (reqIt != requestTimers_.end()) {
        HTTP_LOG_DEBUG("TimeoutManager::cancelTimeouts - cancelling request timer");
        reqIt->second->timer->cancel();
        requestTimers_.erase(reqIt);
    }
}

void TimeoutManager::cancelConnectionTimeout(std::shared_ptr<PooledSession> session) {
    if (!session) {
        return;
    }

    std::lock_guard<std::mutex> lock(timerMutex_);
    
    auto it = connectionTimers_.find(session);
    if (it != connectionTimers_.end()) {
        HTTP_LOG_DEBUG("TimeoutManager::cancelConnectionTimeout - cancelling connection timer");
        it->second->timer->cancel();
        connectionTimers_.erase(it);
    }
}

void TimeoutManager::cancelRequestTimeout(std::shared_ptr<PooledSession> session) {
    if (!session) {
        return;
    }

    std::lock_guard<std::mutex> lock(timerMutex_);
    
    auto it = requestTimers_.find(session);
    if (it != requestTimers_.end()) {
        HTTP_LOG_DEBUG("TimeoutManager::cancelRequestTimeout - cancelling request timer");
        it->second->timer->cancel();
        requestTimers_.erase(it);
    }
}

void TimeoutManager::setConnectionTimeout(std::chrono::seconds timeout) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    connectionTimeout_ = timeout;
    HTTP_LOG_INFO("TimeoutManager::setConnectionTimeout - updated to " + 
                  std::to_string(timeout.count()) + " seconds");
}

void TimeoutManager::setRequestTimeout(std::chrono::seconds timeout) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    requestTimeout_ = timeout;
    HTTP_LOG_INFO("TimeoutManager::setRequestTimeout - updated to " + 
                  std::to_string(timeout.count()) + " seconds");
}

std::chrono::seconds TimeoutManager::getConnectionTimeout() const {
    std::lock_guard<std::mutex> lock(timerMutex_);
    return connectionTimeout_;
}

std::chrono::seconds TimeoutManager::getRequestTimeout() const {
    std::lock_guard<std::mutex> lock(timerMutex_);
    return requestTimeout_;
}

void TimeoutManager::setDefaultTimeoutCallback(TimeoutCallback callback) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    defaultCallback_ = callback ? callback : 
                      std::bind(&TimeoutManager::defaultTimeoutHandler, this,
                               std::placeholders::_1, std::placeholders::_2);
    HTTP_LOG_DEBUG("TimeoutManager::setDefaultTimeoutCallback - callback updated");
}

size_t TimeoutManager::getActiveConnectionTimers() const {
    std::lock_guard<std::mutex> lock(timerMutex_);
    return connectionTimers_.size();
}

size_t TimeoutManager::getActiveRequestTimers() const {
    std::lock_guard<std::mutex> lock(timerMutex_);
    return requestTimers_.size();
}

void TimeoutManager::cancelAllTimers() {
    std::lock_guard<std::mutex> lock(timerMutex_);
    
    HTTP_LOG_DEBUG("TimeoutManager::cancelAllTimers - cancelling " + 
                   std::to_string(connectionTimers_.size()) + " connection timers and " +
                   std::to_string(requestTimers_.size()) + " request timers");

    // Cancel all connection timers
    for (auto& pair : connectionTimers_) {
        pair.second->timer->cancel();
    }
    connectionTimers_.clear();

    // Cancel all request timers
    for (auto& pair : requestTimers_) {
        pair.second->timer->cancel();
    }
    requestTimers_.clear();
}

void TimeoutManager::handleTimeout(std::shared_ptr<PooledSession> session,
                                  TimeoutType type,
                                  TimeoutCallback callback,
                                  const boost::system::error_code& ec) {
    std::string typeStr = (type == TimeoutType::CONNECTION) ? "connection" : "request";
    
    if (ec == boost::asio::error::operation_aborted) {
        // Timer was cancelled, this is normal
        HTTP_LOG_DEBUG("TimeoutManager::handleTimeout - timer cancelled for " + typeStr);
        return;
    }

    if (ec) {
        HTTP_LOG_ERROR("TimeoutManager::handleTimeout - timer error for " + typeStr + ": " + ec.message());
        return;
    }

    HTTP_LOG_DEBUG("TimeoutManager::handleTimeout - timeout occurred for " + typeStr);

    // Remove the timer from our tracking maps
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        removeTimer(session, type);
    }

    // Invoke the callback
    if (callback) {
        try {
            HTTP_LOG_DEBUG("TimeoutManager::handleTimeout - invoking callback for " + typeStr);
            callback(session, type);
        } catch (const std::exception& e) {
            HTTP_LOG_ERROR("TimeoutManager::handleTimeout - exception in timeout callback: " + 
                           std::string(e.what()));
        } catch (...) {
            HTTP_LOG_ERROR("TimeoutManager::handleTimeout - unknown exception in timeout callback");
        }
    } else {
        HTTP_LOG_DEBUG("TimeoutManager::handleTimeout - no callback provided for " + typeStr);
    }
}

void TimeoutManager::defaultTimeoutHandler(std::shared_ptr<PooledSession> session, TimeoutType type) {
    std::string typeStr = (type == TimeoutType::CONNECTION) ? "CONNECTION" : "REQUEST";
    
    HTTP_LOG_WARN("TimeoutManager::defaultTimeoutHandler - " + typeStr + " timeout occurred");
    
    // Log timeout event with context
    std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> context;
    context["timeout_type"] = typeStr;
    context["session_ptr"] = std::to_string(reinterpret_cast<uintptr_t>(session.get()));
    
    Logger::getInstance().warn("TimeoutManager", 
                              "Timeout occurred for " + typeStr, 
                              context);
    
    // Call the session's timeout handler
    if (session) {
        session->handleTimeout(typeStr);
    }
}

void TimeoutManager::removeTimer(std::shared_ptr<PooledSession> session, TimeoutType type) {
    // Note: This method assumes the mutex is already locked
    if (type == TimeoutType::CONNECTION) {
        connectionTimers_.erase(session);
    } else {
        requestTimers_.erase(session);
    }
}