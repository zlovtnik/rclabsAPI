#pragma once

#include "error_codes.hpp"
#include "etl_exceptions.hpp"
#include <boost/beast/http.hpp>
#include <boost/hana.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace ETLPlus {
namespace ExceptionHandling {

// Helper function to escape JSON strings
inline std::string escapeJsonString(const std::string &input) {
  std::ostringstream escaped;
  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    // Handle UTF-8 multi-byte sequences (bytes >= 128)
    if (static_cast<unsigned char>(c) >= 128) {
      // Copy UTF-8 bytes unchanged
      escaped << c;
      continue;
    }
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
      if (static_cast<unsigned char>(c) < 32) {
        // Escape control characters
        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<int>(static_cast<unsigned char>(c));
      } else {
        escaped << c;
      }
      break;
    }
  }
  return escaped.str();
}

// ============================================================================
// Compile-time Exception Type Registry using Hana
// ============================================================================

// Define all exception types in a Hana tuple for compile-time processing
using ExceptionTypes =
    boost::hana::tuple<boost::hana::type<etl::ValidationException>,
                       boost::hana::type<etl::SystemException>,
                       boost::hana::type<etl::BusinessException>>;

// HTTP status mappings for each exception type
template <typename ExceptionType> struct ExceptionHttpStatus;

template <> struct ExceptionHttpStatus<etl::ValidationException> {
  static constexpr boost::beast::http::status value =
      boost::beast::http::status::bad_request;
};

template <> struct ExceptionHttpStatus<etl::SystemException> {
  static constexpr boost::beast::http::status value =
      boost::beast::http::status::internal_server_error;
};

template <> struct ExceptionHttpStatus<etl::BusinessException> {
  static constexpr boost::beast::http::status value =
      boost::beast::http::status::unprocessable_entity;
};

// ============================================================================
// Hana-based Exception Handler Template
// ============================================================================

template <typename ExceptionType> struct HanaExceptionHandler {
  using exception_type = ExceptionType;

  template <typename Func> static constexpr auto make_handler(Func &&func) {
    return std::forward<Func>(func);
  }
};

// ============================================================================
// Hana-based Exception Registry
// ============================================================================

class HanaExceptionRegistry {
private:
  std::unordered_map<std::type_index,
                     std::function<boost::beast::http::response<
                         boost::beast::http::string_body>(
                         const etl::ETLException &, const std::string &)>>
      handlers_;

public:
  HanaExceptionRegistry() = default;

  // Register handlers for specific exception types at compile time
  template <typename ExceptionType, typename Handler>
  void registerHandler(Handler &&handler) {
    handlers_[std::type_index(typeid(ExceptionType))] =
        [this, handler = std::forward<Handler>(handler)](
            const etl::ETLException &ex, const std::string &op) mutable {
          // Try to cast to the expected type
          if (auto *typedEx = dynamic_cast<const ExceptionType *>(&ex)) {
            return handler(*typedEx, op);
          }
          // Fallback to default response if cast fails
          return createDefaultResponse(ex, op);
        };
  }

  // Get handler for exception type
  boost::beast::http::response<boost::beast::http::string_body>
  handle(const etl::ETLException &ex, const std::string &operation = "") const {
    auto it = handlers_.find(std::type_index(typeid(ex)));
    if (it != handlers_.end()) {
      return it->second(ex, operation);
    }

    // Fallback to default response
    return createDefaultResponse(ex, operation);
  }

private:
  boost::beast::http::response<boost::beast::http::string_body>
  createDefaultResponse(const etl::ETLException &ex,
                        const std::string &operation) const {
    namespace http = boost::beast::http;

    http::response<http::string_body> res;

    // Determine status based on exception type
    boost::beast::http::status status = http::status::internal_server_error;
    if (dynamic_cast<const etl::ValidationException *>(&ex)) {
      status = ExceptionHttpStatus<etl::ValidationException>::value;
    } else if (dynamic_cast<const etl::SystemException *>(&ex)) {
      status = ExceptionHttpStatus<etl::SystemException>::value;
    } else if (dynamic_cast<const etl::BusinessException *>(&ex)) {
      status = ExceptionHttpStatus<etl::BusinessException>::value;
    }

    res.result(status);
    res.set(http::field::content_type, "application/json");
    res.set(http::field::server, "ETL Plus Backend");

    // Create error response JSON with proper escaping
    std::ostringstream jsonStream;
    jsonStream << "{";
    jsonStream << "\"status\":\"error\",";
    jsonStream << "\"message\":\"" << escapeJsonString(std::string(ex.what()))
               << "\",";
    jsonStream << "\"code\":\""
               << std::to_string(static_cast<int>(ex.getCode())) << "\",";
    jsonStream << "\"correlationId\":\""
               << escapeJsonString(ex.getCorrelationId()) << "\",";
    jsonStream << "\"timestamp\":\""
               << std::to_string(
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          ex.getTimestamp().time_since_epoch())
                          .count())
               << "\"";

