#include "etl_exceptions.hpp"
#include "hana_exception_handling.hpp"
#include <boost/beast/http.hpp>
#include <iostream>

// Example of how Hana improves real exception handling
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

int main() {
  demonstrate_functional_hana_usage();
  return 0;
}
