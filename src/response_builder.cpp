#include "response_builder.hpp"
#include "component_logger.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

// Component logger specialization
template <> struct etl::ComponentTrait<ResponseBuilder> {
  static constexpr const char *name = "ResponseBuilder";
};

ResponseBuilder::ResponseBuilder(ResponseConfig config)
    : config_(std::move(config)),
      currentContentType_(config_.defaultContentType) {
  resetState();
  etl::ComponentLogger<ResponseBuilder>::info("ResponseBuilder initialized");
}

ResponseBuilder &ResponseBuilder::setStatus(http::status status) {
  currentStatus_ = status;
  return *this;
}

ResponseBuilder &ResponseBuilder::setContentType(ContentType type) {
  currentContentType_ = type;
  return *this;
}

ResponseBuilder &ResponseBuilder::setContentType(const std::string &mimeType) {
  currentContentType_ = stringToContentType(mimeType);
  currentHeaders_["content-type"] = mimeType;
  return *this;
}

ResponseBuilder &ResponseBuilder::setHeader(const std::string &name,
                                            const std::string &value) {
  if (isValidHeaderName(name)) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   ::tolower);
    currentHeaders_[lowerName] = sanitizeHeaderValue(value);
  } else {
    etl::ComponentLogger<ResponseBuilder>::warn("Invalid header name: " + name);
  }
  return *this;
}

ResponseBuilder &ResponseBuilder::setHeaders(
    const std::unordered_map<std::string, std::string> &headers) {
  for (const auto &[name, value] : headers) {
    setHeader(name, value);
  }
  return *this;
}

ResponseBuilder &ResponseBuilder::setCors(const CorsConfig &config) {
  config_.corsConfig = config;
  return *this;
}

ResponseBuilder &ResponseBuilder::enableCors(bool enable) {
  config_.enableCors = enable;
  return *this;
}

ResponseBuilder &ResponseBuilder::setKeepAlive(bool keepAlive) {
  currentKeepAlive_ = keepAlive;
  return *this;
}

ResponseBuilder &ResponseBuilder::setRequestId(const std::string &requestId) {
  currentRequestId_ = requestId;
  return *this;
}

ResponseBuilder &
ResponseBuilder::setMetadata(const ResponseMetadata &metadata) {
  currentMetadata_ = metadata;
  return *this;
}

http::response<http::string_body>
ResponseBuilder::success(const std::string &data) {
  currentStatus_ = http::status::ok;
  return buildResponse(data);
}

http::response<http::string_body>
ResponseBuilder::success(const std::string &data, ContentType type) {
  currentStatus_ = http::status::ok;
  currentContentType_ = type;
  return buildResponse(data);
}

http::response<http::string_body>
ResponseBuilder::successJson(const std::string &jsonData) {
  currentStatus_ = http::status::ok;
  currentContentType_ = ContentType::JSON;

  std::string responseBody;
  if (config_.includeTimestamp || config_.includeRequestId) {
    responseBody = createSuccessJson(jsonData);
  } else {
    responseBody = jsonData;
  }

  return buildResponse(responseBody);
}

http::response<http::string_body>
ResponseBuilder::successWithMessage(const std::string &message,
                                    const std::string &data) {
  currentStatus_ = http::status::ok;
  currentContentType_ = ContentType::JSON;

  std::ostringstream json;
  json << R"({"status":"success","message":")" << escapeJsonString(message)
       << R"(")";

  if (!data.empty()) {
    json << R"(,"data":)" << data;
  }

  if (config_.includeTimestamp) {
    json << R"(,"timestamp":")"
         << formatTimestamp(std::chrono::system_clock::now()) << R"(")";
  }

  if (config_.includeRequestId && !currentRequestId_.empty()) {
    json << R"(,"request_id":")" << currentRequestId_ << R"(")";
  }

  json << "}";

  return buildResponse(json.str());
}

http::response<http::string_body>
ResponseBuilder::error(http::status status, const std::string &message) {
  currentStatus_ = status;
  currentContentType_ = ContentType::JSON;

  std::string errorJson =
      createErrorJson(message, std::to_string(static_cast<int>(status)));
  return buildResponse(errorJson);
}

http::response<http::string_body>
ResponseBuilder::badRequest(const std::string &message) {
  return error(http::status::bad_request, message);
}

