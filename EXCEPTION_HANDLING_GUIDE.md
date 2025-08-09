# ETL Plus Enhanced Exception Handling System

## Overview

This document describes the comprehensive exception handling system implemented for ETL Plus as part of Issue #13 "Proper exception handling". The system provides robust error handling with structured error responses, proper exception hierarchy, and enhanced debugging capabilities.

## Key Features

### ✅ Hierarchical Exception Types
- **BaseException**: Root exception class with common functionality
- **ValidationException**: Input validation and data format errors
- **AuthException**: Authentication and authorization errors
- **DatabaseException**: Database connection and query errors
- **NetworkException**: HTTP and network-related errors
- **ETLException**: ETL job processing errors
- **ConfigException**: Configuration loading and parsing errors
- **ResourceException**: Memory, file, and resource errors
- **SystemException**: System-level and internal errors

### ✅ Error Categorization and Codes
- **Error Categories**: VALIDATION, AUTHENTICATION, DATABASE, NETWORK, ETL_PROCESSING, CONFIGURATION, RESOURCE, SYSTEM
- **Error Codes**: Numeric codes (1000-9999) mapped to specific error types
- **Severity Levels**: LOW, MEDIUM, HIGH, CRITICAL for proper handling priority

### ✅ Enhanced Error Context
- **Correlation IDs**: Unique identifiers for tracking errors across logs
- **Timestamps**: Precise error occurrence timing
- **User Context**: User ID, operation name, component information
- **Additional Info**: Custom key-value pairs for specific error details

### ✅ Exception Chaining
- **Root Cause Analysis**: Link related exceptions to trace error origins
- **Cause Preservation**: Maintain original exception information while adding context
- **Structured Propagation**: Clean exception bubbling with context preservation

### ✅ Structured JSON Responses
- **API-Ready Format**: JSON serialization for HTTP error responses
- **Error Code Mapping**: Automatic HTTP status code assignment
- **Developer Information**: Technical details for debugging
- **User-Friendly Messages**: Clean error messages for end users

### ✅ Transaction Management (RAII)
- **TransactionScope**: Automatic database transaction handling
- **Exception Safety**: Guaranteed rollback on exceptions
- **Resource Cleanup**: RAII patterns for safe resource management

### ✅ Retry Logic and Policies
- **Configurable Retry**: Customizable retry attempts and delays
- **Exponential Backoff**: Smart delay calculation for transient errors
- **Retry Policies**: Intelligent decision making on retryable errors

## Implementation Details

### Error Code Ranges
```cpp
1000-1999: Validation errors
2000-2999: Authentication errors  
3000-3999: Database errors
4000-4999: Network errors
5000-5999: ETL Processing errors
6000-6999: Configuration errors
7000-7999: Resource errors
8000-8999: System errors
9000-9999: Unknown/Generic errors
```

### HTTP Status Code Mapping
```cpp
400 Bad Request:       Validation errors, invalid input
401 Unauthorized:      Authentication failures, invalid credentials
403 Forbidden:         Insufficient permissions
404 Not Found:         Resources not found
408 Request Timeout:   Connection timeouts
429 Too Many Requests: Rate limiting
500 Internal Error:    System and internal errors
503 Service Unavailable: Component unavailability
```

### Exception Context Structure
```cpp
struct ErrorContext {
    std::string correlationId;      // Unique tracking ID
    std::string userId;             // User context
    std::string operation;          // Operation being performed
    std::string component;          // Component name
    std::chrono::time_point timestamp; // Error occurrence time
    std::map<string, string> additionalInfo; // Custom context
};
```

## Usage Examples

### Basic Exception Creation
```cpp
// Validation error with field context
throw ValidationException(
    ErrorCode::INVALID_INPUT,
    "Username must be at least 3 characters",
    "username",
    "ab"
);

// Database error with query context  
throw DatabaseException(
    ErrorCode::CONNECTION_FAILED,
    "Failed to connect to database",
    "SELECT * FROM users"
);
```

### Exception Chaining
```cpp
try {
    // Some database operation
    dbManager->executeQuery("SELECT ...");
} catch (const DatabaseException& dbEx) {
    // Chain with higher-level context
    auto etlEx = ETLException(
        ErrorCode::JOB_EXECUTION_FAILED,
        "ETL job failed due to database error",
        jobId
    );
    etlEx.setCause(std::make_shared<DatabaseException>(dbEx));
    throw etlEx;
}
```

