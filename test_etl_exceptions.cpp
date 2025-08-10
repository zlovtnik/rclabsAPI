#include "etl_exceptions.hpp"
#include <iostream>
#include <cassert>
#include <stdexcept>

using namespace etl;

void testBasicETLException() {
    std::cout << "Testing basic ETLException functionality..." << std::endl;
    
    // Test basic construction
    ETLException ex(ErrorCode::INVALID_INPUT, "Test message");
    
    assert(ex.getCode() == ErrorCode::INVALID_INPUT);
    assert(ex.getMessage() == "Test message");
    assert(!ex.getCorrelationId().empty());
    assert(ex.getContext().empty());
    
    // Test with context
    ErrorContext context = {{"key1", "value1"}, {"key2", "value2"}};
    ETLException ex2(ErrorCode::DATABASE_ERROR, "DB error", context);
    
    assert(ex2.getContext().size() == 2);
    assert(ex2.getContext().at("key1") == "value1");
    assert(ex2.getContext().at("key2") == "value2");
    
    // Test context manipulation
    ex2.addContext("key3", "value3");
    assert(ex2.getContext().size() == 3);
    assert(ex2.getContext().at("key3") == "value3");
    
    // Test correlation ID setting
    ex2.setCorrelationId("custom-correlation-id");
    assert(ex2.getCorrelationId() == "custom-correlation-id");
    
    std::cout << "✓ Basic ETLException tests passed" << std::endl;
}

void testValidationException() {
    std::cout << "Testing ValidationException functionality..." << std::endl;
    
    // Test basic validation exception
    ValidationException ex(ErrorCode::MISSING_FIELD, "Field is required", "username", "");
    
    assert(ex.getCode() == ErrorCode::MISSING_FIELD);
    assert(ex.getMessage() == "Field is required");
    assert(ex.getField() == "username");
    assert(ex.getValue() == "");
    
    // Test with value
    ValidationException ex2(ErrorCode::INVALID_FORMAT, "Invalid email format", "email", "invalid-email");
    
    assert(ex2.getField() == "email");
    assert(ex2.getValue() == "invalid-email");
    
    // Test context is automatically populated
    assert(ex2.getContext().at("field") == "email");
    assert(ex2.getContext().at("invalid_value") == "invalid-email");
    
    // Test utility function
    auto utilEx = createValidationError("age", "150", "Age must be between 0 and 120");
    assert(utilEx.getField() == "age");
    assert(utilEx.getValue() == "150");
    assert(utilEx.getMessage().find("Validation failed for field 'age'") != std::string::npos);
    
    std::cout << "✓ ValidationException tests passed" << std::endl;
}

void testSystemException() {
    std::cout << "Testing SystemException functionality..." << std::endl;
    
    // Test basic system exception
    SystemException ex(ErrorCode::DATABASE_ERROR, "Connection failed", "DatabaseManager");
    
    assert(ex.getCode() == ErrorCode::DATABASE_ERROR);
    assert(ex.getMessage() == "Connection failed");
    assert(ex.getComponent() == "DatabaseManager");
    
    // Test context is automatically populated
    assert(ex.getContext().at("component") == "DatabaseManager");
    
    // Test utility function
    auto utilEx = createSystemError(ErrorCode::NETWORK_ERROR, "HttpClient", "Connection timeout");
    assert(utilEx.getComponent() == "HttpClient");
    assert(utilEx.getMessage().find("Network operation failed") != std::string::npos);
    assert(utilEx.getMessage().find("HttpClient") != std::string::npos);
    assert(utilEx.getMessage().find("Connection timeout") != std::string::npos);
    
    std::cout << "✓ SystemException tests passed" << std::endl;
}

void testBusinessException() {
    std::cout << "Testing BusinessException functionality..." << std::endl;
    
    // Test basic business exception
    BusinessException ex(ErrorCode::JOB_ALREADY_RUNNING, "Cannot start job", "data-processing");
    
    assert(ex.getCode() == ErrorCode::JOB_ALREADY_RUNNING);
    assert(ex.getMessage() == "Cannot start job");
    assert(ex.getOperation() == "data-processing");
    
    // Test context is automatically populated
    assert(ex.getContext().at("operation") == "data-processing");
    
    // Test utility function
    auto utilEx = createBusinessError(ErrorCode::PROCESSING_FAILED, "transform-data", "Invalid data format");
    assert(utilEx.getOperation() == "transform-data");
    assert(utilEx.getMessage().find("Processing operation failed") != std::string::npos);
    assert(utilEx.getMessage().find("transform-data") != std::string::npos);
    assert(utilEx.getMessage().find("Invalid data format") != std::string::npos);
    
    std::cout << "✓ BusinessException tests passed" << std::endl;
}

void testErrorCodes() {
    std::cout << "Testing error code descriptions..." << std::endl;
    
    // Test a few error codes
    assert(std::string(getErrorCodeDescription(ErrorCode::INVALID_INPUT)) == "Invalid input provided");
    assert(std::string(getErrorCodeDescription(ErrorCode::UNAUTHORIZED)) == "Unauthorized access");
    assert(std::string(getErrorCodeDescription(ErrorCode::DATABASE_ERROR)) == "Database operation failed");
    assert(std::string(getErrorCodeDescription(ErrorCode::JOB_NOT_FOUND)) == "Job not found");
    
    std::cout << "✓ Error code description tests passed" << std::endl;
}

