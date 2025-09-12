#ifndef REDIS_CACHE_HPP
#define REDIS_CACHE_HPP

#ifdef ETL_ENABLE_REDIS
#include <hiredis/hiredis.h>
#endif

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#ifdef ETL_ENABLE_REDIS

struct RedisConfig {
  std::string host = "localhost";
  int port = 6379;
  int db = 0;
  std::string password = "";
  std::string clientName = "etl-backend"; // For Redis client tracing
  std::chrono::seconds connectionTimeout = std::chrono::seconds(5);
  int maxRetries = 3;
  std::chrono::milliseconds retryDelay = std::chrono::milliseconds(100);
  // Connection pooling is not yet supported
};

class RedisCache {
public:
  explicit RedisCache(const RedisConfig &config);
  ~RedisCache();

  // Thread-safety: All public methods are thread-safe and can be called
  // concurrently Hiredis contexts are not thread-safe internally, so all
  // operations are protected by mutex_

  /**
   * @brief Deleted copy constructor to make RedisCache non-copyable.
   *
   * RedisCache manages exclusive resources (connection context and
   * synchronization primitives) and must not be copied.
   */
  RedisCache(const RedisCache &) = delete;
  RedisCache &operator=(const RedisCache &) = delete;
  RedisCache(RedisCache &&) = delete;
  RedisCache &operator=(RedisCache &&) = delete;

  // Connection management
  bool connect();
  void disconnect();
  bool isConnected() const;
  bool ping();

  // Basic operations
  bool set(const std::string &key, const std::string &value,
           std::optional<std::chrono::seconds> ttl = std::nullopt);
  std::optional<std::string> get(const std::string &key);
  bool del(const std::string &key);
  bool exists(const std::string &key);
  std::vector<std::string> keys(
      const std::string &pattern); // Uses SCAN internally for production safety

  // JSON operations
  bool setJson(const std::string &key, const nlohmann::json &value,
               std::optional<std::chrono::seconds> ttl = std::nullopt);
  std::optional<nlohmann::json> getJson(const std::string &key);

  // Hash operations
  bool hset(const std::string &key, const std::string &field,
            const std::string &value);
  std::optional<std::string> hget(const std::string &key,
                                  const std::string &field);
  bool hdel(const std::string &key, const std::string &field);
  std::vector<std::string> hkeys(const std::string &key);
  std::vector<std::string> hvals(const std::string &key);

  // List operations
  bool lpush(const std::string &key, const std::string &value);
  bool rpush(const std::string &key, const std::string &value);
  std::optional<std::string> lpop(const std::string &key);
  std::optional<std::string> rpop(const std::string &key);
  std::vector<std::string>
  lrange(const std::string &key, int start,
         int end); // Use batched operations for large lists

  // Set operations
  bool sadd(const std::string &key, const std::string &member);
  bool srem(const std::string &key, const std::string &member);
  bool sismember(const std::string &key, const std::string &member);
  std::vector<std::string>
  smembers(const std::string &key); // Use batched operations for large sets

  // Cache-specific operations
  bool setWithTags(const std::string &key, const std::string &value,
                   const std::vector<std::string> &tags,
                   std::optional<std::chrono::seconds> ttl = std::nullopt);
  bool invalidateByTag(const std::string &tag);
  bool invalidateByTags(const std::vector<std::string> &tags);

  // Metrics
  struct CacheMetrics {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t sets = 0;
    uint64_t deletes = 0;
    uint64_t errors = 0;
    std::chrono::steady_clock::time_point lastAccess;

    // Defaulted operations for efficiency and compatibility
    CacheMetrics() = default;
    CacheMetrics(const CacheMetrics &) = default;
    CacheMetrics(CacheMetrics &&) = default;
    CacheMetrics &operator=(const CacheMetrics &) = default;
    CacheMetrics &operator=(CacheMetrics &&) = default;
  };

  CacheMetrics getMetrics() const;

  // Utility
  void flushAll();
  std::string info();

private:
  RedisConfig config_;
  std::unique_ptr<redisContext, decltype(&redisFree)> context_;
  mutable std::mutex mutex_;
  std::atomic<uint64_t> hits_{0};
  std::atomic<uint64_t> misses_{0};
  std::atomic<uint64_t> sets_{0};
  std::atomic<uint64_t> deletes_{0};
  std::atomic<uint64_t> errors_{0};
  std::chrono::steady_clock::time_point lastAccess_;

  // Private methods
  // Safe command execution methods
  redisReply *executeCommand(const std::string &command);
  redisReply *executeCommandArgv(int argc, const char **argv,
                                 const size_t *argvlen);

  // Deprecated - use executeCommand(const std::string&) instead
  [[deprecated("Use executeCommand(const std::string&) for type safety")]]
  redisReply *executeCommand(const char *format, ...);
  bool reconnect();
  void updateMetrics(bool success, bool isRead = true);
  std::string generateTagKey(const std::string &tag);
};

#endif // ETL_ENABLE_REDIS

#endif // REDIS_CACHE_HPP
