#include "security_validator.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <random>
#include <regex>
#include <sstream>
#include <string_view>

namespace ETLPlus::Security {

SecurityValidator::SecurityValidator(const SecurityConfig &config)
    : config_(config) {
  compileSecurityPatterns();
}

void SecurityValidator::compileSecurityPatterns() {
  // SQL injection patterns - very simple
  sqlInjectionPattern_ = std::regex("select|insert|update|delete|drop|create|"
                                    "alter|exec|union|script|javascript",
                                    std::regex_constants::icase);

  // XSS patterns - very simple
  xssPattern_ = std::regex(
      "<script|javascript|onload|onerror|onclick|<iframe|<object|<embed",
      std::regex_constants::icase);

  // Path traversal patterns
  pathTraversalPattern_ = std::regex("\\.\\./|\\.\\.\\\\");

  // Command injection patterns
  commandInjectionPattern_ = std::regex("[;&|`$()]");
}

SecurityValidator::SecurityResult
SecurityValidator::validateInput(const std::string &input,
                                 const std::string &context) {
  SecurityResult result;

  // Check for null bytes
  if (input.find('\0') != std::string::npos) {
    result.addViolation("Input contains null bytes");
  }

  // Validate request size
  if (input.length() > config_.maxRequestSize) {
    result.addViolation("Input size exceeds maximum allowed size");
  }

  // SQL injection check
  if (config_.enableSqlInjectionProtection) {
    auto sqlResult = validateSqlInjection(input);
    if (!sqlResult.isSecure) {
      result.violations.insert(result.violations.end(),
                               sqlResult.violations.begin(),
                               sqlResult.violations.end());
      result.isSecure = false;
    }
  }

  // XSS check
  if (config_.enableXssProtection) {
    auto xssResult = validateXss(input);
    if (!xssResult.isSecure) {
      result.violations.insert(result.violations.end(),
                               xssResult.violations.begin(),
                               xssResult.violations.end());
      result.isSecure = false;
    }
  }

  // Path traversal check
  if (std::regex_search(input, pathTraversalPattern_)) {
    result.addViolation("Input contains path traversal patterns");
  }

  // Command injection check
  if (std::regex_search(input, commandInjectionPattern_)) {
    result.addViolation("Input contains command injection patterns");
  }

  return result;
}

SecurityValidator::SecurityResult
SecurityValidator::validateSqlInjection(const std::string &input) {
  SecurityResult result;

  if (std::regex_search(input, sqlInjectionPattern_)) {
    result.addViolation("Potential SQL injection detected");
  }

  // Additional SQL injection checks
  if (input.find("' OR '1'='1") != std::string::npos ||
      input.find("1=1") != std::string::npos ||
      input.find("UNION SELECT") != std::string::npos) {
    result.addViolation("SQL injection pattern detected");
  }

  return result;
}

SecurityValidator::SecurityResult
SecurityValidator::validateXss(const std::string &input) {
  SecurityResult result;

  if (std::regex_search(input, xssPattern_)) {
    result.addViolation("Potential XSS (Cross-Site Scripting) detected");
  }

  return result;
}

SecurityValidator::SecurityResult
SecurityValidator::validateCsrfToken(const std::string &token,
                                     const std::string &expectedToken) {
  SecurityResult result;

  if (token.empty()) {
    result.addViolation("CSRF token is missing");
    return result;
  }

  if (token.length() < 32) {
    result.addViolation("CSRF token is too short");
  }

  if (token != expectedToken) {
    result.addViolation("CSRF token validation failed");
  }

  return result;
}

SecurityValidator::SecurityResult
SecurityValidator::validateRequestSize(size_t contentLength) {
  SecurityResult result;

  if (contentLength > config_.maxRequestSize) {
    result.addViolation("Request size exceeds maximum allowed size: " +
                        std::to_string(contentLength) + " > " +
                        std::to_string(config_.maxRequestSize));
  }

  return result;
}

SecurityValidator::SecurityResult SecurityValidator::validateRequestHeaders(
    const std::unordered_map<std::string, std::string> &headers) {
  SecurityResult result;

  // Check header count
  if (headers.size() > config_.maxHeaderCount) {
    result.addViolation("Too many headers: " + std::to_string(headers.size()) +
                        " > " + std::to_string(config_.maxHeaderCount));
  }

  // Check individual header sizes
  for (const auto &[name, value] : headers) {
    if (name.length() > 256) {
      result.addViolation("Header name too long: " + name);
    }
    if (value.length() > config_.maxHeaderSize) {
      result.addViolation("Header value too long: " + name);
    }

    // Check for suspicious header patterns
    if (name.find('\0') != std::string::npos ||
        value.find('\0') != std::string::npos) {
      result.addViolation("Header contains null bytes: " + name);
    }
  }

  return result;
}

std::string SecurityValidator::sanitizeInput(const std::string &input,
                                             const std::string &context) {
  if (!config_.enableInputSanitization) {
    return input;
  }

  std::string sanitized = input;

  // Remove null bytes
  sanitized = removeNullBytes(sanitized);

  // HTML escape for web contexts
  if (context == "html" || context == "web") {
    sanitized = escapeHtml(sanitized);
  }

  // Remove control characters except newlines and tabs
  sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(),
                                 [](char c) {
                                   return std::iscntrl(c) && c != '\n' &&
                                          c != '\t';
                                 }),
                  sanitized.end());

  return sanitized;
}

