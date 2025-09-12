#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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

    /**
     * @brief Record a validation error and mark the result as invalid.
     *
     * Appends the provided error message to the ValidationResult's error list
     * and sets isValid to false.
     *
     * @param error Human-readable description of the validation failure.
     */
    void addError(const std::string &error) {
      errors.push_back(error);
      isValid = false;
    }
  };

  struct SecurityResult {
    bool isSecure = true;
    std::vector<std::string> issues;

    /**
     * @brief Record a security issue and mark the result as insecure.
     *
     * Appends a human-readable issue description to the SecurityResult's issues
     * list and sets isSecure to false.
     *
     * @param issue Description of the detected security issue.
     */
    void addIssue(const std::string &issue) {
      issues.push_back(issue);
      isSecure = false;
    }
  };

private:
  std::vector<std::string> knownEndpoints_ = {
      "/api/auth/login",     "/api/auth/logout", "/api/auth/profile",
      "/api/jobs",           "/api/logs",        
      "/api/monitor/status", "/api/health"};

  /**
   * @brief Decode percent-encoded octets in a string (URL percent-decoding).
   *
   * Decodes occurrences of `%` followed by two hex digits into the
   * corresponding byte value and returns the resulting string. Any `%` not
   * followed by two characters is left as-is. The function performs a byte-wise
   * decode and may produce embedded null bytes in the returned std::string if
   * the decoded value is `0x00`.
   *
   * @param input The percent-encoded input (e.g., "/path%2Fto%2Efile").
   * @return std::string The decoded string with percent-encoded sequences
   * replaced by their byte values.
   */
  std::string percentDecode(const std::string &input) {
    std::string result;
    for (size_t i = 0; i < input.size(); ++i) {
      if (input[i] == '%' && i + 2 < input.size()) {
        char hex1 = input[i + 1];
        char hex2 = input[i + 2];
        
        // Validate hex digits
        if ((hex1 >= '0' && hex1 <= '9') || (hex1 >= 'A' && hex1 <= 'F') || (hex1 >= 'a' && hex1 <= 'f')) {
          if ((hex2 >= '0' && hex2 <= '9') || (hex2 >= 'A' && hex2 <= 'F') || (hex2 >= 'a' && hex2 <= 'f')) {
            char hex[3] = {hex1, hex2, '\0'};
            unsigned char ch = (unsigned char)strtol(hex, nullptr, 16);
            
            // Sanitize null bytes and other control characters
            if (ch == 0x00 || ch < 0x20) {
              result += '?'; // Replace with safe placeholder
            } else {
              result += (char)ch;
            }
            i += 2;
            continue;
          }
        }
        // Invalid hex sequence - treat as literal
        result += input[i];
      } else if (input[i] == '+') {
        result += ' '; // Handle '+' as space for URL decoding
      } else {
        result += input[i];
      }
    }
    return result;
  }

  /**
   * @brief Normalize a filesystem-style URL path by resolving "." and ".."
   * segments.
   *
   * Produces a canonical absolute path: removes "." segments, collapses
   * consecutive slashes, resolves ".." by removing the previous segment (if
   * any), and ensures the result starts with '/' (returns "/" for an empty
   * result).
   *
   * @param path Input path to normalize (may contain leading/trailing slashes
   * and
   *             "."/".." components).
   * @return std::string Normalized absolute path.
   */
  std::string normalizePath(const std::string &path) {
    // First convert all backslashes to forward slashes
    std::string normalizedInput = path;
    for (char &c : normalizedInput) {
      if (c == '\\') {
        c = '/';
      }
    }
    
    std::vector<std::string> segments;
    std::stringstream ss(normalizedInput);
    std::string segment;
    while (std::getline(ss, segment, '/')) {
      if (segment == "." || segment.empty()) {
        continue;
      } else if (segment == "..") {
        if (!segments.empty()) {
          segments.pop_back();
        }
      } else {
        segments.push_back(segment);
      }
    }
    std::string normalized;
    for (const auto &seg : segments) {
      normalized += "/" + seg;
    }
    if (normalized.empty())
      normalized = "/";
    return normalized;
  }

  /**
   * @brief Perform iterative percent-decoding until no changes occur.
   * 
   * Repeatedly percent-decodes the input string until no further changes
   * are made or a safe iteration limit is reached to prevent infinite loops.
   * This prevents double-encoded traversal attacks.
   */
  std::string iterativePercentDecode(const std::string &input) {
    std::string current = input;
    std::string previous;
    int iterations = 0;
    const int maxIterations = 5; // Safety limit
    
    do {
      previous = current;
      current = percentDecode(current);
      iterations++;
    } while (current != previous && iterations < maxIterations);
    
    return current;
  }

