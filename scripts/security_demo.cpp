#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

#include "jwt_key_manager.hpp"
#include "security_auditor.hpp"
#include "security_validator.hpp"
#include "ssl_manager.hpp"

using namespace ETLPlus::Security;
using namespace ETLPlus::Auth;
using namespace ETLPlus::SSL;

/**
 * @brief Entry point for the Security Features Demonstration program.
 *
 * Runs a sequence of security feature demos (input validation & sanitization,
 * SSL/TLS configuration validation, JWT key management, and security auditing),
 * generates a detailed audit report, and attempts to persist that report to
 * disk with robust path handling and fallbacks.
 *
 * The function prints progress and results to stdout/stderr, reads the
 * DEMO_JWT_SECRET environment variable if present to seed the demo JWT key,
 * and uses std::filesystem::temp_directory_path() with fallbacks (HOME or
 * current working directory) to determine where to save a timestamped report
 * under a "security_reports" subdirectory. If file writing fails, it falls
 * back to writing the full report to stdout. Exceptions thrown during file
 * operations are caught and handled so the program still produces output.
 *
 * @param argc Number of command-line arguments (unused by the demo).
 * @param argv Command-line arguments (unused by the demo).
 * @return int Always returns 0 on completion.
 */

int main(int argc, char *argv[]) {
  std::cout << "=== ETL Plus Backend - Security Features Demo ===\n\n";

  // 1. Security Validator Demo
  std::cout << "1. Testing Security Validator...\n";
  SecurityValidator::SecurityConfig securityConfig;
  SecurityValidator validator(securityConfig);

  // Test input validation
  std::string testInput =
      "SELECT * FROM users WHERE id = 1; <script>alert('xss')</script>";
  auto result = validator.validateInput(testInput, "sql");

  std::cout << "   Input validation result: "
            << (result.isSecure ? "SECURE" : "INSECURE") << "\n";
  for (const auto &violation : result.violations) {
    std::cout << "   - " << violation << "\n";
  }

  // Test input sanitization
  std::string maliciousInput = "<script>alert('xss')</script>Hello World";
  std::string sanitized = validator.sanitizeInput(maliciousInput, "html");
  std::cout << "   Sanitized input: " << sanitized << "\n";

  // Test rate limiting
  std::string clientId = "test_client";
  ETLPlus::Security::SecurityValidator::RateLimitOptions rateLimitOpts(
      10, std::chrono::seconds(60), "minute");
  bool rateLimited = validator.isRateLimitExceeded(clientId, rateLimitOpts);
  std::cout << "   Rate limited: " << (rateLimited ? "YES" : "NO") << "\n";

  std::cout << "\n";

  // 2. SSL Manager Demo
  std::cout << "2. Testing SSL Manager...\n";
  SSLManager::SSLConfig sslConfig;
  sslConfig.enableSSL = true;
  sslConfig.minimumTLSVersion = "TLSv1.2";
  sslConfig.certificatePath =
      "/path/to/cert.pem"; // Would be actual path in production
  sslConfig.privateKeyPath = "/path/to/key.pem";

  SSLManager sslManager(sslConfig);
  auto sslResult = sslManager.validateConfiguration();

  std::cout << "   SSL configuration valid: "
            << (sslResult.success ? "YES" : "NO") << "\n";
  if (!sslResult.success) {
    std::cout << "   Error: " << sslResult.errorMessage << "\n";
  }

  // Get security headers
  auto securityHeaders = sslManager.getSecurityHeaders();
  std::cout << "   Security headers configured: " << securityHeaders.size()
            << "\n";

  std::cout << "\n";

  // 3. JWT Key Manager Demo
  std::cout << "3. Testing JWT Key Manager...\n";
  JWTKeyManager::KeyConfig jwtConfig;
  jwtConfig.algorithm = JWTKeyManager::Algorithm::HS256;
  if (const char *env = std::getenv("DEMO_JWT_SECRET")) {
    jwtConfig.secretKey = env;
  } else {
    jwtConfig.secretKey = "demo-only-not-for-production";
  }
  jwtConfig.enableRotation = true;
  jwtConfig.issuer = "etl-backend-demo";

  JWTKeyManager jwtManager(jwtConfig);
  bool jwtInitialized = jwtManager.initialize();

  std::cout << "   JWT manager initialized: " << (jwtInitialized ? "YES" : "NO")
            << "\n";

  if (jwtInitialized) {
    // Generate a test token
    std::unordered_map<std::string, std::string> claims = {
        {"user_id", "12345"},
        {"role", "admin"},
        {"permissions", "read,write,delete"}};

    auto tokenResult = jwtManager.generateToken(claims);
    if (tokenResult) {
      std::cout << "   JWT token generated successfully\n";
      std::cout << "   Token key ID: " << tokenResult->keyId << "\n";

      // Validate the token
      auto validationResult = jwtManager.validateToken(tokenResult->token);
      std::cout << "   Token validation: "
                << (validationResult ? "SUCCESS" : "FAILED") << "\n";

      if (validationResult) {
        std::cout << "   Token claims:\n";
        for (const auto &[key, value] : validationResult->claims) {
          std::cout << "     " << key << ": " << value << "\n";
        }
      }
    }

    // Get key information
    auto keyInfo = jwtManager.getKeyInfo();
    std::cout << "   Key info - Algorithm: " << keyInfo["algorithm"] << "\n";
    std::cout << "   Key info - Rotation enabled: "
              << keyInfo["rotation_enabled"] << "\n";
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

  std::cout << "   Audit result: " << (auditResult.passed ? "PASSED" : "FAILED")
            << "\n";
  std::cout << "   Issues found:\n";
  std::cout << "     Critical: " << auditResult.issueCounts["critical"] << "\n";
  std::cout << "     High: " << auditResult.issueCounts["high"] << "\n";
  std::cout << "     Medium: " << auditResult.issueCounts["medium"] << "\n";
  std::cout << "     Low: " << auditResult.issueCounts["low"] << "\n";

  if (!auditResult.criticalIssues.empty() || !auditResult.highIssues.empty()) {
    std::cout << "   Top issues:\n";
    for (size_t i = 0;
         i < std::min(size_t(3), auditResult.criticalIssues.size()); ++i) {
      std::cout << "     CRITICAL: " << auditResult.criticalIssues[i] << "\n";
    }
    for (size_t i = 0; i < std::min(size_t(3), auditResult.highIssues.size());
         ++i) {
      std::cout << "     HIGH: " << auditResult.highIssues[i] << "\n";
    }
  }

  std::cout << "\n";

  // Generate detailed report
  std::string report = auditor.generateReport(auditResult);
  std::cout << "5. Security Audit Report Summary:\n";
  std::cout << auditResult.getSummary() << "\n\n";

  // Save full report to file with robust path handling
  bool reportSaved = false;
  std::filesystem::path reportPath;

  try {
    // Determine writable directory (prefer temp, fallback to home)
    std::filesystem::path writableDir;
    try {
      writableDir = std::filesystem::temp_directory_path();
    } catch (const std::filesystem::filesystem_error &) {
      // Fallback to home directory
      const char *homeEnv = std::getenv("HOME");
      if (homeEnv) {
        writableDir = std::filesystem::path(homeEnv);
      } else {
        writableDir = std::filesystem::current_path();
      }
    }

    // Create reports subdirectory
    std::filesystem::path reportsDir = writableDir / "security_reports";
    std::filesystem::create_directories(reportsDir);

    // Generate unique filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream filename;
    filename << "security_audit_report_"
             << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
             << ".txt";

    reportPath = reportsDir / filename.str();

    // Attempt to open and write the file
    std::ofstream reportFile(reportPath);
    if (reportFile.is_open()) {
      reportFile << report;
      reportFile.close();
      reportSaved = true;
      std::cout << "Full security audit report saved to: "
                << reportPath.string() << "\n";
    } else {
      std::cerr << "Failed to open file for writing: " << reportPath.string()
                << "\n";
    }
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Filesystem error while saving report: " << e.what() << "\n";
    reportPath = std::filesystem::path("security_audit_report.txt");
  } catch (const std::exception &e) {
    std::cerr << "Error while saving report: " << e.what() << "\n";
    reportPath = std::filesystem::path("security_audit_report.txt");
  }

  // Fallback: write to stdout if file saving failed
  if (!reportSaved) {
    std::cout << "\n=== FALLBACK: Writing report to stdout ===\n";
    std::cout << report << "\n";
    std::cout << "=== End of security audit report ===\n";
  }

  std::cout << "=== Security Features Demo Complete ===\n";

  return 0;
}
