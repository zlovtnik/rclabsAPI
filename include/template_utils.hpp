#pragma once

#include "component_logger.hpp"
#include "type_definitions.hpp"
#include "transparent_string_hash.hpp"
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <sstream>
#include <stdexcept>

namespace etl {
namespace template_utils {

// ============================================================================
// Type Traits for Components
// ============================================================================

/**
 * Type trait to check if a type has a ComponentTrait specialization
 */
template<typename T, typename = void>
struct has_component_trait : std::false_type {};

template<typename T>
struct has_component_trait<T, std::void_t<decltype(ComponentTrait<T>::name)>> : std::true_type {};

template<typename T>
constexpr bool has_component_trait_v = has_component_trait<T>::value;

/**
 * Type trait to check if a type is a strong ID type
 */
template<typename T>
struct is_strong_id : std::false_type {};

template<>
struct is_strong_id<JobId> : std::true_type {};

template<>
struct is_strong_id<ConnectionId> : std::true_type {};

template<>
struct is_strong_id<UserId> : std::true_type {};

template<typename T>
constexpr bool is_strong_id_v = is_strong_id<T>::value;

/**
 * Type trait to extract the underlying value type from strong ID types
 */
template<typename T>
struct strong_id_value_type {
    using type = void;
};

template<>
struct strong_id_value_type<JobId> {
    using type = std::string;
};

template<>
struct strong_id_value_type<ConnectionId> {
    using type = std::string;
};

template<>
struct strong_id_value_type<UserId> {
    using type = std::string;
};

template<typename T>
using strong_id_value_type_t = typename strong_id_value_type<T>::type;

// ============================================================================
// Compile-time String Hashing Utilities
// ============================================================================

/**
 * Compile-time string hash using FNV-1a algorithm
 */
constexpr std::size_t fnv1a_hash(std::string_view str) noexcept {
    constexpr std::size_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr std::size_t FNV_PRIME = 1099511628211ULL;
    
    std::size_t hash = FNV_OFFSET_BASIS;
    for (char c : str) {
        hash ^= static_cast<std::size_t>(c);
        hash *= FNV_PRIME;
    }
    return hash;
}

/**
 * Compile-time string literal wrapper for hashing
 */
template<std::size_t N>
struct string_literal {
    constexpr string_literal(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) {
            value[i] = str[i];
        }
    }
    
    char value[N];
    
    constexpr std::string_view view() const noexcept {
        return std::string_view(value, N - 1); // Exclude null terminator
    }
    
    constexpr std::size_t hash() const noexcept {
        return fnv1a_hash(view());
    }
};

/**
 * Compile-time string hash wrapper
 */
template<std::size_t N>
struct compile_time_hash {
    static constexpr std::size_t hash_string(const char (&str)[N]) {
        return fnv1a_hash(std::string_view(str, N - 1));
    }
};

// Helper macro for compile-time string hashing
#define COMPILE_TIME_HASH(str) etl::template_utils::fnv1a_hash(str)

// ============================================================================
// Template-based Configuration Helpers
// ============================================================================

/**
 * Configuration value wrapper with type safety and default values
 */
template<typename T>
class ConfigValue {
public:
    ConfigValue() = default;
    explicit ConfigValue(T value) : value_(std::move(value)), has_value_(true) {}
    
    const T& get() const {
        if (!has_value_) {
            throw std::runtime_error("ConfigValue has no value");
        }
        return value_;
    }
    
    const T& get_or(const T& default_value) const {
        return has_value_ ? value_ : default_value;
    }
    
    bool has_value() const noexcept { return has_value_; }
    
    void set(T value) {
        value_ = std::move(value);
        has_value_ = true;
    }
    
    void reset() {
        has_value_ = false;
        value_ = T{};
    }
    
private:
    T value_{};
    bool has_value_{false};
};

/**
 * Type-safe configuration map with compile-time key validation
 */
template<typename KeyEnum>
class TypedConfigMap {
public:
    template<KeyEnum Key>
    void set(std::string value) {
        config_[static_cast<std::size_t>(Key)] = std::move(value);
    }
    
    template<KeyEnum Key>
    std::string get() const {
        auto it = config_.find(static_cast<std::size_t>(Key));
        if (it == config_.end()) {
            throw std::runtime_error("Configuration key not found");
        }
        return it->second;
    }
    
    template<KeyEnum Key>
    std::string get_or(const std::string& default_value) const {
        auto it = config_.find(static_cast<std::size_t>(Key));
        return it != config_.end() ? it->second : default_value;
    }
    
    template<KeyEnum Key>
    bool has(KeyEnum key) const {
        return config_.find(static_cast<std::size_t>(Key)) != config_.end();
    }
    
private:
    std::unordered_map<std::size_t, std::string> config_;
};

// ============================================================================
// Performance Measurement Templates
// ============================================================================

/**
 * RAII-based performance timer with automatic logging
 */
template<typename Component>
class ScopedTimer {
public:
    explicit ScopedTimer(std::string operation_name)
        : operation_name_(std::move(operation_name))
        , start_time_(std::chrono::steady_clock::now()) {
        static_assert(has_component_trait_v<Component>, 
                     "Component must have ComponentTrait specialization");
    }
    
    ~ScopedTimer() {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_).count();
        
        ComponentLogger<Component>::logPerformance(
            operation_name_, 
            static_cast<double>(duration) / 1000.0  // Convert to milliseconds
        );
    }
    
    // Get elapsed time without destroying the timer
    double elapsed_ms() const {
        auto current_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            current_time - start_time_).count();
        return static_cast<double>(duration) / 1000.0;
    }
    
