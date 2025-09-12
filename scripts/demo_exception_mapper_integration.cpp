#include "etl_exceptions.hpp"
#include "exception_mapper.hpp"
#include "logger.hpp"
#include "request_handler.hpp"
#include <boost/beast/http.hpp>
#include <iostream>

using namespace ETLPlus::ExceptionHandling;

// Example of how to integrate ExceptionMapper into RequestHandler
class RequestHandlerWithExceptionMapper {
private:
  ExceptionMapper exceptionMapper_;

public:
  /**
   * @brief Constructs a RequestHandlerWithExceptionMapper and configures its ExceptionMapper.
   *
   * Initializes the per-instance ExceptionMapper with production-safe defaults (no internal details exposed,
   * server header, permissive CORS origin, and disabled keep-alive) and applies the configuration.
   * After configuring, installs the class's custom exception handlers used to translate domain exceptions
   * into HTTP responses.
   */
  RequestHandlerWithExceptionMapper() {
    // Configure the exception mapper
    ExceptionMappingConfig config;
    config.includeInternalDetails =
        false; // Don't expose internal details in production
    config.serverHeader = "ETL Plus Backend v2.0";
    config.corsOrigin = "*";
    config.keepAlive = false;

    exceptionMapper_.updateConfig(config);

    // Register custom handlers for specific scenarios
    registerCustomHandlers();
  }

  /**
   * @brief Registers custom exception-to-HTTP handlers on the instance ExceptionMapper.
   *
   * @details Installs two handlers:
   *  - etl::ErrorCode::RATE_LIMIT_EXCEEDED: returns HTTP 429 with JSON body containing
   *    error, escaped exception message, retryAfter (60), and correlationId. Response
   *    includes headers: Retry-After: 60, X-Rate-Limit-Limit: 100, X-Rate-Limit-Remaining: 0,
   *    X-Rate-Limit-Reset: 60 and Content-Type: application/json.
   *  - etl::ErrorCode::COMPONENT_UNAVAILABLE: returns HTTP 503 with JSON body containing
   *    error, escaped exception message, maintenance: true, estimatedRecovery ("5 minutes"),
   *    and correlationId. Response includes header Retry-After: 300 and Content-Type: application/json.
   *
   * These handlers produce ready-to-send HttpResponse objects (body set and payload prepared)
   * using the exception's message and correlation ID (escaped for JSON).
   */
  void registerCustomHandlers() {
    // Custom handler for rate limiting
    exceptionMapper_.registerHandler(
        etl::ErrorCode::RATE_LIMIT_EXCEEDED,
        [](const etl::ETLException &ex, const std::string &operation) {
          HttpResponse response{boost::beast::http::status::too_many_requests,
                                11};
          response.set(boost::beast::http::field::content_type,
                       "application/json");
          response.set(boost::beast::http::field::retry_after, "60");
          response.set("X-Rate-Limit-Limit", "100");
          response.set("X-Rate-Limit-Remaining", "0");
          response.set("X-Rate-Limit-Reset", "60");

          std::string body =
              R"({
                    "error": "Rate limit exceeded",
                    "message": ")" +
              ETLPlus::ExceptionHandling::escapeJsonString(ex.getMessage()) +
              R"(",
                    "retryAfter": 60,
                    "correlationId": ")" +
              ETLPlus::ExceptionHandling::escapeJsonString(
                  ex.getCorrelationId()) +
              R"("
                })";

          response.body() = body;
          response.prepare_payload();
          return response;
        });

    // Custom handler for maintenance mode
    exceptionMapper_.registerHandler(
        etl::ErrorCode::COMPONENT_UNAVAILABLE,
        [](const etl::ETLException &ex, const std::string &operation) {
          HttpResponse response{boost::beast::http::status::service_unavailable,
                                11};
          response.set(boost::beast::http::field::content_type,
                       "application/json");
          response.set(boost::beast::http::field::retry_after,
                       "300"); // 5 minutes

          std::string body =
              R"({
                    "error": "Service temporarily unavailable",
                    "message": ")" +
              ETLPlus::ExceptionHandling::escapeJsonString(ex.getMessage()) +
              R"(",
                    "maintenance": true,
                    "estimatedRecovery": "5 minutes",
                    "correlationId": ")" +
              ETLPlus::ExceptionHandling::escapeJsonString(
                  ex.getCorrelationId()) +
              R"("
                })";

          response.body() = body;
          response.prepare_payload();
          return response;
        });
  }

  // Simplified request handling with ExceptionMapper
  template <class Body, class Allocator>
  /**
   * @brief Handle an HTTP request, ensuring a correlation ID is set and mapping any thrown exceptions to HTTP responses.
   *
   * This function generates and sets a per-request correlation ID, delegates request handling to processRequest,
   * and converts any thrown exceptions into an appropriate HTTP response using the instance's ExceptionMapper.
   *
   * @tparam Body     HTTP message body type of the incoming request.
   * @tparam Allocator Allocator type used for the request fields.
   * @param req       The incoming HTTP request to handle.
   * @return boost::beast::http::response<boost::beast::http::string_body> An HTTP response produced either by
   *         successful request processing or by mapping an exception to a response.
   */
  boost::beast::http::response<boost::beast::http::string_body> handleRequest(
      boost::beast::http::request<Body,
                                  boost::beast::http::basic_fields<Allocator>>
          req) {

    try {
      // Set correlation ID for this request
      std::string correlationId = ExceptionMapper::generateCorrelationId();
      ExceptionMapper::setCurrentCorrelationId(correlationId);

      // Process the request (simplified for demo)
      return processRequest(req);

    } catch (const etl::ETLException &ex) {
      // Use ExceptionMapper to handle ETL exceptions
      return exceptionMapper_.mapToResponse(ex, "handleRequest");

    } catch (const std::exception &ex) {
      // Use ExceptionMapper to handle standard exceptions
      return exceptionMapper_.mapToResponse(ex, "handleRequest");

    } catch (...) {
      // Use ExceptionMapper to handle unknown exceptions
      return exceptionMapper_.mapToResponse("handleRequest");
    }
  }

