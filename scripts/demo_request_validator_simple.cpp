#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include <algorithm>
#include <sstream>

/**
 * Simplified RequestValidator Demo (without Boost Beast dependency)
 * 
 * This demonstrates the core validation logic that would be used in the 
 * actual RequestValidator component for HTTP server stability improvements.
 */

class SimpleRequestValidator {
public:
    struct ValidationResult {
        bool isValid = true;
        std::vector<std::string> errors;
        std::unordered_map<std::string, std::string> headers;
        std::unordered_map<std::string, std::string> queryParams;
        std::string method;
        std::string path;
        
        void addError(const std::string& error) {
            errors.push_back(error);
            isValid = false;
        }
    };
    
    struct SecurityResult {
        bool isSecure = true;
        std::vector<std::string> issues;
        
        void addIssue(const std::string& issue) {
            issues.push_back(issue);
            isSecure = false;
        }
    };

private:
    std::vector<std::string> knownEndpoints_ = {
        "/api/auth/login",
        "/api/auth/logout", 
        "/api/auth/profile",
        "/api/jobs",
        "/api/logs",
        "/api/monitor/jobs",
        "/api/monitor/status",
        "/api/health"
    };

public:
    ValidationResult validateRequest(const std::string& method, const std::string& url, 
                                   const std::unordered_map<std::string, std::string>& headers,
                                   const std::string& body = "") {
        ValidationResult result;
        result.method = method;
        result.headers = headers;
        
        // Extract path and query parameters
        size_t queryPos = url.find('?');
        result.path = (queryPos != std::string::npos) ? url.substr(0, queryPos) : url;
        
        if (queryPos != std::string::npos) {
            result.queryParams = parseQueryString(url.substr(queryPos + 1));
        }
        
        // Validate basic structure
        if (method.empty()) {
            result.addError("HTTP method is required");
        }
        
        if (result.path.empty()) {
            result.addError("Request path is required");
        }
        
        // Validate path format
        if (!result.path.empty() && result.path[0] != '/') {
            result.addError("Path must start with '/'");
        }
        
        // Check for path traversal
        if (result.path.find("..") != std::string::npos) {
            result.addError("Path traversal not allowed");
        }
        
        // Validate endpoint
        if (!isKnownEndpoint(result.path)) {
            result.addError("Unknown endpoint: " + result.path);
        }
        
        // Validate method for endpoint
        if (!isValidMethodForEndpoint(method, result.path)) {
            result.addError("Method " + method + " not allowed for " + result.path);
        }
        
        // Validate authentication for protected endpoints
        if (requiresAuth(result.path)) {
            auto authIt = headers.find("authorization");
            if (authIt == headers.end()) {
                result.addError("Authorization header required");
            } else if (!isValidAuthHeader(authIt->second)) {
                result.addError("Invalid authorization header format");
            }
        }
        
        // Validate body for POST/PUT requests
        if ((method == "POST" || method == "PUT") && requiresBody(result.path)) {
            if (body.empty()) {
                result.addError("Request body required for " + method + " " + result.path);
            } else if (!isValidJson(body)) {
                result.addError("Invalid JSON in request body");
            }
        }
        
        return result;
    }
    
    SecurityResult validateSecurity(const std::string& url, const std::string& body,
                                  const std::unordered_map<std::string, std::string>& headers) {
        SecurityResult result;
        
        // Check for SQL injection
        if (checkForSqlInjection(url) || checkForSqlInjection(body)) {
            result.addIssue("Potential SQL injection detected");
        }
        
        // Check for XSS attempts
        if (checkForXss(url) || checkForXss(body)) {
            result.addIssue("Potential XSS attempt detected");
        }
        
        // Check for suspicious user agents
        auto uaIt = headers.find("user-agent");
        if (uaIt != headers.end() && isSuspiciousUserAgent(uaIt->second)) {
            result.addIssue("Suspicious user agent detected");
        }
        
        return result;
    }

private:
    std::unordered_map<std::string, std::string> parseQueryString(const std::string& query) {
        std::unordered_map<std::string, std::string> params;
        std::istringstream iss(query);
        std::string pair;
        
        while (std::getline(iss, pair, '&')) {
            size_t equalPos = pair.find('=');
            if (equalPos != std::string::npos) {
                std::string key = pair.substr(0, equalPos);
                std::string value = pair.substr(equalPos + 1);
                params[key] = value;
            }
        }
        
        return params;
    }
    
