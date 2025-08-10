#include "etl_exceptions.hpp"
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>

namespace etl {

// Error code descriptions
const char* getErrorCodeDescription(ErrorCode code) {
    switch (code) {
        // Validation errors
        case ErrorCode::INVALID_INPUT: return "Invalid input provided";
        case ErrorCode::MISSING_FIELD: return "Required field is missing";
        case ErrorCode::INVALID_FORMAT: return "Invalid format";
        case ErrorCode::INVALID_RANGE: return "Value out of valid range";
        case ErrorCode::INVALID_TYPE: return "Invalid data type";
        case ErrorCode::CONSTRAINT_VIOLATION: return "Constraint violation";
        
        // Authentication/Authorization errors
        case ErrorCode::UNAUTHORIZED: return "Unauthorized access";
        case ErrorCode::FORBIDDEN: return "Access forbidden";
        case ErrorCode::TOKEN_EXPIRED: return "Authentication token expired";
        case ErrorCode::INVALID_CREDENTIALS: return "Invalid credentials";
        case ErrorCode::ACCESS_DENIED: return "Access denied";
        
        // System errors
        case ErrorCode::DATABASE_ERROR: return "Database operation failed";
        case ErrorCode::NETWORK_ERROR: return "Network operation failed";
        case ErrorCode::FILE_ERROR: return "File operation failed";
        case ErrorCode::MEMORY_ERROR: return "Memory allocation failed";
        case ErrorCode::LOCK_TIMEOUT: return "Lock acquisition timeout";
        case ErrorCode::RESOURCE_EXHAUSTED: return "System resource exhausted";
        case ErrorCode::CONFIGURATION_ERROR: return "Configuration error";
        
        // Business logic errors
        case ErrorCode::JOB_NOT_FOUND: return "Job not found";
        case ErrorCode::JOB_ALREADY_RUNNING: return "Job is already running";
        case ErrorCode::INVALID_JOB_STATE: return "Invalid job state";
        case ErrorCode::PROCESSING_FAILED: return "Processing operation failed";
        case ErrorCode::TRANSFORMATION_ERROR: return "Data transformation error";
        case ErrorCode::DATA_INTEGRITY_ERROR: return "Data integrity violation";
        case ErrorCode::WORKFLOW_ERROR: return "Workflow execution error";
        
        default: return "Unknown error";
    }
}

// ETLException implementation

ETLException::ETLException(ErrorCode code, std::string message, ErrorContext context)
    : errorCode_(code)
    , message_(std::move(message))
    , context_(std::move(context))
    , correlationId_(generateCorrelationId())
    , timestamp_(std::chrono::system_clock::now()) {
}

std::string ETLException::toLogString() const {
    std::ostringstream oss;
    oss << "ETLException[" 
        << "code=" << static_cast<int>(errorCode_) 
        << ", message=\"" << message_ << "\""
        << ", correlation_id=\"" << correlationId_ << "\"";
    
    if (!context_.empty()) {
        oss << ", context={";
        bool first = true;
        for (const auto& [key, value] : context_) {
            if (!first) oss << ", ";
            oss << key << "=\"" << value << "\"";
            first = false;
        }
        oss << "}";
    }
    
    oss << "]";
    return oss.str();
}

std::string ETLException::toJsonString() const {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"ETLException\","
        << "\"code\":" << static_cast<int>(errorCode_) << ","
        << "\"message\":\"" << message_ << "\","
        << "\"correlation_id\":\"" << correlationId_ << "\","
        << "\"timestamp\":\"";
    
    // Format timestamp as ISO 8601
    auto time_t = std::chrono::system_clock::to_time_t(timestamp_);
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    oss << "\"";
    
    if (!context_.empty()) {
        oss << ",\"context\":{";
        bool first = true;
        for (const auto& [key, value] : context_) {
            if (!first) oss << ",";
            oss << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        oss << "}";
    }
    
    oss << "}";
    return oss.str();
}

void ETLException::addContext(const std::string& key, const std::string& value) {
    context_[key] = value;
}

void ETLException::setCorrelationId(const std::string& correlationId) {
    correlationId_ = correlationId;
}

std::string ETLException::generateCorrelationId() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<> dis(0, 15);
    
    std::ostringstream oss;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            oss << '-';
        }
        oss << std::hex << dis(gen);
    }
    return oss.str();
}

// ValidationException implementation

ValidationException::ValidationException(ErrorCode code, std::string message, 
                                       std::string field, std::string value,
                                       ErrorContext context)
    : ETLException(code, std::move(message), std::move(context))
    , field_(std::move(field))
    , value_(std::move(value)) {
    
    // Add field and value to context if provided
    if (!field_.empty()) {
        addContext("field", field_);
    }
    if (!value_.empty()) {
        addContext("invalid_value", value_);
    }
}

