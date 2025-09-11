#include "ssl_manager.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <sstream>

namespace ETLPlus::SSL {

SSLManager::SSLManager(const SSLConfig &config)
    : config_(config), sslContext_(boost::asio::ssl::context::tlsv12_server) {
}

SSLManager::SSLResult SSLManager::initialize() {
  SSLResult result;

  if (!config_.enableSSL) {
    result.addWarning("SSL is disabled in configuration");
    return result;
  }

  try {
    // Configure TLS version
    auto tlsResult = configureTLSVersion();
    if (!tlsResult.success) {
      return tlsResult;
    }

    // Load certificates
    auto certResult = loadCertificates();
    if (!certResult.success) {
      return certResult;
    }

    // Configure cipher suites
    auto cipherResult = configureCipherSuites();
    if (!cipherResult.success) {
      return cipherResult;
    }

    // Configure verification
    auto verifyResult = configureVerification();
    if (!verifyResult.success) {
      return verifyResult;
    }

    // Configure session caching
    auto sessionResult = configureSessionCaching();
    if (!sessionResult.success) {
      return sessionResult;
    }

    initialized_ = true;
    return result;

  } catch (const std::exception &e) {
    result.setError("SSL initialization failed: " + std::string(e.what()));
    return result;
  }
}

boost::asio::ssl::context &SSLManager::getSSLContext() {
  return sslContext_;
}

SSLManager::SSLResult SSLManager::loadCertificates() {
  SSLResult result;

  try {
    // Check if certificate files exist
    if (!std::filesystem::exists(config_.certificatePath)) {
      result.setError("Certificate file not found: " + config_.certificatePath);
      return result;
    }

    if (!std::filesystem::exists(config_.privateKeyPath)) {
      result.setError("Private key file not found: " + config_.privateKeyPath);
      return result;
    }

    // Check certificate permissions
    auto permResult = checkCertificatePermissions(config_.certificatePath,
                                                 config_.privateKeyPath);
    if (!permResult.success) {
      return permResult;
    }

    // Load certificate chain
    sslContext_.use_certificate_chain_file(config_.certificatePath);

    // Load private key
    sslContext_.use_private_key_file(config_.privateKeyPath,
                                    boost::asio::ssl::context::pem);

    // Load CA certificate if specified
    if (!config_.caCertificatePath.empty()) {
      if (std::filesystem::exists(config_.caCertificatePath)) {
        sslContext_.load_verify_file(config_.caCertificatePath);
      } else {
        result.addWarning("CA certificate file not found: " + config_.caCertificatePath);
      }
    }

    // Validate certificate dates
    if (!validateCertificateDates(config_.certificatePath)) {
      result.addWarning("Certificate may be expired or not yet valid");
    }

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to load certificates: " + std::string(e.what()));
    return result;
  }
}

SSLManager::SSLResult SSLManager::validateConfiguration() {
  SSLResult result;

  if (!config_.enableSSL) {
    return result; // SSL disabled, nothing to validate
  }

  // Check certificate files
  if (config_.certificatePath.empty()) {
    result.setError("Certificate path is required when SSL is enabled");
  }

  if (config_.privateKeyPath.empty()) {
    result.setError("Private key path is required when SSL is enabled");
  }

  // Validate TLS version
  std::vector<std::string> validVersions = {"TLSv1.0", "TLSv1.1", "TLSv1.2", "TLSv1.3"};
  bool validVersion = false;
  for (const auto &version : validVersions) {
    if (config_.minimumTLSVersion == version) {
      validVersion = true;
      break;
    }
  }

  if (!validVersion) {
    result.setError("Invalid TLS version: " + config_.minimumTLSVersion);
  }

  return result;
}

SSLManager::SSLResult SSLManager::generateSelfSignedCertificate(const std::string &outputDir) {
  SSLResult result;

  try {
    // This is a simplified self-signed certificate generation
    // In production, you should use proper certificate authorities

    std::string certPath = outputDir + "/server.crt";
    std::string keyPath = outputDir + "/server.key";

    // Generate private key
    std::system(("openssl genrsa -out " + keyPath + " 2048").c_str());

    // Generate self-signed certificate
    std::string cmd = "openssl req -new -x509 -key " + keyPath +
                     " -out " + certPath + " -days 365 -subj \"/C=US/ST=State/L=City/O=Organization/CN=localhost\"";
    std::system(cmd.c_str());

    result.addWarning("Generated self-signed certificate for development only");
    result.addWarning("Use proper CA-signed certificates in production");

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to generate self-signed certificate: " + std::string(e.what()));
    return result;
  }
}

std::unordered_map<std::string, std::string> SSLManager::getSecurityHeaders() {
  std::unordered_map<std::string, std::string> headers;

  if (config_.enableHSTS) {
    std::string hstsValue = "max-age=" + config_.hstsMaxAge;
    
    if (config_.hstsIncludeSubDomains) {
      hstsValue += "; includeSubDomains";
    }
    
    if (config_.hstsPreload) {
      // Validate requirements for preload
      long maxAge = std::stol(config_.hstsMaxAge);
      if (maxAge >= 31536000 && config_.hstsIncludeSubDomains) {
        hstsValue += "; preload";
      }
    }
    
    headers["Strict-Transport-Security"] = hstsValue;
  }

  headers["X-Frame-Options"] = "DENY";
  headers["X-Content-Type-Options"] = "nosniff";
  headers["Referrer-Policy"] = "strict-origin-when-cross-origin";

  return headers;
}

bool SSLManager::isSSLConfigured() const {
  return initialized_ && config_.enableSSL;
}

std::unordered_map<std::string, std::string> SSLManager::getCertificateInfo() {
  std::unordered_map<std::string, std::string> info;

  if (!isSSLConfigured()) {
    info["status"] = "SSL not configured";
    return info;
  }

  try {
    // Get certificate fingerprint
    std::string fingerprint = getCertificateFingerprint(config_.certificatePath);
    if (!fingerprint.empty()) {
      info["fingerprint"] = fingerprint;
    }

    info["certificate_path"] = config_.certificatePath;
    info["private_key_path"] = config_.privateKeyPath;
    info["tls_version"] = config_.minimumTLSVersion;
    info["cipher_suites"] = config_.cipherSuites;

  } catch (const std::exception &e) {
    info["error"] = "Failed to get certificate info: " + std::string(e.what());
  }

  return info;
}

SSLManager::SSLResult SSLManager::reloadCertificates() {
  SSLResult result;

  if (!isSSLConfigured()) {
    result.setError("SSL is not configured");
    return result;
  }

  try {
    // Reload certificates
    sslContext_.use_certificate_chain_file(config_.certificatePath);
    sslContext_.use_private_key_file(config_.privateKeyPath,
                                    boost::asio::ssl::context::pem);

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to reload certificates: " + std::string(e.what()));
    return result;
  }
}

SSLManager::SSLResult SSLManager::configureTLSVersion() {
  SSLResult result;

  try {
    auto method = getTLSMethod(config_.minimumTLSVersion);
    // Note: In newer Boost versions, you might need to recreate the context
    // sslContext_ = boost::asio::ssl::context(method);

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to configure TLS version: " + std::string(e.what()));
    return result;
  }
}

SSLManager::SSLResult SSLManager::configureCipherSuites() {
  SSLResult result;

  try {
    // Configure cipher suites
    SSL_CTX_set_cipher_list(sslContext_.native_handle(), config_.cipherSuites.c_str());

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to configure cipher suites: " + std::string(e.what()));
    return result;
  }
}

SSLManager::SSLResult SSLManager::configureVerification() {
  SSLResult result;

  try {
    if (config_.verifyPeer) {
      sslContext_.set_verify_mode(boost::asio::ssl::verify_peer |
                                 boost::asio::ssl::verify_fail_if_no_peer_cert);
    } else {
      sslContext_.set_verify_mode(boost::asio::ssl::verify_none);
    }

    if (config_.verifyDepth > 0) {
      sslContext_.set_verify_depth(config_.verifyDepth);
    }

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to configure verification: " + std::string(e.what()));
    return result;
  }
}

SSLManager::SSLResult SSLManager::configureSessionCaching() {
  SSLResult result;

  try {
    if (config_.enableSessionCaching) {
      SSL_CTX_set_session_cache_mode(sslContext_.native_handle(),
                                    SSL_SESS_CACHE_SERVER);
      SSL_CTX_set_timeout(sslContext_.native_handle(), config_.sessionTimeout);
    } else {
      SSL_CTX_set_session_cache_mode(sslContext_.native_handle(),
                                    SSL_SESS_CACHE_OFF);
    }

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to configure session caching: " + std::string(e.what()));
    return result;
  }
}

boost::asio::ssl::context::method SSLManager::getTLSMethod(const std::string &version) {
  if (version == "TLSv1.0") return boost::asio::ssl::context::tlsv1;
  if (version == "TLSv1.1") return boost::asio::ssl::context::tlsv11;
  if (version == "TLSv1.2") return boost::asio::ssl::context::tlsv12;
  if (version == "TLSv1.3") return boost::asio::ssl::context::tlsv13;

  // Default to TLS 1.2
  return boost::asio::ssl::context::tlsv12;
}

std::string SSLManager::getCertificateFingerprint(const std::string &certPath) {
  try {
    FILE *fp = fopen(certPath.c_str(), "r");
    if (!fp) return "";

    X509 *cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) return "";

    unsigned char buffer[64];
    unsigned int len;
    const EVP_MD *digest = EVP_sha256();

    if (!X509_digest(cert, digest, buffer, &len)) {
      X509_free(cert);
      return "";
    }

    std::stringstream ss;
    for (unsigned int i = 0; i < len; ++i) {
      if (i > 0) ss << ":";
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i];
    }

