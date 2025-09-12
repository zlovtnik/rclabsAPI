#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Simplified ResponseBuilder Demo (without Boost Beast dependency)
 *
 * This demonstrates the core response building logic that would be used in the
 * actual ResponseBuilder component for HTTP server stability improvements.
 */

class SimpleResponseBuilder {
public:
  enum class ContentType { JSON, XML, HTML, TEXT };

  enum class Status {
    OK = 200,
    CREATED = 201,
    FOUND = 302,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    CONFLICT = 409,
    TOO_MANY_REQUESTS = 429,
    INTERNAL_SERVER_ERROR = 500,
    SERVICE_UNAVAILABLE = 503
  };

  struct Response {
    Status status;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    /**
     * @brief Construct a Response with the given HTTP status.
     *
     * Initializes a Response setting its status to the provided value. Headers
     * and body are left empty.
     *
     * @param s Initial response status.
     */
    Response(Status s) : status(s) {}
  };

  struct Config {
    std::string serverName = "ETL Plus Backend";
    bool enableCors = true;
    bool includeTimestamp = true;
    bool includeRequestId = false;
    ContentType defaultContentType = ContentType::JSON;
  };

private:
  Config config_;
  Status currentStatus_ = Status::OK;
  ContentType currentContentType_;
  std::unordered_map<std::string, std::string> currentHeaders_;
  std::string currentRequestId_;

public:
  /**
   * @brief Construct a SimpleResponseBuilder with default configuration.
   *
   * Initializes internal config_ to a default-constructed Config and sets
   * currentContentType_ from config_.defaultContentType.
   */
  SimpleResponseBuilder()
      : config_({}), currentContentType_(config_.defaultContentType) {}
  /**
   * @brief Constructs a SimpleResponseBuilder with a provided configuration.
   *
   * Initializes the builder by taking ownership of the given Config (moved)
   * and setting the builder's current content type to the config's
   * defaultContentType.
   *
   * @param config Configuration for the builder (moved into the instance).
   */
  explicit SimpleResponseBuilder(Config config)
      : config_(std::move(config)),
        currentContentType_(config_.defaultContentType) {}

  /**
   * @brief Set the HTTP response status for the next built response.
   *
   * Updates the builder's current status and returns a reference to the builder
   * to allow fluent chaining of calls.
   *
   * @param status Response status to use for the next Response.
   * @return SimpleResponseBuilder& Reference to this builder for method
   * chaining.
   */
  SimpleResponseBuilder &setStatus(Status status) {
    currentStatus_ = status;
    return *this;
  }

  /**
   * @brief Set the Content-Type to use for the next built response.
   *
   * Sets the builder's current content type which will be applied as the
   * "Content-Type" header when buildResponse() is called. Does not persist
   * beyond the next response (state is reset after building).
   *
   * @param type The ContentType to apply to the upcoming response.
   * @return Reference to this builder to allow method chaining.
   */
  SimpleResponseBuilder &setContentType(ContentType type) {
    currentContentType_ = type;
    return *this;
  }

  /**
   * @brief Add or update an HTTP header to be included in the next response.
   *
   * The header is stored in the builder's internal header map; if a header
   * with the same name already exists it will be overwritten.
   *
   * @param name Header name (e.g., "Content-Type" or "X-Custom-Header").
   * @param value Header value.
   * @return SimpleResponseBuilder& Reference to this builder to allow method
   * chaining.
   */
  SimpleResponseBuilder &setHeader(const std::string &name,
                                   const std::string &value) {
    currentHeaders_[name] = value;
    return *this;
  }

  /**
   * @brief Set the request identifier to include with the next response.
   *
   * Stores the given requestId in the builder; if the config requests inclusion
   * of request IDs the value will be added to the next response's body (or
   * headers) according to the builder's response helpers.
   *
   * @param requestId Identifier to associate with the next response.
   * @return SimpleResponseBuilder& Reference to this builder for method
   * chaining.
   */
  SimpleResponseBuilder &setRequestId(const std::string &requestId) {
    currentRequestId_ = requestId;
    return *this;
  }

