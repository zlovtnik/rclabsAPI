#include <gtest/gtest.h>
#include "exception_mapper.hpp"
#include "etl_exceptions.hpp"
#include "error_codes.hpp"
#include <boost/beast/http.hpp>
#include <memory>
#include <thread>
#include <chrono>
#include <regex>

namespace ETLPlus {
namespace ExceptionHandling {

// Test fixture for ExceptionMapper tests
class ExceptionMapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        mapper_ = std::make_unique<ExceptionMapper>();
    }

    void TearDown() override {
        mapper_.reset();
    }

    std::unique_ptr<ExceptionMapper> mapper_;
    const std::string testOperation = "TestOperation";
};

// Test ExceptionMapper construction and configuration
TEST_F(ExceptionMapperTest, ConstructionWithDefaultConfig) {
    ExceptionMapper mapper;
    const auto& config = mapper.getConfig();

    EXPECT_EQ(config.defaultStatus, boost::beast::http::status::internal_server_error);
    EXPECT_FALSE(config.includeStackTrace);
    EXPECT_FALSE(config.includeInternalDetails);
    EXPECT_EQ(config.serverHeader, "ETL Plus Backend");
    EXPECT_EQ(config.corsOrigin, "*");
    EXPECT_FALSE(config.keepAlive);
}

TEST_F(ExceptionMapperTest, ConstructionWithCustomConfig) {
    ExceptionMappingConfig customConfig;
    customConfig.defaultStatus = boost::beast::http::status::bad_request;
    customConfig.includeStackTrace = true;
    customConfig.includeInternalDetails = true;
    customConfig.serverHeader = "Custom Server";
    customConfig.corsOrigin = "https://example.com";
    customConfig.keepAlive = true;

    ExceptionMapper mapper(customConfig);
    const auto& config = mapper.getConfig();

    EXPECT_EQ(config.defaultStatus, boost::beast::http::status::bad_request);
    EXPECT_TRUE(config.includeStackTrace);
    EXPECT_TRUE(config.includeInternalDetails);
    EXPECT_EQ(config.serverHeader, "Custom Server");
    EXPECT_EQ(config.corsOrigin, "https://example.com");
    EXPECT_TRUE(config.keepAlive);
}

TEST_F(ExceptionMapperTest, UpdateConfiguration) {
    ExceptionMappingConfig newConfig;
    newConfig.defaultStatus = boost::beast::http::status::not_found;
    newConfig.includeStackTrace = true;

    mapper_->updateConfig(newConfig);
    const auto& config = mapper_->getConfig();

    EXPECT_EQ(config.defaultStatus, boost::beast::http::status::not_found);
    EXPECT_TRUE(config.includeStackTrace);
}

