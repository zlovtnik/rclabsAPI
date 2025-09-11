#include "cache_manager.hpp"
#ifdef ETL_ENABLE_REDIS
#include "redis_cache.hpp"
#endif
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

    // Validate warmup configuration to prevent resource exhaustion and invalid states
    if (config_.enableWarmup) {
        if (config_.warmupBatchSize == 0) {
            throw std::invalid_argument("CacheManager: warmupBatchSize must be greater than 0");
        }

        if (config_.warmupMaxKeys < config_.warmupBatchSize) {
            throw std::invalid_argument("CacheManager: warmupMaxKeys (" +
                                      std::to_string(config_.warmupMaxKeys) +
                                      ") must be >= warmupBatchSize (" +
                                      std::to_string(config_.warmupBatchSize) + ")");
        }

        if (config_.warmupBatchTimeout <= std::chrono::seconds(0)) {
            throw std::invalid_argument("CacheManager: warmupBatchTimeout must be positive");
        }

        if (config_.warmupTotalTimeout <= std::chrono::seconds(0)) {
            throw std::invalid_argument("CacheManager: warmupTotalTimeout must be positive");
        }

        if (config_.warmupTotalTimeout < config_.warmupBatchTimeout) {
            throw std::invalid_argument("CacheManager: warmupTotalTimeout (" +
                                      std::to_string(config_.warmupTotalTimeout.count()) + "s) " +
                                      "must be >= warmupBatchTimeout (" +
                                      std::to_string(config_.warmupBatchTimeout.count()) + "s)");
        }

        // Additional bounds checking for resource protection
        if (config_.warmupBatchSize > 1000) {
            throw std::invalid_argument("CacheManager: warmupBatchSize (" +
                                      std::to_string(config_.warmupBatchSize) +
                                      ") exceeds maximum allowed (1000)");
        }

        if (config_.warmupMaxKeys > 100000) {
            throw std::invalid_argument("CacheManager: warmupMaxKeys (" +
                                      std::to_string(config_.warmupMaxKeys) +
                                      ") exceeds maximum allowed (100000)");
        }
    }

    WS_LOG_INFO("Cache manager initialized with TTL=" + std::to_string(config_.defaultTTL.count()) + "s, health check TTL=" + std::to_string(config_.healthCheckTTL.count()) + "s");
}

CacheManager::~CacheManager() {
#ifdef ETL_ENABLE_REDIS
    if (redisCache_) {
        redisCache_->disconnect();
    }
#endif
}

#ifdef ETL_ENABLE_REDIS
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
#endif

bool CacheManager::cacheUserData(const std::string& userId, const nlohmann::json& userData) {
#ifdef ETL_ENABLE_REDIS
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
#else
    return false;
#endif
}

nlohmann::json CacheManager::getCachedUserData(const std::string& userId) {
#ifdef ETL_ENABLE_REDIS
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
#else
    return nlohmann::json();
#endif
}

bool CacheManager::invalidateUserData(const std::string& userId) {
#ifdef ETL_ENABLE_REDIS
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeUserKey(userId);
    std::vector<std::string> tags = {"user", "user:" + userId};

    bool success = redisCache_->invalidateByTags(tags);
    if (success) {
        stats_.deletes++;
    }

    return success;
#else
    return false;
#endif
}

bool CacheManager::cacheJobData(const std::string& jobId, const nlohmann::json& jobData) {
#ifdef ETL_ENABLE_REDIS
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
#else
    return false;
#endif
}

nlohmann::json CacheManager::getCachedJobData(const std::string& jobId) {
#ifdef ETL_ENABLE_REDIS
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
#else
    return nlohmann::json();
#endif
}

bool CacheManager::invalidateJobData(const std::string& jobId) {
#ifdef ETL_ENABLE_REDIS
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeJobKey(jobId);
    std::vector<std::string> tags = {"job", "job:" + jobId};

    bool success = redisCache_->invalidateByTags(tags);
    if (success) {
        stats_.deletes++;
    }

    return success;
#else
    return false;
#endif
}

bool CacheManager::invalidateAllJobData() {
#ifdef ETL_ENABLE_REDIS
    if (!isCacheEnabled() || !redisCache_) return false;

    return redisCache_->invalidateByTag("job");
#else
    return false;
#endif
}

bool CacheManager::cacheSessionData(const std::string& sessionId, const nlohmann::json& sessionData) {
#ifdef ETL_ENABLE_REDIS
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
#else
    return false;
#endif
}

nlohmann::json CacheManager::getCachedSessionData(const std::string& sessionId) {
#ifdef ETL_ENABLE_REDIS
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
#else
    return nlohmann::json();
#endif
}

