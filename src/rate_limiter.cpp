#include "rate_limiter.hpp"
#include "component_logger.hpp"
#include "logger.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>

RateLimiter::RateLimiter() {
    initializeDefaultRules();
}

void RateLimiter::initializeDefaultRules() {
    // Default rate limiting rules
    rules_ = {
        {"/api/auth/login", 5, 20},      // 5 per minute, 20 per hour for login
        {"/api/auth/logout", 10, 50},    // 10 per minute, 50 per hour for logout
        {"/api/auth/profile", 30, 200},  // 30 per minute, 200 per hour for profile
        {"/api/logs", 60, 500},          // 60 per minute, 500 per hour for logs
        {"/api/jobs", 30, 200},          // 30 per minute, 200 per hour for jobs
        {"/api/monitor", 120, 1000},     // 120 per minute, 1000 per hour for monitoring
        {"/api/health", 300, 2000}       // 300 per minute, 2000 per hour for health checks
    };

    // Sort rules by descending prefix length to ensure more specific rules are checked first
    std::sort(rules_.begin(), rules_.end(), [](const RateLimitRule& a, const RateLimitRule& b) {
        return a.endpoint.length() > b.endpoint.length();
    });

    etl::ComponentLogger<RateLimiter>::info("RateLimiter initialized with default rules");
}

void RateLimiter::addRule(const RateLimitRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.push_back(rule);
    // Sort rules by descending prefix length to ensure more specific rules are checked first
    std::sort(rules_.begin(), rules_.end(), [](const RateLimitRule& a, const RateLimitRule& b) {
        return a.endpoint.length() > b.endpoint.length();
    });
    etl::ComponentLogger<RateLimiter>::info("Added rate limit rule for endpoint: " + rule.endpoint);
}

bool RateLimiter::isAllowed(const std::string& clientId, const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);

    const RateLimitRule* rule = getRuleForEndpoint(endpoint);
    if (!rule) {
        // No rule found, allow request
        return true;
    }

    auto [currentMinute, currentHour] = getCurrentWindows();

    // Get or create client data
    auto& clientData = clientData_[clientId];

    // Check minute limit
    std::string minuteKey = endpoint + "_min_" + std::to_string(currentMinute);
    if (clientData.minuteCounters[minuteKey] >= rule->requestsPerMinute) {
        etl::ComponentLogger<RateLimiter>::warn("Rate limit exceeded for client " + clientId + " on endpoint " + endpoint + " (minute limit)");
        return false;
    }

    // Check hour limit
    std::string hourKey = endpoint + "_hour_" + std::to_string(currentHour);
    if (clientData.hourCounters[hourKey] >= rule->requestsPerHour) {
        etl::ComponentLogger<RateLimiter>::warn("Rate limit exceeded for client " + clientId + " on endpoint " + endpoint + " (hour limit)");
        return false;
    }

    // Increment counters
    clientData.minuteCounters[minuteKey]++;
    clientData.hourCounters[hourKey]++;

    etl::ComponentLogger<RateLimiter>::debug("Request allowed for client " + clientId + " on endpoint " + endpoint);
    return true;
}

RateLimitInfo RateLimiter::getRateLimitInfo(const std::string& clientId, const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);

    const RateLimitRule* rule = getRuleForEndpoint(endpoint);
    if (!rule) {
        return {INT_MAX, std::chrono::system_clock::time_point::max(), INT_MAX};
    }

    auto [currentMinute, currentHour] = getCurrentWindows();

    auto& clientData = clientData_[clientId];

    // Calculate remaining requests for minute window
    std::string minuteKey = endpoint + "_min_" + std::to_string(currentMinute);
    int minuteUsed = clientData.minuteCounters[minuteKey];
    int minuteRemaining = std::max(0, rule->requestsPerMinute - minuteUsed);

    // Calculate remaining requests for hour window
    std::string hourKey = endpoint + "_hour_" + std::to_string(currentHour);
    int hourUsed = clientData.hourCounters[hourKey];
    int hourRemaining = std::max(0, rule->requestsPerHour - hourUsed);

    // Use per-minute limit as the primary enforcement
    int remaining = minuteRemaining;
    int limit = rule->requestsPerMinute;

    // Calculate reset time aligned to next minute boundary
    auto nextMinuteTime = std::chrono::system_clock::time_point(std::chrono::minutes(currentMinute + 1));
    auto resetTime = nextMinuteTime;

    return {remaining, resetTime, limit};
}

void RateLimiter::resetClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    clientData_.erase(clientId);
    etl::ComponentLogger<RateLimiter>::info("Reset rate limits for client: " + clientId);
}

void RateLimiter::cleanupExpiredEntries() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto [currentMinute, currentHour] = getCurrentWindows();

    // Clean up old minute counters (keep only current and previous minute)
    for (auto& [clientId, clientData] : clientData_) {
        std::vector<std::string> toRemove;
        for (const auto& [key, count] : clientData.minuteCounters) {
            if (key.find("_min_") != std::string::npos) {
                size_t pos = key.find_last_of('_');
                if (pos != std::string::npos) {
                    try {
                        int64_t window = std::stoll(key.substr(pos + 1));
                        if (window < currentMinute - 1) {
                            toRemove.push_back(key);
                        }
                    } catch (const std::exception&) {
                        // Invalid key format, remove it
                        toRemove.push_back(key);
                    }
                }
            }
        }
        for (const auto& key : toRemove) {
            clientData.minuteCounters.erase(key);
        }
    }

    // Clean up old hour counters (keep only current and previous hour)
    for (auto& [clientId, clientData] : clientData_) {
        std::vector<std::string> toRemove;
        for (const auto& [key, count] : clientData.hourCounters) {
            if (key.find("_hour_") != std::string::npos) {
                size_t pos = key.find_last_of('_');
                if (pos != std::string::npos) {
                    try {
                        int64_t window = std::stoll(key.substr(pos + 1));
                        if (window < currentHour - 1) {
                            toRemove.push_back(key);
                        }
                    } catch (const std::exception&) {
                        // Invalid key format, remove it
                        toRemove.push_back(key);
                    }
                }
            }
        }
        for (const auto& key : toRemove) {
            clientData.hourCounters.erase(key);
        }
    }

    etl::ComponentLogger<RateLimiter>::debug("Cleaned up expired rate limit entries");
}

const RateLimitRule* RateLimiter::getRuleForEndpoint(const std::string& endpoint) const {
    for (const auto& rule : rules_) {
        if (endpoint.rfind(rule.endpoint, 0) == 0) {
            return &rule;
        }
    }
    return nullptr;
}

std::pair<int64_t, int64_t> RateLimiter::getCurrentWindows() const {
    auto now = std::chrono::system_clock::now();
    auto timeSinceEpoch = now.time_since_epoch();

    const int64_t minutesSinceEpoch = std::chrono::duration_cast<std::chrono::minutes>(timeSinceEpoch).count();
    const int64_t hoursSinceEpoch = std::chrono::duration_cast<std::chrono::hours>(timeSinceEpoch).count();
    return {minutesSinceEpoch, hoursSinceEpoch};
}
