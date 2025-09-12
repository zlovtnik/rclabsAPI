#include "etl_exceptions.hpp"
#include "exception_handler.hpp"
#include "logger.hpp"
#include <iostream>

using namespace etl;

/**
 * @brief Demonstrates wrapping a low-level exception inside a higher-level
 * system exception and surfacing its context.
 *
 * This demo simulates a database failure by throwing a SystemException for a
 * database error, catches it, builds an ErrorContext containing the original
 * error message and code, and rethrows a higher-level SystemException (internal
 * error) that carries that context. The outer catch prints the resulting
 * exception as JSON and enumerates the context entries.
 *
 * Side effects: writes demo headers, the exception JSON, and context key/value
 * pairs to stdout.
 */
void demonstrateExceptionChaining() {
  try {
    try {
      // Simulate a database connection failure
      throw SystemException(ErrorCode::DATABASE_ERROR,
                            "Database connection failed", "DatabaseManager");
    } catch (const SystemException &db_ex) {
      // Wrap in a higher-level exception
      auto system_ex =
          SystemException(ErrorCode::INTERNAL_ERROR,
                          "System initialization failed", "SystemBootstrap");
      // Note: New exception system doesn't have chaining, but we can log the
      // context
      ErrorContext context;
      context["original_error"] = db_ex.getMessage();
      context["original_code"] =
          std::to_string(static_cast<int>(db_ex.getCode()));
      throw SystemException(ErrorCode::INTERNAL_ERROR,
                            "System initialization failed", "SystemBootstrap",
                            context);
    }
  } catch (const ETLException &ex) {
    std::cout << "=== Exception Chaining Demo ===" << std::endl;
    std::cout << "Main Exception: " << ex.toJsonString() << std::endl;
    std::cout << "Exception Context: " << std::endl;
    for (const auto &[key, value] : ex.getContext()) {
      std::cout << "  " << key << ": " << value << std::endl;
    }
  }
}

/**
 * @brief Demonstrates a simple manual retry loop for transient failures.
 *
 * Runs up to three attempts to simulate an operation that may fail with a
 * network-related ETL/SystemException. On each attempt it prints the attempt
 * number; if an attempt fails and there are remaining retries it prints a
 * retry message and continues. If the final attempt fails, the exception is
 * rethrown and then caught by the outer handler which prints a final failure
 * message.
 *
 * Side effects:
 * - Writes status and error messages to std::cout.
 * - May throw an ETLException from the final failed attempt (which is caught
 *   within the function to print the final failure message).
 */
void demonstrateRetryLogic() {
  std::cout << "\n=== Retry Logic Demo ===" << std::endl;

  int attempts = 0;

  try {
    // For simplicity, let's manually implement retry since the template is
    // complex
    for (int i = 0; i < 3; i++) {
      try {
        attempts++;
        std::cout << "Attempt " << attempts << std::endl;
        if (attempts < 3) {
          throw SystemException(ErrorCode::NETWORK_ERROR, "Network timeout",
                                "HttpClient");
        }
        std::cout << "Success on attempt " << attempts << std::endl;
        break;
      } catch (const ETLException &) {
        if (i == 2)
          throw; // Re-throw on final attempt
        std::cout << "Attempt failed, retrying..." << std::endl;
      }
    }
  } catch (const ETLException &ex) {
    std::cout << "Final failure: " << ex.getMessage() << std::endl;
  }
}

/**
 * @brief Demonstrates handling and printing three ETL exception types.
 *
 * Prints a header then exercises ValidationException, SystemException, and
 * BusinessException by throwing each one, catching it as ETLException, and
 * outputting its JSON representation to standard output. Used for demo/testing
 * of exception formatting and basic catch behavior; all throws are handled
 * locally (no exceptions propagate).
 */
void demonstrateBasicExceptions() {
  std::cout << "\n=== Basic Exception Demo ===" << std::endl;

  // Test different exception types
  try {
    throw ValidationException(ErrorCode::INVALID_INPUT, "Invalid user input",
                              "user_email", "invalid@email");
  } catch (const ETLException &ex) {
    std::cout << "ValidationException: " << ex.toJsonString() << std::endl;
  }

  try {
    throw SystemException(ErrorCode::UNAUTHORIZED, "Authentication failed",
                          "AuthManager");
  } catch (const ETLException &ex) {
    std::cout << "SystemException: " << ex.toJsonString() << std::endl;
  }

  try {
    throw BusinessException(ErrorCode::PROCESSING_FAILED,
                            "ETL job processing failed", "DataProcessing");
  } catch (const ETLException &ex) {
    std::cout << "BusinessException: " << ex.toJsonString() << std::endl;
  }
}

/**
 * @brief Entry point for the ETL Plus Exception Handling System demo.
 *
 * Runs three demonstration scenarios that exercise exception chaining, retry
 * logic, and basic exception types, printing human-readable headers and
 * JSON/text outputs to stdout. After executing the demos, prints a completion
 * message.
 *
 * @return int Exit status code (0 on successful completion).
 */
int main() {
  std::cout << "ETL Plus Exception Handling System Demo" << std::endl;
  std::cout << "=======================================" << std::endl;

  demonstrateExceptionChaining();
  demonstrateRetryLogic();
  demonstrateBasicExceptions();

  std::cout << "\nDemo completed successfully!" << std::endl;
  return 0;
}
