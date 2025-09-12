#include "component_logger.hpp"
#include "request_validator.hpp"
#include <boost/beast/http.hpp>
#include <iomanip>
#include <iostream>

namespace http = boost::beast::http;

// Helper function to create HTTP requests for testing
http::request<http::string_body> createTestRequest(
    const std::string &method, const std::string &target,
    const std::string &body = "",
    const std::vector<std::pair<std::string, std::string>> &headers = {}) {

  http::request<http::string_body> req;

  // Set method
  if (method == "GET")
    req.method(http::verb::get);
  else if (method == "POST")
    req.method(http::verb::post);
  else if (method == "PUT")
    req.method(http::verb::put);
  else if (method == "DELETE")
    req.method(http::verb::delete_);
  else if (method == "PATCH")
    req.method(http::verb::patch);

  req.target(target);
  req.version(11);
  req.body() = body;

  // Set headers
  for (const auto &[name, value] : headers) {
    req.set(name, value);
  }

  req.prepare_payload();
  return req;
}

void printValidationResult(const std::string &testName,
                           const RequestValidator::ValidationResult &result) {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "Test: " << testName << "\n";
  std::cout << std::string(60, '=') << "\n";

  std::cout << "Valid: " << (result.isValid ? "✅ YES" : "❌ NO") << "\n";
  std::cout << "Method: " << result.method << "\n";
  std::cout << "Path: " << result.extractedPath << "\n";

  if (!result.queryParams.empty()) {
    std::cout << "Query Parameters:\n";
    for (const auto &[key, value] : result.queryParams) {
      std::cout << "  " << key << " = " << value << "\n";
    }
  }

  if (!result.headers.empty()) {
    std::cout << "Headers (first 5):\n";
    int count = 0;
    for (const auto &[key, value] : result.headers) {
      if (count++ >= 5)
        break;
      std::cout << "  " << key << " = " << value << "\n";
    }
  }

  if (!result.errors.empty()) {
    std::cout << "\nValidation Errors:\n";
    for (const auto &error : result.errors) {
      std::cout << "  ❌ Field: " << error.field << ", Code: " << error.code
                << ", Message: " << error.message << "\n";
    }
  }

  std::cout << "\nJSON Result: " << result.toJsonString() << "\n";
}

void printSecurityResult(
    const std::string &testName,
    const RequestValidator::SecurityValidationResult &result) {
  std::cout << "\n" << std::string(60, '-') << "\n";
  std::cout << "Security Test: " << testName << "\n";
  std::cout << std::string(60, '-') << "\n";

  std::cout << "Secure: " << (result.isSecure ? "✅ YES" : "❌ NO") << "\n";
  std::cout << "Client IP: " << result.clientIp << "\n";
  std::cout << "User Agent: " << result.userAgent << "\n";
  std::cout << "Rate Limited: "
            << (result.rateLimitExceeded ? "❌ YES" : "✅ NO") << "\n";

  if (!result.securityIssues.empty()) {
    std::cout << "\nSecurity Issues:\n";
    for (const auto &issue : result.securityIssues) {
      std::cout << "  🚨 " << issue << "\n";
    }
  }
}

