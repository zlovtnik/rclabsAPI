#pragma once

#include <chrono>
#include <string>
#include <vector>

/**
 * @brief Configuration structure for HTTP server optimization features
 * 
 * This structure contains all configuration parameters for connection pooling,
 * timeout handling, and performance monitoring features.
 */
struct ServerConfig {
    // Connection Pool Settings
    size_t minConnections = 10;           // Minimum connections to maintain in pool
    size_t maxConnections = 100;          // Maximum connections allowed in pool
    std::chrono::seconds idleTimeout{300}; // 5 minutes - timeout for idle connections
    
    // Timeout Settings
    std::chrono::seconds connectionTimeout{30}; // Connection establishment timeout
    std::chrono::seconds requestTimeout{60};    // Request processing timeout
    
    // Performance Settings
    size_t maxRequestBodySize = 10 * 1024 * 1024; // 10MB - maximum request body size
    bool enableMetrics = true;                     // Enable performance metrics collection
    
    // Validation and default value handling
    struct ValidationResult {
        bool isValid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        
        void addError(const std::string& error) {
            isValid = false;
            errors.push_back(error);
        }
        
        void addWarning(const std::string& warning) {
            warnings.push_back(warning);
        }
    };
    
    /**
     * @brief Validate configuration parameters
     * @return ValidationResult containing validation status and any errors/warnings
     */
    ValidationResult validate() const {
        ValidationResult result;
        
        // Validate connection pool settings
        if (minConnections == 0) {
            result.addError("minConnections must be greater than 0");
        }
        
        if (maxConnections == 0) {
            result.addError("maxConnections must be greater than 0");
        }
        
        if (minConnections > maxConnections) {
            result.addError("minConnections cannot be greater than maxConnections");
        }
        
        if (maxConnections > 1000) {
            result.addWarning("maxConnections is very high (" + std::to_string(maxConnections) + 
                            "), consider system resource limits");
        }
        
        // Validate timeout settings
        if (connectionTimeout.count() <= 0) {
            result.addError("connectionTimeout must be positive");
        }
        
        if (requestTimeout.count() <= 0) {
            result.addError("requestTimeout must be positive");
        }
        
        if (idleTimeout.count() <= 0) {
            result.addError("idleTimeout must be positive");
        }
        
        // Validate timeout relationships
        if (connectionTimeout > requestTimeout) {
            result.addWarning("connectionTimeout is greater than requestTimeout, "
                            "which may cause unexpected behavior");
        }
        
        if (idleTimeout < std::chrono::seconds{60}) {
            result.addWarning("idleTimeout is less than 60 seconds, "
                            "which may cause frequent connection cycling");
        }
        
        // Validate performance settings
        if (maxRequestBodySize == 0) {
            result.addError("maxRequestBodySize must be greater than 0");
        }
        
        if (maxRequestBodySize > 100 * 1024 * 1024) { // 100MB
            result.addWarning("maxRequestBodySize is very large (" + 
                            std::to_string(maxRequestBodySize / (1024 * 1024)) + 
                            "MB), consider memory usage implications");
        }
        
        return result;
    }
    
    /**
     * @brief Apply default values for any unset or invalid parameters
     */
    void applyDefaults() {
        // Apply defaults for connection pool settings
        if (minConnections == 0) {
            minConnections = 10;
        }
        
        if (maxConnections == 0) {
            maxConnections = 100;
        }
        
        // Ensure min <= max
        if (minConnections > maxConnections) {
            maxConnections = minConnections;
        }
        
        // Apply defaults for timeout settings
        if (connectionTimeout.count() <= 0) {
            connectionTimeout = std::chrono::seconds{30};
        }
        
        if (requestTimeout.count() <= 0) {
            requestTimeout = std::chrono::seconds{60};
        }
        
        if (idleTimeout.count() <= 0) {
            idleTimeout = std::chrono::seconds{300};
        }
        
        // Apply defaults for performance settings
        if (maxRequestBodySize == 0) {
            maxRequestBodySize = 10 * 1024 * 1024; // 10MB
        }
    }
    
    /**
     * @brief Create ServerConfig from configuration values with validation
     * @param minConn Minimum connections in pool
     * @param maxConn Maximum connections in pool
     * @param idleTimeoutSec Idle timeout in seconds
     * @param connTimeoutSec Connection timeout in seconds
     * @param reqTimeoutSec Request timeout in seconds
     * @param maxBodySize Maximum request body size in bytes
     * @param metricsEnabled Whether to enable metrics collection
     * @return ServerConfig with validated and defaulted values
     */
    static ServerConfig create(size_t minConn = 10, 
                              size_t maxConn = 100,
                              int idleTimeoutSec = 300,
                              int connTimeoutSec = 30,
                              int reqTimeoutSec = 60,
                              size_t maxBodySize = 10 * 1024 * 1024,
                              bool metricsEnabled = true) {
        ServerConfig config;
        config.minConnections = minConn;
        config.maxConnections = maxConn;
        config.idleTimeout = std::chrono::seconds{idleTimeoutSec};
        config.connectionTimeout = std::chrono::seconds{connTimeoutSec};
        config.requestTimeout = std::chrono::seconds{reqTimeoutSec};
        config.maxRequestBodySize = maxBodySize;
        config.enableMetrics = metricsEnabled;
        
        // Apply defaults for any invalid values
        config.applyDefaults();
        
        return config;
    }
    
    /**
     * @brief Equality operator for testing and comparison
     */
    bool operator==(const ServerConfig& other) const {
        return minConnections == other.minConnections &&
               maxConnections == other.maxConnections &&
               idleTimeout == other.idleTimeout &&
               connectionTimeout == other.connectionTimeout &&
               requestTimeout == other.requestTimeout &&
               maxRequestBodySize == other.maxRequestBodySize &&
               enableMetrics == other.enableMetrics;
    }
    
    /**
     * @brief Inequality operator
     */
    bool operator!=(const ServerConfig& other) const {
        return !(*this == other);
    }
};