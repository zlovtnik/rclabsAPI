#include "exception_mapper.hpp"
#include "request_handler.hpp"
#include "etl_exceptions.hpp"
#include "logger.hpp"
#include <boost/beast/http.hpp>
#include <iostream>

using namespace ETLPlus::ExceptionHandling;

// Example of how to integrate ExceptionMapper into RequestHandler
class RequestHandlerWithExceptionMapper {
private:
    ExceptionMapper exceptionMapper_;
    
public:
    RequestHandlerWithExceptionMapper() {
        // Configure the exception mapper
        ExceptionMappingConfig config;
        config.includeInternalDetails = false; // Don't expose internal details in production
        config.serverHeader = "ETL Plus Backend v2.0";
        config.corsOrigin = "*";
        config.keepAlive = false;
        
        exceptionMapper_.updateConfig(config);
        
        // Register custom handlers for specific scenarios
        registerCustomHandlers();
    }
    
    void registerCustomHandlers() {
        // Custom handler for rate limiting
        exceptionMapper_.registerHandler(etl::ErrorCode::RATE_LIMIT_EXCEEDED,
            [](const etl::ETLException& ex, const std::string& operation) {
                HttpResponse response{boost::beast::http::status::too_many_requests, 11};
                response.set(boost::beast::http::field::content_type, "application/json");
                response.set(boost::beast::http::field::retry_after, "60");
                response.set("X-Rate-Limit-Limit", "100");
                response.set("X-Rate-Limit-Remaining", "0");
                response.set("X-Rate-Limit-Reset", "60");
                
                std::string body = R"({
                    "error": "Rate limit exceeded",
                    "message": ")" + ETLPlus::ExceptionHandling::escapeJsonString(ex.getMessage()) + R"(",
                    "retryAfter": 60,
                    "correlationId": ")" + ETLPlus::ExceptionHandling::escapeJsonString(ex.getCorrelationId()) + R"("
                })";
                
                response.body() = body;
                response.prepare_payload();
                return response;
            });
        
        // Custom handler for maintenance mode
        exceptionMapper_.registerHandler(etl::ErrorCode::COMPONENT_UNAVAILABLE,
            [](const etl::ETLException& ex, const std::string& operation) {
                HttpResponse response{boost::beast::http::status::service_unavailable, 11};
                response.set(boost::beast::http::field::content_type, "application/json");
                response.set(boost::beast::http::field::retry_after, "300"); // 5 minutes
                
                std::string body = R"({
                    "error": "Service temporarily unavailable",
                    "message": ")" + ETLPlus::ExceptionHandling::escapeJsonString(ex.getMessage()) + R"(",
                    "maintenance": true,
                    "estimatedRecovery": "5 minutes",
                    "correlationId": ")" + ETLPlus::ExceptionHandling::escapeJsonString(ex.getCorrelationId()) + R"("
                })";
                
                response.body() = body;
                response.prepare_payload();
                return response;
            });
    }
    
    // Simplified request handling with ExceptionMapper
    template <class Body, class Allocator>
    boost::beast::http::response<boost::beast::http::string_body> handleRequest(
        boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator>> req) {
        
        try {
            // Set correlation ID for this request
            std::string correlationId = ExceptionMapper::generateCorrelationId();
            ExceptionMapper::setCurrentCorrelationId(correlationId);
            
            // Process the request (simplified for demo)
            return processRequest(req);
            
        } catch (const etl::ETLException& ex) {
            // Use ExceptionMapper to handle ETL exceptions
            return exceptionMapper_.mapToResponse(ex, "handleRequest");
            
        } catch (const std::exception& ex) {
            // Use ExceptionMapper to handle standard exceptions
            return exceptionMapper_.mapToResponse(ex, "handleRequest");
            
        } catch (...) {
            // Use ExceptionMapper to handle unknown exceptions
            return exceptionMapper_.mapToResponse("handleRequest");
        }
    }
    
