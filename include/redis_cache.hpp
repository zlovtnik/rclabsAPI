#ifndef REDIS_CACHE_HPP
#define REDIS_CACHE_HPP

#ifdef ETL_ENABLE_REDIS
#include <hiredis/hiredis.h>
#endif

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>
#include <atomic>
#include <optional>
#include <nlohmann/json.hpp>

#ifdef ETL_ENABLE_REDIS

struct RedisConfig {
    std::string host = "localhost";
    int port = 6379;
    int db = 0;
    std::string password = "";
    std::chrono::seconds connectionTimeout = std::chrono::seconds(5);
    int maxRetries = 3;
    std::chrono::milliseconds retryDelay = std::chrono::milliseconds(100);
    bool enableConnectionPool = true;
    int poolSize = 5;
};

class RedisCache {
public:
    explicit RedisCache(const RedisConfig& config);
    ~RedisCache();

    // Delete copy and move operations
    RedisCache(const RedisCache&) = delete;
    RedisCache& operator=(const RedisCache&) = delete;
    RedisCache(RedisCache&&) = delete;
    RedisCache& operator=(RedisCache&&) = delete;

    // Connection management
    bool connect();
    void disconnect();
    bool isConnected() const;
    bool ping();

    // Basic operations
    bool set(const std::string& key, const std::string& value, std::optional<std::chrono::seconds> ttl = std::nullopt);
    std::string get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    std::vector<std::string> keys(const std::string& pattern);

    // JSON operations
    bool setJson(const std::string& key, const nlohmann::json& value, std::optional<std::chrono::seconds> ttl = std::nullopt);
    nlohmann::json getJson(const std::string& key);

    // Hash operations
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    std::string hget(const std::string& key, const std::string& field);
    bool hdel(const std::string& key, const std::string& field);
    std::vector<std::string> hkeys(const std::string& key);
    std::vector<std::string> hvals(const std::string& key);

    // List operations
    bool lpush(const std::string& key, const std::string& value);
    bool rpush(const std::string& key, const std::string& value);
    std::string lpop(const std::string& key);
    std::string rpop(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int start, int end);

    // Set operations
    bool sadd(const std::string& key, const std::string& member);
    bool srem(const std::string& key, const std::string& member);
    bool sismember(const std::string& key, const std::string& member);
    std::vector<std::string> smembers(const std::string& key);

    // Cache-specific operations
    bool setWithTags(const std::string& key, const std::string& value, const std::vector<std::string>& tags, std::optional<std::chrono::seconds> ttl = std::nullopt);
    bool invalidateByTag(const std::string& tag);
    bool invalidateByTags(const std::vector<std::string>& tags);

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
        CacheMetrics(const CacheMetrics&) = default;
        CacheMetrics(CacheMetrics&&) = default;
        CacheMetrics& operator=(const CacheMetrics&) = default;
        CacheMetrics& operator=(CacheMetrics&&) = default;
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
    redisReply* executeCommand(const std::string& command);
    redisReply* executeCommandArgv(int argc, const char** argv, const size_t* argvlen);

    // Deprecated - use executeCommand(const std::string&) instead
    [[deprecated("Use executeCommand(const std::string&) for type safety")]]
    redisReply* executeCommand(const char* format, ...);
    bool reconnect();
    void updateMetrics(bool success, bool isRead = true);
    std::string generateTagKey(const std::string& tag);
};

#endif // ETL_ENABLE_REDIS

#endif // REDIS_CACHE_HPP
