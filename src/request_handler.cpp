#include "request_handler.hpp"
#include "auth_manager.hpp"
#include "database_manager.hpp"
#include "etl_job_manager.hpp"
#include "exception_handler.hpp"
#include "exceptions.hpp"
#include "input_validator.hpp"
#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

RequestHandler::RequestHandler(std::shared_ptr<DatabaseManager> dbManager,
                               std::shared_ptr<AuthManager> authManager,
                               std::shared_ptr<ETLJobManager> etlManager)
    : dbManager_(dbManager), authManager_(authManager),
      etlManager_(etlManager) {
  REQ_LOG_INFO("RequestHandler created with components - DB: " +
               std::string(dbManager ? "valid" : "null") +
               ", Auth: " + std::string(authManager ? "valid" : "null") +
               ", ETL: " + std::string(etlManager ? "valid" : "null"));
}

template <class Body, class Allocator>
http::response<http::string_body> RequestHandler::handleRequest(
    http::request<Body, http::basic_fields<Allocator>> &&req) {
  REQ_LOG_DEBUG("RequestHandler::handleRequest() - Received request: " +
                std::string(req.method_string()) + " " +
                std::string(req.target()));

  try {
    // Convert to string_body if needed
    http::request<http::string_body> string_req;
    string_req.method(req.method());
    string_req.target(req.target());
    string_req.version(req.version());
    string_req.keep_alive(req.keep_alive());

    REQ_LOG_DEBUG(
        "RequestHandler::handleRequest() - Converting request headers");
    // Copy headers
    for (auto const &field : req) {
      string_req.set(field.name(), field.value());
    }

    REQ_LOG_DEBUG("RequestHandler::handleRequest() - Converting request body");
    // Copy body if it exists
    if constexpr (std::is_same_v<Body, http::string_body>) {
      string_req.body() = req.body();
    }

    string_req.prepare_payload();

    // Perform comprehensive validation and handle request
    return validateAndHandleRequest(string_req);

  } catch (const ETLPlus::Exceptions::BaseException &ex) {
    REQ_LOG_ERROR("RequestHandler::handleRequest() - ETL Exception caught: " +
                  ex.toLogString());
    return createExceptionResponse(ex);
  } catch (const std::exception &e) {
    REQ_LOG_ERROR("RequestHandler::handleRequest() - Standard exception: " +
                  std::string(e.what()));
    auto convertedException =
        ETLPlus::ExceptionHandling::ExceptionHandler::convertException(
            e, "handleRequest",
            ETLPlus::Exceptions::ErrorContext("handleRequest"));
    return createExceptionResponse(*convertedException);
  } catch (...) {
    REQ_LOG_ERROR(
        "RequestHandler::handleRequest() - Unknown exception occurred");
    auto unknownEx = ETLPlus::Exceptions::SystemException(
        ETLPlus::Exceptions::ErrorCode::UNKNOWN_ERROR,
        "Unknown exception in request handler",
        ETLPlus::Exceptions::ErrorContext("handleRequest"));
    return createExceptionResponse(unknownEx);
  }
}

