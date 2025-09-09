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

        // Create a login request
        http::request<http::string_body> loginReq;
        loginReq.method(http::verb::post);
        loginReq.target("/api/auth/login");
        loginReq.set(http::field::content_type, "application/json");
        loginReq.body() = R"({"username":"admin","password":"password"})";
        loginReq.prepare_payload();

        // Handle the login request
        auto loginResponse = handler.handleRequest(loginReq);

        std::cout << "Login Response Status: " << loginResponse.result_int() << std::endl;
        std::cout << "Login Response Body: " << loginResponse.body() << std::endl;

        // Parse the JWT token from response
        std::string token;
        if (loginResponse.result() == http::status::ok) {
            try {
                auto jsonResponse = nlohmann::json::parse(loginResponse.body());
                if (jsonResponse.contains("token")) {
                    token = jsonResponse["token"];
                    std::cout << "Extracted JWT Token: " << token.substr(0, 50) << "..." << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Failed to parse login response: " << e.what() << std::endl;
            }
        }

        // Test 2: Access protected endpoint with JWT token
        if (!token.empty()) {
            std::cout << "\n=== Test 2: Access Protected Endpoint ===" << std::endl;

            http::request<http::string_body> profileReq;
            profileReq.method(http::verb::get);
            profileReq.target("/api/auth/profile");
            profileReq.set(http::field::authorization, "Bearer " + token);
            profileReq.prepare_payload();

            auto profileResponse = handler.handleRequest(profileReq);

            std::cout << "Profile Response Status: " << profileResponse.result_int() << std::endl;
            std::cout << "Profile Response Body: " << profileResponse.body() << std::endl;
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

        std::cout << "\n=== JWT Authentication Test Complete ===" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
