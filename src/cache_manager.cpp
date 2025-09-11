#include "cache_manager.hpp"
#include "redis_cache.hpp"
#include "database_manager.hpp"
#include "logger.hpp"
#include <algorithm>
#include <atomic>
#include <future>
#include <thread>
#include <optional>

CacheManager::CacheManager(const CacheConfig& config)
    : config_(config),
      lastHealthCheckTime_(std::chrono::steady_clock::time_point::min()),
      lastHealthStatus_(false) {
    WS_LOG_INFO("Cache manager initialized with TTL=" + std::to_string(config_.defaultTTL.count()) + "s, health check TTL=" + std::to_string(config_.healthCheckTTL.count()) + "s");
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

    bool success = redisCache_->setWithTags(key, userData.dump(), tags, config_.userDataTTL);
    if (success) {
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

    bool success = redisCache_->setWithTags(key, jobData.dump(), tags, config_.jobDataTTL);
    if (success) {
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

    bool success = redisCache_->setWithTags(key, sessionData.dump(), tags, config_.sessionDataTTL);
    if (success) {
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
                             std::optional<std::chrono::seconds> ttl) {
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string cacheKey = makeCacheKey(key);
    std::chrono::seconds actualTTL = ttl.has_value() ? ttl.value() : getTTLForTags(tags);

    bool success = false;
    if (!tags.empty()) {
        // Use setWithTags when tags are present
        success = redisCache_->setWithTags(cacheKey, data.dump(), tags, actualTTL);
    } else {
        // Use setJson when no tags are needed
        success = redisCache_->setJson(cacheKey, data, actualTTL);
    }
    
    if (success) {
        updateStats(true, false);  // hit=true for successful set
        stats_.sets++;
    } else {
        updateStats(false, true);  // error=true for failed set
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
    std::lock_guard<std::mutex> lock(statsMutex_);
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
    if (!isCacheEnabled() || !redisCache_ || !dbManager || !config_.enableWarmup) {
        return;
    }

    WS_LOG_INFO("Starting cache warmup with batch size: " + std::to_string(config_.warmupBatchSize) +
                ", max keys: " + std::to_string(config_.warmupMaxKeys));

    auto startTime = std::chrono::steady_clock::now();
    std::atomic<size_t> totalLoaded = 0;
    std::atomic<size_t> totalErrors = 0;

    // Validate and clamp warmupMaxKeys to prevent overflow or negative values
    size_t validatedMaxKeys = config_.warmupMaxKeys;
    if (validatedMaxKeys == 0 || validatedMaxKeys > 10000) {  // Reasonable upper limit
        validatedMaxKeys = 1000;  // Safe default
    }

    try {
        // Query database for frequently accessed keys
        // This is a simplified query - in a real system, you'd have access logs or usage statistics
        std::string query = "SELECT DISTINCT key_name, data_type FROM cache_access_log "
                           "ORDER BY access_count DESC LIMIT " + std::to_string(validatedMaxKeys);

        auto results = dbManager->selectQuery(query);

        if (results.empty()) {
            WS_LOG_INFO("No frequently accessed keys found for warmup");
            return;
        }

        WS_LOG_INFO("Found " + std::to_string(results.size()) + " keys to warmup");

        // Process keys in batches
        std::vector<std::future<bool>> batchFutures;
        auto batchStart = results.begin();

        while (batchStart != results.end()) {
            auto batchEnd = std::min(batchStart + config_.warmupBatchSize, results.end());
            std::vector<std::vector<std::string>> batch(batchStart, batchEnd);

            // Launch async batch processing
            batchFutures.push_back(std::async(std::launch::async, [this, batch, &totalLoaded, &totalErrors]() {
                return processWarmupBatch(batch, totalLoaded, totalErrors);
            }));

            batchStart = batchEnd;

            // Check total timeout
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime);
            if (elapsed >= config_.warmupTotalTimeout) {
                WS_LOG_WARN("Cache warmup timed out after " + std::to_string(elapsed.count()) + " seconds");
                break;
            }
        }

        // Wait for all batches to complete
        for (auto& future : batchFutures) {
            try {
                future.wait_for(config_.warmupBatchTimeout);
            } catch (const std::exception& e) {
                WS_LOG_ERROR("Batch processing error: " + std::string(e.what()));
                totalErrors++;
            }
        }

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        WS_LOG_INFO("Cache warmup completed: loaded=" + std::to_string(totalLoaded.load()) +
                    ", errors=" + std::to_string(totalErrors.load()) +
                    ", duration=" + std::to_string(duration.count()) + "ms");

    } catch (const std::exception& e) {
        WS_LOG_ERROR("Cache warmup failed: " + std::string(e.what()));
    }
}

bool CacheManager::isCacheEnabled() const {
    return config_.enabled && redisCache_ != nullptr;
}

bool CacheManager::isCacheHealthy() const {
    // Quick preconditions - these are fast checks
    if (!isCacheEnabled() || !redisCache_->isConnected()) {
        return false;
    }

    // Check if we have a valid cached health status
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastCheck = now - lastHealthCheckTime_.load(std::memory_order_acquire);

    if (timeSinceLastCheck < config_.healthCheckTTL) {
        // Cached status is still valid
        return lastHealthStatus_.load(std::memory_order_acquire);
    }

    // Cached status is stale, perform health check
    std::lock_guard<std::mutex> lock(healthMutex_);

    // Double-check after acquiring lock (another thread might have updated it)
    timeSinceLastCheck = now - lastHealthCheckTime_.load(std::memory_order_acquire);
    if (timeSinceLastCheck < config_.healthCheckTTL) {
        return lastHealthStatus_.load(std::memory_order_acquire);
    }

    // Perform the expensive ping operation
    bool currentHealthStatus = redisCache_->ping();

    // Update cached values
    lastHealthCheckTime_.store(now, std::memory_order_release);
    lastHealthStatus_.store(currentHealthStatus, std::memory_order_release);

    return currentHealthStatus;
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
    std::lock_guard<std::mutex> lock(statsMutex_);
    // Note: hits and misses are updated by calling methods, not here
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

bool CacheManager::processWarmupBatch(const std::vector<std::vector<std::string>>& batch,
                                     std::atomic<size_t>& totalLoaded,
                                     std::atomic<size_t>& totalErrors) {
    bool batchSuccess = true;

    for (const auto& row : batch) {
        if (row.size() < 2) {
            WS_LOG_WARN("Invalid row format in warmup batch, expected at least 2 columns");
            totalErrors++;
            continue;
        }

        try {
            std::string keyName = row[0];
            std::string dataType = row[1];

            // Create a cache key and fetch data from database
            std::string cacheKey = makeCacheKey(keyName);

            // For this implementation, we'll create a simple JSON object
            // In a real system, you'd fetch the actual data based on dataType
            nlohmann::json data;
            data["key"] = keyName;
            data["type"] = dataType;
            data["warmup_timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

            // Determine TTL based on data type
            std::chrono::seconds ttl = config_.defaultTTL;
            std::vector<std::string> tags = {dataType};

            if (dataType == "user") {
                ttl = config_.userDataTTL;
            } else if (dataType == "job") {
                ttl = config_.jobDataTTL;
            } else if (dataType == "session") {
                ttl = config_.sessionDataTTL;
            }

            // Cache the data
            if (cacheData(keyName, data, tags, ttl)) {
                totalLoaded++;
            } else {
                WS_LOG_WARN("Failed to cache key: " + keyName);
                totalErrors++;
                batchSuccess = false;
            }

        } catch (const std::exception& e) {
            WS_LOG_ERROR("Error processing warmup key '" + row[0] + "': " + std::string(e.what()));
            totalErrors++;
            batchSuccess = false;
        }
    }

    return batchSuccess;
}
