#include "security_auditor.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace ETLPlus::Security {

SecurityAuditor::SecurityAuditor(const AuditConfig &config) : config_(config) {
  // Precompile regex patterns for dangerous functions
  try {
    for (const auto &func : dangerousFunctions_) {
      dangerousFunctionPatterns_.emplace_back("\\b" + func + "\\s*\\(",
                                             std::regex_constants::icase);
    }
  } catch (const std::regex_error &e) {
    std::cerr << "Failed to compile dangerous function regex patterns: " << e.what() << std::endl;
    // Continue with empty patterns - will result in no matches
  }
}

SecurityAuditor::AuditResult SecurityAuditor::performAudit() {
  AuditResult result;

  std::cout << "Starting comprehensive security audit..." << std::endl;

  if (config_.enableStaticAnalysis) {
    std::cout << "Performing static code analysis..." << std::endl;
    auto codeResult = analyzeCodeSecurity();
    result.criticalIssues.insert(result.criticalIssues.end(),
                                codeResult.criticalIssues.begin(),
                                codeResult.criticalIssues.end());
    result.highIssues.insert(result.highIssues.end(),
                            codeResult.highIssues.begin(),
                            codeResult.highIssues.end());
    result.mediumIssues.insert(result.mediumIssues.end(),
                              codeResult.mediumIssues.begin(),
                              codeResult.mediumIssues.end());
    result.lowIssues.insert(result.lowIssues.end(),
                           codeResult.lowIssues.begin(),
                           codeResult.lowIssues.end());
    result.informational.insert(result.informational.end(),
                               codeResult.informational.begin(),
                               codeResult.informational.end());
  }

  if (config_.enableDependencyScanning) {
    std::cout << "Scanning dependencies for vulnerabilities..." << std::endl;
    auto depResult = scanDependencies();
    result.criticalIssues.insert(result.criticalIssues.end(),
                                depResult.criticalIssues.begin(),
                                depResult.criticalIssues.end());
    result.highIssues.insert(result.highIssues.end(),
                            depResult.highIssues.begin(),
                            depResult.highIssues.end());
  }

  if (config_.enableConfigAudit) {
    std::cout << "Auditing configuration files..." << std::endl;
    auto configResult = auditConfiguration();
    result.mediumIssues.insert(result.mediumIssues.end(),
                              configResult.mediumIssues.begin(),
                              configResult.mediumIssues.end());
    result.lowIssues.insert(result.lowIssues.end(),
                           configResult.lowIssues.begin(),
                           configResult.lowIssues.end());
  }

  if (config_.enableCodeReview) {
    std::cout << "Performing automated code review..." << std::endl;
    auto reviewResult = performCodeReview();
    result.mediumIssues.insert(result.mediumIssues.end(),
                              reviewResult.mediumIssues.begin(),
                              reviewResult.mediumIssues.end());
    result.lowIssues.insert(result.lowIssues.end(),
                           reviewResult.lowIssues.begin(),
                           reviewResult.lowIssues.end());
  }

  // Update issue counts
  for (const auto &issue : result.criticalIssues) result.issueCounts["critical"]++;
  for (const auto &issue : result.highIssues) result.issueCounts["high"]++;
  for (const auto &issue : result.mediumIssues) result.issueCounts["medium"]++;
  for (const auto &issue : result.lowIssues) result.issueCounts["low"]++;
  for (const auto &issue : result.informational) result.issueCounts["info"]++;

  // Determine if audit passed based on severity threshold
  if (config_.severityThreshold == "critical" && !result.criticalIssues.empty()) {
    result.passed = false;
  } else if (config_.severityThreshold == "high" &&
             (!result.criticalIssues.empty() || !result.highIssues.empty())) {
    result.passed = false;
  } else if (config_.severityThreshold == "medium" &&
             (!result.criticalIssues.empty() || !result.highIssues.empty() ||
              !result.mediumIssues.empty())) {
    result.passed = false;
  }

  std::cout << "Security audit completed." << std::endl;
  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::analyzeCodeSecurity() {
  AuditResult result;

  auto filesToAnalyze = findFilesToAnalyze();

  for (const auto &filePath : filesToAnalyze) {
    auto fileResult = analyzeFile(filePath);

    result.criticalIssues.insert(result.criticalIssues.end(),
                                fileResult.criticalIssues.begin(),
                                fileResult.criticalIssues.end());
    result.highIssues.insert(result.highIssues.end(),
                            fileResult.highIssues.begin(),
                            fileResult.highIssues.end());
    result.mediumIssues.insert(result.mediumIssues.end(),
                              fileResult.mediumIssues.begin(),
                              fileResult.mediumIssues.end());
    result.lowIssues.insert(result.lowIssues.end(),
                           fileResult.lowIssues.begin(),
                           fileResult.lowIssues.end());
    result.informational.insert(result.informational.end(),
                               fileResult.informational.begin(),
                               fileResult.informational.end());
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::analyzeFile(const std::string &filePath) {
  AuditResult result;

  std::string content = readFileContent(filePath);
  if (content.empty()) {
    return result;
  }

  return analyzeSourceCode(content, filePath);
}

SecurityAuditor::AuditResult SecurityAuditor::analyzeSourceCode(const std::string &content,
                                                              const std::string &filePath) {
  AuditResult result;

  // Check for dangerous functions
  auto dangerousResult = checkForDangerousFunctions(content, filePath);
  result.criticalIssues.insert(result.criticalIssues.end(),
                              dangerousResult.criticalIssues.begin(),
                              dangerousResult.criticalIssues.end());
  result.highIssues.insert(result.highIssues.end(),
                          dangerousResult.highIssues.begin(),
                          dangerousResult.highIssues.end());

  // Check for hardcoded secrets
  auto secretsResult = checkForHardcodedSecrets(content, filePath);
  result.criticalIssues.insert(result.criticalIssues.end(),
                              secretsResult.criticalIssues.begin(),
                              secretsResult.criticalIssues.end());

  // Check for SQL injection vulnerabilities
  auto sqlResult = checkForSQLInjection(content, filePath);
  result.highIssues.insert(result.highIssues.end(),
                          sqlResult.highIssues.begin(),
                          sqlResult.highIssues.end());

  // Check for XSS vulnerabilities
  auto xssResult = checkForXSSVulnerabilities(content, filePath);
  result.mediumIssues.insert(result.mediumIssues.end(),
                            xssResult.mediumIssues.begin(),
                            xssResult.mediumIssues.end());

  // Check for insecure headers
  auto headerResult = checkForInsecureHeaders(content, filePath);
  result.mediumIssues.insert(result.mediumIssues.end(),
                            headerResult.mediumIssues.begin(),
                            headerResult.mediumIssues.end());

  // Check for weak cryptography
  auto cryptoResult = checkForWeakCryptography(content, filePath);
  result.mediumIssues.insert(result.mediumIssues.end(),
                            cryptoResult.mediumIssues.begin(),
                            cryptoResult.mediumIssues.end());

  // Check for path traversal
  auto pathResult = checkForPathTraversal(content, filePath);
  result.highIssues.insert(result.highIssues.end(),
                          pathResult.highIssues.begin(),
                          pathResult.highIssues.end());

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::checkForDangerousFunctions(const std::string &content,
                                                                       const std::string &filePath) {
  AuditResult result;

  // Use precompiled regex patterns for better performance
  for (size_t i = 0; i < dangerousFunctions_.size() && i < dangerousFunctionPatterns_.size(); ++i) {
    if (std::regex_search(content, dangerousFunctionPatterns_[i])) {
      result.addCritical("Dangerous function '" + dangerousFunctions_[i] + "' found in " + filePath +
                        " - consider using safer alternatives");
    }
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::checkForHardcodedSecrets(const std::string &content,
                                                                     const std::string &filePath) {
  AuditResult result;

  for (const auto &keyword : sensitiveKeywords_) {
    // Escape regex special characters in keyword
    std::string escapedKeyword = keyword;
    std::string specialChars = ".^$|()[]{}+*?\\";
    for (char c : specialChars) {
      size_t pos = 0;
      std::string charStr(1, c);
      std::string escaped = "\\" + charStr;
      while ((pos = escapedKeyword.find(charStr, pos)) != std::string::npos) {
        escapedKeyword.replace(pos, 1, escaped);
        pos += escaped.length();
      }
    }

    std::regex pattern(escapedKeyword + R"(\s*[=:]?\s*["'][^"']+["'])",
                      std::regex_constants::icase);
    if (std::regex_search(content, pattern)) {
      result.addCritical("Potential hardcoded " + keyword + " found in " + filePath +
                        " - move to environment variables or secure storage");
    }
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::checkForSQLInjection(const std::string &content,
                                                                 const std::string &filePath) {
  AuditResult result;

  // Check for string concatenation in SQL queries
  if (containsRegex(content, "SELECT.*\\+.*FROM|INSERT.*\\+.*INTO|UPDATE.*\\+.*SET")) {
    result.addHigh("Potential SQL injection vulnerability in " + filePath +
                  " - avoid string concatenation in SQL queries");
  }

  // Check for missing parameterized queries
  if (containsRegex(content, "execute.*SELECT|execute.*INSERT|execute.*UPDATE") &&
      !containsRegex(content, "parameter|bind")) {
    result.addHigh("SQL query without parameters detected in " + filePath +
                  " - use parameterized queries");
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::checkForXSSVulnerabilities(const std::string &content,
                                                                      const std::string &filePath) {
  AuditResult result;

  // Check for direct HTML output without escaping
  if (containsRegex(content, "response.*<<.*\\$") &&
      !containsRegex(content, "escape|encode")) {
    result.addMedium("Potential XSS vulnerability in " + filePath +
                    " - HTML output should be escaped");
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::checkForInsecureHeaders(const std::string &content,
                                                                    const std::string &filePath) {
  AuditResult result;

  for (const auto &header : insecureHeaders_) {
    if (containsRegex(content, header)) {
      result.addMedium("Insecure header '" + header + "' found in " + filePath +
                      " - consider removing or securing this header");
    }
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::checkForWeakCryptography(const std::string &content,
                                                                     const std::string &filePath) {
  AuditResult result;

  // Check for weak hash functions
  if (containsRegex(content, "MD5|SHA1")) {
    result.addMedium("Weak cryptographic hash function detected in " + filePath +
                    " - use SHA-256 or stronger");
  }

  // Check for ECB mode
  if (containsRegex(content, "ECB")) {
    result.addMedium("ECB cipher mode detected in " + filePath +
                    " - use CBC or GCM mode instead");
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::checkForPathTraversal(const std::string &content,
                                                                  const std::string &filePath) {
  AuditResult result;

  // Check for path traversal patterns (Unix and Windows)
  if (containsRegex(content, R"(\.\.(/|\\))")) {
    result.addHigh("Potential path traversal vulnerability in " + filePath +
                  " - validate and sanitize file paths");
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::scanDependencies() {
  AuditResult result;

  // Check for known vulnerable versions in CMakeLists.txt
  std::string cmakeContent = readFileContent("CMakeLists.txt");
  if (!cmakeContent.empty()) {
    // Check for old OpenSSL versions
    if (containsRegex(cmakeContent, R"(OpenSSL\s*1\.(?:0|1)\.[0-9]+)")) {
      result.addHigh("Potentially vulnerable OpenSSL version detected - upgrade to 1.1.1 or later");
    }

    // Check for old Boost versions
    if (containsRegex(cmakeContent, "Boost.*1\\.[0-6][0-9]")) {
      result.addMedium("Older Boost version detected - consider upgrading for security fixes");
    }
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::auditConfiguration() {
  AuditResult result;

  // Check config files for security issues
  std::vector<std::string> configFiles = {"config.json", "config/config.json"};

  for (const auto &configFile : configFiles) {
    std::string content = readFileContent(configFile);
    if (content.empty()) continue;

    // Check for debug mode in production
    if (containsRegex(content, "\"debug\"\\s*:\\s*true") ||
        containsRegex(content, "\"development\"\\s*:\\s*true")) {
      result.addMedium("Debug mode enabled in " + configFile +
                      " - disable for production deployment");
    }

    // Check for default passwords
    if (containsRegex(content, "password.*123|admin.*admin|root.*root")) {
      result.addLow("Default or weak password detected in " + configFile +
                   " - use strong passwords");
    }
  }

  return result;
}

SecurityAuditor::AuditResult SecurityAuditor::performCodeReview() {
  AuditResult result;

  auto filesToAnalyze = findFilesToAnalyze();

  for (const auto &filePath : filesToAnalyze) {
    std::string content = readFileContent(filePath);
    if (content.empty()) continue;

    // Check for TODO comments that might indicate security debt
    if (containsRegex(content, "TODO.*secur|FIXME.*secur|XXX.*secur")) {
      result.addLow("Security-related TODO/FIXME comment found in " + filePath);
    }

    // Check for commented-out security code
    if (containsRegex(content, "//.*password|//.*encrypt|//.*auth")) {
      result.addLow("Commented-out security code found in " + filePath +
                   " - review and remove if not needed");
    }

    // Check for large functions that might be hard to secure
    std::regex functionPattern(R"((void|bool|int|string|auto)\s+\w+\s*\([^)]*\)\s*\{)");
    auto functions = findMatches(content, functionPattern);
    if (functions.size() > 0) {
      // Count lines in functions (simplified check)
      size_t braceCount = 0;
      size_t lineCount = 0;
      for (char c : content) {
        if (c == '{') braceCount++;
        if (c == '\n') lineCount++;
      }

      if (lineCount > 1000 && braceCount > 10) {
        result.addLow("Large file with many functions in " + filePath +
                     " - consider breaking down for better security review");
      }
    }
  }

  return result;
}

std::string SecurityAuditor::generateReport(const AuditResult &result) {
  std::stringstream ss;

  ss << "=== Security Audit Report ===\n\n";
  ss << "Audit Status: " << (result.passed ? "PASSED" : "FAILED") << "\n\n";

  ss << "Issue Summary:\n";
  auto getCount = [&](const std::string& key) -> size_t {
    auto it = result.issueCounts.find(key);
    return it != result.issueCounts.end() ? it->second : 0;
  };
  ss << "Critical: " << getCount("critical") << "\n";
  ss << "High: " << getCount("high") << "\n";
  ss << "Medium: " << getCount("medium") << "\n";
  ss << "Low: " << getCount("low") << "\n";
  ss << "Informational: " << getCount("info") << "\n\n";

  if (!result.criticalIssues.empty()) {
    ss << "Critical Issues:\n";
    for (const auto &issue : result.criticalIssues) {
      ss << "  - " << issue << "\n";
    }
    ss << "\n";
  }

  if (!result.highIssues.empty()) {
    ss << "High Priority Issues:\n";
    for (const auto &issue : result.highIssues) {
      ss << "  - " << issue << "\n";
    }
    ss << "\n";
  }

  if (!result.mediumIssues.empty()) {
    ss << "Medium Priority Issues:\n";
    for (const auto &issue : result.mediumIssues) {
      ss << "  - " << issue << "\n";
    }
    ss << "\n";
  }

  if (!result.lowIssues.empty()) {
    ss << "Low Priority Issues:\n";
    for (const auto &issue : result.lowIssues) {
      ss << "  - " << issue << "\n";
    }
    ss << "\n";
  }

  if (!result.informational.empty()) {
    ss << "Informational:\n";
    for (const auto &info : result.informational) {
      ss << "  - " << info << "\n";
    }
    ss << "\n";
  }

  return ss.str();
}

std::vector<std::string> SecurityAuditor::findFilesToAnalyze() {
  std::vector<std::string> files;

  for (const auto &dir : config_.sourceDirectories) {
    if (!std::filesystem::exists(dir)) continue;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
      if (entry.is_regular_file()) {
        std::string path = entry.path().string();
        if (shouldAnalyzeFile(path)) {
          files.push_back(path);
        }
      }
    }
  }

  return files;
}

bool SecurityAuditor::shouldAnalyzeFile(const std::string &filePath) {
  // Check exclude patterns
  for (const auto &pattern : config_.excludePatterns) {
    if (filePath.find(pattern) != std::string::npos) {
      return false;
    }
  }

  // Only analyze source files
  std::vector<std::string> extensions = {".cpp", ".hpp", ".h", ".cc", ".cxx"};
  for (const auto &ext : extensions) {
    if (filePath.size() >= ext.size() &&
        filePath.substr(filePath.size() - ext.size()) == ext) {
      return true;
    }
  }

  return false;
}

std::string SecurityAuditor::readFileContent(const std::string &filePath) {
  std::ifstream file(filePath);
  if (!file.is_open()) {
    return "";
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::vector<std::string> SecurityAuditor::findMatches(const std::string &content,
                                                    const std::regex &pattern) {
  std::vector<std::string> matches;
  std::sregex_iterator iter(content.begin(), content.end(), pattern);
  std::sregex_iterator end;

  for (; iter != end; ++iter) {
    matches.push_back(iter->str());
  }

  return matches;
}

bool SecurityAuditor::containsRegex(const std::string &content, const std::string &pattern) {
  std::regex regexPattern(pattern, std::regex_constants::icase);
  return std::regex_search(content, regexPattern);
}

std::string SecurityAuditor::AuditResult::getSummary() const {
  std::stringstream ss;
  ss << "Security Audit: " << (passed ? "PASSED" : "FAILED") << " | ";
  ss << "Critical: " << issueCounts.at("critical") << ", ";
  ss << "High: " << issueCounts.at("high") << ", ";
  ss << "Medium: " << issueCounts.at("medium") << ", ";
  ss << "Low: " << issueCounts.at("low");
  return ss.str();
}

} // namespace ETLPlus::Security