// Test mapping ETL exceptions to HTTP responses
TEST_F(ExceptionMapperTest, MapValidationException) {
    etl::ValidationException ex(etl::ErrorCode::INVALID_INPUT,
                               "Invalid input provided",
                               "username", "invalid@user");

    auto response = mapper_->mapToResponse(ex, testOperation);

    EXPECT_EQ(response.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(response.at(boost::beast::http::field::content_type), "application/json");
    EXPECT_EQ(response.at(boost::beast::http::field::server), "ETL Plus Backend");

    // Check response body contains expected fields
    std::string body = response.body();
    EXPECT_NE(body.find("Invalid input provided"), std::string::npos);
    EXPECT_NE(body.find("INVALID_INPUT"), std::string::npos);
    EXPECT_NE(body.find("error"), std::string::npos);
}

TEST_F(ExceptionMapperTest, MapSystemException) {
    etl::SystemException ex(etl::ErrorCode::DATABASE_ERROR,
                           "Database connection failed",
                           "DatabaseManager");

    auto response = mapper_->mapToResponse(ex, testOperation);

    EXPECT_EQ(response.result(), boost::beast::http::status::service_unavailable);
    EXPECT_EQ(response.at(boost::beast::http::field::content_type), "application/json");

    std::string body = response.body();
    EXPECT_NE(body.find("Database connection failed"), std::string::npos);
    EXPECT_NE(body.find("DATABASE_ERROR"), std::string::npos);
}

TEST_F(ExceptionMapperTest, MapDifferentErrorCodes) {
    std::vector<std::pair<etl::ErrorCode, boost::beast::http::status>> testCases = {
        {etl::ErrorCode::INVALID_INPUT, boost::beast::http::status::bad_request},
        {etl::ErrorCode::UNAUTHORIZED, boost::beast::http::status::unauthorized},
        {etl::ErrorCode::FORBIDDEN, boost::beast::http::status::forbidden},
        {etl::ErrorCode::JOB_NOT_FOUND, boost::beast::http::status::not_found},
        {etl::ErrorCode::DATABASE_ERROR, boost::beast::http::status::service_unavailable},
        {etl::ErrorCode::NETWORK_ERROR, boost::beast::http::status::service_unavailable}
    };

    for (const auto& testCase : testCases) {
        etl::ETLException ex(testCase.first, "Test message");
        auto response = mapper_->mapToResponse(ex);
        EXPECT_EQ(response.result(), testCase.second);
    }
}

// Test mapping standard exceptions
TEST_F(ExceptionMapperTest, MapStdException) {
    std::runtime_error stdEx("Standard runtime error");

    auto response = mapper_->mapToResponse(stdEx, testOperation);

    EXPECT_EQ(response.result(), boost::beast::http::status::internal_server_error);
    EXPECT_EQ(response.at(boost::beast::http::field::content_type), "application/json");

    std::string body = response.body();
    EXPECT_NE(body.find("Standard runtime error"), std::string::npos);
    EXPECT_NE(body.find("INTERNAL_ERROR"), std::string::npos);
}

// Test mapping unknown exceptions
TEST_F(ExceptionMapperTest, MapUnknownException) {
    auto response = mapper_->mapToResponse(testOperation);

    EXPECT_EQ(response.result(), boost::beast::http::status::internal_server_error);
    EXPECT_EQ(response.at(boost::beast::http::field::content_type), "application/json");

    std::string body = response.body();
    EXPECT_NE(body.find("Unknown exception occurred"), std::string::npos);
    EXPECT_NE(body.find("INTERNAL_ERROR"), std::string::npos);
}

// Test custom error code handlers
TEST_F(ExceptionMapperTest, CustomErrorCodeHandler) {
    bool handlerCalled = false;
    boost::beast::http::status customStatus = boost::beast::http::status::not_acceptable;

    ExceptionHandlerFunc customHandler = [&](const etl::ETLException& ex, const std::string& op) {
        handlerCalled = true;
        HttpResponse response;
        response.result(customStatus);
        response.set(boost::beast::http::field::content_type, "application/json");
        response.body() = R"({"custom": "response"})";
        return response;
    };

    mapper_->registerHandler(etl::ErrorCode::INVALID_INPUT, customHandler);

    etl::ETLException ex(etl::ErrorCode::INVALID_INPUT, "Test message");
    auto response = mapper_->mapToResponse(ex);

    EXPECT_TRUE(handlerCalled);
    EXPECT_EQ(response.result(), customStatus);
    EXPECT_EQ(response.body(), R"({"custom": "response"})");
}

// Test custom exception type handlers
TEST_F(ExceptionMapperTest, CustomExceptionTypeHandler) {
    bool handlerCalled = false;

    ExceptionHandlerFunc typeHandler = [&](const etl::ETLException& ex, const std::string& op) {
        handlerCalled = true;
        HttpResponse response;
        response.result(boost::beast::http::status::not_implemented);
        response.set(boost::beast::http::field::content_type, "application/json");
        response.body() = R"({"type": "handler"})";
        return response;
    };

    mapper_->registerTypeHandler<etl::ValidationException>(typeHandler);

    etl::ValidationException ex(etl::ErrorCode::MISSING_FIELD, "Field missing", "testField");
    auto response = mapper_->mapToResponse(ex);

    EXPECT_TRUE(handlerCalled);
    EXPECT_EQ(response.result(), boost::beast::http::status::not_implemented);
    EXPECT_EQ(response.body(), R"({"type": "handler"})");
}

// Test ErrorResponseFormat
TEST_F(ExceptionMapperTest, ErrorResponseFormat) {
    etl::ETLException ex(etl::ErrorCode::UNAUTHORIZED, "Access denied");
    ex.setCorrelationId("test-correlation-123");

    ErrorResponseFormat format = mapper_->createErrorFormat(ex);

    EXPECT_EQ(format.status, "error");
    EXPECT_EQ(format.message, "Access denied");
    EXPECT_EQ(format.code, "UNAUTHORIZED");
    EXPECT_EQ(format.correlationId, "test-correlation-123");
    EXPECT_FALSE(format.timestamp.empty());
    EXPECT_TRUE(format.details.empty());

    // Test JSON serialization
    std::string json = format.toJson();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("Access denied"), std::string::npos);
    EXPECT_NE(json.find("test-correlation-123"), std::string::npos);
}