http::response<http::string_body> RequestHandler::validateAndHandleRequest(
    const http::request<http::string_body> &req) {
  // Step 1: Validate basic request structure
  auto basicValidation = validateRequestBasics(req);
  if (!basicValidation.isValid) {
    REQ_LOG_WARN(
        "RequestHandler::validateAndHandleRequest() - Basic validation failed");
    throw ETLPlus::Exceptions::ValidationException(
        ETLPlus::Exceptions::ErrorCode::INVALID_INPUT,
        "Request validation failed",
        ETLPlus::Exceptions::ErrorContext("validateAndHandleRequest"));
  }

  std::string target = std::string(req.target());
  std::string method = std::string(req.method_string());

  REQ_LOG_INFO("RequestHandler::validateAndHandleRequest() - Processing "
               "validated request: " +
               method + " " + target);

  // Step 2: Validate components before routing
  if (!dbManager_) {
    REQ_LOG_ERROR("RequestHandler::validateAndHandleRequest() - Database "
                  "manager is null");
    throw ETLPlus::Exceptions::SystemException(
        ETLPlus::Exceptions::ErrorCode::COMPONENT_UNAVAILABLE,
        "Database manager not available",
        ETLPlus::Exceptions::ErrorContext("validateAndHandleRequest"));
  }

  if (!authManager_) {
    REQ_LOG_ERROR(
        "RequestHandler::validateAndHandleRequest() - Auth manager is null");
    throw ETLPlus::Exceptions::SystemException(
        ETLPlus::Exceptions::ErrorCode::COMPONENT_UNAVAILABLE,
        "Authentication manager not available",
        ETLPlus::Exceptions::ErrorContext("validateAndHandleRequest"));
  }

  if (!etlManager_) {
    REQ_LOG_ERROR(
        "RequestHandler::validateAndHandleRequest() - ETL manager is null");
    throw ETLPlus::Exceptions::SystemException(
        ETLPlus::Exceptions::ErrorCode::COMPONENT_UNAVAILABLE,
        "ETL manager not available",
        ETLPlus::Exceptions::ErrorContext("validateAndHandleRequest"));
  }

  // Step 3: Route requests with endpoint-specific validation
  if (target.find("/api/auth") == 0) {
    REQ_LOG_DEBUG(
        "RequestHandler::validateAndHandleRequest() - Routing to auth handler");
    return handleAuth(req);
  } else if (target.find("/api/jobs") == 0) {
    REQ_LOG_DEBUG("RequestHandler::validateAndHandleRequest() - Routing to ETL "
                  "jobs handler");
    return handleETLJobs(req);
  } else if (target.find("/api/monitor") == 0) {
    REQ_LOG_DEBUG("RequestHandler::validateAndHandleRequest() - Routing to "
                  "monitoring handler");
    return handleMonitoring(req);
  } else if (target == "/api/health" || target == "/api/status") {
    REQ_LOG_DEBUG("RequestHandler::validateAndHandleRequest() - Routing to "
                  "health/status handler");
    return createSuccessResponse("{\"status\":\"healthy\",\"timestamp\":\"" +
                                 std::to_string(std::time(nullptr)) + "\"}");
  } else {
    REQ_LOG_WARN(
        "RequestHandler::validateAndHandleRequest() - Unknown endpoint: " +
        target);
    throw ETLPlus::Exceptions::NetworkException(
        ETLPlus::Exceptions::ErrorCode::INVALID_RESPONSE,
        "Endpoint not found: " + target,
        ETLPlus::Exceptions::ErrorContext("validateAndHandleRequest"), 404);
  }
}

InputValidator::ValidationResult RequestHandler::validateRequestBasics(
    const http::request<http::string_body> &req) {
  InputValidator::ValidationResult result;

  std::string target = std::string(req.target());
  std::string method = std::string(req.method_string());

  // Validate request target/path
  auto pathValidation = InputValidator::validateEndpointPath(target);
  if (!pathValidation.isValid) {
    result.errors.insert(result.errors.end(), pathValidation.errors.begin(),
                         pathValidation.errors.end());
    result.isValid = false;
  }

  // Validate query parameters if present
  size_t queryPos = target.find('?');
  if (queryPos != std::string::npos) {
    std::string queryString = target.substr(queryPos + 1);
    auto queryValidation = InputValidator::validateQueryParameters(queryString);
    if (!queryValidation.isValid) {
      result.errors.insert(result.errors.end(), queryValidation.errors.begin(),
                           queryValidation.errors.end());
      result.isValid = false;
    }
  }

  // Validate HTTP method
  std::vector<std::string> allowedMethods = {"GET",    "POST",    "PUT",
                                             "DELETE", "OPTIONS", "PATCH"};
  if (!InputValidator::isValidHttpMethod(method, allowedMethods)) {
    result.addError("method", "HTTP method not allowed", "METHOD_NOT_ALLOWED");
  }

  // Validate request headers
  auto headers = extractHeaders(req);
  auto headerValidation = InputValidator::validateRequestHeaders(headers);
  if (!headerValidation.isValid) {
    result.errors.insert(result.errors.end(), headerValidation.errors.begin(),
                         headerValidation.errors.end());
    result.isValid = false;
  }

  // Validate request size
  if (!InputValidator::isValidRequestSize(req.body().length())) {
    result.addError("content-length", "Request body too large",
                    "BODY_TOO_LARGE");
  }

  // Validate content type for POST/PUT/PATCH requests
  if (method == "POST" || method == "PUT" || method == "PATCH") {
    auto contentTypeIt = headers.find("content-type");
    if (contentTypeIt != headers.end()) {
      if (!InputValidator::isValidContentType(contentTypeIt->second)) {
        result.addError("content-type", "Unsupported content type",
                        "INVALID_CONTENT_TYPE");
      }
    }
  }

  return result;
}

