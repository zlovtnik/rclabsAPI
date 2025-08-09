#include "exceptions.hpp"
#include "exception_handler.hpp"
#include "logger.hpp"
#include <iostream>
#include <cassert>

using namespace ETLPlus::Exceptions;
using namespace ETLPlus::ExceptionHandling;

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

void testExceptionHandler() {
    std::cout << "\nTesting exception handler..." << std::endl;
    
    // Test executeWithHandling - success case
    auto result = ExceptionHandler::executeWithHandling(
        []() { return 42; },
        ExceptionPolicy::PROPAGATE,
        "test_operation"
    );
    assert(result == 42);
    
    // Test executeWithHandling - exception case with LOG_AND_RETURN policy
    auto resultWithException = ExceptionHandler::executeWithHandling(
        []() -> int {
            throw ValidationException(ErrorCode::INVALID_INPUT, "Test error");
            return 42;  // This won't be reached
        },
        ExceptionPolicy::LOG_AND_RETURN,
        "test_operation_with_error"
    );
    assert(resultWithException == 0);  // Default value for int
    
    std::cout << "✓ Exception handler tests passed" << std::endl;
}

void testRetryLogic() {
    std::cout << "\nTesting retry logic..." << std::endl;
    
    RetryConfig config;
    config.maxAttempts = 3;
    config.initialDelay = std::chrono::milliseconds(10);
    config.shouldRetry = [](const BaseException& ex) {
        return ex.getErrorCode() == ErrorCode::CONNECTION_TIMEOUT;
    };
    
    int attemptCount = 0;
    
    try {
        auto result = ExceptionHandler::executeWithRetry(
            [&attemptCount]() -> int {
                attemptCount++;
                if (attemptCount < 3) {
                    throw DatabaseException(ErrorCode::CONNECTION_TIMEOUT, "Timeout on attempt " + std::to_string(attemptCount));
                }
                return 100;  // Success on third attempt
            },
            config,
            "retry_test"
        );
        
        assert(result == 100);
        assert(attemptCount == 3);
        std::cout << "✓ Retry logic succeeded after " << attemptCount << " attempts" << std::endl;
        
    } catch (const BaseException& ex) {
        std::cout << "Unexpected exception in retry test: " << ex.toLogString() << std::endl;
        assert(false);
    }
    
    // Test retry with non-retryable exception
    attemptCount = 0;
    try {
        ExceptionHandler::executeWithRetry(
            [&attemptCount]() -> int {
                attemptCount++;
                throw ValidationException(ErrorCode::INVALID_INPUT, "Non-retryable error");
                return 100;
            },
            config,
            "non_retry_test"
        );
        assert(false);  // Should not reach here
    } catch (const ValidationException& ex) {
        assert(attemptCount == 1);  // Should only attempt once
        std::cout << "✓ Non-retryable exception handled correctly" << std::endl;
    }
    
    std::cout << "✓ Retry logic tests passed" << std::endl;
}

void testConvertException() {
    std::cout << "\nTesting standard exception conversion..." << std::endl;
    
    // Test converting standard exception
    try {
        throw std::runtime_error("Standard runtime error");
    } catch (const std::exception& ex) {
        auto convertedEx = ExceptionHandler::convertException(ex, "test_conversion");
        
        assert(convertedEx->getCategory() == ErrorCategory::SYSTEM);
        assert(convertedEx->getErrorCode() == ErrorCode::INTERNAL_ERROR);
        assert(convertedEx->getMessage().find("Standard runtime error") != std::string::npos);
        
        std::cout << "Converted Exception: " << convertedEx->toLogString() << std::endl;
    }
    
    // Test converting database-related exception
    try {
        throw std::runtime_error("Database connection failed");
    } catch (const std::exception& ex) {
        auto convertedEx = ExceptionHandler::convertException(ex, "db_test");
        
        assert(convertedEx->getCategory() == ErrorCategory::DATABASE);
        assert(convertedEx->getErrorCode() == ErrorCode::QUERY_FAILED);
        
        std::cout << "Converted DB Exception: " << convertedEx->toLogString() << std::endl;
    }
    
    std::cout << "✓ Exception conversion tests passed" << std::endl;
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
    
    // Test logging at different severity levels
    ExceptionHandler::logException(ex, "context_test");
    
    std::cout << "✓ Error context and logging tests passed" << std::endl;
}

int main() {
    std::cout << "=== ETL Plus Exception Handling Test Suite ===" << std::endl;
    
    // Initialize logger for tests
    Logger::getInstance().enableConsoleOutput(true);
    Logger::getInstance().setLogLevel(LogLevel::DEBUG);
    
    try {
        testBasicExceptionCreation();
        testExceptionChaining();
        testExceptionHandler();
        testRetryLogic();
        testConvertException();
        testErrorContextAndLogging();
        
        std::cout << "\n🎉 All exception handling tests passed successfully!" << std::endl;
        return 0;
        
    } catch (const std::exception& ex) {
        std::cerr << "❌ Test failed with exception: " << ex.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}