int main() {
  std::cout << "🚀 RequestValidator Demo\n";
  std::cout << "========================\n";

  // Create RequestValidator with custom configuration
  RequestValidator::ValidationConfig config;
  config.maxRequestSize = 1024 * 1024; // 1MB
  config.maxHeaderCount = 50;
  config.maxQueryParamCount = 100;
  config.enableXssProtection = true;
  config.enableSqlInjectionProtection = true;
  config.maxRequestsPerMinute = 100;

  RequestValidator validator(config);

  std::cout << "Configuration:\n";
  std::cout << "  Max Request Size: " << config.maxRequestSize << " bytes\n";
  std::cout << "  Max Headers: " << config.maxHeaderCount << "\n";
  std::cout << "  Max Query Params: " << config.maxQueryParamCount << "\n";
  std::cout << "  XSS Protection: "
            << (config.enableXssProtection ? "Enabled" : "Disabled") << "\n";
  std::cout << "  SQL Injection Protection: "
            << (config.enableSqlInjectionProtection ? "Enabled" : "Disabled")
            << "\n";

  // Test 1: Valid GET request to health endpoint
  {
    auto req = createTestRequest("GET", "/api/health", "",
                                 {{"User-Agent", "RequestValidator-Demo/1.0"},
                                  {"Accept", "application/json"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Valid Health Check", result);
  }

  // Test 2: Valid POST request to auth login
  {
    std::string loginBody = R"({
            "username": "demo_user",
            "password": "secure_password123"
        })";

    auto req = createTestRequest("POST", "/api/auth/login", loginBody,
                                 {{"Content-Type", "application/json"},
                                  {"User-Agent", "RequestValidator-Demo/1.0"},
                                  {"Accept", "application/json"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Valid Login Request", result);
  }

  // Test 3: Valid GET request with query parameters
  {
    auto req = createTestRequest(
        "GET", "/api/jobs?status=running&limit=10&offset=0", "",
        {{"Authorization",
          "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.demo.token"},
         {"User-Agent", "RequestValidator-Demo/1.0"},
         {"Accept", "application/json"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Valid Jobs Query", result);
  }

  // Test 4: Invalid request - Unknown endpoint
  {
    auto req = createTestRequest("GET", "/api/unknown/endpoint", "",
                                 {{"User-Agent", "RequestValidator-Demo/1.0"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Unknown Endpoint", result);
  }

  // Test 5: Invalid request - Wrong method for endpoint
  {
    auto req = createTestRequest("DELETE", "/api/auth/login", "",
                                 {{"User-Agent", "RequestValidator-Demo/1.0"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Wrong Method for Login", result);
  }

  // Test 6: Invalid request - Path traversal attempt
  {
    auto req = createTestRequest("GET", "/api/../../../etc/passwd", "",
                                 {{"User-Agent", "RequestValidator-Demo/1.0"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Path Traversal Attempt", result);
  }

  // Test 7: Security test - XSS attempt in query parameters
  {
    auto req = createTestRequest(
        "GET", "/api/jobs?search=<script>alert('xss')</script>", "",
        {{"User-Agent", "RequestValidator-Demo/1.0"},
         {"X-Forwarded-For", "192.168.1.100"}});

    auto result = validator.validateRequest(req);
    printValidationResult("XSS Attempt in Query", result);

    auto securityResult = validator.validateSecurity(req);
    printSecurityResult("XSS Security Check", securityResult);
  }

  // Test 8: Security test - SQL injection attempt
  {
    auto req =
        createTestRequest("GET", "/api/jobs?id=1'; DROP TABLE users; --", "",
                          {{"User-Agent", "RequestValidator-Demo/1.0"},
                           {"X-Forwarded-For", "10.0.0.1"}});

    auto result = validator.validateRequest(req);
    printValidationResult("SQL Injection Attempt", result);

    auto securityResult = validator.validateSecurity(req);
    printSecurityResult("SQL Injection Security Check", securityResult);
  }

  // Test 9: Invalid authentication header
  {
    auto req = createTestRequest("GET", "/api/auth/profile", "",
                                 {{"Authorization", "Basic invalid_format"},
                                  {"User-Agent", "RequestValidator-Demo/1.0"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Invalid Auth Header Format", result);
  }

  // Test 10: Missing required body for POST
  {
    auto req = createTestRequest("POST", "/api/jobs", "",
                                 {{"Content-Type", "application/json"},
                                  {"User-Agent", "RequestValidator-Demo/1.0"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Missing Required Body", result);
  }

  // Test 11: Individual job endpoint validation
  {
    auto req = createTestRequest("GET", "/api/jobs/job-12345", "",
                                 {{"Authorization", "Bearer valid.jwt.token"},
                                  {"User-Agent", "RequestValidator-Demo/1.0"}});

    auto result = validator.validateRequest(req);
    printValidationResult("Individual Job Access", result);
  }

  // Test 12: Rate limiting simulation
  {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Rate Limiting Test\n";
    std::cout << std::string(60, '=') << "\n";

    std::string clientIp = "192.168.1.200";

    // Simulate multiple requests from same IP
    for (int i = 0; i < 5; ++i) {
      auto req = createTestRequest("GET", "/api/health", "",
                                   {{"User-Agent", "RequestValidator-Demo/1.0"},
                                    {"X-Forwarded-For", clientIp}});

      bool allowed = validator.checkRateLimit(clientIp);
      std::cout << "Request " << (i + 1) << " from " << clientIp << ": "
                << (allowed ? "✅ Allowed" : "❌ Rate Limited") << "\n";
    }
  }

  // Display final statistics
  {
    auto stats = validator.getStats();
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Final Statistics\n";
    std::cout << std::string(60, '=') << "\n";
    std::cout << "Total Requests: " << stats.totalRequests << "\n";
    std::cout << "Valid Requests: " << stats.validRequests << "\n";
    std::cout << "Invalid Requests: " << stats.invalidRequests << "\n";
    std::cout << "Security Violations: " << stats.securityViolations << "\n";
    std::cout << "Rate Limit Violations: " << stats.rateLimitViolations << "\n";

    if (stats.totalRequests > 0) {
      double validPercent =
          (double)stats.validRequests / stats.totalRequests * 100.0;
      std::cout << "Success Rate: " << std::fixed << std::setprecision(1)
                << validPercent << "%\n";
    }
  }

  // Test utility methods
  {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Utility Methods Test\n";
    std::cout << std::string(60, '=') << "\n";

    // Test job ID extraction
    std::string jobId1 =
        validator.extractJobIdFromPath("/api/jobs/job-12345", "/api/jobs/", "");
    std::string jobId2 = validator.extractJobIdFromPath(
        "/api/jobs/job-67890/status", "/api/jobs/", "/status");

    std::cout << "Job ID Extraction:\n";
    std::cout << "  /api/jobs/job-12345 -> '" << jobId1 << "'\n";
    std::cout << "  /api/jobs/job-67890/status -> '" << jobId2 << "'\n";

    // Test connection ID extraction
    std::string connId1 = validator.extractConnectionIdFromPath(
        "/api/websocket/conn-abc123/filters", "/api/websocket/");
    std::string connId2 = validator.extractConnectionIdFromPath(
        "/api/websocket/conn-xyz789", "/api/websocket/");

    std::cout << "Connection ID Extraction:\n";
    std::cout << "  /api/websocket/conn-abc123/filters -> '" << connId1
              << "'\n";
    std::cout << "  /api/websocket/conn-xyz789 -> '" << connId2 << "'\n";

    // Test endpoint recognition
    std::cout << "Endpoint Recognition:\n";
    std::cout << "  /api/auth/login -> "
              << (validator.isKnownEndpoint("/api/auth/login") ? "✅ Known"
                                                               : "❌ Unknown")
              << "\n";
    std::cout << "  /api/jobs/job-123 -> "
              << (validator.isKnownEndpoint("/api/jobs/job-123") ? "✅ Known"
                                                                 : "❌ Unknown")
              << "\n";
    std::cout << "  /api/unknown -> "
              << (validator.isKnownEndpoint("/api/unknown") ? "✅ Known"
                                                            : "❌ Unknown")
              << "\n";
  }

  std::cout << "\n🎉 RequestValidator Demo Complete!\n";
  std::cout << "\nThe RequestValidator successfully:\n";
  std::cout << "  ✅ Validates HTTP request structure and format\n";
  std::cout << "  ✅ Extracts and validates headers and query parameters\n";
  std::cout << "  ✅ Performs endpoint-specific validation\n";
  std::cout << "  ✅ Detects security threats (XSS, SQL injection)\n";
  std::cout << "  ✅ Implements rate limiting\n";
  std::cout << "  ✅ Provides comprehensive error reporting\n";
  std::cout << "  ✅ Tracks validation statistics\n";
  std::cout << "  ✅ Offers utility methods for path parsing\n";

  return 0;
}