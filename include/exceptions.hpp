#pragma once

#include <chrono>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "transparent_string_hash.hpp"

namespace ETLPlus::Exceptions {

// Error severity levels
enum class ErrorSeverity {
  LOW = 1,     // Warning-level, operation can continue
  MEDIUM = 2,  // Error-level, operation fails but system continues
  HIGH = 3,    // Critical error, component failure
  CRITICAL = 4 // Fatal error, system shutdown required
};

// Error categories for proper handling
enum class ErrorCategory {
  VALIDATION,     // Input validation errors
  AUTHENTICATION, // Auth and authorization errors
  DATABASE,       // Database connection and query errors
  NETWORK,        // Network and HTTP-related errors
  ETL_PROCESSING, // ETL pipeline and job errors
  CONFIGURATION,  // Config loading and parsing errors
  RESOURCE,       // Memory, file, and resource errors
  SYSTEM,         // System-level errors
  UNKNOWN         // Uncategorized errors
};

// Error codes for programmatic handling
enum class ErrorCode {
  // Validation errors (1000-1999)
  INVALID_INPUT = 1000,
  MISSING_REQUIRED_FIELD = 1001,
  INVALID_FORMAT = 1002,
  VALUE_OUT_OF_RANGE = 1003,
  INVALID_TYPE = 1004,

  // Authentication errors (2000-2999)
  INVALID_CREDENTIALS = 2000,
  TOKEN_EXPIRED = 2001,
  TOKEN_INVALID = 2002,
  INSUFFICIENT_PERMISSIONS = 2003,
  ACCOUNT_LOCKED = 2004,

  // Database errors (3000-3999)
  CONNECTION_FAILED = 3000,
  QUERY_FAILED = 3001,
  TRANSACTION_FAILED = 3002,
  DEADLOCK_DETECTED = 3003,
  CONSTRAINT_VIOLATION = 3004,
  CONNECTION_TIMEOUT = 3005,

  // Network errors (4000-4999)
  REQUEST_TIMEOUT = 4000,
  CONNECTION_REFUSED = 4001,
  INVALID_RESPONSE = 4002,
  RATE_LIMIT_EXCEEDED = 4003,
  SERVICE_UNAVAILABLE = 4004,

  // ETL Processing errors (5000-5999)
  JOB_EXECUTION_FAILED = 5000,
  DATA_TRANSFORMATION_ERROR = 5001,
  EXTRACT_FAILED = 5002,
  LOAD_FAILED = 5003,
  JOB_NOT_FOUND = 5004,
  JOB_ALREADY_RUNNING = 5005,

  // Configuration errors (6000-6999)
  CONFIG_NOT_FOUND = 6000,
  CONFIG_PARSE_ERROR = 6001,
  INVALID_CONFIG_VALUE = 6002,
  MISSING_CONFIG_SECTION = 6003,

  // Resource errors (7000-7999)
  OUT_OF_MEMORY = 7000,
  FILE_NOT_FOUND = 7001,
  PERMISSION_DENIED = 7002,
  DISK_FULL = 7003,
  RESOURCE_EXHAUSTED = 7004,

  // System errors (8000-8999)
  INTERNAL_ERROR = 8000,
  SERVICE_STARTUP_FAILED = 8001,
  COMPONENT_UNAVAILABLE = 8002,
  THREAD_POOL_EXHAUSTED = 8003,

  // Unknown/Generic (9000-9999)
  UNKNOWN_ERROR = 9000
};

// Error context information
struct ErrorContext {
  std::string correlationId;
  std::string userId;
  std::string operation;
  std::string component;
  std::chrono::system_clock::time_point timestamp;
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> additionalInfo;

  ErrorContext();
  explicit ErrorContext(const std::string &operation);

  void addInfo(const std::string &key, const std::string &value);
  std::string toString() const;
};

// Base exception class for all ETL Plus exceptions
class BaseException : public std::exception {
protected:
  ErrorCode errorCode_;
  ErrorCategory category_;
  ErrorSeverity severity_;
  std::string message_;
  std::string technicalDetails_;
  ErrorContext context_;
  std::shared_ptr<BaseException> cause_;
  std::vector<std::string> stackTrace_;

public:
  BaseException(ErrorCode code, ErrorCategory category, ErrorSeverity severity,
                const std::string &message,
                const std::string &technicalDetails = "");

  BaseException(ErrorCode code, ErrorCategory category, ErrorSeverity severity,
                const std::string &message, const ErrorContext &context,
                const std::string &technicalDetails = "");

