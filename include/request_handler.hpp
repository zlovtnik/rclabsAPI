#pragma once

#include "exceptions.hpp"
#include "input_validator.hpp"
#include "etl_job_manager.hpp"
#include <boost/beast/http.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>

namespace http = boost::beast::http;

class DatabaseManager;
class AuthManager;
class ETLJobManager;

class RequestHandler {
public:
  RequestHandler(std::shared_ptr<DatabaseManager> dbManager,
                 std::shared_ptr<AuthManager> authManager,
                 std::shared_ptr<ETLJobManager> etlManager);

  template <class Body, class Allocator>
  http::response<http::string_body>
  handleRequest(http::request<Body, http::basic_fields<Allocator>> &&req);

private:
  std::shared_ptr<DatabaseManager> dbManager_;
  std::shared_ptr<AuthManager> authManager_;
  std::shared_ptr<ETLJobManager> etlManager_;

  // Enhanced validation methods
  http::response<http::string_body>
  validateAndHandleRequest(const http::request<http::string_body> &req);
  InputValidator::ValidationResult
  validateRequestBasics(const http::request<http::string_body> &req);
  std::unordered_map<std::string, std::string>
  extractHeaders(const http::request<http::string_body> &req);
  std::unordered_map<std::string, std::string>
  extractQueryParams(const std::string &target);

  // Request handlers with validation
  http::response<http::string_body>
  handleAuth(const http::request<http::string_body> &req);
  http::response<http::string_body>
  handleETLJobs(const http::request<http::string_body> &req);
  http::response<http::string_body>
  handleMonitoring(const http::request<http::string_body> &req);

  // Response creation methods
  http::response<http::string_body>
  createErrorResponse(http::status status, const std::string &message);
  http::response<http::string_body>
  createExceptionResponse(const ETLPlus::Exceptions::BaseException &ex);
  http::response<http::string_body>
  createValidationErrorResponse(const InputValidator::ValidationResult &result);
  http::response<http::string_body>
  createSuccessResponse(const std::string &data);

  // Utility methods for job monitoring endpoints
  std::string extractJobIdFromPath(const std::string& target, 
                                   const std::string& prefix, 
                                   const std::string& suffix);
  std::string jobStatusToString(JobStatus status);
  JobStatus stringToJobStatus(const std::string& statusStr);
  std::string jobTypeToString(JobType type);
  JobType stringToJobType(const std::string& typeStr);
  std::string formatTimestamp(const std::chrono::system_clock::time_point& timePoint);
  std::chrono::system_clock::time_point parseTimestamp(const std::string& timestampStr);
};
