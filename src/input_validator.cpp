#include "input_validator.hpp"
#include "job_monitoring_models.hpp"
#include "logger.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>

// Static regex patterns initialization
const std::regex InputValidator::emailPattern_(
    R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
const std::regex InputValidator::jobIdPattern_(R"(^[a-zA-Z0-9_-]{1,64}$)");
const std::regex InputValidator::userIdPattern_(R"(^[a-zA-Z0-9_-]{1,32}$)");
const std::regex InputValidator::tokenPattern_(R"(^[a-zA-Z0-9._-]{10,512}$)");
const std::regex InputValidator::pathPattern_(R"(^/api/[a-zA-Z0-9/_-]*$)");

namespace {
// Utility function to normalize status values to uppercase
std::string normalizeStatus(const std::string &status) {
  std::string normalized = status;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 ::toupper);
  return normalized;
}

// Static regex for ISO 8601 timestamps to avoid recompilation
const std::regex
    kIso8601MsZ(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{3})?Z?$)");

// Parse UTC timestamp string to time_point
static std::optional<std::chrono::system_clock::time_point>
parseUtc(const std::string &ts) {
  std::tm tm = {};
  std::istringstream ss(ts);
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (ss.fail())
    return std::nullopt;

  // Handle optional milliseconds
  if (ss.peek() == '.') {
    ss.get(); // consume '.'
    int ms;
    ss >> ms;
  }

  // Handle optional 'Z'
  if (ss.peek() == 'Z') {
    ss.get();
  }

  auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
  return tp;
}

// Check if status string is valid (case-insensitive)
static bool isValidStatus(std::string_view s) {
  std::string v(s);
  std::transform(v.begin(), v.end(), v.begin(), ::tolower);
  static const std::unordered_set<std::string> ok = {
      "pending", "running", "completed", "failed", "cancelled"};
  return ok.count(v) > 0;
}
} // namespace

std::string InputValidator::ValidationResult::toJsonString() const {
  nlohmann::json j;
  j["valid"] = isValid;
  if (!errors.empty()) {
    j["errors"] = nlohmann::json::array();
    for (const auto &e : errors) {
      j["errors"].push_back(
          {{"field", e.field}, {"message", e.message}, {"code", e.code}});
    }
  }
  return j.dump();
}

InputValidator::ValidationResult
InputValidator::validateJson(const std::string &json) {
  ValidationResult result;

  if (json.empty()) {
    result.addError("json", "Empty JSON body", "EMPTY_BODY");
    return result;
  }

  if (json.length() > 1024 * 1024) { // 1MB limit
    result.addError("json", "JSON body too large", "BODY_TOO_LARGE");
    return result;
  }

  // Basic JSON structure validation
  if (!isValidJsonStructure(json)) {
    result.addError("json", "Invalid JSON format", "INVALID_JSON");
    return result;
  }

  // Check for potential security issues
  if (containsSqlInjection(json)) {
    result.addError("json", "Potential SQL injection detected",
                    "SECURITY_VIOLATION");
    return result;
  }

  if (containsXss(json)) {
    result.addError("json", "Potential XSS attack detected",
                    "SECURITY_VIOLATION");
    return result;
  }

  return result;
}

InputValidator::ValidationResult InputValidator::validateJsonStructure(
    const std::string &json, const std::vector<std::string> &requiredFields) {
  ValidationResult result = validateJson(json);

  if (!result.isValid) {
    return result;
  }

  // Check for required fields
  for (const auto &field : requiredFields) {
    std::string value = extractJsonField(json, field);
    if (value.empty()) {
      result.addError(field, "Required field is missing", "MISSING_FIELD");
    }
  }

  return result;
}

bool InputValidator::isValidString(const std::string &value, size_t minLength,
                                   size_t maxLength) {
  if (value.length() < minLength || value.length() > maxLength) {
    return false;
  }

  // Check for null bytes and control characters
  for (char c : value) {
    if (c == '\0' || (c < 32 && c != '\t' && c != '\n' && c != '\r')) {
      return false;
    }
  }

  return true;
}

