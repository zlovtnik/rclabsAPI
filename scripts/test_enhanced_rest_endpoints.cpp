#include "../include/auth_manager.hpp"
#include "../include/database_manager.hpp"
#include "../include/etl_job_manager.hpp"
#include "../include/input_validator.hpp"
#include "../include/request_handler.hpp"
#include <boost/beast/http.hpp>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>

namespace http = boost::beast::http;

class MockDatabaseManager : public DatabaseManager {
public:
  /**
   * @brief Default constructor for the mock database manager used in tests.
   *
   * Creates a MockDatabaseManager that does not establish any real database
   * connection; isConnected() will always return true to simulate an available
   * database for test scenarios.
   */
  MockDatabaseManager() {}
  /**
   * @brief Always reports the database as connected.
   *
   * This mock implementation unconditionally returns true to simulate an
   * available database connection for tests.
   *
   * @return true Always returns true.
   */
  bool isConnected() const { return true; }
};

class MockAuthManager : public AuthManager {
public:
  /**
   * @brief Default constructor for MockAuthManager.
   *
   * Creates a no-op mock authentication manager used in tests; it does not
   * perform any real authentication setup.
   */
  MockAuthManager() {}
};

class MockETLJobManager : public ETLJobManager {
public:
  /**
   * @brief Constructs a MockETLJobManager for unit tests.
   *
   * Initializes the base ETLJobManager with null dependencies and populates
   * the manager with a predefined set of mock ETL jobs used by the test
   * harness.
   *
   * The created instance reports as running (isRunning() returns true)
   * and provides in-memory mock job data for endpoints exercised by tests.
   */
  MockETLJobManager() : ETLJobManager(nullptr, nullptr) {
    // Create some mock jobs for testing
    createMockJobs();
  }

  /**
   * @brief Indicates whether the ETL job manager is running.
   *
   * In this mock implementation, always returns true to simulate an
   * active/running manager.
   *
   * @return true Always indicates the manager is running.
   */
  bool isRunning() const { return true; }

private:
  std::vector<std::shared_ptr<ETLJob>> mockJobs_;

  /**
   * @brief Populate the mock ETL job collection used by unit tests.
   *
   * This function creates and registers a set of predefined
   * ETLJob instances (e.g., completed, running, failed) into the test
   * manager's internal job store so test cases have deterministic data.
   */
  void createMockJobs() {
    // Create a completed job
    auto job1 = std::make_shared<ETLJob>();
    job1->jobId = "job_001";
    job1->type = JobType::FULL_ETL;
    job1->status = JobStatus::COMPLETED;
    job1->createdAt = std::chrono::system_clock::now() - std::chrono::hours(2);
    job1->startedAt = std::chrono::system_clock::now() - std::chrono::hours(2) +
                      std::chrono::minutes(5);
    job1->completedAt =
        std::chrono::system_clock::now() - std::chrono::hours(1);
    job1->recordsProcessed = 1000;
    job1->recordsSuccessful = 995;
    job1->recordsFailed = 5;
    job1->errorMessage = "";
    mockJobs_.push_back(job1);

    // Create a running job
    auto job2 = std::make_shared<ETLJob>();
    job2->jobId = "job_002";
    job2->type = JobType::EXTRACT;
    job2->status = JobStatus::RUNNING;
    job2->createdAt =
        std::chrono::system_clock::now() - std::chrono::minutes(30);
    job2->startedAt =
        std::chrono::system_clock::now() - std::chrono::minutes(25);
    job2->completedAt = std::chrono::system_clock::time_point{};
    job2->recordsProcessed = 500;
    job2->recordsSuccessful = 500;
    job2->recordsFailed = 0;
    job2->errorMessage = "";
    mockJobs_.push_back(job2);

    // Create a failed job
    auto job3 = std::make_shared<ETLJob>();
    job3->jobId = "job_003";
    job3->type = JobType::LOAD;
    job3->status = JobStatus::FAILED;
    job3->createdAt = std::chrono::system_clock::now() - std::chrono::hours(3);
    job3->startedAt = std::chrono::system_clock::now() - std::chrono::hours(3) +
                      std::chrono::minutes(2);
    job3->completedAt = std::chrono::system_clock::now() -
                        std::chrono::hours(2) - std::chrono::minutes(30);
    job3->recordsProcessed = 100;
    job3->recordsSuccessful = 80;
    job3->recordsFailed = 20;
    job3->errorMessage = "Database connection failed";
    mockJobs_.push_back(job3);
  }
};

