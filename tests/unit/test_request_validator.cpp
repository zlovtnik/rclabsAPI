#include "request_validator.hpp"
#include <boost/beast/http.hpp>
#include <gtest/gtest.h>

namespace http = boost::beast::http;

class RequestValidatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    RequestValidator::ValidationConfig config;
    config.maxRequestSize = 1024;
    config.maxHeaderCount = 10;
    config.maxQueryParamCount = 20;
    config.enableXssProtection = true;
    config.enableSqlInjectionProtection = true;

    validator_ = std::make_unique<RequestValidator>(config);
  }

  http::request<http::string_body> createRequest(const std::string &method,
                                                 const std::string &target,
                                                 const std::string &body = "") {

    http::request<http::string_body> req;

    if (method == "GET")
      req.method(http::verb::get);
    else if (method == "POST")
      req.method(http::verb::post);
    else if (method == "PUT")
      req.method(http::verb::put);
    else if (method == "DELETE")
      req.method(http::verb::delete_);

    req.target(target);
    req.version(11);
    req.body() = body;
    req.prepare_payload();

    return req;
  }

  std::unique_ptr<RequestValidator> validator_;
};

// Basic Request Validation Tests
TEST_F(RequestValidatorTest, ValidateBasicGetRequest) {
  auto req = createRequest("GET", "/api/health");
  auto result = validator_->validateRequestBasics(req);

  EXPECT_TRUE(result.isValid);
  EXPECT_TRUE(result.errors.empty());
  EXPECT_EQ(result.method, "GET");
  EXPECT_EQ(result.extractedPath, "/api/health");
}

TEST_F(RequestValidatorTest, ValidateBasicPostRequest) {
  auto req = createRequest("POST", "/api/auth/login",
                           R"({"username":"test","password":"pass"})");
  req.set(http::field::content_type, "application/json");

  auto result = validator_->validateRequestBasics(req);

  EXPECT_TRUE(result.isValid);
  EXPECT_EQ(result.method, "POST");
  EXPECT_EQ(result.extractedPath, "/api/auth/login");
}

TEST_F(RequestValidatorTest, RejectEmptyPath) {
  auto req = createRequest("GET", "");
  auto result = validator_->validateRequestBasics(req);

  EXPECT_FALSE(result.isValid);
  EXPECT_FALSE(result.errors.empty());

  bool foundPathError = false;
  for (const auto &error : result.errors) {
    if (error.field == "path" && error.code == "MISSING_PATH") {
      foundPathError = true;
      break;
    }
  }
  EXPECT_TRUE(foundPathError);
}

TEST_F(RequestValidatorTest, RejectInvalidPathFormat) {
  auto req = createRequest("GET", "invalid-path-without-slash");
  auto result = validator_->validateRequestBasics(req);

  EXPECT_FALSE(result.isValid);

  bool foundPathError = false;
  for (const auto &error : result.errors) {
    if (error.field == "path" && error.code == "INVALID_PATH_FORMAT") {
      foundPathError = true;
      break;
    }
  }
  EXPECT_TRUE(foundPathError);
}

TEST_F(RequestValidatorTest, RejectPathTraversal) {
  auto req = createRequest("GET", "/api/../../../etc/passwd");
  auto result = validator_->validateRequestBasics(req);

  EXPECT_FALSE(result.isValid);

  bool foundPathError = false;
  for (const auto &error : result.errors) {
    if (error.field == "path" && error.code == "PATH_TRAVERSAL") {
      foundPathError = true;
      break;
    }
  }
  EXPECT_TRUE(foundPathError);
}

// Header Validation Tests
TEST_F(RequestValidatorTest, ExtractHeaders) {
  auto req = createRequest("GET", "/api/health");
  req.set(http::field::authorization, "Bearer token123");
  req.set(http::field::content_type, "application/json");
  req.set(http::field::user_agent, "TestAgent/1.0");

  auto headers = validator_->extractHeaders(req);

  EXPECT_EQ(headers["authorization"], "Bearer token123");
  EXPECT_EQ(headers["content-type"], "application/json");
  EXPECT_EQ(headers["user-agent"], "TestAgent/1.0");
}

