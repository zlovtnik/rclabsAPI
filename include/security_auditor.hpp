#pragma once

#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>
#include <unordered_set>

namespace ETLPlus::Security {

/**
 * @brief Security audit and analysis tool
 *
 * This class provides comprehensive security auditing capabilities including:
 * - Static code analysis for security vulnerabilities
 * - Dependency vulnerability scanning
 * - Security best practices validation
 * - Configuration security assessment
 */
class SecurityAuditor {
public:
  /**
   * @brief Audit result structure
   */
  struct AuditResult {
    bool passed = true;
    std::vector<std::string> criticalIssues;
    std::vector<std::string> highIssues;
    std::vector<std::string> mediumIssues;
    std::vector<std::string> lowIssues;
    std::vector<std::string> informational;
    std::unordered_map<std::string, int> issueCounts;

    void addCritical(const std::string &issue) {
      criticalIssues.push_back(issue);
      passed = false;
      issueCounts["critical"]++;
    }

    void addHigh(const std::string &issue) {
      highIssues.push_back(issue);
      passed = false;
      issueCounts["high"]++;
    }

    void addMedium(const std::string &issue) {
      mediumIssues.push_back(issue);
      issueCounts["medium"]++;
    }

    void addLow(const std::string &issue) {
      lowIssues.push_back(issue);
      issueCounts["low"]++;
    }

    void addInfo(const std::string &info) {
      informational.push_back(info);
      issueCounts["info"]++;
    }

    std::string getSummary() const;
  };

  /**
   * @brief Audit configuration
   */
  struct AuditConfig {
    bool enableStaticAnalysis = true;
    bool enableDependencyScanning = true;
    bool enableConfigAudit = true;
    bool enableCodeReview = true;
    std::vector<std::string> sourceDirectories = {"src", "include"};
    std::vector<std::string> excludePatterns = {".git", "build", "cmake*"};
    std::string severityThreshold = "medium"; // critical, high, medium, low

    AuditConfig()
      : enableStaticAnalysis(true),
        enableDependencyScanning(true),
        enableConfigAudit(true),
        enableCodeReview(true),
        sourceDirectories({"src", "include"}),
        excludePatterns({".git", "build", "cmake*"}),
        severityThreshold("medium") {}
  };

  SecurityAuditor(const AuditConfig &config = AuditConfig());
  ~SecurityAuditor() = default;

  /**
   * @brief Perform comprehensive security audit
   */
  AuditResult performAudit();

  /**
   * @brief Static code analysis for security issues
   */
  AuditResult analyzeCodeSecurity();

  /**
   * @brief Scan dependencies for vulnerabilities
   */
  AuditResult scanDependencies();

  /**
   * @brief Audit configuration files for security issues
   */
  AuditResult auditConfiguration();

  /**
   * @brief Perform automated code review for security
   */
  AuditResult performCodeReview();

  /**
   * @brief Generate security report
   */
  std::string generateReport(const AuditResult &result);

private:
  AuditConfig config_;

  // Security patterns and rules
  std::vector<std::string> dangerousFunctions_ = {
      "system", "popen", "exec", "fork", "strcpy", "strcat",
      "sprintf", "gets", "scanf", "atoi", "atol", "atof"
  };

  std::vector<std::string> insecureHeaders_ = {
      "X-Powered-By", "Server", "X-AspNet-Version"
  };

  std::unordered_set<std::string> sensitiveKeywords_ = {
      "password", "secret", "key", "token", "api_key", "private_key"
  };

  // Helper methods
  AuditResult analyzeFile(const std::string &filePath);
  AuditResult analyzeSourceCode(const std::string &content, const std::string &filePath);
  AuditResult checkForDangerousFunctions(const std::string &content, const std::string &filePath);
  AuditResult checkForHardcodedSecrets(const std::string &content, const std::string &filePath);
  AuditResult checkForSQLInjection(const std::string &content, const std::string &filePath);
  AuditResult checkForXSSVulnerabilities(const std::string &content, const std::string &filePath);
  AuditResult checkForInsecureHeaders(const std::string &content, const std::string &filePath);
  AuditResult checkForWeakCryptography(const std::string &content, const std::string &filePath);
  AuditResult checkForPathTraversal(const std::string &content, const std::string &filePath);

  std::vector<std::string> findFilesToAnalyze();
  bool shouldAnalyzeFile(const std::string &filePath);
  std::string readFileContent(const std::string &filePath);
  std::vector<std::string> findMatches(const std::string &content, const std::regex &pattern);
  bool containsRegex(const std::string &content, const std::string &pattern);
};

} // namespace ETLPlus::Security
