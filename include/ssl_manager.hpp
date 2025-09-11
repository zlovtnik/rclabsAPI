#pragma once

#include <boost/beast/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ETLPlus::SSL {

/**
 * @brief SSL/TLS configuration and management
 *
 * This class provides comprehensive SSL/TLS support including:
 * - Certificate management and validation
 * - SSL context configuration
 * - Secure WebSocket (WSS) support
 * - TLS version enforcement
 * - Cipher suite configuration
 */
class SSLManager {
public:
  /**
   * @brief SSL configuration structure
   */
  struct SSLConfig {
    // Certificate paths
    std::string certificatePath;
    std::string privateKeyPath;
    std::string caCertificatePath;

    // SSL settings
    bool enableSSL;
    bool requireClientCertificate;
    std::string minimumTLSVersion;
    std::string cipherSuites;

    // Certificate validation
    bool verifyPeer;
    bool verifyHost;
    int verifyDepth;

    // Session settings
    bool enableSessionCaching;
    long sessionTimeout;

    // Security headers
    bool enableHSTS;
    std::string hstsMaxAge;
    bool hstsIncludeSubDomains;
    bool hstsPreload;
    bool enableHPKP;

    SSLConfig()
        : enableSSL(true),
          requireClientCertificate(false),
          minimumTLSVersion("TLSv1.3"), // Use TLS 1.3 for better security and performance
          cipherSuites("HIGH:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP:!CAMELLIA"),
          verifyPeer(true),
          verifyHost(true),
          verifyDepth(9),
          enableSessionCaching(true),
          sessionTimeout(3600), // 1 hour - balance between security and performance
          enableHSTS(true),
          hstsMaxAge("31536000"), // 1 year
          hstsIncludeSubDomains(false), // Make configurable
          hstsPreload(false), // Make configurable, don't set by default
          enableHPKP(false) {} // HTTP Public Key Pinning (deprecated)
  };

  /**
   * @brief SSL operation result
   */
  struct SSLResult {
    bool success = true;
    std::string errorMessage;
    std::vector<std::string> warnings;

    void setError(const std::string &message) {
      success = false;
      errorMessage = message;
    }

    void addWarning(const std::string &message) {
      warnings.push_back(message);
    }
  };

  SSLManager(const SSLConfig &config = SSLConfig());
  ~SSLManager() = default;

  /**
   * @brief Initialize SSL context
   */
  SSLResult initialize();

  /**
   * @brief Get SSL context for server
   */
  boost::asio::ssl::context &getSSLContext();

  /**
   * @brief Load SSL certificates
   */
  SSLResult loadCertificates();

  /**
   * @brief Validate SSL configuration
   */
  SSLResult validateConfiguration();

  /**
   * @brief Generate self-signed certificate (for development only)
   */
  SSLResult generateSelfSignedCertificate(const std::string &outputDir);

  /**
   * @brief Get security headers for HTTPS responses
   */
  std::unordered_map<std::string, std::string> getSecurityHeaders();

  /**
   * @brief Check if SSL is properly configured
   */
  bool isSSLConfigured() const;

  /**
   * @brief Get SSL certificate information
   */
  std::unordered_map<std::string, std::string> getCertificateInfo();

  /**
   * @brief Reload SSL certificates (for certificate rotation)
   */
  SSLResult reloadCertificates();

private:
  SSLConfig config_;
  boost::asio::ssl::context sslContext_;
  bool initialized_ = false;

  // Helper methods
  SSLResult configureTLSVersion();
  SSLResult configureCipherSuites();
  SSLResult configureVerification();
  SSLResult configureSessionCaching();
  boost::asio::ssl::context::method getTLSMethod(const std::string &version);
  std::string getCertificateFingerprint(const std::string &certPath);
  bool validateCertificateDates(const std::string &certPath) const;
  SSLResult checkCertificatePermissions(const std::string &certPath,
                                      const std::string &keyPath) const;
};

} // namespace ETLPlus::SSL