std::unordered_map<std::string, std::string>
RequestHandler::extractHeaders(const http::request<http::string_body> &req) {
  std::unordered_map<std::string, std::string> headers;

  for (auto const &field : req) {
    std::string key = std::string(field.name_string());
    std::string value = std::string(field.value());
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    headers[key] = value;
  }

  return headers;
}

std::unordered_map<std::string, std::string>
RequestHandler::extractQueryParams(const std::string &target) {
  size_t queryPos = target.find('?');
  if (queryPos == std::string::npos) {
    return {};
  }

  std::string queryString = target.substr(queryPos + 1);
  return InputValidator::parseQueryString(queryString);
}

http::response<http::string_body>
RequestHandler::handleAuth(const http::request<http::string_body> &req) {
  std::string target = std::string(req.target());
  std::string method = std::string(req.method_string());

  // Handle CORS preflight
  if (req.method() == http::verb::options) {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers,
            "Content-Type, Authorization");
    res.keep_alive(false);
    res.prepare_payload();
    return res;
  }

  // Validate allowed methods for auth endpoints
  if (!InputValidator::isValidHttpMethod(method, {"POST", "GET"})) {
    return createErrorResponse(http::status::method_not_allowed,
                               "Method not allowed for auth endpoint");
  }

  if (req.method() == http::verb::post && target == "/api/auth/login") {
    // Validate login request body
    auto validation = InputValidator::validateLoginRequest(req.body());
    if (!validation.isValid) {
      REQ_LOG_WARN("RequestHandler::handleAuth() - Login validation failed");
      return createValidationErrorResponse(validation);
    }

    REQ_LOG_INFO(
        "RequestHandler::handleAuth() - Processing validated login request");

    // For now, return a mock success response
    return createSuccessResponse("{\"token\":\"mock_jwt_token\",\"user_id\":"
                                 "\"123\",\"expires_in\":3600}");

  } else if (req.method() == http::verb::post && target == "/api/auth/logout") {
    // Validate logout request (may be empty or contain token)
    if (!req.body().empty()) {
      auto validation = InputValidator::validateLogoutRequest(req.body());
      if (!validation.isValid) {
        REQ_LOG_WARN("RequestHandler::handleAuth() - Logout validation failed");
        return createValidationErrorResponse(validation);
      }
    }

    return createSuccessResponse("{\"message\":\"Logged out successfully\"}");

  } else if (req.method() == http::verb::get && target == "/api/auth/profile") {
    // Validate authorization header for profile access
    auto headers = extractHeaders(req);
    auto authIt = headers.find("authorization");
    if (authIt != headers.end()) {
      auto authValidation =
          InputValidator::validateAuthorizationHeader(authIt->second);
      if (!authValidation.isValid) {
        REQ_LOG_WARN(
            "RequestHandler::handleAuth() - Profile auth validation failed");
        return createValidationErrorResponse(authValidation);
      }
    } else {
      return createErrorResponse(http::status::unauthorized,
                                 "Authorization header required");
    }

    return createSuccessResponse(
        "{\"user_id\":\"123\",\"username\":\"testuser\",\"email\":\"test@"
        "example.com\"}");
  }

  return createErrorResponse(http::status::bad_request,
                             "Invalid auth endpoint");
}

