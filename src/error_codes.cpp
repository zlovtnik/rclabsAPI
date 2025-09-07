#include "error_codes.hpp"
#include <stdexcept>

namespace etl {

// Error code information mapping
const std::unordered_map<ErrorCode, ErrorCodeInfo>& getErrorCodeInfo() {
    static const std::unordered_map<ErrorCode, ErrorCodeInfo> errorInfo = {
        // Validation errors
        {ErrorCode::INVALID_INPUT, {
            "Invalid input data or format",
            "Validation",
            false, // Not retryable - client error
            400    // Bad Request
        }},
        {ErrorCode::MISSING_FIELD, {
            "Required field is missing",
            "Validation", 
            false,
            400
        }},
        {ErrorCode::INVALID_RANGE, {
            "Value is outside acceptable range",
            "Validation",
            false,
            400
        }},
        {ErrorCode::CONSTRAINT_VIOLATION, {
            "Data violates business or database constraints",
            "Validation",
            false,
            409 // Conflict
        }},
        
        // Authentication/Authorization errors
        {ErrorCode::UNAUTHORIZED, {
            "Authentication required or credentials invalid",
            "Authentication",
            false,
            401 // Unauthorized
        }},
        {ErrorCode::FORBIDDEN, {
            "Access forbidden - insufficient permissions",
            "Authorization",
            false,
            403 // Forbidden
        }},
        {ErrorCode::TOKEN_EXPIRED, {
            "Authentication token has expired",
            "Authentication",
            false,
            401
        }},
        {ErrorCode::ACCESS_DENIED, {
            "Access denied to requested resource",
            "Authorization",
            false,
            403
        }},
        
        // System errors
        {ErrorCode::DATABASE_ERROR, {
            "Database operation failed",
            "System",
            true, // Retryable - might be transient
            500   // Internal Server Error
        }},
        {ErrorCode::NETWORK_ERROR, {
            "Network communication failed",
            "System",
            true,
            502 // Bad Gateway
        }},
        {ErrorCode::FILE_ERROR, {
            "File system operation failed",
            "System",
            true,
            500
        }},
        {ErrorCode::MEMORY_ERROR, {
            "Memory allocation or resource exhaustion",
            "System",
            true,
            503 // Service Unavailable
        }},
        {ErrorCode::CONFIGURATION_ERROR, {
            "Configuration loading or parsing failed",
            "System",
            false, // Usually requires manual intervention
            500
        }},
        {ErrorCode::LOCK_TIMEOUT, {
            "Lock acquisition timeout",
            "System",
            true,
            503
        }},
        {ErrorCode::RATE_LIMIT_EXCEEDED, {
            "Request rate limit exceeded",
            "System",
            true, // Retryable after delay
            429 // Too Many Requests
        }},
        {ErrorCode::DISK_FULL, {
            "Disk space exhausted",
            "System",
            false, // Requires manual intervention
            507 // Insufficient Storage
        }},
        {ErrorCode::THREAD_POOL_EXHAUSTED, {
            "Thread pool capacity exceeded",
            "System",
            true,
            503
        }},
        {ErrorCode::SERVICE_STARTUP_FAILED, {
            "Service initialization failed",
            "System",
            false,
            500
        }},
        {ErrorCode::COMPONENT_UNAVAILABLE, {
            "Required component is unavailable",
            "System",
            true,
            503
        }},
        {ErrorCode::INTERNAL_ERROR, {
            "Unexpected internal error",
            "System",
            false,
            500
        }},
        
        // Business logic errors
        {ErrorCode::JOB_NOT_FOUND, {
            "Requested job does not exist",
            "Business",
            false,
            404 // Not Found
        }},
        {ErrorCode::JOB_ALREADY_RUNNING, {
            "Job is already in running state",
            "Business",
            false,
            409 // Conflict
        }},
        {ErrorCode::INVALID_JOB_STATE, {
            "Job is in invalid state for requested operation",
            "Business",
            false,
            409
        }},
        {ErrorCode::PROCESSING_FAILED, {
            "Data processing operation failed",
            "Business",
            true, // Might be retryable depending on cause
            500
        }},
        {ErrorCode::TRANSFORMATION_ERROR, {
            "Data transformation failed",
            "Business",
            false, // Usually data-specific
            422 // Unprocessable Entity
        }},
        {ErrorCode::DATA_INTEGRITY_ERROR, {
            "Data integrity validation failed",
            "Business",
            false,
            422
        }}
    };
    
    return errorInfo;
}

// Utility function implementations
const char* getErrorCodeDescription(ErrorCode code) {
    const auto& info = getErrorCodeInfo();
    auto it = info.find(code);
    return (it != info.end()) ? it->second.description.c_str() : "Unknown error";
}

std::string getErrorCategory(ErrorCode code) {
    const auto& info = getErrorCodeInfo();
    auto it = info.find(code);
    return (it != info.end()) ? it->second.category : "Unknown";
}

bool isRetryableError(ErrorCode code) {
    const auto& info = getErrorCodeInfo();
    auto it = info.find(code);
    return (it != info.end()) ? it->second.isRetryable : false;
}

int getDefaultHttpStatus(ErrorCode code) {
    const auto& info = getErrorCodeInfo();
    auto it = info.find(code);
    return (it != info.end()) ? it->second.defaultHttpStatus : 500;
}

std::string errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::INVALID_INPUT: return "INVALID_INPUT";
        case ErrorCode::MISSING_FIELD: return "MISSING_FIELD";
        case ErrorCode::INVALID_RANGE: return "INVALID_RANGE";
        case ErrorCode::CONSTRAINT_VIOLATION: return "CONSTRAINT_VIOLATION";
        case ErrorCode::UNAUTHORIZED: return "UNAUTHORIZED";
        case ErrorCode::FORBIDDEN: return "FORBIDDEN";
        case ErrorCode::TOKEN_EXPIRED: return "TOKEN_EXPIRED";
        case ErrorCode::ACCESS_DENIED: return "ACCESS_DENIED";
        case ErrorCode::DATABASE_ERROR: return "DATABASE_ERROR";
        case ErrorCode::NETWORK_ERROR: return "NETWORK_ERROR";
        case ErrorCode::FILE_ERROR: return "FILE_ERROR";
        case ErrorCode::MEMORY_ERROR: return "MEMORY_ERROR";
        case ErrorCode::CONFIGURATION_ERROR: return "CONFIGURATION_ERROR";
        case ErrorCode::LOCK_TIMEOUT: return "LOCK_TIMEOUT";
        case ErrorCode::RATE_LIMIT_EXCEEDED: return "RATE_LIMIT_EXCEEDED";
        case ErrorCode::DISK_FULL: return "DISK_FULL";
        case ErrorCode::THREAD_POOL_EXHAUSTED: return "THREAD_POOL_EXHAUSTED";
        case ErrorCode::SERVICE_STARTUP_FAILED: return "SERVICE_STARTUP_FAILED";
        case ErrorCode::COMPONENT_UNAVAILABLE: return "COMPONENT_UNAVAILABLE";
        case ErrorCode::INTERNAL_ERROR: return "INTERNAL_ERROR";
        case ErrorCode::JOB_NOT_FOUND: return "JOB_NOT_FOUND";
        case ErrorCode::JOB_ALREADY_RUNNING: return "JOB_ALREADY_RUNNING";
        case ErrorCode::INVALID_JOB_STATE: return "INVALID_JOB_STATE";
        case ErrorCode::PROCESSING_FAILED: return "PROCESSING_FAILED";
        case ErrorCode::TRANSFORMATION_ERROR: return "TRANSFORMATION_ERROR";
        case ErrorCode::DATA_INTEGRITY_ERROR: return "DATA_INTEGRITY_ERROR";
        default: return "UNKNOWN_ERROR";
    }
}

