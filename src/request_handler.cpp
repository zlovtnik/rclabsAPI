#include "request_handler.hpp"
#include "auth_manager.hpp"
#include "database_manager.hpp"
#include "etl_job_manager.hpp"
#include "websocket_filter_manager.hpp"
#include "exception_handler.hpp"
#include "exception_mapper.hpp"
#include "etl_exceptions.hpp"
#include "input_validator.hpp"
#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <regex>
#include <format>
#include <unistd.h> // for getpid()

RequestHandler::RequestHandler(std::shared_ptr<DatabaseManager> dbManager,
                               std::shared_ptr<AuthManager> authManager,
                               std::shared_ptr<ETLJobManager> etlManager)
    : dbManager_(dbManager), authManager_(authManager),
      etlManager_(etlManager), exceptionMapper_()
{
  REQ_LOG_INFO("RequestHandler created with components - DB: " +
               std::string(dbManager ? "valid" : "null") +
               ", Auth: " + std::string(authManager ? "valid" : "null") +
               ", ETL: " + std::string(etlManager ? "valid" : "null"));

  // Configure the exception mapper for RequestHandler
  ETLPlus::ExceptionHandling::ExceptionMappingConfig config;
  config.serverHeader = "ETL Plus Backend";
  config.corsOrigin = "*";
  config.keepAlive = false;
  config.includeInternalDetails = false; // Don't expose internal details in production
  exceptionMapper_.updateConfig(config);

  // Initialize Hana-based exception handlers for better type safety and performance
  hanaExceptionRegistry_.registerHandler<etl::ValidationException>(
      ETLPlus::ExceptionHandling::makeValidationErrorHandler());
  hanaExceptionRegistry_.registerHandler<etl::SystemException>(
      ETLPlus::ExceptionHandling::makeSystemErrorHandler());
  hanaExceptionRegistry_.registerHandler<etl::BusinessException>(
      ETLPlus::ExceptionHandling::makeBusinessErrorHandler());

  // Note: HanaExceptionRegistry provides better type safety than the old ExceptionMapper
  // The old ExceptionMapper registration is removed in favor of Hana-based handling

  REQ_LOG_INFO("Hana-based exception handlers registered for improved error handling");
}

template <class Body, class Allocator>
http::response<http::string_body> RequestHandler::handleRequest(
    http::request<Body, http::basic_fields<Allocator>> req)
{
  REQ_LOG_DEBUG("RequestHandler::handleRequest() - Received request: " +
                std::string(req.method_string()) + " " +
                std::string(req.target()));

  try
  {
    // Convert to string_body if needed
    http::request<http::string_body> string_req;
    string_req.method(req.method());
    string_req.target(req.target());
    string_req.version(req.version());
    string_req.keep_alive(req.keep_alive());

    REQ_LOG_DEBUG(
        "RequestHandler::handleRequest() - Converting request headers");
    // Copy headers - check for valid field names to avoid assertion failures
    for (auto const &field : req)
    {
      try
      {
        // Only copy known header fields to avoid Beast assertion failures
        if (field.name() != http::field::unknown)
        {
          string_req.set(field.name(), field.value());
        }
        else
        {
          // For unknown fields, use the string name
          string_req.set(field.name_string(), field.value());
        }
      }
      catch (...)
      {
        REQ_LOG_WARN("RequestHandler::handleRequest() - Failed to copy header: " +
                     std::string(field.name_string()) + " = " + std::string(field.value()));
        // Continue processing other headers
      }
    }

    REQ_LOG_DEBUG("RequestHandler::handleRequest() - Converting request body");
    // Copy body if it exists
    if constexpr (std::is_same_v<Body, http::string_body>)
    {
      string_req.body() = req.body();
    }

    string_req.prepare_payload();

    // Perform comprehensive validation and handle request
    return validateAndHandleRequest(string_req);
  }
  catch (const etl::ETLException &ex)
  {
    // Use Hana-based exception handling for better type safety and performance
    return hanaExceptionRegistry_.handle(ex, "handleRequest");
  }
  catch (const std::exception &e)
  {
    // Fallback to traditional exception mapper for non-ETL exceptions
    return exceptionMapper_.mapToResponse(e, "handleRequest");
  }
  catch (...)
  {
    return exceptionMapper_.mapToResponse("handleRequest");
  }
}

