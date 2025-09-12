#include "etl_exceptions.hpp"
#include "response_builder.hpp"
#include <boost/beast/http.hpp>
#include <gtest/gtest.h>

namespace http = boost::beast::http;

class ResponseBuilderTest : public ::testing::Test {
protected:
  void SetUp() override {
    ResponseBuilder::ResponseConfig config;
    config.serverName = "Test Server";
    config.includeTimestamp = false; // Disable for predictable tests
    config.includeRequestId = false;

    builder_ = std::make_unique<ResponseBuilder>(config);
  }

  std::unique_ptr<ResponseBuilder> builder_;
};

// Basic Response Building Tests
TEST_F(ResponseBuilderTest, CreateSuccessResponse) {
  auto response = builder_->success(R"({"message":"test"})");

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "application/json");
  EXPECT_EQ(response[http::field::server], "Test Server");
  EXPECT_EQ(response.body(), R"({"message":"test"})");
}

TEST_F(ResponseBuilderTest, CreateSuccessResponseWithContentType) {
  auto response =
      builder_->success("Hello World", ResponseBuilder::ContentType::TEXT);

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "text/plain; charset=utf-8");
  EXPECT_EQ(response.body(), "Hello World");
}

TEST_F(ResponseBuilderTest, CreateSuccessJsonResponse) {
  auto response = builder_->successJson(R"({"data":"value"})");

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "application/json");
  EXPECT_EQ(response.body(), R"({"data":"value"})");
}

TEST_F(ResponseBuilderTest, CreateSuccessWithMessage) {
  auto response =
      builder_->successWithMessage("Operation completed", R"({"id":123})");

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "application/json");

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("status":"success")") != std::string::npos);
  EXPECT_TRUE(body.find(R"("message":"Operation completed")") !=
              std::string::npos);
  EXPECT_TRUE(body.find(R"("data":{"id":123})") != std::string::npos);
}

