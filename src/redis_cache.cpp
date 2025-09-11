#include "redis_cache.hpp"
#include "logger.hpp"
#include <cstdarg>
#include <algorithm>
#include <sstream>

RedisCache::RedisCache(const RedisConfig& config)
    : config_(config), context_(nullptr) {
    WS_LOG_INFO("Redis cache initialized with host=" + config_.host +
                ", port=" + std::to_string(config_.port) +
                ", db=" + std::to_string(config_.db));
}

RedisCache::~RedisCache() {
    disconnect();
}

bool RedisCache::connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }

    struct timeval timeout = {
        static_cast<time_t>(config_.connectionTimeout.count()),
        0
    };

    context_ = redisConnectWithTimeout(config_.host.c_str(), config_.port, timeout);

    if (!context_ || context_->err) {
        if (context_) {
            WS_LOG_ERROR("Redis connection failed: " + std::string(context_->errstr));
            redisFree(context_);
            context_ = nullptr;
        } else {
            WS_LOG_ERROR("Redis connection failed: can't allocate redis context");
        }
        return false;
    }

    // Authenticate if password is provided
    if (!config_.password.empty()) {
        redisReply* reply = executeCommand("AUTH %s", config_.password.c_str());
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            WS_LOG_ERROR("Redis authentication failed");
            if (reply) freeReplyObject(reply);
            disconnect();
            return false;
        }
        if (reply) freeReplyObject(reply);
    }

    // Select database
    if (config_.db != 0) {
        redisReply* reply = executeCommand("SELECT %d", config_.db);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            WS_LOG_ERROR("Redis database selection failed");
            if (reply) freeReplyObject(reply);
            disconnect();
            return false;
        }
        if (reply) freeReplyObject(reply);
    }

    WS_LOG_INFO("Redis connection established successfully");
    return true;
}

void RedisCache::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
        WS_LOG_INFO("Redis connection closed");
    }
}

bool RedisCache::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return context_ && !context_->err;
}

bool RedisCache::ping() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("PING");
    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "PONG");
    freeReplyObject(reply);
    updateMetrics(success, true);
    return success;
}

bool RedisCache::set(const std::string& key, const std::string& value, std::chrono::seconds ttl) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply;
    if (ttl.count() > 0) {
        reply = executeCommand("SETEX %s %ld %s", key.c_str(), ttl.count(), value.c_str());
    } else {
        reply = executeCommand("SET %s %s", key.c_str(), value.c_str());
    }

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
    freeReplyObject(reply);
    updateMetrics(success, false);
    if (success) sets_++;
    return success;
}

std::string RedisCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return "";

    redisReply* reply = executeCommand("GET %s", key.c_str());
    if (!reply) {
        updateMetrics(false, true);
        return "";
    }

    std::string result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = reply->str;
        updateMetrics(true, true);
    } else if (reply->type == REDIS_REPLY_NIL) {
        updateMetrics(false, true);
    } else {
        updateMetrics(false, true);
    }

    freeReplyObject(reply);
    return result;
}

bool RedisCache::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("DEL %s", key.c_str());
    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    if (success) deletes_++;
    return success;
}

bool RedisCache::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("EXISTS %s", key.c_str());
    if (!reply) return false;

    bool exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return exists;
}

std::vector<std::string> RedisCache::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return {};

    redisReply* reply = executeCommand("KEYS %s", pattern.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return {};
    }

    std::vector<std::string> result;
    for (size_t i = 0; i < reply->elements; ++i) {
        if (reply->element[i]->type == REDIS_REPLY_STRING) {
            result.push_back(reply->element[i]->str);
        }
    }

    freeReplyObject(reply);
    return result;
}

bool RedisCache::setJson(const std::string& key, const nlohmann::json& value, std::chrono::seconds ttl) {
    return set(key, value.dump(), ttl);
}

nlohmann::json RedisCache::getJson(const std::string& key) {
    std::string data = get(key);
    if (data.empty()) return nlohmann::json();

    try {
        return nlohmann::json::parse(data);
    } catch (const std::exception& e) {
        WS_LOG_ERROR("Failed to parse JSON from cache: " + std::string(e.what()));
        return nlohmann::json();
    }
}

bool RedisCache::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

std::string RedisCache::hget(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return "";

    redisReply* reply = executeCommand("HGET %s %s", key.c_str(), field.c_str());
    if (!reply) return "";

    std::string result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = reply->str;
    }

    freeReplyObject(reply);
    return result;
}

bool RedisCache::hdel(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("HDEL %s %s", key.c_str(), field.c_str());
    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return success;
}

std::vector<std::string> RedisCache::hkeys(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return {};

    redisReply* reply = executeCommand("HKEYS %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return {};
    }

    std::vector<std::string> result;
    for (size_t i = 0; i < reply->elements; ++i) {
        if (reply->element[i]->type == REDIS_REPLY_STRING) {
            result.push_back(reply->element[i]->str);
        }
    }

    freeReplyObject(reply);
    return result;
}

std::vector<std::string> RedisCache::hvals(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return {};

    redisReply* reply = executeCommand("HVALS %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return {};
    }

    std::vector<std::string> result;
    for (size_t i = 0; i < reply->elements; ++i) {
        if (reply->element[i]->type == REDIS_REPLY_STRING) {
            result.push_back(reply->element[i]->str);
        }
    }

    freeReplyObject(reply);
    return result;
}

