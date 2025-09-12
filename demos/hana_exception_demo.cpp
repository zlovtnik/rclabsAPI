#include "etl_exceptions.hpp"
#include "hana_exception_handling.hpp"
#include <boost/beast/http.hpp>
#include <iostream>

/**
 * @brief Demonstrates using a Hana-based exception registry to handle ETL
 * exceptions.
 *
 * This function showcases registering type-specific handlers with a
 * HanaExceptionRegistry, then simulating three ETL exception scenarios
 * (validation, system, and business). For each simulated exception it invokes
 * the registry to produce an HTTP-like response and writes the response bodies
 * to standard output. It also performs compile-time checks via static_assert to
 * verify that a specific exception type is registered and that its mapped HTTP
 * status is as expected.
 *
 * Side effects:
 * - Registers handlers on a local HanaExceptionRegistry instance.
 * - Writes multiple diagnostic/response lines to std::cout.
 * - Triggers compile-time assertions (static_assert) for registration and
 * status mapping.
 *
 * This function does not return a value and does not propagate the simulated
 * exceptions (they are thrown and caught internally).
 */
void demonstrate_functional_hana_usage() {
  using namespace ETLPlus::ExceptionHandling;

  std::cout << "=== Functional Hana Exception Handling Demo ===\n";

  // Create the Hana-based exception registry
  HanaExceptionRegistry registry;

  // Register specific handlers for each exception type
  registry.registerHandler<etl::ValidationException>(
      makeValidationErrorHandler());
  registry.registerHandler<etl::SystemException>(makeSystemErrorHandler());
  registry.registerHandler<etl::BusinessException>(makeBusinessErrorHandler());

  // Test with different exception types
  try {
    // Simulate a validation error
    throw etl::ValidationException(
        etl::ErrorCode::INVALID_INPUT, "Invalid email format", "email",
        "invalid-email",
        {{"pattern", "user@domain.com"}, {"maxLength", "254"}});
  } catch (const etl::ETLException &ex) {
    auto response = registry.handle(ex, "user_registration");
    std::cout << "Validation Error Response:\n" << response.body() << "\n\n";
  }

  try {
    // Simulate a system error
    throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                               "Database connection failed", "PostgreSQL",
                               {{"host", "localhost"}, {"port", "5432"}});
  } catch (const etl::ETLException &ex) {
    auto response = registry.handle(ex, "user_query");
    std::cout << "System Error Response:\n" << response.body() << "\n\n";
  }

  try {
    // Simulate a business error
    throw etl::BusinessException(
        etl::ErrorCode::DATA_INTEGRITY_ERROR, "Insufficient account balance",
        "funds_transfer", {{"required", "100.00"}, {"available", "50.00"}});
  } catch (const etl::ETLException &ex) {
    auto response = registry.handle(ex, "transfer_funds");
    std::cout << "Business Error Response:\n" << response.body() << "\n\n";
  }

  // Demonstrate compile-time type checking
  static_assert(is_registered_exception<etl::ValidationException>,
                "ValidationException should be registered");
  static_assert(get_exception_status<etl::ValidationException>() ==
                    boost::beast::http::status::bad_request,
                "ValidationException should map to 400 Bad Request");

  std::cout << "✅ All compile-time checks passed!\n";
  std::cout << "=== Hana Integration Provides Real Functional Benefits ===\n";
}

/**
 * @brief Program entry point that runs the Hana-based exception handling
 * demonstration.
 *
 * Calls demonstrate_functional_hana_usage() to register handlers, exercise
 * exception scenarios, and print resulting HTTP-like responses to stdout.
 *
 * @return int Exit status (0 indicates successful completion of the demo).
 */
int main() {
  demonstrate_functional_hana_usage();
  return 0;
}