std::string ValidationException::toLogString() const {
    std::ostringstream oss;
    oss << "ValidationException[" 
        << "code=" << static_cast<int>(errorCode_) 
        << ", message=\"" << message_ << "\""
        << ", correlation_id=\"" << correlationId_ << "\"";
    
    if (!field_.empty()) {
        oss << ", field=\"" << field_ << "\"";
    }
    if (!value_.empty()) {
        oss << ", value=\"" << value_ << "\"";
    }
    
    if (!context_.empty()) {
        oss << ", context={";
        bool first = true;
        for (const auto& [key, value] : context_) {
            if (!first) oss << ", ";
            oss << key << "=\"" << value << "\"";
            first = false;
        }
        oss << "}";
    }
    
    oss << "]";
    return oss.str();
}

// SystemException implementation

SystemException::SystemException(ErrorCode code, std::string message, 
                               std::string component, ErrorContext context)
    : ETLException(code, std::move(message), std::move(context))
    , component_(std::move(component)) {
    
    // Add component to context if provided
    if (!component_.empty()) {
        addContext("component", component_);
    }
}

std::string SystemException::toLogString() const {
    std::ostringstream oss;
    oss << "SystemException[" 
        << "code=" << static_cast<int>(errorCode_) 
        << ", message=\"" << message_ << "\""
        << ", correlation_id=\"" << correlationId_ << "\"";
    
    if (!component_.empty()) {
        oss << ", component=\"" << component_ << "\"";
    }
    
    if (!context_.empty()) {
        oss << ", context={";
        bool first = true;
        for (const auto& [key, value] : context_) {
            if (!first) oss << ", ";
            oss << key << "=\"" << value << "\"";
            first = false;
        }
        oss << "}";
    }
    
    oss << "]";
    return oss.str();
}

// BusinessException implementation

BusinessException::BusinessException(ErrorCode code, std::string message, 
                                   std::string operation, ErrorContext context)
    : ETLException(code, std::move(message), std::move(context))
    , operation_(std::move(operation)) {
    
    // Add operation to context if provided
    if (!operation_.empty()) {
        addContext("operation", operation_);
    }
}

std::string BusinessException::toLogString() const {
    std::ostringstream oss;
    oss << "BusinessException[" 
        << "code=" << static_cast<int>(errorCode_) 
        << ", message=\"" << message_ << "\""
        << ", correlation_id=\"" << correlationId_ << "\"";
    
    if (!operation_.empty()) {
        oss << ", operation=\"" << operation_ << "\"";
    }
    
    if (!context_.empty()) {
        oss << ", context={";
        bool first = true;
        for (const auto& [key, value] : context_) {
            if (!first) oss << ", ";
            oss << key << "=\"" << value << "\"";
            first = false;
        }
        oss << "}";
    }
    
    oss << "]";
    return oss.str();
}

// Utility functions

ValidationException createValidationError(const std::string& field, 
                                        const std::string& value,
                                        const std::string& reason) {
    std::ostringstream message;
    message << "Validation failed for field '" << field << "'";
    if (!value.empty()) {
        message << " with value '" << value << "'";
    }
    if (!reason.empty()) {
        message << ": " << reason;
    }
    
    return ValidationException(ErrorCode::INVALID_INPUT, message.str(), field, value);
}

SystemException createSystemError(ErrorCode code, 
                                const std::string& component,
                                const std::string& details) {
    std::ostringstream message;
    message << getErrorCodeDescription(code);
    if (!component.empty()) {
        message << " in component '" << component << "'";
    }
    if (!details.empty()) {
        message << ": " << details;
    }
    
    return SystemException(code, message.str(), component);
}

BusinessException createBusinessError(ErrorCode code,
                                    const std::string& operation,
                                    const std::string& details) {
    std::ostringstream message;
    message << getErrorCodeDescription(code);
    if (!operation.empty()) {
        message << " during operation '" << operation << "'";
    }
    if (!details.empty()) {
        message << ": " << details;
    }
    
    return BusinessException(code, message.str(), operation);
}

// Exception type checking

bool isValidationError(const std::exception& ex) {
    return dynamic_cast<const ValidationException*>(&ex) != nullptr;
}

bool isSystemError(const std::exception& ex) {
    return dynamic_cast<const SystemException*>(&ex) != nullptr;
}

bool isBusinessError(const std::exception& ex) {
    return dynamic_cast<const BusinessException*>(&ex) != nullptr;
}

} // namespace etl