// Error Response Tests
TEST_F(ResponseBuilderTest, CreateErrorResponse) {
  auto response = builder_->error(http::status::bad_request, "Invalid input");

  EXPECT_EQ(response.result(), http::status::bad_request);
  EXPECT_EQ(response[http::field::content_type], "application/json");

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("status":"error")") != std::string::npos);
  EXPECT_TRUE(body.find(R"("error":"Invalid input")") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateBadRequestResponse) {
  auto response = builder_->badRequest("Missing required field");

  EXPECT_EQ(response.result(), http::status::bad_request);

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("error":"Missing required field")") !=
              std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateUnauthorizedResponse) {
  auto response = builder_->unauthorized("Invalid token");

  EXPECT_EQ(response.result(), http::status::unauthorized);

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("error":"Invalid token")") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateForbiddenResponse) {
  auto response = builder_->forbidden("Access denied");

  EXPECT_EQ(response.result(), http::status::forbidden);

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("error":"Access denied")") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateNotFoundResponse) {
  auto response = builder_->notFound("User");

  EXPECT_EQ(response.result(), http::status::not_found);

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("error":"User not found")") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateMethodNotAllowedResponse) {
  auto response = builder_->methodNotAllowed("DELETE", "/api/users");

  EXPECT_EQ(response.result(), http::status::method_not_allowed);
  EXPECT_EQ(response[http::field::allow], "GET, POST, PUT, DELETE, OPTIONS");

  std::string body = response.body();
  EXPECT_TRUE(body.find("Method DELETE not allowed") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateTooManyRequestsResponse) {
  auto response = builder_->tooManyRequests("Rate limit exceeded");

  EXPECT_EQ(response.result(), http::status::too_many_requests);
  EXPECT_EQ(response[http::field::retry_after], "60");

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("error":"Rate limit exceeded")") !=
              std::string::npos);
}

// Fluent Interface Tests
TEST_F(ResponseBuilderTest, FluentInterfaceSetStatus) {
  auto response =
      builder_->setStatus(http::status::created).success(R"({"id":123})");

  EXPECT_EQ(response.result(), http::status::created);
  EXPECT_EQ(response.body(), R"({"id":123})");
}

TEST_F(ResponseBuilderTest, FluentInterfaceSetHeaders) {
  auto response = builder_->setHeader("x-custom-header", "custom-value")
                      .setHeader("x-another-header", "another-value")
                      .success("test");

  EXPECT_EQ(response["x-custom-header"], "custom-value");
  EXPECT_EQ(response["x-another-header"], "another-value");
}

TEST_F(ResponseBuilderTest, FluentInterfaceSetContentType) {
  auto response = builder_->setContentType(ResponseBuilder::ContentType::XML)
                      .success("<root>test</root>");

  EXPECT_EQ(response[http::field::content_type], "application/xml");
  EXPECT_EQ(response.body(), "<root>test</root>");
}

TEST_F(ResponseBuilderTest, FluentInterfaceSetKeepAlive) {
  auto response = builder_->setKeepAlive(true).success("test");

  EXPECT_TRUE(response.keep_alive());
}

// Exception Response Tests
TEST_F(ResponseBuilderTest, CreateResponseFromETLException) {
  etl::ValidationException ex(etl::ErrorCode::INVALID_INPUT,
                              "Invalid field value", "field1", "invalid");

  auto response = builder_->fromException(ex);

  EXPECT_EQ(response.result(), http::status::bad_request);
  EXPECT_EQ(response[http::field::content_type], "application/json");

  std::string body = response.body();
  EXPECT_TRUE(body.find("Invalid field value") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateResponseFromValidationResult) {
  InputValidator::ValidationResult result;
  result.addError("username", "Username is required", "MISSING_FIELD");
  result.addError("email", "Invalid email format", "INVALID_FORMAT");

  auto response = builder_->fromValidationResult(result);

  EXPECT_EQ(response.result(), http::status::bad_request);

  std::string body = response.body();
  EXPECT_TRUE(body.find("Validation failed") != std::string::npos);
  EXPECT_TRUE(body.find("username") != std::string::npos);
  EXPECT_TRUE(body.find("email") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateResponseFromStandardException) {
  std::runtime_error ex("Database connection failed");

  auto response = builder_->fromStandardException(ex, "Database operation");

  EXPECT_EQ(response.result(), http::status::internal_server_error);

  std::string body = response.body();
  EXPECT_TRUE(body.find("Database operation: Database connection failed") !=
              std::string::npos);
}

// Specialized Response Tests
TEST_F(ResponseBuilderTest, CreateAuthenticationRequiredResponse) {
  auto response = builder_->authenticationRequired("ETL API");

  EXPECT_EQ(response.result(), http::status::unauthorized);
  EXPECT_EQ(response[http::field::www_authenticate],
            "Bearer realm=\"ETL API\"");

  std::string body = response.body();
  EXPECT_TRUE(body.find("Authentication required") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateCorsPreflightResponse) {
  auto response = builder_->corsPreflightResponse();

  EXPECT_EQ(response.result(), http::status::no_content);
  EXPECT_EQ(response["access-control-allow-origin"], "*");
  EXPECT_TRUE(response.body().empty());
}

TEST_F(ResponseBuilderTest, CreateHealthCheckResponse) {
  auto response = builder_->healthCheck(true, "All systems operational");

  EXPECT_EQ(response.result(), http::status::ok);

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("status":"healthy")") != std::string::npos);
  EXPECT_TRUE(body.find("All systems operational") != std::string::npos);
}

TEST_F(ResponseBuilderTest, CreateUnhealthyHealthCheckResponse) {
  auto response = builder_->healthCheck(false, "Database unavailable");

  EXPECT_EQ(response.result(), http::status::service_unavailable);

  std::string body = response.body();
  EXPECT_TRUE(body.find(R"("status":"unhealthy")") != std::string::npos);
  EXPECT_TRUE(body.find("Database unavailable") != std::string::npos);
}

// Redirect Response Tests
TEST_F(ResponseBuilderTest, CreateRedirectResponse) {
  auto response = builder_->redirect("https://example.com/new-location");

  EXPECT_EQ(response.result(), http::status::found);
  EXPECT_EQ(response[http::field::location],
            "https://example.com/new-location");
}

TEST_F(ResponseBuilderTest, CreatePermanentRedirectResponse) {
  auto response = builder_->permanentRedirect("https://example.com/permanent");

  EXPECT_EQ(response.result(), http::status::moved_permanently);
  EXPECT_EQ(response[http::field::location], "https://example.com/permanent");
}

TEST_F(ResponseBuilderTest, CreateTemporaryRedirectResponse) {
  auto response = builder_->temporaryRedirect("https://example.com/temporary");

  EXPECT_EQ(response.result(), http::status::temporary_redirect);
  EXPECT_EQ(response[http::field::location], "https://example.com/temporary");
}

// Caching Response Tests
TEST_F(ResponseBuilderTest, CreateCachedResponse) {
  auto response =
      builder_->cached("cached content", std::chrono::seconds(3600));

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::cache_control], "public, max-age=3600");
  EXPECT_EQ(response.body(), "cached content");
}

TEST_F(ResponseBuilderTest, CreateNoCacheResponse) {
  auto response = builder_->noCache("dynamic content");

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::cache_control],
            "no-cache, no-store, must-revalidate");
  EXPECT_EQ(response[http::field::pragma], "no-cache");
  EXPECT_EQ(response.body(), "dynamic content");
}

// CORS Tests
TEST_F(ResponseBuilderTest, CorsHeadersApplied) {
  ResponseBuilder::CorsConfig corsConfig;
  corsConfig.allowOrigin = "https://example.com";
  corsConfig.allowMethods = "GET, POST";
  corsConfig.allowCredentials = true;

  auto response = builder_->setCors(corsConfig).success("test");

  EXPECT_EQ(response["access-control-allow-origin"], "https://example.com");
  EXPECT_EQ(response["access-control-allow-methods"], "GET, POST");
  EXPECT_EQ(response["access-control-allow-credentials"], "true");
}