std::unordered_map<std::string, std::string>
SecurityValidator::generateSecurityHeaders() {
  std::unordered_map<std::string, std::string> headers;

  // Content Security Policy
  headers["Content-Security-Policy"] = config_.cspHeader;

  // XSS Protection
  headers["X-XSS-Protection"] = "1; mode=block";

  // Content Type Options
  headers["X-Content-Type-Options"] = "nosniff";

  // Frame Options
  headers["X-Frame-Options"] = "DENY";

  // Referrer Policy
  headers["Referrer-Policy"] = "strict-origin-when-cross-origin";

  // Permissions Policy
  headers["Permissions-Policy"] = "geolocation=(), microphone=(), camera=()";

  return headers;
}

SecurityValidator::SecurityResult
SecurityValidator::validateFileUpload(const std::string &filename,
                                      const std::string &contentType,
                                      size_t fileSize) {
  SecurityResult result;

  // Check file size
  if (fileSize > config_.maxRequestSize) {
    result.addViolation("File size exceeds maximum allowed size");
  }

  // Check filename for path traversal
  if (std::regex_search(filename, pathTraversalPattern_)) {
    result.addViolation("Filename contains path traversal patterns");
  }

  // Check file extension
  if (!isValidFileExtension(filename)) {
    result.addViolation("Invalid file extension");
  }

  // Check content type
  const auto &allowedTypes = config_.allowedContentTypes;

  bool validType = false;
  for (const auto &allowed : allowedTypes) {
    if (contentType == allowed) {
      validType = true;
      break;
    }
  }

  if (!validType) {
    result.addViolation("Invalid content type: " + contentType);
  }

  return result;
}

bool SecurityValidator::isRateLimitExceeded(const std::string &clientId,
                                            const RateLimitOptions &options) {
  std::lock_guard<std::mutex> lock(rateLimitMutex_);

  cleanupExpiredRateLimitEntries();

  auto now = std::chrono::system_clock::now();
  auto windowStart = now - options.windowDuration;

  auto &timestamps = rateLimitStore_[clientId];

  // Count requests in the time window
  size_t recentRequests = 0;
  for (const auto &timestamp : timestamps) {
    if (timestamp > windowStart) {
      recentRequests++;
    }
  }

  if (recentRequests >= options.allowedRequests) {
    return true;
  }

  // Add current request timestamp
  timestamps.push_back(now);
  return false;
}