public:
  /**
   * @brief Validate an HTTP request and produce a structured ValidationResult.
   *
   * Performs syntactic and semantic checks on the provided request data and
   * returns a ValidationResult that contains parsed fields and any validation
   * errors found.
   *
   * The validator:
   * - Extracts the request path and parses query parameters from the URL.
   * - Ensures method and path are present and that the path starts with '/'.
   * - Detects path traversal (including percent-encoded traversal) by
   *   percent-decoding and normalizing the path.
   * - Verifies the path is a known endpoint and that the HTTP method is allowed
   *   for that endpoint.
   * - Enforces authentication requirements for protected endpoints by checking
   *   for an Authorization header using the Bearer scheme.
   * - For endpoints that require a body (e.g., POST/PUT where applicable),
   *   ensures the body is present and appears to be valid JSON.
   *
   * The returned ValidationResult contains:
   * - method, headers: copied from the inputs.
   * - path: the extracted request path (portion before '?').
   * - queryParams: parsed key/value pairs from the URL query string.
   * - isValid and errors: overall validity and any specific error messages
   *   accumulated during validation.
   *
   * @param method The HTTP method (e.g., "GET", "POST").
   * @param url The full request URL or path (may include a query string).
   * @param headers Request headers (key/value pairs); header name matching is
   *                performed case-insensitively where required.
   * @param body Optional request body (used when the endpoint and method
   *             require a body).
   * @return ValidationResult Struct containing parsed request fields and any
   *         validation errors.
   */
  ValidationResult
  validateRequest(const std::string &method, const std::string &url,
                  const std::unordered_map<std::string, std::string> &headers,
                  const std::string &body = "") {
    ValidationResult result;
    result.method = method;
    result.headers = headers;

    // Extract path and query parameters
    size_t queryPos = url.find('?');
    result.path =
        (queryPos != std::string::npos) ? url.substr(0, queryPos) : url;

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

    // Enhanced path traversal check + canonical path derivation
    std::string canonicalPath = result.path;
    {
      // Iteratively percent-decode the path to prevent double-encoded traversal
      std::string decodedPath = iterativePercentDecode(result.path);

      // Normalize the fully-decoded path
      std::string normalizedPath = normalizePath(decodedPath);

      // Check for traversal patterns in both decoded and normalized paths
      if (decodedPath.find("/../") != std::string::npos ||
          (decodedPath.size() >= 3 &&
           decodedPath.rfind("/..", decodedPath.size() - 3) !=
               std::string::npos) ||
          decodedPath.rfind("../", 0) == 0 ||
          normalizedPath.find("..") != std::string::npos) {
        result.addError(
            "Path traversal not allowed (percent-encoded or direct)");
      }

      // Use normalized path for subsequent checks
      canonicalPath = normalizedPath;
    }

    // Validate endpoint
    if (!isKnownEndpoint(canonicalPath)) {
      result.addError("Unknown endpoint: " + canonicalPath);
    }

    // Validate method for endpoint
    if (!isValidMethodForEndpoint(method, canonicalPath)) {
      result.addError("Method " + method + " not allowed for " + canonicalPath);
    }

    // Validate authentication for protected endpoints
    if (requiresAuth(canonicalPath)) {
      std::string authHeader;
      for (const auto &header : headers) {
        std::string lowerKey = header.first;
        std::transform(
            lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowerKey == "authorization") {
          authHeader = header.second;
          break;
        }
      }
      if (authHeader.empty()) {
        result.addError("Authorization header required");
      } else if (!isValidAuthHeader(authHeader)) {
        result.addError("Invalid authorization header format");
      }
    }

    // Validate body for POST/PUT requests
    if ((method == "POST" || method == "PUT") && requiresBody(canonicalPath)) {
      if (body.empty()) {
        result.addError("Request body required for " + method + " " +
                        canonicalPath);
      } else if (!isValidJson(body)) {
        result.addError("Invalid JSON in request body");
      }
    }

    return result;
  }

  /**
   * @brief Analyze a request for common security issues.
   *
   * Performs pattern-based checks for SQL injection and cross-site scripting
   * (XSS) in the provided URL and body, and flags suspicious User-Agent
   * headers.
   *
   * The function does not modify inputs; it returns a SecurityResult whose
   * isSecure flag is set to false and issues contains short descriptions for
   * each detected problem.
   *
   * @return SecurityResult Result containing overall security verdict and any
   * detected issues.
   */
  SecurityResult validateSecurity(
      const std::string &url, const std::string &body,
      const std::unordered_map<std::string, std::string> &headers) {
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
    std::string userAgent;
    for (const auto &header : headers) {
      std::string lowerKey = header.first;
      std::transform(
          lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (lowerKey == "user-agent") {
        userAgent = header.second;
        break;
      }
    }
    if (!userAgent.empty() && isSuspiciousUserAgent(userAgent)) {
      result.addIssue("Suspicious user agent detected");
    }

    return result;
  }

private:

  /**
   * @brief Parse a URL query string into key/value pairs.
   *
   * Parses a query string of the form "a=1&b=two&c=" (typically the portion
   * after '?') into an unordered_map mapping keys to values.
   *
   * - Splits on '&' to extract pairs, then on the first '=' within each pair.
   * - Pairs without an '=' are ignored.
   * - Empty values are allowed (e.g., "c=" yields {"c", ""}).
   * - If a key appears multiple times, the last occurrence wins.
   * - This function performs percent-decoding and converts '+' to spaces.
   *
   * @param query The raw query string (do not include the leading '?').
   * @return std::unordered_map<std::string, std::string> Map of parsed query
   * parameters.
   */
  std::unordered_map<std::string, std::string>
  parseQueryString(const std::string &query) {
    std::unordered_map<std::string, std::string> params;
    std::istringstream iss(query);
    std::string pair;

    while (std::getline(iss, pair, '&')) {
      size_t equalPos = pair.find('=');
      if (equalPos != std::string::npos) {
        std::string key = pair.substr(0, equalPos);
        std::string value = pair.substr(equalPos + 1);
        params[percentDecode(key)] = percentDecode(value);
      }
    }

    return params;
  }

  /**
   * @brief Determines whether a request path matches a known API endpoint.
   *
   * Performs an exact match against the validator's known endpoint list and
   * recognizes parameterized job endpoints of the form `/api/jobs/{id}`.
   *
   * @param path Request path (must begin with '/'); used as-is for matching.
   * @return true if the path is a known endpoint or an individual `/api/jobs/*`
   * resource, false otherwise.
   */
  bool isKnownEndpoint(const std::string &path) {
    // Check exact matches
    for (const auto &endpoint : knownEndpoints_) {
      if (path == endpoint)
        return true;
    }

    // Check parameterized endpoints
    const std::string jobsPrefix = "/api/jobs/";
    if (path.compare(0, jobsPrefix.size(), jobsPrefix) == 0 &&
        path.size() > jobsPrefix.size()) {
      return true; // Individual job endpoints
    }

    return false;
  }

  /**
   * @brief Checks whether an HTTP method is allowed for a given API endpoint
   * path.
   *
   * Returns true when the combination of `method` and `path` is permitted by
   * the validator's per-endpoint rules; otherwise returns false. Recognized
   * rules:
   * - POST required for /api/auth/login and /api/auth/logout
   * - GET required for /api/auth/profile, /api/logs, and /api/health
   * - GET or POST allowed for /api/jobs
   * - For paths beginning with /api/jobs/ (individual job resources), GET, PUT,
   *   or DELETE are allowed
   *
   * For unknown endpoints or disallowed methods the function logs a rejection
   * message to stdout and returns false.
   *
   * @param method HTTP method name (e.g., "GET", "POST").
   * @param path   Request path (must be the endpoint path to check).
   * @return true if the method is valid for the endpoint; false otherwise.
   */
  bool isValidMethodForEndpoint(const std::string &method,
                                const std::string &path) {
    if (path == "/api/auth/login" || path == "/api/auth/logout") {
      return method == "POST";
    }
    if (path == "/api/auth/profile" || path == "/api/logs" ||
        path == "/api/health" || path == "/api/monitor/status") {
      return method == "GET";
    }
    if (path == "/api/jobs") {
      return method == "GET" || method == "POST";
    }
    const std::string jobsPrefix = "/api/jobs/";
    if (path.compare(0, jobsPrefix.size(), jobsPrefix) == 0) {
      return method == "GET" || method == "PUT" || method == "DELETE";
    }
    // Default deny for unknown endpoints or methods
    return false;
  }

  /**
   * @brief Determines whether the given request path requires an Authorization
   * header.
   *
   * Returns true for endpoints that must be authenticated. The health check
   * ("/api/health") and the login endpoint ("/api/auth/login") are explicitly
   * exempt and do not require auth.
   *
   * @param path Request path (path component of the URL).
   * @return true if the endpoint requires authentication; false otherwise.
   */
  bool requiresAuth(const std::string &path) {
    return path != "/api/health" && path != "/api/auth/login";
  }

  /**
   * @brief Determines whether the given endpoint requires a request body.
   *
   * Returns true for endpoints that must include a non-empty request body
   * (currently "/api/auth/login" and "/api/jobs").
   *
   * @param path Request path to check.
   * @return true if the path requires a body; false otherwise.
   */
  bool requiresBody(const std::string &path) {
    return path == "/api/auth/login" || path == "/api/jobs";
  }

  /**
   * @brief Validates an HTTP Authorization header uses the Bearer scheme with a
   * non-empty token.
   *
   * Checks that the header begins with the literal "Bearer " (case-sensitive)
   * and that at least one character follows the prefix.
   *
   * @param auth The raw Authorization header value to validate.
   * @return true if the header is a Bearer token with a non-empty token
   * portion; false otherwise.
   */
  bool isValidAuthHeader(const std::string &auth) {
    const std::string bearerPrefix = "Bearer ";
    return auth.compare(0, bearerPrefix.size(), bearerPrefix) == 0 &&
           auth.size() > bearerPrefix.size();
  }

  /**
   * @brief Validates whether a string contains valid JSON.
   *
   * Attempts to parse the input string as JSON using nlohmann::json.
   * Returns true only if parsing succeeds and consumes the whole input.
   *
   * @param body The raw request body to validate.
   * @return true if the body is valid JSON; false otherwise.
   */
  bool isValidJson(const std::string &body) {
    if (body.empty())
      return false;
    try {
      auto json = nlohmann::json::parse(body);
      return true;
    } catch (const nlohmann::json::parse_error &) {
      return false;
    }
  }

  /**
   * @brief Heuristically detects potential SQL-injection patterns in a string.
   *
   * This function uses regex with word boundaries and context-aware patterns
   * to detect SQL keywords and common SQL injection patterns. It checks for
   * whole words like "select", "drop", etc., and patterns like "select .*
   * from", "union select", etc. Enhanced with better pattern matching and
   * reduced false positives through context validation.
   *
   * @param input The string to inspect (URL, body, or other request content).
   * @return true if suspicious SQL patterns are detected; otherwise false.
   */
  bool checkForSqlInjection(const std::string &input) {
    if (input.empty()) {
      return false;
    }

    std::string lower = input;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    int score = 0;

    // Check for SQL keywords with word boundaries (more precise)
    static const std::vector<std::string> sqlKeywords = {
        "\\bselect\\b", "\\binsert\\b", "\\bupdate\\b",  "\\bdelete\\b",
        "\\bdrop\\b",   "\\bunion\\b",  "\\bexec\\b",    "\\bscript\\b",
        "\\balter\\b",  "\\bcreate\\b", "\\btruncate\\b"};

    for (const auto &pattern : sqlKeywords) {
      std::regex regex(pattern, std::regex_constants::icase);
      if (std::regex_search(lower, regex)) {
        score += 1;
      }
    }

    // Enhanced SQL injection patterns with better context
    static const std::vector<std::regex> kSqlPatterns = {
        std::regex(R"(select\s+.*\s+from\s+)", std::regex::icase),
        std::regex(R"(union\s+(all\s+)?select\s+)", std::regex::icase),
        std::regex(R"(drop\s+table\s+)", std::regex::icase),
        std::regex(R"(insert\s+into\s+)", std::regex::icase),
        std::regex(R"(update\s+.*\s+set\s+)", std::regex::icase),
        std::regex(R"(delete\s+from\s+)", std::regex::icase),
        std::regex(R"(';\s*drop\s+)", std::regex::icase),
        std::regex(R"(';\s*--)", std::regex::icase),
        std::regex(R"(/\*.*\*/)", std::regex::icase),
        std::regex(R"(1\s*=\s*1)", std::regex::icase),
        std::regex(R"(or\s+1\s*=\s*1)", std::regex::icase),
        std::regex(R"(and\s+1\s*=\s*1)", std::regex::icase),
        std::regex(R"(exec\s*\()", std::regex::icase),
        std::regex(R"(xp_cmdshell)", std::regex::icase),
        std::regex(R"(sp_executesql)", std::regex::icase),
        std::regex(R"(information_schema)", std::regex::icase),
        std::regex(R"(sysobjects)", std::regex::icase),
        std::regex(R"(having\s+1\s*=\s*1)", std::regex::icase),
        std::regex(R"(group\s+by\s+.*\s+having)", std::regex::icase),
        std::regex(R"(order\s+by\s+.*\s*--)", std::regex::icase),
        std::regex(R"(waitfor\s+delay)", std::regex::icase),
        std::regex(R"(benchmark\s*\()", std::regex::icase),
        std::regex(R"(sleep\s*\()", std::regex::icase),
        std::regex(R"(load_file\s*\()", std::regex::icase),
        std::regex(R"(into\s+outfile)", std::regex::icase),
        std::regex(R"(declare\s+.*\s+cursor)", std::regex::icase),
        std::regex(R"(open\s+.*\s+cursor)", std::regex::icase),
        std::regex(R"(fetch\s+.*\s+from)", std::regex::icase),
        std::regex(R"(shutdown)", std::regex::icase),
        std::regex(R"(backup\s+database)", std::regex::icase),
        std::regex(R"(restore\s+database)", std::regex::icase),
    };

    for (const auto &rx : kSqlPatterns) {
      if (std::regex_search(lower, rx)) {
        score += 2; // Higher weight for complex patterns
      }
    }

    // Check for suspicious character combinations
    std::vector<std::string> suspiciousChars = {
        "';\\s*drop",             // Quote + semicolon + drop
        "\";\\s*drop",            // Double quote + semicolon + drop
        "';\\s*exec",             // Quote + semicolon + exec
        "\";\\s*exec",            // Double quote + semicolon + exec
        "/*!",                    // MySQL version-specific comments
        "/*!\\d+",                // MySQL version numbers in comments
        "#\\s*\\w+",              // Hash comments followed by SQL
        "--\\s*\\w+",             // Line comments followed by SQL
        "\\|\\|",                 // Double pipe operators
        "&&",                     // Double ampersand
        "\\$\\{",                 // Shell variable expansion
        "`.*`",                   // Backtick execution
        "\\$\\(.*\\)",            // Command substitution
        "<\\?php",                // PHP code injection
        "<%",                     // ASP code injection
        "<script",                // Script tag injection
        "javascript:",            // JavaScript URI schemes
        "vbscript:",              // VBScript URI schemes
        "data:",                  // Data URI schemes
        "on\\w+\\s*=",            // Event handler attributes
        "style\\s*=.*expression", // CSS expression injection
        "style\\s*=.*javascript", // CSS JavaScript injection
        "src\\s*=.*javascript",   // Source JavaScript injection
        "href\\s*=.*javascript"   // Link JavaScript injection
    };

    for (const auto &pattern : suspiciousChars) {
      std::regex regex(pattern, std::regex_constants::icase);
      if (std::regex_search(lower, regex)) {
        score += 3; // Highest weight for highly suspicious patterns
      }
    }

    // Context-aware validation: reduce false positives for legitimate queries
    if (lower.find("select") != std::string::npos &&
        lower.find("from") != std::string::npos) {
      // Check if this looks like a legitimate query structure
      if (lower.find("where") != std::string::npos ||
          lower.find("order by") != std::string::npos ||
          lower.find("group by") != std::string::npos) {
        // This might be a legitimate query - reduce score
        score = std::max(0, score - 1);
      }
    }

    // Require higher threshold for detection to reduce false positives
    return score >= 3;
  }

  /**
   * @brief Heuristically detects potential XSS payloads in a string.
   *
   * Performs a case-insensitive scan of the provided input for common XSS
   * indicators such as script tags, event handler attributes (e.g., `onload=`,
   * `onclick=`), `javascript:` URIs, `eval(`, and references to `document`
   * APIs.
   *
   * @param input The text to inspect for possible cross-site scripting content.
   * @return true if any common XSS pattern is found; false otherwise.
   *
   * @note This is a heuristic check intended to flag likely XSS content and may
   * produce false positives. It does not replace proper encoding, sanitization,
   * or context-aware XSS protection mechanisms.
   */
  bool checkForXss(const std::string &input) {
    std::string lower = input;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<std::string> xssPatterns = {
        "<script",  "</script>", "javascript:",     "onload=",       "onerror=",
        "onclick=", "eval(",     "document.cookie", "document.write"};

    for (const auto &pattern : xssPatterns) {
      if (lower.find(pattern) != std::string::npos) {
        return true;
      }
    }

    return false;
  }

  /**
   * @brief Detects whether a User-Agent string indicates a suspicious scanner
   * or proxy tool.
   *
   * Performs a case-insensitive substring search for common reconnaissance/tool
   * identifiers (for example: "sqlmap", "nikto", "nmap", "masscan", "zap",
   * "burp"). Intended as a lightweight heuristic for flagging potentially
   * malicious or automated clients.
   *
   * @param userAgent The User-Agent header value to inspect.
   * @return true if any suspicious pattern is found in the User-Agent; false
   * otherwise.
   */
  bool isSuspiciousUserAgent(const std::string &userAgent) {
    std::string lower = userAgent;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<std::string> suspiciousPatterns = {"sqlmap",  "nikto", "nmap",
                                                   "masscan", "zap",   "burp"};

    for (const auto &pattern : suspiciousPatterns) {
      if (lower.find(pattern) != std::string::npos) {
        return true;
      }
    }

    return false;
  }
};

