#include "logger.hpp"
#include "request_handler.hpp"
#include "database_manager.hpp"
#include "auth_manager.hpp"
#include "etl_job_manager.hpp"
#include <iostream>
#include <sstream>

RequestHandler::RequestHandler(std::shared_ptr<DatabaseManager> dbManager,
                              std::shared_ptr<AuthManager> authManager,
                              std::shared_ptr<ETLJobManager> etlManager)
    : dbManager_(dbManager)
    , authManager_(authManager)
    , etlManager_(etlManager) {
    REQ_LOG_INFO("RequestHandler created with components - DB: " + std::string(dbManager ? "valid" : "null") +
                 ", Auth: " + std::string(authManager ? "valid" : "null") +
                 ", ETL: " + std::string(etlManager ? "valid" : "null"));
}

template<class Body, class Allocator>
http::response<http::string_body> 
RequestHandler::handleRequest(http::request<Body, http::basic_fields<Allocator>>&& req) {
    REQ_LOG_DEBUG("RequestHandler::handleRequest() - Received request: " + std::string(req.method_string()) + " " + std::string(req.target()));
    
    try {
        // Validate request basic structure
        if (req.target().empty()) {
            REQ_LOG_WARN("RequestHandler::handleRequest() - Empty target in request");
            return createErrorResponse(http::status::bad_request, "Empty request target");
        }
        
        // Convert to string_body if needed
        http::request<http::string_body> string_req;
        string_req.method(req.method());
        string_req.target(req.target());
        string_req.version(req.version());
        string_req.keep_alive(req.keep_alive());
        
        REQ_LOG_DEBUG("RequestHandler::handleRequest() - Converting request headers");
        // Copy headers
        for (auto const& field : req) {
            string_req.set(field.name(), field.value());
        }
        
        REQ_LOG_DEBUG("RequestHandler::handleRequest() - Converting request body");
        // Copy body if it exists
        if constexpr (std::is_same_v<Body, http::string_body>) {
            string_req.body() = req.body();
        }
        
        string_req.prepare_payload();
        
        std::string target = std::string(string_req.target());
        REQ_LOG_INFO("RequestHandler::handleRequest() - Routing request to: " + target);
        
        // Validate components before routing
        if (!dbManager_) {
            REQ_LOG_ERROR("RequestHandler::handleRequest() - Database manager is null");
            return createErrorResponse(http::status::internal_server_error, "Database not available");
        }
        
        if (!authManager_) {
            REQ_LOG_ERROR("RequestHandler::handleRequest() - Auth manager is null");
            return createErrorResponse(http::status::internal_server_error, "Authentication not available");
        }
        
        if (!etlManager_) {
            REQ_LOG_ERROR("RequestHandler::handleRequest() - ETL manager is null");
            return createErrorResponse(http::status::internal_server_error, "ETL manager not available");
        }
        
        // Route requests
        if (target.starts_with("/api/auth")) {
            REQ_LOG_DEBUG("RequestHandler::handleRequest() - Routing to auth handler");
            return handleAuth(string_req);
        } else if (target.starts_with("/api/jobs")) {
            REQ_LOG_DEBUG("RequestHandler::handleRequest() - Routing to ETL jobs handler");
            return handleETLJobs(string_req);
        } else if (target.starts_with("/api/monitor")) {
            REQ_LOG_DEBUG("RequestHandler::handleRequest() - Routing to monitoring handler");
            return handleMonitoring(string_req);
        } else if (target == "/api/health" || target == "/api/status") {
            REQ_LOG_DEBUG("RequestHandler::handleRequest() - Routing to health/status handler");
            return createSuccessResponse("{\"status\":\"healthy\",\"timestamp\":\"" + 
                                       std::to_string(std::time(nullptr)) + "\"}");
        } else {
            REQ_LOG_WARN("RequestHandler::handleRequest() - Unknown endpoint: " + target);
            return createErrorResponse(http::status::not_found, "Not Found");
        }
    } catch (const std::exception& e) {
        REQ_LOG_ERROR("RequestHandler::handleRequest() - Exception: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, "Internal Server Error");
    } catch (...) {
        REQ_LOG_ERROR("RequestHandler::handleRequest() - Unknown exception occurred");
        return createErrorResponse(http::status::internal_server_error, "Unknown Internal Server Error");
    }
}

// Explicit template instantiation
template http::response<http::string_body> 
RequestHandler::handleRequest<http::string_body, std::allocator<char>>(
    http::request<http::string_body, http::basic_fields<std::allocator<char>>>&& req);

http::response<http::string_body> RequestHandler::handleAuth(const http::request<http::string_body>& req) {
    std::string target = std::string(req.target());
    
    // Add CORS headers for preflight requests
    if (req.method() == http::verb::options) {
        http::response<http::string_body> res{http::status::ok, 11};
        res.set(http::field::server, "ETL Plus Backend");
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        res.keep_alive(false);
        res.prepare_payload();
        return res;
    }
    
    if (req.method() == http::verb::post && target == "/api/auth/login") {
        // Parse login credentials from body
        std::string body = req.body();
        
        // Basic JSON validation
        if (body.empty()) {
            REQ_LOG_WARN("RequestHandler::handleAuth() - Empty request body for login");
            return createErrorResponse(http::status::bad_request, "Empty request body");
        }
        
        // Check for basic JSON structure
        if (body.front() != '{' || body.back() != '}') {
            REQ_LOG_WARN("RequestHandler::handleAuth() - Invalid JSON format in login request");
            return createErrorResponse(http::status::bad_request, "Invalid JSON format");
        }
        
        REQ_LOG_INFO("RequestHandler::handleAuth() - Processing login request");
        std::cout << "Login attempt with body: " << body << std::endl;
        
        // For now, return a mock success response
        return createSuccessResponse("{\"token\":\"mock_jwt_token\",\"user_id\":\"123\"}");
    } else if (req.method() == http::verb::post && target == "/api/auth/logout") {
        return createSuccessResponse("{\"message\":\"Logged out successfully\"}");
    } else if (req.method() == http::verb::get && target == "/api/auth/profile") {
        return createSuccessResponse("{\"user_id\":\"123\",\"username\":\"testuser\",\"email\":\"test@example.com\"}");
    }
    
    return createErrorResponse(http::status::bad_request, "Invalid auth endpoint");
}

http::response<http::string_body> RequestHandler::handleETLJobs(const http::request<http::string_body>& req) {
    std::string target = std::string(req.target());
    
    // Add CORS headers for preflight requests
    if (req.method() == http::verb::options) {
        http::response<http::string_body> res{http::status::ok, 11};
        res.set(http::field::server, "ETL Plus Backend");
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        res.keep_alive(false);
        res.prepare_payload();
        return res;
    }
    
    if (req.method() == http::verb::get && target == "/api/jobs") {
        // Return list of jobs
        auto jobs = etlManager_->getAllJobs();
        std::ostringstream json;
        json << "{\"jobs\":[";
        for (size_t i = 0; i < jobs.size(); ++i) {
            if (i > 0) json << ",";
            json << "{\"id\":\"" << jobs[i]->jobId << "\",\"status\":\"";
            switch (jobs[i]->status) {
                case JobStatus::PENDING: json << "pending"; break;
                case JobStatus::RUNNING: json << "running"; break;
                case JobStatus::COMPLETED: json << "completed"; break;
                case JobStatus::FAILED: json << "failed"; break;
                case JobStatus::CANCELLED: json << "cancelled"; break;
            }
            json << "\"}";
        }
        json << "]}";
        return createSuccessResponse(json.str());
    } else if (req.method() == http::verb::post && target == "/api/jobs") {
        // Create new job
        std::string body = req.body();
        
        // Basic JSON validation
        if (body.empty()) {
            REQ_LOG_WARN("RequestHandler::handleETLJobs() - Empty request body for job creation");
            return createErrorResponse(http::status::bad_request, "Empty request body");
        }
        
        // Check for basic JSON structure
        if (body.front() != '{' || body.back() != '}') {
            REQ_LOG_WARN("RequestHandler::handleETLJobs() - Invalid JSON format in job creation request");
            return createErrorResponse(http::status::bad_request, "Invalid JSON format");
        }
        
        REQ_LOG_INFO("RequestHandler::handleETLJobs() - Processing job creation request");
        std::cout << "Creating job with config: " << body << std::endl;
        
        try {
            // Mock job creation
            ETLJobConfig config;
            config.jobId = "job_" + std::to_string(std::time(nullptr));
            config.type = JobType::FULL_ETL;
            config.sourceConfig = "mock_source";
            config.targetConfig = "mock_target";
            
            std::string jobId = etlManager_->scheduleJob(config);
            return createSuccessResponse("{\"job_id\":\"" + jobId + "\",\"status\":\"scheduled\"}");
        } catch (const std::exception& e) {
            REQ_LOG_ERROR("RequestHandler::handleETLJobs() - Exception during job creation: " + std::string(e.what()));
            return createErrorResponse(http::status::internal_server_error, "Failed to create job");
        }
    }
    
    return createErrorResponse(http::status::bad_request, "Invalid jobs endpoint");
}

http::response<http::string_body> RequestHandler::handleMonitoring(const http::request<http::string_body>& req) {
    std::string target = std::string(req.target());
    
    // Add CORS headers for preflight requests
    if (req.method() == http::verb::options) {
        http::response<http::string_body> res{http::status::ok, 11};
        res.set(http::field::server, "ETL Plus Backend");
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        res.keep_alive(false);
        res.prepare_payload();
        return res;
    }
    
    if (req.method() == http::verb::get && target == "/api/monitor/status") {
        return createSuccessResponse("{\"server_status\":\"running\",\"db_connected\":" + 
                                   std::string(dbManager_->isConnected() ? "true" : "false") + 
                                   ",\"etl_manager_running\":" + 
                                   std::string(etlManager_->isRunning() ? "true" : "false") + "}");
    } else if (req.method() == http::verb::get && target == "/api/monitor/metrics") {
        return createSuccessResponse("{\"total_jobs\":0,\"running_jobs\":0,\"completed_jobs\":0,\"failed_jobs\":0}");
    }
    
    return createErrorResponse(http::status::bad_request, "Invalid monitoring endpoint");
}

http::response<http::string_body> RequestHandler::createErrorResponse(http::status status, const std::string& message) {
    http::response<http::string_body> res{status, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");
    res.keep_alive(false);
    
    // Escape quotes in the message to prevent JSON injection
    std::string escaped_message = message;
    size_t pos = 0;
    while ((pos = escaped_message.find('"', pos)) != std::string::npos) {
        escaped_message.replace(pos, 1, "\\\"");
        pos += 2;
    }
    
    // Also escape backslashes
    pos = 0;
    while ((pos = escaped_message.find('\\', pos)) != std::string::npos) {
        if (pos + 1 < escaped_message.length() && escaped_message[pos + 1] != '"') {
            escaped_message.replace(pos, 1, "\\\\");
            pos += 2;
        } else {
            pos++;
        }
    }
    
    res.body() = "{\"error\":\"" + escaped_message + "\",\"status\":\"error\"}";
    res.prepare_payload();
    return res;
}

http::response<http::string_body> RequestHandler::createSuccessResponse(const std::string& data) {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");
    res.keep_alive(false);
    res.body() = data;
    res.prepare_payload();
    return res;
}