bool InputValidator::isValidEmail(const std::string &email) {
  if (!isValidString(email, 5, 254)) { // RFC 5322 limit
    return false;
  }

  return std::regex_match(email, emailPattern_);
}

bool InputValidator::isValidPassword(const std::string &password) {
  if (!isValidString(password, 8, 128)) {
    return false;
  }

  // Check for at least one uppercase, one lowercase, one digit
  bool hasUpper = false, hasLower = false, hasDigit = false;
  for (char c : password) {
    if (std::isupper(c))
      hasUpper = true;
    else if (std::islower(c))
      hasLower = true;
    else if (std::isdigit(c))
      hasDigit = true;
  }

  return hasUpper && hasLower && hasDigit;
}

bool InputValidator::isValidJobId(const std::string &jobId) {
  return std::regex_match(jobId, jobIdPattern_);
}

bool InputValidator::isValidUserId(const std::string &userId) {
  return std::regex_match(userId, userIdPattern_);
}

bool InputValidator::isValidToken(const std::string &token) {
  return std::regex_match(token, tokenPattern_);
}

InputValidator::ValidationResult
InputValidator::validateLoginRequest(const std::string &json) {
  ValidationResult result =
      validateJsonStructure(json, {"username", "password"});

  if (!result.isValid) {
    return result;
  }

  std::string username = extractJsonField(json, "username");
  std::string password = extractJsonField(json, "password");

  // Validate username (can be email or username)
  if (!isValidString(username, 3, 100)) {
    result.addError("username", "Username must be between 3 and 100 characters",
                    "INVALID_USERNAME");
  } else if (username.find('@') != std::string::npos &&
             !isValidEmail(username)) {
    result.addError("username", "Invalid email format", "INVALID_EMAIL");
  } else if (username.find('@') == std::string::npos &&
             !std::regex_match(username, userIdPattern_)) {
    result.addError("username", "Username contains invalid characters",
                    "INVALID_USERNAME");
  }

  // Validate password
  if (!isValidString(password, 1, 128)) { // Relaxed for login
    result.addError("password", "Password length invalid", "INVALID_PASSWORD");
  }

  return result;
}

InputValidator::ValidationResult
InputValidator::validateLogoutRequest(const std::string &json) {
  ValidationResult result = validateJson(json);

  if (!result.isValid) {
    return result;
  }

  // Logout may contain optional fields like token or user_id
  std::string token = extractJsonField(json, "token");
  if (!token.empty() && !isValidToken(token)) {
    result.addError("token", "Invalid token format", "INVALID_TOKEN");
  }

  return result;
}

InputValidator::ValidationResult
InputValidator::validateJobCreationRequest(const std::string &json) {
  ValidationResult result =
      validateJsonStructure(json, {"type", "source_config", "target_config"});

  if (!result.isValid) {
    return result;
  }

  std::string type = extractJsonField(json, "type");
  std::string sourceConfig = extractJsonField(json, "source_config");
  std::string targetConfig = extractJsonField(json, "target_config");

  // Validate job type
  if (type != "FULL_ETL" && type != "INCREMENTAL_ETL" && type != "DATA_SYNC" &&
      type != "VALIDATION") {
    result.addError("type", "Invalid job type", "INVALID_JOB_TYPE");
  }

  // Validate source config
  if (!isValidString(sourceConfig, 1, 1024)) {
    result.addError("source_config",
                    "Source config must be between 1 and 1024 characters",
                    "INVALID_SOURCE_CONFIG");
  }

  // Validate target config
  if (!isValidString(targetConfig, 1, 1024)) {
    result.addError("target_config",
                    "Target config must be between 1 and 1024 characters",
                    "INVALID_TARGET_CONFIG");
  }

  // Validate optional job_id if provided
  std::string jobId = extractJsonField(json, "job_id");
  if (!jobId.empty() && !isValidJobId(jobId)) {
    result.addError("job_id", "Invalid job ID format", "INVALID_JOB_ID");
  }

  return result;
}

