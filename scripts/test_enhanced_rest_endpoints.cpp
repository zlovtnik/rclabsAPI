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
  MockDatabaseManager() {}
  bool isConnected() const { return true; }
};

class MockAuthManager : public AuthManager {
public:
  MockAuthManager() {}
};

class MockETLJobManager : public ETLJobManager {
public:
  MockETLJobManager() : ETLJobManager(nullptr, nullptr) {
    // Create some mock jobs for testing
    createMockJobs();
  }

  bool isRunning() const { return true; }

private:
  void createMockJobs() {
    // Note: We'll populate the jobs through the protected addJob method
    // if available, or we'll modify the tests to work with the existing
    // interface
  }
};
// Create a completed job
auto job1 = std::make_shared<ETLJob>();
job1->jobId = "job_001";
job1->type = JobType::FULL_ETL;
job1->status = JobStatus::COMPLETED;
job1->createdAt = std::chrono::system_clock::now() - std::chrono::hours(2);
job1->startedAt = std::chrono::system_clock::now() - std::chrono::hours(2) +
                  std::chrono::minutes(5);
job1->completedAt = std::chrono::system_clock::now() - std::chrono::hours(1);
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
job2->createdAt = std::chrono::system_clock::now() - std::chrono::minutes(30);
job2->startedAt = std::chrono::system_clock::now() - std::chrono::minutes(25);
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
job3->completedAt = std::chrono::system_clock::now() - std::chrono::hours(2) -
                    std::chrono::minutes(30);
job3->recordsProcessed = 100;
job3->recordsSuccessful = 80;
job3->recordsFailed = 20;
job3->errorMessage = "Database connection failed";
mockJobs_.push_back(job3);
}
}
;

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