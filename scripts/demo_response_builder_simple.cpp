#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <chrono>

/**
 * Simplified ResponseBuilder Demo (without Boost Beast dependency)
 * 
 * This demonstrates the core response building logic that would be used in the 
 * actual ResponseBuilder component for HTTP server stability improvements.
 */

class SimpleResponseBuilder {
public:
    enum class ContentType {
        JSON,
        XML,
        HTML,
        TEXT
    };
    
    enum class Status {
        OK = 200,
        CREATED = 201,
        FOUND = 302,
        BAD_REQUEST = 400,
        UNAUTHORIZED = 401,
        FORBIDDEN = 403,
        NOT_FOUND = 404,
        METHOD_NOT_ALLOWED = 405,
        CONFLICT = 409,
        TOO_MANY_REQUESTS = 429,
        INTERNAL_SERVER_ERROR = 500,
        SERVICE_UNAVAILABLE = 503
    };
    
    struct Response {
        Status status;
        std::unordered_map<std::string, std::string> headers;
        std::string body;
        
        Response(Status s) : status(s) {}
    };
    
    struct Config {
        std::string serverName = "ETL Plus Backend";
        bool enableCors = true;
        bool includeTimestamp = true;
        bool includeRequestId = false;
        ContentType defaultContentType = ContentType::JSON;
    };

private:
    Config config_;
    Status currentStatus_ = Status::OK;
    ContentType currentContentType_;
    std::unordered_map<std::string, std::string> currentHeaders_;
    std::string currentRequestId_;

public:
    SimpleResponseBuilder() : config_({}), currentContentType_(config_.defaultContentType) {}
    explicit SimpleResponseBuilder(Config config) 
        : config_(std::move(config)), currentContentType_(config_.defaultContentType) {}
    
    // Fluent interface methods
    SimpleResponseBuilder& setStatus(Status status) {
        currentStatus_ = status;
        return *this;
    }
    
    SimpleResponseBuilder& setContentType(ContentType type) {
        currentContentType_ = type;
        return *this;
    }
    
    SimpleResponseBuilder& setHeader(const std::string& name, const std::string& value) {
        currentHeaders_[name] = value;
        return *this;
    }
    
    SimpleResponseBuilder& setRequestId(const std::string& requestId) {
        currentRequestId_ = requestId;
        return *this;
    }
    
    // Success response methods
    Response success(const std::string& data) {
        currentStatus_ = Status::OK;
        return buildResponse(data);
    }
    
    Response successJson(const std::string& jsonData) {
        currentStatus_ = Status::OK;
        currentContentType_ = ContentType::JSON;
        return buildResponse(jsonData);
    }
    
    Response successWithMessage(const std::string& message, const std::string& data = "") {
        currentStatus_ = Status::OK;
        currentContentType_ = ContentType::JSON;
        
        std::ostringstream json;
        json << R"({"status":"success","message":")" << escapeJson(message) << R"(")";
        
        if (!data.empty()) {
            json << R"(,"data":)" << data;
        }
        
        if (config_.includeTimestamp) {
            json << R"(,"timestamp":")" << getCurrentTimestamp() << R"(")";
        }
        
        if (config_.includeRequestId && !currentRequestId_.empty()) {
            json << R"(,"request_id":")" << currentRequestId_ << R"(")";
        }
        
        json << "}";
        
        return buildResponse(json.str());
    }
    
    // Error response methods
    Response error(Status status, const std::string& message) {
        currentStatus_ = status;
        currentContentType_ = ContentType::JSON;
        
        std::ostringstream json;
        json << R"({"status":"error","error":")" << escapeJson(message) << R"(")";
        json << R"(,"code":)" << static_cast<int>(status);
        
        if (config_.includeTimestamp) {
            json << R"(,"timestamp":")" << getCurrentTimestamp() << R"(")";
        }
        
        if (config_.includeRequestId && !currentRequestId_.empty()) {
            json << R"(,"request_id":")" << currentRequestId_ << R"(")";
        }
        
        json << "}";
        
        return buildResponse(json.str());
    }
    
    Response badRequest(const std::string& message) {
        return error(Status::BAD_REQUEST, message);
    }
    
    Response unauthorized(const std::string& message = "Unauthorized") {
        return error(Status::UNAUTHORIZED, message);
    }
    
    Response forbidden(const std::string& message = "Forbidden") {
        return error(Status::FORBIDDEN, message);
    }
    
