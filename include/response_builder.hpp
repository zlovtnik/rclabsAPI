#pragma once

#include "etl_exceptions.hpp"
#include "input_validator.hpp"
#include <boost/beast/http.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace http = boost::beast::http;

/**
 * @brief Comprehensive HTTP response builder component
 *
 * This class extracts and centralizes all response building logic from
 * RequestHandler, providing a clean fluent interface for response construction,
 * content negotiation, serialization support, and standardized error handling.
 */
class ResponseBuilder {
public:
  /**
   * @brief Content types supported by the response builder
   */
  enum class ContentType { JSON, XML, HTML, TEXT, BINARY };

  /**
   * @brief CORS configuration
   */
  struct CorsConfig {
    std::string allowOrigin = "*";
    std::string allowMethods = "GET, POST, PUT, DELETE, OPTIONS";
    std::string allowHeaders = "Content-Type, Authorization, X-Requested-With";
    std::string exposeHeaders = "";
    int maxAge = 86400; // 24 hours
    bool allowCredentials = false;

    CorsConfig() = default;
  };

  /**
   * @brief Response configuration
   */
  struct ResponseConfig {
    std::string serverName = "ETL Plus Backend";
    bool enableCors = true;
    CorsConfig corsConfig;
    ContentType defaultContentType = ContentType::JSON;
    bool prettyPrintJson = false;
    bool includeTimestamp = true;
    bool includeRequestId = true;

    ResponseConfig() = default;
  };

  /**
   * @brief Response metadata for tracking and debugging
   */
  struct ResponseMetadata {
    std::string requestId;
    std::chrono::system_clock::time_point timestamp;
    std::string endpoint;
    std::string method;
    size_t responseSize = 0;
    std::chrono::milliseconds processingTime{0};

    ResponseMetadata() : timestamp(std::chrono::system_clock::now()) {}
  };

public:
  explicit ResponseBuilder(ResponseConfig config);

  // Fluent interface methods for building responses
  ResponseBuilder &setStatus(http::status status);
  ResponseBuilder &setContentType(ContentType type);
  ResponseBuilder &setContentType(const std::string &mimeType);
  ResponseBuilder &setHeader(const std::string &name, const std::string &value);
  ResponseBuilder &
  setHeaders(const std::unordered_map<std::string, std::string> &headers);
  ResponseBuilder &setCors(const CorsConfig &config);
  ResponseBuilder &enableCors(bool enable = true);
  ResponseBuilder &setKeepAlive(bool keepAlive);
  ResponseBuilder &setRequestId(const std::string &requestId);
  ResponseBuilder &setMetadata(const ResponseMetadata &metadata);

  // Success response methods
  http::response<http::string_body> success(const std::string &data);
  http::response<http::string_body> success(const std::string &data,
                                            ContentType type);
  http::response<http::string_body> successJson(const std::string &jsonData);
  http::response<http::string_body>
  successWithMessage(const std::string &message, const std::string &data = "");

  // Error response methods
  http::response<http::string_body> error(http::status status,
                                          const std::string &message);
  http::response<http::string_body> badRequest(const std::string &message);
  http::response<http::string_body>
  unauthorized(const std::string &message = "Unauthorized");
  http::response<http::string_body>
  forbidden(const std::string &message = "Forbidden");
  http::response<http::string_body>
  notFound(const std::string &resource = "Resource");
  http::response<http::string_body>
  methodNotAllowed(const std::string &method, const std::string &endpoint);
  http::response<http::string_body> conflict(const std::string &message);
  http::response<http::string_body>
  tooManyRequests(const std::string &message = "Rate limit exceeded");
  http::response<http::string_body>
  internalServerError(const std::string &message = "Internal server error");
  http::response<http::string_body>
  serviceUnavailable(const std::string &message = "Service unavailable");

  // Exception-based response methods
  http::response<http::string_body> fromException(const etl::ETLException &ex);
  http::response<http::string_body>
  fromValidationResult(const InputValidator::ValidationResult &result);
  http::response<http::string_body>
  fromStandardException(const std::exception &ex,
                        const std::string &context = "");