http::response<http::string_body>
RequestHandler::handleETLJobs(const http::request<http::string_body> &req) {
  std::string target = std::string(req.target());
  std::string method = std::string(req.method_string());

  // Handle CORS preflight
  if (req.method() == http::verb::options) {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods,
            "GET, POST, PUT, DELETE, OPTIONS");
    res.set(http::field::access_control_allow_headers,
            "Content-Type, Authorization");
    res.keep_alive(false);
    res.prepare_payload();
    return res;
  }

  // Validate allowed methods for jobs endpoints
  if (!InputValidator::isValidHttpMethod(method,
                                         {"GET", "POST", "PUT", "DELETE"})) {
    return createErrorResponse(http::status::method_not_allowed,
                               "Method not allowed for jobs endpoint");
  }

  // Handle GET /api/jobs/{id}/status - detailed job status
  if (req.method() == http::verb::get && target.find("/api/jobs/") == 0 && 
      target.length() > 7 && target.substr(target.length() - 7) == "/status") {
    std::string jobId = extractJobIdFromPath(target, "/api/jobs/", "/status");
    if (!InputValidator::isValidJobId(jobId)) {
      return createErrorResponse(http::status::bad_request, "Invalid job ID format");
    }

    auto job = etlManager_->getJob(jobId);
    if (!job) {
      return createErrorResponse(http::status::not_found, "Job not found");
    }

    // Create detailed job status response
    std::ostringstream json;
    json << "{"
         << "\"jobId\":\"" << job->jobId << "\","
         << "\"type\":\"" << jobTypeToString(job->type) << "\","
         << "\"status\":\"" << jobStatusToString(job->status) << "\","
         << "\"createdAt\":\"" << formatTimestamp(job->createdAt) << "\","
         << "\"startedAt\":\"" << formatTimestamp(job->startedAt) << "\","
         << "\"completedAt\":\"" << formatTimestamp(job->completedAt) << "\","
         << "\"recordsProcessed\":" << job->recordsProcessed << ","
         << "\"recordsSuccessful\":" << job->recordsSuccessful << ","
         << "\"recordsFailed\":" << job->recordsFailed;
    
    if (!job->errorMessage.empty()) {
      json << ",\"errorMessage\":\"" << InputValidator::sanitizeString(job->errorMessage) << "\"";
    }
    
    // Calculate execution time
    auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
      job->completedAt - job->startedAt);
    if (job->status == JobStatus::RUNNING) {
      executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - job->startedAt);
    }
    json << ",\"executionTimeMs\":" << executionTime.count();
    
    json << "}";
    return createSuccessResponse(json.str());
  }

  // Handle GET /api/jobs/{id}/metrics - job execution metrics
  if (req.method() == http::verb::get && target.find("/api/jobs/") == 0 && 
      target.length() > 8 && target.substr(target.length() - 8) == "/metrics") {
    std::string jobId = extractJobIdFromPath(target, "/api/jobs/", "/metrics");
    if (!InputValidator::isValidJobId(jobId)) {
      return createErrorResponse(http::status::bad_request, "Invalid job ID format");
    }

    auto job = etlManager_->getJob(jobId);
    if (!job) {
      return createErrorResponse(http::status::not_found, "Job not found");
    }

    // Calculate metrics
    auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
      job->status == JobStatus::RUNNING ? 
        std::chrono::system_clock::now() - job->startedAt :
        job->completedAt - job->startedAt);
    
    double processingRate = 0.0;
    if (executionTime.count() > 0) {
      processingRate = (double)job->recordsProcessed / (executionTime.count() / 1000.0);
    }

    double successRate = 0.0;
    if (job->recordsProcessed > 0) {
      successRate = (double)job->recordsSuccessful / job->recordsProcessed * 100.0;
    }

    std::ostringstream json;
    json << "{"
         << "\"jobId\":\"" << job->jobId << "\","
         << "\"recordsProcessed\":" << job->recordsProcessed << ","
         << "\"recordsSuccessful\":" << job->recordsSuccessful << ","
         << "\"recordsFailed\":" << job->recordsFailed << ","
         << "\"processingRate\":" << processingRate << ","
         << "\"successRate\":" << successRate << ","
         << "\"executionTimeMs\":" << executionTime.count() << ","
         << "\"status\":\"" << jobStatusToString(job->status) << "\""
         << "}";
    
    return createSuccessResponse(json.str());
  }

  if (req.method() == http::verb::get && target == "/api/jobs") {
    // Validate query parameters
    auto queryParams = extractQueryParams(target);
    auto queryValidation = InputValidator::validateJobQueryParams(queryParams);
    if (!queryValidation.isValid) {
      REQ_LOG_WARN("RequestHandler::handleETLJobs() - Query parameter "
                   "validation failed");
      return createValidationErrorResponse(queryValidation);
    }

    // Return list of jobs
    auto jobs = etlManager_->getAllJobs();
    std::ostringstream json;
    json << "{\"jobs\":[";
    for (size_t i = 0; i < jobs.size(); ++i) {
      if (i > 0)
        json << ",";
      json << "{\"id\":\"" << jobs[i]->jobId << "\",\"status\":\"";
      switch (jobs[i]->status) {
      case JobStatus::PENDING:
        json << "pending";
        break;
      case JobStatus::RUNNING:
        json << "running";
        break;
      case JobStatus::COMPLETED:
        json << "completed";
        break;
      case JobStatus::FAILED:
        json << "failed";
        break;
      case JobStatus::CANCELLED:
        json << "cancelled";
        break;
      }
      json << "\"}";
    }
    json << "]}";
    return createSuccessResponse(json.str());

  } else if (req.method() == http::verb::post && target == "/api/jobs") {
    // Validate job creation request
    auto validation = InputValidator::validateJobCreationRequest(req.body());
    if (!validation.isValid) {
      REQ_LOG_WARN(
          "RequestHandler::handleETLJobs() - Job creation validation failed");
      return createValidationErrorResponse(validation);
    }

    REQ_LOG_INFO("RequestHandler::handleETLJobs() - Processing validated job "
                 "creation request");

    try {
      // Mock job creation
      ETLJobConfig config;
      config.jobId = "job_" + std::to_string(std::time(nullptr));
      config.type = JobType::FULL_ETL;
      config.sourceConfig = "mock_source";
      config.targetConfig = "mock_target";

      std::string jobId = etlManager_->scheduleJob(config);
      return createSuccessResponse("{\"job_id\":\"" + jobId +
                                   "\",\"status\":\"scheduled\"}");
    } catch (const std::exception &e) {
      REQ_LOG_ERROR(
          "RequestHandler::handleETLJobs() - Exception during job creation: " +
          std::string(e.what()));
      return createErrorResponse(http::status::internal_server_error,
                                 "Failed to create job");
    }

  } else if (req.method() == http::verb::put &&
             target.find("/api/jobs/") == 0) {
    // Extract job ID from path
    std::string jobId = target.substr(11); // Remove "/api/jobs/"
    if (!InputValidator::isValidJobId(jobId)) {
      return createErrorResponse(http::status::bad_request,
                                 "Invalid job ID format");
    }

    // Validate job update request
    auto validation = InputValidator::validateJobUpdateRequest(req.body());
    if (!validation.isValid) {
      REQ_LOG_WARN(
          "RequestHandler::handleETLJobs() - Job update validation failed");
      return createValidationErrorResponse(validation);
    }

    return createSuccessResponse("{\"job_id\":\"" + jobId +
                                 "\",\"status\":\"updated\"}");
  }

  return createErrorResponse(http::status::bad_request,
                             "Invalid jobs endpoint");
}