InputValidator::ValidationResult
InputValidator::validateJobUpdateRequest(const std::string &json) {
  ValidationResult result = validateJson(json);

  if (!result.isValid) {
    return result;
  }

  // For updates, at least one field should be present
  std::string status = extractJsonField(json, "status");
  std::string config = extractJsonField(json, "config");

  if (status.empty() && config.empty()) {
    result.addError("request", "At least one field must be provided for update",
                    "NO_UPDATE_FIELDS");
    return result;
  }

  // Validate status if provided
  if (!status.empty()) {
    std::string norm = normalizeStatus(status);
    static const std::unordered_set<std::string> valid = {
        "PENDING", "RUNNING", "COMPLETED", "FAILED", "CANCELLED"};
    if (valid.find(norm) == valid.end()) {
      result.addError("status", "Invalid job status", "INVALID_STATUS");
    }
  }

  // Validate config if provided
  if (!config.empty() && !isValidString(config, 1, 2048)) {
    result.addError("config", "Config must be between 1 and 2048 characters",
                    "INVALID_CONFIG");
  }

  return result;
}

InputValidator::ValidationResult InputValidator::validateJobQueryParams(
    const std::unordered_map<std::string, std::string> &params) {
  ValidationResult result;

  for (const auto &[key, value] : params) {
    if (key == "status") {
      // Convert to lowercase for case-insensitive comparison
      std::string lowerValue = value;
      std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(),
                     ::tolower);

      if (!isValidStatus(lowerValue)) {
        result.addError("status", "Invalid status filter",
                        "INVALID_STATUS_FILTER");
      }
    } else if (key == "limit") {
      try {
        int limit = std::stoi(value);
        if (limit < 1 || limit > 1000) {
          result.addError("limit", "Limit must be between 1 and 1000",
                          "INVALID_LIMIT");
        }
      } catch (const std::exception &) {
        result.addError("limit", "Limit must be a valid integer",
                        "INVALID_LIMIT");
      }
    } else if (key == "offset") {
      try {
        int offset = std::stoi(value);
        if (offset < 0) {
          result.addError("offset", "Offset must be non-negative",
                          "INVALID_OFFSET");
        }
      } catch (const std::exception &) {
        result.addError("offset", "Offset must be a valid integer",
                        "INVALID_OFFSET");
      }
    } else if (key == "job_id") {
      if (!isValidJobId(value)) {
        result.addError("job_id", "Invalid job ID format", "INVALID_JOB_ID");
      }
    } else {
      result.addError(key, "Unknown query parameter", "UNKNOWN_PARAMETER");
    }
  }

  return result;
}

InputValidator::ValidationResult InputValidator::validateMetricsParams(
    const std::unordered_map<std::string, std::string> &params) {
  ValidationResult result;

  for (const auto &[key, value] : params) {
    if (key == "metric_type") {
      if (value != "performance" && value != "errors" && value != "system" &&
          value != "jobs") {
        result.addError("metric_type", "Invalid metric type",
                        "INVALID_METRIC_TYPE");
      }
    } else if (key == "time_range") {
      if (value != "1h" && value != "24h" && value != "7d" && value != "30d") {
        result.addError("time_range", "Invalid time range",
                        "INVALID_TIME_RANGE");
      }
    } else {
      result.addError(key, "Unknown metrics parameter", "UNKNOWN_PARAMETER");
    }
  }

  return result;
}

