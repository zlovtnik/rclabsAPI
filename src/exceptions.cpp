#include "exceptions.hpp"
#include "logger.hpp"
#include <random>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace ETLPlus {
    namespace Exceptions {

        // Generate unique correlation ID
        std::string generateCorrelationId() {
            static thread_local std::random_device rd;
            static thread_local std::mt19937 gen(rd());
            static thread_local std::uniform_int_distribution<> dis(10000000, 99999999);
            
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            return "ETL-" + std::to_string(timestamp) + "-" + std::to_string(dis(gen));
        }

        // ErrorContext implementation
        ErrorContext::ErrorContext() 
            : correlationId(generateCorrelationId())
            , timestamp(std::chrono::system_clock::now()) {
        }

        ErrorContext::ErrorContext(const std::string& operation)
            : correlationId(generateCorrelationId())
            , operation(operation)
            , timestamp(std::chrono::system_clock::now()) {
        }

        void ErrorContext::addInfo(const std::string& key, const std::string& value) {
            additionalInfo[key] = value;
        }

        std::string ErrorContext::toString() const {
            std::ostringstream oss;
            auto time_t = std::chrono::system_clock::to_time_t(timestamp);
            
            oss << "CorrelationId: " << correlationId;
            if (!operation.empty()) oss << ", Operation: " << operation;
            if (!userId.empty()) oss << ", UserId: " << userId;
            if (!component.empty()) oss << ", Component: " << component;
            oss << ", Timestamp: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            
            if (!additionalInfo.empty()) {
                oss << ", Additional: {";
                bool first = true;
                for (const auto& [key, value] : additionalInfo) {
                    if (!first) oss << ", ";
                    oss << key << ": " << value;
                    first = false;
                }
                oss << "}";
            }
            
            return oss.str();
        }

        // BaseException implementation
        BaseException::BaseException(ErrorCode code, ErrorCategory category, ErrorSeverity severity,
                                   const std::string& message, const std::string& technicalDetails)
            : errorCode_(code)
            , category_(category)
            , severity_(severity)
            , message_(message)
            , technicalDetails_(technicalDetails)
            , context_() {
            captureStackTrace();
        }

        BaseException::BaseException(ErrorCode code, ErrorCategory category, ErrorSeverity severity,
                                   const std::string& message, const ErrorContext& context,
                                   const std::string& technicalDetails)
            : errorCode_(code)
            , category_(category)
            , severity_(severity)
            , message_(message)
            , technicalDetails_(technicalDetails)
            , context_(context) {
            captureStackTrace();
        }

        const char* BaseException::what() const noexcept {
            return message_.c_str();
        }

        void BaseException::setCause(std::shared_ptr<BaseException> cause) {
            cause_ = cause;
        }

        void BaseException::captureStackTrace() {
            // Simplified stack trace - in production, consider using libunwind or similar
            stackTrace_.push_back("BaseException::" + errorCodeToString(errorCode_));
            stackTrace_.push_back("Category: " + errorCategoryToString(category_));
            stackTrace_.push_back("Severity: " + errorSeverityToString(severity_));
        }

        std::string BaseException::toJsonString() const {
            std::ostringstream json;
            json << "{"
                 << "\"error_code\":" << static_cast<int>(errorCode_) << ","
                 << "\"error_name\":\"" << errorCodeToString(errorCode_) << "\","
                 << "\"category\":\"" << errorCategoryToString(category_) << "\","
                 << "\"severity\":\"" << errorSeverityToString(severity_) << "\","
                 << "\"message\":\"" << message_ << "\","
                 << "\"correlation_id\":\"" << context_.correlationId << "\","
                 << "\"timestamp\":\"" << context_.toString() << "\"";
            
            if (!technicalDetails_.empty()) {
                json << ",\"technical_details\":\"" << technicalDetails_ << "\"";
            }
            
            if (cause_) {
                json << ",\"caused_by\":" << cause_->toJsonString();
            }
            
            json << "}";
            return json.str();
        }

        std::string BaseException::toLogString() const {
            std::ostringstream log;
            log << "[" << errorCodeToString(errorCode_) << "] "
                << "[" << errorCategoryToString(category_) << "] "
                << "[" << errorSeverityToString(severity_) << "] "
                << message_;
            
            if (!context_.correlationId.empty()) {
                log << " [ID: " << context_.correlationId << "]";
            }
            
            if (!technicalDetails_.empty()) {
                log << " [Details: " << technicalDetails_ << "]";
            }
            
            if (cause_) {
                log << " [Caused by: " << cause_->toLogString() << "]";
            }
            
            return log.str();
        }

        // ValidationException implementation
        ValidationException::ValidationException(ErrorCode code, const std::string& message,
                                               const std::string& field, const std::string& value)
            : BaseException(code, ErrorCategory::VALIDATION, getDefaultSeverity(code), message) {
            if (!field.empty()) {
                context_.addInfo("field", field);
            }
            if (!value.empty()) {
                context_.addInfo("value", value);
            }
        }

        ValidationException::ValidationException(ErrorCode code, const std::string& message,
                                               const ErrorContext& context, const std::string& field,
                                               const std::string& value)
            : BaseException(code, ErrorCategory::VALIDATION, getDefaultSeverity(code), message, context) {
            if (!field.empty()) {
                context_.addInfo("field", field);
            }
            if (!value.empty()) {
                context_.addInfo("value", value);
            }
        }

        // AuthException implementation
        AuthException::AuthException(ErrorCode code, const std::string& message, const std::string& userId)
            : BaseException(code, ErrorCategory::AUTHENTICATION, getDefaultSeverity(code), message) {
            if (!userId.empty()) {
                context_.userId = userId;
            }
        }

        AuthException::AuthException(ErrorCode code, const std::string& message,
                                   const ErrorContext& context, const std::string& userId)
            : BaseException(code, ErrorCategory::AUTHENTICATION, getDefaultSeverity(code), message, context) {
            if (!userId.empty()) {
                context_.userId = userId;
            }
        }

        // DatabaseException implementation
        DatabaseException::DatabaseException(ErrorCode code, const std::string& message, const std::string& query)
            : BaseException(code, ErrorCategory::DATABASE, getDefaultSeverity(code), message) {
            if (!query.empty()) {
                context_.addInfo("query", query);
            }
        }

        DatabaseException::DatabaseException(ErrorCode code, const std::string& message,
                                           const ErrorContext& context, const std::string& query)
            : BaseException(code, ErrorCategory::DATABASE, getDefaultSeverity(code), message, context) {
            if (!query.empty()) {
                context_.addInfo("query", query);
            }
        }

        // NetworkException implementation
        NetworkException::NetworkException(ErrorCode code, const std::string& message, int httpStatusCode)
            : BaseException(code, ErrorCategory::NETWORK, getDefaultSeverity(code), message) {
            if (httpStatusCode > 0) {
                context_.addInfo("http_status", std::to_string(httpStatusCode));
            }
        }

        NetworkException::NetworkException(ErrorCode code, const std::string& message,
                                         const ErrorContext& context, int httpStatusCode)
            : BaseException(code, ErrorCategory::NETWORK, getDefaultSeverity(code), message, context) {
            if (httpStatusCode > 0) {
                context_.addInfo("http_status", std::to_string(httpStatusCode));
            }
        }

        // ETLException implementation
        ETLException::ETLException(ErrorCode code, const std::string& message, const std::string& jobId)
            : BaseException(code, ErrorCategory::ETL_PROCESSING, getDefaultSeverity(code), message) {
            if (!jobId.empty()) {
                context_.addInfo("job_id", jobId);
            }
        }

        ETLException::ETLException(ErrorCode code, const std::string& message,
                                 const ErrorContext& context, const std::string& jobId)
            : BaseException(code, ErrorCategory::ETL_PROCESSING, getDefaultSeverity(code), message, context) {
            if (!jobId.empty()) {
                context_.addInfo("job_id", jobId);
            }
        }

        // ConfigException implementation
        ConfigException::ConfigException(ErrorCode code, const std::string& message, const std::string& configPath)
            : BaseException(code, ErrorCategory::CONFIGURATION, getDefaultSeverity(code), message) {
            if (!configPath.empty()) {
                context_.addInfo("config_path", configPath);
            }
        }

        ConfigException::ConfigException(ErrorCode code, const std::string& message,
                                       const ErrorContext& context, const std::string& configPath)
            : BaseException(code, ErrorCategory::CONFIGURATION, getDefaultSeverity(code), message, context) {
            if (!configPath.empty()) {
                context_.addInfo("config_path", configPath);
            }
        }

        // ResourceException implementation
        ResourceException::ResourceException(ErrorCode code, const std::string& message, const std::string& resourceType)
            : BaseException(code, ErrorCategory::RESOURCE, getDefaultSeverity(code), message) {
            if (!resourceType.empty()) {
                context_.addInfo("resource_type", resourceType);
            }
        }

        ResourceException::ResourceException(ErrorCode code, const std::string& message,
                                           const ErrorContext& context, const std::string& resourceType)
            : BaseException(code, ErrorCategory::RESOURCE, getDefaultSeverity(code), message, context) {
            if (!resourceType.empty()) {
                context_.addInfo("resource_type", resourceType);
            }
        }

        // SystemException implementation
        SystemException::SystemException(ErrorCode code, const std::string& message, const std::string& component)
            : BaseException(code, ErrorCategory::SYSTEM, getDefaultSeverity(code), message) {
            if (!component.empty()) {
                context_.component = component;
            }
        }

        SystemException::SystemException(ErrorCode code, const std::string& message,
                                       const ErrorContext& context, const std::string& component)
            : BaseException(code, ErrorCategory::SYSTEM, getDefaultSeverity(code), message, context) {
            if (!component.empty()) {
                context_.component = component;
            }
        }

        // Utility functions implementation
        std::string errorCodeToString(ErrorCode code) {
            switch (code) {
                // Validation errors
                case ErrorCode::INVALID_INPUT: return "INVALID_INPUT";
                case ErrorCode::MISSING_REQUIRED_FIELD: return "MISSING_REQUIRED_FIELD";
                case ErrorCode::INVALID_FORMAT: return "INVALID_FORMAT";
                case ErrorCode::VALUE_OUT_OF_RANGE: return "VALUE_OUT_OF_RANGE";
                case ErrorCode::INVALID_TYPE: return "INVALID_TYPE";
                
                // Authentication errors
                case ErrorCode::INVALID_CREDENTIALS: return "INVALID_CREDENTIALS";
                case ErrorCode::TOKEN_EXPIRED: return "TOKEN_EXPIRED";
                case ErrorCode::TOKEN_INVALID: return "TOKEN_INVALID";
                case ErrorCode::INSUFFICIENT_PERMISSIONS: return "INSUFFICIENT_PERMISSIONS";
                case ErrorCode::ACCOUNT_LOCKED: return "ACCOUNT_LOCKED";
                
                // Database errors
                case ErrorCode::CONNECTION_FAILED: return "CONNECTION_FAILED";
                case ErrorCode::QUERY_FAILED: return "QUERY_FAILED";
                case ErrorCode::TRANSACTION_FAILED: return "TRANSACTION_FAILED";
                case ErrorCode::DEADLOCK_DETECTED: return "DEADLOCK_DETECTED";
                case ErrorCode::CONSTRAINT_VIOLATION: return "CONSTRAINT_VIOLATION";
                case ErrorCode::CONNECTION_TIMEOUT: return "CONNECTION_TIMEOUT";
                
                // Network errors
                case ErrorCode::REQUEST_TIMEOUT: return "REQUEST_TIMEOUT";
                case ErrorCode::CONNECTION_REFUSED: return "CONNECTION_REFUSED";
                case ErrorCode::INVALID_RESPONSE: return "INVALID_RESPONSE";
                case ErrorCode::RATE_LIMIT_EXCEEDED: return "RATE_LIMIT_EXCEEDED";
                case ErrorCode::SERVICE_UNAVAILABLE: return "SERVICE_UNAVAILABLE";
                
                // ETL Processing errors
                case ErrorCode::JOB_EXECUTION_FAILED: return "JOB_EXECUTION_FAILED";
                case ErrorCode::DATA_TRANSFORMATION_ERROR: return "DATA_TRANSFORMATION_ERROR";
                case ErrorCode::EXTRACT_FAILED: return "EXTRACT_FAILED";
                case ErrorCode::LOAD_FAILED: return "LOAD_FAILED";
                case ErrorCode::JOB_NOT_FOUND: return "JOB_NOT_FOUND";
                case ErrorCode::JOB_ALREADY_RUNNING: return "JOB_ALREADY_RUNNING";
                
                // Configuration errors
                case ErrorCode::CONFIG_NOT_FOUND: return "CONFIG_NOT_FOUND";
                case ErrorCode::CONFIG_PARSE_ERROR: return "CONFIG_PARSE_ERROR";
                case ErrorCode::INVALID_CONFIG_VALUE: return "INVALID_CONFIG_VALUE";
                case ErrorCode::MISSING_CONFIG_SECTION: return "MISSING_CONFIG_SECTION";
                
                // Resource errors
                case ErrorCode::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
                case ErrorCode::FILE_NOT_FOUND: return "FILE_NOT_FOUND";
                case ErrorCode::PERMISSION_DENIED: return "PERMISSION_DENIED";
                case ErrorCode::DISK_FULL: return "DISK_FULL";
                case ErrorCode::RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
                
                // System errors
                case ErrorCode::INTERNAL_ERROR: return "INTERNAL_ERROR";
                case ErrorCode::SERVICE_STARTUP_FAILED: return "SERVICE_STARTUP_FAILED";
                case ErrorCode::COMPONENT_UNAVAILABLE: return "COMPONENT_UNAVAILABLE";
                case ErrorCode::THREAD_POOL_EXHAUSTED: return "THREAD_POOL_EXHAUSTED";
                
                // Unknown
                case ErrorCode::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
                default: return "UNKNOWN_ERROR_CODE";
            }
        }

        std::string errorCategoryToString(ErrorCategory category) {
            switch (category) {
                case ErrorCategory::VALIDATION: return "VALIDATION";
                case ErrorCategory::AUTHENTICATION: return "AUTHENTICATION";
                case ErrorCategory::DATABASE: return "DATABASE";
                case ErrorCategory::NETWORK: return "NETWORK";
                case ErrorCategory::ETL_PROCESSING: return "ETL_PROCESSING";
                case ErrorCategory::CONFIGURATION: return "CONFIGURATION";
                case ErrorCategory::RESOURCE: return "RESOURCE";
                case ErrorCategory::SYSTEM: return "SYSTEM";
                case ErrorCategory::UNKNOWN: return "UNKNOWN";
                default: return "UNKNOWN_CATEGORY";
            }
        }

        std::string errorSeverityToString(ErrorSeverity severity) {
            switch (severity) {
                case ErrorSeverity::LOW: return "LOW";
                case ErrorSeverity::MEDIUM: return "MEDIUM";
                case ErrorSeverity::HIGH: return "HIGH";
                case ErrorSeverity::CRITICAL: return "CRITICAL";
                default: return "UNKNOWN_SEVERITY";
            }
        }

        ErrorCategory getErrorCategory(ErrorCode code) {
            int codeValue = static_cast<int>(code);
            if (codeValue >= 1000 && codeValue < 2000) return ErrorCategory::VALIDATION;
            if (codeValue >= 2000 && codeValue < 3000) return ErrorCategory::AUTHENTICATION;
            if (codeValue >= 3000 && codeValue < 4000) return ErrorCategory::DATABASE;
            if (codeValue >= 4000 && codeValue < 5000) return ErrorCategory::NETWORK;
            if (codeValue >= 5000 && codeValue < 6000) return ErrorCategory::ETL_PROCESSING;
            if (codeValue >= 6000 && codeValue < 7000) return ErrorCategory::CONFIGURATION;
            if (codeValue >= 7000 && codeValue < 8000) return ErrorCategory::RESOURCE;
            if (codeValue >= 8000 && codeValue < 9000) return ErrorCategory::SYSTEM;
            return ErrorCategory::UNKNOWN;
        }

        ErrorSeverity getDefaultSeverity(ErrorCode code) {
            switch (code) {
                // Critical errors
                case ErrorCode::OUT_OF_MEMORY:
                case ErrorCode::SERVICE_STARTUP_FAILED:
                case ErrorCode::COMPONENT_UNAVAILABLE:
                    return ErrorSeverity::CRITICAL;
                
                // High severity errors
                case ErrorCode::CONNECTION_FAILED:
                case ErrorCode::TRANSACTION_FAILED:
                case ErrorCode::DEADLOCK_DETECTED:
                case ErrorCode::JOB_EXECUTION_FAILED:
                case ErrorCode::INTERNAL_ERROR:
                    return ErrorSeverity::HIGH;
                
                // Medium severity errors
                case ErrorCode::QUERY_FAILED:
                case ErrorCode::DATA_TRANSFORMATION_ERROR:
                case ErrorCode::CONFIG_PARSE_ERROR:
                case ErrorCode::PERMISSION_DENIED:
                    return ErrorSeverity::MEDIUM;
                
                // Low severity errors (mostly validation)
                default:
                    return ErrorSeverity::LOW;
            }
        }

        // Factory functions
        std::shared_ptr<BaseException> createValidationException(
            const std::string& message, const std::string& field,
            const std::string& value, const ErrorContext& context) {
            return std::make_shared<ValidationException>(
                ErrorCode::INVALID_INPUT, message, context, field, value);
        }

        std::shared_ptr<BaseException> createAuthException(
            ErrorCode code, const std::string& message, const std::string& userId,
            const ErrorContext& context) {
            return std::make_shared<AuthException>(code, message, context, userId);
        }

        std::shared_ptr<BaseException> createDatabaseException(
            ErrorCode code, const std::string& message, const std::string& query,
            const ErrorContext& context) {
            return std::make_shared<DatabaseException>(code, message, context, query);
        }

    } // namespace Exceptions
} // namespace ETLPlus
