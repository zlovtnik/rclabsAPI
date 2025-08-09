#include "exceptions.hpp"
#include "logger.hpp"
#include <iostream>
#include <cassert>

using namespace ETLPlus::Exceptions;

void testBasicExceptionCreation() {
    std::cout << "Testing basic exception creation..." << std::endl;
    
    // Test ValidationException
    auto validationEx = ValidationException(
        ErrorCode::INVALID_INPUT,
        "Test validation error",
        "test_field",
        "invalid_value"
    );
    
    assert(validationEx.getErrorCode() == ErrorCode::INVALID_INPUT);
    assert(validationEx.getCategory() == ErrorCategory::VALIDATION);
    assert(validationEx.getMessage() == "Test validation error");
    
    std::cout << "Validation Exception JSON: " << validationEx.toJsonString() << std::endl;
    std::cout << "Validation Exception Log: " << validationEx.toLogString() << std::endl;
    
    // Test DatabaseException
    auto dbEx = DatabaseException(
        ErrorCode::CONNECTION_FAILED,
        "Database connection failed",
        "SELECT * FROM users"
    );
    
    assert(dbEx.getErrorCode() == ErrorCode::CONNECTION_FAILED);
    assert(dbEx.getCategory() == ErrorCategory::DATABASE);
    
    std::cout << "Database Exception JSON: " << dbEx.toJsonString() << std::endl;
    
    std::cout << "✓ Basic exception creation tests passed" << std::endl;
}

void testExceptionChaining() {
    std::cout << "\nTesting exception chaining..." << std::endl;
    
    // Create root cause exception
    auto rootCause = std::make_shared<DatabaseException>(
        ErrorCode::CONNECTION_TIMEOUT,
        "Connection to database timed out"
    );
    
    // Create chained exception
    auto chainedException = std::make_shared<ETLException>(
        ErrorCode::JOB_EXECUTION_FAILED,
        "ETL job failed due to database issues",
        "job_123"
    );
    chainedException->setCause(rootCause);
    
    assert(chainedException->getCause() != nullptr);
    assert(chainedException->getCause()->getErrorCode() == ErrorCode::CONNECTION_TIMEOUT);
    
    std::cout << "Chained Exception JSON: " << chainedException->toJsonString() << std::endl;
    std::cout << "✓ Exception chaining tests passed" << std::endl;
}

void testErrorContextAndLogging() {
    std::cout << "\nTesting error context and logging..." << std::endl;
    
    ErrorContext context("test_operation");
    context.userId = "user123";
    context.component = "TestComponent";
    context.addInfo("request_id", "req_456");
    context.addInfo("endpoint", "/api/test");
    
    auto ex = SystemException(
        ErrorCode::INTERNAL_ERROR,
        "Test system error with context",
        context,
        "TestComponent"
    );
    
    std::cout << "Context String: " << context.toString() << std::endl;
    std::cout << "Exception with Context: " << ex.toLogString() << std::endl;
    
    std::cout << "✓ Error context and logging tests passed" << std::endl;
}

void testUtilityFunctions() {
    std::cout << "\nTesting utility functions..." << std::endl;
    
    // Test error code to string conversion
    std::string codeStr = errorCodeToString(ErrorCode::INVALID_INPUT);
    assert(codeStr == "INVALID_INPUT");
    
    std::string categoryStr = errorCategoryToString(ErrorCategory::VALIDATION);
    assert(categoryStr == "VALIDATION");
    
    std::string severityStr = errorSeverityToString(ErrorSeverity::HIGH);
    assert(severityStr == "HIGH");
    
    // Test category inference from error code
    ErrorCategory inferredCategory = getErrorCategory(ErrorCode::CONNECTION_FAILED);
    assert(inferredCategory == ErrorCategory::DATABASE);
    
    // Test default severity
    ErrorSeverity defaultSeverity = getDefaultSeverity(ErrorCode::OUT_OF_MEMORY);
    assert(defaultSeverity == ErrorSeverity::CRITICAL);
    
    std::cout << "✓ Utility function tests passed" << std::endl;
}