http::response<http::string_body>
ResponseBuilder::unauthorized(const std::string &message) {
  return error(http::status::unauthorized, message);
}

http::response<http::string_body>
ResponseBuilder::forbidden(const std::string &message) {
  return error(http::status::forbidden, message);
}

http::response<http::string_body>
ResponseBuilder::notFound(const std::string &resource) {
  return error(http::status::not_found, resource + " not found");
}

http::response<http::string_body>
ResponseBuilder::methodNotAllowed(const std::string &method,
                                  const std::string &endpoint) {
  setHeader("allow", "GET, POST, PUT, OPTIONS");
  return error(http::status::method_not_allowed,
               "Method " + method + " not allowed for " + endpoint);
}

http::response<http::string_body>
ResponseBuilder::conflict(const std::string &message) {
  return error(http::status::conflict, message);
}

http::response<http::string_body>
ResponseBuilder::tooManyRequests(const std::string &message) {
  setHeader("retry-after", "60");
  return error(http::status::too_many_requests, message);
}

http::response<http::string_body>
ResponseBuilder::internalServerError(const std::string &message) {
  return error(http::status::internal_server_error, message);
}

http::response<http::string_body>
ResponseBuilder::serviceUnavailable(const std::string &message) {
  setHeader("retry-after", "300");
  return error(http::status::service_unavailable, message);
}

http::response<http::string_body>
ResponseBuilder::fromException(const etl::ETLException &ex) {
  // Map exception codes to HTTP status codes
  http::status status = http::status::internal_server_error;

  switch (ex.getCode()) {
  case etl::ErrorCode::INVALID_INPUT:
  case etl::ErrorCode::MISSING_FIELD:
  case etl::ErrorCode::INVALID_RANGE:
    status = http::status::bad_request;
    break;

  case etl::ErrorCode::UNAUTHORIZED:
  case etl::ErrorCode::TOKEN_EXPIRED:
    status = http::status::unauthorized;
    break;

  case etl::ErrorCode::FORBIDDEN:
  case etl::ErrorCode::ACCESS_DENIED:
    status = http::status::forbidden;
    break;

  case etl::ErrorCode::JOB_NOT_FOUND:
    status = http::status::not_found;
    break;

  case etl::ErrorCode::CONSTRAINT_VIOLATION:
  case etl::ErrorCode::JOB_ALREADY_RUNNING:
  case etl::ErrorCode::INVALID_JOB_STATE:
    status = http::status::conflict;
    break;

  case etl::ErrorCode::RATE_LIMIT_EXCEEDED:
    status = http::status::too_many_requests;
    break;

  case etl::ErrorCode::COMPONENT_UNAVAILABLE:
  case etl::ErrorCode::MEMORY_ERROR:
  case etl::ErrorCode::THREAD_POOL_EXHAUSTED:
    status = http::status::service_unavailable;
    break;

  default:
    status = http::status::internal_server_error;
    break;
  }

  currentStatus_ = status;
  currentContentType_ = ContentType::JSON;

  std::string exceptionJson = createExceptionJson(ex);
  return buildResponse(exceptionJson);
}

http::response<http::string_body> ResponseBuilder::fromValidationResult(
    const InputValidator::ValidationResult &result) {
  return validationError(result);
}

http::response<http::string_body>
ResponseBuilder::fromStandardException(const std::exception &ex,
                                       const std::string &context) {
  currentStatus_ = http::status::internal_server_error;
  currentContentType_ = ContentType::JSON;

  std::string message = std::string(ex.what());
  if (!context.empty()) {
    message = context + ": " + message;
  }

  std::string errorJson = createErrorJson(message, "INTERNAL_ERROR");
  return buildResponse(errorJson);
}

http::response<http::string_body> ResponseBuilder::validationError(
    const InputValidator::ValidationResult &result) {
  currentStatus_ = http::status::bad_request;
  currentContentType_ = ContentType::JSON;

  std::string validationJson = createValidationErrorJson(result);
  return buildResponse(validationJson);
}

http::response<http::string_body>
ResponseBuilder::authenticationRequired(const std::string &realm) {
  setHeader("www-authenticate", "Bearer realm=\"" + realm + "\"");
  return error(http::status::unauthorized, "Authentication required");
}