/**
 * @brief Unit test for the GET /api/jobs/{id}/status REST endpoint.
 *
 * Runs an integration-style unit test using mock managers (database, auth,
 * ETL), issues an HTTP GET request for job ID "job_001", and verifies the
 * handler returns a 200 OK JSON response containing the expected job fields.
 *
 * The test asserts:
 * - HTTP status is 200 OK.
 * - Content-Type is "application/json".
 * - Response body contains "jobId":"job_001", "status":"completed",
 *   "type":"full_etl", and "recordsProcessed":1000.
 *
 * Side effects:
 * - Writes progress messages to stdout.
 * - Uses assert() for validation; a failing assertion will terminate the test.
 */
void testJobStatusEndpoint() {
  std::cout << "Testing GET /api/jobs/{id}/status endpoint..." << std::endl;

  auto dbManager = std::make_shared<MockDatabaseManager>();
  auto authManager = std::make_shared<MockAuthManager>();
  auto etlManager = std::make_shared<MockETLJobManager>();

  RequestHandler handler(dbManager, authManager, etlManager);

  // Test valid job ID
  http::request<http::string_body> req{http::verb::get,
                                       "/api/jobs/job_001/status", 11};
  req.set(http::field::host, "localhost");
  req.set(http::field::content_type, "application/json");

  auto response = handler.handleRequest(std::move(req));

  assert(response.result() == http::status::ok);
  assert(response[http::field::content_type] == "application/json");

  std::string body = response.body();
  assert(body.find("\"jobId\":\"job_001\"") != std::string::npos);
  assert(body.find("\"status\":\"completed\"") != std::string::npos);
  assert(body.find("\"type\":\"full_etl\"") != std::string::npos);
  assert(body.find("\"recordsProcessed\":1000") != std::string::npos);

  std::cout << "✓ Job status endpoint test passed" << std::endl;
}

/**
 * @brief Unit test for the GET /api/jobs/{id}/metrics endpoint.
 *
 * Verifies that requesting metrics for a known mock job ("job_001") returns
 * HTTP 200 with JSON content and contains expected metric fields and values:
 * - jobId "job_001"
 * - recordsProcessed 1000
 * - recordsSuccessful 995
 * - recordsFailed 5
 * - presence of "processingRate" and "successRate"
 *
 * The test constructs a RequestHandler with mock database, auth, and ETL
 * managers, issues the GET request, and uses assertions to validate the
 * response. Failing assertions indicate a test failure.
 */
void testJobMetricsEndpoint() {
  std::cout << "Testing GET /api/jobs/{id}/metrics endpoint..." << std::endl;

  auto dbManager = std::make_shared<MockDatabaseManager>();
  auto authManager = std::make_shared<MockAuthManager>();
  auto etlManager = std::make_shared<MockETLJobManager>();

  RequestHandler handler(dbManager, authManager, etlManager);

  // Test valid job ID
  http::request<http::string_body> req{http::verb::get,
                                       "/api/jobs/job_001/metrics", 11};
  req.set(http::field::host, "localhost");
  req.set(http::field::content_type, "application/json");

  auto response = handler.handleRequest(std::move(req));

  assert(response.result() == http::status::ok);
  assert(response[http::field::content_type] == "application/json");

  std::string body = response.body();
  assert(body.find("\"jobId\":\"job_001\"") != std::string::npos);
  assert(body.find("\"recordsProcessed\":1000") != std::string::npos);
  assert(body.find("\"recordsSuccessful\":995") != std::string::npos);
  assert(body.find("\"recordsFailed\":5") != std::string::npos);
  assert(body.find("\"processingRate\"") != std::string::npos);
  assert(body.find("\"successRate\"") != std::string::npos);

  std::cout << "✓ Job metrics endpoint test passed" << std::endl;
}