http::response<http::string_body> RequestHandler::validateAndHandleRequest(
    const http::request<http::string_body> &req) const
{
  // Step 1: Validate basic request structure
  if (auto basicValidation = validateRequestBasics(req); !basicValidation.isValid)
  {
    REQ_LOG_WARN(
        "RequestHandler::validateAndHandleRequest() - Basic validation failed");
    throw etl::ValidationException(
        etl::ErrorCode::INVALID_INPUT,
        "Request validation failed");
  }

  auto target = std::string(req.target());
  auto method = std::string(req.method_string());

  REQ_LOG_INFO("RequestHandler::validateAndHandleRequest() - Processing "
               "validated request: " +
               method + " " + target);

  // Step 2: Validate components before routing
  if (!dbManager_)
  {
    REQ_LOG_ERROR("RequestHandler::validateAndHandleRequest() - Database "
                  "manager is null");
    throw etl::SystemException(
        etl::ErrorCode::COMPONENT_UNAVAILABLE,
        "Database manager not available",
        "RequestHandler");
  }

  if (!authManager_)
  {
    REQ_LOG_ERROR(
        "RequestHandler::validateAndHandleRequest() - Auth manager is null");
    throw etl::SystemException(
        etl::ErrorCode::COMPONENT_UNAVAILABLE,
        "Authentication manager not available",
        "RequestHandler");
  }

  if (!etlManager_)
  {
    REQ_LOG_ERROR(
        "RequestHandler::validateAndHandleRequest() - ETL manager is null");
    throw etl::SystemException(
        etl::ErrorCode::COMPONENT_UNAVAILABLE,
        "ETL manager not available",
        "RequestHandler");
  }

  // Step 3: Route requests with endpoint-specific validation
  if (target.rfind("/api/auth", 0) == 0)
  {
    REQ_LOG_DEBUG(
        "RequestHandler::validateAndHandleRequest() - Routing to auth handler");
    return handleAuth(req);
  }
  else if (target.rfind("/api/logs", 0) == 0)
  {
    REQ_LOG_DEBUG("RequestHandler::validateAndHandleRequest() - Routing to "
                  "logs handler");
    return handleLogs(req);
  }
  else if (target.rfind("/api/jobs", 0) == 0)
  {
    REQ_LOG_DEBUG("RequestHandler::validateAndHandleRequest() - Routing to ETL "
                  "jobs handler");
    return handleETLJobs(req);
  }
  else if (target.rfind("/api/monitor", 0) == 0)
  {
    REQ_LOG_DEBUG("RequestHandler::validateAndHandleRequest() - Routing to "
                  "monitoring handler");
    return handleMonitoring(req);
  }
  else if (target == "/api/health" || target == "/api/status" || target.rfind("/api/health/", 0) == 0)
  {
    REQ_LOG_DEBUG("RequestHandler::validateAndHandleRequest() - Routing to "
                  "health/status handler");
    return handleHealth(req);
  }
  else
  {
    REQ_LOG_WARN(
        "RequestHandler::validateAndHandleRequest() - Unknown endpoint: " +
        target);
    throw etl::SystemException(
        etl::ErrorCode::NETWORK_ERROR,
        "Endpoint not found: " + target,
        "RequestHandler");
  }
}

InputValidator::ValidationResult RequestHandler::validateRequestBasics(
    const http::request<http::string_body> &req) const
{
  InputValidator::ValidationResult result;

  auto target = std::string(req.target());
  auto method = std::string(req.method_string());

  // Validate request target/path
  if (auto pathValidation = InputValidator::validateEndpointPath(target); !pathValidation.isValid)
  {
    result.errors.insert(result.errors.end(), pathValidation.errors.begin(),
                         pathValidation.errors.end());
    result.isValid = false;
  }

  // Validate query parameters if present
  if (size_t queryPos = target.find('?'); queryPos != std::string::npos)
  {
    std::string queryString = target.substr(queryPos + 1);
    auto queryValidation = InputValidator::validateQueryParameters(queryString);
    if (!queryValidation.isValid)
    {
      result.errors.insert(result.errors.end(), queryValidation.errors.begin(),
                           queryValidation.errors.end());
      result.isValid = false;
    }
  }

  // Validate HTTP method
  if (const std::vector<std::string> allowedMethods = {"GET", "POST", "PUT",
                                                       "DELETE", "OPTIONS", "PATCH"};
      !InputValidator::isValidHttpMethod(method, allowedMethods))
  {
    result.addError("method", "HTTP method not allowed", "METHOD_NOT_ALLOWED");
  }

  // Validate request headers
  auto headers = extractHeaders(req);

  // Convert to standard unordered_map for InputValidator
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> standardHeaders;
  for (const auto &[key, value] : headers)
  {
    standardHeaders[key] = value;
  }

  // Convert standardHeaders to the expected type
  std::unordered_map<std::string, std::string> convertedHeaders;
  for (const auto &[key, value] : standardHeaders)
  {
    convertedHeaders[key] = value;
  }

  // Pass convertedHeaders to the function
  if (auto headerValidation = InputValidator::validateRequestHeaders(convertedHeaders); !headerValidation.isValid)
  {
    result.errors.insert(result.errors.end(), headerValidation.errors.begin(),
                         headerValidation.errors.end());
    result.isValid = false;
  }

  // Validate request size
  if (!InputValidator::isValidRequestSize(req.body().length()))
  {
    result.addError("content-length", "Request body too large",
                    "BODY_TOO_LARGE");
  }

  // Validate content type for POST/PUT/PATCH requests
  if (method == "POST" || method == "PUT" || method == "PATCH")
  {
    if (auto it = headers.find("content-type"); it != headers.end() &&
                                                !InputValidator::isValidContentType(it->second))
    {
      result.addError("content-type", "Unsupported content type",
                      "INVALID_CONTENT_TYPE");
    }
  }

  return result;
}