  /**
   * @brief Build a successful (200 OK) response with the given body.
   *
   * Sets the builder's status to OK and constructs a Response whose body is the
   * provided string using the builder's current headers and content-type.
   *
   * @param data Response body to include.
   * @return Response Constructed response with status OK and the provided body.
   */
  Response success(const std::string &data) {
    currentStatus_ = Status::OK;
    return buildResponse(data);
  }

  /**
   * @brief Build a successful (200 OK) response with a JSON body.
   *
   * Marks the response as HTTP 200 OK, sets the Content-Type to
   * `application/json`, and constructs a Response containing the provided
   * serialized JSON payload.
   *
   * @param jsonData A pre-serialized JSON string to use as the response body.
   * @return Response The constructed response with status, headers and body
   * set.
   */
  Response successJson(const std::string &jsonData) {
    currentStatus_ = Status::OK;
    currentContentType_ = ContentType::JSON;
    return buildResponse(jsonData);
  }

  /**
   * @brief Build a 200 OK JSON response that indicates success with a message.
   *
   * Constructs a JSON object with at least the fields `status":"success"` and
   * `message`. Optionally includes a `data` field (if non-empty), and—depending
   * on configuration—`timestamp` and `request_id`.
   *
   * The builder's current status is set to OK and the content type to JSON.
   *
   * @param message Human-readable success message; content is JSON-escaped.
   * @param data Optional JSON fragment to include as the value of the `data`
   *             field. This string is inserted verbatim into the output body,
   *             so it must already be valid JSON (e.g., an object or array).
   * @return Response A Response with status 200, appropriate headers, and the
   *         constructed JSON body.
   */
  Response successWithMessage(const std::string &message,
                              const std::string &data = "") {
    currentStatus_ = Status::OK;
    currentContentType_ = ContentType::JSON;

    std::ostringstream json;
    json << R"({"status":"success","message":")" << escapeJson(message)
         << R"(")";

    if (!data.empty()) {
      json << R"(,"data":)" << data;
    }

    if (config_.includeTimestamp) {
      json << R"(,"timestamp":")" << getCurrentTimestamp() << R"(")";
    }

    if (config_.includeRequestId && !currentRequestId_.empty()) {
      json << R"(,"request_id":")" << currentRequestId_ << R"(")";
    }

    json << "}";

    return buildResponse(json.str());
  }

  /**
   * @brief Build an error HTTP response with a JSON body.
   *
   * Constructs a JSON error payload and returns a Response with the builder's
   * current status, headers and content type set to application/json. The
   * produced JSON includes:
   * - "status": "error"
   * - "error": the provided message (JSON-escaped)
   * - "code": numeric HTTP status code
   * Optionally includes "timestamp" and "request_id" when enabled in the
   * builder configuration.
   *
   * This call updates the builder's current status and content type, and then
   * delegates to the core response construction which applies default and
   * custom headers. The builder's mutable state is reset after the response is
   * built.
   *
   * @param status HTTP status to use for the response (e.g.,
   * Status::BAD_REQUEST).
   * @param message Human-readable error message; will be JSON-escaped in the
   * body.
   * @return Response A Response whose body is the JSON error object and whose
   *         headers/content-type are set according to the builder
   * configuration.
   */
  Response error(Status status, const std::string &message) {
    currentStatus_ = status;
    currentContentType_ = ContentType::JSON;

    std::ostringstream json;
    json << R"({"status":"error","error":")" << escapeJson(message) << R"(")";
    json << R"(,"code":)" << static_cast<int>(status);

    if (config_.includeTimestamp) {
      json << R"(,"timestamp":")" << getCurrentTimestamp() << R"(")";
    }

    if (config_.includeRequestId && !currentRequestId_.empty()) {
      json << R"(,"request_id":")" << currentRequestId_ << R"(")";
    }

    json << "}";

    return buildResponse(json.str());
  }

  /**
   * @brief Build a 400 Bad Request JSON error response.
   *
   * Constructs an error response with HTTP status 400 (Bad Request). The
   * returned Response has Content-Type application/json and a body containing
   * an error object (fields: "status":"error", "error":<escaped message>,
   * "code":400). Timestamp and request ID are included in the body when enabled
   * in the builder configuration.
   *
   * @param message Human-readable error message (will be JSON-escaped).
   * @return Response Prepared error response with status BAD_REQUEST.
   */
  Response badRequest(const std::string &message) {
    return error(Status::BAD_REQUEST, message);
  }

  /**
   * @brief Build a 401 Unauthorized error response.
   *
   * Constructs a JSON-formatted error Response with HTTP-like status
   * UNAUTHORIZED (401). The response body contains an error object with the
   * provided message and the numeric status code; standard headers (Server,
   * Content-Type, security headers, etc.) are applied by the builder.
   *
   * @param message Human-readable error message to include in the response
   * body. Defaults to "Unauthorized".
   * @return Response A Response with status UNAUTHORIZED and a JSON error
   * payload.
   */
  Response unauthorized(const std::string &message = "Unauthorized") {
    return error(Status::UNAUTHORIZED, message);
  }

  /**
   * @brief Create a 403 Forbidden error response.
   *
   * Constructs a Response with status set to Forbidden (403) and a JSON error
   * body containing the provided message.
   *
   * @param message Human-readable error message to include in the response body
   * (default: "Forbidden").
   * @return Response Error response with status = Status::FORBIDDEN and a
   * JSON-formatted body.
   */
  Response forbidden(const std::string &message = "Forbidden") {
    return error(Status::FORBIDDEN, message);
  }

  /**
   * @brief Build a 404 Not Found JSON error response for a missing resource.
   *
   * Constructs an error response with HTTP status NOT_FOUND and a JSON body
   * containing an error message of the form "`<resource> not found`".
   *
   * @param resource Name of the missing resource; used verbatim in the error
   * message. Defaults to "Resource".
   * @return Response Response object with status NOT_FOUND, appropriate headers
   *         (including Content-Type set to application/json) and the error
   * body.
   */
  Response notFound(const std::string &resource = "Resource") {
    return error(Status::NOT_FOUND, resource + " not found");
  }

  /**
   * @brief Build a 405 Method Not Allowed response and include the standard
   * Allow header.
   *
   * Sets the "Allow" header to "GET, POST, PUT, DELETE, OPTIONS" and returns a
   * response with HTTP status 405 and a JSON-formatted error message indicating
   * that the given HTTP method is not allowed for the specified endpoint.
   *
   * @param method The HTTP method that was attempted (e.g., "DELETE").
   * @param endpoint The request path or endpoint for which the method is not
   * allowed (e.g., "/api/users").
   * @return Response A Response object with status Method Not Allowed (405),
   * the Allow header set, and an error body.
   */
  Response methodNotAllowed(const std::string &method,
                            const std::string &endpoint) {
    setHeader("Allow", "GET, POST, PUT, OPTIONS");
    return error(Status::METHOD_NOT_ALLOWED,
                 "Method " + method + " not allowed for " + endpoint);
  }

  /**
   * @brief Build a 429 Too Many Requests response.
   *
   * Constructs an error response with status TOO_MANY_REQUESTS and sets a
   * "Retry-After" header advising a 60-second wait.
   *
   * @param message Human-readable error message included in the JSON error
   * body. Defaults to "Rate limit exceeded".
   * @return Response Response object with status 429, the JSON error body, and
   * the "Retry-After: 60" header.
   */
  Response tooManyRequests(const std::string &message = "Rate limit exceeded") {
    setHeader("Retry-After", "60");
    return error(Status::TOO_MANY_REQUESTS, message);
  }

  /**
   * @brief Create a 500 Internal Server Error JSON response.
   *
   * Builds an error response with HTTP status 500 and a JSON body containing
   * an error message and numeric code. The provided message is escaped for JSON
   * and inserted into the response body; if omitted, a generic "Internal server
   * error" message is used.
   *
   * @param message Human-readable error message to include in the response
   * body.
   * @return Response Response with status INTERNAL_SERVER_ERROR and a JSON
   * error body.
   */
  Response
  internalServerError(const std::string &message = "Internal server error") {
    return error(Status::INTERNAL_SERVER_ERROR, message);
  }

  /**
   * @brief Construct a 400 Bad Request response describing validation failures.
   *
   * @details Produces a JSON body of the form:
   * {"status":"error","error":"Validation
   * failed","validation":{"errors":[...]}}, where each string from @p errors is
   * JSON-escaped and placed in the errors array.
   *
   * @param errors Vector of human-readable validation messages to include in
   * the response.
   * @return Response Response with status BAD_REQUEST and Content-Type set to
   * application/json; other default headers are applied by the builder.
   */
  Response validationError(const std::vector<std::string> &errors) {
    currentStatus_ = Status::BAD_REQUEST;
    currentContentType_ = ContentType::JSON;

    std::ostringstream json;
    json
        << R"({"status":"error","error":"Validation failed","validation":{"errors":[)";

    for (size_t i = 0; i < errors.size(); ++i) {
      if (i > 0)
        json << ",";
      json << R"(")" << escapeJson(errors[i]) << R"(")";
    }

    json << "]}}";

    return buildResponse(json.str());
  }

  /**
   * @brief Build a JSON health-check response.
   *
   * Constructs a JSON body with a "status" field ("healthy" or "unhealthy"),
   * always includes a "timestamp" (current UTC ISO 8601), and includes a
   * "details" field when a non-empty detail string is supplied. Sets the HTTP
   * status to 200 OK when healthy is true, otherwise 503 Service Unavailable.
   * The response Content-Type is set to application/json.
   *
   * @param healthy true to produce a healthy (200) response; false yields an
   * unhealthy (503) response.
   * @param details Optional human-readable details included in the JSON
   * `details` field (escaped for JSON).
   * @return Response The constructed response with status, headers, and JSON
   * body.
   */
  Response healthCheck(bool healthy, const std::string &details = "") {
    currentStatus_ = healthy ? Status::OK : Status::SERVICE_UNAVAILABLE;
    currentContentType_ = ContentType::JSON;

    std::ostringstream json;
    json << R"({"status":")" << (healthy ? "healthy" : "unhealthy") << R"(")";

    if (!details.empty()) {
      json << R"(,"details":")" << escapeJson(details) << R"(")";
    }

    json << R"(,"timestamp":")" << getCurrentTimestamp() << R"(")";
    json << "}";

    return buildResponse(json.str());
  }

  /**
   * @brief Build a redirect response with a Location header.
   *
   * Constructs a Response that redirects the client to the given location.
   *
   * @param location Target URL or path for the Location header.
   * @param status HTTP status to use for the redirect (defaults to
   * Status::FOUND).
   * @return Response Response with the configured status, a `Location` header
   * set to `location`, and an empty body.
   */
  Response redirect(const std::string &location,
                    Status status = Status::FOUND) {
    currentStatus_ = status;
    setHeader("Location", location);
    return buildResponse("");
  }

