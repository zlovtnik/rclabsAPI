#include "request_handler.hpp"
#include "database_manager.hpp"
#include "auth_manager.hpp"
#include "etl_job_manager.hpp"
#include "exception_mapper.hpp"
#include "logger.hpp"
#include <boost/beast/http.hpp>
#include <iostream>
#include <memory>

using namespace boost::beast::http;

void testRequestHandlerWithExceptionMapper() {
    std::cout << "=== Testing Refactored RequestHandler with ExceptionMapper ===" << std::endl;
    
    // Create mock components (null pointers for testing)
    auto dbManager = std::shared_ptr<DatabaseManager>(nullptr);
    auto authManager = std::shared_ptr<AuthManager>(nullptr);
    auto etlManager = std::shared_ptr<ETLJobManager>(nullptr);
    
    // Create RequestHandler with ExceptionMapper
    RequestHandler handler(dbManager, authManager, etlManager);
    
    // Test 1: Invalid method for auth endpoint
    std::cout << "\n--- Test 1: Invalid method for auth endpoint ---" << std::endl;
    try {
        request<string_body> req{verb::delete_, "/api/auth/login", 11};
        req.set(field::content_type, "application/json");
        req.body() = R"({"username":"test","password":"test"})";
        req.prepare_payload();
        
        auto response = handler.handleRequest(req);
        std::cout << "Status: " << response.result() << std::endl;
        std::cout << "Body: " << response.body() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
    }
    
    // Test 2: Invalid job ID format
    std::cout << "\n--- Test 2: Invalid job ID format ---" << std::endl;
    try {
        request<string_body> req{verb::get, "/api/jobs/invalid-id/status", 11};
        req.set(field::content_type, "application/json");
        req.prepare_payload();
        
        auto response = handler.handleRequest(req);
        std::cout << "Status: " << response.result() << std::endl;
        std::cout << "Body: " << response.body() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
    }
    
    // Test 3: Invalid logs endpoint method
    std::cout << "\n--- Test 3: Invalid logs endpoint method ---" << std::endl;
    try {
        request<string_body> req{verb::delete_, "/api/logs", 11};
        req.set(field::content_type, "application/json");
        req.prepare_payload();
        
        auto response = handler.handleRequest(req);
        std::cout << "Status: " << response.result() << std::endl;
        std::cout << "Body: " << response.body() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
    }
    
    // Test 4: Invalid monitoring endpoint method
    std::cout << "\n--- Test 4: Invalid monitoring endpoint method ---" << std::endl;
    try {
        request<string_body> req{verb::post, "/api/monitor/jobs", 11};
        req.set(field::content_type, "application/json");
        req.body() = R"({"status":"running"})";
        req.prepare_payload();
        
        auto response = handler.handleRequest(req);
        std::cout << "Status: " << response.result() << std::endl;
        std::cout << "Body: " << response.body() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
    }
    
    // Test 5: Valid request (should work)
    std::cout << "\n--- Test 5: Valid request ---" << std::endl;
    try {
        request<string_body> req{verb::get, "/api/logs", 11};
        req.set(field::content_type, "application/json");
        req.prepare_payload();
        
        auto response = handler.handleRequest(req);
        std::cout << "Status: " << response.result() << std::endl;
        std::cout << "Body: " << response.body() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
    }
}

void testExceptionMapperIntegration() {
    std::cout << "\n=== Testing ExceptionMapper Integration ===" << std::endl;
    
    // Test that ExceptionMapper is properly integrated
    auto& globalMapper = ETLPlus::ExceptionHandling::getGlobalExceptionMapper();
    
    // Create a test exception
    auto testException = etl::ValidationException(
        etl::ErrorCode::INVALID_INPUT,
        "Test validation error",
        "testField",
        "invalidValue"
    );
    
    auto response = globalMapper.mapToResponse(testException, "test_integration");
    
    std::cout << "ExceptionMapper integration test:" << std::endl;
    std::cout << "Status: " << response.result() << std::endl;
    std::cout << "Body: " << response.body() << std::endl;
}

int main() {
    try {
        testRequestHandlerWithExceptionMapper();
        testExceptionMapperIntegration();
        
        std::cout << "\n=== All tests completed! ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
