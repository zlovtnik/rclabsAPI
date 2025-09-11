#pragma once

#include "input_validator.hpp"
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <chrono>

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
    std::vector<std::string> allowedFileExtensions;

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
              "EXEC", "EXECUTE", "UNION", "JOIN", "WHERE", "FROM", "INTO",
              "TRUNCATE", "CALL", "MERGE", "GRANT", "REVOKE", "--", "/*", "*/",
              "xp_", "sp_"
          }),
          blockedXssPatterns({
              "<script", "</script>", "<svg", "<iframe", "<object", "<embed",
              "javascript:", "data:text/html", "data:", "vbscript:", "onload=",
              "onerror=", "onclick=", "onmouseover=", "onmouseenter=", "onfocus=",
              "oninput=", "onchange=", "onkeypress=", "onkeydown=", "onkeyup=",
              "style=", "expression(", "url(", "background:", "&#x", "&#", "%3C", "%3E"
          }),
          allowedContentTypes({
              "text/plain", "text/csv", "application/json",
              "application/xml", "text/xml", "image/jpeg",
              "image/png", "image/gif"
          }),
          allowedFileExtensions({".txt", ".csv", ".json", ".xml", ".jpg", ".jpeg", ".png", ".gif"}),
          // Dynamic nonces must be generated at runtime using generateCSPNonce() and createCSPHeaderWithNonce(nonce) for per-response nonces.
          cspHeader("default-src 'self'; script-src 'self'; "
                   "style-src 'self' 'unsafe-inline'; "
                   "img-src 'self' data: https:; "
                   "font-src 'self'; connect-src 'self'") {}
  };

  /**
   * @brief Rate limit options
   */
  struct RateLimitOptions {
    size_t allowedRequests = 1000;
    std::chrono::seconds windowDuration = std::chrono::seconds(60); // 1 minute in seconds
    std::string timeUnit = "minute"; // "second", "minute", "hour"
    std::string context = ""; // endpoint or context identifier

    RateLimitOptions()
        : allowedRequests(1000),
          windowDuration(std::chrono::seconds(60)), // 1 minute = 60 seconds
          timeUnit("minute"),
          context("") {}
    RateLimitOptions(size_t requests, std::chrono::seconds duration, const std::string& unit = "minute")
        : allowedRequests(requests), windowDuration(duration), timeUnit(unit) {}
  };

  /**
   * @brief Rate limit metadata
   */
  struct RateLimitMetadata {
    size_t remaining = 0;
    std::chrono::seconds reset = std::chrono::seconds(0);
    size_t limit = 0;
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
   * @param input The input string to validate
   * @param context The validation context that determines security rules:
   *   - "sql": Disallow/escape SQL metacharacters and enforce no unparameterized queries
   *   - "html": Strip/encode HTML tags and attributes to prevent XSS
   *   - "json": Validate UTF-8 encoding and JSON structural safety
   *   - "xml": Validate well-formedness and disallow external entities
   *   - "url": Percent-encode/validate scheme and host components
   *   - "general": Default generic sanitization for general-purpose input
   * @return SecurityResult indicating validation success/failure with violations
   * @note Validation returns an error for disallowed input; unknown contexts fall back to "general"
   * @example SecurityResult result = validator.validateInput(userInput, "sql");
   *          if (!result.isSecure) { handleValidationError(result.violations); }
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
   * @brief Input sanitization with context-aware cleaning
   * @param input The input string to sanitize
   * @param context The sanitization context that determines cleaning rules:
   *   - "sql": Escape SQL metacharacters and prepare for parameterized queries
   *   - "html": Encode HTML tags and attributes while preserving safe content
   *   - "json": Ensure UTF-8 encoding and escape special characters
   *   - "xml": Escape XML entities and ensure well-formed output
   *   - "url": Percent-encode unsafe characters for URL components
   *   - "general": Default generic sanitization for general-purpose input
   * @return Sanitized string safe for the specified context
   * @note Sanitization returns a cleaned string; unknown contexts fall back to "general"
   * @example std::string safeInput = validator.sanitizeInput(userInput, "html");
   *          // safeInput now has encoded HTML entities
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
                          const RateLimitOptions& options = RateLimitOptions());

  /**
   * @brief Get rate limit metadata
   */
  RateLimitMetadata getRateLimitMetadata(const std::string &clientId,
                                        const std::string& context = "");

  /**
   * @brief Generate a cryptographically secure CSP nonce
   * @return A base64-encoded nonce string
   * 
   * Usage:
   * std::string nonce = SecurityValidator::generateCSPNonce();
   * std::string cspHeader = SecurityValidator::createCSPHeaderWithNonce(nonce);
   * // Add nonce to script tags: <script nonce="nonce_value">...</script>
   */
  static std::string generateCSPNonce();

  /**
   * @brief Generate a SHA-256 hash for CSP script-src
   * @param scriptContent The script content to hash
   * @return SHA-256 hash in CSP format (sha256-<base64>)
   * 
   * Usage:
   * std::string hash = SecurityValidator::generateScriptHash(scriptContent);
   * std::string cspHeader = SecurityValidator::createCSPHeaderWithScriptHash(hash);
   */
  static std::string generateScriptHash(const std::string& scriptContent);

  /**
   * @brief Create CSP header with nonce
   * @param nonce The nonce to include in the CSP
   * @return CSP header string with nonce
   * 
   * Example output:
   * "default-src 'self'; script-src 'self' 'nonce-abc123...'; ..."
   */
  static std::string createCSPHeaderWithNonce(const std::string& nonce);

  /**
   * @brief Create CSP header with script hash
   * @param scriptHash The script hash to include in the CSP
   * @return CSP header string with script hash
   * 
   * Example output:
   * "default-src 'self'; script-src 'self' 'sha256-abc123...'; ..."
   */
  static std::string createCSPHeaderWithScriptHash(const std::string& scriptHash);

  /**
   * @brief Validate CSP nonce
   * @param nonce The nonce to validate
   * @param expectedNonce The expected nonce value
   * @return true if nonce is valid
   */
  static bool validateCSPNonce(const std::string& nonce, const std::string& expectedNonce);

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