TEST_F(RequestValidatorTest, ValidateHeaderCount) {
  auto req = createRequest("GET", "/api/health");

  // Add more headers than allowed (config has maxHeaderCount = 10)
  for (int i = 0; i < 15; ++i) {
    req.set("x-custom-header-" + std::to_string(i),
            "value" + std::to_string(i));
  }

  auto result = validator_->validateHeaders(req);

  EXPECT_FALSE(result.isValid);

  bool foundHeaderError = false;
  for (const auto &error : result.errors) {
    if (error.field == "headers" && error.code == "TOO_MANY_HEADERS") {
      foundHeaderError = true;
      break;
    }
  }
  EXPECT_TRUE(foundHeaderError);
}

// Query Parameter Tests
TEST_F(RequestValidatorTest, ExtractQueryParameters) {
  auto req = createRequest("GET", "/api/jobs?status=running&limit=10&offset=0");

  auto params = validator_->extractQueryParams(req.target());

  EXPECT_EQ(params["status"], "running");
  EXPECT_EQ(params["limit"], "10");
  EXPECT_EQ(params["offset"], "0");
}

TEST_F(RequestValidatorTest, ValidateQueryParametersFormat) {
  auto req =
      createRequest("GET", "/api/jobs?invalid_param_without_value&valid=value");

  auto result = validator_->validateQueryParameters(req.target());

  EXPECT_FALSE(result.isValid);

  bool foundParamError = false;
  for (const auto &error : result.errors) {
    if (error.field == "query" && error.code == "INVALID_PARAM_FORMAT") {
      foundParamError = true;
      break;
    }
  }
  EXPECT_TRUE(foundParamError);
}

// Security Validation Tests
TEST_F(RequestValidatorTest, DetectSqlInjection) {
  EXPECT_TRUE(validator_->checkForSqlInjection("'; DROP TABLE users; --"));
  EXPECT_TRUE(validator_->checkForSqlInjection("1' OR '1'='1"));
  EXPECT_TRUE(
      validator_->checkForSqlInjection("UNION SELECT * FROM passwords"));
  EXPECT_FALSE(validator_->checkForSqlInjection("normal search term"));
}

TEST_F(RequestValidatorTest, DetectXssAttempts) {
  EXPECT_TRUE(validator_->checkForXssAttempts("<script>alert('xss')</script>"));
  EXPECT_TRUE(validator_->checkForXssAttempts("javascript:alert(1)"));
  EXPECT_TRUE(validator_->checkForXssAttempts("onload=alert(1)"));
  EXPECT_FALSE(validator_->checkForXssAttempts("normal text content"));
}

TEST_F(RequestValidatorTest, ValidateSecurityInQueryParams) {
  auto req =
      createRequest("GET", "/api/jobs?search=<script>alert('xss')</script>");

  auto result = validator_->validateQueryParameters(req.target());

  EXPECT_FALSE(result.isValid);

  bool foundXssError = false;
  for (const auto &error : result.errors) {
    if (error.field == "query" && error.code == "XSS_ATTEMPT") {
      foundXssError = true;
      break;
    }
  }
  EXPECT_TRUE(foundXssError);
}

// Endpoint-Specific Validation Tests
TEST_F(RequestValidatorTest, ValidateAuthLoginEndpoint) {
  auto req = createRequest("POST", "/api/auth/login",
                           R"({"username":"test","password":"password123"})");
  req.set(http::field::content_type, "application/json");

  auto result = validator_->validateAuthEndpoint(req);

  EXPECT_TRUE(result.isValid);
}

TEST_F(RequestValidatorTest, RejectInvalidMethodForAuthLogin) {
  auto req = createRequest("GET", "/api/auth/login");

  auto result = validator_->validateAuthEndpoint(req);

  EXPECT_FALSE(result.isValid);

  bool foundMethodError = false;
  for (const auto &error : result.errors) {
    if (error.field == "method" && error.code == "INVALID_METHOD") {
      foundMethodError = true;
      break;
    }
  }
  EXPECT_TRUE(foundMethodError);
}

TEST_F(RequestValidatorTest, ValidateJobsEndpointGet) {
  auto req = createRequest("GET", "/api/jobs?status=running&limit=10");

  auto result = validator_->validateJobsEndpoint(req);

  EXPECT_TRUE(result.isValid);
}