http::response<http::string_body>
RequestHandler::handleMonitoring(const http::request<http::string_body> &req) {
  std::string target = std::string(req.target());
  std::string method = std::string(req.method_string());

  // Handle CORS preflight
  if (req.method() == http::verb::options) {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, OPTIONS");
    res.set(http::field::access_control_allow_headers,
            "Content-Type, Authorization");
    res.keep_alive(false);
    res.prepare_payload();
    return res;
  }

  // Validate allowed methods for monitoring endpoints
  if (!InputValidator::isValidHttpMethod(method, {"GET"})) {
    return createErrorResponse(http::status::method_not_allowed,
                               "Method not allowed for monitoring endpoint");
  }

  // Handle GET /api/monitor/jobs - filtered job monitoring
  if (req.method() == http::verb::get && target.starts_with("/api/monitor/jobs")) {
    auto queryParams = extractQueryParams(target);
    auto queryValidation = InputValidator::validateMonitoringParams(queryParams);
    if (!queryValidation.isValid) {
      REQ_LOG_WARN("RequestHandler::handleMonitoring() - Jobs query validation failed");
      return createValidationErrorResponse(queryValidation);
    }

    // Get all jobs from ETL manager
    auto allJobs = etlManager_->getAllJobs();
    
    // Apply filters
    std::vector<std::shared_ptr<ETLJob>> filteredJobs;
    
    // Filter by status if specified
    auto statusIt = queryParams.find("status");
    if (statusIt != queryParams.end()) {
      JobStatus filterStatus = stringToJobStatus(statusIt->second);
      for (const auto& job : allJobs) {
        if (job->status == filterStatus) {
          filteredJobs.push_back(job);
        }
      }
    } else {
      filteredJobs = allJobs;
    }

    // Filter by job type if specified
    auto typeIt = queryParams.find("type");
    if (typeIt != queryParams.end()) {
      JobType filterType = stringToJobType(typeIt->second);
      std::vector<std::shared_ptr<ETLJob>> typeFiltered;
      for (const auto& job : filteredJobs) {
        if (job->type == filterType) {
          typeFiltered.push_back(job);
        }
      }
      filteredJobs = typeFiltered;
    }

    // Filter by date range if specified
    auto fromIt = queryParams.find("from");
    auto toIt = queryParams.find("to");
    if (fromIt != queryParams.end() || toIt != queryParams.end()) {
      std::vector<std::shared_ptr<ETLJob>> dateFiltered;
      
      std::chrono::system_clock::time_point fromTime = std::chrono::system_clock::time_point::min();
      std::chrono::system_clock::time_point toTime = std::chrono::system_clock::time_point::max();
      
      if (fromIt != queryParams.end()) {
        fromTime = parseTimestamp(fromIt->second);
      }
      if (toIt != queryParams.end()) {
        toTime = parseTimestamp(toIt->second);
      }
      
      for (const auto& job : filteredJobs) {
        if (job->createdAt >= fromTime && job->createdAt <= toTime) {
          dateFiltered.push_back(job);
        }
      }
      filteredJobs = dateFiltered;
    }

    // Apply limit if specified
    auto limitIt = queryParams.find("limit");
    if (limitIt != queryParams.end()) {
      try {
        size_t limit = std::stoull(limitIt->second);
        if (filteredJobs.size() > limit) {
          filteredJobs.resize(limit);
        }
      } catch (const std::exception&) {
        return createErrorResponse(http::status::bad_request, "Invalid limit parameter");
      }
    }

    // Build JSON response
    std::ostringstream json;
    json << "{\"jobs\":[";
    for (size_t i = 0; i < filteredJobs.size(); ++i) {
      if (i > 0) json << ",";
      
      const auto& job = filteredJobs[i];
      
      // Calculate execution time
      auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        job->status == JobStatus::RUNNING ? 
          std::chrono::system_clock::now() - job->startedAt :
          job->completedAt - job->startedAt);
      
      double processingRate = 0.0;
      if (executionTime.count() > 0) {
        processingRate = (double)job->recordsProcessed / (executionTime.count() / 1000.0);
      }

      json << "{"
           << "\"jobId\":\"" << job->jobId << "\","
           << "\"type\":\"" << jobTypeToString(job->type) << "\","
           << "\"status\":\"" << jobStatusToString(job->status) << "\","
           << "\"createdAt\":\"" << formatTimestamp(job->createdAt) << "\","
           << "\"startedAt\":\"" << formatTimestamp(job->startedAt) << "\","
           << "\"completedAt\":\"" << formatTimestamp(job->completedAt) << "\","
           << "\"recordsProcessed\":" << job->recordsProcessed << ","
           << "\"recordsSuccessful\":" << job->recordsSuccessful << ","
           << "\"recordsFailed\":" << job->recordsFailed << ","
           << "\"processingRate\":" << processingRate << ","
           << "\"executionTimeMs\":" << executionTime.count();
      
      if (!job->errorMessage.empty()) {
        json << ",\"errorMessage\":\"" << InputValidator::sanitizeString(job->errorMessage) << "\"";
      }
      
      json << "}";
    }
    json << "],\"total\":" << filteredJobs.size() << "}";
    
    return createSuccessResponse(json.str());
  }

  if (req.method() == http::verb::get && target == "/api/monitor/status") {
    return createSuccessResponse(
        "{\"server_status\":\"running\",\"db_connected\":" +
        std::string(dbManager_->isConnected() ? "true" : "false") +
        ",\"etl_manager_running\":" +
        std::string(etlManager_->isRunning() ? "true" : "false") + "}");

  } else if (req.method() == http::verb::get &&
             target == "/api/monitor/metrics") {
    // Validate query parameters for metrics
    auto queryParams = extractQueryParams(target);
    auto queryValidation =
        InputValidator::validateMetricsParams(queryParams);
    if (!queryValidation.isValid) {
      REQ_LOG_WARN("RequestHandler::handleMonitoring() - Metrics query "
                   "validation failed");
      return createValidationErrorResponse(queryValidation);
    }

    return createSuccessResponse("{\"total_jobs\":0,\"running_jobs\":0,"
                                 "\"completed_jobs\":0,\"failed_jobs\":0}");
  }

  return createErrorResponse(http::status::bad_request,
                             "Invalid monitoring endpoint");
}