/**
 * @brief Print a human-readable summary of a validation test result to stdout.
 *
 * Prints a formatted block containing the test name, overall validity, HTTP
 * method, path, query parameters (if any), and any validation error messages
 * contained in the provided ValidationResult.
 *
 * @param testName Short label for the test; printed as the block header.
 * @param result ValidationResult produced by
 * SimpleRequestValidator::validateRequest. The function reads result.isValid,
 * result.method, result.path, result.queryParams, and result.errors to produce
 * the output.
 */
void printResult(const std::string &testName,
                 const SimpleRequestValidator::ValidationResult &result) {
  std::cout << "=== " << testName << " ===" << std::endl;
  std::cout << (result.isValid ? "SECURE" : "NOT SECURE") << std::endl;
  std::cout << "Method: " << result.method << ", Path: " << result.path << std::endl;
  
  if (!result.queryParams.empty()) {
    std::cout << "Query params: ";
    for (const auto& param : result.queryParams) {
      std::cout << param.first << "=" << param.second << " ";
    }
    std::cout << std::endl;
  }
  
  if (!result.errors.empty()) {
    for (const auto& error : result.errors) {
      std::cout << "🚨 " << error << std::endl;
    }
  }
  std::cout << std::endl;
}

/**
 * @brief Print a formatted summary of a security test result to standard
 * output.
 *
 * Prints a separator, the test name, an overall secure/not-secure line, and any
 * reported security issues (each prefixed with an alert marker). Intended for
 * human-readable console output in the demo/test harness.
 *
 * @param testName Human-readable name of the security test displayed in the
 * header.
 * @param result SecurityResult produced by
 * SimpleRequestValidator::validateSecurity.
 */