TEST_F(RequestValidatorTest, ValidateJobsEndpointPost) {
  auto req = createRequest(
      "POST", "/api/jobs",
      R"({"name":"test-job","type":"FULL_ETL","source_config":"test-source","target_config":"test-target"})");
  req.set(http::field::content_type, "application/json");

  auto result = validator_->validateJobsEndpoint(req);

  EXPECT_TRUE(result.isValid);
}

TEST_F(RequestValidatorTest, ValidateIndividualJobEndpoint) {
  auto req = createRequest("GET", "/api/jobs/job-123");

  auto result = validator_->validateJobsEndpoint(req);

  EXPECT_TRUE(result.isValid);
}

TEST_F(RequestValidatorTest, RejectInvalidJobId) {
  auto req = createRequest("GET", "/api/jobs/");

  auto result = validator_->validateJobsEndpoint(req);

  EXPECT_FALSE(result.isValid);

  bool foundJobIdError = false;
  for (const auto &error : result.errors) {
    if (error.field == "job_id" && error.code == "INVALID_JOB_ID") {
      foundJobIdError = true;
      break;
    }
  }
  EXPECT_TRUE(foundJobIdError);
}

// Authentication Header Tests
TEST_F(RequestValidatorTest, ValidateBearerToken) {
  auto result = validator_->validateAuthenticationHeader(
      "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9");

  EXPECT_TRUE(result.isValid);
}

TEST_F(RequestValidatorTest, RejectEmptyAuthHeader) {
  auto result = validator_->validateAuthenticationHeader("");

  EXPECT_FALSE(result.isValid);

  bool foundAuthError = false;
  for (const auto &error : result.errors) {
    if (error.field == "authorization" && error.code == "EMPTY_AUTH_HEADER") {
      foundAuthError = true;
      break;
    }
  }
  EXPECT_TRUE(foundAuthError);
}

TEST_F(RequestValidatorTest, RejectInvalidAuthFormat) {
  auto result = validator_->validateAuthenticationHeader("Basic dXNlcjpwYXNz");

  EXPECT_FALSE(result.isValid);

  bool foundAuthError = false;
  for (const auto &error : result.errors) {
    if (error.field == "authorization" && error.code == "INVALID_AUTH_FORMAT") {
      foundAuthError = true;
      break;
    }
  }
  EXPECT_TRUE(foundAuthError);
}

// Utility Method Tests
TEST_F(RequestValidatorTest, ExtractJobIdFromPath) {
  EXPECT_EQ(
      validator_->extractJobIdFromPath("/api/jobs/job-123", "/api/jobs/", ""),
      "job-123");
  EXPECT_EQ(validator_->extractJobIdFromPath("/api/jobs/job-123/status",
                                             "/api/jobs/", "/status"),
            "job-123");
  EXPECT_EQ(validator_->extractJobIdFromPath("/api/jobs/", "/api/jobs/", ""),
            "");
  EXPECT_EQ(
      validator_->extractJobIdFromPath("/api/other/job-123", "/api/jobs/", ""),
      "");
}

TEST_F(RequestValidatorTest, ExtractConnectionIdFromPath) {
  EXPECT_EQ(validator_->extractConnectionIdFromPath(
                "/api/websocket/conn-123/filters", "/api/websocket/"),
            "conn-123");
  EXPECT_EQ(validator_->extractConnectionIdFromPath("/api/websocket/conn-456",
                                                    "/api/websocket/"),
            "conn-456");
  EXPECT_EQ(validator_->extractConnectionIdFromPath("/api/websocket/",
                                                    "/api/websocket/"),
            "");
}

TEST_F(RequestValidatorTest, ExtractClientIp) {
  auto req = createRequest("GET", "/api/health");
  req.set("x-forwarded-for", "192.168.1.100, 10.0.0.1");
  req.set("x-real-ip", "192.168.1.200");

  // Should prefer x-forwarded-for and take the first IP
  std::string ip = validator_->extractClientIp(req);
  EXPECT_EQ(ip, "192.168.1.100");
}