### Transaction Management
```cpp
// Safe database operations with automatic rollback
WITH_DATABASE_TRANSACTION(dbManager, "ProcessUserData", {
    dbManager->executeQuery("INSERT INTO users ...");
    dbManager->executeQuery("UPDATE user_stats ...");
    // Automatic commit on success, rollback on exception
});
```

### Exception Handling with Policies
```cpp
// Handle with logging but continue execution
auto result = ExceptionHandler::executeWithHandling(
    []() { return riskyOperation(); },
    ExceptionPolicy::LOG_AND_IGNORE,
    "OptionalOperation"
);

// Retry transient errors automatically
auto result = ExceptionHandler::executeWithRetry(
    []() { return networkOperation(); },
    RetryConfig{.maxAttempts = 3, .initialDelay = 100ms},
    "NetworkRequest"
);
```

### Structured Error Responses
```cpp
// In request handlers
try {
    processRequest(request);
} catch (const BaseException& ex) {
    return createExceptionResponse(ex); // Automatic JSON formatting
}
```

## Integration Points

### Request Handler Integration
- **Automatic Exception Catching**: Template-based exception handling in `handleRequest()`
- **HTTP Status Mapping**: Automatic conversion of exception codes to HTTP statuses
- **JSON Response Generation**: Structured error responses for API clients

### ETL Job Manager Integration
- **Job Failure Handling**: Proper exception capture and job status updates
- **Transaction Safety**: Database operations wrapped in transaction scopes
- **Error Context Propagation**: Job IDs and operation context in all exceptions

### Database Manager Integration
- **Connection Error Handling**: Specific exceptions for connection issues
- **Query Error Context**: SQL queries included in database exceptions
- **Transaction Management**: RAII-based transaction scopes

### Logger Integration
- **Severity-Based Logging**: Automatic log level selection based on exception severity
- **Structured Log Messages**: Consistent log formatting with correlation IDs
- **Component-Specific Logging**: Integration with existing component logging macros

## Error Response Format

### API Error Response Structure
```json
{
    "error_code": 1000,
    "error_name": "INVALID_INPUT",
    "category": "VALIDATION", 
    "severity": "LOW",
    "message": "Username must be at least 3 characters",
    "correlation_id": "ETL-1754701188002-55182385",
    "timestamp": "2025-08-08 20:59:48",
    "technical_details": "Field validation failed",
    "caused_by": {
        // Chained exception details
    }
}
```

### Log Message Format
```
[INVALID_INPUT] [VALIDATION] [LOW] Username must be at least 3 characters [ID: ETL-1754701188002-55182385] [Details: Field validation failed]
```

## Benefits

### For Developers
- **Clear Error Categories**: Easy identification of error types
- **Rich Context Information**: Detailed debugging information
- **Consistent Error Handling**: Standardized patterns across the codebase
- **Exception Safety**: RAII patterns prevent resource leaks

### For Operations
- **Correlation IDs**: Track errors across distributed systems
- **Structured Logging**: Easy parsing and analysis
- **Severity Levels**: Proper alerting and monitoring
- **Root Cause Analysis**: Exception chaining for debugging

### For API Clients
- **Structured Responses**: Programmatic error handling
- **Error Codes**: Specific handling for different error types
- **User-Friendly Messages**: Clean error presentation
- **Consistent Format**: Predictable error response structure

## Testing

The exception handling system includes comprehensive tests covering:
- ✅ Basic exception creation and properties
- ✅ Exception chaining and cause preservation
- ✅ Error context and correlation ID generation
- ✅ Utility function correctness
- ✅ Factory function behavior
- ✅ Exception hierarchy and polymorphism
- ✅ JSON serialization accuracy
- ✅ HTTP status code mapping

## Future Enhancements

### Potential Improvements
1. **Stack Trace Capture**: Integration with libunwind for detailed stack traces
2. **Metrics Integration**: Exception counting and alerting
3. **Error Recovery**: Automatic recovery strategies for specific error types
4. **Error Analytics**: Statistical analysis of error patterns
5. **Distributed Tracing**: Integration with distributed tracing systems

## Conclusion

The enhanced exception handling system provides a robust foundation for error management in ETL Plus. It addresses all aspects of Issue #13 by implementing:

1. **Proper Exception Hierarchy**: Type-safe exception handling with clear categorization
2. **Structured Error Responses**: Consistent API error formatting
3. **Enhanced Logging**: Detailed error logging with context
4. **Exception Safety**: RAII patterns and transaction management
5. **Developer Experience**: Easy-to-use APIs and clear error messages

This implementation significantly improves the reliability, maintainability, and debuggability of the ETL Plus system while providing excellent error handling capabilities for both developers and end users.