http::response<http::string_body>
RequestHandler::createErrorResponse(http::status status,
                                    const std::string &message) {
  http::response<http::string_body> res{status, 11};
  res.set(http::field::server, "ETL Plus Backend");
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(false);

  // Escape quotes in the message to prevent JSON injection
  std::string escaped_message = InputValidator::sanitizeString(message);

  res.body() = "{\"error\":\"" + escaped_message + "\",\"status\":\"error\"}";
  res.prepare_payload();
  return res;
}

http::response<http::string_body> RequestHandler::createExceptionResponse(
    const ETLPlus::Exceptions::BaseException &ex) {
  http::status status = http::status::internal_server_error;

  // Map exception codes to HTTP status codes
  switch (ex.getErrorCode()) {
  case ETLPlus::Exceptions::ErrorCode::INVALID_INPUT:
  case ETLPlus::Exceptions::ErrorCode::MISSING_REQUIRED_FIELD:
  case ETLPlus::Exceptions::ErrorCode::INVALID_FORMAT:
  case ETLPlus::Exceptions::ErrorCode::VALUE_OUT_OF_RANGE:
  case ETLPlus::Exceptions::ErrorCode::INVALID_TYPE:
    status = http::status::bad_request;
    break;

  case ETLPlus::Exceptions::ErrorCode::INVALID_CREDENTIALS:
  case ETLPlus::Exceptions::ErrorCode::TOKEN_EXPIRED:
  case ETLPlus::Exceptions::ErrorCode::TOKEN_INVALID:
    status = http::status::unauthorized;
    break;

  case ETLPlus::Exceptions::ErrorCode::INSUFFICIENT_PERMISSIONS:
  case ETLPlus::Exceptions::ErrorCode::ACCOUNT_LOCKED:
    status = http::status::forbidden;
    break;

  case ETLPlus::Exceptions::ErrorCode::JOB_NOT_FOUND:
  case ETLPlus::Exceptions::ErrorCode::FILE_NOT_FOUND:
    status = http::status::not_found;
    break;

  case ETLPlus::Exceptions::ErrorCode::REQUEST_TIMEOUT:
  case ETLPlus::Exceptions::ErrorCode::CONNECTION_TIMEOUT:
    status = http::status::request_timeout;
    break;

  case ETLPlus::Exceptions::ErrorCode::RATE_LIMIT_EXCEEDED:
    status = http::status::too_many_requests;
    break;

  case ETLPlus::Exceptions::ErrorCode::SERVICE_UNAVAILABLE:
  case ETLPlus::Exceptions::ErrorCode::COMPONENT_UNAVAILABLE:
    status = http::status::service_unavailable;
    break;

  default:
    status = http::status::internal_server_error;
    break;
  }

  http::response<http::string_body> res{status, 11};
  res.set(http::field::server, "ETL Plus Backend");
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(false);

  // Create structured error response using the exception's JSON representation
  res.body() = ex.toJsonString();
  res.prepare_payload();
  return res;
}

