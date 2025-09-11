#ifndef CACHE_MANAGER_HPP
#define CACHE_MANAGER_HPP

// Forward declarations to avoid circular includes
#ifdef ETL_ENABLE_REDIS
class RedisCache;
#endif
class DatabaseManager;

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <atomic>

struct CacheConfig {
    bool enabled = true;
    std::chrono::seconds defaultTTL = std::chrono::seconds(300); // 5 minutes
    std::chrono::seconds userDataTTL = std::chrono::seconds(600); // 10 minutes
    std::chrono::seconds jobDataTTL = std::chrono::seconds(60); // 1 minute
    std::chrono::seconds sessionDataTTL = std::chrono::seconds(1800); // 30 minutes
    std::chrono::seconds healthCheckTTL = std::chrono::seconds(30); // 30 seconds
    size_t maxCacheSize = 10000; // Maximum number of cached items
    std::string cachePrefix = "etlplus:"; // Prefix for cache keys

    // Cache warmup configuration
    bool enableWarmup = true; // Enable/disable cache warmup
    size_t warmupBatchSize = 10; // Number of keys to fetch per batch
    size_t warmupMaxKeys = 100; // Maximum number of keys to warmup
    std::chrono::seconds warmupBatchTimeout = std::chrono::seconds(5); // Timeout per batch
    std::chrono::seconds warmupTotalTimeout = std::chrono::seconds(60); // Total warmup timeout
};

class CacheManager {
public:
    explicit CacheManager(const CacheConfig& config = CacheConfig{});
    ~CacheManager();

    // Initialize with Redis cache
#ifdef ETL_ENABLE_REDIS
    bool initialize(std::unique_ptr<RedisCache> redisCache);
#endif

    // User data caching
    bool cacheUserData(const std::string& userId, const nlohmann::json& userData);
    nlohmann::json getCachedUserData(const std::string& userId);
    bool invalidateUserData(const std::string& userId);

    // Job data caching
    bool cacheJobData(const std::string& jobId, const nlohmann::json& jobData);
    nlohmann::json getCachedJobData(const std::string& jobId);
    bool invalidateJobData(const std::string& jobId);
    bool invalidateAllJobData();

    // Session data caching
    bool cacheSessionData(const std::string& sessionId, const nlohmann::json& sessionData);
    nlohmann::json getCachedSessionData(const std::string& sessionId);
    bool invalidateSessionData(const std::string& sessionId);

    // Generic data caching with tags
    bool cacheData(const std::string& key, const nlohmann::json& data,
                   const std::vector<std::string>& tags = {},
                   std::optional<std::chrono::seconds> ttl = std::nullopt);
    nlohmann::json getCachedData(const std::string& key);
    bool invalidateData(const std::string& key);
    bool invalidateByTags(const std::vector<std::string>& tags);

    // Cache statistics
    struct CacheStats {
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t sets = 0;
        uint64_t deletes = 0;
        uint64_t errors = 0;
        double hitRate = 0.0;
    };

    CacheStats getCacheStats() const;

    // Cache management
    void clearAllCache();
    void warmupCache(DatabaseManager* dbManager);
    bool isCacheEnabled() const;
    bool isCacheHealthy() const;

private:
    CacheConfig config_;
#ifdef ETL_ENABLE_REDIS
    std::unique_ptr<RedisCache> redisCache_;
#endif
    mutable CacheStats stats_;
    mutable std::mutex statsMutex_;

    // Health check caching
    mutable std::atomic<std::chrono::steady_clock::time_point> lastHealthCheckTime_;
    mutable std::atomic<bool> lastHealthStatus_;
    mutable std::mutex healthMutex_; // For atomic operations that need synchronization

    // Private methods
    std::string makeCacheKey(const std::string& key) const;
    std::string makeUserKey(const std::string& userId) const;
    std::string makeJobKey(const std::string& jobId) const;
    std::string makeSessionKey(const std::string& sessionId) const;
    void updateStats(bool hit, bool error = false);
    std::chrono::seconds getTTLForTags(const std::vector<std::string>& tags) const;
    bool processWarmupBatch(const std::vector<std::vector<std::string>>& batch,
                           std::atomic<size_t>& totalLoaded,
                           std::atomic<size_t>& totalErrors);
};

#endif // CACHE_MANAGER_HPP