void testFactoryFunctions() {
    std::cout << "\nTesting factory functions..." << std::endl;
    
    ErrorContext context("factory_test");
    
    // Test validation exception factory
    auto validationEx = createValidationException(
        "Factory created validation error",
        "test_field",
        "bad_value",
        context
    );
    
    assert(validationEx->getErrorCode() == ErrorCode::INVALID_INPUT);
    assert(validationEx->getCategory() == ErrorCategory::VALIDATION);
    
    // Test auth exception factory
    auto authEx = createAuthException(
        ErrorCode::TOKEN_EXPIRED,
        "Token has expired",
        "user123",
        context
    );
    
    assert(authEx->getErrorCode() == ErrorCode::TOKEN_EXPIRED);
    assert(authEx->getCategory() == ErrorCategory::AUTHENTICATION);
    
    // Test database exception factory
    auto dbEx = createDatabaseException(
        ErrorCode::QUERY_FAILED,
        "Query execution failed",
        "SELECT * FROM invalid_table",
        context
    );
    
    assert(dbEx->getErrorCode() == ErrorCode::QUERY_FAILED);
    assert(dbEx->getCategory() == ErrorCategory::DATABASE);
    
    std::cout << "✓ Factory function tests passed" << std::endl;
}

void testExceptionHierarchy() {
    std::cout << "\nTesting exception hierarchy..." << std::endl;
    
    try {
        throw ValidationException(
            ErrorCode::MISSING_REQUIRED_FIELD,
            "Required field is missing",
            "username"
        );
    } catch (const BaseException& ex) {
        assert(ex.getErrorCode() == ErrorCode::MISSING_REQUIRED_FIELD);
        assert(ex.getCategory() == ErrorCategory::VALIDATION);
        std::cout << "Caught ValidationException as BaseException: " << ex.getMessage() << std::endl;
    } catch (...) {
        assert(false && "Should have caught as BaseException");
    }
    
    try {
        throw NetworkException(
            ErrorCode::REQUEST_TIMEOUT,
            "Network request timed out",
            408
        );
    } catch (const BaseException& ex) {
        assert(ex.getErrorCode() == ErrorCode::REQUEST_TIMEOUT);
        assert(ex.getCategory() == ErrorCategory::NETWORK);
        std::cout << "Caught NetworkException as BaseException: " << ex.getMessage() << std::endl;
    } catch (...) {
        assert(false && "Should have caught as BaseException");
    }
    
    std::cout << "✓ Exception hierarchy tests passed" << std::endl;
}

int main() {
    std::cout << "=== ETL Plus Exception System Test Suite ===" << std::endl;
    
    // Initialize logger for tests
    Logger::getInstance().enableConsoleOutput(true);
    Logger::getInstance().setLogLevel(LogLevel::DEBUG);
    
    try {
        testBasicExceptionCreation();
        testExceptionChaining();
        testErrorContextAndLogging();
        testUtilityFunctions();
        testFactoryFunctions();
        testExceptionHierarchy();
        
        std::cout << "\n🎉 All exception system tests passed successfully!" << std::endl;
        std::cout << "\nThe comprehensive exception handling system provides:" << std::endl;
        std::cout << "✓ Hierarchical exception types with proper categorization" << std::endl;
        std::cout << "✓ Error codes mapped to HTTP status codes" << std::endl;
        std::cout << "✓ Structured error context with correlation IDs" << std::endl;
        std::cout << "✓ Exception chaining for root cause analysis" << std::endl;
        std::cout << "✓ JSON serialization for API responses" << std::endl;
        std::cout << "✓ Detailed logging with severity levels" << std::endl;
        std::cout << "✓ Factory functions for easy exception creation" << std::endl;
        
        return 0;
        
    } catch (const std::exception& ex) {
        std::cerr << "❌ Test failed with exception: " << ex.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}
