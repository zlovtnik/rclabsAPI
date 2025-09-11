#include "cache_manager.hpp"
#include "logger.hpp"
#include <algorithm>

CacheManager::CacheManager(const CacheConfig& config)
    : config_(config) {
    WS_LOG_INFO("Cache manager initialized with TTL=" + std::to_string(config_.defaultTTL.count()) + "s");
}

CacheManager::~CacheManager() {
    if (redisCache_) {
        redisCache_->disconnect();
    }
}

bool CacheManager::initialize(std::unique_ptr<RedisCache> redisCache) {
    redisCache_ = std::move(redisCache);

    if (!redisCache_->connect()) {
        WS_LOG_ERROR("Failed to connect to Redis cache");
        return false;
    }

    if (!redisCache_->ping()) {
        WS_LOG_ERROR("Redis cache ping failed");
        return false;
    }

    WS_LOG_INFO("Cache manager initialized successfully");
    return true;
}

bool CacheManager::cacheUserData(const std::string& userId, const nlohmann::json& userData) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeUserKey(userId);
    std::vector<std::string> tags = {"user", "user:" + userId};

    bool success = redisCache_->setJson(key, userData, config_.userDataTTL);
    if (success) {
        redisCache_->setWithTags(key, userData.dump(), tags, config_.userDataTTL);
        updateStats(false, false);
        stats_.sets++;
    } else {
        updateStats(false, true);
    }

    return success;
}

nlohmann::json CacheManager::getCachedUserData(const std::string& userId) {
    if (!isCacheEnabled() || !redisCache_) return nlohmann::json();

    std::string key = makeUserKey(userId);
    nlohmann::json data = redisCache_->getJson(key);

    if (!data.empty()) {
        updateStats(true, false);
        stats_.hits++;
    } else {
        updateStats(false, false);
        stats_.misses++;
    }

    return data;
}

bool CacheManager::invalidateUserData(const std::string& userId) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeUserKey(userId);
    std::vector<std::string> tags = {"user", "user:" + userId};

    bool success = redisCache_->invalidateByTags(tags);
    if (success) {
        stats_.deletes++;
    }

    return success;
}

bool CacheManager::cacheJobData(const std::string& jobId, const nlohmann::json& jobData) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeJobKey(jobId);
    std::vector<std::string> tags = {"job", "job:" + jobId};

    bool success = redisCache_->setJson(key, jobData, config_.jobDataTTL);
    if (success) {
        redisCache_->setWithTags(key, jobData.dump(), tags, config_.jobDataTTL);
        updateStats(false, false);
        stats_.sets++;
    } else {
        updateStats(false, true);
    }

    return success;
}

nlohmann::json CacheManager::getCachedJobData(const std::string& jobId) {
    if (!isCacheEnabled() || !redisCache_) return nlohmann::json();

    std::string key = makeJobKey(jobId);
    nlohmann::json data = redisCache_->getJson(key);

    if (!data.empty()) {
        updateStats(true, false);
        stats_.hits++;
    } else {
        updateStats(false, false);
        stats_.misses++;
    }

    return data;
}

bool CacheManager::invalidateJobData(const std::string& jobId) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeJobKey(jobId);
    std::vector<std::string> tags = {"job", "job:" + jobId};

    bool success = redisCache_->invalidateByTags(tags);
    if (success) {
        stats_.deletes++;
    }

    return success;
}

bool CacheManager::invalidateAllJobData() {
    if (!isCacheEnabled() || !redisCache_) return false;

    return redisCache_->invalidateByTag("job");
}

bool CacheManager::cacheSessionData(const std::string& sessionId, const nlohmann::json& sessionData) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeSessionKey(sessionId);
    std::vector<std::string> tags = {"session", "session:" + sessionId};

    bool success = redisCache_->setJson(key, sessionData, config_.sessionDataTTL);
    if (success) {
        redisCache_->setWithTags(key, sessionData.dump(), tags, config_.sessionDataTTL);
        updateStats(false, false);
        stats_.sets++;
    } else {
        updateStats(false, true);
    }

    return success;
}

nlohmann::json CacheManager::getCachedSessionData(const std::string& sessionId) {
    if (!isCacheEnabled() || !redisCache_) return nlohmann::json();

    std::string key = makeSessionKey(sessionId);
    nlohmann::json data = redisCache_->getJson(key);

    if (!data.empty()) {
        updateStats(true, false);
        stats_.hits++;
    } else {
        updateStats(false, false);
        stats_.misses++;
    }

    return data;
}

