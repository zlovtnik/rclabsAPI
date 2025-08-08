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
}

template<class Body, class Allocator>
http::response<http::string_body> 
RequestHandler::handleRequest(http::request<Body, http::basic_fields<Allocator>>&& req) {
    // Convert to string_body if needed
    http::request<http::string_body> string_req;
    string_req.method(req.method());
    string_req.target(req.target());
    string_req.version(req.version());
    string_req.keep_alive(req.keep_alive());
    
    // Copy headers
    for (auto const& field : req) {
        string_req.set(field.name(), field.value());
    }
    
    // Copy body if it exists
    if constexpr (std::is_same_v<Body, http::string_body>) {
        string_req.body() = req.body();
    }
    
    string_req.prepare_payload();
    
    std::string target = std::string(string_req.target());
    
    // Route requests
    if (target.starts_with("/api/auth")) {
        return handleAuth(string_req);
    } else if (target.starts_with("/api/jobs")) {
        return handleETLJobs(string_req);
    } else if (target.starts_with("/api/monitor")) {
        return handleMonitoring(string_req);
    } else if (target == "/api/health") {
        return createSuccessResponse("{\"status\":\"healthy\",\"timestamp\":\"" + 
                                   std::to_string(std::time(nullptr)) + "\"}");
    } else {
        return createErrorResponse(http::status::not_found, "Endpoint not found");
    }
}

// Explicit template instantiation
template http::response<http::string_body> 
RequestHandler::handleRequest<http::string_body, std::allocator<char>>(
    http::request<http::string_body, http::basic_fields<std::allocator<char>>>&& req);

http::response<http::string_body> RequestHandler::handleAuth(const http::request<http::string_body>& req) {
    std::string target = std::string(req.target());
    
    if (req.method() == http::verb::post && target == "/api/auth/login") {
        // Parse login credentials from body
        std::string body = req.body();
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
        std::cout << "Creating job with config: " << body << std::endl;
        
        // Mock job creation
        ETLJobConfig config;
        config.jobId = "job_" + std::to_string(std::time(nullptr));
        config.type = JobType::FULL_ETL;
        config.sourceConfig = "mock_source";
        config.targetConfig = "mock_target";
        
        std::string jobId = etlManager_->scheduleJob(config);
        return createSuccessResponse("{\"job_id\":\"" + jobId + "\",\"status\":\"scheduled\"}");
    }
    
    return createErrorResponse(http::status::bad_request, "Invalid jobs endpoint");
}

http::response<http::string_body> RequestHandler::handleMonitoring(const http::request<http::string_body>& req) {
    std::string target = std::string(req.target());
    
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
    res.keep_alive(false);
    res.body() = "{\"error\":\"" + message + "\"}";
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