private:
  template <class Body, class Allocator>
  /**
   * @brief Process an HTTP request and simulate various success/error scenarios.
   *
   * This function inspects the request target and either returns a 200 OK JSON
   * response for normal paths or deliberately throws specific exceptions to
   * demonstrate the ExceptionMapper behavior for different failure modes.
   *
   * Recognized request targets and effects:
   * - "/test/validation"      : throws etl::ValidationException (INVALID_INPUT)
   * - "/test/rate-limit"      : throws etl::SystemException (RATE_LIMIT_EXCEEDED)
   * - "/test/maintenance"     : throws etl::SystemException (COMPONENT_UNAVAILABLE)
   * - "/test/not-found"       : throws etl::BusinessException (JOB_NOT_FOUND)
   * - "/test/database"        : throws etl::SystemException (DATABASE_ERROR) with ErrorContext
   * - "/test/standard"        : throws std::runtime_error
   * - any other target        : returns HTTP 200 with JSON body {"status":"success","message":"Request processed successfully"}
   *
   * @param req The incoming HTTP request whose target determines the simulated outcome.
   * @return boost::beast::http::response<boost::beast::http::string_body> HTTP response for successful (non-error) targets.
   *
   * @throws etl::ValidationException Thrown for validation error simulation.
   * @throws etl::SystemException     Thrown for system-level error simulations (rate limit, maintenance, database).
   * @throws etl::BusinessException   Thrown for business-logic error simulation (not found).
   * @throws std::runtime_error       Thrown for the standard error simulation.
   */
  boost::beast::http::response<boost::beast::http::string_body> processRequest(
      boost::beast::http::request<Body,
                                  boost::beast::http::basic_fields<Allocator>>
          req) {

    // Simulate different types of errors for demonstration
    std::string target = std::string(req.target());

    if (target == "/test/validation") {
      throw etl::ValidationException(etl::ErrorCode::INVALID_INPUT,
                                     "Invalid request format", "body",
                                     "malformed json");
    } else if (target == "/test/rate-limit") {
      throw etl::SystemException(etl::ErrorCode::RATE_LIMIT_EXCEEDED,
                                 "API rate limit exceeded", "RateLimiter");
    } else if (target == "/test/maintenance") {
      throw etl::SystemException(etl::ErrorCode::COMPONENT_UNAVAILABLE,
                                 "System maintenance in progress",
                                 "MaintenanceMode");
    } else if (target == "/test/not-found") {
      throw etl::BusinessException(etl::ErrorCode::JOB_NOT_FOUND,
                                   "Job with ID 12345 not found",
                                   "JobManager::getJob");
    } else if (target == "/test/database") {
      throw etl::SystemException(
          etl::ErrorCode::DATABASE_ERROR, "Database connection failed",
          "DatabaseManager",
          etl::ErrorContext{{"host", "localhost"}, {"port", "5432"}});
    } else if (target == "/test/standard") {
      throw std::runtime_error("Standard runtime error occurred");
    } else {
      // Success response
      boost::beast::http::response<boost::beast::http::string_body> response{
          boost::beast::http::status::ok, 11};
      response.set(boost::beast::http::field::content_type, "application/json");
      response.body() =
          R"({"status":"success","message":"Request processed successfully"})";
      response.prepare_payload();
      return response;
    }
  }
};

