#include "etl_exceptions.hpp"
#include <random>
#include <iomanip>
#include <sstream>

namespace etl {

// Generate unique correlation ID
std::string ETLException::generateCorrelationId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

// ETLException implementation
ETLException::ETLException(ErrorCode code, std::string message, ErrorContext context)
    : errorCode_(code), message_(std::move(message)), context_(std::move(context)),
      correlationId_(generateCorrelationId()), timestamp_(std::chrono::system_clock::now()) {
}

std::string ETLException::toLogString() const {
    std::stringstream ss;
    ss << "[" << correlationId_ << "] "
       << "ErrorCode=" << static_cast<int>(errorCode_) << " "
       << "Message=\"" << message_ << "\"";
    
    if (!context_.empty()) {
        ss << " Context={";
        bool first = true;
        for (const auto& [key, value] : context_) {
            if (!first) ss << ", ";
            ss << key << "=\"" << value << "\"";
            first = false;
        }
        ss << "}";
    }
    
    return ss.str();
}

std::string ETLException::toJsonString() const {
    std::stringstream ss;
    ss << "{"
       << "\"correlationId\":\"" << correlationId_ << "\","
       << "\"errorCode\":" << static_cast<int>(errorCode_) << ","
       << "\"message\":\"" << message_ << "\","
       << "\"timestamp\":\"" << std::chrono::duration_cast<std::chrono::milliseconds>(timestamp_.time_since_epoch()).count() << "\"";
    
    if (!context_.empty()) {
        ss << ",\"context\":{";
        bool first = true;
        for (const auto& [key, value] : context_) {
            if (!first) ss << ",";
            ss << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        ss << "}";
    }
    
    ss << "}";
    return ss.str();
}

void ETLException::addContext(const std::string& key, const std::string& value) {
    context_[key] = value;
}

void ETLException::setCorrelationId(const std::string& correlationId) {
    correlationId_ = correlationId;
}

// ValidationException implementation
ValidationException::ValidationException(ErrorCode code, std::string message, 
                                       std::string field, std::string value,
                                       ErrorContext context)
    : ETLException(code, std::move(message), std::move(context)),
      field_(std::move(field)), value_(std::move(value)) {
    
    if (!field_.empty()) {
        addContext("field", field_);
    }
    if (!value_.empty()) {
        addContext("value", value_);
    }
}

std::string ValidationException::toLogString() const {
    std::stringstream ss;
    ss << "[VALIDATION] " << ETLException::toLogString();
    if (!field_.empty()) {
        ss << " Field=\"" << field_ << "\"";
    }
    if (!value_.empty()) {
        ss << " Value=\"" << value_ << "\"";
    }
    return ss.str();
}

// SystemException implementation
SystemException::SystemException(ErrorCode code, std::string message, 
                               std::string component, ErrorContext context)
    : ETLException(code, std::move(message), std::move(context)),
      component_(std::move(component)) {
    
    if (!component_.empty()) {
        addContext("component", component_);
    }
}

std::string SystemException::toLogString() const {
    std::stringstream ss;
    ss << "[SYSTEM] " << ETLException::toLogString();
    if (!component_.empty()) {
        ss << " Component=\"" << component_ << "\"";
    }
    return ss.str();
}

// BusinessException implementation
BusinessException::BusinessException(ErrorCode code, std::string message, 
                                   std::string operation, ErrorContext context)
    : ETLException(code, std::move(message), std::move(context)),
      operation_(std::move(operation)) {
    
    if (!operation_.empty()) {
        addContext("operation", operation_);
    }
}

std::string BusinessException::toLogString() const {
    std::stringstream ss;
    ss << "[BUSINESS] " << ETLException::toLogString();
    if (!operation_.empty()) {
        ss << " Operation=\"" << operation_ << "\"";
    }
    return ss.str();
}

// Utility functions for exception handling

ValidationException createValidationError(const std::string& field, 
                                        const std::string& value,
                                        const std::string& reason) {
    ErrorContext context;
    context["reason"] = reason;
    return ValidationException(ErrorCode::INVALID_INPUT, 
                             "Validation failed: " + reason, 
                             field, value, context);
}

SystemException createSystemError(ErrorCode code, 
                                const std::string& component,
                                const std::string& details) {
    ErrorContext context;
    context["details"] = details;
    return SystemException(code, getErrorCodeDescription(code), component, context);
}

BusinessException createBusinessError(ErrorCode code,
                                    const std::string& operation,
                                    const std::string& details) {
    ErrorContext context;
    context["details"] = details;
    return BusinessException(code, getErrorCodeDescription(code), operation, context);
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