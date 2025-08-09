#pragma once

#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Input validation utility class for API endpoints
 *
 * This class provides comprehensive validation methods for different types
 * of input data including JSON structure, field types, string formats,
 * and business logic constraints.
 */
class InputValidator {
public:
  /**
   * @brief Validation error structure
   */
  struct ValidationError {
    std::string field;
    std::string message;
    std::string code;

    ValidationError(const std::string &f, const std::string &m,
                    const std::string &c = "INVALID_INPUT")
        : field(f), message(m), code(c) {}
  };

  /**
   * @brief Validation result containing errors if any
   */
  struct ValidationResult {
    bool isValid;
    std::vector<ValidationError> errors;

    ValidationResult() : isValid(true) {}

    void addError(const std::string &field, const std::string &message,
                  const std::string &code = "INVALID_INPUT") {
      errors.emplace_back(field, message, code);
      isValid = false;
    }

    std::string toJsonString() const;
  };

  // JSON validation methods
  static ValidationResult validateJson(const std::string &json);
  static ValidationResult
  validateJsonStructure(const std::string &json,
                        const std::vector<std::string> &requiredFields);

  // Field type validation methods
  static bool isValidString(const std::string &value, size_t minLength = 0,
                            size_t maxLength = SIZE_MAX);
  static bool isValidEmail(const std::string &email);
  static bool isValidPassword(const std::string &password);
  static bool isValidJobId(const std::string &jobId);
  static bool isValidUserId(const std::string &userId);
  static bool isValidToken(const std::string &token);

  // Authentication endpoint validation
  static ValidationResult validateLoginRequest(const std::string &json);
  static ValidationResult validateLogoutRequest(const std::string &json);

  // ETL job endpoint validation
  static ValidationResult validateJobCreationRequest(const std::string &json);
  static ValidationResult validateJobUpdateRequest(const std::string &json);
  static ValidationResult validateJobQueryParams(
      const std::unordered_map<std::string, std::string> &params);

  // Monitoring endpoint validation
  static ValidationResult validateMonitoringParams(
      const std::unordered_map<std::string, std::string> &params);

  // URL and path validation
  static ValidationResult validateEndpointPath(const std::string &path);
  static ValidationResult
  validateQueryParameters(const std::string &queryString);

  // HTTP method validation
  static bool isValidHttpMethod(const std::string &method,
                                const std::vector<std::string> &allowedMethods);

  // Content type validation
  static bool isValidContentType(const std::string &contentType);

  // Authorization header validation
  static ValidationResult
  validateAuthorizationHeader(const std::string &authHeader);

  // Rate limiting and security validation
  static bool isValidRequestSize(size_t contentLength,
                                 size_t maxSize = 1024 * 1024); // 1MB default
  static ValidationResult validateRequestHeaders(
      const std::unordered_map<std::string, std::string> &headers);

  // Utility methods
  static std::string extractJsonField(const std::string &json,
                                      const std::string &field);
  static std::unordered_map<std::string, std::string>
  parseQueryString(const std::string &queryString);
  static std::string sanitizeString(const std::string &input);

private:
  // Regex patterns for validation
  static const std::regex emailPattern_;
  static const std::regex jobIdPattern_;
  static const std::regex userIdPattern_;
  static const std::regex tokenPattern_;
  static const std::regex pathPattern_;

  // Helper methods
  static bool isValidJsonStructure(const std::string &json);
  static std::optional<size_t> findJsonFieldStart(const std::string &json,
                                                  const std::string &field);
  static std::string extractJsonValue(const std::string &json, size_t start,
                                      size_t end);
  static bool containsSqlInjection(const std::string &input);
  static bool containsXss(const std::string &input);
};