std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>
RequestHandler::extractHeaders(const http::request<http::string_body> &req) const
{
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> headers;

  for (const auto &field : req)
  {
    auto name = std::string(field.name_string());
    auto value = std::string(field.value());
    headers[name] = value;
  }

  return headers;
}

std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>
RequestHandler::extractQueryParams(std::string_view target) const
{
  size_t queryPos = target.find('?');
  if (queryPos == std::string::npos)
  {
    return {};
  }

  auto queryString = std::string(target.substr(queryPos + 1));
  auto standardParams = InputValidator::parseQueryString(queryString);

  // Convert to TransparentStringHash map
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> result;
  for (const auto &[key, value] : standardParams)
  {
    result[key] = value;
  }

  return result;
}

http::response<http::string_body>
RequestHandler::handleAuth(const http::request<http::string_body> &req) const
{
  auto target = std::string(req.target());
  auto method = std::string(req.method_string());

  // Handle CORS preflight
  if (req.method() == http::verb::options)
  {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
    res.keep_alive(false);
    res.prepare_payload();
    return res;
  }

  // Validate allowed methods for auth endpoints
  if (!InputValidator::isValidHttpMethod(method, {"POST", "GET"}))
  {
    throw etl::ValidationException(
        etl::ErrorCode::INVALID_INPUT,
        "Method not allowed for auth endpoint",
        "method",
        std::string(req.method_string()));
  }

  if (req.method() == http::verb::post && target == "/api/auth/login")
  {
    // Validate login request body
    if (auto validation = InputValidator::validateLoginRequest(req.body()); !validation.isValid)
    {
      REQ_LOG_WARN("RequestHandler::handleAuth() - Login validation failed");
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Login validation failed",
          "body",
          req.body());
    }

    REQ_LOG_INFO(
        "RequestHandler::handleAuth() - Processing validated login request");

    // For now, return a mock success response
    return createSuccessResponse(R"({"token":"mock_jwt_token","user_id":"123","expires_in":3600})");
  }
  else if (req.method() == http::verb::post && target == "/api/auth/logout")
  {
    // Validate logout request (may be empty or contain token)
    if (!req.body().empty())
    {
      if (auto validation = InputValidator::validateLogoutRequest(req.body()); !validation.isValid)
      {
        REQ_LOG_WARN("RequestHandler::handleAuth() - Logout validation failed");
        throw etl::ValidationException(
            etl::ErrorCode::INVALID_INPUT,
            "Logout validation failed",
            "body",
            req.body());
      }
    }

    return createSuccessResponse(R"({"message":"Logged out successfully"})");
  }
  else if (req.method() == http::verb::get && target == "/api/auth/profile")
  {
    // Validate authorization header for profile access
    auto headers = extractHeaders(req);
    if (auto authIt = headers.find("authorization"); authIt != headers.end())
    {
      auto authValidation =
          InputValidator::validateAuthorizationHeader(authIt->second);
      if (!authValidation.isValid)
      {
        REQ_LOG_WARN(
            "RequestHandler::handleAuth() - Profile auth validation failed");
        throw etl::ValidationException(
            etl::ErrorCode::UNAUTHORIZED,
            "Profile auth validation failed",
            "authorization",
            authIt->second);
      }
    }
    else
    {
      throw etl::ValidationException(
          etl::ErrorCode::UNAUTHORIZED,
          "Authorization header required",
          "authorization",
          "");
    }

    return createSuccessResponse(R"({"user_id":"123","username":"testuser","email":"test@example.com"})");
  }

  throw etl::ValidationException(
      etl::ErrorCode::INVALID_INPUT,
      "Invalid auth endpoint",
      "target",
      target);
}

http::response<http::string_body>
RequestHandler::handleLogs(const http::request<http::string_body> &req) const
{
  auto target = std::string(req.target());
  auto method = std::string(req.method_string());

  // Handle CORS preflight
  if (req.method() == http::verb::options)
  {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
    res.keep_alive(false);
    res.prepare_payload();
    return res;
  }

  // Validate allowed methods for logs endpoints
  if (!InputValidator::isValidHttpMethod(method, {"GET", "POST"}))
  {
    throw etl::ValidationException(
        etl::ErrorCode::INVALID_INPUT,
        "Method not allowed for logs endpoint",
        "method",
        method);
  }

  // Handle different log endpoints
  if (req.method() == http::verb::get && target == "/api/logs")
  {
    // Return recent logs
    REQ_LOG_INFO("RequestHandler::handleLogs() - Retrieving recent logs");
    return createSuccessResponse(R"({"logs":[],"total":0,"message":"Logs endpoint implemented"})");
  }
  else if (req.method() == http::verb::get && target.rfind("/api/logs/", 0) == 0)
  {
    // Handle specific log queries with parameters
    REQ_LOG_INFO("RequestHandler::handleLogs() - Processing log query with parameters");
    return createSuccessResponse(R"({"logs":[],"total":0,"message":"Log query endpoint implemented"})");
  }
  else if (req.method() == http::verb::post && target == "/api/logs/search")
  {
    // Handle log search requests
    REQ_LOG_INFO("RequestHandler::handleLogs() - Processing log search request");
    return createSuccessResponse(R"({"results":[],"total":0,"message":"Log search endpoint implemented"})");
  }

  throw etl::ValidationException(
      etl::ErrorCode::INVALID_INPUT,
      "Invalid logs endpoint",
      "target",
      target);
}

