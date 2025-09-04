#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include "transparent_string_hash.hpp"

namespace etl {

// String handling type aliases for performance and consistency
using StringMap = std::unordered_map<std::string, std::string, 
                                   TransparentStringHash, std::equal_to<>>;
using StringSet = std::unordered_set<std::string, 
                                   TransparentStringHash, std::equal_to<>>;

// Strong types for IDs to prevent mixing different ID types
class JobId {
public:
    explicit JobId(std::string id) : value_(std::move(id)) {
        if (value_.empty()) {
            throw std::invalid_argument("JobId cannot be empty");
        }
    }
    
    const std::string& value() const noexcept { return value_; }
    
    bool operator==(const JobId& other) const noexcept {
        return value_ == other.value_;
    }
    
    bool operator!=(const JobId& other) const noexcept {
        return !(*this == other);
    }
    
    bool operator<(const JobId& other) const noexcept {
        return value_ < other.value_;
    }
    
private:
    std::string value_;
};

class ConnectionId {
public:
    explicit ConnectionId(std::string id) : value_(std::move(id)) {
        if (value_.empty()) {
            throw std::invalid_argument("ConnectionId cannot be empty");
        }
    }
    
    const std::string& value() const noexcept { return value_; }
    
    bool operator==(const ConnectionId& other) const noexcept {
        return value_ == other.value_;
    }
    
    bool operator!=(const ConnectionId& other) const noexcept {
        return !(*this == other);
    }
    
    bool operator<(const ConnectionId& other) const noexcept {
        return value_ < other.value_;
    }
    
private:
    std::string value_;
};

class UserId {
public:
    explicit UserId(std::string id) : value_(std::move(id)) {
        if (value_.empty()) {
            throw std::invalid_argument("UserId cannot be empty");
        }
    }
    
    const std::string& value() const noexcept { return value_; }
    
    bool operator==(const UserId& other) const noexcept {
        return value_ == other.value_;
    }
    
    bool operator!=(const UserId& other) const noexcept {
        return !(*this == other);
    }
    
    bool operator<(const UserId& other) const noexcept {
        return value_ < other.value_;
    }
    
private:
    std::string value_;
};

// Type-safe ID generation utilities
class IdGenerator {
public:
    static JobId generateJobId();
    static ConnectionId generateConnectionId();
    static UserId generateUserId();
    
private:
    static std::string generateUuid();
};

} // namespace etl

// Hash specializations for strong types to enable use in unordered containers
namespace std {
    template<>
    struct hash<etl::JobId> {
        size_t operator()(const etl::JobId& id) const noexcept {
            return hash<string>{}(id.value());
        }
    };
    
    template<>
    struct hash<etl::ConnectionId> {
        size_t operator()(const etl::ConnectionId& id) const noexcept {
            return hash<string>{}(id.value());
        }
    };
    
    template<>
    struct hash<etl::UserId> {
        size_t operator()(const etl::UserId& id) const noexcept {
            return hash<string>{}(id.value());
        }
    };
}