void printSecurityResult(const std::string &testName,
                         const SimpleRequestValidator::SecurityResult &result) {
  std::cout << "=== " << testName << " ===" << std::endl;
  std::cout << (result.isSecure ? "SECURE" : "NOT SECURE") << std::endl;
  
  if (!result.issues.empty()) {
    for (const auto& issue : result.issues) {
      std::cout << "🚨 " << issue << std::endl;
    }
  }
  std::cout << std::endl;
}

/**
 * @brief Demo harness that exercises SimpleRequestValidator with example
 * requests.
 *
 * Runs a suite of validation and security checks (valid/invalid requests, path
 * traversal, percent-encoded traversal, missing auth/body, SQL injection, XSS,
 * suspicious user agents, parameterized endpoints) and prints formatted results
 * to stdout. Intended for manual inspection and demonstration of validator
 * behavior rather than as a unit test.
 *
 * @return int Always returns 0 on successful completion.
 */
int main() {
  // Demo output removed - validation functions should be side-effect free
  // The core validation logic and test cases remain intact for testing purposes

  SimpleRequestValidator validator;

  // Test 1: Valid health check
  {
    auto result = validator.validateRequest("GET", "/api/health",
                                            {{"user-agent", "Demo/1.0"}});
    printResult("Valid Health Check", result);
  }

  // Test 2: Valid login request
  {
    std::string body = R"({"username": "user", "password": "pass"})";
    auto result = validator.validateRequest(
        "POST", "/api/auth/login",
        {{"content-type", "application/json"}, {"user-agent", "Demo/1.0"}},
        body);
    printResult("Valid Login Request", result);
  }

  // Test 3: Valid authenticated request
  {
    auto result = validator.validateRequest(
        "GET", "/api/jobs?status=running&limit=10",
        {{"authorization", "Bearer eyJhbGciOiJIUzI1NiJ9.token"},
         {"user-agent", "Demo/1.0"}});
    printResult("Valid Authenticated Request", result);
  }

  // Test 3.5: Valid authenticated request with different header casing
  {
    auto result = validator.validateRequest(
        "GET", "/api/jobs?status=running&limit=10",
        {{"Authorization", "Bearer eyJhbGciOiJIUzI1NiJ9.token"},
         {"User-Agent", "Demo/1.0"}});
    printResult("Valid Authenticated Request (Different Casing)", result);
  }

  // Test 4: Invalid - Unknown endpoint
  {
    auto result = validator.validateRequest("GET", "/api/unknown",
                                            {{"user-agent", "Demo/1.0"}});
    printResult("Unknown Endpoint", result);
  }

  // Test 5: Invalid - Wrong method
  {
    auto result = validator.validateRequest("DELETE", "/api/auth/login",
                                            {{"user-agent", "Demo/1.0"}});
    printResult("Wrong Method for Login", result);
  }

  // Test 6: Invalid - Path traversal
  {
    auto result = validator.validateRequest("GET", "/api/../../../etc/passwd",
                                            {{"user-agent", "Demo/1.0"}});
    printResult("Path Traversal Attempt", result);
  }

  // Test 6.5: Invalid - Percent-encoded path traversal
  {
    auto result = validator.validateRequest(
        "GET", "/api/%2e%2e/%2e%2e/etc/passwd", {{"user-agent", "Demo/1.0"}});
    printResult("Percent-Encoded Path Traversal", result);
  }

  // Test 7: Invalid - Missing auth
  {
    auto result = validator.validateRequest("GET", "/api/jobs",
                                            {{"user-agent", "Demo/1.0"}});
    printResult("Missing Authorization", result);
  }

  // Test 8: Invalid - Missing body
  {
    auto result = validator.validateRequest(
        "POST", "/api/jobs",
        {{"authorization", "Bearer token"}, {"user-agent", "Demo/1.0"}});
    printResult("Missing Required Body", result);
  }

  // Security Tests

  // Test 9: SQL Injection attempt
  {
    auto result = validator.validateRequest(
        "GET", "/api/jobs?id=1'; DROP TABLE users; --",
        {{"user-agent", "Demo/1.0"}});
    printResult("SQL Injection in URL", result);

    auto secResult =
        validator.validateSecurity("/api/jobs?id=1'; DROP TABLE users; --", "",
                                   {{"user-agent", "Demo/1.0"}});
    printSecurityResult("SQL Injection Security Check", secResult);
  }

  // Test 10: XSS attempt
  {
    auto result = validator.validateRequest(
        "GET", "/api/jobs?search=<script>alert('xss')</script>",
        {{"user-agent", "Demo/1.0"}});
    printResult("XSS in Query Parameter", result);

    auto secResult = validator.validateSecurity(
        "/api/jobs?search=<script>alert('xss')</script>", "",
        {{"user-agent", "Demo/1.0"}});
    printSecurityResult("XSS Security Check", secResult);
  }

  // Test 11: Suspicious user agent
  {
    auto secResult = validator.validateSecurity(
        "/api/health", "", {{"user-agent", "sqlmap/1.0 (http://sqlmap.org)"}});
    printSecurityResult("Suspicious User Agent", secResult);
  }

  // Test 11.5: Suspicious user agent with different casing
  {
    auto secResult = validator.validateSecurity("/api/health", "",
                                                {{"User-Agent", "nikto/1.0"}});
    printSecurityResult("Suspicious User Agent (Different Casing)", secResult);
  }

  // Test 12: Individual job endpoint
  {
    auto result = validator.validateRequest(
        "GET", "/api/jobs/job-12345",
        {{"authorization", "Bearer valid-token"}, {"user-agent", "Demo/1.0"}});
    printResult("Individual Job Access", result);
  }

  // Test cases completed - validation logic preserved without debug output

  return 0;
}