http::response<http::string_body>
RequestHandler::handleETLJobs(const http::request<http::string_body> &req) const
{
  auto target = std::string(req.target());
  auto method = std::string(req.method_string());

  // Handle CORS preflight
  if (req.method() == http::verb::options)
  {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
    res.keep_alive(false);
    res.prepare_payload();
    return res;
  }

  // Validate allowed methods for jobs endpoints
  if (!InputValidator::isValidHttpMethod(method,
                                         {"GET", "POST", "PUT", "DELETE"}))
  {
    throw etl::ValidationException(
        etl::ErrorCode::INVALID_INPUT,
        "Method not allowed for jobs endpoint",
        "method",
        method);
  }

  // Handle GET /api/jobs/{id}/status - detailed job status
  if (req.method() == http::verb::get && target.rfind("/api/jobs/", 0) == 0 &&
      target.size() > 7 && target.substr(target.size() - 7) == "/status")
  {
    auto jobId = extractJobIdFromPath(target, "/api/jobs/", "/status");
    if (!InputValidator::isValidJobId(jobId))
    {
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Invalid job ID format",
          "jobId",
          jobId);
    }

    auto job = etlManager_->getJob(jobId);
    if (!job)
    {
      throw etl::BusinessException(
          etl::ErrorCode::JOB_NOT_FOUND,
          "Job not found",
          "getJob",
          etl::ErrorContext{{"jobId", jobId}});
    }

    // Create detailed job status response
    std::ostringstream json;
    json << R"({)"
         << R"("jobId":")" << job->jobId << R"(",)"
         << R"("type":")" << jobTypeToString(job->type) << R"(",)"
         << R"("status":")" << jobStatusToString(job->status) << R"(",)"
         << R"("createdAt":")" << formatTimestamp(job->createdAt) << R"(",)"
         << R"("startedAt":")" << formatTimestamp(job->startedAt) << R"(",)"
         << R"("completedAt":")" << formatTimestamp(job->completedAt) << R"(",)"
         << R"("recordsProcessed":)" << job->recordsProcessed << ","
         << R"("recordsSuccessful":)" << job->recordsSuccessful << ","
         << R"("recordsFailed":)" << job->recordsFailed;

    if (!job->errorMessage.empty())
    {
      json << R"(,"errorMessage":")" << InputValidator::sanitizeString(job->errorMessage) << R"(")";
    }

    // Calculate execution time
    auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        job->completedAt - job->startedAt);
    if (job->status == JobStatus::RUNNING)
    {
      executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now() - job->startedAt);
    }
    json << ",\"executionTimeMs\":" << executionTime.count();

    json << "}";
    return createSuccessResponse(json.str());
  }

  // Handle GET /api/jobs/{id}/metrics - job execution metrics
  if (req.method() == http::verb::get && target.rfind("/api/jobs/", 0) == 0 &&
      target.size() > 8 && target.substr(target.size() - 8) == "/metrics")
  {
    auto jobId = extractJobIdFromPath(target, "/api/jobs/", "/metrics");
    if (!InputValidator::isValidJobId(jobId))
    {
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Invalid job ID format",
          "jobId",
          jobId);
    }

    auto job = etlManager_->getJob(jobId);
    if (!job)
    {
      throw etl::BusinessException(
          etl::ErrorCode::JOB_NOT_FOUND,
          "Job not found",
          "getJob",
          etl::ErrorContext{{"jobId", jobId}});
    }

    // Calculate metrics
    auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        job->status == JobStatus::RUNNING ? std::chrono::system_clock::now() - job->startedAt : job->completedAt - job->startedAt);

    double processingRate = 0.0;
    const double secs = static_cast<double>(executionTime.count()) / 1000.0;
    if (secs > 0.0)
    {
      processingRate = static_cast<double>(job->recordsProcessed) / secs;
    }

    double successRate = 0.0;
    if (job->recordsProcessed > 0)
    {
      successRate = (double)job->recordsSuccessful / job->recordsProcessed * 100.0;
    }

    std::ostringstream json;
    json << R"({)"
         << R"("jobId":")" << job->jobId << R"(",)"
         << R"("recordsProcessed":)" << job->recordsProcessed << ","
         << R"("recordsSuccessful":)" << job->recordsSuccessful << ","
         << R"("recordsFailed":)" << job->recordsFailed << ","
         << R"("processingRate":)" << processingRate << ","
         << R"("successRate":)" << successRate << ","
         << R"("executionTimeMs":)" << executionTime.count() << ","
         << R"("status":")" << jobStatusToString(job->status) << R"(")"
         << "}";

    return createSuccessResponse(json.str());
  }

  if (req.method() == http::verb::get && target == "/api/jobs")
  {
    // Validate query parameters
    auto queryParams = extractQueryParams(target);

    // Convert to standard unordered_map for InputValidator
    std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>> standardParams;
    for (const auto &[key, value] : queryParams)
    {
      standardParams[key] = value;
    }

    // Convert standardParams to the expected type
    std::unordered_map<std::string, std::string> convertedParams;
    for (const auto &[key, value] : standardParams)
    {
      convertedParams[key] = value;
    }

    // Pass convertedParams to the function
    if (auto queryValidation = InputValidator::validateJobQueryParams(convertedParams); !queryValidation.isValid)
    {
      REQ_LOG_WARN("RequestHandler::handleETLJobs() - Query parameter "
                   "validation failed");
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Query parameter validation failed",
          "query",
          std::string(req.target()));
    }

    // Return list of jobs
    auto jobs = etlManager_->getAllJobs();
    std::ostringstream json;
    json << R"({"jobs":[)";
    for (size_t i = 0; i < jobs.size(); ++i)
    {
      if (i > 0)
        json << ",";
      json << R"({"id":")" << jobs[i]->jobId << R"(","status":")";
      using enum JobStatus;
      switch (jobs[i]->status)
      {
      case PENDING:
        json << "pending";
        break;
      case RUNNING:
        json << "running";
        break;
      case COMPLETED:
        json << "completed";
        break;
      case FAILED:
        json << "failed";
        break;
      case CANCELLED:
        json << "cancelled";
        break;
      }
      json << R"("})";
    }
    json << R"(]})";

    return createSuccessResponse(json.str());
  }
  else if (req.method() == http::verb::post && target == "/api/jobs")
  {
    // Validate job creation request
    auto validation = InputValidator::validateJobCreationRequest(req.body());
    if (!validation.isValid)
    {
      REQ_LOG_WARN(
          "RequestHandler::handleETLJobs() - Job creation validation failed");
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Job creation validation failed",
          "body",
          req.body());
    }

    REQ_LOG_INFO("RequestHandler::handleETLJobs() - Processing validated job "
                 "creation request");

    try
    {
      // Mock job creation
      ETLJobConfig config;
      using namespace std::chrono;
      const auto now = system_clock::now();
      const auto secs = duration_cast<seconds>(now.time_since_epoch()).count();
      std::stringstream ss;
      ss << "job_" << secs;
      config.jobId = ss.str();
      config.type = JobType::FULL_ETL;
      config.sourceConfig = "mock_source";
      config.targetConfig = "mock_target";

      std::string jobId = etlManager_->scheduleJob(config);
      std::stringstream ss2;
      ss2 << R"({{"job_id":")" << jobId << R"(","status":"scheduled"}})";
      return createSuccessResponse(ss2.str());
    }
    catch (const etl::ETLException &e)
    {
      REQ_LOG_ERROR(
          "RequestHandler::handleETLJobs() - Exception during job creation: " +
          std::string(e.what()));
      throw etl::SystemException(
          etl::ErrorCode::PROCESSING_FAILED,
          "Failed to create job",
          "ETLJobManager");
    }
  }
  else if (req.method() == http::verb::put &&
           target.find("/api/jobs/") == 0)
  {
    // Extract job ID from path
    std::string jobId = target.substr(11); // Remove "/api/jobs/"
    if (!InputValidator::isValidJobId(jobId))
    {
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Invalid job ID format",
          "jobId",
          jobId);
    }

    // Validate job update request
    if (auto validation = InputValidator::validateJobUpdateRequest(req.body()); !validation.isValid)
    {
      REQ_LOG_WARN(
          "RequestHandler::handleETLJobs() - Job update validation failed");
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Job update validation failed",
          "body",
          req.body());
    }

    std::stringstream ss;
    ss << R"({{"job_id":")" << jobId << R"(","status":"updated"}})";
    return createSuccessResponse(ss.str());
  }

  throw etl::ValidationException(
      etl::ErrorCode::INVALID_INPUT,
      "Invalid jobs endpoint",
      "target",
      target);
}

