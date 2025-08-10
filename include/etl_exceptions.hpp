#pragma once

#include <exception>
#include <string>
#include <unordered_map>
#include <chrono>
#include <sstream>
#include "error_codes.hpp"

namespace etl {

// Forward declarations
class ETLException;
class ValidationException;
class SystemException;
class BusinessException;

// Use the consolidated error codes from error_codes.hpp
// Error context for additional debugging information
using ErrorContext = std::unordered_map<std::string, std::string>;

// Base ETL exception class with error context and correlation ID support
class ETLException : public std::exception {
public:
    ETLException(ErrorCode code, std::string message, ErrorContext context = {});
    
    // Copy constructor and assignment
    ETLException(const ETLException& other) = default;
    ETLException& operator=(const ETLException& other) = default;
    
    // Move constructor and assignment
    ETLException(ETLException&& other) noexcept = default;
    ETLException& operator=(ETLException&& other) noexcept = default;
    
    virtual ~ETLException() = default;
    
    // Accessors
    ErrorCode getCode() const { return errorCode_; }
    const std::string& getMessage() const { return message_; }
    const ErrorContext& getContext() const { return context_; }
    const std::string& getCorrelationId() const { return correlationId_; }
    std::chrono::system_clock::time_point getTimestamp() const { return timestamp_; }
    
    // Standard exception interface
    const char* what() const noexcept override { return message_.c_str(); }
    
    // Serialization for logging
    virtual std::string toLogString() const;
    std::string toJsonString() const;
    
    // Context manipulation
    void addContext(const std::string& key, const std::string& value);
    void setCorrelationId(const std::string& correlationId);
    
protected:
    ErrorCode errorCode_;
    std::string message_;
    ErrorContext context_;
    std::string correlationId_;
    std::chrono::system_clock::time_point timestamp_;
    
    // Generate unique correlation ID
    static std::string generateCorrelationId();
};

// Validation exception for input validation errors
class ValidationException : public ETLException {
public:
    ValidationException(ErrorCode code, std::string message, 
                       std::string field = "", std::string value = "",
                       ErrorContext context = {});
    
    // Accessors for validation-specific information
    const std::string& getField() const { return field_; }
    const std::string& getValue() const { return value_; }
    
    // Enhanced logging for validation errors
    std::string toLogString() const override;
    
private:
    std::string field_;
    std::string value_;
};

// System exception for infrastructure and system-level errors
class SystemException : public ETLException {
public:
    SystemException(ErrorCode code, std::string message, 
                   std::string component = "",
                   ErrorContext context = {});
    
    // Accessors for system-specific information
    const std::string& getComponent() const { return component_; }
    
    // Enhanced logging for system errors
    std::string toLogString() const override;
    
private:
    std::string component_;
};

// Business exception for business logic and workflow errors
class BusinessException : public ETLException {
public:
    BusinessException(ErrorCode code, std::string message, 
                     std::string operation = "",
                     ErrorContext context = {});
    
    // Accessors for business-specific information
    const std::string& getOperation() const { return operation_; }
    
    // Enhanced logging for business errors
    std::string toLogString() const override;
    
private:
    std::string operation_;
};

// Utility functions for exception handling

// Create exceptions with common patterns
ValidationException createValidationError(const std::string& field, 
                                        const std::string& value,
                                        const std::string& reason);

SystemException createSystemError(ErrorCode code, 
                                const std::string& component,
                                const std::string& details);

BusinessException createBusinessError(ErrorCode code,
                                    const std::string& operation,
                                    const std::string& details);

// Exception type checking
bool isValidationError(const std::exception& ex);
bool isSystemError(const std::exception& ex);
bool isBusinessError(const std::exception& ex);

// Exception conversion utilities
template<typename ExceptionType>
const ExceptionType* asException(const std::exception& ex) {
    return dynamic_cast<const ExceptionType*>(&ex);
}

} // namespace etl