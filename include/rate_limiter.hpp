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
  int remainingRequests; // Remaining requests for the current minute window
  std::chrono::system_clock::time_point
      resetTime; // Reset time for the minute window
  int limit;     // Request limit for the minute window
};

class RateLimiter {
public:
  RateLimiter();
  ~RateLimiter() = default;

  // Delete copy and move operations
  RateLimiter(const RateLimiter&) = delete;
  RateLimiter& operator=(const RateLimiter&) = delete;
  RateLimiter(RateLimiter&&) = delete;
  RateLimiter& operator=(RateLimiter&&) = delete;

  // Initialize with default rules
  void initializeDefaultRules();

  // Add custom rate limit rule
  void addRule(const RateLimitRule &rule);

  // Check if request is allowed
  bool isAllowed(const std::string &clientId, const std::string &endpoint);

  // Get rate limit info for client
  RateLimitInfo getRateLimitInfo(const std::string &clientId,
                                 const std::string &endpoint);

  // Reset rate limits for a client
  void resetClient(const std::string &clientId);

  // Clean up expired entries
  // This method should be called periodically (e.g., every hour) by a
  // background task to remove stale client data and prevent memory leaks
  void cleanupExpiredEntries();

private:
  struct ClientData {
    std::unordered_map<std::string, int> minuteCounters;
    std::unordered_map<std::string, int> hourCounters;
  };

  std::unordered_map<std::string, ClientData> clientData_;
  std::vector<RateLimitRule> rules_;
  mutable std::mutex mutex_;

  // Get rule for endpoint
  const RateLimitRule *getRuleForEndpoint(const std::string &endpoint) const;

  // Get current minute/hour window
  std::pair<int64_t, int64_t> getCurrentWindows() const;
};
