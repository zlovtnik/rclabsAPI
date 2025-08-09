#pragma once

#include "exceptions.hpp"
#include "input_validator.hpp"
#include "etl_job_manager.hpp"
#include "websocket_manager.hpp"
#include "job_monitoring_models.hpp"
#include "transparent_string_hash.hpp"
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
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>
  extractHeaders(const http::request<http::string_body> &req);
  std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>
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
  
  // WebSocket filter management methods
  http::response<http::string_body>
  handleGetConnectionFilters(const std::string& connectionId);
  http::response<http::string_body>
  handleSetConnectionFilters(const std::string& connectionId, const std::string& requestBody);
  http::response<http::string_body>
  handleUpdateConnectionFilters(const std::string& connectionId, const std::string& requestBody);
  http::response<http::string_body>
  handleAddJobFilter(const std::string& connectionId, const std::string& jobId);
  http::response<http::string_body>
  handleRemoveJobFilter(const std::string& connectionId, const std::string& jobId);
  http::response<http::string_body>
  handleAddMessageTypeFilter(const std::string& connectionId, const std::string& messageType);
  http::response<http::string_body>
  handleRemoveMessageTypeFilter(const std::string& connectionId, const std::string& messageType);
  http::response<http::string_body>
  handleAddLogLevelFilter(const std::string& connectionId, const std::string& logLevel);
  http::response<http::string_body>
  handleRemoveLogLevelFilter(const std::string& connectionId, const std::string& logLevel);
  http::response<http::string_body>
  handleClearConnectionFilters(const std::string& connectionId);
  http::response<http::string_body>
  handleGetConnectionStats();
  http::response<http::string_body>
  handleTestConnectionFilter(const std::string& connectionId, const std::string& requestBody);
  
  // Utility methods for WebSocket filter management
  std::string extractConnectionIdFromPath(const std::string& target, const std::string& prefix);
  ConnectionFilters parseConnectionFiltersFromJson(const std::string& json);
  WebSocketMessage parseWebSocketMessageFromJson(const std::string& json);
  std::string connectionFiltersToJson(const ConnectionFilters& filters);
  std::string connectionStatsToJson() const;
};