InputValidator::ValidationResult
InputValidator::validateEndpointPath(const std::string &path) {
  ValidationResult result;

  if (path.empty()) {
    result.addError("path", "Empty request path", "EMPTY_PATH");
    return result;
  }

  if (path.length() > 512) {
    result.addError("path", "Path too long", "PATH_TOO_LONG");
    return result;
  }

  if (!std::regex_match(path, pathPattern_)) {
    result.addError("path", "Invalid path format", "INVALID_PATH");
    return result;
  }

  // Check for path traversal attempts
  if (path.find("..") != std::string::npos ||
      path.find("//") != std::string::npos) {
    result.addError("path", "Path traversal detected", "SECURITY_VIOLATION");
    return result;
  }

  return result;
}

InputValidator::ValidationResult
InputValidator::validateQueryParameters(const std::string &queryString) {
  ValidationResult result;

  if (queryString.length() > 2048) {
    result.addError("query", "Query string too long", "QUERY_TOO_LONG");
    return result;
  }

  auto params = parseQueryString(queryString);

  // Check for parameter injection
  for (const auto &[key, value] : params) {
    if (containsSqlInjection(key) || containsSqlInjection(value)) {
      result.addError("query", "Potential SQL injection in query parameters",
                      "SECURITY_VIOLATION");
      return result;
    }

    if (containsXss(key) || containsXss(value)) {
      result.addError("query", "Potential XSS in query parameters",
                      "SECURITY_VIOLATION");
      return result;
    }
  }

  return result;
}

bool InputValidator::isValidHttpMethod(
    const std::string &method, const std::vector<std::string> &allowedMethods) {
  return std::find(allowedMethods.begin(), allowedMethods.end(), method) !=
         allowedMethods.end();
}

bool InputValidator::isValidContentType(const std::string &contentType) {
  return contentType == "application/json" ||
         contentType == "application/x-www-form-urlencoded" ||
         contentType.find("application/json;") == 0 ||
         contentType.find("application/x-www-form-urlencoded;") == 0;
}

InputValidator::ValidationResult
InputValidator::validateAuthorizationHeader(const std::string &authHeader) {
  ValidationResult result;

  if (authHeader.empty()) {
    result.addError("authorization", "Authorization header is required",
                    "MISSING_AUTH");
    return result;
  }

  if (authHeader.size() < 7 ||
      !std::equal(authHeader.begin(), authHeader.begin() + 7, "Bearer ",
                  [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                  })) {
    result.addError("authorization", "Invalid authorization scheme",
                    "INVALID_AUTH_SCHEME");
    return result;
  }

  std::string token = authHeader.substr(7); // Remove "Bearer "
  if (!isValidToken(token)) {
    result.addError("authorization", "Invalid token format", "INVALID_TOKEN");
  }

  return result;
}

bool InputValidator::isValidRequestSize(size_t contentLength, size_t maxSize) {
  return contentLength <= maxSize;
}

InputValidator::ValidationResult InputValidator::validateRequestHeaders(
    const std::unordered_map<std::string, std::string> &headers) {
  ValidationResult result;

  // Helper function for case-insensitive header lookup
  auto findHeader = [&](const std::string &target)
      -> std::unordered_map<std::string, std::string>::const_iterator {
    for (auto it = headers.begin(); it != headers.end(); ++it) {
      std::string lowerKey = it->first;
      std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
                     ::tolower);
      if (lowerKey == target) {
        return it;
      }
    }
    return headers.end();
  };

  // Check for required headers (case-insensitive)
  auto contentTypeIt = findHeader("content-type");
  if (contentTypeIt != headers.end()) {
    if (!isValidContentType(contentTypeIt->second)) {
      result.addError("content-type", "Unsupported content type",
                      "INVALID_CONTENT_TYPE");
    }
  }

  // Check for suspicious headers
  for (const auto &[key, value] : headers) {
    if (containsXss(key) || containsXss(value)) {
      result.addError(key, "Potential XSS in header", "SECURITY_VIOLATION");
    }

    if (value.length() > 8192) { // 8KB limit per header
      result.addError(key, "Header value too long", "HEADER_TOO_LONG");
    }
  }

  return result;
}

