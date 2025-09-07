#include "exception_mapper.hpp"
#include "etl_exceptions.hpp"
#include "logger.hpp"
#include <iostream>
#include <cassert>

using namespace ETLPlus::ExceptionHandling;

void testBasicExceptionMapping() {
    std::cout << "Testing basic exception mapping..." << std::endl;
    
    ExceptionMapper mapper;
    
    // Test validation exception
    auto validationEx = etl::ValidationException(
        etl::ErrorCode::INVALID_INPUT,
        "Invalid email format",
        "email",
        "invalid@",
        etl::ErrorContext{{"field", "email"}, {"value", "invalid@"}}
    );
    
    auto response = mapper.mapToResponse(validationEx, "test_validation");
    
    assert(response.result() == boost::beast::http::status::bad_request);
    assert(response[boost::beast::http::field::content_type] == "application/json");
    
    std::cout << "Validation exception mapped to: " << response.result() << std::endl;
    std::cout << "Response body: " << response.body() << std::endl;
}

void testSystemExceptionMapping() {
    std::cout << "\nTesting system exception mapping..." << std::endl;
    
    ExceptionMapper mapper;
    
    // Test system exception
    auto systemEx = etl::SystemException(
        etl::ErrorCode::DATABASE_ERROR,
        "Connection to database failed",
        "DatabaseManager",
        etl::ErrorContext{{"host", "localhost"}, {"port", "5432"}}
    );
    
    auto response = mapper.mapToResponse(systemEx, "test_system");
    
    assert(response.result() == boost::beast::http::status::service_unavailable);
    
    std::cout << "System exception mapped to: " << response.result() << std::endl;
    std::cout << "Response body: " << response.body() << std::endl;
}

void testBusinessExceptionMapping() {
    std::cout << "\nTesting business exception mapping..." << std::endl;
    
    ExceptionMapper mapper;
    
    // Test business exception
    auto businessEx = etl::BusinessException(
        etl::ErrorCode::JOB_NOT_FOUND,
        "Job with ID 12345 not found",
        "JobManager::getJob",
        etl::ErrorContext{{"jobId", "12345"}}
    );
    
    auto response = mapper.mapToResponse(businessEx, "test_business");
    
    assert(response.result() == boost::beast::http::status::not_found);
    
    std::cout << "Business exception mapped to: " << response.result() << std::endl;
    std::cout << "Response body: " << response.body() << std::endl;
}

void testCustomHandler() {
    std::cout << "\nTesting custom exception handler..." << std::endl;
    
    ExceptionMapper mapper;
    
    // Register custom handler for RATE_LIMIT_EXCEEDED
    mapper.registerHandler(etl::ErrorCode::RATE_LIMIT_EXCEEDED, 
        [](const etl::ETLException& ex, const std::string& operation) {
            HttpResponse response{boost::beast::http::status::too_many_requests, 11};
            response.set(boost::beast::http::field::content_type, "application/json");
            response.set(boost::beast::http::field::retry_after, "60");
            response.body() = R"({"error":"Rate limit exceeded","retryAfter":60})";
            response.prepare_payload();
            return response;
        });
    
    auto rateLimitEx = etl::SystemException(
        etl::ErrorCode::RATE_LIMIT_EXCEEDED,
        "API rate limit exceeded",
        "RateLimiter"
    );
    
    auto response = mapper.mapToResponse(rateLimitEx, "test_rate_limit");
    
    assert(response.result() == boost::beast::http::status::too_many_requests);
    assert(response[boost::beast::http::field::retry_after] == "60");
    
    std::cout << "Custom handler response: " << response.result() << std::endl;
    std::cout << "Response body: " << response.body() << std::endl;
}

void testCorrelationIdTracking() {
    std::cout << "\nTesting correlation ID tracking..." << std::endl;
    
    ExceptionMapper mapper;
    
    // Set a correlation ID
    std::string correlationId = ExceptionMapper::generateCorrelationId();
    ExceptionMapper::setCurrentCorrelationId(correlationId);
    
    auto ex = etl::SystemException(
        etl::ErrorCode::INTERNAL_ERROR,
        "Test exception with correlation ID"
    );
    
    // Set the correlation ID on the exception
    ex.setCorrelationId(correlationId);
    
    auto response = mapper.mapToResponse(ex, "test_correlation");
    
    // Check that correlation ID is in the response
    std::string body = response.body();
    assert(body.find(correlationId) != std::string::npos);
    
    std::cout << "Correlation ID: " << correlationId << std::endl;
    std::cout << "Response contains correlation ID: " << (body.find(correlationId) != std::string::npos) << std::endl;
}

void testStandardExceptionMapping() {
    std::cout << "\nTesting standard exception mapping..." << std::endl;
    
    ExceptionMapper mapper;
    
    // Test standard exception
    std::runtime_error stdEx("Standard runtime error");
    auto response = mapper.mapToResponse(stdEx, "test_standard");
    
    assert(response.result() == boost::beast::http::status::internal_server_error);
    
    std::cout << "Standard exception mapped to: " << response.result() << std::endl;
    std::cout << "Response body: " << response.body() << std::endl;
}

void testUtilityFunctions() {
    std::cout << "\nTesting utility functions..." << std::endl;
    
    // Test rate limit response
    auto rateLimitResponse = createRateLimitResponse("Too many requests", "120");
    assert(rateLimitResponse.result() == boost::beast::http::status::too_many_requests);
    assert(rateLimitResponse[boost::beast::http::field::retry_after] == "120");
    
    // Test maintenance response
    auto maintenanceResponse = createMaintenanceResponse("System maintenance in progress");
    assert(maintenanceResponse.result() == boost::beast::http::status::service_unavailable);
    
    std::cout << "Utility functions working correctly" << std::endl;
}

int main() {
    try {
        std::cout << "=== ExceptionMapper Test Suite ===" << std::endl;
        
        testBasicExceptionMapping();
        testSystemExceptionMapping();
        testBusinessExceptionMapping();
        testCustomHandler();
        testCorrelationIdTracking();
        testStandardExceptionMapping();
        testUtilityFunctions();
        
        std::cout << "\n=== All tests passed! ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