void testSerialization() {
    std::cout << "Testing exception serialization..." << std::endl;
    
    // Test log string serialization
    ValidationException ex(ErrorCode::INVALID_FORMAT, "Invalid email", "email", "bad-email");
    ex.addContext("user_id", "12345");
    
    std::string logStr = ex.toLogString();
    assert(logStr.find("ValidationException") != std::string::npos);
    assert(logStr.find("1002") != std::string::npos); // Error code
    assert(logStr.find("Invalid email") != std::string::npos);
    assert(logStr.find("field=\"email\"") != std::string::npos);
    assert(logStr.find("value=\"bad-email\"") != std::string::npos);
    assert(logStr.find("user_id=\"12345\"") != std::string::npos);
    
    // Test JSON serialization
    std::string jsonStr = ex.toJsonString();
    assert(jsonStr.find("\"type\":\"ETLException\"") != std::string::npos);
    assert(jsonStr.find("\"code\":1002") != std::string::npos);
    assert(jsonStr.find("\"message\":\"Invalid email\"") != std::string::npos);
    assert(jsonStr.find("\"correlation_id\"") != std::string::npos);
    assert(jsonStr.find("\"timestamp\"") != std::string::npos);
    assert(jsonStr.find("\"context\"") != std::string::npos);
    
    std::cout << "✓ Serialization tests passed" << std::endl;
}

void testTypeChecking() {
    std::cout << "Testing exception type checking..." << std::endl;
    
    ValidationException valEx(ErrorCode::INVALID_INPUT, "Validation error");
    SystemException sysEx(ErrorCode::DATABASE_ERROR, "System error");
    BusinessException bizEx(ErrorCode::JOB_NOT_FOUND, "Business error");
    std::runtime_error stdEx("Standard error");
    
    // Test type checking functions
    assert(isValidationError(valEx) == true);
    assert(isSystemError(valEx) == false);
    assert(isBusinessError(valEx) == false);
    
    assert(isValidationError(sysEx) == false);
    assert(isSystemError(sysEx) == true);
    assert(isBusinessError(sysEx) == false);
    
    assert(isValidationError(bizEx) == false);
    assert(isSystemError(bizEx) == false);
    assert(isBusinessError(bizEx) == true);
    
    assert(isValidationError(stdEx) == false);
    assert(isSystemError(stdEx) == false);
    assert(isBusinessError(stdEx) == false);
    
    // Test template conversion function
    const ValidationException* valPtr = asException<ValidationException>(valEx);
    assert(valPtr != nullptr);
    assert(valPtr->getField().empty()); // Default constructed
    
    const SystemException* sysPtr = asException<SystemException>(valEx);
    assert(sysPtr == nullptr); // Wrong type
    
    std::cout << "✓ Type checking tests passed" << std::endl;
}

void testInheritance() {
    std::cout << "Testing exception inheritance..." << std::endl;
    
    ValidationException valEx(ErrorCode::INVALID_INPUT, "Validation error");
    SystemException sysEx(ErrorCode::DATABASE_ERROR, "System error");
    BusinessException bizEx(ErrorCode::JOB_NOT_FOUND, "Business error");
    
    // Test that all exceptions can be caught as ETLException
    try {
        throw valEx;
    } catch (const ETLException& ex) {
        assert(ex.getCode() == ErrorCode::INVALID_INPUT);
        assert(ex.getMessage() == "Validation error");
    } catch (...) {
        assert(false && "Should have caught as ETLException");
    }
    
    try {
        throw sysEx;
    } catch (const ETLException& ex) {
        assert(ex.getCode() == ErrorCode::DATABASE_ERROR);
        assert(ex.getMessage() == "System error");
    } catch (...) {
        assert(false && "Should have caught as ETLException");
    }
    
    try {
        throw bizEx;
    } catch (const ETLException& ex) {
        assert(ex.getCode() == ErrorCode::JOB_NOT_FOUND);
        assert(ex.getMessage() == "Business error");
    } catch (...) {
        assert(false && "Should have caught as ETLException");
    }
    
    // Test that all exceptions can be caught as std::exception
    try {
        throw valEx;
    } catch (const std::exception& ex) {
        assert(std::string(ex.what()) == "Validation error");
    } catch (...) {
        assert(false && "Should have caught as std::exception");
    }
    
    std::cout << "✓ Inheritance tests passed" << std::endl;
}

int main() {
    std::cout << "ETL Exception System Test Suite" << std::endl;
    std::cout << "================================" << std::endl;
    
    try {
        testBasicETLException();
        testValidationException();
        testSystemException();
        testBusinessException();
        testErrorCodes();
        testSerialization();
        testTypeChecking();
        testInheritance();
        
        std::cout << std::endl;
        std::cout << "🎉 All tests passed! Exception system is working correctly." << std::endl;
        std::cout << std::endl;
        
        // Demonstrate usage examples
        std::cout << "Usage Examples:" << std::endl;
        std::cout << "===============" << std::endl;
        
        // Example 1: Validation error
        try {
            throw createValidationError("email", "invalid-email", "Must be valid email format");
        } catch (const ValidationException& ex) {
            std::cout << "Validation Error: " << ex.toLogString() << std::endl;
        }
        
        // Example 2: System error
        try {
            throw createSystemError(ErrorCode::DATABASE_ERROR, "ConnectionPool", "Max connections exceeded");
        } catch (const SystemException& ex) {
            std::cout << "System Error: " << ex.toLogString() << std::endl;
        }
        
        // Example 3: Business error
        try {
            throw createBusinessError(ErrorCode::JOB_ALREADY_RUNNING, "data-transform", "Job ID: job-123");
        } catch (const BusinessException& ex) {
            std::cout << "Business Error: " << ex.toLogString() << std::endl;
        }
        
        return 0;
        
    } catch (const std::exception& ex) {
        std::cerr << "Test failed with exception: " << ex.what() << std::endl;
        return 1;
    }
}