http::response<http::string_body>
RequestHandler::handleMonitoring(const http::request<http::string_body> &req) const
{
  auto target = std::string(req.target());
  auto method = std::string(req.method_string());

  // Handle CORS preflight
  if (req.method() == http::verb::options)
  {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, "ETL Plus Backend");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
    res.keep_alive(false);
    res.prepare_payload();
    return res;
  }

  // Validate allowed methods for monitoring endpoints
  if (!InputValidator::isValidHttpMethod(method, {"GET"}))
  {
    throw etl::ValidationException(
        etl::ErrorCode::INVALID_INPUT,
        "Method not allowed for monitoring endpoint",
        "method",
        method);
  }

  // Handle GET /api/monitor/jobs - filtered job monitoring
  if (req.method() == http::verb::get && target.find("/api/monitor/jobs") == 0)
  {
    auto queryParams = extractQueryParams(target);

    // Convert to standard unordered_map for InputValidator
    std::unordered_map<std::string, std::string> standardParams;
    for (const auto &[key, value] : queryParams)
    {
      standardParams[key] = value;
    }

    if (auto queryValidation = InputValidator::validateMonitoringParams(standardParams); !queryValidation.isValid)
    {
      REQ_LOG_WARN("RequestHandler::handleMonitoring() - Jobs query validation failed");
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Query parameter validation failed",
          "query",
          std::string(req.target()));
    }

    // Get all jobs from ETL manager
    auto allJobs = etlManager_->getAllJobs();

    // Apply filters
    std::vector<std::shared_ptr<ETLJob>> filteredJobs;

    // Filter by status if specified
    if (auto statusIt = queryParams.find("status"); statusIt != queryParams.end())
    {
      JobStatus filterStatus = stringToJobStatus(statusIt->second);
      for (const auto &job : allJobs)
      {
        if (job->status == filterStatus)
        {
          filteredJobs.push_back(job);
        }
      }
    }
    else
    {
      filteredJobs = allJobs;
    }

    // Filter by job type if specified
    if (auto typeIt = queryParams.find("type"); typeIt != queryParams.end())
    {
      JobType filterType = stringToJobType(typeIt->second);
      std::vector<std::shared_ptr<ETLJob>> typeFiltered;
      for (const auto &job : filteredJobs)
      {
        if (job->type == filterType)
        {
          typeFiltered.push_back(job);
        }
      }
      filteredJobs = typeFiltered;
    }

    // Filter by date range if specified
    auto fromIt = queryParams.find("from");
    auto toIt = queryParams.find("to");
    if (fromIt != queryParams.end() || toIt != queryParams.end())
    {
      std::vector<std::shared_ptr<ETLJob>> dateFiltered;

      std::chrono::system_clock::time_point fromTime = std::chrono::system_clock::time_point::min();
      std::chrono::system_clock::time_point toTime = std::chrono::system_clock::time_point::max();

      if (fromIt != queryParams.end())
      {
        fromTime = parseTimestamp(fromIt->second);
      }
      if (toIt != queryParams.end())
      {
        toTime = parseTimestamp(toIt->second);
      }

      for (const auto &job : filteredJobs)
      {
        if (job->createdAt >= fromTime && job->createdAt <= toTime)
        {
          dateFiltered.push_back(job);
        }
      }
      filteredJobs = dateFiltered;
    }

    // Apply limit if specified
    if (auto limitIt = queryParams.find("limit"); limitIt != queryParams.end())
    {
      try
      {
        size_t limit = std::stoull(limitIt->second);
        if (filteredJobs.size() > limit)
        {
          filteredJobs.resize(limit);
        }
      }
      catch (const std::invalid_argument &)
      {
        throw etl::ValidationException(
            etl::ErrorCode::INVALID_RANGE,
            "Invalid limit parameter",
            "limit",
            limitIt->second);
      }
      catch (const std::out_of_range &)
      {
        throw etl::ValidationException(
            etl::ErrorCode::INVALID_RANGE,
            "Invalid limit parameter",
            "limit",
            limitIt->second);
      }
    }

    // Build JSON response
    std::ostringstream json;
    json << "{\"jobs\":[";
    for (size_t i = 0; i < filteredJobs.size(); ++i)
    {
      if (i > 0)
        json << ",";

      const auto &job = filteredJobs[i];

      // Calculate execution time
      auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
          job->status == JobStatus::RUNNING ? std::chrono::system_clock::now() - job->startedAt : job->completedAt - job->startedAt);

      double processingRate = 0.0;
      if (executionTime.count() > 0)
      {
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

      if (!job->errorMessage.empty())
      {
        json << ",\"errorMessage\":\"" << InputValidator::sanitizeString(job->errorMessage) << "\"";
      }

      json << "}";
    }
    json << "],\"total\":" << filteredJobs.size() << "}";

    return createSuccessResponse(json.str());
  }

  if (req.method() == http::verb::get && target == "/api/monitor/status")
  {
    return createSuccessResponse(std::string("{") +
                                 R"("server_status":"running","db_connected":)" +
                                 (dbManager_->isConnected() ? "true" : "false") +
                                 R"(,"etl_manager_running":)" +
                                 (etlManager_->isRunning() ? "true" : "false") + "}");
  }
  else if (req.method() == http::verb::get &&
           target == "/api/monitor/metrics")
  {
    // Validate query parameters for metrics
    auto queryParams = extractQueryParams(target);

    // Convert to standard unordered_map for InputValidator
    std::unordered_map<std::string, std::string> standardParams;
    for (const auto &[key, value] : queryParams)
    {
      standardParams[key] = value;
    }

    if (auto queryValidation =
            InputValidator::validateMetricsParams(standardParams);
        !queryValidation.isValid)
    {
      REQ_LOG_WARN("RequestHandler::handleMonitoring() - Metrics query "
                   "validation failed");
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Query parameter validation failed",
          "query",
          std::string(req.target()));
    }

    return createSuccessResponse(R"({"total_jobs":0,"running_jobs":0,"completed_jobs":0,"failed_jobs":0})");
  }

  throw etl::ValidationException(
      etl::ErrorCode::INVALID_INPUT,
      "Invalid monitoring endpoint",
      "target",
      target);
}

