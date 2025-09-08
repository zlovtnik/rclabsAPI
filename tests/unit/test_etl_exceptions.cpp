#include <gtest/gtest.h>
#include "etl_exceptions.hpp"
#include "error_codes.hpp"
#include <stdexcept>
#include <unordered_map>
#include <chrono>
#include <thread>

namespace etl {

// Test fixture for exception tests
class ExceptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test ETLException basic functionality
TEST_F(ExceptionTest, ETLExceptionConstruction) {
    std::string testMessage = "Test error message";
    ErrorContext context = {{"key1", "value1"}, {"key2", "value2"}};

    ETLException ex(ErrorCode::INVALID_INPUT, testMessage, context);

    EXPECT_EQ(ex.getCode(), ErrorCode::INVALID_INPUT);
    EXPECT_EQ(ex.getMessage(), testMessage);
    EXPECT_STREQ(ex.what(), testMessage.c_str());

    const auto& returnedContext = ex.getContext();
    EXPECT_EQ(returnedContext.size(), 2);
    EXPECT_EQ(returnedContext.at("key1"), "value1");
    EXPECT_EQ(returnedContext.at("key2"), "value2");

    // Check that timestamp is set
    auto now = std::chrono::system_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(now - ex.getTimestamp());
    EXPECT_LT(timeDiff.count(), 5); // Within 5 seconds of now

    // Check correlation ID is generated
    EXPECT_FALSE(ex.getCorrelationId().empty());
}

TEST_F(ExceptionTest, ETLExceptionCopyAndMove) {
    ETLException original(ErrorCode::DATABASE_ERROR, "Original message");
    std::string originalCorrelationId = original.getCorrelationId();

    // Test copy constructor
    ETLException copy = original;
    EXPECT_EQ(copy.getCode(), ErrorCode::DATABASE_ERROR);
    EXPECT_EQ(copy.getMessage(), "Original message");
    EXPECT_EQ(copy.getCorrelationId(), originalCorrelationId);

    // Test copy assignment
    ETLException copyAssign(ErrorCode::INVALID_INPUT, "Different message");
    copyAssign = original;
    EXPECT_EQ(copyAssign.getCode(), ErrorCode::DATABASE_ERROR);
    EXPECT_EQ(copyAssign.getMessage(), "Original message");

    // Test move constructor
    ETLException moveSource(ErrorCode::NETWORK_ERROR, "Move source");
    std::string moveSourceId = moveSource.getCorrelationId();
    ETLException moveDest = std::move(moveSource);
    EXPECT_EQ(moveDest.getCode(), ErrorCode::NETWORK_ERROR);
    EXPECT_EQ(moveDest.getMessage(), "Move source");
    EXPECT_EQ(moveDest.getCorrelationId(), moveSourceId);

    // Test move assignment
    ETLException moveAssignSource(ErrorCode::FILE_ERROR, "Move assign source");
    ETLException moveAssignDest(ErrorCode::INVALID_INPUT, "Different");
    moveAssignDest = std::move(moveAssignSource);
    EXPECT_EQ(moveAssignDest.getCode(), ErrorCode::FILE_ERROR);
    EXPECT_EQ(moveAssignDest.getMessage(), "Move assign source");
}

TEST_F(ExceptionTest, ETLExceptionContextManipulation) {
    ETLException ex(ErrorCode::CONFIGURATION_ERROR, "Config error");

    // Initially empty context
    EXPECT_TRUE(ex.getContext().empty());

    // Add context
    ex.addContext("component", "ConfigManager");
    ex.addContext("file", "config.json");

    EXPECT_EQ(ex.getContext().size(), 2);
    EXPECT_EQ(ex.getContext().at("component"), "ConfigManager");
    EXPECT_EQ(ex.getContext().at("file"), "config.json");

    // Set correlation ID
    std::string testId = "test-correlation-123";
    ex.setCorrelationId(testId);
    EXPECT_EQ(ex.getCorrelationId(), testId);
}

TEST_F(ExceptionTest, ETLExceptionSerialization) {
    ErrorContext context = {{"user", "testuser"}, {"action", "login"}};
    ETLException ex(ErrorCode::UNAUTHORIZED, "Authentication failed", context);
    ex.setCorrelationId("test-123");

    // Test JSON serialization
    std::string jsonStr = ex.toJsonString();
    EXPECT_FALSE(jsonStr.empty());

    // JSON should contain key elements
    EXPECT_NE(jsonStr.find("2000"), std::string::npos);  // UNAUTHORIZED = 2000
    EXPECT_NE(jsonStr.find("Authentication failed"), std::string::npos);
    EXPECT_NE(jsonStr.find("test-123"), std::string::npos);
    EXPECT_NE(jsonStr.find("testuser"), std::string::npos);

    // Test log string
    std::string logStr = ex.toLogString();
    EXPECT_FALSE(logStr.empty());
    EXPECT_NE(logStr.find("Authentication failed"), std::string::npos);
}

// Test ValidationException
TEST_F(ExceptionTest, ValidationExceptionConstruction) {
    std::string field = "username";
    std::string value = "invalid@format";
    ErrorContext context = {{"validation_rule", "email_format"}};

    ValidationException ex(ErrorCode::INVALID_INPUT, "Invalid email format",
                          field, value, context);

    // Test base class properties
    EXPECT_EQ(ex.getCode(), ErrorCode::INVALID_INPUT);
    EXPECT_EQ(ex.getMessage(), "Invalid email format");

    // Test validation-specific properties
    EXPECT_EQ(ex.getField(), field);
    EXPECT_EQ(ex.getValue(), value);

    // Test context
    EXPECT_EQ(ex.getContext().at("validation_rule"), "email_format");
}

TEST_F(ExceptionTest, ValidationExceptionLogString) {
    ValidationException ex(ErrorCode::MISSING_FIELD, "Required field missing",
                          "email", "", {{"required", "true"}});

    std::string logStr = ex.toLogString();
    EXPECT_FALSE(logStr.empty());

    // Should include field information
    EXPECT_NE(logStr.find("email"), std::string::npos);
    EXPECT_NE(logStr.find("Required field missing"), std::string::npos);
}

// Test SystemException
TEST_F(ExceptionTest, SystemExceptionConstruction) {
    std::string component = "DatabaseManager";
    ErrorContext context = {{"operation", "connect"}, {"timeout", "30s"}};

    SystemException ex(ErrorCode::DATABASE_ERROR, "Connection timeout",
                      component, context);

    // Test base class properties
    EXPECT_EQ(ex.getCode(), ErrorCode::DATABASE_ERROR);
    EXPECT_EQ(ex.getMessage(), "Connection timeout");

    // Test system-specific properties
    EXPECT_EQ(ex.getComponent(), component);

    // Test context
    EXPECT_EQ(ex.getContext().at("operation"), "connect");
    EXPECT_EQ(ex.getContext().at("timeout"), "30s");
}

TEST_F(ExceptionTest, SystemExceptionLogString) {
    SystemException ex(ErrorCode::NETWORK_ERROR, "Service unavailable",
                      "HttpServer", {{"endpoint", "/api/jobs"}});

    std::string logStr = ex.toLogString();
    EXPECT_FALSE(logStr.empty());

    // Should include component information
    EXPECT_NE(logStr.find("HttpServer"), std::string::npos);
    EXPECT_NE(logStr.find("Service unavailable"), std::string::npos);
}

// Test exception hierarchy
TEST_F(ExceptionTest, ExceptionHierarchy) {
    ValidationException validationEx(ErrorCode::INVALID_RANGE, "Value out of range");
    SystemException systemEx(ErrorCode::MEMORY_ERROR, "Out of memory");

    // Test that they are instances of ETLException
    EXPECT_THROW(throw validationEx, ETLException);
    EXPECT_THROW(throw systemEx, ETLException);

    // Test that they are also standard exceptions
    EXPECT_THROW(throw validationEx, std::exception);
    EXPECT_THROW(throw systemEx, std::exception);
}

// Test error code coverage
TEST_F(ExceptionTest, ErrorCodeCoverage) {
    // Test various error codes work with exceptions
    std::vector<ErrorCode> testCodes = {
        ErrorCode::INVALID_INPUT,
        ErrorCode::MISSING_FIELD,
        ErrorCode::UNAUTHORIZED,
        ErrorCode::DATABASE_ERROR,
        ErrorCode::NETWORK_ERROR,
        ErrorCode::JOB_NOT_FOUND,
        ErrorCode::PROCESSING_FAILED
    };

    for (auto code : testCodes) {
        ETLException ex(code, "Test message");
        EXPECT_EQ(ex.getCode(), code);
    }
}

// Test thread safety of exception creation
TEST_F(ExceptionTest, ThreadSafety) {
    const int numThreads = 10;
    const int exceptionsPerThread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> completedThreads(0);

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i, exceptionsPerThread, &completedThreads]() {
            try {
                for (int j = 0; j < exceptionsPerThread; ++j) {
                    ETLException ex(ErrorCode::INTERNAL_ERROR,
                                  "Thread " + std::to_string(i) + " exception " + std::to_string(j));
                    // Just create and discard - testing that creation is thread-safe
                    (void)ex;
                }
                completedThreads++;
            } catch (...) {
                // Should not happen
                FAIL() << "Exception creation failed in thread " << i;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(completedThreads.load(), numThreads);
}

// Test exception context with complex data
TEST_F(ExceptionTest, ComplexContext) {
    ErrorContext complexContext = {
        {"user_id", "12345"},
        {"session_id", "sess_abc123"},
        {"request_id", "req_xyz789"},
        {"timestamp", "2024-01-15T10:30:00Z"},
        {"user_agent", "Mozilla/5.0 (compatible; TestClient/1.0)"},
        {"ip_address", "192.168.1.100"}
    };

    ETLException ex(ErrorCode::ACCESS_DENIED, "Access denied for user", complexContext);

    const auto& context = ex.getContext();
    EXPECT_EQ(context.size(), 6);
    EXPECT_EQ(context.at("user_id"), "12345");
    EXPECT_EQ(context.at("ip_address"), "192.168.1.100");

    // Test JSON serialization includes all context
    std::string jsonStr = ex.toJsonString();
    for (const auto& pair : complexContext) {
        EXPECT_NE(jsonStr.find(pair.second), std::string::npos);
    }
}

// Test exception chaining (simulated)
TEST_F(ExceptionTest, ExceptionChaining) {
    // Create a chain of exceptions
    try {
        try {
            throw ValidationException(ErrorCode::INVALID_INPUT, "Invalid input data",
                                    "field1", "badvalue");
        } catch (const ETLException& inner) {
            ErrorContext context = inner.getContext();
            context["wrapping_component"] = "RequestHandler";
            throw SystemException(ErrorCode::PROCESSING_FAILED,
                                "Failed to process request: " + std::string(inner.what()),
                                "RequestHandler", context);
        }
    } catch (const SystemException& outer) {
        EXPECT_EQ(outer.getCode(), ErrorCode::PROCESSING_FAILED);
        EXPECT_EQ(outer.getComponent(), "RequestHandler");

        // Should contain both original and wrapping context
        const auto& context = outer.getContext();
        EXPECT_EQ(context.at("wrapping_component"), "RequestHandler");
    }
}

} // namespace etl
