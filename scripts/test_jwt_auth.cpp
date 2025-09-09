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
        loginReq.body() = std::string(R"({"username":")") + testUsername + R"(","password":")" + testPassword + R"("})";
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

        // Assert profile response
        if (profileResponse.result() != http::status::ok) {
            std::cerr << "Profile access failed with status: " << profileResponse.result_int() << std::endl;
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
        if (noAuthResponse.result() != http::status::unauthorized && noAuthResponse.result() != http::status::forbidden) {
            std::cerr << "Expected unauthorized/forbidden status, got: " << noAuthResponse.result_int() << std::endl;
            return 1;
        }

        std::cout << "\n=== JWT Authentication Test Complete - All tests passed ===" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