// Migration namespace implementation
namespace migration {
    
    ErrorCode migrateLegacyErrorCode(LegacyErrorCode legacyCode) {
        switch (legacyCode) {
            // Validation errors migration
            case LegacyErrorCode::INVALID_INPUT:
            case LegacyErrorCode::INVALID_FORMAT:
            case LegacyErrorCode::INVALID_TYPE:
                return ErrorCode::INVALID_INPUT;
                
            case LegacyErrorCode::MISSING_REQUIRED_FIELD:
                return ErrorCode::MISSING_FIELD;
                
            case LegacyErrorCode::VALUE_OUT_OF_RANGE:
                return ErrorCode::INVALID_RANGE;
                
            // Authentication errors migration
            case LegacyErrorCode::INVALID_CREDENTIALS:
            case LegacyErrorCode::TOKEN_INVALID:
                return ErrorCode::UNAUTHORIZED;
                
            case LegacyErrorCode::TOKEN_EXPIRED:
                return ErrorCode::TOKEN_EXPIRED;
                
            case LegacyErrorCode::INSUFFICIENT_PERMISSIONS:
                return ErrorCode::FORBIDDEN;
                
            case LegacyErrorCode::ACCOUNT_LOCKED:
                return ErrorCode::ACCESS_DENIED;
                
            // Database errors migration
            case LegacyErrorCode::CONNECTION_FAILED:
            case LegacyErrorCode::QUERY_FAILED:
            case LegacyErrorCode::TRANSACTION_FAILED:
            case LegacyErrorCode::DEADLOCK_DETECTED:
            case LegacyErrorCode::CONNECTION_TIMEOUT:
                return ErrorCode::DATABASE_ERROR;
                
            case LegacyErrorCode::CONSTRAINT_VIOLATION:
                return ErrorCode::CONSTRAINT_VIOLATION;
                
            // Network errors migration
            case LegacyErrorCode::REQUEST_TIMEOUT:
            case LegacyErrorCode::CONNECTION_REFUSED:
            case LegacyErrorCode::INVALID_RESPONSE:
            case LegacyErrorCode::SERVICE_UNAVAILABLE:
                return ErrorCode::NETWORK_ERROR;
                
            case LegacyErrorCode::RATE_LIMIT_EXCEEDED:
                return ErrorCode::RATE_LIMIT_EXCEEDED;
                
            // ETL Processing errors migration
            case LegacyErrorCode::JOB_EXECUTION_FAILED:
            case LegacyErrorCode::EXTRACT_FAILED:
            case LegacyErrorCode::LOAD_FAILED:
                return ErrorCode::PROCESSING_FAILED;
                
            case LegacyErrorCode::DATA_TRANSFORMATION_ERROR:
                return ErrorCode::TRANSFORMATION_ERROR;
                
            case LegacyErrorCode::JOB_NOT_FOUND:
                return ErrorCode::JOB_NOT_FOUND;
                
            case LegacyErrorCode::JOB_ALREADY_RUNNING:
                return ErrorCode::JOB_ALREADY_RUNNING;
                
            // Configuration errors migration
            case LegacyErrorCode::CONFIG_NOT_FOUND:
            case LegacyErrorCode::CONFIG_PARSE_ERROR:
            case LegacyErrorCode::INVALID_CONFIG_VALUE:
            case LegacyErrorCode::MISSING_CONFIG_SECTION:
                return ErrorCode::CONFIGURATION_ERROR;
                
            // Resource errors migration
            case LegacyErrorCode::OUT_OF_MEMORY:
            case LegacyErrorCode::RESOURCE_EXHAUSTED:
                return ErrorCode::MEMORY_ERROR;
                
            case LegacyErrorCode::FILE_NOT_FOUND:
            case LegacyErrorCode::PERMISSION_DENIED:
                return ErrorCode::FILE_ERROR;
                
            case LegacyErrorCode::DISK_FULL:
                return ErrorCode::DISK_FULL;
                
            // System errors migration
            case LegacyErrorCode::SERVICE_STARTUP_FAILED:
                return ErrorCode::SERVICE_STARTUP_FAILED;
                
            case LegacyErrorCode::COMPONENT_UNAVAILABLE:
                return ErrorCode::COMPONENT_UNAVAILABLE;
                
            case LegacyErrorCode::THREAD_POOL_EXHAUSTED:
                return ErrorCode::THREAD_POOL_EXHAUSTED;
                
            case LegacyErrorCode::INTERNAL_ERROR:
            case LegacyErrorCode::UNKNOWN_ERROR:
            default:
                return ErrorCode::INTERNAL_ERROR;
        }
    }
    
    std::string getMigrationInfo(LegacyErrorCode legacyCode) {
        ErrorCode newCode = migrateLegacyErrorCode(legacyCode);
        
        std::string info = "Legacy code " + std::to_string(static_cast<int>(legacyCode)) + 
                          " migrates to " + std::to_string(static_cast<int>(newCode)) + 
                          " (" + getErrorCodeDescription(newCode) + ")";
        
        // Add specific migration notes for consolidated codes
        switch (legacyCode) {
            case LegacyErrorCode::INVALID_FORMAT:
            case LegacyErrorCode::INVALID_TYPE:
                info += " - Consolidated into INVALID_INPUT for simpler validation handling";
                break;
            case LegacyErrorCode::QUERY_FAILED:
            case LegacyErrorCode::TRANSACTION_FAILED:
            case LegacyErrorCode::DEADLOCK_DETECTED:
                info += " - Consolidated into DATABASE_ERROR with specific details in error context";
                break;
            case LegacyErrorCode::REQUEST_TIMEOUT:
            case LegacyErrorCode::CONNECTION_REFUSED:
                info += " - Consolidated into NETWORK_ERROR for unified network error handling";
                break;
            default:
                break;
        }
        
        return info;
    }
    
} // namespace migration

} // namespace etl