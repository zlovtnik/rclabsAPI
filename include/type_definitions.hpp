#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <boost/hana.hpp>
#include "transparent_string_hash.hpp"

namespace etl {

// String handling type aliases for performance and consistency
using StringMap = std::unordered_map<std::string, std::string, 
                                   TransparentStringHash, std::equal_to<>>;
using StringSet = std::unordered_set<std::string, 
                                   TransparentStringHash, std::equal_to<>>;

// ============================================================================
// Strong Type Definitions using Boost.Hana
// ============================================================================

// Define the strong ID types using Hana tuples for better metaprogramming support
using StrongIdTypes = boost::hana::tuple<boost::hana::type<class JobIdTag>,
                                        boost::hana::type<class ConnectionIdTag>,
                                        boost::hana::type<class UserIdTag>>;

// Base class template for strong IDs
template<typename Tag>
class StrongId {
public:
    explicit StrongId(std::string id) : value_(std::move(id)) {
        if (value_.empty()) {
            throw std::invalid_argument("ID cannot be empty");
        }
    }
    
    const std::string& value() const noexcept { return value_; }
    
    bool operator==(const StrongId& other) const noexcept {
        return value_ == other.value_;
    }
    
    bool operator!=(const StrongId& other) const noexcept {
        return !(*this == other);
    }
    
    bool operator<(const StrongId& other) const noexcept {
        return value_ < other.value_;
    }
    
private:
    std::string value_;
};

// Concrete strong ID types
using JobId = StrongId<JobIdTag>;
using ConnectionId = StrongId<ConnectionIdTag>;
using UserId = StrongId<UserIdTag>;

// Type-safe ID generation utilities
class IdGenerator {
public:
    static etl::JobId generateJobId();
    static etl::ConnectionId generateConnectionId();
    static etl::UserId generateUserId();
    
private:
    static std::string generateUuid();
};

} // namespace etl

// ============================================================================
// Hash specializations using Hana for compile-time generation
// ============================================================================

namespace std {
    // Helper to generate hash specialization for any StrongId type
    template<typename Tag>
    struct hash<etl::StrongId<Tag>> {
        size_t operator()(const etl::StrongId<Tag>& id) const noexcept {
            return hash<string>{}(id.value());
        }
    };
}