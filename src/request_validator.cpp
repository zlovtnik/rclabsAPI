#include "request_validator.hpp"
#include "component_logger.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <iomanip>
#include <mutex>

// Component logger specialization
template<> struct etl::ComponentTrait<RequestValidator> {
    static constexpr const char* name = "RequestValidator";
};

RequestValidator::RequestValidator(ValidationConfig config) 
    : config_(std::move(config)) {
    initializeKnownEndpoints();
    initializeAllowedMethods();
    etl::ComponentLogger<RequestValidator>::info("RequestValidator initialized with config");
}

RequestValidator::ValidationResult RequestValidator::validateRequest(
    const http::request<http::string_body>& req) {
    
    stats_.totalRequests++;
    
    etl::ComponentLogger<RequestValidator>::debug("Validating request: " + 
        std::string(req.method_string()) + " " + std::string(req.target()));
    
    ValidationResult result;
    
    // Step 1: Basic request structure validation
    auto basicResult = validateRequestBasics(req);
    if (!basicResult.isValid) {
        result.errors.insert(result.errors.end(), 
                           basicResult.errors.begin(), basicResult.errors.end());
        result.isValid = false;
        stats_.invalidRequests++;
        return result;
    }
    
    // Copy extracted data from basic validation
    result.headers = std::move(basicResult.headers);
    result.queryParams = std::move(basicResult.queryParams);
    result.extractedPath = std::move(basicResult.extractedPath);
    result.method = std::move(basicResult.method);
    
    // Step 2: Security validation
    auto securityResult = validateSecurity(req);
    if (!securityResult.isSecure) {
        for (const auto& issue : securityResult.securityIssues) {
            result.addError("security", issue, "SECURITY_VIOLATION");
        }
        stats_.securityViolations++;
    }
    
    if (securityResult.rateLimitExceeded) {
        result.addError("rate_limit", "Rate limit exceeded", "RATE_LIMIT_EXCEEDED");
        stats_.rateLimitViolations++;
    }
    
    // Step 3: Endpoint-specific validation
    std::string target = std::string(req.target());
    if (target.find('?') != std::string::npos) {
        target = target.substr(0, target.find('?'));
    }
    
    if (target.rfind("/api/auth", 0) == 0) {
        auto authResult = validateAuthEndpoint(req);
        if (!authResult.isValid) {
            result.errors.insert(result.errors.end(), 
                               authResult.errors.begin(), authResult.errors.end());
            result.isValid = false;
        }
    } else if (target.rfind("/api/jobs", 0) == 0) {
        auto jobsResult = validateJobsEndpoint(req);
        if (!jobsResult.isValid) {
            result.errors.insert(result.errors.end(), 
                               jobsResult.errors.begin(), jobsResult.errors.end());
            result.isValid = false;
        }
    } else if (target.rfind("/api/logs", 0) == 0) {
        auto logsResult = validateLogsEndpoint(req);
        if (!logsResult.isValid) {
            result.errors.insert(result.errors.end(), 
                               logsResult.errors.begin(), logsResult.errors.end());
            result.isValid = false;
        }
    } else if (target.rfind("/api/monitor", 0) == 0) {
        auto monitorResult = validateMonitoringEndpoint(req);
        if (!monitorResult.isValid) {
            result.errors.insert(result.errors.end(), 
                               monitorResult.errors.begin(), monitorResult.errors.end());
            result.isValid = false;
        }
    } else if (target == "/api/health" || target == "/api/status" || 
               target.rfind("/api/health/", 0) == 0) {
        auto healthResult = validateHealthEndpoint(req);
        if (!healthResult.isValid) {
            result.errors.insert(result.errors.end(), 
                               healthResult.errors.begin(), healthResult.errors.end());
            result.isValid = false;
        }
    } else if (!isKnownEndpoint(target)) {
        result.addError("endpoint", "Unknown endpoint: " + target, "UNKNOWN_ENDPOINT");
    }
    
    if (result.isValid) {
        stats_.validRequests++;
        etl::ComponentLogger<RequestValidator>::debug("Request validation successful");
    } else {
        stats_.invalidRequests++;
        etl::ComponentLogger<RequestValidator>::warn("Request validation failed with " + 
            std::to_string(result.errors.size()) + " errors");
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateRequestBasics(
    const http::request<http::string_body>& req) {
    
    ValidationResult result;
    
    // Extract basic information
    result.method = std::string(req.method_string());
    std::string target = std::string(req.target());
    
    // Validate HTTP method
    if (result.method.empty()) {
        result.addError("method", "HTTP method is required", "MISSING_METHOD");
        return result;
    }
    
    // Validate target path
    if (target.empty()) {
        result.addError("path", "Request path is required", "MISSING_PATH");
        return result;
    }
    
    if (target.length() > config_.maxPathLength) {
        result.addError("path", "Request path too long", "PATH_TOO_LONG");
        return result;
    }
    
    // Extract path without query parameters
    size_t queryPos = target.find('?');
    result.extractedPath = (queryPos != std::string::npos) ? 
        target.substr(0, queryPos) : target;
    
    // Validate path format
    auto pathResult = validatePath(result.extractedPath);
    if (!pathResult.isValid) {
        result.errors.insert(result.errors.end(), 
                           pathResult.errors.begin(), pathResult.errors.end());
        result.isValid = false;
    }
    
    // Extract and validate headers
    result.headers = extractHeaders(req);
    auto headerResult = validateHeaders(req);
    if (!headerResult.isValid) {
        result.errors.insert(result.errors.end(), 
                           headerResult.errors.begin(), headerResult.errors.end());
        result.isValid = false;
    }
    
    // Extract and validate query parameters
    if (queryPos != std::string::npos) {
        result.queryParams = extractQueryParams(target);
        auto queryResult = validateQueryParameters(target);
        if (!queryResult.isValid) {
            result.errors.insert(result.errors.end(), 
                               queryResult.errors.begin(), queryResult.errors.end());
            result.isValid = false;
        }
    }
    
    // Validate content length
    auto contentLengthResult = validateContentLength(req.body().size());
    if (!contentLengthResult.isValid) {
        result.errors.insert(result.errors.end(), 
                           contentLengthResult.errors.begin(), contentLengthResult.errors.end());
        result.isValid = false;
    }
    
    return result;
}

RequestValidator::SecurityValidationResult RequestValidator::validateSecurity(
    const http::request<http::string_body>& req) {
    
    SecurityValidationResult result;
    
    // Extract client information
    result.clientIp = extractClientIp(req);
    result.userAgent = extractUserAgent(req);
    
    // Check rate limiting
    if (config_.maxRequestsPerMinute > 0) {
        result.rateLimitExceeded = !checkRateLimit(result.clientIp);
        if (result.rateLimitExceeded) {
            etl::ComponentLogger<RequestValidator>::warn("Rate limit exceeded for IP: " + 
                sanitizeLogString(result.clientIp));
        }
    }
    
    // Check HTTPS requirement
    if (config_.requireHttps && !validateHttpsRequirement(req)) {
        result.addIssue("HTTPS required but request is not secure");
    }
    
    // Check for SQL injection attempts
    if (config_.enableSqlInjectionProtection) {
        std::string target = std::string(req.target());
        if (checkForSqlInjection(target) || checkForSqlInjection(req.body())) {
            result.addIssue("Potential SQL injection attempt detected");
            etl::ComponentLogger<RequestValidator>::error("SQL injection attempt from IP: " + 
                sanitizeLogString(result.clientIp));
        }
    }
    
    // Check for XSS attempts
    if (config_.enableXssProtection) {
        std::string target = std::string(req.target());
        if (checkForXssAttempts(target) || checkForXssAttempts(req.body())) {
            result.addIssue("Potential XSS attempt detected");
            etl::ComponentLogger<RequestValidator>::error("XSS attempt from IP: " + 
                sanitizeLogString(result.clientIp));
        }
    }
    
    return result;
}

std::unordered_map<std::string, std::string>
RequestValidator::extractHeaders(const http::request<http::string_body>& req) {
    
    std::unordered_map<std::string, std::string> headers;
    
    for (auto const& field : req) {
        try {
            std::string name = std::string(field.name_string());
            std::string value = std::string(field.value());
            
            // Convert to lowercase for case-insensitive lookup
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            headers[name] = value;
        } catch (const std::exception& e) {
            etl::ComponentLogger<RequestValidator>::warn("Failed to extract header: " + 
                std::string(e.what()));
        }
    }
    
    return headers;
}

std::unordered_map<std::string, std::string>
RequestValidator::extractQueryParams(std::string_view target) {
    
    std::unordered_map<std::string, std::string> params;
    
    size_t queryPos = target.find('?');
    if (queryPos == std::string::npos) {
        return params;
    }
    
    std::string queryString = std::string(target.substr(queryPos + 1));
    std::istringstream iss(queryString);
    std::string pair;
    
    while (std::getline(iss, pair, '&')) {
        size_t equalPos = pair.find('=');
        if (equalPos != std::string::npos) {
            std::string key = pair.substr(0, equalPos);
            std::string value = pair.substr(equalPos + 1);
            
            // URL decode (basic implementation)
            // In production, use a proper URL decoding library
            params[key] = value;
        }
    }
    
    return params;
}

RequestValidator::ValidationResult RequestValidator::validatePath(std::string_view path) {
    ValidationResult result;
    
    if (path.empty()) {
        result.addError("path", "Path cannot be empty", "EMPTY_PATH");
        return result;
    }
    
    if (path[0] != '/') {
        result.addError("path", "Path must start with '/'", "INVALID_PATH_FORMAT");
    }
    
    // Check for path traversal attempts
    if (path.find("..") != std::string::npos) {
        result.addError("path", "Path traversal not allowed", "PATH_TRAVERSAL");
    }
    
    // Check for null bytes
    if (path.find('\0') != std::string::npos) {
        result.addError("path", "Null bytes not allowed in path", "NULL_BYTE_IN_PATH");
    }
    
    // Validate path characters
    static const std::regex validPathRegex(R"(^[a-zA-Z0-9/_\-\.?&=]+$)");
    if (!std::regex_match(std::string(path), validPathRegex)) {
        result.addError("path", "Path contains invalid characters", "INVALID_PATH_CHARS");
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateHeaders(
    const http::request<http::string_body>& req) {
    
    ValidationResult result;
    size_t headerCount = 0;
    
    for (auto const& field : req) {
        headerCount++;
        
        if (headerCount > config_.maxHeaderCount) {
            result.addError("headers", "Too many headers", "TOO_MANY_HEADERS");
            break;
        }
        
        std::string name = std::string(field.name_string());
        std::string value = std::string(field.value());
        
        if (name.length() + value.length() > config_.maxHeaderSize) {
            result.addError("headers", "Header too large: " + name, "HEADER_TOO_LARGE");
        }
        
        if (!isValidHeaderName(name)) {
            result.addError("headers", "Invalid header name: " + name, "INVALID_HEADER_NAME");
        }
        
        if (!isValidHeaderValue(value)) {
            result.addError("headers", "Invalid header value for: " + name, "INVALID_HEADER_VALUE");
        }
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateQueryParameters(std::string_view target) {
    ValidationResult result;
    
    size_t queryPos = target.find('?');
    if (queryPos == std::string::npos) {
        return result; // No query parameters is valid
    }
    
    std::string queryString = std::string(target.substr(queryPos + 1));
    std::istringstream iss(queryString);
    std::string pair;
    size_t paramCount = 0;
    
    while (std::getline(iss, pair, '&')) {
        paramCount++;
        
        if (paramCount > config_.maxQueryParamCount) {
            result.addError("query", "Too many query parameters", "TOO_MANY_PARAMS");
            break;
        }
        
        size_t equalPos = pair.find('=');
        if (equalPos == std::string::npos) {
            result.addError("query", "Invalid query parameter format: " + pair, "INVALID_PARAM_FORMAT");
            continue;
        }
        
        std::string key = pair.substr(0, equalPos);
        std::string value = pair.substr(equalPos + 1);
        
        if (key.empty()) {
            result.addError("query", "Empty parameter name", "EMPTY_PARAM_NAME");
        }
        
        // Check for security issues in parameters
        if (config_.enableSqlInjectionProtection && 
            (checkForSqlInjection(key) || checkForSqlInjection(value))) {
            result.addError("query", "Potential SQL injection in parameter: " + key, "SQL_INJECTION");
        }
        
        if (config_.enableXssProtection && 
            (checkForXssAttempts(key) || checkForXssAttempts(value))) {
            result.addError("query", "Potential XSS in parameter: " + key, "XSS_ATTEMPT");
        }
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateContentLength(size_t contentLength) {
    ValidationResult result;
    
    if (contentLength > config_.maxRequestSize) {
        result.addError("content_length", "Request body too large", "REQUEST_TOO_LARGE");
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateAuthEndpoint(
    const http::request<http::string_body>& req) {
    
    ValidationResult result;
    std::string target = std::string(req.target());
    std::string method = std::string(req.method_string());
    
    // Remove query parameters for endpoint matching
    if (size_t queryPos = target.find('?'); queryPos != std::string::npos) {
        target = target.substr(0, queryPos);
    }
    
    if (target == "/api/auth/login") {
        if (method != "POST") {
            result.addError("method", "Login endpoint requires POST method", "INVALID_METHOD");
        }
        
        if (!req.body().empty()) {
            auto bodyResult = InputValidator::validateLoginRequest(req.body());
            if (!bodyResult.isValid) {
                for (const auto& error : bodyResult.errors) {
                    result.addError(error.field, error.message, error.code);
                }
            }
        } else {
            result.addError("body", "Login request requires body", "MISSING_BODY");
        }
    } else if (target == "/api/auth/logout") {
        if (method != "POST") {
            result.addError("method", "Logout endpoint requires POST method", "INVALID_METHOD");
        }
        
        // Validate authorization header for logout
        auto headers = extractHeaders(req);
        if (auto authIt = headers.find("authorization"); authIt != headers.end()) {
            auto authResult = validateAuthenticationHeader(authIt->second);
            if (!authResult.isValid) {
                result.errors.insert(result.errors.end(), 
                                   authResult.errors.begin(), authResult.errors.end());
                result.isValid = false;
            }
        } else {
            result.addError("authorization", "Authorization header required for logout", "MISSING_AUTH");
        }
    } else if (target == "/api/auth/profile") {
        if (method != "GET") {
            result.addError("method", "Profile endpoint requires GET method", "INVALID_METHOD");
        }
        
        // Validate authorization header for profile access
        auto headers = extractHeaders(req);
        if (auto authIt = headers.find("authorization"); authIt != headers.end()) {
            auto authResult = validateAuthenticationHeader(authIt->second);
            if (!authResult.isValid) {
                result.errors.insert(result.errors.end(), 
                                   authResult.errors.begin(), authResult.errors.end());
                result.isValid = false;
            }
        } else {
            result.addError("authorization", "Authorization header required for profile", "MISSING_AUTH");
        }
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateJobsEndpoint(
    const http::request<http::string_body>& req) {
    
    ValidationResult result;
    std::string target = std::string(req.target());
    std::string method = std::string(req.method_string());
    
    // Remove query parameters for endpoint matching
    std::string path = target;
    if (size_t queryPos = target.find('?'); queryPos != std::string::npos) {
        path = target.substr(0, queryPos);
    }
    
    if (path == "/api/jobs") {
        if (method == "GET") {
            // Validate query parameters for job listing
            auto queryParams = extractQueryParams(target);
            auto queryResult = InputValidator::validateJobQueryParams(queryParams);
            if (!queryResult.isValid) {
                for (const auto& error : queryResult.errors) {
                    result.addError(error.field, error.message, error.code);
                }
            }
        } else if (method == "POST") {
            // Validate job creation request
            if (!req.body().empty()) {
                auto bodyResult = InputValidator::validateJobCreationRequest(req.body());
                if (!bodyResult.isValid) {
                    for (const auto& error : bodyResult.errors) {
                        result.addError(error.field, error.message, error.code);
                    }
                }
            } else {
                result.addError("body", "Job creation requires body", "MISSING_BODY");
            }
        } else {
            result.addError("method", "Jobs endpoint supports GET and POST methods only", "INVALID_METHOD");
        }
    } else if (path.find("/api/jobs/") == 0) {
        // Individual job operations
        std::string jobId = extractJobIdFromPath(path, "/api/jobs/", "");
        if (jobId.empty()) {
            result.addError("job_id", "Invalid job ID in path", "INVALID_JOB_ID");
        } else if (!InputValidator::isValidJobId(jobId)) {
            result.addError("job_id", "Job ID format is invalid", "INVALID_JOB_ID_FORMAT");
        }
        
        if (method == "GET") {
            // Job details - no additional validation needed
        } else if (method == "PUT" || method == "PATCH") {
            // Job update
            if (!req.body().empty()) {
                auto bodyResult = InputValidator::validateJobUpdateRequest(req.body());
                if (!bodyResult.isValid) {
                    for (const auto& error : bodyResult.errors) {
                        result.addError(error.field, error.message, error.code);
                    }
                }
            } else {
                result.addError("body", "Job update requires body", "MISSING_BODY");
            }
        } else if (method == "DELETE") {
            // Job deletion - no additional validation needed
        } else {
            result.addError("method", "Invalid method for job operations", "INVALID_METHOD");
        }
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateLogsEndpoint(
    const http::request<http::string_body>& req) {
    
    ValidationResult result;
    std::string target = std::string(req.target());
    std::string method = std::string(req.method_string());
    
    if (method != "GET") {
        result.addError("method", "Logs endpoint only supports GET method", "INVALID_METHOD");
        return result;
    }
    
    // Validate query parameters for log filtering
    auto queryParams = extractQueryParams(target);
    
    // Validate log level if provided
    if (auto levelIt = queryParams.find("level"); levelIt != queryParams.end()) {
        static const std::unordered_set<std::string> validLevels = {
            "debug", "info", "warn", "error", "critical"
        };
        if (validLevels.find(levelIt->second) == validLevels.end()) {
            result.addError("level", "Invalid log level: " + levelIt->second, "INVALID_LOG_LEVEL");
        }
    }
    
    // Validate limit parameter
    if (auto limitIt = queryParams.find("limit"); limitIt != queryParams.end()) {
        try {
            int limit = std::stoi(limitIt->second);
            if (limit < 1 || limit > 10000) {
                result.addError("limit", "Limit must be between 1 and 10000", "INVALID_LIMIT");
            }
        } catch (const std::exception&) {
            result.addError("limit", "Limit must be a valid number", "INVALID_LIMIT_FORMAT");
        }
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateMonitoringEndpoint(
    const http::request<http::string_body>& req) {
    
    ValidationResult result;
    std::string method = std::string(req.method_string());
    
    if (method != "GET") {
        result.addError("method", "Monitoring endpoints only support GET method", "INVALID_METHOD");
        return result;
    }
    
    // Validate query parameters for monitoring
    auto queryParams = extractQueryParams(std::string(req.target()));
    auto queryResult = InputValidator::validateMonitoringParams(queryParams);
    if (!queryResult.isValid) {
        for (const auto& error : queryResult.errors) {
            result.addError(error.field, error.message, error.code);
        }
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateHealthEndpoint(
    const http::request<http::string_body>& req) {
    
    ValidationResult result;
    std::string method = std::string(req.method_string());
    
    if (method != "GET") {
        result.addError("method", "Health endpoint only supports GET method", "INVALID_METHOD");
        return result;
    }
    
    // Health endpoint has minimal validation requirements
    return result;
}

bool RequestValidator::checkRateLimit(const std::string& clientIp) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    
    auto now = std::chrono::system_clock::now();
    auto& timestamps = rateLimitMap_[clientIp];
    
    // Remove timestamps older than 1 minute
    timestamps.erase(
        std::remove_if(timestamps.begin(), timestamps.end(),
            [now](const auto& timestamp) {
                return std::chrono::duration_cast<std::chrono::minutes>(now - timestamp).count() >= 1;
            }),
        timestamps.end()
    );
    
    // Check if rate limit is exceeded
    if (timestamps.size() >= config_.maxRequestsPerMinute) {
        return false;
    }
    
    // Add current timestamp
    timestamps.push_back(now);
    return true;
}

bool RequestValidator::checkForSqlInjection(const std::string& input) {
    static const std::vector<std::string> sqlKeywords = {
        "select", "insert", "update", "delete", "drop", "create", "alter",
        "union", "exec", "execute", "script", "javascript", "vbscript",
        "onload", "onerror", "onclick", "onmouseover", "onfocus", "onblur",
        "eval", "expression", "url", "link", "import", "include"
    };
    
    std::string lowerInput = input;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
    
    for (const auto& keyword : sqlKeywords) {
        if (lowerInput.find(keyword) != std::string::npos) {
            return true;
        }
    }
    
    // Check for common SQL injection patterns
    static const std::vector<std::regex> sqlPatterns = {
        std::regex(R"(\b(union|select|insert|update|delete)\b.*\b(from|where|into)\b)", std::regex::icase),
        std::regex(R"((\%27)|(\')|(\\x27)|(\\u0027))", std::regex::icase),
        std::regex(R"((\%3D)|(=)[^\n]*((\%27)|(\')|(\\x27)|(\\u0027)))", std::regex::icase),
        std::regex(R"(\w*((\%27)|(\'))((\%6F)|o|(\%4F))((\%72)|r|(\%52)))", std::regex::icase)
    };
    
    for (const auto& pattern : sqlPatterns) {
        if (std::regex_search(input, pattern)) {
            return true;
        }
    }
    
    return false;
}

bool RequestValidator::checkForXssAttempts(const std::string& input) {
    static const std::vector<std::string> xssPatterns = {
        "<script", "</script>", "javascript:", "vbscript:", "onload=", "onerror=",
        "onclick=", "onmouseover=", "onfocus=", "onblur=", "eval(", "expression(",
        "url(", "import(", "document.cookie", "document.write", "innerHTML",
        "outerHTML", "document.location", "window.location"
    };
    
    std::string lowerInput = input;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
    
    for (const auto& pattern : xssPatterns) {
        if (lowerInput.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

std::string RequestValidator::extractClientIp(const http::request<http::string_body>& req) {
    auto headers = extractHeaders(req);
    
    // Check for forwarded IP headers (in order of preference)
    static const std::vector<std::string> ipHeaders = {
        "x-forwarded-for", "x-real-ip", "x-client-ip", "cf-connecting-ip"
    };
    
    for (const auto& header : ipHeaders) {
        if (auto it = headers.find(header); it != headers.end()) {
            std::string ip = it->second;
            // Take the first IP if there are multiple (comma-separated)
            if (size_t commaPos = ip.find(','); commaPos != std::string::npos) {
                ip = ip.substr(0, commaPos);
            }
            // Trim whitespace
            ip.erase(0, ip.find_first_not_of(" \t"));
            ip.erase(ip.find_last_not_of(" \t") + 1);
            if (!ip.empty()) {
                return ip;
            }
        }
    }
    
    return "unknown";
}

std::string RequestValidator::extractUserAgent(const http::request<http::string_body>& req) {
    auto headers = extractHeaders(req);
    if (auto it = headers.find("user-agent"); it != headers.end()) {
        return it->second;
    }
    return "unknown";
}

void RequestValidator::initializeKnownEndpoints() {
    knownEndpoints_ = {
        "/api/auth/login",
        "/api/auth/logout", 
        "/api/auth/profile",
        "/api/jobs",
        "/api/logs",
        "/api/monitor/jobs",
        "/api/monitor/status",
        "/api/monitor/metrics",
        "/api/health",
        "/api/status"
    };
}

void RequestValidator::initializeAllowedMethods() {
    allowedMethodsPerEndpoint_["/api/auth/login"] = {"POST"};
    allowedMethodsPerEndpoint_["/api/auth/logout"] = {"POST"};
    allowedMethodsPerEndpoint_["/api/auth/profile"] = {"GET"};
    allowedMethodsPerEndpoint_["/api/jobs"] = {"GET", "POST"};
    allowedMethodsPerEndpoint_["/api/logs"] = {"GET"};
    allowedMethodsPerEndpoint_["/api/monitor/jobs"] = {"GET"};
    allowedMethodsPerEndpoint_["/api/monitor/status"] = {"GET"};
    allowedMethodsPerEndpoint_["/api/monitor/metrics"] = {"GET"};
    allowedMethodsPerEndpoint_["/api/health"] = {"GET"};
    allowedMethodsPerEndpoint_["/api/status"] = {"GET"};
}

bool RequestValidator::isKnownEndpoint(const std::string& path) {
    // Check exact matches first
    if (knownEndpoints_.find(path) != knownEndpoints_.end()) {
        return true;
    }
    
    // Check parameterized endpoints
    if (path.find("/api/jobs/") == 0 && path.length() > 10) {
        return true; // Individual job endpoints
    }
    
    if (path.find("/api/health/") == 0) {
        return true; // Health sub-endpoints
    }
    
    return false;
}

bool RequestValidator::isValidHeaderName(const std::string& name) {
    if (name.empty()) return false;
    
    // HTTP header names should contain only ASCII letters, digits, and hyphens
    static const std::regex headerNameRegex(R"(^[a-zA-Z0-9\-]+$)");
    return std::regex_match(name, headerNameRegex);
}

bool RequestValidator::isValidHeaderValue(const std::string& value) {
    // Header values should not contain control characters (except tab)
    for (char c : value) {
        if (c < 32 && c != 9) { // Allow tab (9) but not other control chars
            return false;
        }
    }
    return true;
}

std::string RequestValidator::sanitizeLogString(const std::string& input) {
    std::string result = input;
    
    // Replace control characters with spaces
    for (char& c : result) {
        if (c < 32 && c != 9 && c != 10 && c != 13) {
            c = ' ';
        }
    }
    
    // Truncate if too long
    if (result.length() > 200) {
        result = result.substr(0, 197) + "...";
    }
    
    return result;
}

std::string RequestValidator::ValidationResult::toJsonString() const {
    std::ostringstream oss;
    oss << "{\"valid\":" << (isValid ? "true" : "false");
    
    if (!errors.empty()) {
        oss << ",\"errors\":[";
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"field\":\"" << errors[i].field 
                << "\",\"message\":\"" << errors[i].message
                << "\",\"code\":\"" << errors[i].code << "\"}";
        }
        oss << "]";
    }
    
    oss << "}";
    return oss.str();
}

RequestValidator::ValidationResult RequestValidator::validateAuthenticationHeader(
    const std::string& authHeader) {
    
    ValidationResult result;
    
    if (authHeader.empty()) {
        result.addError("authorization", "Authorization header is empty", "EMPTY_AUTH_HEADER");
        return result;
    }
    
    // Check for Bearer token format
    if (authHeader.find("Bearer ") == 0) {
        std::string token = authHeader.substr(7); // Remove "Bearer " prefix
        auto tokenResult = validateBearerToken(token);
        if (!tokenResult.isValid) {
            result.errors.insert(result.errors.end(), 
                               tokenResult.errors.begin(), tokenResult.errors.end());
            result.isValid = false;
        }
    } else {
        result.addError("authorization", "Invalid authorization format", "INVALID_AUTH_FORMAT");
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateBearerToken(const std::string& token) {
    ValidationResult result;
    
    if (token.empty()) {
        result.addError("token", "Bearer token is empty", "EMPTY_TOKEN");
        return result;
    }
    
    if (!InputValidator::isValidToken(token)) {
        result.addError("token", "Invalid token format", "INVALID_TOKEN_FORMAT");
    }
    
    return result;
}

std::string RequestValidator::extractJobIdFromPath(
    std::string_view target, std::string_view prefix, std::string_view suffix) {
    
    if (target.length() <= prefix.length() + suffix.length()) {
        return "";
    }
    
    if (target.substr(0, prefix.length()) != prefix) {
        return "";
    }
    
    size_t startPos = prefix.length();
    size_t endPos = target.length() - suffix.length();
    
    if (!suffix.empty() && target.substr(endPos) != suffix) {
        return "";
    }
    
    std::string jobId = std::string(target.substr(startPos, endPos - startPos));
    
    // Remove any query parameters
    if (size_t queryPos = jobId.find('?'); queryPos != std::string::npos) {
        jobId = jobId.substr(0, queryPos);
    }
    
    // Remove trailing slashes
    while (!jobId.empty() && jobId.back() == '/') {
        jobId.pop_back();
    }
    
    return jobId;
}

std::string RequestValidator::extractConnectionIdFromPath(
    const std::string& target, const std::string& prefix) {
    
    if (target.length() <= prefix.length()) {
        return "";
    }
    
    if (target.substr(0, prefix.length()) != prefix) {
        return "";
    }
    
    std::string connectionId = target.substr(prefix.length());
    
    // Remove any path after the connection ID
    if (size_t slashPos = connectionId.find('/'); slashPos != std::string::npos) {
        connectionId = connectionId.substr(0, slashPos);
    }
    
    // Remove any query parameters
    if (size_t queryPos = connectionId.find('?'); queryPos != std::string::npos) {
        connectionId = connectionId.substr(0, queryPos);
    }
    
    return connectionId;
}

bool RequestValidator::validateHttpsRequirement(const http::request<http::string_body>& req) {
    auto headers = extractHeaders(req);
    
    // Check for HTTPS indicators
    if (auto it = headers.find("x-forwarded-proto"); it != headers.end()) {
        return it->second == "https";
    }
    
    if (auto it = headers.find("x-forwarded-ssl"); it != headers.end()) {
        return it->second == "on";
    }
    
    // In a real implementation, you might also check the connection itself
    // For now, assume HTTP if no forwarded headers indicate HTTPS
    return false;
}

void RequestValidator::updateConfig(const ValidationConfig& newConfig) {
    config_ = newConfig;
    etl::ComponentLogger<RequestValidator>::info("RequestValidator configuration updated");
}

void RequestValidator::resetStats() {
    stats_ = ValidationStats{};
    etl::ComponentLogger<RequestValidator>::info("RequestValidator statistics reset");
}

bool RequestValidator::isValidMethod(const std::string& method, const std::string& endpoint) {
    auto it = allowedMethodsPerEndpoint_.find(endpoint);
    if (it == allowedMethodsPerEndpoint_.end()) {
        // For unknown endpoints, allow common HTTP methods
        static const std::unordered_set<std::string> commonMethods = {
            "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"
        };
        return commonMethods.find(method) != commonMethods.end();
    }
    
    return it->second.find(method) != it->second.end();
}

RequestValidator::ValidationResult RequestValidator::validateMethodForEndpoint(
    const std::string& method, const std::string& path) {
    
    ValidationResult result;
    
    if (!isValidMethod(method, path)) {
        result.addError("method", "Method " + method + " not allowed for endpoint " + path, 
                       "METHOD_NOT_ALLOWED");
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateEndpoint(
    const std::string& method, const std::string& path) {
    
    ValidationResult result;
    
    // Validate path format
    auto pathResult = validatePath(path);
    if (!pathResult.isValid) {
        result.errors.insert(result.errors.end(), 
                           pathResult.errors.begin(), pathResult.errors.end());
        result.isValid = false;
    }
    
    // Validate method for endpoint
    auto methodResult = validateMethodForEndpoint(method, path);
    if (!methodResult.isValid) {
        result.errors.insert(result.errors.end(), 
                           methodResult.errors.begin(), methodResult.errors.end());
        result.isValid = false;
    }
    
    // Check if endpoint is known
    if (!isKnownEndpoint(path)) {
        result.addError("endpoint", "Unknown endpoint: " + path, "UNKNOWN_ENDPOINT");
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateJsonBody(const std::string& body) {
    ValidationResult result;
    
    if (body.empty()) {
        result.addError("body", "JSON body is empty", "EMPTY_BODY");
        return result;
    }
    
    auto jsonResult = InputValidator::validateJson(body);
    if (!jsonResult.isValid) {
        for (const auto& error : jsonResult.errors) {
            result.addError(error.field, error.message, error.code);
        }
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateBody(
    const std::string& body, const std::string& contentType) {
    
    ValidationResult result;
    
    if (contentType.find("application/json") != std::string::npos) {
        return validateJsonBody(body);
    }
    
    // For other content types, perform basic validation
    if (body.length() > config_.maxRequestSize) {
        result.addError("body", "Request body too large", "BODY_TOO_LARGE");
    }
    
    // Check for security issues
    if (config_.enableSqlInjectionProtection && checkForSqlInjection(body)) {
        result.addError("body", "Potential SQL injection in body", "SQL_INJECTION");
    }
    
    if (config_.enableXssProtection && checkForXssAttempts(body)) {
        result.addError("body", "Potential XSS in body", "XSS_ATTEMPT");
    }
    
    return result;
}

RequestValidator::ValidationResult RequestValidator::validateContentType(
    const std::string& contentType, const std::string& endpoint) {
    
    ValidationResult result;
    
    if (contentType.empty()) {
        // Content-Type is not always required (e.g., for GET requests)
        return result;
    }
    
    // Define allowed content types per endpoint
    static const std::unordered_map<std::string, std::unordered_set<std::string>> allowedContentTypes = {
        {"/api/auth/login", {"application/json"}},
        {"/api/jobs", {"application/json"}},
    };
    
    auto it = allowedContentTypes.find(endpoint);
    if (it != allowedContentTypes.end()) {
        bool found = false;
        for (const auto& allowed : it->second) {
            if (contentType.find(allowed) != std::string::npos) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            result.addError("content_type", "Invalid content type for endpoint", "INVALID_CONTENT_TYPE");
        }
    }
    
    return result;
}

bool RequestValidator::validateCsrfToken(const std::string& token, const std::string& sessionId) {
    // In a real implementation, this would validate against stored CSRF tokens
    // For now, just check basic format
    return !token.empty() && !sessionId.empty() && token.length() >= 16;
}