/**
 * @brief Unit test for the /api/monitor/jobs REST endpoint.
 *
 * Exercises the monitor jobs endpoint using mock managers and a RequestHandler.
 * Verifies successful HTTP 200 responses with JSON content and correct
 * filtering and pagination behavior for these scenarios:
 * - no filters (expects total 3 jobs and presence of job_001, job_002, job_003)
 * - status filter (e.g., ?status=completed expects total 1 and only job_001)
 * - type filter (e.g., ?type=extract expects total 1 and job_002)
 * - limit parameter (e.g., ?limit=2 expects total 2)
 *
 * The test uses assertions to validate responses and prints progress to stdout.
 * Failures will trigger assertion failures (process abort). It relies on the
 * global mock job data provided by MockETLJobManager.
 */
void testMonitorJobsEndpoint() {
  std::cout << "Testing GET /api/monitor/jobs endpoint..." << std::endl;

  auto dbManager = std::make_shared<MockDatabaseManager>();
  auto authManager = std::make_shared<MockAuthManager>();
  auto etlManager = std::make_shared<MockETLJobManager>();

  RequestHandler handler(dbManager, authManager, etlManager);

  // Test without filters
  http::request<http::string_body> req1{http::verb::get, "/api/monitor/jobs",
                                        11};
  req1.set(http::field::host, "localhost");
  req1.set(http::field::content_type, "application/json");

  auto response1 = handler.handleRequest(std::move(req1));

  assert(response1.result() == http::status::ok);
  assert(response1[http::field::content_type] == "application/json");

  std::string body1 = response1.body();
  assert(body1.find("\"jobs\":[") != std::string::npos);
  assert(body1.find("\"total\":3") != std::string::npos);
  assert(body1.find("job_001") != std::string::npos);
  assert(body1.find("job_002") != std::string::npos);
  assert(body1.find("job_003") != std::string::npos);

  // Test with status filter
  http::request<http::string_body> req2{
      http::verb::get, "/api/monitor/jobs?status=completed", 11};
  req2.set(http::field::host, "localhost");
  req2.set(http::field::content_type, "application/json");

  auto response2 = handler.handleRequest(std::move(req2));

  assert(response2.result() == http::status::ok);
  std::string body2 = response2.body();
  assert(body2.find("\"total\":1") != std::string::npos);
  assert(body2.find("job_001") != std::string::npos);
  assert(body2.find("job_002") == std::string::npos);

  // Test with type filter
  http::request<http::string_body> req3{http::verb::get,
                                        "/api/monitor/jobs?type=extract", 11};
  req3.set(http::field::host, "localhost");
  req3.set(http::field::content_type, "application/json");

  auto response3 = handler.handleRequest(std::move(req3));

  assert(response3.result() == http::status::ok);
  std::string body3 = response3.body();
  assert(body3.find("\"total\":1") != std::string::npos);
  assert(body3.find("job_002") != std::string::npos);

  // Test with limit
  http::request<http::string_body> req4{http::verb::get,
                                        "/api/monitor/jobs?limit=2", 11};
  req4.set(http::field::host, "localhost");
  req4.set(http::field::content_type, "application/json");

  auto response4 = handler.handleRequest(std::move(req4));

  assert(response4.result() == http::status::ok);
  std::string body4 = response4.body();
  assert(body4.find("\"total\":2") != std::string::npos);

  std::cout << "✓ Monitor jobs endpoint test passed" << std::endl;
}

/**
 * @brief Unit test that verifies the API returns 404 for a non-existent job ID.
 *
 * Constructs mock managers and a RequestHandler, sends a GET request to
 * /api/jobs/nonexistent/status, and asserts the response HTTP status is
 * `404 Not Found`.
 *
 * This test uses MockDatabaseManager, MockAuthManager, and MockETLJobManager
 * fixtures and will terminate the test via assert if the handler does not
 * produce the expected not_found status.
 */
