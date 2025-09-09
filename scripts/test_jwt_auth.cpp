#include "request_handler.hpp"
#include "auth_manager.hpp"
#include "database_manager.hpp"
#include "etl_job_manager.hpp"
#include "logger.hpp"
#include <boost/beast/http.hpp>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

namespace http = boost::beast::http;

int main() {
    try {
        // Initialize components
        auto dbManager = std::make_shared<DatabaseManager>();
        auto authManager = std::make_shared<AuthManager>(dbManager);
        auto etlManager = std::make_shared<ETLJobManager>(dbManager, nullptr);

        // Create request handler
        RequestHandler handler(dbManager, authManager, etlManager);

        // Test 1: Login request
        std::cout << "\n=== Test 1: JWT Login ===" << std::endl;

        // Get test credentials from environment variables
        const char* testUsername = std::getenv("TEST_USERNAME");
        const char* testPassword = std::getenv("TEST_PASSWORD");
        
        if (!testUsername || !testPassword) {
            std::cerr << "Error: TEST_USERNAME and TEST_PASSWORD environment variables must be set" << std::endl;
            return 1;
        }

        // Create a login request
        http::request<http::string_body> loginReq;
        loginReq.method(http::verb::post);
        loginReq.target("/api/auth/login");
        loginReq.set(http::field::content_type, "application/json");
        nlohmann::json loginBody = {
            {"username", std::string(testUsername)},
            {"password", std::string(testPassword)}
        };
        loginReq.body() = loginBody.dump();
        loginReq.prepare_payload();

        // Handle the login request
        auto loginResponse = handler.handleRequest(loginReq);

        // Assert login response
        if (loginResponse.result() != http::status::ok) {
            std::cerr << "Login failed with status: " << loginResponse.result_int() << std::endl;
            return 1;
        }

        // Parse and validate login response
        std::string token;
        try {
            auto jsonResponse = nlohmann::json::parse(loginResponse.body());
            auto ct = loginResponse.base().find(http::field::content_type);
            if (ct == loginResponse.base().end() || ct->value().find("application/json") == boost::beast::string_view::npos) {
                std::cerr << "Login response missing/invalid Content-Type" << std::endl;
                return 1;
            }
            if (!jsonResponse.contains("token") || !jsonResponse["token"].is_string() || jsonResponse["token"].get<std::string>().empty()) {
                std::cerr << "Login response missing or invalid token" << std::endl;
                return 1;
            }
            token = jsonResponse["token"];
            std::cout << "Token extracted successfully (length: " << token.length() << ")" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to parse login response: " << e.what() << std::endl;
            return 1;
        }

        // Test 2: Access protected endpoint with JWT token
        std::cout << "\n=== Test 2: Access Protected Endpoint ===" << std::endl;

        http::request<http::string_body> profileReq;
        profileReq.method(http::verb::get);
        profileReq.target("/api/auth/profile");
        profileReq.set(http::field::authorization, "Bearer " + token);
        profileReq.prepare_payload();

        auto profileResponse = handler.handleRequest(profileReq);

        std::cout << "Profile Response Status: " << profileResponse.result_int() << std::endl;
        std::cout << "Profile Response Body: " << profileResponse.body() << std::endl;

        // Assert profile response + rate-limit headers
        if (profileResponse.result() != http::status::ok) {
            std::cerr << "Profile access failed with status: " << profileResponse.result_int() << std::endl;
            return 1;
        }

        // Check for rate-limit headers (custom headers need to be checked differently)
        bool hasRateLimitLimit = false;
        bool hasRateLimitRemaining = false;
        bool hasRateLimitReset = false;

        for (auto const& field : profileResponse.base()) {
            std::string fieldName = field.name_string();
            if (fieldName == "X-RateLimit-Limit") hasRateLimitLimit = true;
            else if (fieldName == "X-RateLimit-Remaining") hasRateLimitRemaining = true;
            else if (fieldName == "X-RateLimit-Reset") hasRateLimitReset = true;
        }

        if (!hasRateLimitLimit || !hasRateLimitRemaining || !hasRateLimitReset) {
            std::cerr << "Missing expected rate-limit headers" << std::endl;
            return 1;
        }

        // Test 3: Access protected endpoint without token
        std::cout << "\n=== Test 3: Access Protected Endpoint Without Token ===" << std::endl;

        http::request<http::string_body> noAuthReq;
        noAuthReq.method(http::verb::get);
        noAuthReq.target("/api/auth/profile");
        noAuthReq.prepare_payload();

        auto noAuthResponse = handler.handleRequest(noAuthReq);

        std::cout << "No Auth Response Status: " << noAuthResponse.result_int() << std::endl;
        std::cout << "No Auth Response Body: " << noAuthResponse.body() << std::endl;

        // Assert unauthorized access
        if (noAuthResponse.result() != http::status::unauthorized) {
            std::cerr << "Expected unauthorized status, got: " << noAuthResponse.result_int() << std::endl;
            return 1;
        }

        // Test 4: Tampered token should be rejected
        std::cout << "\n=== Test 4: Access With Tampered Token ===" << std::endl;
        http::request<http::string_body> badTokReq;
        badTokReq.method(http::verb::get);
        badTokReq.target("/api/auth/profile");
        badTokReq.set(http::field::authorization, "Bearer " + token + "x");
        badTokReq.prepare_payload();
        auto badTokResp = handler.handleRequest(badTokReq);
        if (badTokResp.result() != http::status::unauthorized) {
            std::cerr << "Expected 401 for tampered token, got: " << badTokResp.result_int() << std::endl;
            return 1;
        }

        std::cout << "\n=== JWT Authentication Test Complete - All tests passed ===" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