// Security Headers Tests
TEST_F(ResponseBuilderTest, SecurityHeadersApplied) {
  auto response = builder_->success("test");

  EXPECT_EQ(response["x-content-type-options"], "nosniff");
  EXPECT_EQ(response["x-frame-options"], "DENY");
  EXPECT_EQ(response["x-xss-protection"], "1; mode=block");
  EXPECT_EQ(response["referrer-policy"], "strict-origin-when-cross-origin");
}

// Utility Method Tests
TEST_F(ResponseBuilderTest, ContentTypeToString) {
  EXPECT_EQ(
      ResponseBuilder::contentTypeToString(ResponseBuilder::ContentType::JSON),
      "application/json");
  EXPECT_EQ(
      ResponseBuilder::contentTypeToString(ResponseBuilder::ContentType::XML),
      "application/xml");
  EXPECT_EQ(
      ResponseBuilder::contentTypeToString(ResponseBuilder::ContentType::HTML),
      "text/html; charset=utf-8");
  EXPECT_EQ(
      ResponseBuilder::contentTypeToString(ResponseBuilder::ContentType::TEXT),
      "text/plain; charset=utf-8");
}

TEST_F(ResponseBuilderTest, StringToContentType) {
  EXPECT_EQ(ResponseBuilder::stringToContentType("application/json"),
            ResponseBuilder::ContentType::JSON);
  EXPECT_EQ(ResponseBuilder::stringToContentType("application/xml"),
            ResponseBuilder::ContentType::XML);
  EXPECT_EQ(ResponseBuilder::stringToContentType("text/html"),
            ResponseBuilder::ContentType::HTML);
  EXPECT_EQ(ResponseBuilder::stringToContentType("text/plain"),
            ResponseBuilder::ContentType::TEXT);
}

TEST_F(ResponseBuilderTest, EscapeJsonString) {
  EXPECT_EQ(ResponseBuilder::escapeJsonString("Hello \"World\""),
            "Hello \\\"World\\\"");
  EXPECT_EQ(ResponseBuilder::escapeJsonString("Line 1\nLine 2"),
            "Line 1\\nLine 2");
  EXPECT_EQ(ResponseBuilder::escapeJsonString("Tab\tSeparated"),
            "Tab\\tSeparated");
}

TEST_F(ResponseBuilderTest, StatusToReasonPhrase) {
  EXPECT_EQ(ResponseBuilder::statusToReasonPhrase(http::status::ok), "OK");
  EXPECT_EQ(ResponseBuilder::statusToReasonPhrase(http::status::bad_request),
            "Bad Request");
  EXPECT_EQ(ResponseBuilder::statusToReasonPhrase(http::status::not_found),
            "Not Found");
  EXPECT_EQ(ResponseBuilder::statusToReasonPhrase(
                http::status::internal_server_error),
            "Internal Server Error");
}

TEST_F(ResponseBuilderTest, GenerateRequestId) {
  std::string id1 = ResponseBuilder::generateRequestId();
  std::string id2 = ResponseBuilder::generateRequestId();

  EXPECT_NE(id1, id2);
  EXPECT_EQ(id1.length(), 36); // UUID format: 8-4-4-4-12 + 4 hyphens
  EXPECT_EQ(id1[8], '-');
  EXPECT_EQ(id1[13], '-');
  EXPECT_EQ(id1[18], '-');
  EXPECT_EQ(id1[23], '-');
}

// Configuration Tests
TEST_F(ResponseBuilderTest, UpdateConfiguration) {
  ResponseBuilder::ResponseConfig newConfig;
  newConfig.serverName = "Updated Server";
  newConfig.enableCors = false;
  newConfig.defaultContentType = ResponseBuilder::ContentType::XML;

  builder_->updateConfig(newConfig);

  auto response = builder_->success("<test>data</test>");

  EXPECT_EQ(response[http::field::server], "Updated Server");
  EXPECT_EQ(response[http::field::content_type], "application/xml");
  // CORS headers should not be present
  EXPECT_TRUE(response["access-control-allow-origin"].empty());
}

// Statistics Tests
TEST_F(ResponseBuilderTest, TrackResponseStatistics) {
  auto initialStats = builder_->getStats();
  EXPECT_EQ(initialStats.totalResponses, 0);
  EXPECT_EQ(initialStats.successResponses, 0);
  EXPECT_EQ(initialStats.errorResponses, 0);

  // Create success response
  builder_->success("test");

  // Create error response
  builder_->badRequest("error");

  auto finalStats = builder_->getStats();
  EXPECT_EQ(finalStats.totalResponses, 2);
  EXPECT_EQ(finalStats.successResponses, 1);
  EXPECT_EQ(finalStats.errorResponses, 1);
  EXPECT_GT(finalStats.totalBytes, 0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}