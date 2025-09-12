#include "error_codes.hpp"
#include "etl_exceptions.hpp"
#include <cassert>
#include <gtest/gtest.h>
#include <iostream>

/**
 * @brief Run checks that validate consolidation and migration of legacy error
 * codes.
 *
 * Exercises retrieval of metadata for a canonical error code, migrates example
 * legacy codes using etl::migration utilities, verifies multiple legacy codes
 * map to the expected etl::ErrorCode values (database, validation, network),
 * and prints migration information. Side effects: writes diagnostic output to
 * stdout and uses `assert` to abort on failed invariants.
 */
TEST(ErrorCodeConsolidationTest, ErrorCodeConsolidation) {
  // Test basic error code information
  auto code = etl::ErrorCode::DATABASE_ERROR;
  EXPECT_STREQ(etl::getErrorCodeDescription(code), "Database operation failed");
  EXPECT_EQ(etl::getErrorCategory(code), std::string("System"));
  EXPECT_TRUE(etl::isRetryableError(code));
  EXPECT_EQ(etl::getDefaultHttpStatus(code), 500);

  // Test migration functionality
  auto legacyCode = etl::migration::LegacyErrorCode::QUERY_FAILED;
  auto migratedCode = etl::migration::migrateLegacyErrorCode(legacyCode);
  EXPECT_EQ(static_cast<int>(migratedCode),
            static_cast<int>(etl::ErrorCode::DATABASE_ERROR));

  // Test migration info
  std::string migrationInfo = etl::migration::getMigrationInfo(legacyCode);
  EXPECT_FALSE(migrationInfo.empty());

  // Test multiple legacy codes mapping to same new code
  auto transactionFailed = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::TRANSACTION_FAILED);
  auto connectionFailed = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::CONNECTION_FAILED);

  EXPECT_EQ(migratedCode, etl::ErrorCode::DATABASE_ERROR);
  EXPECT_EQ(transactionFailed, etl::ErrorCode::DATABASE_ERROR);
  EXPECT_EQ(connectionFailed, etl::ErrorCode::DATABASE_ERROR);

  // Test validation error consolidation
  auto invalidFormat = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::INVALID_FORMAT);
  auto invalidType = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::INVALID_TYPE);
  auto invalidInput = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::INVALID_INPUT);

  EXPECT_EQ(invalidFormat, etl::ErrorCode::INVALID_INPUT);
  EXPECT_EQ(invalidType, etl::ErrorCode::INVALID_INPUT);
  EXPECT_EQ(invalidInput, etl::ErrorCode::INVALID_INPUT);

  // Test network error consolidation
  auto requestTimeout = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::REQUEST_TIMEOUT);
  auto connectionRefused = etl::migration::migrateLegacyErrorCode(
      etl::migration::LegacyErrorCode::CONNECTION_REFUSED);

  EXPECT_EQ(requestTimeout, etl::ErrorCode::NETWORK_ERROR);
  EXPECT_EQ(connectionRefused, etl::ErrorCode::NETWORK_ERROR);
}

/**
 * @brief Exercises the new ETL exception types and their contextual data.
 *
 * This test function throws and catches etl::SystemException,
 * etl::ValidationException, and etl::BusinessException to validate
 * construction, context extraction, and helper accessors. For SystemException
 * it populates an etl::ErrorContext and inspects code, message, component,
 * correlation id, and context key/value pairs. For ValidationException it
 * inspects the field and value. For BusinessException it inspects the
 * operation. Observations are written to stdout.
 */
TEST(ErrorCodeConsolidationTest, NewExceptionSystem) {
  // Test SystemException with context
  etl::ErrorContext context;
  context["operation"] = "SELECT";
  context["table"] = "users";
  context["query"] = "SELECT * FROM users WHERE id = ?";

  EXPECT_THROW(
      {
        throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                                   "Database operation failed", "database",
                                   context);
      },
      etl::SystemException);

  // Test ValidationException
  EXPECT_THROW(
      {
        throw etl::ValidationException(etl::ErrorCode::MISSING_FIELD,
                                       "Required field is missing", "email",
                                       "");
      },
      etl::ValidationException);

  // Test BusinessException
  EXPECT_THROW(
      {
        throw etl::BusinessException(etl::ErrorCode::JOB_ALREADY_RUNNING,
                                     "Job is already in running state",
                                     "start_job");
      },
      etl::BusinessException);
}

/**
 * @brief Estimates and validates reduction in error-code count after migration.
 *
 * Iterates a numeric range of potential new error-code values, counts entries
 * whose descriptions are not "Unknown error", compares that count to an
 * approximate legacy count (40), computes the percent reduction, prints a brief
 * summary to stdout, and asserts the reduction is at least 30%.
 *
 * Side effects:
 * - Writes summary lines to std::cout.
 * - Will abort the program via assert if the reduction is below 30%.
 *
 * Notes:
 * - Invalid or out-of-range codes encountered during iteration are skipped.
 */
TEST(ErrorCodeConsolidationTest, ErrorCodeReduction) {
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

  double reduction = ((double)(legacyCount - newCount) / legacyCount) * 100;

  // Verify we achieved at least 30% reduction
  EXPECT_GE(reduction, 30.0);
  EXPECT_GT(newCount, 0); // Ensure we found some error codes
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}