    Response notFound(const std::string& resource = "Resource") {
        return error(Status::NOT_FOUND, resource + " not found");
    }
    
    Response methodNotAllowed(const std::string& method, const std::string& endpoint) {
        setHeader("Allow", "GET, POST, PUT, DELETE, OPTIONS");
        return error(Status::METHOD_NOT_ALLOWED, 
                    "Method " + method + " not allowed for " + endpoint);
    }
    
    Response tooManyRequests(const std::string& message = "Rate limit exceeded") {
        setHeader("Retry-After", "60");
        return error(Status::TOO_MANY_REQUESTS, message);
    }
    
    Response internalServerError(const std::string& message = "Internal server error") {
        return error(Status::INTERNAL_SERVER_ERROR, message);
    }
    
    // Specialized responses
    Response validationError(const std::vector<std::string>& errors) {
        currentStatus_ = Status::BAD_REQUEST;
        currentContentType_ = ContentType::JSON;
        
        std::ostringstream json;
        json << R"({"status":"error","error":"Validation failed","validation":{"errors":[)";
        
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) json << ",";
            json << R"(")" << escapeJson(errors[i]) << R"(")";
        }
        
        json << "]}}";
        
        return buildResponse(json.str());
    }
    
    Response healthCheck(bool healthy, const std::string& details = "") {
        currentStatus_ = healthy ? Status::OK : Status::SERVICE_UNAVAILABLE;
        currentContentType_ = ContentType::JSON;
        
        std::ostringstream json;
        json << R"({"status":")" << (healthy ? "healthy" : "unhealthy") << R"(")";
        
        if (!details.empty()) {
            json << R"(,"details":")" << escapeJson(details) << R"(")";
        }
        
        json << R"(,"timestamp":")" << getCurrentTimestamp() << R"(")";
        json << "}";
        
        return buildResponse(json.str());
    }
    
    Response redirect(const std::string& location, Status status = Status::FOUND) {
        currentStatus_ = status;
        setHeader("Location", location);
        return buildResponse("");
    }

private:
    Response buildResponse(const std::string& body) {
        Response response(currentStatus_);
        
        // Set body
        response.body = body;
        
        // Apply default headers
        response.headers["Server"] = config_.serverName;
        response.headers["Content-Type"] = contentTypeToString(currentContentType_);
        
        // Apply custom headers
        for (const auto& [name, value] : currentHeaders_) {
            response.headers[name] = value;
        }
        
        // Apply CORS headers if enabled
        if (config_.enableCors) {
            response.headers["Access-Control-Allow-Origin"] = "*";
            response.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
            response.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
        }
        
        // Apply security headers
        response.headers["X-Content-Type-Options"] = "nosniff";
        response.headers["X-Frame-Options"] = "DENY";
        response.headers["X-XSS-Protection"] = "1; mode=block";
        
        // Reset state for next response
        resetState();
        
        return response;
    }
    
    std::string contentTypeToString(ContentType type) const {
        switch (type) {
        case ContentType::JSON: return "application/json";
        case ContentType::XML: return "application/xml";
        case ContentType::HTML: return "text/html; charset=utf-8";
        case ContentType::TEXT: return "text/plain; charset=utf-8";
        default: return "application/json";
        }
    }
    
    std::string escapeJson(const std::string& input) const {
        std::ostringstream escaped;
        for (char c : input) {
            switch (c) {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default: escaped << c; break;
            }
        }
        return escaped.str();
    }
    
    std::string getCurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }
    
    void resetState() {
        currentStatus_ = Status::OK;
        currentContentType_ = config_.defaultContentType;
        currentHeaders_.clear();
        currentRequestId_.clear();
    }
};

void printResponse(const std::string& testName, const SimpleResponseBuilder::Response& response) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Test: " << testName << "\n";
    std::cout << std::string(60, '=') << "\n";
    
    std::cout << "Status: " << static_cast<int>(response.status) << "\n";
    
    std::cout << "Headers:\n";
    for (const auto& [name, value] : response.headers) {
        std::cout << "  " << name << ": " << value << "\n";
    }
    
    std::cout << "Body:\n";
    if (response.body.empty()) {
        std::cout << "  (empty)\n";
    } else {
        std::cout << "  " << response.body << "\n";
    }
}