bool RedisCache::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("LPUSH %s %s", key.c_str(), value.c_str());
    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

bool RedisCache::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("RPUSH %s %s", key.c_str(), value.c_str());
    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

std::string RedisCache::lpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return "";

    redisReply* reply = executeCommand("LPOP %s", key.c_str());
    if (!reply) return "";

    std::string result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = reply->str;
    }

    freeReplyObject(reply);
    return result;
}

std::string RedisCache::rpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return "";

    redisReply* reply = executeCommand("RPOP %s", key.c_str());
    if (!reply) return "";

    std::string result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = reply->str;
    }

    freeReplyObject(reply);
    return result;
}

std::vector<std::string> RedisCache::lrange(const std::string& key, int start, int end) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return {};

    redisReply* reply = executeCommand("LRANGE %s %d %d", key.c_str(), start, end);
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return {};
    }

    std::vector<std::string> result;
    for (size_t i = 0; i < reply->elements; ++i) {
        if (reply->element[i]->type == REDIS_REPLY_STRING) {
            result.push_back(reply->element[i]->str);
        }
    }

    freeReplyObject(reply);
    return result;
}

bool RedisCache::sadd(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("SADD %s %s", key.c_str(), member.c_str());
    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

bool RedisCache::srem(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("SREM %s %s", key.c_str(), member.c_str());
    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return success;
}

bool RedisCache::sismember(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return false;

    redisReply* reply = executeCommand("SISMEMBER %s %s", key.c_str(), member.c_str());
    if (!reply) return false;

    bool isMember = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return isMember;
}

std::vector<std::string> RedisCache::smembers(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return {};

    redisReply* reply = executeCommand("SMEMBERS %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return {};
    }

    std::vector<std::string> result;
    for (size_t i = 0; i < reply->elements; ++i) {
        if (reply->element[i]->type == REDIS_REPLY_STRING) {
            result.push_back(reply->element[i]->str);
        }
    }

    freeReplyObject(reply);
    return result;
}

bool RedisCache::setWithTags(const std::string& key, const std::string& value, const std::vector<std::string>& tags, std::chrono::seconds ttl) {
    if (!set(key, value, ttl)) return false;

    // Add key to tag sets
    for (const auto& tag : tags) {
        std::string tagKey = generateTagKey(tag);
        if (!sadd(tagKey, key)) {
            WS_LOG_WARN("Failed to add key to tag set: " + tag);
        }
    }

    return true;
}

bool RedisCache::invalidateByTag(const std::string& tag) {
    std::string tagKey = generateTagKey(tag);
    std::vector<std::string> keys = smembers(tagKey);

    bool success = true;
    for (const auto& key : keys) {
        if (!del(key)) {
            success = false;
        }
    }

    // Remove the tag set itself
    srem(tagKey, "dummy"); // This will remove the set if it's empty

    return success;
}

bool RedisCache::invalidateByTags(const std::vector<std::string>& tags) {
    bool success = true;
    for (const auto& tag : tags) {
        if (!invalidateByTag(tag)) {
            success = false;
        }
    }
    return success;
}

RedisCache::CacheMetrics RedisCache::getMetrics() const {
    CacheMetrics result;
    result.hits = hits_.load();
    result.misses = misses_.load();
    result.sets = sets_.load();
    result.deletes = deletes_.load();
    result.errors = errors_.load();
    result.lastAccess = lastAccess_;
    return result;
}

void RedisCache::flushAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return;

    redisReply* reply = executeCommand("FLUSHDB");
    if (reply) freeReplyObject(reply);
    WS_LOG_INFO("Redis cache flushed");
}

std::string RedisCache::info() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isConnected()) return "";

    redisReply* reply = executeCommand("INFO");
    if (!reply || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return "";
    }

    std::string result = reply->str;
    freeReplyObject(reply);
    return result;
}

redisReply* RedisCache::executeCommand(const char* format, ...) {
    if (!context_ || context_->err) {
        return nullptr;
    }

    va_list args;
    va_start(args, format);
    redisReply* reply = static_cast<redisReply*>(redisvCommand(context_, format, args));
    va_end(args);

    if (!reply) {
        WS_LOG_ERROR("Redis command failed: " + std::string(context_->errstr));
        return nullptr;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        WS_LOG_ERROR("Redis command error: " + std::string(reply->str));
        freeReplyObject(reply);
        return nullptr;
    }

    return reply;
}

redisReply* RedisCache::executeCommandArgv(int argc, const char** argv, const size_t* argvlen) {
    if (!context_ || context_->err) {
        return nullptr;
    }

    redisReply* reply = static_cast<redisReply*>(redisCommandArgv(context_, argc, argv, argvlen));

    if (!reply) {
        WS_LOG_ERROR("Redis command failed: " + std::string(context_->errstr));
        return nullptr;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        WS_LOG_ERROR("Redis command error: " + std::string(reply->str));
        freeReplyObject(reply);
        return nullptr;
    }

    return reply;
}

bool RedisCache::reconnect() {
    disconnect();
    return connect();
}

void RedisCache::updateMetrics(bool success, bool isRead) {
    if (success) {
        if (isRead) {
            hits_++;
        }
    } else {
        errors_++;
        if (isRead) {
            misses_++;
        }
    }
    lastAccess_ = std::chrono::steady_clock::now();
}

std::string RedisCache::generateTagKey(const std::string& tag) {
    return "tag:" + tag;
}
