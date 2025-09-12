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
  /**
 * @brief Default destructor.
 *
 * Performs normal destruction of the RateLimiter instance. As the class manages no RAII resources
 * that require custom teardown beyond member destructors, the default destructor is sufficient.
 */
~RateLimiter() = default;

  /**
 * @brief Deletes the copy constructor to prevent copying of RateLimiter instances.
 *
 * Making the class non-copyable avoids accidental duplication of internal state
 * (including mutexes and client tracking), ensuring a single authoritative instance.
 */
  RateLimiter(const RateLimiter &) = delete;
  /**
 * @brief Deleted copy-assignment operator to prevent copying.
 *
 * The copy-assignment operator is explicitly deleted to make RateLimiter non-copyable.
 * This ensures internal state (client counters, rules, mutex) cannot be duplicated.
 */
RateLimiter &operator=(const RateLimiter &) = delete;
  /**
 * @brief Deleted move constructor; RateLimiter instances cannot be moved.
 *
 * Prevents transfer of internal state (mutexes, client counters, rules) by disabling move semantics.
 * Use the default-constructed instance and explicit initialization methods rather than moving.
 */
RateLimiter(RateLimiter &&) = delete;
  /**
 * @brief Deleted move assignment operator.
 *
 * Disables move-assignment to ensure RateLimiter instances are neither movable
 * nor copyable; use by-value transfer of ownership is not supported.
 */
RateLimiter &operator=(RateLimiter &&) = delete;

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