    X509_free(cert);
    return ss.str();

  } catch (...) {
    return "";
  }
}

bool SSLManager::validateCertificateDates(const std::string &certPath) const {
  try {
    FILE *fp = fopen(certPath.c_str(), "r");
    if (!fp) return false;

    X509 *cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) return false;

    // Check if certificate is not yet valid
    if (X509_cmp_current_time(X509_get_notBefore(cert)) > 0) {
      X509_free(cert);
      return false;
    }

    // Check if certificate has expired
    if (X509_cmp_current_time(X509_get_notAfter(cert)) < 0) {
      X509_free(cert);
      return false;
    }

    X509_free(cert);
    return true;

  } catch (...) {
    return false;
  }
}

SSLManager::SSLResult SSLManager::checkCertificatePermissions(const std::string &certPath,
                                                             const std::string &keyPath) const {
  SSLResult result;

  try {
    // Check certificate file permissions (should be readable by owner only)
    std::filesystem::perms certPerms = std::filesystem::status(certPath).permissions();
    if ((certPerms & (std::filesystem::perms::group_read | std::filesystem::perms::group_write |
                      std::filesystem::perms::others_read | std::filesystem::perms::others_write)) != std::filesystem::perms::none ||
        (certPerms & std::filesystem::perms::owner_read) == std::filesystem::perms::none) {
      result.addWarning("Certificate file permissions are too permissive");
    }

    // Check private key file permissions (should be readable by owner only)
    std::filesystem::perms keyPerms = std::filesystem::status(keyPath).permissions();
    if ((keyPerms & (std::filesystem::perms::group_read | std::filesystem::perms::group_write |
                     std::filesystem::perms::others_read | std::filesystem::perms::others_write)) != std::filesystem::perms::none ||
        (keyPerms & std::filesystem::perms::owner_read) == std::filesystem::perms::none) {
      result.addWarning("Private key file permissions are too permissive");
    }

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to check certificate permissions: " + std::string(e.what()));
    return result;
  }
}

} // namespace ETLPlus::SSL