http::response<http::string_body> RequestHandler::createValidationErrorResponse(
    const InputValidator::ValidationResult &result) {
  http::response<http::string_body> res{http::status::bad_request, 11};
  res.set(http::field::server, "ETL Plus Backend");
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(false);

  std::ostringstream json;
  json
      << "{\"error\":\"Validation failed\",\"status\":\"error\",\"validation\":"
      << result.toJsonString() << "}";

  res.body() = json.str();
  res.prepare_payload();
  return res;
}

http::response<http::string_body>
RequestHandler::createSuccessResponse(const std::string &data) {
  http::response<http::string_body> res{http::status::ok, 11};
  res.set(http::field::server, "ETL Plus Backend");
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(false);
  res.body() = data;
  res.prepare_payload();
  return res;
}

std::string RequestHandler::extractJobIdFromPath(const std::string& target, 
                                                 const std::string& prefix, 
                                                 const std::string& suffix) {
  if (target.length() <= prefix.length() + suffix.length()) {
    return "";
  }
  
  size_t startPos = prefix.length();
  size_t endPos = target.length() - suffix.length();
  
  if (startPos >= endPos) {
    return "";
  }
  
  return target.substr(startPos, endPos - startPos);
}

std::string RequestHandler::jobStatusToString(JobStatus status) {
  switch (status) {
    case JobStatus::PENDING: return "pending";
    case JobStatus::RUNNING: return "running";
    case JobStatus::COMPLETED: return "completed";
    case JobStatus::FAILED: return "failed";
    case JobStatus::CANCELLED: return "cancelled";
    default: return "unknown";
  }
}

