#pragma once

#include "etl_exceptions.hpp"
#include "error_codes.hpp"
#include <boost/hana.hpp>
#include <boost/beast/http.hpp>
#include <unordered_map>
#include <string>
#include <typeindex>

namespace etl {
namespace hana_exception_handling {

// ============================================================================
// Compile-time Exception Type Registry using Hana
// ============================================================================

// Define all exception types in a Hana tuple for compile-time processing
using ExceptionTypes = boost::hana::tuple<
    boost::hana::type<ValidationException>,
    boost::hana::type<SystemException>,
    boost::hana::type<BusinessException>
>;

// HTTP status mappings for each exception type
template<typename ExceptionType>
struct ExceptionHttpStatus;

template<>
struct ExceptionHttpStatus<ValidationException> {
    static constexpr boost::beast::http::status value = boost::beast::http::status::bad_request;
};

template<>
struct ExceptionHttpStatus<SystemException> {
    static constexpr boost::beast::http::status value = boost::beast::http::status::internal_server_error;
};

template<>
struct ExceptionHttpStatus<BusinessException> {
    static constexpr boost::beast::http::status value = boost::beast::http::status::unprocessable_entity;
};

// ============================================================================
// Hana-based Exception Handler Registry
// ============================================================================

template<typename ExceptionType>
struct ExceptionHandler {
    using exception_type = ExceptionType;

    template<typename Func>
    static constexpr auto make_handler(Func&& func) {
        return std::forward<Func>(func);
    }
};

// Compile-time exception handler registry
template<typename... Handlers>
class HanaExceptionRegistry {
private:
    std::unordered_map<std::type_index, std::function<boost::beast::http::response<boost::beast::http::string_body>(
        const ETLException&, const std::string&)>> handlers_;

public:
    HanaExceptionRegistry() = default;

    // Register handlers for specific exception types at compile time
    template<typename ExceptionType, typename Handler>
    void registerHandler(Handler&& handler) {
        handlers_[std::type_index(typeid(ExceptionType))] =
            [handler = std::forward<Handler>(handler)](const ETLException& ex, const std::string& op) mutable {
                // Try to cast to the expected type
                if (auto* typedEx = dynamic_cast<const ExceptionType*>(&ex)) {
                    return handler(*typedEx, op);
                }
                // If cast fails, this shouldn't happen with proper usage
                throw std::bad_cast();
            };
    }

    // Get handler for exception type
    boost::beast::http::response<boost::beast::http::string_body>
    handle(const ETLException& ex, const std::string& operation = "") const {
        auto it = handlers_.find(std::type_index(typeid(ex)));
        if (it != handlers_.end()) {
            return it->second(ex, operation);
        }

        // Default handler based on exception type
        return createDefaultResponse(ex, operation);
    }

private:
    boost::beast::http::response<boost::beast::http::string_body>
    createDefaultResponse(const ETLException& ex, const std::string& operation) const {
        namespace http = boost::beast::http;

        http::response<http::string_body> res;

        // Determine the appropriate HTTP status based on exception type
        boost::beast::http::status status = boost::beast::http::status::internal_server_error;
        if (dynamic_cast<const ValidationException*>(&ex)) {
            status = ExceptionHttpStatus<ValidationException>::value;
        } else if (dynamic_cast<const SystemException*>(&ex)) {
            status = ExceptionHttpStatus<SystemException>::value;
        } else if (dynamic_cast<const BusinessException*>(&ex)) {
            status = ExceptionHttpStatus<BusinessException>::value;
        }

        res.result(status);
        res.set(http::field::content_type, "application/json");
        res.set(http::field::server, "ETL Plus Backend");

        // Create error response JSON
        std::string errorJson = R"({
            "status": "error",
            "message": ")" + std::string(ex.what()) + R"(",
            "code": ")" + std::to_string(static_cast<int>(ex.getCode())) + R"(",
            "correlationId": ")" + ex.getCorrelationId() + R"(",
            "timestamp": ")" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                ex.getTimestamp().time_since_epoch()).count()) + R"("
        })";

        if (!operation.empty()) {
            errorJson += R"(,
            "operation": ")" + operation + R"(")";
        }

        errorJson += "\n}";

        res.body() = errorJson;
        res.prepare_payload();

        return res;
    }
};

