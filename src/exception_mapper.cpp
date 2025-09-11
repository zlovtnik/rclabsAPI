#include "exception_mapper.hpp"
#include "hana_exception_handling.hpp"
#include "string_utils.hpp"
#include <boost/beast/http.hpp>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <thread>

namespace ETLPlus {
namespace ExceptionHandling {

// Thread-local storage for correlation ID
thread_local std::string ExceptionMapper::currentCorrelationId_;

// ErrorResponseFormat implementation
std::string ErrorResponseFormat::toJson() const {
  std::ostringstream json;
  json << "{";
  json << "\"status\":\""
       << ETLPlus::ExceptionHandling::escapeJsonString(status) << "\",";
  json << "\"message\":\""
       << ETLPlus::ExceptionHandling::escapeJsonString(message) << "\",";
  json << "\"code\":\"" << ETLPlus::ExceptionHandling::escapeJsonString(code)
       << "\",";
  json << "\"correlationId\":\""
       << ETLPlus::ExceptionHandling::escapeJsonString(correlationId) << "\",";
  json << "\"timestamp\":\""
       << ETLPlus::ExceptionHandling::escapeJsonString(timestamp) << "\"";

  if (!context.empty()) {
    json << ",\"context\":{";
    bool first = true;
    for (const auto &[key, value] : context) {
      if (!first)
        json << ",";
      json << "\"" << ETLPlus::ExceptionHandling::escapeJsonString(key)
           << "\":\"" << ETLPlus::ExceptionHandling::escapeJsonString(value)
           << "\"";
      first = false;
    }
    json << "}";
  }

  if (!details.empty()) {
    json << ",\"details\":\""
         << ETLPlus::ExceptionHandling::escapeJsonString(details) << "\"";
  }

  json << "}";
  return json.str();
}

// ExceptionMapper implementation
ExceptionMapper::ExceptionMapper(const ExceptionMappingConfig &config)
    : config_(config) {
  initializeDefaultMappings();
}

HttpResponse
ExceptionMapper::mapToResponse(const etl::ETLException &exception,
                               const std::string &operationName) const {
  // Log the exception with correlation ID tracking
  logException(exception, operationName);

  // Try custom handler first
  if (auto customResponse = tryCustomHandler(exception, operationName)) {
    return *customResponse;
  }

  // Map error code to HTTP status
  auto status = mapErrorCodeToStatus(exception.getCode());

  // Create error response format
  auto errorFormat = createErrorFormat(exception);

  // Convert to JSON
  std::string jsonBody = errorFormat.toJson();

  // Create HTTP response
  return createHttpResponse(status, jsonBody);
}

HttpResponse
ExceptionMapper::mapToResponse(const std::exception &exception,
                               const std::string &operationName) const {
  // Convert standard exception to ETLException
  auto etlException = std::make_shared<etl::SystemException>(
      etl::ErrorCode::INTERNAL_ERROR,
      "Standard exception: " + std::string(exception.what()), "ExceptionMapper",
      etl::ErrorContext{{"original_type", typeid(exception).name()}});

  return mapToResponse(*etlException, operationName);
}

HttpResponse
ExceptionMapper::mapToResponse(const std::string &operationName) const {
  // Create unknown exception
  auto unknownException = std::make_shared<etl::SystemException>(
      etl::ErrorCode::INTERNAL_ERROR, "Unknown exception occurred",
      "ExceptionMapper", etl::ErrorContext{{"operation", operationName}});

  return mapToResponse(*unknownException, operationName);
}

void ExceptionMapper::registerHandler(etl::ErrorCode code,
                                      ExceptionHandlerFunc handler) {
  codeHandlers_[code] = std::move(handler);
}

void ExceptionMapper::updateConfig(const ExceptionMappingConfig &config) {
  config_ = config;
}

ErrorResponseFormat
ExceptionMapper::createErrorFormat(const etl::ETLException &exception) const {
  ErrorResponseFormat format;

  format.message = exception.getMessage();
  format.code = etl::errorCodeToString(exception.getCode());
  format.correlationId = exception.getCorrelationId();

  // Format timestamp using thread-safe gmtime
  auto time_t = std::chrono::system_clock::to_time_t(exception.getTimestamp());
  std::tm tm_buf;
  std::tm *tm_ptr;
  std::ostringstream timestampStream;

#ifdef _WIN32
  gmtime_s(&tm_buf, &time_t);
  tm_ptr = &tm_buf;
#else
  tm_ptr = gmtime_r(&time_t, &tm_buf);
#endif

  if (tm_ptr) {
    timestampStream << std::put_time(tm_ptr, "%Y-%m-%dT%H:%M:%SZ");
  } else {
    timestampStream << "1970-01-01T00:00:00Z"; // Fallback
  }
  format.timestamp = timestampStream.str();

  // Copy context
  format.context = exception.getContext();

  // Add details if configured to include them
  if (config_.includeInternalDetails) {
    format.details = exception.toLogString();
  }

  return format;
}

void ExceptionMapper::logException(const etl::ETLException &exception,
                                   const std::string &operationName) const {
  std::string logMessage = "Exception in operation '" + operationName +
                           "': " + exception.toLogString();

  // Use appropriate log level based on error code
  switch (exception.getCode()) {
  case etl::ErrorCode::INVALID_INPUT:
  case etl::ErrorCode::MISSING_FIELD:
  case etl::ErrorCode::INVALID_RANGE:
    LOG_WARN("ExceptionMapper", logMessage);
    break;

  case etl::ErrorCode::UNAUTHORIZED:
  case etl::ErrorCode::TOKEN_EXPIRED:
  case etl::ErrorCode::FORBIDDEN:
  case etl::ErrorCode::ACCESS_DENIED:
    LOG_WARN("ExceptionMapper", logMessage);
    break;

  case etl::ErrorCode::JOB_NOT_FOUND:
    LOG_INFO("ExceptionMapper", logMessage);
    break;

  case etl::ErrorCode::RATE_LIMIT_EXCEEDED:
    LOG_WARN("ExceptionMapper", logMessage);
    break;

  case etl::ErrorCode::COMPONENT_UNAVAILABLE:
  case etl::ErrorCode::MEMORY_ERROR:
  case etl::ErrorCode::THREAD_POOL_EXHAUSTED:
  case etl::ErrorCode::DATABASE_ERROR:
  case etl::ErrorCode::NETWORK_ERROR:
    LOG_ERROR("ExceptionMapper", logMessage);
    break;

  default:
    LOG_ERROR("ExceptionMapper", logMessage);
    break;
  }
}

std::string ExceptionMapper::generateCorrelationId() {
  thread_local std::random_device rd;
  thread_local std::mt19937 gen(rd());
  thread_local std::uniform_int_distribution<> dis(0, 15);

  std::ostringstream oss;
  oss << std::hex;
  for (int i = 0; i < 8; ++i) {
    oss << dis(gen);
  }
  oss << "-";
  for (int i = 0; i < 4; ++i) {
    oss << dis(gen);
  }
  oss << "-4"; // Version 4 UUID
  for (int i = 0; i < 3; ++i) {
    oss << dis(gen);
  }
  oss << "-";
  oss << (dis(gen) & 0x3 | 0x8); // Variant bits
  for (int i = 0; i < 3; ++i) {
    oss << dis(gen);
  }
  oss << "-";
  for (int i = 0; i < 12; ++i) {
    oss << dis(gen);
  }

  return oss.str();
}

void ExceptionMapper::setCurrentCorrelationId(
    const std::string &correlationId) {
  currentCorrelationId_ = correlationId;
}

std::string ExceptionMapper::getCurrentCorrelationId() {
  return currentCorrelationId_;
}

boost::beast::http::status
ExceptionMapper::mapErrorCodeToStatus(etl::ErrorCode code) const {
  switch (code) {
  case etl::ErrorCode::INVALID_INPUT:
  case etl::ErrorCode::MISSING_FIELD:
  case etl::ErrorCode::INVALID_RANGE:
    return boost::beast::http::status::bad_request;

  case etl::ErrorCode::UNAUTHORIZED:
  case etl::ErrorCode::TOKEN_EXPIRED:
    return boost::beast::http::status::unauthorized;

  case etl::ErrorCode::FORBIDDEN:
  case etl::ErrorCode::ACCESS_DENIED:
    return boost::beast::http::status::forbidden;

  case etl::ErrorCode::JOB_NOT_FOUND:
    return boost::beast::http::status::not_found;

  case etl::ErrorCode::CONSTRAINT_VIOLATION:
  case etl::ErrorCode::JOB_ALREADY_RUNNING:
  case etl::ErrorCode::INVALID_JOB_STATE:
    return boost::beast::http::status::conflict;

  case etl::ErrorCode::RATE_LIMIT_EXCEEDED:
    return boost::beast::http::status::too_many_requests;

  case etl::ErrorCode::COMPONENT_UNAVAILABLE:
  case etl::ErrorCode::MEMORY_ERROR:
  case etl::ErrorCode::THREAD_POOL_EXHAUSTED:
  case etl::ErrorCode::DATABASE_ERROR:
  case etl::ErrorCode::NETWORK_ERROR:
    return boost::beast::http::status::service_unavailable;

  default:
    return config_.defaultStatus;
  }
}

HttpResponse
ExceptionMapper::createHttpResponse(boost::beast::http::status status,
                                    const std::string &body) const {
  HttpResponse response{status, 11};

  // Set standard headers
  response.set(boost::beast::http::field::server, config_.serverHeader);
  response.set(boost::beast::http::field::content_type, "application/json");
  response.set(boost::beast::http::field::access_control_allow_origin,
               config_.corsOrigin);
  response.set(boost::beast::http::field::access_control_expose_headers,
               "X-RateLimit-Limit, X-RateLimit-Remaining, X-RateLimit-Reset, "
               "Retry-After");
  response.keep_alive(config_.keepAlive);

  // Set body
  response.body() = body;
  response.prepare_payload();

  return response;
}

std::string ExceptionMapper::createJsonResponseBody(
    const etl::ETLException &exception) const {
  auto format = createErrorFormat(exception);
  return format.toJson();
}

bool ExceptionMapper::hasCustomHandler(etl::ErrorCode code) const {
  return codeHandlers_.find(code) != codeHandlers_.end();
}

std::optional<HttpResponse>
ExceptionMapper::tryCustomHandler(const etl::ETLException &exception,
                                  const std::string &operationName) const {
  // Try error code handler first
  auto codeIt = codeHandlers_.find(exception.getCode());
  if (codeIt != codeHandlers_.end()) {
    return codeIt->second(exception, operationName);
  }

  // Try type handler
  std::lock_guard<std::mutex> lock(typeHandlersMutex_);
  auto typeIt = typeHandlers_.find(std::type_index(typeid(exception)));
  if (typeIt != typeHandlers_.end()) {
    return typeIt->second(exception, operationName);
  }

  return std::nullopt;
}

void ExceptionMapper::initializeDefaultMappings() {
  // Default mappings are handled in mapErrorCodeToStatus()
  // Custom handlers can be registered via registerHandler()
}

// Factory function
std::unique_ptr<ExceptionMapper>
createExceptionMapper(const ExceptionMappingConfig &config) {
  return std::make_unique<ExceptionMapper>(config);
}

// Utility functions
HttpResponse
createValidationErrorResponse(const etl::ValidationException &exception,
                              const std::string &operationName) {
  return getGlobalExceptionMapper().mapToResponse(exception, operationName);
}

HttpResponse createSystemErrorResponse(const etl::SystemException &exception,
                                       const std::string &operationName) {
  return getGlobalExceptionMapper().mapToResponse(exception, operationName);
}

HttpResponse
createBusinessErrorResponse(const etl::BusinessException &exception,
                            const std::string &operationName) {
  return getGlobalExceptionMapper().mapToResponse(exception, operationName);
}

HttpResponse createRateLimitResponse(const std::string &message,
                                     const std::string &retryAfter) {
  auto &mapper = getGlobalExceptionMapper();
  auto exception = etl::SystemException(etl::ErrorCode::RATE_LIMIT_EXCEEDED,
                                        message, "RateLimiter");

  auto response = mapper.mapToResponse(exception);
  response.set(boost::beast::http::field::retry_after, retryAfter);
  return response;
}

HttpResponse createMaintenanceResponse(const std::string &message) {
  auto &mapper = getGlobalExceptionMapper();
  auto exception = etl::SystemException(etl::ErrorCode::COMPONENT_UNAVAILABLE,
                                        message, "MaintenanceMode");

  return mapper.mapToResponse(exception);
}

// Global exception mapper
ExceptionMapper &getGlobalExceptionMapper() {
  static ExceptionMapper instance;
  return instance;
}

} // namespace ExceptionHandling
} // namespace ETLPlus
