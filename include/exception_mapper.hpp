#pragma once

#include "etl_exceptions.hpp"
#include "logger.hpp"
#include <boost/beast/http.hpp>
#include <functional>
#include <memory>
#include <unordered_map>
#include <string>
#include <chrono>

namespace ETLPlus {
namespace ExceptionHandling {

// Forward declarations
class ExceptionMapper;

// HTTP response type alias
using HttpResponse = boost::beast::http::response<boost::beast::http::string_body>;

// Exception handler function type for pluggable handlers
using ExceptionHandlerFunc = std::function<HttpResponse(const etl::ETLException&, const std::string&)>;

// Standard error response format
struct ErrorResponseFormat {
    std::string status = "error";
    std::string message;
    std::string code;
    std::string correlationId;
    std::string timestamp;
    std::unordered_map<std::string, std::string> context;
    std::string details;
    
    // Convert to JSON string
    std::string toJson() const;
};

// Exception mapping configuration
struct ExceptionMappingConfig {
    // Default HTTP status for unmapped exceptions
    boost::beast::http::status defaultStatus = boost::beast::http::status::internal_server_error;
    
    // Whether to include stack traces in responses (for debugging)
    bool includeStackTrace = false;
    
    // Whether to include internal error details in responses
    bool includeInternalDetails = false;
    
    // Custom server header
    std::string serverHeader = "ETL Plus Backend";
    
    // CORS settings
    std::string corsOrigin = "*";
    
    // Whether to keep connections alive
    bool keepAlive = false;
};

// Main ExceptionMapper class for converting exceptions to HTTP responses
class ExceptionMapper {
public:
    // Constructor with optional configuration
    explicit ExceptionMapper(const ExceptionMappingConfig& config = ExceptionMappingConfig{});
    
    // Destructor
    ~ExceptionMapper() = default;
    
    // Disable copy constructor and assignment
    ExceptionMapper(const ExceptionMapper&) = delete;
    ExceptionMapper& operator=(const ExceptionMapper&) = delete;
    
    // Enable move constructor and assignment
    ExceptionMapper(ExceptionMapper&&) = default;
    ExceptionMapper& operator=(ExceptionMapper&&) = default;
    
    // Main method to map exception to HTTP response
    HttpResponse mapToResponse(const etl::ETLException& exception, 
                              const std::string& operationName = "") const;
    
    // Map standard exceptions to HTTP response
    HttpResponse mapToResponse(const std::exception& exception, 
                              const std::string& operationName = "") const;
    
    // Map unknown exceptions to HTTP response
    HttpResponse mapToResponse(const std::string& operationName = "") const;
    
    // Register custom exception handler for specific error codes
    void registerHandler(etl::ErrorCode code, ExceptionHandlerFunc handler);
    
    // Register custom exception handler for exception types
    template<typename ExceptionType>
    void registerTypeHandler(ExceptionHandlerFunc handler) {
        typeHandlers_[typeid(ExceptionType)] = handler;
    }
    
    // Update configuration
    void updateConfig(const ExceptionMappingConfig& config);
    
    // Get current configuration
    const ExceptionMappingConfig& getConfig() const { return config_; }
    
    // Create standard error response format
    ErrorResponseFormat createErrorFormat(const etl::ETLException& exception) const;
    
    // Log exception with correlation ID tracking
    void logException(const etl::ETLException& exception, 
                     const std::string& operationName = "") const;
    
    // Generate correlation ID for request tracking
    static std::string generateCorrelationId();
    
    // Set correlation ID for current request context
    static void setCurrentCorrelationId(const std::string& correlationId);
    
    // Get current correlation ID
    static std::string getCurrentCorrelationId();

private:
    // Configuration
    ExceptionMappingConfig config_;
    
    // Custom handlers for specific error codes
    std::unordered_map<etl::ErrorCode, ExceptionHandlerFunc> codeHandlers_;
    
    // Custom handlers for exception types
    std::unordered_map<std::type_index, ExceptionHandlerFunc> typeHandlers_;
    
    // Map error codes to HTTP status codes
    boost::beast::http::status mapErrorCodeToStatus(etl::ErrorCode code) const;
    
    // Create HTTP response with standard headers
    HttpResponse createHttpResponse(boost::beast::http::status status, 
                                   const std::string& body) const;
    
    // Convert exception to JSON response body
    std::string createJsonResponseBody(const etl::ETLException& exception) const;
    
    // Check if custom handler exists for error code
    bool hasCustomHandler(etl::ErrorCode code) const;
    
    // Check if custom handler exists for exception type
    template<typename ExceptionType>
    bool hasCustomTypeHandler() const {
        return typeHandlers_.find(typeid(ExceptionType)) != typeHandlers_.end();
    }
    
    // Apply custom handler if available
    std::optional<HttpResponse> tryCustomHandler(const etl::ETLException& exception,
                                                const std::string& operationName) const;
    
    // Initialize default error code to HTTP status mappings
    void initializeDefaultMappings();
    
    // Thread-local storage for correlation ID
    static thread_local std::string currentCorrelationId_;
};

// Factory function to create ExceptionMapper with common configurations
std::unique_ptr<ExceptionMapper> createExceptionMapper(
    const ExceptionMappingConfig& config = ExceptionMappingConfig{});

// Utility functions for common exception mapping scenarios

// Create validation error response
HttpResponse createValidationErrorResponse(const etl::ValidationException& exception,
                                          const std::string& operationName = "");

// Create system error response
HttpResponse createSystemErrorResponse(const etl::SystemException& exception,
                                      const std::string& operationName = "");

// Create business error response
HttpResponse createBusinessErrorResponse(const etl::BusinessException& exception,
                                        const std::string& operationName = "");

// Create rate limit error response
HttpResponse createRateLimitResponse(const std::string& message = "Rate limit exceeded",
                                    const std::string& retryAfter = "60");

// Create maintenance mode response
HttpResponse createMaintenanceResponse(const std::string& message = "Service temporarily unavailable");

// Global exception mapper instance (thread-safe)
ExceptionMapper& getGlobalExceptionMapper();

// Convenience macros for exception mapping
#define MAP_EXCEPTION_TO_RESPONSE(exception, operation) \
    ETLPlus::ExceptionHandling::getGlobalExceptionMapper().mapToResponse(exception, operation)

#define LOG_AND_MAP_EXCEPTION(exception, operation) \
    do { \
        ETLPlus::ExceptionHandling::getGlobalExceptionMapper().logException(exception, operation); \
        return ETLPlus::ExceptionHandling::getGlobalExceptionMapper().mapToResponse(exception, operation); \
    } while(0)

} // namespace ExceptionHandling
} // namespace ETLPlus