bool CacheManager::invalidateSessionData(const std::string& sessionId) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeSessionKey(sessionId);
    std::vector<std::string> tags = {"session", "session:" + sessionId};

    bool success = redisCache_->invalidateByTags(tags);
    if (success) {
        stats_.deletes++;
    }

    return success;
}

bool CacheManager::cacheData(const std::string& key, const nlohmann::json& data,
                             const std::vector<std::string>& tags,
                             std::chrono::seconds ttl) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string cacheKey = makeCacheKey(key);
    std::chrono::seconds actualTTL = (ttl.count() > 0) ? ttl : getTTLForTags(tags);

    bool success = redisCache_->setJson(cacheKey, data, actualTTL);
    if (success && !tags.empty()) {
        redisCache_->setWithTags(cacheKey, data.dump(), tags, actualTTL);
        updateStats(false, false);
        stats_.sets++;
    } else if (success) {
        updateStats(false, false);
        stats_.sets++;
    } else {
        updateStats(false, true);
    }

    return success;
}

nlohmann::json CacheManager::getCachedData(const std::string& key) {
    if (!isCacheEnabled() || !redisCache_) return nlohmann::json();

    std::string cacheKey = makeCacheKey(key);
    nlohmann::json data = redisCache_->getJson(cacheKey);

    if (!data.empty()) {
        updateStats(true, false);
        stats_.hits++;
    } else {
        updateStats(false, false);
        stats_.misses++;
    }

    return data;
}

bool CacheManager::invalidateData(const std::string& key) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string cacheKey = makeCacheKey(key);
    bool success = redisCache_->del(cacheKey);
    if (success) {
        stats_.deletes++;
    }

    return success;
}

bool CacheManager::invalidateByTags(const std::vector<std::string>& tags) {
    if (!isCacheEnabled() || !redisCache_) return false;

    return redisCache_->invalidateByTags(tags);
}

CacheManager::CacheStats CacheManager::getCacheStats() const {
    CacheStats stats = stats_;
    if (redisCache_) {
        auto redisMetrics = redisCache_->getMetrics();
        stats.hits = redisMetrics.hits;
        stats.misses = redisMetrics.misses;
        stats.sets = redisMetrics.sets;
        stats.deletes = redisMetrics.deletes;
        stats.errors = redisMetrics.errors;
    }

    uint64_t totalRequests = stats.hits + stats.misses;
    stats.hitRate = (totalRequests > 0) ? (static_cast<double>(stats.hits) / totalRequests) * 100.0 : 0.0;

    return stats;
}

void CacheManager::clearAllCache() {
    if (!isCacheEnabled() || !redisCache_) return;

    redisCache_->flushAll();
    WS_LOG_INFO("All cache data cleared");
}

void CacheManager::warmupCache(DatabaseManager* dbManager) {
    if (!isCacheEnabled() || !redisCache_ || !dbManager) return;

    WS_LOG_INFO("Starting cache warmup...");

    try {
        // This is a placeholder for cache warmup logic
        // In a real implementation, you would query the database
        // for frequently accessed data and preload it into cache

        WS_LOG_INFO("Cache warmup completed");
    } catch (const std::exception& e) {
        WS_LOG_ERROR("Cache warmup failed: " + std::string(e.what()));
    }
}

bool CacheManager::isCacheEnabled() const {
    return config_.enabled && redisCache_ != nullptr;
}

bool CacheManager::isCacheHealthy() const {
    return isCacheEnabled() && redisCache_->isConnected() && redisCache_->ping();
}

std::string CacheManager::makeCacheKey(const std::string& key) const {
    return config_.cachePrefix + key;
}

std::string CacheManager::makeUserKey(const std::string& userId) const {
    return config_.cachePrefix + "user:" + userId;
}

std::string CacheManager::makeJobKey(const std::string& jobId) const {
    return config_.cachePrefix + "job:" + jobId;
}

std::string CacheManager::makeSessionKey(const std::string& sessionId) const {
    return config_.cachePrefix + "session:" + sessionId;
}

void CacheManager::updateStats(bool hit, bool error) {
    if (error) {
        stats_.errors++;
    }
}

std::chrono::seconds CacheManager::getTTLForTags(const std::vector<std::string>& tags) const {
    // Determine TTL based on tags
    for (const auto& tag : tags) {
        if (tag.find("user") != std::string::npos) {
            return config_.userDataTTL;
        } else if (tag.find("job") != std::string::npos) {
            return config_.jobDataTTL;
        } else if (tag.find("session") != std::string::npos) {
            return config_.sessionDataTTL;
        }
    }

    return config_.defaultTTL;
}