void testInvalidJobId() {
  std::cout << "Testing invalid job ID handling..." << std::endl;

  auto dbManager = std::make_shared<MockDatabaseManager>();
  auto authManager = std::make_shared<MockAuthManager>();
  auto etlManager = std::make_shared<MockETLJobManager>();

  RequestHandler handler(dbManager, authManager, etlManager);

  // Test non-existent job ID
  http::request<http::string_body> req{http::verb::get,
                                       "/api/jobs/nonexistent/status", 11};
  req.set(http::field::host, "localhost");
  req.set(http::field::content_type, "application/json");

  auto response = handler.handleRequest(std::move(req));

  assert(response.result() == http::status::not_found);

  std::cout << "✓ Invalid job ID test passed" << std::endl;
}

/**
 * @brief Runs unit checks for monitoring parameter validation.
 *
 * Executes a set of assertions against InputValidator::validateMonitoringParams
 * to verify accepted and rejected monitoring query parameters used by the
 * monitoring endpoints. Tested cases:
 * - A fully valid parameter set (status, type, limit, from, to) must validate.
 * - An invalid status value must produce a validation failure and an error
 * entry for "status".
 * - A non-numeric limit must fail validation.
 * - A numeric limit outside the allowed range must fail validation.
 *
 * This function prints progress to stdout and uses assert for verifications;
 * failed assertions will terminate the test run.
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
 * @brief Unit test that verifies JSON response formatting for the job status
 * endpoint.
 *
 * Sets up mock managers and a RequestHandler, issues a GET to
 * /api/jobs/job_001/status, and asserts the response body is a JSON object
 * (starts with '{' and ends with '}') and contains the required fields:
 * "jobId", "type", "status", "createdAt", "recordsProcessed", and
 * "executionTimeMs".
 *
 * This test prints progress to stdout and uses assertions; a failing assertion
 * will terminate the test. It assumes the mock ETL manager contains a job with
 * ID "job_001".
 */
void testResponseFormatting() {
  std::cout << "Testing response formatting..." << std::endl;

  auto dbManager = std::make_shared<MockDatabaseManager>();
  auto authManager = std::make_shared<MockAuthManager>();
  auto etlManager = std::make_shared<MockETLJobManager>();

  RequestHandler handler(dbManager, authManager, etlManager);

  // Test job status response format
  http::request<http::string_body> req{http::verb::get,
                                       "/api/jobs/job_001/status", 11};
  req.set(http::field::host, "localhost");
  req.set(http::field::content_type, "application/json");

  auto response = handler.handleRequest(std::move(req));

  std::string body = response.body();

  // Verify JSON structure
  assert(body.find("{") == 0);                 // Starts with {
  assert(body.find("}") == body.length() - 1); // Ends with }

  // Verify required fields are present
  assert(body.find("\"jobId\"") != std::string::npos);
  assert(body.find("\"type\"") != std::string::npos);
  assert(body.find("\"status\"") != std::string::npos);
  assert(body.find("\"createdAt\"") != std::string::npos);
  assert(body.find("\"recordsProcessed\"") != std::string::npos);
  assert(body.find("\"executionTimeMs\"") != std::string::npos);

  std::cout << "✓ Response formatting test passed" << std::endl;
}

/**
 * @brief Runs the test suite for the Enhanced REST API endpoints.
 *
 * Executes all unit tests that validate job status, metrics, monitoring,
 * input validation, and response formatting for the REST API handlers.
 * Prints progress and a summary to stdout/stderr. Catches exceptions and
 * translates test failures into a non-zero exit code.
 *
 * @return int Returns 0 if all tests pass; returns 1 if a test throws an
 * exception.
 */
int main() {
  std::cout << "Running Enhanced REST API Endpoints Tests..." << std::endl;
  std::cout << "=============================================" << std::endl;

  try {
    testJobStatusEndpoint();
    testJobMetricsEndpoint();
    testMonitorJobsEndpoint();
    testInvalidJobId();
    testInputValidation();
    testResponseFormatting();

    std::cout << std::endl;
    std::cout << "✅ All tests passed successfully!" << std::endl;
    std::cout << "Enhanced REST API endpoints are working correctly."
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "❌ Test failed with unknown exception" << std::endl;
    return 1;
  }

  return 0;
}