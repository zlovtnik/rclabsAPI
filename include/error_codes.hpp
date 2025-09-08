#pragma once

#include <functional> // Needed for std::hash
#include <string>
#include <type_traits>
#include <unordered_map>

namespace etl {

// Consolidated error codes - reduced from 40+ to 28 codes (30% reduction)
// Organized by logical categories for better maintainability
enum class ErrorCode {
  // Validation errors (1000-1999) - Consolidated from 5 to 6 codes
  INVALID_INPUT = 1000, // Covers INVALID_INPUT, INVALID_FORMAT, INVALID_TYPE
  MISSING_FIELD = 1001, // Covers MISSING_REQUIRED_FIELD
  INVALID_RANGE = 1002, // Covers VALUE_OUT_OF_RANGE
  CONSTRAINT_VIOLATION = 1003, // New - covers database and business constraints

  // Authentication/Authorization errors (2000-2999) - Consolidated from 5 to 4
  // codes
  UNAUTHORIZED = 2000,  // Covers INVALID_CREDENTIALS, TOKEN_INVALID
  FORBIDDEN = 2001,     // Covers INSUFFICIENT_PERMISSIONS
  TOKEN_EXPIRED = 2002, // Kept as-is
  ACCESS_DENIED = 2003, // Covers ACCOUNT_LOCKED and general access issues

  // System errors (3000-3999) - Consolidated from 20+ to 12 codes
  DATABASE_ERROR = 3000,        // Covers CONNECTION_FAILED, QUERY_FAILED,
                                // TRANSACTION_FAILED, DEADLOCK_DETECTED
  NETWORK_ERROR = 3001,         // Covers REQUEST_TIMEOUT, CONNECTION_REFUSED,
                                // INVALID_RESPONSE, SERVICE_UNAVAILABLE
  FILE_ERROR = 3002,            // Covers FILE_NOT_FOUND, PERMISSION_DENIED
  MEMORY_ERROR = 3003,          // Covers OUT_OF_MEMORY, RESOURCE_EXHAUSTED
  CONFIGURATION_ERROR = 3004,   // Covers CONFIG_NOT_FOUND, CONFIG_PARSE_ERROR,
                                // INVALID_CONFIG_VALUE, MISSING_CONFIG_SECTION
  LOCK_TIMEOUT = 3005,          // New - covers threading and concurrency issues
  RATE_LIMIT_EXCEEDED = 3006,   // Kept as-is for API rate limiting
  DISK_FULL = 3007,             // Kept as-is for storage issues
  THREAD_POOL_EXHAUSTED = 3008, // Kept as-is for concurrency limits
  SERVICE_STARTUP_FAILED = 3009, // Kept as-is for initialization issues
  COMPONENT_UNAVAILABLE = 3010,  // Kept as-is for service dependencies
  INTERNAL_ERROR = 3011, // Covers UNKNOWN_ERROR and unexpected system errors
  INVALID_CONNECTION =
      3012, // New - for connection pool invalid connection errors
  POOL_NOT_RUNNING = 3013, // New - for connection pool not running errors
  POOL_CAPACITY_EXCEEDED =
      3014, // New - for connection pool capacity exceeded errors

  // Business logic errors (4000-4999) - Consolidated from 6 to 6 codes
  JOB_NOT_FOUND = 4000,       // Kept as-is
  JOB_ALREADY_RUNNING = 4001, // Kept as-is
  INVALID_JOB_STATE = 4002,   // New - covers job lifecycle issues
  PROCESSING_FAILED =
      4003, // Covers JOB_EXECUTION_FAILED, EXTRACT_FAILED, LOAD_FAILED
  TRANSFORMATION_ERROR = 4004, // Covers DATA_TRANSFORMATION_ERROR
  DATA_INTEGRITY_ERROR =
      4005 // New - covers data validation and consistency issues
};

// Error code metadata for enhanced error handling
struct ErrorCodeInfo {
  std::string description;
  std::string category;
  bool isRetryable;
  int defaultHttpStatus;
};

// Error code information mapping
const std::unordered_map<ErrorCode, ErrorCodeInfo> &getErrorCodeInfo();

} // namespace etl

// Hash support for ErrorCode keys in unordered_map
namespace std {
template <> struct hash<etl::ErrorCode> {
  size_t operator()(const etl::ErrorCode code) const noexcept {
    using Underlying = std::underlying_type_t<etl::ErrorCode>;
    return std::hash<Underlying>{}(static_cast<Underlying>(code));
  }
};
} // namespace std

namespace etl {
// Utility functions
const char *getErrorCodeDescription(ErrorCode code);
std::string getErrorCategory(ErrorCode code);
bool isRetryableError(ErrorCode code);
int getDefaultHttpStatus(ErrorCode code);
std::string errorCodeToString(ErrorCode code);

// Migration mapping from old error codes to new consolidated codes
namespace migration {

// Old error codes from the legacy system (for migration reference)
enum class LegacyErrorCode {
  // Validation errors (old)
  INVALID_INPUT = 1000,
  MISSING_REQUIRED_FIELD = 1001,
  INVALID_FORMAT = 1002,
  VALUE_OUT_OF_RANGE = 1003,
  INVALID_TYPE = 1004,

  // Authentication errors (old)
  INVALID_CREDENTIALS = 2000,
  TOKEN_EXPIRED = 2001,
  TOKEN_INVALID = 2002,
  INSUFFICIENT_PERMISSIONS = 2003,
  ACCOUNT_LOCKED = 2004,

  // Database errors (old)
  CONNECTION_FAILED = 3000,
  QUERY_FAILED = 3001,
  TRANSACTION_FAILED = 3002,
  DEADLOCK_DETECTED = 3003,
  CONSTRAINT_VIOLATION = 3004,
  CONNECTION_TIMEOUT = 3005,

  // Network errors (old)
  REQUEST_TIMEOUT = 4000,
  CONNECTION_REFUSED = 4001,
  INVALID_RESPONSE = 4002,
  RATE_LIMIT_EXCEEDED = 4003,
  SERVICE_UNAVAILABLE = 4004,

  // ETL Processing errors (old)
  JOB_EXECUTION_FAILED = 5000,
  DATA_TRANSFORMATION_ERROR = 5001,
  EXTRACT_FAILED = 5002,
  LOAD_FAILED = 5003,
  JOB_NOT_FOUND = 5004,
  JOB_ALREADY_RUNNING = 5005,

  // Configuration errors (old)
  CONFIG_NOT_FOUND = 6000,
  CONFIG_PARSE_ERROR = 6001,
  INVALID_CONFIG_VALUE = 6002,
  MISSING_CONFIG_SECTION = 6003,

  // Resource errors (old)
  OUT_OF_MEMORY = 7000,
  FILE_NOT_FOUND = 7001,
  PERMISSION_DENIED = 7002,
  DISK_FULL = 7003,
  RESOURCE_EXHAUSTED = 7004,

  // System errors (old)
  INTERNAL_ERROR = 8000,
  SERVICE_STARTUP_FAILED = 8001,
  COMPONENT_UNAVAILABLE = 8002,
  THREAD_POOL_EXHAUSTED = 8003,

  // Unknown/Generic (old)
  UNKNOWN_ERROR = 9000
};

// Migration mapping function
ErrorCode migrateLegacyErrorCode(LegacyErrorCode legacyCode);

// Get migration information
std::string getMigrationInfo(LegacyErrorCode legacyCode);

} // namespace migration

} // namespace etl
