#pragma once

#include "input_validator.hpp"
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ETLPlus::Security {

/**
 * @brief Enhanced security validation component
 *
 * This class provides advanced security validation features including:
 * - SQL injection detection and prevention
 * - XSS (Cross-Site Scripting) protection
 * - CSRF (Cross-Site Request Forgery) protection
 * - Input sanitization and filtering
 * - Security headers validation
 * - Request size limits and rate limiting
 */
class SecurityValidator {
public:
  /**
   * @brief Security configuration
   */
  struct SecurityConfig {
    // Input validation settings
    bool enableSqlInjectionProtection;
    bool enableXssProtection;
    bool enableCsrfProtection;
    bool enableInputSanitization;

    // Request limits
    size_t maxRequestSize;
    size_t maxHeaderCount;
    size_t maxHeaderSize;
    size_t maxQueryParamCount;
    size_t maxPathLength;

    // Security patterns
    std::vector<std::string> blockedSqlKeywords;
    std::vector<std::string> blockedXssPatterns;

    // File upload validation
    std::vector<std::string> allowedContentTypes;

    // Content Security Policy
    std::string cspHeader;

    SecurityConfig()
        : enableSqlInjectionProtection(true),
          enableXssProtection(true),
          enableCsrfProtection(true),
          enableInputSanitization(true),
          maxRequestSize(1024 * 1024), // 1MB
          maxHeaderCount(50),
          maxHeaderSize(8192), // 8KB
          maxQueryParamCount(100),
          maxPathLength(2048),
          blockedSqlKeywords({
              "SELECT", "INSERT", "UPDATE", "DELETE", "DROP", "CREATE", "ALTER",
              "EXEC", "EXECUTE", "UNION", "JOIN", "WHERE", "FROM", "INTO"
          }),
          blockedXssPatterns({
              "<script", "</script>", "javascript:", "onload=", "onerror=",
              "onclick=", "onmouseover=", "<iframe", "<object", "<embed"
          }),
          allowedContentTypes({
              "text/plain", "text/csv", "application/json",
              "application/xml", "text/xml", "image/jpeg",
              "image/png", "image/gif"
          }),
          cspHeader("default-src 'self'; script-src 'self'; "
                   "style-src 'self' 'unsafe-inline'; "
                   "img-src 'self' data: https:; "
                   "font-src 'self'; connect-src 'self'") {}
  };

  /**
   * @brief Security validation result
   */
  struct SecurityResult {
    bool isSecure = true;
    std::vector<std::string> violations;
    std::vector<std::string> warnings;
    std::unordered_map<std::string, std::string> securityHeaders;

    void addViolation(const std::string &message) {
      violations.push_back(message);
      isSecure = false;
    }

    void addWarning(const std::string &message) {
      warnings.push_back(message);
    }

    void addSecurityHeader(const std::string &name, const std::string &value) {
      securityHeaders[name] = value;
    }
  };

  SecurityValidator(const SecurityConfig &config = SecurityConfig());
  ~SecurityValidator() = default;

  /**
   * @brief Comprehensive input security validation
   */
  SecurityResult validateInput(const std::string &input,
                              const std::string &context = "general");

  /**
   * @brief SQL injection detection and prevention
   */
  SecurityResult validateSqlInjection(const std::string &input);

  /**
   * @brief XSS (Cross-Site Scripting) protection
   */
  SecurityResult validateXss(const std::string &input);

  /**
   * @brief CSRF token validation
   */
  SecurityResult validateCsrfToken(const std::string &token,
                                  const std::string &expectedToken);

  /**
   * @brief Request size and structure validation
   */
  SecurityResult validateRequestSize(size_t contentLength);
  SecurityResult validateRequestHeaders(
      const std::unordered_map<std::string, std::string> &headers);

  /**
   * @brief Input sanitization
   */
  std::string sanitizeInput(const std::string &input,
                           const std::string &context = "general");

  /**
   * @brief Generate security headers
   */
  std::unordered_map<std::string, std::string> generateSecurityHeaders();

  /**
   * @brief Validate file upload security
   */
  SecurityResult validateFileUpload(const std::string &filename,
                                   const std::string &contentType,
                                   size_t fileSize);

  /**
   * @brief Rate limiting validation
   */
  bool isRateLimitExceeded(const std::string &clientId,
                          size_t maxRequestsPerMinute = 1000);

private:
  SecurityConfig config_;

  // Security patterns
  std::regex sqlInjectionPattern_;
  std::regex xssPattern_;
  std::regex pathTraversalPattern_;
  std::regex commandInjectionPattern_;

  // Rate limiting storage
  std::unordered_map<std::string, std::vector<std::chrono::system_clock::time_point>>
      rateLimitStore_;
  std::mutex rateLimitMutex_;

  // Helper methods
  bool containsBlockedPattern(const std::string &input,
                             const std::vector<std::string> &patterns);
  std::string escapeHtml(const std::string &input);
  std::string removeNullBytes(const std::string &input);
  bool isValidFileExtension(const std::string &filename);
  void cleanupExpiredRateLimitEntries();

  // Security pattern compilation
  void compileSecurityPatterns();
};

} // namespace ETLPlus::Security