  virtual ~BaseException() = default;

  // Standard exception interface
  const char *what() const noexcept override;

  // Extended exception interface
  ErrorCode getErrorCode() const { return errorCode_; }
  ErrorCategory getCategory() const { return category_; }
  ErrorSeverity getSeverity() const { return severity_; }
  const std::string &getMessage() const { return message_; }
  const std::string &getTechnicalDetails() const { return technicalDetails_; }
  const ErrorContext &getContext() const { return context_; }

  void setCause(std::shared_ptr<BaseException> cause);
  std::shared_ptr<BaseException> getCause() const { return cause_; }

  void captureStackTrace();
  const std::vector<std::string> &getStackTrace() const { return stackTrace_; }

  // Serialization for logging and API responses
  std::string toJsonString() const;
  std::string toLogString() const;
};

// Validation exceptions
class ValidationException : public BaseException {
public:
  ValidationException(ErrorCode code, const std::string &message,
                      const std::string &field = "",
                      const std::string &value = "");

  ValidationException(ErrorCode code, const std::string &message,
                      const ErrorContext &context,
                      const std::string &field = "",
                      const std::string &value = "");
};

// Authentication and authorization exceptions
class AuthException : public BaseException {
public:
  AuthException(ErrorCode code, const std::string &message,
                const std::string &userId = "");

  AuthException(ErrorCode code, const std::string &message,
                const ErrorContext &context, const std::string &userId = "");
};

// Database-related exceptions
class DatabaseException : public BaseException {
public:
  DatabaseException(ErrorCode code, const std::string &message,
                    const std::string &query = "");

  DatabaseException(ErrorCode code, const std::string &message,
                    const ErrorContext &context, const std::string &query = "");
};

// Network and HTTP exceptions
class NetworkException : public BaseException {
public:
  NetworkException(ErrorCode code, const std::string &message,
                   int httpStatusCode = 0);

  NetworkException(ErrorCode code, const std::string &message,
                   const ErrorContext &context, int httpStatusCode = 0);
};

// ETL job processing exceptions
class ETLException : public BaseException {
public:
  ETLException(ErrorCode code, const std::string &message,
               const std::string &jobId = "");

  ETLException(ErrorCode code, const std::string &message,
               const ErrorContext &context, const std::string &jobId = "");
};

// Configuration exceptions
class ConfigException : public BaseException {
public:
  ConfigException(ErrorCode code, const std::string &message,
                  const std::string &configPath = "");

  ConfigException(ErrorCode code, const std::string &message,
                  const ErrorContext &context,
                  const std::string &configPath = "");
};

// Resource management exceptions
class ResourceException : public BaseException {
public:
  ResourceException(ErrorCode code, const std::string &message,
                    const std::string &resourceType = "");

  ResourceException(ErrorCode code, const std::string &message,
                    const ErrorContext &context,
                    const std::string &resourceType = "");
};

// System-level exceptions
class SystemException : public BaseException {
public:
  SystemException(ErrorCode code, const std::string &message,
                  const std::string &component = "");

  SystemException(ErrorCode code, const std::string &message,
                  const ErrorContext &context,
                  const std::string &component = "");
};

// Utility functions
std::string errorCodeToString(ErrorCode code);
std::string errorCategoryToString(ErrorCategory category);
std::string errorSeverityToString(ErrorSeverity severity);

ErrorCategory getErrorCategory(ErrorCode code);
ErrorSeverity getDefaultSeverity(ErrorCode code);

// Exception factory functions
std::shared_ptr<BaseException>
createValidationException(const std::string &message,
                          const std::string &field = "",
                          const std::string &value = "",
                          const ErrorContext &context = ErrorContext());

std::shared_ptr<BaseException>
createAuthException(ErrorCode code, const std::string &message,
                    const std::string &userId = "",
                    const ErrorContext &context = ErrorContext());

std::shared_ptr<BaseException>
createDatabaseException(ErrorCode code, const std::string &message,
                        const std::string &query = "",
                        const ErrorContext &context = ErrorContext());

// Exception chaining helpers
template <typename ExceptionType, typename... Args>
std::shared_ptr<ExceptionType>
chainException(std::shared_ptr<BaseException> cause, Args &&...args) {
  auto exception = std::make_shared<ExceptionType>(std::forward<Args>(args)...);
  exception->setCause(cause);
  return exception;
}

} // namespace ETLPlus::Exceptions
