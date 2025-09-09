#pragma once

#include "etl_exceptions.hpp"
#include "input_validator.hpp"
#include "transparent_string_hash.hpp"
#include <boost/beast/http.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace http = boost::beast::http;

/**
 * @brief Comprehensive request validation component
 *
 * This class extracts and centralizes all request validation logic from
 * RequestHandler, providing a clean separation of concerns and reusable
 * validation utilities. It handles input validation, security checks, parameter
 * extraction, and request routing validation.
 */
class RequestValidator {
public:
  /**
   * @brief Configuration for request validation
   */
  struct ValidationConfig {
    size_t maxRequestSize = 1024 * 1024; // 1MB default
    size_t maxHeaderCount = 50;
    size_t maxHeaderSize = 8192; // 8KB per header
    size_t maxQueryParamCount = 100;
    size_t maxPathLength = 2048;
    std::chrono::milliseconds requestTimeout{30000}; // 30 seconds

    // Security settings
    bool enableXssProtection = true;
    bool enableSqlInjectionProtection = true;
    bool enableCsrfProtection = true;
    bool requireHttps = false;

    // Rate limiting
    size_t maxRequestsPerMinute = 1000;

    ValidationConfig() = default;
  };

  /**
   * @brief Comprehensive validation result
   */
  struct ValidationResult {
    bool isValid = true;
    std::vector<InputValidator::ValidationError> errors;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> queryParams;
    std::string extractedPath;
    std::string method;

    void addError(const std::string &field, const std::string &message,
                  const std::string &code = "INVALID_INPUT") {
      errors.emplace_back(field, message, code);
      isValid = false;
    }

    std::string toJsonString() const;
  };

  /**
   * @brief Security validation result
   */
  struct SecurityValidationResult {
    bool isSecure = true;
    std::vector<std::string> securityIssues;
    std::string clientIp;
    std::string userAgent;
    bool rateLimitExceeded = false;

    void addIssue(const std::string &issue) {
      securityIssues.push_back(issue);
      isSecure = false;
    }
  };

  /**
   * @brief Statistics and monitoring
   */
  struct ValidationStats {
    size_t totalRequests = 0;
    size_t validRequests = 0;
    size_t invalidRequests = 0;
    size_t securityViolations = 0;
    size_t rateLimitViolations = 0;
    std::chrono::system_clock::time_point lastReset;

    ValidationStats() : lastReset(std::chrono::system_clock::now()) {}
  };

public:
  explicit RequestValidator(ValidationConfig config);

  // Main validation methods
  ValidationResult validateRequest(const http::request<http::string_body> &req);
  ValidationResult
  validateRequestBasics(const http::request<http::string_body> &req);
  SecurityValidationResult
  validateSecurity(const http::request<http::string_body> &req);

  // HTTP method validation
  bool isValidMethod(const std::string &method, const std::string &endpoint);
  ValidationResult validateMethodForEndpoint(const std::string &method,
                                             const std::string &path);

  // Path and routing validation
  ValidationResult validatePath(std::string_view path);
  ValidationResult validateEndpoint(const std::string &method,
                                    const std::string &path);
  bool isKnownEndpoint(const std::string &path);

  // Header validation
  ValidationResult validateHeaders(const http::request<http::string_body> &req);
  std::unordered_map<std::string, std::string>
  extractHeaders(const http::request<http::string_body> &req);

  // Query parameter validation
  ValidationResult validateQueryParameters(std::string_view target);
  std::unordered_map<std::string, std::string>
  extractQueryParams(std::string_view target);

  // Body validation
  ValidationResult validateBody(const std::string &body,
                                const std::string &contentType);
  ValidationResult validateJsonBody(const std::string &body);

  // Authentication validation
  ValidationResult validateAuthenticationHeader(const std::string &authHeader);
  ValidationResult validateBearerToken(const std::string &token);

  // Content validation
  ValidationResult validateContentType(const std::string &contentType,
                                       const std::string &endpoint);
  ValidationResult validateContentLength(size_t contentLength);

  // Endpoint-specific validation
  ValidationResult
  validateAuthEndpoint(const http::request<http::string_body> &req);
  ValidationResult
  validateJobsEndpoint(const http::request<http::string_body> &req);
  ValidationResult
  validateLogsEndpoint(const http::request<http::string_body> &req);
  ValidationResult
  validateMonitoringEndpoint(const http::request<http::string_body> &req);
  ValidationResult
  validateHealthEndpoint(const http::request<http::string_body> &req);

  // Security validation methods
  bool checkRateLimit(const std::string &clientIp);
  bool validateCsrfToken(const std::string &token,
                         const std::string &sessionId);
  bool checkForSqlInjection(const std::string &input);
  bool checkForXssAttempts(const std::string &input);
  bool validateHttpsRequirement(const http::request<http::string_body> &req);

  // Utility methods
  std::string extractClientIp(const http::request<http::string_body> &req);
  std::string extractUserAgent(const http::request<http::string_body> &req);
  std::string extractJobIdFromPath(std::string_view target,
                                   std::string_view prefix,
                                   std::string_view suffix);
  std::string extractConnectionIdFromPath(const std::string &target,
                                          const std::string &prefix);

  // Configuration management
  void updateConfig(const ValidationConfig &newConfig);
  const ValidationConfig &getConfig() const { return config_; }

  // Statistics
  ValidationStats getStats() const { return stats_; }
  void resetStats();

private:
  ValidationConfig config_;
  mutable ValidationStats stats_;

  // Rate limiting storage (in production, this would be Redis or similar)
  mutable std::unordered_map<std::string,
                             std::vector<std::chrono::system_clock::time_point>>
      rateLimitMap_;
  mutable std::mutex rateLimitMutex_;

  // Known endpoints for validation
  std::unordered_set<std::string> knownEndpoints_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      allowedMethodsPerEndpoint_;

  // Helper methods
  void initializeKnownEndpoints();
  void initializeAllowedMethods();
  bool isPathParameterized(const std::string &path);
  std::string normalizeEndpointPath(const std::string &path);
  ValidationResult validateParameterizedPath(const std::string &path,
                                             const std::string &pattern);

  // Security helpers
  void updateRateLimit(const std::string &clientIp);
  bool isValidHeaderName(const std::string &name);
  bool isValidHeaderValue(const std::string &value);
  std::string sanitizeLogString(const std::string &input);
};