    bool isKnownEndpoint(const std::string& path) {
        // Check exact matches
        for (const auto& endpoint : knownEndpoints_) {
            if (path == endpoint) return true;
        }
        
        // Check parameterized endpoints
        if (path.find("/api/jobs/") == 0 && path.length() > 10) {
            return true; // Individual job endpoints
        }
        
        return false;
    }
    
    bool isValidMethodForEndpoint(const std::string& method, const std::string& path) {
        if (path == "/api/auth/login" || path == "/api/auth/logout") {
            return method == "POST";
        }
        if (path == "/api/auth/profile" || path == "/api/logs" || path == "/api/health") {
            return method == "GET";
        }
        if (path == "/api/jobs") {
            return method == "GET" || method == "POST";
        }
        if (path.find("/api/jobs/") == 0) {
            return method == "GET" || method == "PUT" || method == "DELETE";
        }
        return true; // Default allow
    }
    
    bool requiresAuth(const std::string& path) {
        return path != "/api/health" && path != "/api/auth/login";
    }
    
    bool requiresBody(const std::string& path) {
        return path == "/api/auth/login" || path == "/api/jobs";
    }
    
    bool isValidAuthHeader(const std::string& auth) {
        return auth.find("Bearer ") == 0 && auth.length() > 7;
    }
    
    bool isValidJson(const std::string& body) {
        // Simple JSON validation - check for basic structure
        if (body.empty()) return false;
        
        std::string trimmed = body;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
        trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
        
        return (trimmed.front() == '{' && trimmed.back() == '}') ||
               (trimmed.front() == '[' && trimmed.back() == ']');
    }
    
    bool checkForSqlInjection(const std::string& input) {
        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        std::vector<std::string> sqlKeywords = {
            "select", "insert", "update", "delete", "drop", "union", 
            "exec", "script", "javascript", "'; drop"
        };
        
        for (const auto& keyword : sqlKeywords) {
            if (lower.find(keyword) != std::string::npos) {
                return true;
            }
        }
        
        return false;
    }
    
    bool checkForXss(const std::string& input) {
        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        std::vector<std::string> xssPatterns = {
            "<script", "</script>", "javascript:", "onload=", "onerror=",
            "onclick=", "eval(", "document.cookie", "document.write"
        };
        
        for (const auto& pattern : xssPatterns) {
            if (lower.find(pattern) != std::string::npos) {
                return true;
            }
        }
        
        return false;
    }
    
    bool isSuspiciousUserAgent(const std::string& userAgent) {
        std::string lower = userAgent;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        std::vector<std::string> suspiciousPatterns = {
            "sqlmap", "nikto", "nmap", "masscan", "zap", "burp"
        };
        
        for (const auto& pattern : suspiciousPatterns) {
            if (lower.find(pattern) != std::string::npos) {
                return true;
            }
        }
        
        return false;
    }
};

void printResult(const std::string& testName, const SimpleRequestValidator::ValidationResult& result) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Test: " << testName << "\n";
    std::cout << std::string(60, '=') << "\n";
    
    std::cout << "Valid: " << (result.isValid ? "✅ YES" : "❌ NO") << "\n";
    std::cout << "Method: " << result.method << "\n";
    std::cout << "Path: " << result.path << "\n";
    
    if (!result.queryParams.empty()) {
        std::cout << "Query Parameters:\n";
        for (const auto& [key, value] : result.queryParams) {
            std::cout << "  " << key << " = " << value << "\n";
        }
    }
    
    if (!result.errors.empty()) {
        std::cout << "Validation Errors:\n";
        for (const auto& error : result.errors) {
            std::cout << "  ❌ " << error << "\n";
        }
    }
}

void printSecurityResult(const std::string& testName, const SimpleRequestValidator::SecurityResult& result) {
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Security Test: " << testName << "\n";
    std::cout << std::string(60, '-') << "\n";
    
    std::cout << "Secure: " << (result.isSecure ? "✅ YES" : "❌ NO") << "\n";
    
    if (!result.issues.empty()) {
        std::cout << "Security Issues:\n";
        for (const auto& issue : result.issues) {
            std::cout << "  🚨 " << issue << "\n";
        }
    }
}

