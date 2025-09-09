#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct RateLimitRule {
    std::string endpoint;
    int requestsPerMinute;
    int requestsPerHour;
};

struct RateLimitInfo {
    int remainingRequests;
    std::chrono::system_clock::time_point resetTime;
    int limit;
};

class RateLimiter {
public:
    RateLimiter();
    ~RateLimiter() = default;

    // Initialize with default rules
    void initializeDefaultRules();

    // Add custom rate limit rule
    void addRule(const RateLimitRule& rule);

    // Check if request is allowed
    bool isAllowed(const std::string& clientId, const std::string& endpoint);

    // Get rate limit info for client
    RateLimitInfo getRateLimitInfo(const std::string& clientId, const std::string& endpoint);

    // Reset rate limits for a client
    void resetClient(const std::string& clientId);

    // Clean up expired entries
    void cleanupExpiredEntries();

private:
    struct ClientData {
        std::unordered_map<std::string, int> minuteCounters;
        std::unordered_map<std::string, int> hourCounters;
        std::unordered_map<std::string, std::chrono::system_clock::time_point> lastReset;
    };

    std::unordered_map<std::string, ClientData> clientData_;
    std::vector<RateLimitRule> rules_;
    mutable std::mutex mutex_;

    // Get rule for endpoint
    const RateLimitRule* getRuleForEndpoint(const std::string& endpoint) const;

    // Get current minute/hour window
    std::pair<int, int> getCurrentWindows() const;
};