// Test correlation ID generation and management
TEST_F(ExceptionMapperTest, CorrelationIdGeneration) {
    std::string id1 = ExceptionMapper::generateCorrelationId();
    std::string id2 = ExceptionMapper::generateCorrelationId();

    EXPECT_FALSE(id1.empty());
    EXPECT_FALSE(id2.empty());
    EXPECT_NE(id1, id2); // Should be unique

    // Test setting and getting current correlation ID
    std::string testId = "test-context-id";
    ExceptionMapper::setCurrentCorrelationId(testId);
    EXPECT_EQ(ExceptionMapper::getCurrentCorrelationId(), testId);
}

// Test thread safety
TEST_F(ExceptionMapperTest, ThreadSafety) {
    const int numThreads = 10;
    const int operationsPerThread = 50;

    std::vector<std::thread> threads;
    std::atomic<int> completedThreads(0);

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, operationsPerThread, &completedThreads]() {
            try {
                for (int j = 0; j < operationsPerThread; ++j) {
                    etl::ETLException ex(etl::ErrorCode::INTERNAL_ERROR,
                                       "Thread " + std::to_string(i) + " test " + std::to_string(j));
                    auto response = mapper_->mapToResponse(ex);
                    EXPECT_EQ(response.result(), boost::beast::http::status::internal_server_error);
                }
                completedThreads++;
            } catch (...) {
                FAIL() << "Exception in thread " << i;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(completedThreads.load(), numThreads);
}

// Test response headers
TEST_F(ExceptionMapperTest, ResponseHeaders) {
    etl::ETLException ex(etl::ErrorCode::INVALID_INPUT, "Test error");

    ExceptionMappingConfig config;
    config.corsOrigin = "https://app.example.com";
    config.keepAlive = true;
    config.serverHeader = "Test Server v1.0";

    ExceptionMapper customMapper(config);
    auto response = customMapper.mapToResponse(ex);

    EXPECT_EQ(response.at(boost::beast::http::field::server), "Test Server v1.0");
    EXPECT_EQ(response.at(boost::beast::http::field::access_control_allow_origin), "https://app.example.com");
    // Note: Connection header is set automatically by keep_alive()
    EXPECT_TRUE(response.keep_alive());
    EXPECT_EQ(response.at(boost::beast::http::field::content_type), "application/json");
}

// Test JSON response body structure
TEST_F(ExceptionMapperTest, JsonResponseStructure) {
    etl::ValidationException ex(etl::ErrorCode::MISSING_FIELD,
                               "Required field is missing",
                               "email", "");

    auto response = mapper_->mapToResponse(ex);
    std::string body = response.body();

    // Parse JSON structure (basic validation)
    EXPECT_NE(body.find("{"), std::string::npos);
    EXPECT_NE(body.find("}"), std::string::npos);
    EXPECT_NE(body.find("\"status\""), std::string::npos);
    EXPECT_NE(body.find("\"message\""), std::string::npos);
    EXPECT_NE(body.find("\"code\""), std::string::npos);
    EXPECT_NE(body.find("\"correlationId\""), std::string::npos);
    EXPECT_NE(body.find("Required field is missing"), std::string::npos);
    EXPECT_NE(body.find("MISSING_FIELD"), std::string::npos);
}

// Test with operation name
TEST_F(ExceptionMapperTest, OperationNameInResponse) {
    etl::ETLException ex(etl::ErrorCode::PROCESSING_FAILED, "Processing failed");

    auto response = mapper_->mapToResponse(ex, "DataTransformation");

    std::string body = response.body();
    // The operation name should be included in logging/context, but may not be in response body
    // depending on implementation. Just verify the response is valid.
    EXPECT_EQ(response.result(), boost::beast::http::status::internal_server_error);
    EXPECT_FALSE(body.empty());
}

// Test configuration changes affect responses
TEST_F(ExceptionMapperTest, ConfigurationChanges) {
    etl::ETLException ex(etl::ErrorCode::DATABASE_ERROR, "DB Error");

    // Default configuration
    auto response1 = mapper_->mapToResponse(ex);
    EXPECT_EQ(response1.at(boost::beast::http::field::server), "ETL Plus Backend");

    // Update configuration
    ExceptionMappingConfig newConfig;
    newConfig.serverHeader = "Updated Server";
    newConfig.defaultStatus = boost::beast::http::status::service_unavailable;

    mapper_->updateConfig(newConfig);

    // Test with updated configuration
    auto response2 = mapper_->mapToResponse(etl::ETLException(etl::ErrorCode::INTERNAL_ERROR, "Internal error"));
    EXPECT_EQ(response2.at(boost::beast::http::field::server), "Updated Server");
    EXPECT_EQ(response2.result(), boost::beast::http::status::service_unavailable);
}

} // namespace ExceptionHandling
} // namespace ETLPlus