private:
    template <class Body, class Allocator>
    boost::beast::http::response<boost::beast::http::string_body> processRequest(
        boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator>> req) {
        
        // Simulate different types of errors for demonstration
        std::string target = std::string(req.target());
        
        if (target == "/test/validation") {
            throw etl::ValidationException(
                etl::ErrorCode::INVALID_INPUT,
                "Invalid request format",
                "body",
                "malformed json"
            );
        } else if (target == "/test/rate-limit") {
            throw etl::SystemException(
                etl::ErrorCode::RATE_LIMIT_EXCEEDED,
                "API rate limit exceeded",
                "RateLimiter"
            );
        } else if (target == "/test/maintenance") {
            throw etl::SystemException(
                etl::ErrorCode::COMPONENT_UNAVAILABLE,
                "System maintenance in progress",
                "MaintenanceMode"
            );
        } else if (target == "/test/not-found") {
            throw etl::BusinessException(
                etl::ErrorCode::JOB_NOT_FOUND,
                "Job with ID 12345 not found",
                "JobManager::getJob"
            );
        } else if (target == "/test/database") {
            throw etl::SystemException(
                etl::ErrorCode::DATABASE_ERROR,
                "Database connection failed",
                "DatabaseManager",
                etl::ErrorContext{{"host", "localhost"}, {"port", "5432"}}
            );
        } else if (target == "/test/standard") {
            throw std::runtime_error("Standard runtime error occurred");
        } else {
            // Success response
            boost::beast::http::response<boost::beast::http::string_body> response{
                boost::beast::http::status::ok, 11
            };
            response.set(boost::beast::http::field::content_type, "application/json");
            response.body() = R"({"status":"success","message":"Request processed successfully"})";
            response.prepare_payload();
            return response;
        }
    }
};

void demonstrateExceptionMapping() {
    std::cout << "=== ExceptionMapper Integration Demo ===" << std::endl;
    
    RequestHandlerWithExceptionMapper handler;
    
    // Test different error scenarios
    std::vector<std::string> testPaths = {
        "/test/validation",
        "/test/rate-limit", 
        "/test/maintenance",
        "/test/not-found",
        "/test/database",
        "/test/standard",
        "/test/success"
    };
    
    for (const auto& path : testPaths) {
        std::cout << "\n--- Testing path: " << path << " ---" << std::endl;
        
        // Create a simple request
        boost::beast::http::request<boost::beast::http::string_body> req{
            boost::beast::http::verb::get, path, 11
        };
        
        try {
            auto response = handler.handleRequest(req);
            
            std::cout << "Status: " << response.result() << std::endl;
            std::cout << "Content-Type: " << response[boost::beast::http::field::content_type] << std::endl;
            
            // Show special headers for rate limiting
            if (response.result() == boost::beast::http::status::too_many_requests) {
                std::cout << "Retry-After: " << response[boost::beast::http::field::retry_after] << std::endl;
                std::cout << "X-Rate-Limit-Limit: " << response["X-Rate-Limit-Limit"] << std::endl;
            }
            
            std::cout << "Body: " << response.body() << std::endl;
            
        } catch (const std::exception& e) {
            std::cout << "Unexpected exception: " << e.what() << std::endl;
        }
    }
}

void demonstrateGlobalExceptionMapper() {
    std::cout << "\n=== Global ExceptionMapper Demo ===" << std::endl;
    
    // Use the global exception mapper
    auto& globalMapper = getGlobalExceptionMapper();
    
    // Test with different exception types
    auto validationEx = etl::ValidationException(
        etl::ErrorCode::MISSING_FIELD,
        "Required field 'email' is missing",
        "email"
    );
    
    auto response = globalMapper.mapToResponse(validationEx, "global_test");
    
    std::cout << "Global mapper response: " << response.result() << std::endl;
    std::cout << "Body: " << response.body() << std::endl;
}

int main() {
    try {
        demonstrateExceptionMapping();
        demonstrateGlobalExceptionMapper();
        
        std::cout << "\n=== Demo completed successfully! ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