private:
  /**
   * @brief Build a Response object from the builder's current state and body.
   *
   * Constructs a Response with the builder's current status and the provided
   * body, applies headers (Server and Content-Type), merges any previously set
   * custom headers, and adds CORS headers (when enabled) and security headers.
   * The builder's mutable state is reset before returning.
   *
   * The header application order is:
   * 1. Default headers ("Server", "Content-Type")
   * 2. Custom headers set via setHeader (these override defaults if names
   * match)
   * 3. CORS headers (if enabled) — these will overwrite any matching custom
   * headers
   * 4. Security headers (always applied) — these will overwrite any matching
   * headers
   *
   * @param body Raw response body to place in Response.body.
   * @return Response The constructed response with status, headers, and body.
   */
  Response buildResponse(const std::string &body) {
    Response response(currentStatus_);

    // Set body
    response.body = body;

    // Apply default headers
    response.headers["Server"] = config_.serverName;
    response.headers["Content-Type"] = contentTypeToString(currentContentType_);

    // Apply custom headers
    for (const auto &[name, value] : currentHeaders_) {
      response.headers[name] = value;
    }

    // Apply CORS headers if enabled
    if (config_.enableCors) {
      response.headers["Access-Control-Allow-Origin"] = "*";
      response.headers["Access-Control-Allow-Methods"] =
          "GET, POST, PUT, DELETE, OPTIONS";
      response.headers["Access-Control-Allow-Headers"] =
          "Content-Type, Authorization";
    }

    // Apply security headers
    response.headers["X-Content-Type-Options"] = "nosniff";
    response.headers["X-Frame-Options"] = "DENY";
    response.headers["X-XSS-Protection"] = "1; mode=block";

    // Reset state for next response
    resetState();

    return response;
  }

  /**
   * @brief Converts a ContentType enum value to its corresponding MIME type
   * string.
   *
   * Maps ContentType::JSON, ::XML, ::HTML, and ::TEXT to their standard
   * Content-Type header values. If an unknown enum value is provided, the
   * function falls back to "application/json".
   *
   * @param type The ContentType value to convert.
   * @return std::string MIME type string suitable for an HTTP Content-Type
   * header.
   */
  std::string contentTypeToString(ContentType type) const {
    switch (type) {
    case ContentType::JSON:
      return "application/json";
    case ContentType::XML:
      return "application/xml";
    case ContentType::HTML:
      return "text/html; charset=utf-8";
    case ContentType::TEXT:
      return "text/plain; charset=utf-8";
    default:
      return "application/json";
    }
  }

  /**
   * @brief Escape special characters in a string so it is safe for embedding in
   * JSON string values.
   *
   * Replaces the characters `"`, `\`, newline, carriage return, and tab with
   * their JSON escape sequences (`\"`, `\\`, `\n`, `\r`, `\t`). Other
   * characters (including other control characters or non-ASCII Unicode) are
   * left unchanged.
   *
   * @param input The source string to escape.
   * @return std::string A new string with JSON-special characters escaped.
   */
  std::string escapeJson(const std::string &input) const {
    std::ostringstream escaped;
    for (char c : input) {
      switch (c) {
      case '"':
        escaped << "\\\"";
        break;
      case '\\':
        escaped << "\\\\";
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
        escaped << c;
        break;
      }
    }
    return escaped.str();
  }

  /**
   * @brief Returns the current UTC timestamp in ISO 8601 format.
   *
   * Produces a string in the form `YYYY-MM-DDTHH:MM:SSZ` representing the
   * current time in Coordinated Universal Time (UTC). The value is derived from
   * the system clock and formatted to second precision.
   *
   * @return std::string Current UTC timestamp as an ISO 8601 string.
   */
  std::string getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
  }

  /**
   * @brief Reset the builder's mutable state to its defaults.
   *
   * Restores the current status to OK, sets the current content type to the
   * builder's configured default, clears any custom headers, and removes the
   * current request ID. Intended for internal use to ensure each response is
   * built from a clean state.
   */
  void resetState() {
    currentStatus_ = Status::OK;
    currentContentType_ = config_.defaultContentType;
    currentHeaders_.clear();
    currentRequestId_.clear();
  }
};

