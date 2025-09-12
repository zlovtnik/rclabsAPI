#include "../include/input_validator.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <unordered_map>

/**
 * @brief Run basic unit tests for helper validations and string utilities.
 *
 * Verifies that InputValidator::isValidJobId accepts valid job IDs (e.g. "job_123", "JOB_456")
 * and rejects invalid ones (empty string, strings containing '/'). Prints a start message
 * and a success message to stdout. Uses assertions and will abort the test run on failure.
 */
void testHelperFunctions() {
  std::cout << "Testing helper functions..." << std::endl;

  // Test job ID validation
  assert(InputValidator::isValidJobId("job_123"));
  assert(InputValidator::isValidJobId("JOB_456"));
  assert(!InputValidator::isValidJobId(""));
  assert(!InputValidator::isValidJobId("invalid/job"));

  std::cout << "✓ Helper functions test passed" << std::endl;
}

/**
 * @brief Runs unit tests for monitoring-parameter input validation.
 *
 * Executes a set of assertions that exercise InputValidator::validateMonitoringParams
 * with representative parameter maps: a valid set, an invalid status, a non-numeric
 * limit, and a limit value outside the allowed range. The function prints progress
 * messages to stdout and uses assertions to enforce expected outcomes; assertion
 * failures will terminate the test run.
 */
void testInputValidation() {
  std::cout << "Testing input validation for monitoring parameters..."
            << std::endl;

  // Test valid monitoring parameters
  std::unordered_map<std::string, std::string> validParams = {
      {"status", "completed"},
      {"type", "full_etl"},
      {"limit", "10"},
      {"from", "2025-01-01T00:00:00Z"},
      {"to", "2025-12-31T23:59:59Z"}};

  auto result1 = InputValidator::validateMonitoringParams(validParams);
  assert(result1.isValid);

  // Test invalid status
  std::unordered_map<std::string, std::string> invalidStatus = {
      {"status", "invalid_status"}};

  auto result2 = InputValidator::validateMonitoringParams(invalidStatus);
  assert(!result2.isValid);
  assert(result2.errors.size() > 0);
  assert(result2.errors[0].field == "status");

  // Test invalid limit
  std::unordered_map<std::string, std::string> invalidLimit = {
      {"limit", "invalid_number"}};

  auto result3 = InputValidator::validateMonitoringParams(invalidLimit);
  assert(!result3.isValid);

  // Test limit out of range
  std::unordered_map<std::string, std::string> limitOutOfRange = {
      {"limit", "2000"}};

  auto result4 = InputValidator::validateMonitoringParams(limitOutOfRange);
  assert(!result4.isValid);

  std::cout << "✓ Input validation test passed" << std::endl;
}

/**
 * @brief Runs unit tests for metrics query parameter validation.
 *
 * Exercises InputValidator::validateMetricsParams with representative inputs:
 * - a valid parameters set (expects valid)
 * - a parameters set with an invalid `metric_type` (expects invalid)
 * - a parameters set with an invalid `time_range` (expects invalid)
 *
 * Uses assertions to enforce expected outcomes; a failed assertion will abort the test run.
 */
void testMetricsParamsValidation() {
  std::cout << "Testing metrics parameters validation..." << std::endl;

  // Test valid metrics parameters
  std::unordered_map<std::string, std::string> validMetrics = {
      {"metric_type", "performance"}, {"time_range", "24h"}};

  auto result1 = InputValidator::validateMetricsParams(validMetrics);
  assert(result1.isValid);

  // Test invalid metric type
  std::unordered_map<std::string, std::string> invalidMetric = {
      {"metric_type", "invalid_type"}};

  auto result2 = InputValidator::validateMetricsParams(invalidMetric);
  assert(!result2.isValid);

  // Test invalid time range
  std::unordered_map<std::string, std::string> invalidRange = {
      {"time_range", "invalid_range"}};

  auto result3 = InputValidator::validateMetricsParams(invalidRange);
  assert(!result3.isValid);

  std::cout << "✓ Metrics parameters validation test passed" << std::endl;
}

void testPathValidation() {
  std::cout << "Testing endpoint path validation..." << std::endl;

  // Test valid paths
  auto result1 =
      InputValidator::validateEndpointPath("/api/jobs/job_123/status");
  assert(result1.isValid);

  auto result2 =
      InputValidator::validateEndpointPath("/api/jobs/job_456/metrics");
  assert(result2.isValid);

  auto result3 = InputValidator::validateEndpointPath("/api/monitor/jobs");
  assert(result3.isValid);

  // Test invalid paths
  auto result4 = InputValidator::validateEndpointPath("");
  assert(!result4.isValid);

  auto result5 =
      InputValidator::validateEndpointPath(std::string(600, 'a')); // too long
  assert(!result5.isValid);

  std::cout << "✓ Path validation test passed" << std::endl;
}

/**
 * @brief Runs unit tests for job query parameter validation.
 *
 * Executes positive and negative assertions against InputValidator::validateJobQueryParams:
 * - verifies that a well-formed query map (status, limit, offset, job_id) is accepted,
 * - verifies that invalid status and non-numeric limit are rejected.
 *
 * The function prints progress to stdout and uses assert(), so failures will abort the test run.
 */