JobStatus RequestHandler::stringToJobStatus(const std::string& statusStr) {
  if (statusStr == "pending") return JobStatus::PENDING;
  if (statusStr == "running") return JobStatus::RUNNING;
  if (statusStr == "completed") return JobStatus::COMPLETED;
  if (statusStr == "failed") return JobStatus::FAILED;
  if (statusStr == "cancelled") return JobStatus::CANCELLED;
  return JobStatus::PENDING; // default
}

std::string RequestHandler::jobTypeToString(JobType type) {
  switch (type) {
    case JobType::EXTRACT: return "extract";
    case JobType::TRANSFORM: return "transform";
    case JobType::LOAD: return "load";
    case JobType::FULL_ETL: return "full_etl";
    default: return "unknown";
  }
}

JobType RequestHandler::stringToJobType(const std::string& typeStr) {
  if (typeStr == "extract") return JobType::EXTRACT;
  if (typeStr == "transform") return JobType::TRANSFORM;
  if (typeStr == "load") return JobType::LOAD;
  if (typeStr == "full_etl") return JobType::FULL_ETL;
  return JobType::FULL_ETL; // default
}

std::string RequestHandler::formatTimestamp(const std::chrono::system_clock::time_point& timePoint) {
  auto time_t = std::chrono::system_clock::to_time_t(timePoint);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    timePoint.time_since_epoch()) % 1000;
  
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
  oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
  return oss.str();
}

std::chrono::system_clock::time_point RequestHandler::parseTimestamp(const std::string& timestampStr) {
  std::tm tm = {};
  std::istringstream ss(timestampStr);
  
  // Try to parse ISO 8601 format: YYYY-MM-DDTHH:MM:SS.sssZ
  if (ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S")) {
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
  }
  
  // If parsing fails, return current time
  return std::chrono::system_clock::now();
}

// Explicit template instantiation
template http::response<http::string_body>
RequestHandler::handleRequest<http::string_body, std::allocator<char>>(
    http::request<http::string_body, http::basic_fields<std::allocator<char>>>
        &&req);
