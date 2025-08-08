#pragma once

#include <boost/beast/http.hpp>
#include <string>
#include <memory>

namespace http = boost::beast::http;

class DatabaseManager;
class AuthManager;
class ETLJobManager;

class RequestHandler {
public:
    RequestHandler(std::shared_ptr<DatabaseManager> dbManager,
                  std::shared_ptr<AuthManager> authManager,
                  std::shared_ptr<ETLJobManager> etlManager);
    
    template<class Body, class Allocator>
    http::response<http::string_body> 
    handleRequest(http::request<Body, http::basic_fields<Allocator>>&& req);
    
private:
    std::shared_ptr<DatabaseManager> dbManager_;
    std::shared_ptr<AuthManager> authManager_;
    std::shared_ptr<ETLJobManager> etlManager_;
    
    http::response<http::string_body> handleAuth(const http::request<http::string_body>& req);
    http::response<http::string_body> handleETLJobs(const http::request<http::string_body>& req);
    http::response<http::string_body> handleMonitoring(const http::request<http::string_body>& req);
    http::response<http::string_body> createErrorResponse(http::status status, const std::string& message);
    http::response<http::string_body> createSuccessResponse(const std::string& data);
};