/**
 * @brief Print a human-readable representation of a Response to stdout.
 *
 * Prints a formatted block containing the test name, a separator line, the
 * numeric HTTP-like status code, all response headers, and the response body
 * (or
 * "(empty)" when the body is empty). Intended for demo and debugging output.
 *
 * @param testName Short label identifying the test or scenario.
 * @param response Response object whose status, headers, and body will be
 * printed.
 *
 * Side effects: writes to std::cout.
 */
void printResponse(const std::string &testName,
                   const SimpleResponseBuilder::Response &response) {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test: " << testName << "\n";
  std::cout << std::string(60, '=') << "\n";

  std::cout << "Status: " << static_cast<int>(response.status) << "\n";

  std::cout << "Headers:\n";
  for (const auto &[name, value] : response.headers) {
    std::cout << "  " << name << ": " << value << "\n";
  }

  std::cout << "Body:\n";
  if (response.body.empty()) {
    std::cout << "  (empty)\n";
  } else {
    std::cout << "  " << response.body << "\n";
  }
}

/**
 * @brief Demo program that exercises the SimpleResponseBuilder API.
 *
 * This demonstration runs 15 scenarios covering common response construction
 * patterns (success, errors, validation failures, health checks, redirects,
 * content-type handling, CORS/security headers, timestamps, and request IDs)
 * and prints the resulting Response objects to stdout. Intended to validate and
 * showcase the builder's default headers, JSON escaping, fluent interface, and
 * stateless reset between responses.
 *
 * @return int Exit status code (returns 0 on successful completion).
 */
