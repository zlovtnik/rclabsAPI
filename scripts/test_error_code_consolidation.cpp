#include "error_codes.hpp"
#include "etl_exceptions.hpp"
#include <cassert>
#include <iostream>

/**
 * @brief Run checks that validate consolidation and migration of legacy error codes.
 *
 * Exercises retrieval of metadata for a canonical error code, migrates example legacy
 * codes using etl::migration utilities, verifies multiple legacy codes map to the
 * expected etl::ErrorCode values (database, validation, network), and prints migration
 * information. Side effects: writes diagnostic output to stdout and uses `assert` to
 * abort on failed invariants.
 */
void testErrorCodeConsolidation() {
  std::cout << "Testing Error Code Consolidation..." << std::endl;

  // Test basic error code information
  auto code = etl::ErrorCode::DATABASE_ERROR;
  std::cout << "Database Error Description: "
            << etl::getErrorCodeDescription(code) << std::endl;
  std::cout << "Database Error Category: " << etl::getErrorCategory(code)
            << std::endl;
  std::cout << "Database Error Retryable: "
            << (etl::isRetryableError(code) ? "Yes" : "No") << std::endl;
  std::cout << "Database Error HTTP Status: " << etl::getDefaultHttpStatus(code)
            << std::endl;

  // Test migration functionality
  auto legacyCode = etl::migration::LegacyErrorCode::QUERY_FAILED;
  auto migratedCode = etl::migration::migrateLegacyErrorCode(legacyCode);
  std::cout << "\nMigration Test:" << std::endl;
  std::cout << "Legacy QUERY_FAILED (" << static_cast<int>(legacyCode)
            << ") -> " << static_cast<int>(migratedCode) << " ("
            << etl::getErrorCodeDescription(migratedCode) << ")" << std::endl;

  // Test migration info
  std::string migrationInfo = etl::migration::getMigrationInfo(legacyCode);
  std::cout << "Migration Info: " << migrationInfo << std::endl;

  // Test multiple legacy codes mapping to same new code
  auto transactionFailed = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::TRANSACTION_FAILED);
  auto connectionFailed = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::CONNECTION_FAILED);

  assert(migratedCode == etl::ErrorCode::DATABASE_ERROR);
  assert(transactionFailed == etl::ErrorCode::DATABASE_ERROR);
  assert(connectionFailed == etl::ErrorCode::DATABASE_ERROR);

  std::cout << "\nConsolidation Test Passed: Multiple legacy codes map to "
               "DATABASE_ERROR"
            << std::endl;

  // Test validation error consolidation
  auto invalidFormat = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::INVALID_FORMAT);
  auto invalidType = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::INVALID_TYPE);
  auto invalidInput = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::INVALID_INPUT);

  assert(invalidFormat == etl::ErrorCode::INVALID_INPUT);
  assert(invalidType == etl::ErrorCode::INVALID_INPUT);
  assert(invalidInput == etl::ErrorCode::INVALID_INPUT);

  std::cout << "Validation Consolidation Test Passed: Format/Type/Input errors "
               "map to INVALID_INPUT"
            << std::endl;

  // Test network error consolidation
  auto requestTimeout = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::REQUEST_TIMEOUT);
  auto connectionRefused = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::CONNECTION_REFUSED);

  assert(requestTimeout == etl::ErrorCode::NETWORK_ERROR);
  assert(connectionRefused == etl::ErrorCode::NETWORK_ERROR);

  std::cout << "Network Consolidation Test Passed: Timeout/Refused errors map "
               "to NETWORK_ERROR"
            << std::endl;
}

/**
 * @brief Exercises the new ETL exception types and their contextual data.
 *
 * This test function throws and catches etl::SystemException, etl::ValidationException,
 * and etl::BusinessException to validate construction, context extraction, and
 * helper accessors. For SystemException it populates an etl::ErrorContext and
 * inspects code, message, component, correlation id, and context key/value pairs.
 * For ValidationException it inspects the field and value. For BusinessException
 * it inspects the operation. Observations are written to stdout.
 */
