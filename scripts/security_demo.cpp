#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

#include "security_validator.hpp"
#include "ssl_manager.hpp"
#include "jwt_key_manager.hpp"
#include "security_auditor.hpp"

using namespace ETLPlus::Security;
using namespace ETLPlus::Auth;
using namespace ETLPlus::SSL;

/**
 * Security Features Demonstration
 *
 * This program demonstrates the comprehensive security enhancements:
 * 1. Input validation and sanitization
 * 2. SSL/TLS configuration
 * 3. JWT key management
 * 4. Security auditing
 */

int main(int argc, char* argv[]) {
    std::cout << "=== ETL Plus Backend - Security Features Demo ===\n\n";

    // 1. Security Validator Demo
    std::cout << "1. Testing Security Validator...\n";
    SecurityValidator::SecurityConfig securityConfig;
    SecurityValidator validator(securityConfig);

    // Test input validation
    std::string testInput = "SELECT * FROM users WHERE id = 1; <script>alert('xss')</script>";
    auto result = validator.validateInput(testInput, "sql");

    std::cout << "   Input validation result: " << (result.isSecure ? "SECURE" : "INSECURE") << "\n";
    for (const auto& violation : result.violations) {
        std::cout << "   - " << violation << "\n";
    }

    // Test input sanitization
    std::string maliciousInput = "<script>alert('xss')</script>Hello World";
    std::string sanitized = validator.sanitizeInput(maliciousInput, "html");
    std::cout << "   Sanitized input: " << sanitized << "\n";

    // Test rate limiting
    std::string clientId = "test_client";
    bool rateLimited = validator.isRateLimitExceeded(clientId, 10); // 10 requests per minute
    std::cout << "   Rate limited: " << (rateLimited ? "YES" : "NO") << "\n";

    std::cout << "\n";

    // 2. SSL Manager Demo
    std::cout << "2. Testing SSL Manager...\n";
    SSLManager::SSLConfig sslConfig;
    sslConfig.enableSSL = true;
    sslConfig.minimumTLSVersion = "TLSv1.2";
    sslConfig.certificatePath = "/path/to/cert.pem"; // Would be actual path in production
    sslConfig.privateKeyPath = "/path/to/key.pem";

    SSLManager sslManager(sslConfig);
    auto sslResult = sslManager.validateConfiguration();

    std::cout << "   SSL configuration valid: " << (sslResult.success ? "YES" : "NO") << "\n";
    if (!sslResult.success) {
        std::cout << "   Error: " << sslResult.errorMessage << "\n";
    }

    // Get security headers
    auto securityHeaders = sslManager.getSecurityHeaders();
    std::cout << "   Security headers configured: " << securityHeaders.size() << "\n";

    std::cout << "\n";

    // 3. JWT Key Manager Demo
    std::cout << "3. Testing JWT Key Manager...\n";
    JWTKeyManager::KeyConfig jwtConfig;
    jwtConfig.algorithm = JWTKeyManager::Algorithm::HS256;
    jwtConfig.secretKey = "your-super-secret-key-change-in-production";
    jwtConfig.enableRotation = true;
    jwtConfig.issuer = "etl-backend-demo";

    JWTKeyManager jwtManager(jwtConfig);
    bool jwtInitialized = jwtManager.initialize();

    std::cout << "   JWT manager initialized: " << (jwtInitialized ? "YES" : "NO") << "\n";

    if (jwtInitialized) {
        // Generate a test token
        std::unordered_map<std::string, std::string> claims = {
            {"user_id", "12345"},
            {"role", "admin"},
            {"permissions", "read,write,delete"}
        };

        auto tokenResult = jwtManager.generateToken(claims);
        if (tokenResult) {
            std::cout << "   JWT token generated successfully\n";
            std::cout << "   Token key ID: " << tokenResult->keyId << "\n";

            // Validate the token
            auto validationResult = jwtManager.validateToken(tokenResult->token);
            std::cout << "   Token validation: " << (validationResult ? "SUCCESS" : "FAILED") << "\n";

            if (validationResult) {
                std::cout << "   Token claims:\n";
                for (const auto& [key, value] : validationResult->claims) {
                    std::cout << "     " << key << ": " << value << "\n";
                }
            }
        }

        // Get key information
        auto keyInfo = jwtManager.getKeyInfo();
        std::cout << "   Key info - Algorithm: " << keyInfo["algorithm"] << "\n";
        std::cout << "   Key info - Rotation enabled: " << keyInfo["rotation_enabled"] << "\n";
    }

    std::cout << "\n";

    // 4. Security Auditor Demo
    std::cout << "4. Running Security Audit...\n";
    SecurityAuditor::AuditConfig auditConfig;
    auditConfig.enableStaticAnalysis = true;
    auditConfig.enableDependencyScanning = true;
    auditConfig.enableConfigAudit = true;
    auditConfig.severityThreshold = "medium";

    SecurityAuditor auditor(auditConfig);
    auto auditResult = auditor.performAudit();

    std::cout << "   Audit result: " << (auditResult.passed ? "PASSED" : "FAILED") << "\n";
    std::cout << "   Issues found:\n";
    std::cout << "     Critical: " << auditResult.issueCounts["critical"] << "\n";
    std::cout << "     High: " << auditResult.issueCounts["high"] << "\n";
    std::cout << "     Medium: " << auditResult.issueCounts["medium"] << "\n";
    std::cout << "     Low: " << auditResult.issueCounts["low"] << "\n";

    if (!auditResult.criticalIssues.empty() || !auditResult.highIssues.empty()) {
        std::cout << "   Top issues:\n";
        for (size_t i = 0; i < std::min(size_t(3), auditResult.criticalIssues.size()); ++i) {
            std::cout << "     CRITICAL: " << auditResult.criticalIssues[i] << "\n";
        }
        for (size_t i = 0; i < std::min(size_t(3), auditResult.highIssues.size()); ++i) {
            std::cout << "     HIGH: " << auditResult.highIssues[i] << "\n";
        }
    }

    std::cout << "\n";

    // Generate detailed report
    std::string report = auditor.generateReport(auditResult);
    std::cout << "5. Security Audit Report Summary:\n";
    std::cout << auditResult.getSummary() << "\n\n";

    // Save full report to file
    std::ofstream reportFile("security_audit_report.txt");
    if (reportFile.is_open()) {
        reportFile << report;
        reportFile.close();
        std::cout << "Full security audit report saved to: security_audit_report.txt\n";
    }

    std::cout << "=== Security Features Demo Complete ===\n";

    return 0;
}