bool CacheManager::invalidateSessionData(const std::string& sessionId) {
#ifdef ETL_ENABLE_REDIS
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string key = makeSessionKey(sessionId);
    std::vector<std::string> tags = {"session", "session:" + sessionId};

    bool success = redisCache_->invalidateByTags(tags);
    if (success) {
        stats_.deletes++;
    }

    return success;
#else
    return false;
#endif
}

bool CacheManager::cacheData(const std::string& key, const nlohmann::json& data,
                             const std::vector<std::string>& tags,
                             std::optional<std::chrono::seconds> ttl) {
#ifdef ETL_ENABLE_REDIS
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
#else
    return false;
#endif
}

nlohmann::json CacheManager::getCachedData(const std::string& key) {
#ifdef ETL_ENABLE_REDIS
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
#else
    return nlohmann::json();
#endif
}

bool CacheManager::invalidateData(const std::string& key) {
#ifdef ETL_ENABLE_REDIS
    if (!isCacheEnabled() || !redisCache_) return false;

    std::string cacheKey = makeCacheKey(key);
    bool success = redisCache_->del(cacheKey);
    if (success) {
        stats_.deletes++;
    }

    return success;
#else
    return false;
#endif
}

bool CacheManager::invalidateByTags(const std::vector<std::string>& tags) {
#ifdef ETL_ENABLE_REDIS
    if (!isCacheEnabled() || !redisCache_) return false;

    return redisCache_->invalidateByTags(tags);
#else
    return false;
#endif
}

CacheManager::CacheStats CacheManager::getCacheStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    CacheStats stats = stats_;
#ifdef ETL_ENABLE_REDIS
    if (redisCache_) {
        auto redisMetrics = redisCache_->getMetrics();
        stats.hits = redisMetrics.hits;
        stats.misses = redisMetrics.misses;
        stats.sets = redisMetrics.sets;
        stats.deletes = redisMetrics.deletes;
        stats.errors = redisMetrics.errors;
    }
#endif

    uint64_t totalRequests = stats.hits + stats.misses;
    stats.hitRate = (totalRequests > 0) ? (static_cast<double>(stats.hits) / totalRequests) * 100.0 : 0.0;

    return stats;
}

void CacheManager::clearAllCache() {
#ifdef ETL_ENABLE_REDIS
    if (!isCacheEnabled() || !redisCache_) return;

    redisCache_->flushAll();
    WS_LOG_INFO("All cache data cleared");
#endif
}

void CacheManager::warmupCache(DatabaseManager* dbManager) {
#ifdef ETL_ENABLE_REDIS
    if (!isCacheEnabled() || !redisCache_ || !dbManager || !config_.enableWarmup) {
        return;
    }

    WS_LOG_INFO("Starting cache warmup with batch size: " + std::to_string(config_.warmupBatchSize) +
                ", max keys: " + std::to_string(config_.warmupMaxKeys));

    auto startTime = std::chrono::steady_clock::now();
    std::atomic<size_t> totalLoaded = 0;
    std::atomic<size_t> totalErrors = 0;

    // Strictly validate and sanitize warmupMaxKeys to prevent SQL injection
    // Clamp to safe range and ensure it's a valid positive integer
    size_t safeMaxKeys = 1000; // Default safe value
    if (config_.warmupMaxKeys > 0 && config_.warmupMaxKeys <= 50000) {
        // Only accept values in reasonable range (1-50000)
        safeMaxKeys = static_cast<size_t>(config_.warmupMaxKeys);
    } else if (config_.warmupMaxKeys > 50000) {
        WS_LOG_WARN("warmupMaxKeys value " + std::to_string(config_.warmupMaxKeys) +
                   " exceeds maximum allowed (50000), clamping to 50000");
        safeMaxKeys = 50000;
    } else {
        WS_LOG_WARN("Invalid warmupMaxKeys value " + std::to_string(config_.warmupMaxKeys) +
                   ", using default of 1000");
    }

    try {
        // Build SQL query using validated integer value
        // Use stringstream for safe integer formatting
        std::stringstream queryStream;
        queryStream << "SELECT DISTINCT key_name, data_type FROM cache_access_log "
                   << "ORDER BY access_count DESC LIMIT " << safeMaxKeys;
        std::string query = queryStream.str();

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
#endif
}

bool CacheManager::isCacheEnabled() const {
#ifdef ETL_ENABLE_REDIS
    return config_.enabled && redisCache_ != nullptr;
#else
    return false;
#endif
}

bool CacheManager::isCacheHealthy() const {
#ifdef ETL_ENABLE_REDIS
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
#else
    return false;
#endif
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