std::string InputValidator::extractJsonField(const std::string &json,
                                             const std::string &field) {
  auto start = findJsonFieldStart(json, field);
  if (!start) {
    return "";
  }

  size_t valueStart = *start;
  size_t valueEnd = json.find_first_of(",}\n\r", valueStart);
  if (valueEnd == std::string::npos) {
    valueEnd = json.length();
  }

  return extractJsonValue(json, valueStart, valueEnd);
}

std::unordered_map<std::string, std::string>
InputValidator::parseQueryString(const std::string &queryString) {
  std::unordered_map<std::string, std::string> params;

  if (queryString.empty()) {
    return params;
  }

  std::istringstream iss(queryString);
  std::string param;

  while (std::getline(iss, param, '&')) {
    size_t equalPos = param.find('=');
    if (equalPos != std::string::npos) {
      std::string key = param.substr(0, equalPos);
      std::string value = param.substr(equalPos + 1);
      params[key] = value;
    }
  }

  return params;
}

std::string InputValidator::sanitizeString(const std::string &input) {
  std::string result = input;

  // Replace dangerous characters
  std::replace(result.begin(), result.end(), '"', '\'');
  std::replace(result.begin(), result.end(), '\n', ' ');
  std::replace(result.begin(), result.end(), '\r', ' ');
  std::replace(result.begin(), result.end(), '\t', ' ');

  return result;
}

// Private helper methods
bool InputValidator::isValidJsonStructure(const std::string &json) {
  if (json.empty())
    return false;

  // Basic bracket matching
  int braceCount = 0;
  int bracketCount = 0;
  bool inString = false;
  bool escaped = false;

  for (size_t i = 0; i < json.length(); ++i) {
    char c = json[i];

    if (escaped) {
      escaped = false;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      continue;
    }

    if (c == '"') {
      inString = !inString;
      continue;
    }

    if (!inString) {
      if (c == '{')
        braceCount++;
      else if (c == '}')
        braceCount--;
      else if (c == '[')
        bracketCount++;
      else if (c == ']')
        bracketCount--;

      if (braceCount < 0 || bracketCount < 0) {
        return false;
      }
    }
  }

  return braceCount == 0 && bracketCount == 0 && !inString;
}

std::optional<size_t>
InputValidator::findJsonFieldStart(const std::string &json,
                                   const std::string &field) {
  std::string searchPattern = "\"" + field + "\"";
  size_t pos = json.find(searchPattern);

  if (pos == std::string::npos) {
    return std::nullopt;
  }

  // Find the colon after the field name
  size_t colonPos = json.find(':', pos + searchPattern.length());
  if (colonPos == std::string::npos) {
    return std::nullopt;
  }

  // Skip whitespace after colon
  size_t valueStart = colonPos + 1;
  while (valueStart < json.length() && std::isspace(json[valueStart])) {
    valueStart++;
  }

  return valueStart;
}

std::string InputValidator::extractJsonValue(const std::string &json,
                                             size_t start, size_t end) {
  if (start >= json.length() || start >= end) {
    return "";
  }

  std::string value = json.substr(start, end - start);

  // Remove quotes if present
  if (value.front() == '"' && value.back() == '"' && value.length() >= 2) {
    value = value.substr(1, value.length() - 2);
  }

  // Trim whitespace
  value.erase(0, value.find_first_not_of(" \t\n\r"));
  value.erase(value.find_last_not_of(" \t\n\r") + 1);

  return value;
}

bool InputValidator::containsSqlInjection(const std::string &input) {
  std::string lowerInput = input;
  std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(),
                 ::tolower);

  // Common SQL injection patterns
  std::vector<std::string> sqlPatterns = {"' or '1'='1",
                                          "' or 1=1",
                                          "'; drop table",
                                          "'; delete from",
                                          "union select",
                                          "' union select",
                                          "/*",
                                          "*/",
                                          "xp_",
                                          "sp_"};

  for (const auto &pattern : sqlPatterns) {
    if (lowerInput.find(pattern) != std::string::npos) {
      return true;
    }
  }

  return false;
}