int main() {
  std::cout << "🚀 ResponseBuilder Demo (Simplified)\n";
  std::cout << "====================================\n";
  std::cout << "Demonstrating HTTP response building for server stability "
               "improvements\n";

  SimpleResponseBuilder::Config config;
  config.serverName = "ETL Plus Demo Server";
  config.includeTimestamp = true;
  config.includeRequestId = true;

  SimpleResponseBuilder builder(config);

  // Test 1: Success response
  {
    auto response = builder.success(R"({"message":"Hello World"})");
    printResponse("Basic Success Response", response);
  }

  // Test 2: Success with message
  {
    auto response = builder.successWithMessage("User created successfully",
                                               R"({"id":123,"name":"John"})");
    printResponse("Success with Message", response);
  }

  // Test 3: JSON success response
  {
    auto response = builder.successJson(
        R"({"users":[{"id":1,"name":"Alice"},{"id":2,"name":"Bob"}]})");
    printResponse("JSON Success Response", response);
  }

  // Test 4: Bad request error
  {
    auto response = builder.badRequest("Missing required field: username");
    printResponse("Bad Request Error", response);
  }

  // Test 5: Unauthorized error
  {
    auto response = builder.unauthorized("Invalid authentication token");
    printResponse("Unauthorized Error", response);
  }

  // Test 6: Not found error
  {
    auto response = builder.notFound("User");
    printResponse("Not Found Error", response);
  }

  // Test 7: Method not allowed
  {
    auto response = builder.methodNotAllowed("DELETE", "/api/users");
    printResponse("Method Not Allowed", response);
  }

  // Test 8: Rate limit exceeded
  {
    auto response = builder.tooManyRequests("Too many requests from this IP");
    printResponse("Rate Limit Exceeded", response);
  }

  // Test 9: Validation error
  {
    std::vector<std::string> errors = {
        "Username must be at least 3 characters", "Email format is invalid",
        "Password must contain at least one number"};
    auto response = builder.validationError(errors);
    printResponse("Validation Error", response);
  }

  // Test 10: Health check (healthy)
  {
    auto response = builder.healthCheck(true, "All systems operational");
    printResponse("Health Check - Healthy", response);
  }

  // Test 11: Health check (unhealthy)
  {
    auto response = builder.healthCheck(false, "Database connection failed");
    printResponse("Health Check - Unhealthy", response);
  }

  // Test 12: Redirect response
  {
    auto response = builder.redirect("https://api.example.com/v2/users");
    printResponse("Redirect Response", response);
  }

  // Test 13: Fluent interface usage
  {
    auto response =
        builder.setStatus(SimpleResponseBuilder::Status::CREATED)
            .setContentType(SimpleResponseBuilder::ContentType::JSON)
            .setHeader("X-Custom-Header", "custom-value")
            .setRequestId("req-12345")
            .success(R"({"id":456,"status":"created"})");
    printResponse("Fluent Interface Usage", response);
  }

  // Test 14: Custom content type
  {
    auto response =
        builder.setContentType(SimpleResponseBuilder::ContentType::XML)
            .success("<users><user id=\"1\">Alice</user></users>");
    printResponse("XML Content Type", response);
  }

  // Test 15: Internal server error
  {
    auto response = builder.internalServerError("Database connection timeout");
    printResponse("Internal Server Error", response);
  }

  std::cout << "\n🎉 ResponseBuilder Demo Complete!\n";
  std::cout << "\nKey Features Demonstrated:\n";
  std::cout << "  ✅ Fluent interface for response building\n";
  std::cout << "  ✅ Standardized success and error responses\n";
  std::cout << "  ✅ Automatic header management (CORS, security)\n";
  std::cout << "  ✅ Content type negotiation\n";
  std::cout << "  ✅ JSON formatting and escaping\n";
  std::cout << "  ✅ Timestamp and request ID inclusion\n";
  std::cout << "  ✅ HTTP status code mapping\n";
  std::cout << "  ✅ Validation error formatting\n";
  std::cout << "  ✅ Health check responses\n";
  std::cout << "  ✅ Redirect responses\n";

  std::cout << "\nThis response building logic will improve HTTP server "
               "stability by:\n";
  std::cout
      << "  • Ensuring consistent response formats across all endpoints\n";
  std::cout << "  • Automatically applying security and CORS headers\n";
  std::cout
      << "  • Providing proper HTTP status codes for different scenarios\n";
  std::cout << "  • Standardizing error response structures\n";
  std::cout
      << "  • Including debugging information (timestamps, request IDs)\n";
  std::cout << "  • Preventing JSON injection through proper escaping\n";

  return 0;
}