bool SecurityValidator::containsBlockedPattern(
    const std::string &input, const std::vector<std::string> &patterns) {
  for (const auto &pattern : patterns) {
    if (input.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string SecurityValidator::escapeHtml(const std::string &input) {
  std::string escaped;
  escaped.reserve(input.size() *
                  1.2); // Reserve ~20% extra for escape sequences

  // Fast lookup table for HTML entities (single character keys)
  static const std::array<std::string_view, 256> htmlEntities = []() {
    std::array<std::string_view, 256> entities{};
    entities['&'] = "&amp;";
    entities['<'] = "&lt;";
    entities['>'] = "&gt;";
    entities['"'] = "&quot;";
    entities['\''] = "&#x27;";
    entities['/'] = "&#x2F;";
    return entities;
  }();

  for (char c : input) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (!htmlEntities[uc].empty()) {
      escaped.append(htmlEntities[uc]);
    } else {
      escaped.push_back(c);
    }
  }

  return escaped;
}

std::string SecurityValidator::removeNullBytes(const std::string &input) {
  std::string result = input;
  result.erase(std::remove(result.begin(), result.end(), '\0'), result.end());
  return result;
}

bool SecurityValidator::isValidFileExtension(const std::string &filename) {
  std::vector<std::string> allowedExtensions = {
      ".txt", ".csv", ".json", ".xml", ".jpg", ".jpeg", ".png", ".gif"};

  std::string lowerFilename = filename;
  std::transform(lowerFilename.begin(), lowerFilename.end(),
                 lowerFilename.begin(), ::tolower);

  for (const auto &ext : allowedExtensions) {
    if (lowerFilename.length() >= ext.length() &&
        lowerFilename.substr(lowerFilename.length() - ext.length()) == ext) {
      return true;
    }
  }

  return false;
}

void SecurityValidator::cleanupExpiredRateLimitEntries() {
  auto now = std::chrono::system_clock::now();
  auto cutoff = now - std::chrono::minutes(2); // Keep 2 minutes of history

  for (auto it = rateLimitStore_.begin(); it != rateLimitStore_.end();) {
    auto &timestamps = it->second;
    timestamps.erase(std::remove_if(timestamps.begin(), timestamps.end(),
                                    [cutoff](const auto &timestamp) {
                                      return timestamp < cutoff;
                                    }),
                     timestamps.end());

    if (timestamps.empty()) {
      it = rateLimitStore_.erase(it);
    } else {
      ++it;
    }
  }
}

std::string SecurityValidator::generateCSPNonce() {
  unsigned char random_bytes[16]; // 128 bits for security

  if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1) {
    // Fallback to less secure random if OpenSSL fails
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
      random_bytes[i] = static_cast<unsigned char>(dis(gen));
    }
  }

  // Base64 encode the random bytes
  BIO *bio = BIO_new(BIO_s_mem());
  BIO *b64 = BIO_new(BIO_f_base64());
  BIO_push(b64, bio);

  BIO_write(b64, random_bytes, sizeof(random_bytes));
  BIO_flush(b64);

  char *encoded_data = nullptr;
  long encoded_length = BIO_get_mem_data(bio, &encoded_data);

  std::string nonce(encoded_data, encoded_length);

  // Remove any trailing newlines
  nonce.erase(std::remove(nonce.begin(), nonce.end(), '\n'), nonce.end());

  BIO_free_all(b64);

  return nonce;
}

std::string
SecurityValidator::generateScriptHash(const std::string &scriptContent) {
  unsigned char hash[SHA256_DIGEST_LENGTH];

  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, scriptContent.c_str(), scriptContent.length());
  SHA256_Final(hash, &sha256);

  // Base64 encode the hash
  BIO *bio = BIO_new(BIO_s_mem());
  BIO *b64 = BIO_new(BIO_f_base64());
  BIO_push(b64, bio);

  BIO_write(b64, hash, SHA256_DIGEST_LENGTH);
  BIO_flush(b64);

  char *encoded_data = nullptr;
  long encoded_length = BIO_get_mem_data(bio, &encoded_data);

  std::string hash_b64(encoded_data, encoded_length);

  // Remove any trailing newlines and construct CSP hash
  hash_b64.erase(std::remove(hash_b64.begin(), hash_b64.end(), '\n'),
                 hash_b64.end());

  BIO_free_all(b64);

  return "sha256-" + hash_b64;
}

std::string
SecurityValidator::createCSPHeaderWithNonce(const std::string &nonce) {
  std::ostringstream csp;
  csp << "default-src 'self'; "
      << "script-src 'self' 'nonce-" << nonce << "'; "
      << "style-src 'self' 'unsafe-inline'; "
      << "img-src 'self' data: https:; "
      << "font-src 'self'; "
      << "connect-src 'self'";
  return csp.str();
}

std::string SecurityValidator::createCSPHeaderWithScriptHash(
    const std::string &scriptHash) {
  std::ostringstream csp;
  csp << "default-src 'self'; "
      << "script-src 'self' '" << scriptHash << "'; "
      << "style-src 'self' 'unsafe-inline'; "
      << "img-src 'self' data: https:; "
      << "font-src 'self'; "
      << "connect-src 'self'";
  return csp.str();
}

bool SecurityValidator::validateCSPNonce(const std::string &nonce,
                                         const std::string &expectedNonce) {
  // Simple string comparison - in production, you might want to use
  // constant-time comparison
  return nonce == expectedNonce;
}

} // namespace ETLPlus::Security