bool InputValidator::containsXss(const std::string &input) {
  std::string lowerInput = input;
  std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(),
                 ::tolower);

  // Static set for O(1) lookups instead of linear search
  static const std::unordered_set<std::string> xssPatterns = {
      "<script",
      "</script>",
      "javascript:",
      "onload=",
      "onerror=",
      "onclick=",
      "onmouseover=",
      "<iframe",
      "eval(",
      "alert(",
      "vbscript:",
      "data:text/html",
      "data:text/javascript",
      "%3cscript",
      "%3c/script%3e",
      "&#x3c;script",
      "&#60;script",
      "onfocus=",
      "onblur=",
      "onchange=",
      "onsubmit=",
      "onreset=",
      "onselect=",
      "onkeydown=",
      "onkeypress=",
      "onkeyup=",
      "ondblclick=",
      "onmousedown=",
      "onmouseup=",
      "onmousemove=",
      "onmouseout=",
      "onmouseenter=",
      "onmouseleave="};

  for (const auto &pattern : xssPatterns) {
    if (lowerInput.find(pattern) != std::string::npos) {
      return true;
    }
  }

  return false;
}
InputValidator::ValidationResult InputValidator::validateMonitoringParams(
    const std::unordered_map<std::string, std::string> &params) {
  ValidationResult result;

  // Validate status parameter if present
  auto statusIt = params.find("status");
  if (statusIt != params.end()) {
    std::string normalizedStatus = normalizeStatus(statusIt->second);
    static const std::unordered_set<std::string> validStatuses = {
        "PENDING", "RUNNING", "COMPLETED", "FAILED", "CANCELLED"};
    if (validStatuses.find(normalizedStatus) == validStatuses.end()) {
      result.addError("status", "Invalid job status value", "INVALID_STATUS");
    }
  }

  // Validate type parameter if present
  auto typeIt = params.find("type");
  if (typeIt != params.end()) {
    const std::string &type = typeIt->second;
    if (type != "extract" && type != "transform" && type != "load" &&
        type != "full_etl") {
      result.addError("type", "Invalid job type value", "INVALID_TYPE");
    }
  }

  // Validate limit parameter if present
  auto limitIt = params.find("limit");
  if (limitIt != params.end()) {
    try {
      int limit = std::stoi(limitIt->second);
      if (limit < 1 || limit > 1000) {
        result.addError("limit", "Limit must be between 1 and 1000",
                        "INVALID_LIMIT");
      }
    } catch (const std::exception &) {
      result.addError("limit", "Limit must be a valid integer",
                      "INVALID_LIMIT");
    }
  }

  // Validate from/to timestamp parameters if present
  auto fromIt = params.find("from");
  if (fromIt != params.end()) {
    const std::string &from = fromIt->second;
    if (!std::regex_match(from, kIso8601MsZ)) {
      result.addError("from", "Invalid timestamp format, expected ISO 8601",
                      "INVALID_TIMESTAMP");
    }
  }

  auto toIt = params.find("to");
  if (toIt != params.end()) {
    const std::string &to = toIt->second;
    if (!std::regex_match(to, kIso8601MsZ)) {
      result.addError("to", "Invalid timestamp format, expected ISO 8601",
                      "INVALID_TIMESTAMP");
    }
  }

  // Validate that from is before to if both are present
  if (fromIt != params.end() && toIt != params.end()) {
    auto fromTime = parseUtc(fromIt->second);
    auto toTime = parseUtc(toIt->second);
    if (fromTime && toTime && *fromTime >= *toTime) {
      result.addError("from", "From timestamp must be before to timestamp",
                      "INVALID_RANGE");
    }
  }

  return result;
}