http::response<http::string_body> ResponseBuilder::corsPreflightResponse() {
  currentStatus_ = http::status::no_content;
  currentContentType_ = ContentType::TEXT;

  // CORS preflight headers will be added by applyCorsHeaders
  return buildResponse("");
}

http::response<http::string_body>
ResponseBuilder::healthCheck(bool healthy, const std::string &details) {
  currentStatus_ =
      healthy ? http::status::ok : http::status::service_unavailable;
  currentContentType_ = ContentType::JSON;

  std::ostringstream json;
  json << R"({"status":")" << (healthy ? "healthy" : "unhealthy") << R"(")";

  if (!details.empty()) {
    json << R"(,"details":")" << escapeJsonString(details) << R"(")";
  }

  json << R"(,"timestamp":")"
       << formatTimestamp(std::chrono::system_clock::now()) << R"(")";
  json << "}";

  return buildResponse(json.str());
}

http::response<http::string_body>
ResponseBuilder::redirect(const std::string &location, http::status status) {
  currentStatus_ = status;
  setHeader("location", location);
  return buildResponse("");
}

http::response<http::string_body>
ResponseBuilder::permanentRedirect(const std::string &location) {
  return redirect(location, http::status::moved_permanently);
}

http::response<http::string_body>
ResponseBuilder::temporaryRedirect(const std::string &location) {
  return redirect(location, http::status::temporary_redirect);
}

http::response<http::string_body>
ResponseBuilder::cached(const std::string &data, std::chrono::seconds maxAge) {
  setHeader("cache-control",
            "public, max-age=" + std::to_string(maxAge.count()));
  setHeader("expires",
            formatTimestamp(std::chrono::system_clock::now() + maxAge));
  return success(data);
}

http::response<http::string_body>
ResponseBuilder::noCache(const std::string &data) {
  setHeader("cache-control", "no-cache, no-store, must-revalidate");
  setHeader("pragma", "no-cache");
  setHeader("expires", "0");
  return success(data);
}

http::response<http::string_body>
ResponseBuilder::buildResponse(const std::string &body) {
  http::response<http::string_body> response{currentStatus_, 11};

  // Set body
  response.body() = body;

  // Apply default headers
  applyDefaultHeaders(response);

  // Apply custom headers
  for (const auto &[name, value] : currentHeaders_) {
    response.set(name, value);
  }

  // Apply CORS headers if enabled
  if (config_.enableCors) {
    applyCorsHeaders(response, config_.corsConfig);
  }

  // Apply security headers
  applySecurityHeaders(response);

  // Set keep-alive
  response.keep_alive(currentKeepAlive_);

  // Prepare payload
  response.prepare_payload();

  // Update statistics
  updateStats(response);

  // Reset state for next response
  resetState();

  return response;
}

void ResponseBuilder::applyDefaultHeaders(
    http::response<http::string_body> &response) {
  response.set(http::field::server, config_.serverName);
  response.set(http::field::content_type, getContentTypeString());

  if (config_.includeRequestId && !currentRequestId_.empty()) {
    response.set("x-request-id", currentRequestId_);
  }

  if (config_.includeTimestamp) {
    response.set("x-timestamp",
                 formatTimestamp(std::chrono::system_clock::now()));
  }
}

void ResponseBuilder::applyCorsHeaders(
    http::response<http::string_body> &response, const CorsConfig &cors) {
  response.set("access-control-allow-origin", cors.allowOrigin);
  response.set("access-control-allow-methods", cors.allowMethods);
  response.set("access-control-allow-headers", cors.allowHeaders);

  if (!cors.exposeHeaders.empty()) {
    response.set("access-control-expose-headers", cors.exposeHeaders);
  }

  if (cors.maxAge > 0) {
    response.set("access-control-max-age", std::to_string(cors.maxAge));
  }

  if (cors.allowCredentials) {
    response.set("access-control-allow-credentials", "true");
  }
}

void ResponseBuilder::applySecurityHeaders(
    http::response<http::string_body> &response) {
  // Security headers to prevent common attacks
  response.set("x-content-type-options", "nosniff");
  response.set("x-frame-options", "DENY");
  response.set("x-xss-protection", "1; mode=block");
  response.set("referrer-policy", "strict-origin-when-cross-origin");

  // Only add CSP for HTML responses
  if (currentContentType_ == ContentType::HTML) {
    response.set("content-security-policy", "default-src 'self'");
  }
}