private:
    std::string operation_name_;
    std::chrono::steady_clock::time_point start_time_;
};

/**
 * Macro for easy scoped timing
 */
#define SCOPED_TIMER(Component, operation) \
    etl::template_utils::ScopedTimer<Component> timer_##__LINE__(operation)

// ============================================================================
// Template-based Validation Helpers
// ============================================================================

/**
 * Compile-time validation for configuration values
 */
template<typename T>
struct Validator {
    using value_type = T;
    
    virtual ~Validator() = default;
    virtual bool validate(const T& value) const = 0;
    virtual std::string error_message() const = 0;
};

/**
 * Range validator for numeric types
 */
template<typename T>
class RangeValidator : public Validator<T> {
public:
    RangeValidator(T min_val, T max_val) 
        : min_val_(min_val), max_val_(max_val) {
        static_assert(std::is_arithmetic_v<T>, "RangeValidator requires arithmetic type");
    }
    
    bool validate(const T& value) const override {
        return value >= min_val_ && value <= max_val_;
    }
    
    std::string error_message() const override {
        std::stringstream ss;
        ss << "Value must be between " << min_val_ << " and " << max_val_;
        return ss.str();
    }
    
private:
    T min_val_;
    T max_val_;
};

/**
 * String length validator
 */
class StringLengthValidator : public Validator<std::string> {
public:
    StringLengthValidator(std::size_t min_len, std::size_t max_len)
        : min_len_(min_len), max_len_(max_len) {}
    
    bool validate(const std::string& value) const override {
        return value.length() >= min_len_ && value.length() <= max_len_;
    }
    
    std::string error_message() const override {
        std::stringstream ss;
        ss << "String length must be between " << min_len_ << " and " << max_len_;
        return ss.str();
    }
    
private:
    std::size_t min_len_;
    std::size_t max_len_;
};

// ============================================================================
// Template-based Factory Pattern
// ============================================================================

/**
 * Generic factory template with type registration
 */
template<typename BaseType, typename KeyType = std::string>
class Factory {
public:
    using CreateFunc = std::function<std::unique_ptr<BaseType>()>;
    
    template<typename DerivedType>
    void register_type(const KeyType& key) {
        static_assert(std::is_base_of_v<BaseType, DerivedType>,
                     "DerivedType must inherit from BaseType");
        
        auto creator = []() -> std::unique_ptr<BaseType> {
            return std::make_unique<DerivedType>();
        };
        
        auto [it, inserted] = creators_.emplace(key, creator);
        if (!inserted) {
            throw std::runtime_error("Type registration failed: key '" + 
                                   std::string(key) + "' is already registered");
        }
    }
    
    std::unique_ptr<BaseType> create(const KeyType& key) const {
        auto it = creators_.find(key);
        if (it == creators_.end()) {
            return nullptr;
        }
        return it->second();
    }
    
    bool is_registered(const KeyType& key) const {
        return creators_.find(key) != creators_.end();
    }
    
    std::vector<KeyType> registered_keys() const {
        std::vector<KeyType> keys;
        keys.reserve(creators_.size());
        for (const auto& pair : creators_) {
            keys.push_back(pair.first);
        }
        return keys;
    }
    
private:
    std::unordered_map<KeyType, CreateFunc, 
                      std::conditional_t<std::is_same_v<KeyType, std::string>,
                                       TransparentStringHash,
                                       std::hash<KeyType>>,
                      std::conditional_t<std::is_same_v<KeyType, std::string>,
                                       std::equal_to<>,
                                       std::equal_to<KeyType>>> creators_;
};

// ============================================================================
// Template-based Event System
// ============================================================================

/**
 * Type-safe event dispatcher
 */
template<typename EventType>
class EventDispatcher {
public:
    using HandlerFunc = std::function<void(const EventType&)>;
    using HandlerId = std::size_t;
    
    HandlerId subscribe(HandlerFunc handler) {
        HandlerId id = next_id_++;
        handlers_[id] = std::move(handler);
        return id;
    }
    
    void unsubscribe(HandlerId id) {
        handlers_.erase(id);
    }
    
    void dispatch(const EventType& event) {
        auto snapshot = handlers_; // copy handlers (cheap if small)
        for (const auto& [id, handler] : snapshot) {
            try {
                handler(event);
            } catch (const std::exception& e) {
                // Log error but continue with other handlers
                // Could use ComponentLogger here if we had a component context
            }
        }
    }
    
    std::size_t handler_count() const {
        return handlers_.size();
    }
    
private:
    std::unordered_map<HandlerId, HandlerFunc> handlers_;
    HandlerId next_id_{1};
};

// ============================================================================
// Template Utility Functions
// ============================================================================

/**
 * Convert strong ID types to their underlying string representation
 */
template<typename T>
std::enable_if_t<is_strong_id_v<T>, std::string> to_string(const T& id) {
    return id.value();
}

/**
 * Convert regular types to string
 */
template<typename T>
std::enable_if_t<!is_strong_id_v<T> && !std::is_same_v<T, std::string>, std::string> 
to_string(const T& value) {
    if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(value);
    } else {
        std::stringstream ss;
        ss << value;
        return ss.str();
    }
}

/**
 * String passthrough
 */
inline const std::string& to_string(const std::string& str) {
    return str;
}

/**
 * Template-based conditional compilation helper
 */
template<bool Condition>
struct conditional_compile {
    template<typename T>
    static constexpr T value(T true_val, T false_val) {
        if constexpr (Condition) {
            return true_val;
        } else {
            return false_val;
        }
    }
};

} // namespace template_utils
} // namespace etl