int main() {
    std::cout << "🚀 ResponseBuilder Demo (Simplified)\n";
    std::cout << "====================================\n";
    std::cout << "Demonstrating HTTP response building for server stability improvements\n";
    
    SimpleResponseBuilder::Config config;
    config.serverName = "ETL Plus Demo Server";
    config.includeTimestamp = true;
    config.includeRequestId = true;
    
    SimpleResponseBuilder builder(config);
    
    // Test 1: Success response
    {
        auto response = builder.success(R"({"message":"Hello World"})");
        printResponse("Basic Success Response", response);
    }
    
    // Test 2: Success with message
    {
        auto response = builder.successWithMessage("User created successfully", R"({"id":123,"name":"John"})");
        printResponse("Success with Message", response);
    }
    
    // Test 3: JSON success response
    {
        auto response = builder.successJson(R"({"users":[{"id":1,"name":"Alice"},{"id":2,"name":"Bob"}]})");
        printResponse("JSON Success Response", response);
    }
    
    // Test 4: Bad request error
    {
        auto response = builder.badRequest("Missing required field: username");
        printResponse("Bad Request Error", response);
    }
    
    // Test 5: Unauthorized error
    {
        auto response = builder.unauthorized("Invalid authentication token");
        printResponse("Unauthorized Error", response);
    }
    
    // Test 6: Not found error
    {
        auto response = builder.notFound("User");
        printResponse("Not Found Error", response);
    }
    
    // Test 7: Method not allowed
    {
        auto response = builder.methodNotAllowed("DELETE", "/api/users");
        printResponse("Method Not Allowed", response);
    }
    
    // Test 8: Rate limit exceeded
    {
        auto response = builder.tooManyRequests("Too many requests from this IP");
        printResponse("Rate Limit Exceeded", response);
    }
    
    // Test 9: Validation error
    {
        std::vector<std::string> errors = {
            "Username must be at least 3 characters",
            "Email format is invalid",
            "Password must contain at least one number"
        };
        auto response = builder.validationError(errors);
        printResponse("Validation Error", response);
    }
    
    // Test 10: Health check (healthy)
    {
        auto response = builder.healthCheck(true, "All systems operational");
        printResponse("Health Check - Healthy", response);
    }
    
    // Test 11: Health check (unhealthy)
    {
        auto response = builder.healthCheck(false, "Database connection failed");
        printResponse("Health Check - Unhealthy", response);
    }
    
    // Test 12: Redirect response
    {
        auto response = builder.redirect("https://api.example.com/v2/users");
        printResponse("Redirect Response", response);
    }
    
    // Test 13: Fluent interface usage
    {
        auto response = builder.setStatus(SimpleResponseBuilder::Status::CREATED)
                              .setContentType(SimpleResponseBuilder::ContentType::JSON)
                              .setHeader("X-Custom-Header", "custom-value")
                              .setRequestId("req-12345")
                              .success(R"({"id":456,"status":"created"})");
        printResponse("Fluent Interface Usage", response);
    }
    
    // Test 14: Custom content type
    {
        auto response = builder.setContentType(SimpleResponseBuilder::ContentType::XML)
                              .success("<users><user id=\"1\">Alice</user></users>");
        printResponse("XML Content Type", response);
    }
    
    // Test 15: Internal server error
    {
        auto response = builder.internalServerError("Database connection timeout");
        printResponse("Internal Server Error", response);
    }
    
    std::cout << "\n🎉 ResponseBuilder Demo Complete!\n";
    std::cout << "\nKey Features Demonstrated:\n";
    std::cout << "  ✅ Fluent interface for response building\n";
    std::cout << "  ✅ Standardized success and error responses\n";
    std::cout << "  ✅ Automatic header management (CORS, security)\n";
    std::cout << "  ✅ Content type negotiation\n";
    std::cout << "  ✅ JSON formatting and escaping\n";
    std::cout << "  ✅ Timestamp and request ID inclusion\n";
    std::cout << "  ✅ HTTP status code mapping\n";
    std::cout << "  ✅ Validation error formatting\n";
    std::cout << "  ✅ Health check responses\n";
    std::cout << "  ✅ Redirect responses\n";
    
    std::cout << "\nThis response building logic will improve HTTP server stability by:\n";
    std::cout << "  • Ensuring consistent response formats across all endpoints\n";
    std::cout << "  • Automatically applying security and CORS headers\n";
    std::cout << "  • Providing proper HTTP status codes for different scenarios\n";
    std::cout << "  • Standardizing error response structures\n";
    std::cout << "  • Including debugging information (timestamps, request IDs)\n";
    std::cout << "  • Preventing JSON injection through proper escaping\n";
    
    return 0;
}