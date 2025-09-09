#pragma once

#include "etl_exceptions.hpp"
#include "etl_job_manager.hpp"
#include "exception_mapper.hpp"
#include "hana_exception_handling.hpp"
#include "input_validator.hpp"
#include "job_monitoring_models.hpp"
#include "logger.hpp"
#include "rate_limiter.hpp"
#include "transparent_string_hash.hpp"
#include "websocket_manager.hpp"
#include <boost/beast/http.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace http = boost::beast::http;

class DatabaseManager;
class AuthManager;
class ETLJobManager;
class JobMonitorService;

class RequestHandler {
public:
  RequestHandler(std::shared_ptr<DatabaseManager> dbManager,
                 std::shared_ptr<AuthManager> authManager,
                 std::shared_ptr<ETLJobManager> etlManager);

  template <class Body, class Allocator>
  http::response<http::string_body>
  handleRequest(http::request<Body, http::basic_fields<Allocator>> req);

  // Add getters for testing purposes
  std::shared_ptr<ETLJobManager> getJobManager() { return etlManager_; }
  std::shared_ptr<JobMonitorService> getJobMonitorService() {
    return monitorService_;
  }

private:
  std::shared_ptr<DatabaseManager> dbManager_;
  std::shared_ptr<AuthManager> authManager_;
  std::shared_ptr<ETLJobManager> etlManager_;
  std::shared_ptr<JobMonitorService> monitorService_; // Add this member
  std::unique_ptr<RateLimiter> rateLimiter_;
  ETLPlus::ExceptionHandling::ExceptionMapper exceptionMapper_;

  // Hana-based exception handling registry for better type safety
  ETLPlus::ExceptionHandling::HanaExceptionRegistry hanaExceptionRegistry_;

  // JWT validation middleware
#ifdef ETL_ENABLE_JWT
  std::optional<std::string> validateJWTToken(const http::request<http::string_body> &req) const;
  bool isProtectedEndpoint(std::string_view target) const;
#endif

  // Rate limiting middleware
  std::string getClientId(const http::request<http::string_body> &req) const;
  bool checkRateLimit(const http::request<http::string_body> &req) const;
  void addRateLimitHeaders(http::response<http::string_body> &res, const std::string &clientId, const std::string &endpoint);

  // Enhanced validation methods
  http::response<http::string_body>
  validateAndHandleRequest(const http::request<http::string_body> &req) const;
  InputValidator::ValidationResult
  validateRequestBasics(const http::request<http::string_body> &req) const;
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
  extractHeaders(const http::request<http::string_body> &req) const;
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
  extractQueryParams(std::string_view target) const;

  // Request handlers with validation
  http::response<http::string_body>
  handleAuth(const http::request<http::string_body> &req) const;
  http::response<http::string_body>
  handleLogs(const http::request<http::string_body> &req) const;
  http::response<http::string_body>
  handleETLJobs(const http::request<http::string_body> &req) const;
  http::response<http::string_body>
  handleMonitoring(const http::request<http::string_body> &req) const;
  http::response<http::string_body>
  handleHealth(const http::request<http::string_body> &req) const;

  // Response creation methods
  http::response<http::string_body>
  createSuccessResponse(std::string_view data) const;

  // Utility methods for job monitoring endpoints
  std::string extractJobIdFromPath(std::string_view target,
                                   std::string_view prefix,
                                   std::string_view suffix) const;
  std::string jobStatusToString(JobStatus status) const;
  JobStatus stringToJobStatus(std::string_view statusStr) const;
  std::string jobTypeToString(JobType type) const;
  JobType stringToJobType(std::string_view typeStr) const;
  std::string
  formatTimestamp(const std::chrono::system_clock::time_point &timePoint) const;
  std::chrono::system_clock::time_point
  parseTimestamp(std::string_view timestampStr) const;

  // Log level conversion helpers
  LogLevel stringToLogLevel(const std::string &levelStr) const;
  std::string levelToString(LogLevel level) const;

  // WebSocket filter management methods
  http::response<http::string_body>
  handleGetConnectionFilters(const std::string &connectionId);
  http::response<http::string_body>
  handleSetConnectionFilters(const std::string &connectionId,
                             const std::string &requestBody);
  http::response<http::string_body>
  handleUpdateConnectionFilters(const std::string &connectionId,
                                const std::string &requestBody);
  http::response<http::string_body>
  handleAddJobFilter(const std::string &connectionId, const std::string &jobId);
  http::response<http::string_body>
  handleRemoveJobFilter(const std::string &connectionId,
                        const std::string &jobId);
  http::response<http::string_body>
  handleAddMessageTypeFilter(const std::string &connectionId,
                             const std::string &messageType);
  http::response<http::string_body>
  handleRemoveMessageTypeFilter(const std::string &connectionId,
                                const std::string &messageType);
  http::response<http::string_body>
  handleAddLogLevelFilter(const std::string &connectionId,
                          const std::string &logLevel);
  http::response<http::string_body>
  handleRemoveLogLevelFilter(const std::string &connectionId,
                             const std::string &logLevel);
  http::response<http::string_body>
  handleClearConnectionFilters(const std::string &connectionId);
  http::response<http::string_body> handleGetConnectionStats();
  http::response<http::string_body>
  handleTestConnectionFilter(const std::string &connectionId,
                             const std::string &requestBody);

  // Utility methods for WebSocket filter management
  std::string extractConnectionIdFromPath(const std::string &target,
                                          const std::string &prefix);
  ConnectionFilters parseConnectionFiltersFromJson(const std::string &json);
  WebSocketMessage parseWebSocketMessageFromJson(const std::string &json);
  std::string connectionFiltersToJson(const ConnectionFilters &filters);
  std::string connectionStatsToJson() const;

  // Internal helpers to reduce complexity
  http::response<http::string_body>
  handleJobStatus(const std::string &target) const;
  http::response<http::string_body>
  handleJobMetrics(const std::string &target) const;
  http::response<http::string_body> listJobs(std::string_view target) const;
  http::response<http::string_body> createJob(const std::string &body) const;
  http::response<http::string_body> updateJob(const std::string &target,
                                              const std::string &body) const;

  http::response<http::string_body> monitorJobs(std::string_view target) const;
  http::response<http::string_body> monitorStatus() const;
  http::response<http::string_body>
  monitorMetrics(std::string_view target) const;
};