    if (!operation.empty()) {
      jsonStream << ",\"operation\":\"" << escapeJsonString(operation) << "\"";
    }

    jsonStream << "}";

    res.body() = jsonStream.str();
    res.prepare_payload();

    return res;
  }
};

// ============================================================================
// Hana-based Exception Type Checking
// ============================================================================

// Compile-time check if an exception type is registered
template <typename ExceptionType>
constexpr bool is_registered_exception =
    boost::hana::contains(ExceptionTypes{}, boost::hana::type_c<ExceptionType>);

// Get the HTTP status for an exception type at compile time
template <typename ExceptionType> constexpr auto get_exception_status() {
  static_assert(is_registered_exception<ExceptionType>,
                "Exception type not registered in Hana registry");
  return ExceptionHttpStatus<ExceptionType>::value;
}

// ============================================================================
// Functional Exception Processing Pipeline
// ============================================================================

template <typename... Processors> class ExceptionProcessingPipeline {
private:
  boost::hana::tuple<Processors...> processors_;

public:
  explicit ExceptionProcessingPipeline(Processors... processors)
      : processors_(std::move(processors)...) {}

  template <typename ExceptionType>
  auto process(const ExceptionType &ex, const std::string &context = "") const {
    return boost::hana::fold_left(processors_, std::make_tuple(ex, context),
                                  [](auto &&state, auto &&processor) {
                                    auto &&[currentEx, currentContext] = state;
                                    return processor(currentEx, currentContext);
                                  });
  }
};

// ============================================================================
// Practical Exception Handler Factories
// ============================================================================

// Factory for creating validation error handlers
inline auto makeValidationErrorHandler() {
  return [](const etl::ValidationException &ex, const std::string &operation) {
    namespace http = boost::beast::http;
    http::response<http::string_body> res{http::status::bad_request, 11};
    res.set(http::field::content_type, "application/json");

    std::string json = R"({
            "status": "validation_error",
            "message": ")" +
                       std::string(ex.what()) + R"(",
            "field": ")" +
                       ex.getField() + R"(",
            "value": ")" +
                       ex.getValue() + R"(",
            "operation": ")" +
                       operation + R"("
        })";

    // Add validation context if available
    const auto &context = ex.getContext();
    if (!context.empty()) {
      json += R"(,
            "validation_details": {)";
      for (auto it = context.begin(); it != context.end(); ++it) {
        if (it != context.begin())
          json += ",";
        json += "\"" + it->first + "\":\"" + it->second + "\"";
      }
      json += "}";
    }
    json += "\n}";

    res.body() = json;
    res.prepare_payload();
    return res;
  };
}

// Factory for creating system error handlers
inline auto makeSystemErrorHandler() {
  return [](const etl::SystemException &ex, const std::string &operation) {
    namespace http = boost::beast::http;
    http::response<http::string_body> res{http::status::internal_server_error,
                                          11};
    res.set(http::field::content_type, "application/json");

    std::string json = R"({
            "status": "system_error",
            "message": ")" +
                       std::string(ex.what()) + R"(",
            "component": ")" +
                       ex.getComponent() + R"(",
            "operation": ")" +
                       operation + R"("
        })";

    // Add system context
    const auto &context = ex.getContext();
    if (!context.empty()) {
      json += R"(,
            "system_details": {)";
      for (auto it = context.begin(); it != context.end(); ++it) {
        if (it != context.begin())
          json += ",";
        json += "\"" + it->first + "\":\"" + it->second + "\"";
      }
      json += "}";
    }
    json += "\n}";

    res.body() = json;
    res.prepare_payload();
    return res;
  };
}

// Factory for creating business error handlers
inline auto makeBusinessErrorHandler() {
  return [](const etl::BusinessException &ex, const std::string &operation) {
    namespace http = boost::beast::http;
    http::response<http::string_body> res{http::status::unprocessable_entity,
                                          11};
    res.set(http::field::content_type, "application/json");

    std::string json = R"({
            "status": "business_error",
            "message": ")" +
                       std::string(ex.what()) + R"(",
            "operation": ")" +
                       ex.getOperation() + R"(",
            "request_operation": ")" +
                       operation + R"("
        })";

    // Add business context
    const auto &context = ex.getContext();
    if (!context.empty()) {
      json += R"(,
            "business_details": {)";
      for (auto it = context.begin(); it != context.end(); ++it) {
        if (it != context.begin())
          json += ",";
        json += "\"" + it->first + "\":\"" + it->second + "\"";
      }
      json += "}";
    }
    json += "\n}";

    res.body() = json;
    res.prepare_payload();
    return res;
  };
}

} // namespace ExceptionHandling
} // namespace ETLPlus