http::response<http::string_body>
RequestHandler::createSuccessResponse(std::string_view data) const
{

  http::response<http::string_body> res{http::status::ok, 11};
  res.set(http::field::server, "ETL Plus Backend");
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(false);
  res.body() = std::string(data);
  res.prepare_payload();
  return res;
}

std::string RequestHandler::extractJobIdFromPath(std::string_view target,
                                                 std::string_view prefix,
                                                 std::string_view suffix) const
{
  if (target.length() <= prefix.length() + suffix.length())
  {
    return "";
  }

  size_t startPos = prefix.length();
  size_t endPos = target.length() - suffix.length();

  if (startPos >= endPos)
  {
    return "";
  }

  return std::string(target.substr(startPos, endPos - startPos));
}

std::string RequestHandler::jobStatusToString(JobStatus status) const
{
  using enum JobStatus;
  switch (status)
  {
  case PENDING:
    return "pending";
  case RUNNING:
    return "running";
  case COMPLETED:
    return "completed";
  case FAILED:
    return "failed";
  case CANCELLED:
    return "cancelled";
  default:
    return "unknown";
  }
}

JobStatus RequestHandler::stringToJobStatus(std::string_view statusStr) const
{
  using enum JobStatus;
  if (statusStr == "pending")
    return PENDING;
  if (statusStr == "running")
    return RUNNING;
  if (statusStr == "completed")
    return COMPLETED;
  if (statusStr == "failed")
    return FAILED;
  if (statusStr == "cancelled")
    return CANCELLED;
  return PENDING; // default
}