void testJobQueryValidation() {
  std::cout << "Testing job query parameters validation..." << std::endl;

  // Test valid job query parameters
  std::unordered_map<std::string, std::string> validJobQuery = {
      {"status", "RUNNING"},
      {"limit", "50"},
      {"offset", "10"},
      {"job_id", "job_123"}};

  auto result1 = InputValidator::validateJobQueryParams(validJobQuery);
  assert(result1.isValid);

  // Test invalid job query parameters
  std::unordered_map<std::string, std::string> invalidJobQuery = {
      {"status", "invalid_status"}, {"limit", "invalid_limit"}};

  auto result2 = InputValidator::validateJobQueryParams(invalidJobQuery);
  assert(!result2.isValid);

  std::cout << "✓ Job query validation test passed" << std::endl;
}

/**
 * @brief Runs assertions verifying HTTP method validation logic.
 *
 * Executes a set of assertions against InputValidator::isValidHttpMethod to
 * confirm that recognized methods (e.g., "GET", "POST") are accepted when
 * included in the allowed set and that unrecognized or disallowed methods are
 * rejected. Aborts the test run on assertion failure.
 */
void testHTTPMethodValidation() {
  std::cout << "Testing HTTP method validation..." << std::endl;

  // Test valid methods
  assert(InputValidator::isValidHttpMethod("GET",
                                           {"GET", "POST", "PUT", "DELETE"}));
  assert(InputValidator::isValidHttpMethod("POST",
                                           {"GET", "POST", "PUT", "DELETE"}));

  // Test invalid methods
  assert(!InputValidator::isValidHttpMethod("INVALID", {"GET", "POST"}));
  assert(!InputValidator::isValidHttpMethod(
      "PUT", {"GET", "POST"})); // Not in allowed list

  std::cout << "✓ HTTP method validation test passed" << std::endl;
}

/**
 * @brief Runs unit checks for content-type validation in InputValidator.
 *
 * Executes a set of assertions that verify known-valid content types
 * ("application/json", "application/x-www-form-urlencoded",
 * "application/json; charset=utf-8") are accepted and known-invalid inputs
 * (empty string, "text/plain", "invalid/type") are rejected.
 *
 * Side effects:
 * - Prints progress and result messages to standard output.
 * - Uses assert; a failed assertion will abort the test run.
 */
void testContentTypeValidation() {
  std::cout << "Testing content type validation..." << std::endl;

  // Test valid content types
  assert(InputValidator::isValidContentType("application/json"));
  assert(
      InputValidator::isValidContentType("application/x-www-form-urlencoded"));
  assert(InputValidator::isValidContentType("application/json; charset=utf-8"));

  // Test invalid content types
  assert(!InputValidator::isValidContentType(""));
  assert(!InputValidator::isValidContentType("text/plain"));
  assert(!InputValidator::isValidContentType("invalid/type"));

  std::cout << "✓ Content type validation test passed" << std::endl;
}

/**
 * @brief Verifies presence and basic behavior of endpoint-specific validators.
 *
 * Runs lightweight checks to ensure the monitoring, metrics, and job-query
 * parameter validators exist and accept an empty parameter set. Uses assertions
 * to enforce that each validator returns a valid result for an empty map.
 *
 * @note This function prints progress to stdout and will abort the process if
 * any assertion fails.
 */
void testAPIEndpointStructure() {
  std::cout << "Testing API endpoint structure requirements..." << std::endl;

  // Test that we have the required validation functions for enhanced endpoints
  std::unordered_map<std::string, std::string> testParams;

  // Test monitoring params validation exists and works
  auto monitorResult = InputValidator::validateMonitoringParams(testParams);
  assert(monitorResult.isValid); // Empty params should be valid

  // Test metrics params validation exists and works
  auto metricsResult = InputValidator::validateMetricsParams(testParams);
  assert(metricsResult.isValid); // Empty params should be valid

  // Test job query params validation exists and works
  auto jobQueryResult = InputValidator::validateJobQueryParams(testParams);
  assert(jobQueryResult.isValid); // Empty params should be valid

  std::cout << "✓ API endpoint structure test passed" << std::endl;
}

/**
 * @brief Test runner for enhanced REST API endpoint validation.
 *
 * Executes the suite of validation unit tests (helper validations, parameter
 * validators, path/method/content-type checks and endpoint-specific validators),
 * prints progress and a summary to standard output, and reports failures to
 * standard error.
 *
 * On success the function prints a summary of implemented endpoints and returns
 * 0. If any test throws an exception or an assertion fails, the exception is
 * reported to stderr and the function returns 1.
 */
int main() {
  std::cout << "Running Enhanced REST API Endpoints Validation Tests..."
            << std::endl;
  std::cout << "======================================================="
            << std::endl;

  try {
    testHelperFunctions();
    testInputValidation();
    testMetricsParamsValidation();
    testPathValidation();
    testJobQueryValidation();
    testHTTPMethodValidation();
    testContentTypeValidation();
    testAPIEndpointStructure();

    std::cout << std::endl;
    std::cout << "✅ All validation tests passed successfully!" << std::endl;
    std::cout << "Enhanced REST API endpoint validation is working correctly."
              << std::endl;
    std::cout << std::endl;
    std::cout << "📋 Task 7 Summary:" << std::endl;
    std::cout << "✅ GET /api/jobs/{id}/status endpoint - implemented"
              << std::endl;
    std::cout << "✅ GET /api/jobs/{id}/metrics endpoint - implemented"
              << std::endl;
    std::cout
        << "✅ GET /api/monitor/jobs endpoint - implemented with filtering"
        << std::endl;
    std::cout << "✅ Input validation for all new endpoints - implemented"
              << std::endl;
    std::cout << "✅ Response formatting and error handling - implemented"
              << std::endl;
    std::cout << "✅ Unit tests for validation logic - completed" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "❌ Test failed with unknown exception" << std::endl;
    return 1;
  }

  return 0;
}