/**
 * @brief Runs a console demonstration of the ExceptionMapper integrated with a request handler.
 *
 * Performs several simulated HTTP GET requests against a RequestHandlerWithExceptionMapper
 * to illustrate how different exceptions are mapped to HTTP responses. For each test path
 * the function builds a request, invokes the handler, and prints the response status,
 * Content-Type, body, and (when applicable) rate-limit headers to standard output.
 *
 * This function is intended for interactive/demo use and has no return value.
 */
void demonstrateExceptionMapping() {
  std::cout << "=== ExceptionMapper Integration Demo ===" << std::endl;

  RequestHandlerWithExceptionMapper handler;

  // Test different error scenarios
  std::vector<std::string> testPaths = {"/test/validation",  "/test/rate-limit",
                                        "/test/maintenance", "/test/not-found",
                                        "/test/database",    "/test/standard",
                                        "/test/success"};

  for (const auto &path : testPaths) {
    std::cout << "\n--- Testing path: " << path << " ---" << std::endl;

    // Create a simple request
    boost::beast::http::request<boost::beast::http::string_body> req{
        boost::beast::http::verb::get, path, 11};

    try {
      auto response = handler.handleRequest(req);

      std::cout << "Status: " << response.result() << std::endl;
      std::cout << "Content-Type: "
                << response[boost::beast::http::field::content_type]
                << std::endl;

      // Show special headers for rate limiting
      if (response.result() == boost::beast::http::status::too_many_requests) {
        std::cout << "Retry-After: "
                  << response[boost::beast::http::field::retry_after]
                  << std::endl;
        std::cout << "X-Rate-Limit-Limit: " << response["X-Rate-Limit-Limit"]
                  << std::endl;
      }

      std::cout << "Body: " << response.body() << std::endl;

    } catch (const std::exception &e) {
      std::cout << "Unexpected exception: " << e.what() << std::endl;
    }
  }
}

/**
 * @brief Demonstrates mapping an exception using the global ExceptionMapper.
 *
 * Constructs a ValidationException (missing 'email'), maps it to an HTTP-like
 * response via the global ExceptionMapper, and prints the resulting status and
 * body to stdout. Used for demonstration and manual verification of the
 * globally shared mapper configuration.
 */
void demonstrateGlobalExceptionMapper() {
  std::cout << "\n=== Global ExceptionMapper Demo ===" << std::endl;

  // Use the global exception mapper
  auto &globalMapper = getGlobalExceptionMapper();

  // Test with different exception types
  auto validationEx =
      etl::ValidationException(etl::ErrorCode::MISSING_FIELD,
                               "Required field 'email' is missing", "email");

  auto response = globalMapper.mapToResponse(validationEx, "global_test");

  std::cout << "Global mapper response: " << response.result() << std::endl;
  std::cout << "Body: " << response.body() << std::endl;
}

/**
 * @brief Program entry point that runs demonstration workflows for exception mapping.
 *
 * Executes the per-instance and global ExceptionMapper demonstrations. Prints a completion
 * message on success and returns a zero exit code. If an uncaught std::exception is thrown
 * during the demos, the exception message is printed to stderr and the process returns 1.
 *
 * @return int 0 on successful completion; 1 if a std::exception is caught.
 */
int main() {
  try {
    demonstrateExceptionMapping();
    demonstrateGlobalExceptionMapper();

    std::cout << "\n=== Demo completed successfully! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Demo failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