// ============================================================================
// Hana-based Exception Type Checking
// ============================================================================

// Compile-time check if an exception type is registered
template<typename ExceptionType>
constexpr bool is_registered_exception = boost::hana::contains(
    ExceptionTypes{},
    boost::hana::type_c<ExceptionType>
);

// Get the HTTP status for an exception type at compile time
template<typename ExceptionType>
constexpr auto get_exception_status() {
    static_assert(is_registered_exception<ExceptionType>,
                  "Exception type not registered in Hana registry");
    return ExceptionHttpStatus<ExceptionType>::value;
}

// ============================================================================
// Functional Exception Processing Pipeline
// ============================================================================

template<typename... Processors>
class ExceptionProcessingPipeline {
private:
    boost::hana::tuple<Processors...> processors_;

public:
    explicit ExceptionProcessingPipeline(Processors... processors)
        : processors_(std::move(processors)...) {}

    template<typename ExceptionType>
    auto process(const ExceptionType& ex, const std::string& context = "") const {
        return boost::hana::fold_left(
            processors_,
            std::make_tuple(ex, context),
            [](auto&& state, auto&& processor) {
                auto&& [currentEx, currentContext] = state;
                return processor(currentEx, currentContext);
            }
        );
    }
};

// ============================================================================
// Practical Exception Handler Factories
// ============================================================================

// Factory for creating validation error handlers
inline auto makeValidationErrorHandler() {
    return [](const ValidationException& ex, const std::string& operation) {
        namespace http = boost::beast::http;
        http::response<http::string_body> res{http::status::bad_request, 11};
        res.set(http::field::content_type, "application/json");

        std::string json = R"({
            "status": "validation_error",
            "message": ")" + std::string(ex.what()) + R"(",
            "field": ")" + ex.getField() + R"(",
            "value": ")" + ex.getValue() + R"(",
            "operation": ")" + operation + R"("
        })";

        // Add validation context if available
        const auto& context = ex.getContext();
        if (!context.empty()) {
            json += R"(,
            "validation_details": {)";
            for (auto it = context.begin(); it != context.end(); ++it) {
                if (it != context.begin()) json += ",";
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
    return [](const SystemException& ex, const std::string& operation) {
        namespace http = boost::beast::http;
        http::response<http::string_body> res{http::status::internal_server_error, 11};
        res.set(http::field::content_type, "application/json");

        std::string json = R"({
            "status": "system_error",
            "message": ")" + std::string(ex.what()) + R"(",
            "component": ")" + ex.getComponent() + R"(",
            "operation": ")" + operation + R"("
        })";

        // Add system context
        const auto& context = ex.getContext();
        if (!context.empty()) {
            json += R"(,
            "system_details": {)";
            for (auto it = context.begin(); it != context.end(); ++it) {
                if (it != context.begin()) json += ",";
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
    return [](const BusinessException& ex, const std::string& operation) {
        namespace http = boost::beast::http;
        http::response<http::string_body> res{http::status::unprocessable_entity, 11};
        res.set(http::field::content_type, "application/json");

        std::string json = R"({
            "status": "business_error",
            "message": ")" + std::string(ex.what()) + R"(",
            "operation": ")" + ex.getOperation() + R"(",
            "request_operation": ")" + operation + R"("
        })";

        // Add business context
        const auto& context = ex.getContext();
        if (!context.empty()) {
            json += R"(,
            "business_details": {)";
            for (auto it = context.begin(); it != context.end(); ++it) {
                if (it != context.begin()) json += ",";
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

} // namespace hana_exception_handling
} // namespace etl