void ResponseBuilder::updateStats(
    const http::response<http::string_body> &response) {
  stats_.totalResponses++;
  stats_.totalBytes += response.body().size();

  int statusCode = static_cast<int>(response.result());
  stats_.statusCodeCounts[statusCode]++;

  if (statusCode >= 200 && statusCode < 300) {
    stats_.successResponses++;
  } else {
    stats_.errorResponses++;
  }
}

std::string ResponseBuilder::createErrorJson(const std::string &message,
                                             const std::string &code) {
  std::ostringstream json;
  json << R"({"status":"error","error":")" << escapeJsonString(message)
       << R"(")";

  if (!code.empty()) {
    json << R"(,"code":")" << escapeJsonString(code) << R"(")";
  }

  if (config_.includeTimestamp) {
    json << R"(,"timestamp":")"
         << formatTimestamp(std::chrono::system_clock::now()) << R"(")";
  }

  if (config_.includeRequestId && !currentRequestId_.empty()) {
    json << R"(,"request_id":")" << currentRequestId_ << R"(")";
  }

  json << "}";
  return json.str();
}

std::string ResponseBuilder::createSuccessJson(const std::string &data,
                                               const std::string &message) {
  std::ostringstream json;
  json << R"({"status":"success")";

  if (!message.empty()) {
    json << R"(,"message":")" << escapeJsonString(message) << R"(")";
  }

  if (!data.empty()) {
    json << R"(,"data":)" << data;
  }

  if (config_.includeTimestamp) {
    json << R"(,"timestamp":")"
         << formatTimestamp(std::chrono::system_clock::now()) << R"(")";
  }

  if (config_.includeRequestId && !currentRequestId_.empty()) {
    json << R"(,"request_id":")" << currentRequestId_ << R"(")";
  }

  json << "}";
  return json.str();
}

std::string ResponseBuilder::createValidationErrorJson(
    const InputValidator::ValidationResult &result) {
  std::ostringstream json;
  json << R"({"status":"error","error":"Validation failed","validation":)"
       << result.toJsonString();

  if (config_.includeTimestamp) {
    json << R"(,"timestamp":")"
         << formatTimestamp(std::chrono::system_clock::now()) << R"(")";
  }

  if (config_.includeRequestId && !currentRequestId_.empty()) {
    json << R"(,"request_id":")" << currentRequestId_ << R"(")";
  }

  json << "}";
  return json.str();
}

std::string ResponseBuilder::createExceptionJson(const etl::ETLException &ex) {
  // Use the exception's built-in JSON representation
  std::string exceptionJson = ex.toJsonString();

  if (config_.includeTimestamp ||
      (config_.includeRequestId && !currentRequestId_.empty())) {
    // Parse and enhance the exception JSON
    std::ostringstream json;
    json << R"({"status":"error","exception":)" << exceptionJson;

    if (config_.includeTimestamp) {
      json << R"(,"timestamp":")"
           << formatTimestamp(std::chrono::system_clock::now()) << R"(")";
    }

    if (config_.includeRequestId && !currentRequestId_.empty()) {
      json << R"(,"request_id":")" << currentRequestId_ << R"(")";
    }

    json << "}";
    return json.str();
  }

  return exceptionJson;
}

std::string ResponseBuilder::getContentTypeString() const {
  auto it = currentHeaders_.find("content-type");
  if (it != currentHeaders_.end()) {
    return it->second;
  }

  return contentTypeToString(currentContentType_);
}

bool ResponseBuilder::isJsonContentType() const {
  return currentContentType_ == ContentType::JSON;
}

bool ResponseBuilder::isTextContentType() const {
  return currentContentType_ == ContentType::TEXT ||
         currentContentType_ == ContentType::HTML;
}

std::string ResponseBuilder::sanitizeHeaderValue(const std::string &value) {
  std::string sanitized = value;

  // Remove control characters except tab, LF, and CR
  sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(),
                                 [](char c) {
                                   return c < 32 && c != 9 && c != 10 &&
                                          c != 13;
                                 }),
                  sanitized.end());

  // Truncate if too long
  if (sanitized.length() > 8192) {
    sanitized = sanitized.substr(0, 8189) + "...";
  }

  return sanitized;
}