TEST_F(RequestValidatorTest, ExtractUserAgent) {
  auto req = createRequest("GET", "/api/health");
  req.set(http::field::user_agent, "Mozilla/5.0 (Test Browser)");

  std::string userAgent = validator_->extractUserAgent(req);
  EXPECT_EQ(userAgent, "Mozilla/5.0 (Test Browser)");
}

// Content Length Validation Tests
TEST_F(RequestValidatorTest, ValidateContentLength) {
  auto result = validator_->validateContentLength(512); // Within limit
  EXPECT_TRUE(result.isValid);

  result = validator_->validateContentLength(2048); // Exceeds limit (1024)
  EXPECT_FALSE(result.isValid);

  bool foundSizeError = false;
  for (const auto &error : result.errors) {
    if (error.field == "content_length" && error.code == "REQUEST_TOO_LARGE") {
      foundSizeError = true;
      break;
    }
  }
  EXPECT_TRUE(foundSizeError);
}

// Known Endpoint Tests
TEST_F(RequestValidatorTest, RecognizeKnownEndpoints) {
  EXPECT_TRUE(validator_->isKnownEndpoint("/api/auth/login"));
  EXPECT_TRUE(validator_->isKnownEndpoint("/api/jobs"));
  EXPECT_TRUE(validator_->isKnownEndpoint("/api/health"));
  EXPECT_TRUE(
      validator_->isKnownEndpoint("/api/jobs/job-123")); // Parameterized
  EXPECT_FALSE(validator_->isKnownEndpoint("/api/unknown"));
}

// Full Request Validation Tests
TEST_F(RequestValidatorTest, ValidateCompleteValidRequest) {
  auto req = createRequest("GET", "/api/jobs?status=running&limit=10");
  req.set(http::field::authorization, "Bearer valid-token-123");
  req.set(http::field::user_agent, "TestClient/1.0");

  auto result = validator_->validateRequest(req);

  EXPECT_TRUE(result.isValid);
  EXPECT_TRUE(result.errors.empty());
  EXPECT_EQ(result.method, "GET");
  EXPECT_EQ(result.extractedPath, "/api/jobs");
  EXPECT_EQ(result.queryParams["status"], "running");
  EXPECT_EQ(result.queryParams["limit"], "10");
}

TEST_F(RequestValidatorTest, ValidateCompleteInvalidRequest) {
  auto req =
      createRequest("GET", "/api/jobs?search=<script>alert('xss')</script>");

  auto result = validator_->validateRequest(req);

  EXPECT_FALSE(result.isValid);
  EXPECT_FALSE(result.errors.empty());

  // Should contain XSS error
  bool foundXssError = false;
  for (const auto &error : result.errors) {
    if (error.code == "XSS_ATTEMPT") {
      foundXssError = true;
      break;
    }
  }
  EXPECT_TRUE(foundXssError);
}

// Statistics Tests
TEST_F(RequestValidatorTest, TrackValidationStatistics) {
  auto initialStats = validator_->getStats();
  EXPECT_EQ(initialStats.totalRequests, 0);
  EXPECT_EQ(initialStats.validRequests, 0);
  EXPECT_EQ(initialStats.invalidRequests, 0);

  // Valid request
  auto validReq = createRequest("GET", "/api/health");
  validator_->validateRequest(validReq);

  // Invalid request
  auto invalidReq = createRequest("GET", "/api/unknown");
  validator_->validateRequest(invalidReq);

  auto finalStats = validator_->getStats();
  EXPECT_EQ(finalStats.totalRequests, 2);
  EXPECT_EQ(finalStats.validRequests, 1);
  EXPECT_EQ(finalStats.invalidRequests, 1);
}

// Configuration Tests
TEST_F(RequestValidatorTest, UpdateConfiguration) {
  RequestValidator::ValidationConfig newConfig;
  newConfig.maxRequestSize = 2048;
  newConfig.maxHeaderCount = 20;
  newConfig.enableXssProtection = false;

  validator_->updateConfig(newConfig);

  const auto &config = validator_->getConfig();
  EXPECT_EQ(config.maxRequestSize, 2048);
  EXPECT_EQ(config.maxHeaderCount, 20);
  EXPECT_FALSE(config.enableXssProtection);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}