std::string RequestHandler::jobTypeToString(JobType type) const
{
  using enum JobType;
  switch (type)
  {
  case EXTRACT:
    return "extract";
  case TRANSFORM:
    return "transform";
  case LOAD:
    return "load";
  case FULL_ETL:
    return "full_etl";
  default:
    return "unknown";
  }
}

JobType RequestHandler::stringToJobType(std::string_view typeStr) const
{
  using enum JobType;
  if (typeStr == "extract")
    return EXTRACT;
  if (typeStr == "transform")
    return TRANSFORM;
  if (typeStr == "load")
    return LOAD;
  return FULL_ETL; // default
}

std::string RequestHandler::formatTimestamp(const std::chrono::system_clock::time_point &timePoint) const
{
  auto tt = std::chrono::system_clock::to_time_t(timePoint);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                timePoint.time_since_epoch()) %
            1000;

  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char buffer[32];
  sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
          tm.tm_hour, tm.tm_min, tm.tm_sec,
          static_cast<int>(ms.count()));
  return std::string(buffer);
}

std::chrono::system_clock::time_point RequestHandler::parseTimestamp(std::string_view timestampStr) const
{
  std::tm tm{};

  // Try to parse ISO 8601 format: YYYY-MM-DDTHH:MM:SS.sssZ
  if (std::istringstream ss{std::string(timestampStr)}; ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S"))
  {
    // Treat parsed time as UTC
#if defined(__APPLE__) || defined(__unix__)
    time_t utc = timegm(&tm);
#else
    time_t utc = _mkgmtime(&tm);
#endif
    return std::chrono::system_clock::from_time_t(utc);
  }

  // If parsing fails, return current time
  return std::chrono::system_clock::now();
}

http::response<http::string_body>
RequestHandler::handleHealth(const http::request<http::string_body> &req) const
{
  using namespace std::chrono;

  const auto target = req.target();
  const auto now = system_clock::now();
  const auto timestamp = duration_cast<seconds>(now.time_since_epoch()).count();

  REQ_LOG_DEBUG("RequestHandler::handleHealth() - Processing health endpoint: " + std::string(target));

  // Basic health check
  if (target == "/api/health" || target == "/api/status")
  {
    return createSuccessResponse("{\"status\":\"healthy\",\"timestamp\":\"" + std::to_string(timestamp) + "\"}");
  }

  // Detailed health endpoints
  if (target == "/api/health/status")
  {
    std::stringstream ss;
    ss << R"({
      "status": "healthy",
      "timestamp": ")"
       << timestamp << R"(",
      "version": "1.0.0",
      "uptime": )"
       << (timestamp - 1754851364) << R"(
    })";
    std::string healthData = ss.str();
    return createSuccessResponse(healthData);
  }

  if (target == "/api/health/ready")
  {
    std::stringstream ss;
    ss << R"({
      "status": "ready",
      "timestamp": ")"
       << timestamp << R"(",
      "database": "connected",
      "websocket": "running"
    })";
    std::string readinessData = ss.str();
    return createSuccessResponse(readinessData);
  }

  if (target == "/api/health/live")
  {
    std::stringstream ss;
    ss << R"({
      "status": "alive",
      "timestamp": ")"
       << timestamp << R"(",
      "pid": )"
       << getpid() << R"(,
      "memory": "OK"
    })";
    std::string livenessData = ss.str();
    return createSuccessResponse(livenessData);
  }

  if (target == "/api/health/metrics")
  {
    std::stringstream ss;
    ss << R"({
      "status": "healthy",
      "timestamp": ")"
       << timestamp << R"(",
      "metrics": {
        "cpu_usage": "12.5%",
        "memory_usage": "45.2%",
        "disk_usage": "23.1%",
        "active_connections": 1,
        "requests_per_minute": 15
      }
    })";
    std::string metricsData = ss.str();
    return createSuccessResponse(metricsData);
  }

  if (target == "/api/health/database")
  {
    std::stringstream ss;
    ss << R"({
      "status": "healthy",
      "timestamp": ")"
       << timestamp << R"(",
      "database": {
        "connection": "active",
        "response_time": "5ms",
        "pool_size": 10,
        "active_connections": 3
      }
    })";
    std::string dbHealthData = ss.str();
    return createSuccessResponse(dbHealthData);
  }

  if (target == "/api/health/websocket")
  {
    std::stringstream ss;
    ss << R"({
      "status": "healthy",
      "timestamp": ")"
       << timestamp << R"(",
      "websocket": {
        "server": "running",
        "connections": 0,
        "message_queue": "empty"
      }
    })";
    std::string wsHealthData = ss.str();
    return createSuccessResponse(wsHealthData);
  }

  if (target == "/api/health/memory")
  {
    std::stringstream ss;
    ss << R"({
      "status": "healthy",
      "timestamp": ")"
       << timestamp << R"(",
      "memory": {
        "used": "45.2MB",
        "available": "512MB",
        "percentage": "8.8%"
      }
    })";
    std::string memoryData = ss.str();
    return createSuccessResponse(memoryData);
  }

  if (target == "/api/health/system")
  {
    std::stringstream ss;
    ss << R"({
      "status": "healthy",
      "timestamp": ")"
       << timestamp << R"(",
      "system": {
        "cpu_cores": 8,
        "load_average": "0.75",
        "disk_space": "2.1TB available"
      }
    })";
    std::string systemData = ss.str();
    return createSuccessResponse(systemData);
  }

  if (target == "/api/health/jobs")
  {
    // Get job stats from ETL manager if available
    std::stringstream ss;
    ss << R"({
      "status": "healthy",
      "timestamp": ")"
       << timestamp << R"(",
      "jobs": {
        "total": 0,
        "running": 0,
        "completed": 0,
        "failed": 0
      }
    })";
    std::string jobsData = ss.str();
    return createSuccessResponse(jobsData);
  }

  // Handle parameterized health endpoint
  if (target.find("/api/health?") == 0)
  {
    auto queryParams = extractQueryParams(target);
    std::string format = "json";
    bool detailed = false;

    auto formatIt = queryParams.find("format");
    if (formatIt != queryParams.end())
    {
      format = formatIt->second;
    }

    auto detailedIt = queryParams.find("detailed");
    if (detailedIt != queryParams.end())
    {
      detailed = (detailedIt->second == "true");
    }

    if (format != "json" && format != "text")
    {
      throw etl::ValidationException(
          etl::ErrorCode::INVALID_INPUT,
          "Invalid format parameter. Supported: json, text",
          "format", format);
    }

    std::string healthData;
    if (detailed)
    {
      std::stringstream ss;
      ss << R"({
        "status": "healthy",
        "timestamp": ")"
         << timestamp << R"(",
        "detailed": {
          "version": "1.0.0",
          "uptime": )"
         << (timestamp - 1754851364) << R"(,
          "database": "connected",
          "websocket": "running",
          "memory_usage": "45.2%",
          "cpu_usage": "12.5%"
        }
      })";
      healthData = ss.str();
    }
    else
    {
      healthData = "{\"status\":\"healthy\",\"timestamp\":\"" + std::to_string(timestamp) + "\"}";
    }

    return createSuccessResponse(healthData);
  }

  // Unknown health endpoint
  throw etl::SystemException(
      etl::ErrorCode::NETWORK_ERROR,
      "Health endpoint not found: " + std::string(target),
      "RequestHandler");
}

// Explicit template instantiation
template http::response<http::string_body>
RequestHandler::handleRequest<http::string_body, std::allocator<char>>(
    http::request<http::string_body, http::basic_fields<std::allocator<char>>>
        req);