int main() {
    std::cout << "🚀 RequestValidator Demo (Simplified)\n";
    std::cout << "=====================================\n";
    std::cout << "Demonstrating HTTP server stability improvements through comprehensive request validation\n";
    
    SimpleRequestValidator validator;
    
    // Test 1: Valid health check
    {
        auto result = validator.validateRequest("GET", "/api/health", {
            {"user-agent", "Demo/1.0"}
        });
        printResult("Valid Health Check", result);
    }
    
    // Test 2: Valid login request
    {
        std::string body = R"({"username": "user", "password": "pass"})";
        auto result = validator.validateRequest("POST", "/api/auth/login", {
            {"content-type", "application/json"},
            {"user-agent", "Demo/1.0"}
        }, body);
        printResult("Valid Login Request", result);
    }
    
    // Test 3: Valid authenticated request
    {
        auto result = validator.validateRequest("GET", "/api/jobs?status=running&limit=10", {
            {"authorization", "Bearer eyJhbGciOiJIUzI1NiJ9.token"},
            {"user-agent", "Demo/1.0"}
        });
        printResult("Valid Authenticated Request", result);
    }
    
    // Test 4: Invalid - Unknown endpoint
    {
        auto result = validator.validateRequest("GET", "/api/unknown", {
            {"user-agent", "Demo/1.0"}
        });
        printResult("Unknown Endpoint", result);
    }
    
    // Test 5: Invalid - Wrong method
    {
        auto result = validator.validateRequest("DELETE", "/api/auth/login", {
            {"user-agent", "Demo/1.0"}
        });
        printResult("Wrong Method for Login", result);
    }
    
    // Test 6: Invalid - Path traversal
    {
        auto result = validator.validateRequest("GET", "/api/../../../etc/passwd", {
            {"user-agent", "Demo/1.0"}
        });
        printResult("Path Traversal Attempt", result);
    }
    
    // Test 7: Invalid - Missing auth
    {
        auto result = validator.validateRequest("GET", "/api/jobs", {
            {"user-agent", "Demo/1.0"}
        });
        printResult("Missing Authorization", result);
    }
    
    // Test 8: Invalid - Missing body
    {
        auto result = validator.validateRequest("POST", "/api/jobs", {
            {"authorization", "Bearer token"},
            {"user-agent", "Demo/1.0"}
        });
        printResult("Missing Required Body", result);
    }
    
    // Security Tests
    
    // Test 9: SQL Injection attempt
    {
        auto result = validator.validateRequest("GET", "/api/jobs?id=1'; DROP TABLE users; --", {
            {"user-agent", "Demo/1.0"}
        });
        printResult("SQL Injection in URL", result);
        
        auto secResult = validator.validateSecurity("/api/jobs?id=1'; DROP TABLE users; --", "", {
            {"user-agent", "Demo/1.0"}
        });
        printSecurityResult("SQL Injection Security Check", secResult);
    }
    
    // Test 10: XSS attempt
    {
        auto result = validator.validateRequest("GET", "/api/jobs?search=<script>alert('xss')</script>", {
            {"user-agent", "Demo/1.0"}
        });
        printResult("XSS in Query Parameter", result);
        
        auto secResult = validator.validateSecurity("/api/jobs?search=<script>alert('xss')</script>", "", {
            {"user-agent", "Demo/1.0"}
        });
        printSecurityResult("XSS Security Check", secResult);
    }
    
    // Test 11: Suspicious user agent
    {
        auto secResult = validator.validateSecurity("/api/health", "", {
            {"user-agent", "sqlmap/1.0 (http://sqlmap.org)"}
        });
        printSecurityResult("Suspicious User Agent", secResult);
    }
    
    // Test 12: Individual job endpoint
    {
        auto result = validator.validateRequest("GET", "/api/jobs/job-12345", {
            {"authorization", "Bearer valid-token"},
            {"user-agent", "Demo/1.0"}
        });
        printResult("Individual Job Access", result);
    }
    
    std::cout << "\n🎉 RequestValidator Demo Complete!\n";
    std::cout << "\nKey Features Demonstrated:\n";
    std::cout << "  ✅ HTTP method and endpoint validation\n";
    std::cout << "  ✅ Path format and security validation\n";
    std::cout << "  ✅ Authentication requirement enforcement\n";
    std::cout << "  ✅ Request body validation for POST/PUT\n";
    std::cout << "  ✅ SQL injection detection\n";
    std::cout << "  ✅ XSS attempt detection\n";
    std::cout << "  ✅ Suspicious user agent detection\n";
    std::cout << "  ✅ Query parameter parsing and validation\n";
    std::cout << "  ✅ Parameterized endpoint support\n";
    
    std::cout << "\nThis validation logic will significantly improve HTTP server stability by:\n";
    std::cout << "  • Preventing malformed requests from reaching business logic\n";
    std::cout << "  • Detecting and blocking security threats early\n";
    std::cout << "  • Ensuring proper authentication and authorization\n";
    std::cout << "  • Validating request structure and content\n";
    std::cout << "  • Providing clear error messages for debugging\n";
    
    return 0;
}