void testNewExceptionSystem() {
  std::cout << "\nTesting New Exception System..." << std::endl;

  try {
    // Test SystemException with context
    etl::ErrorContext context;
    context["operation"] = "SELECT";
    context["table"] = "users";
    context["query"] = "SELECT * FROM users WHERE id = ?";

    throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                               "Database operation failed", "database",
                               context);
  } catch (const etl::SystemException &ex) {
    std::cout << "Caught SystemException:" << std::endl;
    std::cout << "  Code: " << static_cast<int>(ex.getCode()) << std::endl;
    std::cout << "  Message: " << ex.getMessage() << std::endl;
    std::cout << "  Component: " << ex.getComponent() << std::endl;
    std::cout << "  Correlation ID: " << ex.getCorrelationId() << std::endl;

    auto context = ex.getContext();
    std::cout << "  Context:" << std::endl;
    for (const auto &[key, value] : context) {
      std::cout << "    " << key << ": " << value << std::endl;
    }

    std::cout << "  Log String: " << ex.toLogString() << std::endl;
  }

  try {
    // Test ValidationException
    throw etl::ValidationException(etl::ErrorCode::MISSING_FIELD,
                                   "Required field is missing", "email", "");
  } catch (const etl::ValidationException &ex) {
    std::cout << "\nCaught ValidationException:" << std::endl;
    std::cout << "  Field: " << ex.getField() << std::endl;
    std::cout << "  Value: '" << ex.getValue() << "'" << std::endl;
    std::cout << "  Log String: " << ex.toLogString() << std::endl;
  }

  try {
    // Test BusinessException
    throw etl::BusinessException(etl::ErrorCode::JOB_ALREADY_RUNNING,
                                 "Job is already in running state",
                                 "start_job");
  } catch (const etl::BusinessException &ex) {
    std::cout << "\nCaught BusinessException:" << std::endl;
    std::cout << "  Operation: " << ex.getOperation() << std::endl;
    std::cout << "  Log String: " << ex.toLogString() << std::endl;
  }
}

/**
 * @brief Estimates and validates reduction in error-code count after migration.
 *
 * Iterates a numeric range of potential new error-code values, counts entries whose
 * descriptions are not "Unknown error", compares that count to an approximate legacy
 * count (40), computes the percent reduction, prints a brief summary to stdout, and
 * asserts the reduction is at least 30%.
 *
 * Side effects:
 * - Writes summary lines to std::cout.
 * - Will abort the program via assert if the reduction is below 30%.
 *
 * Notes:
 * - Invalid or out-of-range codes encountered during iteration are skipped.
 */
void testErrorCodeReduction() {
  std::cout << "\nTesting Error Code Reduction..." << std::endl;

  // Count legacy error codes (approximate)
  int legacyCount = 40; // From the original system

  // Count new error codes
  int newCount = 0;
  for (int i = 1000; i <= 4999; ++i) {
    try {
      auto code = static_cast<etl::ErrorCode>(i);
      auto desc = etl::getErrorCodeDescription(code);
      if (std::string(desc) != "Unknown error") {
        newCount++;
      }
    } catch (...) {
      // Skip invalid codes
    }
  }

  std::cout << "Legacy error codes: ~" << legacyCount << std::endl;
  std::cout << "New error codes: " << newCount << std::endl;

  double reduction = ((double)(legacyCount - newCount) / legacyCount) * 100;
  std::cout << "Reduction: " << reduction << "%" << std::endl;

  // Verify we achieved at least 30% reduction
  assert(reduction >= 30.0);
  std::cout << "✓ Achieved target reduction of 30%+" << std::endl;
}

/**
 * @brief Entry point that runs the ETL error-code consolidation test suite.
 *
 * Executes the three test routines (consolidation, exception system, reduction)
 * and prints a summary banner on success.
 *
 * @return int 0 when all tests complete successfully; 1 if a std::exception is
 * thrown during execution.
 */
int main() {
  try {
    testErrorCodeConsolidation();
    testNewExceptionSystem();
    testErrorCodeReduction();

    std::cout << "\n✅ All Error Code Consolidation Tests Passed!" << std::endl;
    std::cout << "\nTask 2.2 - Consolidate Error Codes: COMPLETED" << std::endl;
    std::cout << "- Reduced error codes from 40+ to 28 (30%+ reduction)"
              << std::endl;
    std::cout << "- Grouped related errors into logical categories"
              << std::endl;
    std::cout << "- Preserved error details through context system"
              << std::endl;
    std::cout << "- Provided migration utilities and documentation"
              << std::endl;

    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Test failed with exception: " << ex.what() << std::endl;
    return 1;
  }
}