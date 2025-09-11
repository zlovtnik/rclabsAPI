#ifndef CACHE_MANAGER_HPP
#define CACHE_MANAGER_HPP

#include "redis_cache.hpp"
#include "database_manager.hpp"
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

struct CacheConfig {
    bool enabled = true;
    std::chrono::seconds defaultTTL = std::chrono::seconds(300); // 5 minutes
    std::chrono::seconds userDataTTL = std::chrono::seconds(600); // 10 minutes
    std::chrono::seconds jobDataTTL = std::chrono::seconds(60); // 1 minute
    std::chrono::seconds sessionDataTTL = std::chrono::seconds(1800); // 30 minutes
    size_t maxCacheSize = 10000; // Maximum number of cached items
    std::string cachePrefix = "etlplus:"; // Prefix for cache keys
};

class CacheManager {
public:
    explicit CacheManager(const CacheConfig& config = CacheConfig{});
    ~CacheManager();

    // Initialize with Redis cache
    bool initialize(std::unique_ptr<RedisCache> redisCache);

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
                   std::chrono::seconds ttl = std::chrono::seconds(0));
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
    std::unique_ptr<RedisCache> redisCache_;
    mutable CacheStats stats_;

    // Private methods
    std::string makeCacheKey(const std::string& key) const;
    std::string makeUserKey(const std::string& userId) const;
    std::string makeJobKey(const std::string& jobId) const;
    std::string makeSessionKey(const std::string& sessionId) const;
    void updateStats(bool hit, bool error = false);
    std::chrono::seconds getTTLForTags(const std::vector<std::string>& tags) const;
};

#endif // CACHE_MANAGER_HPP