bool ResponseBuilder::isValidHeaderName(const std::string &name) {
  if (name.empty())
    return false;

  // HTTP header names should contain only ASCII letters, digits, and hyphens
  return std::all_of(name.begin(), name.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-';
  });
}

void ResponseBuilder::resetState() {
  currentStatus_ = http::status::ok;
  currentContentType_ = config_.defaultContentType;
  currentHeaders_.clear();
  currentKeepAlive_ = false;
  currentRequestId_.clear();
  currentMetadata_ = ResponseMetadata{};
}

void ResponseBuilder::updateConfig(const ResponseConfig &newConfig) {
  config_.serverName = newConfig.serverName;
  config_.enableCors = newConfig.enableCors;
  config_.corsConfig = newConfig.corsConfig;
  config_.defaultContentType = newConfig.defaultContentType;
  config_.prettyPrintJson = newConfig.prettyPrintJson;
  config_.includeTimestamp = newConfig.includeTimestamp;
  config_.includeRequestId = newConfig.includeRequestId;
  etl::ComponentLogger<ResponseBuilder>::info(
      "ResponseBuilder configuration updated");
}

void ResponseBuilder::resetStats() {
  stats_ = ResponseStats{};
  etl::ComponentLogger<ResponseBuilder>::info(
      "ResponseBuilder statistics reset");
}

// Static utility methods
std::string ResponseBuilder::contentTypeToString(ContentType type) {
  switch (type) {
  case ContentType::JSON:
    return "application/json";
  case ContentType::XML:
    return "application/xml";
  case ContentType::HTML:
    return "text/html; charset=utf-8";
  case ContentType::TEXT:
    return "text/plain; charset=utf-8";
  case ContentType::BINARY:
    return "application/octet-stream";
  default:
    return "application/json";
  }
}

ResponseBuilder::ContentType
ResponseBuilder::stringToContentType(const std::string &mimeType) {
  std::string lower = mimeType;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower.find("application/json") != std::string::npos) {
    return ContentType::JSON;
  } else if (lower.find("application/xml") != std::string::npos ||
             lower.find("text/xml") != std::string::npos) {
    return ContentType::XML;
  } else if (lower.find("text/html") != std::string::npos) {
    return ContentType::HTML;
  } else if (lower.find("text/") != std::string::npos) {
    return ContentType::TEXT;
  } else {
    return ContentType::BINARY;
  }
}

std::string ResponseBuilder::statusToReasonPhrase(http::status status) {
  switch (status) {
  case http::status::ok:
    return "OK";
  case http::status::created:
    return "Created";
  case http::status::accepted:
    return "Accepted";
  case http::status::no_content:
    return "No Content";
  case http::status::bad_request:
    return "Bad Request";
  case http::status::unauthorized:
    return "Unauthorized";
  case http::status::forbidden:
    return "Forbidden";
  case http::status::not_found:
    return "Not Found";
  case http::status::method_not_allowed:
    return "Method Not Allowed";
  case http::status::conflict:
    return "Conflict";
  case http::status::too_many_requests:
    return "Too Many Requests";
  case http::status::internal_server_error:
    return "Internal Server Error";
  case http::status::service_unavailable:
    return "Service Unavailable";
  default:
    return "Unknown";
  }
}

std::string ResponseBuilder::escapeJsonString(const std::string &input) {
  std::ostringstream escaped;

  for (char c : input) {
    switch (c) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\b':
      escaped << "\\b";
      break;
    case '\f':
      escaped << "\\f";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      if (c < 32) {
        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<int>(c);
      } else {
        escaped << c;
      }
      break;
    }
  }

  return escaped.str();
}

std::string ResponseBuilder::formatTimestamp(
    const std::chrono::system_clock::time_point &time) {
  auto time_t = std::chrono::system_clock::to_time_t(time);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                time.time_since_epoch()) %
            1000;

  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
  oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';

  return oss.str();
}

std::string ResponseBuilder::generateRequestId() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);

  std::ostringstream oss;
  for (int i = 0; i < 32; ++i) {
    if (i == 8 || i == 12 || i == 16 || i == 20) {
      oss << '-';
    }
    oss << std::hex << dis(gen);
  }

  return oss.str();
}