  // Specialized response methods
  http::response<http::string_body>
  validationError(const InputValidator::ValidationResult &result);
  http::response<http::string_body>
  authenticationRequired(const std::string &realm = "ETL Plus API");
  http::response<http::string_body> corsPreflightResponse();
  http::response<http::string_body>
  healthCheck(bool healthy, const std::string &details = "");

  // Content negotiation and serialization
  http::response<http::string_body>
  negotiate(const std::string &acceptHeader,
            const std::unordered_map<ContentType, std::string> &content);

  // Streaming and chunked responses
  http::response<http::string_body>
  streamingResponse(const std::string &contentType = "text/plain");
  http::response<http::string_body>
  chunkedResponse(const std::vector<std::string> &chunks,
                  ContentType type = ContentType::JSON);

  // File and binary responses
  http::response<http::string_body>
  fileResponse(const std::string &filePath, const std::string &filename = "");
  http::response<http::string_body>
  binaryResponse(const std::vector<uint8_t> &data, const std::string &mimeType);

  // Redirect responses
  http::response<http::string_body>
  redirect(const std::string &location,
           http::status status = http::status::found);
  http::response<http::string_body>
  permanentRedirect(const std::string &location);
  http::response<http::string_body>
  temporaryRedirect(const std::string &location);

  // Caching responses
  http::response<http::string_body>
  cached(const std::string &data,
         std::chrono::seconds maxAge = std::chrono::seconds(3600));
  http::response<http::string_body> noCache(const std::string &data);

  // Configuration management
  void updateConfig(const ResponseConfig &newConfig);
  const ResponseConfig &getConfig() const { return config_; }

  // Statistics and monitoring
  struct ResponseStats {
    size_t totalResponses = 0;
    size_t successResponses = 0;
    size_t errorResponses = 0;
    size_t totalBytes = 0;
    std::unordered_map<int, size_t> statusCodeCounts;
    std::chrono::system_clock::time_point lastReset;

    ResponseStats() : lastReset(std::chrono::system_clock::now()) {}
  };

  ResponseStats getStats() const { return stats_; }
  void resetStats();

  // Utility methods
  static std::string contentTypeToString(ContentType type);
  static ContentType stringToContentType(const std::string &mimeType);
  static std::string statusToReasonPhrase(http::status status);
  static std::string escapeJsonString(const std::string &input);
  static std::string
  formatTimestamp(const std::chrono::system_clock::time_point &time);
  static std::string generateRequestId();

private:
  ResponseConfig config_;
  mutable ResponseStats stats_;

  // Current response state (for fluent interface)
  http::status currentStatus_ = http::status::ok;
  ContentType currentContentType_;
  std::unordered_map<std::string, std::string> currentHeaders_;
  bool currentKeepAlive_ = false;
  std::string currentRequestId_;
  ResponseMetadata currentMetadata_;

  // Helper methods
  http::response<http::string_body> buildResponse(const std::string &body);
  void applyDefaultHeaders(http::response<http::string_body> &response);
  void applyCorsHeaders(http::response<http::string_body> &response,
                        const CorsConfig &cors);
  void applySecurityHeaders(http::response<http::string_body> &response);
  void updateStats(const http::response<http::string_body> &response);

  // JSON formatting helpers
  std::string createErrorJson(const std::string &message,
                              const std::string &code = "ERROR");
  std::string createSuccessJson(const std::string &data,
                                const std::string &message = "");
  std::string
  createValidationErrorJson(const InputValidator::ValidationResult &result);
  std::string createExceptionJson(const etl::ETLException &ex);

  // Content type helpers
  std::string getContentTypeString() const;
  bool isJsonContentType() const;
  bool isTextContentType() const;

  // Security helpers
  std::string sanitizeHeaderValue(const std::string &value);
  bool isValidHeaderName(const std::string &name);

  // Reset fluent state
  void resetState();
};