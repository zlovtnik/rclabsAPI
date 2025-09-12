#include "ssl_manager.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <logger.hpp>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sstream>
#include <sys/stat.h>

namespace ETLPlus::SSL {

SSLManager::SSLManager(const SSLConfig &config)
    : config_(config), sslContext_(boost::asio::ssl::context::tlsv12_server) {}

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

boost::asio::ssl::context &SSLManager::getSSLContext() { return sslContext_; }

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
        result.addWarning("CA certificate file not found: " +
                          config_.caCertificatePath);
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

  // Validate TLS version - reject insecure versions
  std::vector<std::string> validVersions = {"TLSv1.2", "TLSv1.3"};
  std::vector<std::string> insecureVersions = {"TLSv1.0", "TLSv1.1"};

  // Check if the configured version is insecure
  for (const auto &version : insecureVersions) {
    if (config_.minimumTLSVersion == version) {
      result.setError("Insecure TLS version not allowed: " + version +
                      " - use TLSv1.2 or TLSv1.3 for security");
      return result;
    }
  }

  // Check if the configured version is valid
  bool validVersion = false;
  for (const auto &version : validVersions) {
    if (config_.minimumTLSVersion == version) {
      validVersion = true;
      break;
    }
  }

  if (!validVersion) {
    result.setError("Invalid TLS version: " + config_.minimumTLSVersion +
                    " - supported versions are TLSv1.2 and TLSv1.3");
  }

  return result;
}

SSLManager::SSLResult
SSLManager::generateSelfSignedCertificate(const std::string &outputDir) {
  SSLResult result;

  try {
    // Validate and canonicalize output directory
    std::filesystem::path outputPath(outputDir);
    if (!std::filesystem::exists(outputPath)) {
      std::filesystem::create_directories(outputPath);
    }

    // Canonicalize paths to prevent directory traversal
    outputPath = std::filesystem::canonical(outputPath);
    std::filesystem::path certPath = outputPath / "server.crt";
    std::filesystem::path keyPath = outputPath / "server.key";

    // Convert to strings for OpenSSL API
    std::string certPathStr = certPath.string();
    std::string keyPathStr = keyPath.string();

    Logger::getInstance().info(
        "SSLManager", "Generating self-signed certificate in: " + outputDir);

    // Declare ALL variables at the beginning to avoid goto issues
    EVP_PKEY *pkey = nullptr;
    EVP_PKEY_CTX *ctx = nullptr;
    X509 *x509 = nullptr;
    BIO *keyBio = nullptr;
    BIO *certBio = nullptr;
    FILE *keyFile = nullptr;
    FILE *certFile = nullptr;
    X509_NAME *name = nullptr;

    // Generate RSA private key using OpenSSL API
    // Create EVP_PKEY context for RSA key generation
    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) {
      result.setError(
          "Failed to create EVP_PKEY context for RSA key generation");
      goto cleanup;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
      result.setError("Failed to initialize RSA key generation");
      goto cleanup;
    }

    // Set RSA key size to 2048 bits
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
      result.setError("Failed to set RSA key size to 2048 bits");
      goto cleanup;
    }

    // Generate the RSA key pair
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
      result.setError("Failed to generate RSA key pair");
      goto cleanup;
    }

    Logger::getInstance().info("SSLManager",
                               "RSA key pair generated successfully");

    // Create X.509 certificate
    x509 = X509_new();
    if (!x509) {
      result.setError("Failed to create X.509 certificate");
      goto cleanup;
    }

    // Set certificate version to X.509 v3
    if (X509_set_version(x509, 2) != 1) {
      result.setError("Failed to set certificate version");
      goto cleanup;
    }

    // Set serial number
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

    // Set validity period (365 days from now)
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 365 * 24 * 60 * 60);

    // Set public key
    if (X509_set_pubkey(x509, pkey) != 1) {
      result.setError("Failed to set certificate public key");
      goto cleanup;
    }

    // Set certificate subject
    name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"US",
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "ST", MBSTRING_ASC,
                               (unsigned char *)"State", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "L", MBSTRING_ASC, (unsigned char *)"City",
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               (unsigned char *)"Organization", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (unsigned char *)"localhost", -1, -1, 0);

    // Set issuer (self-signed)
    X509_set_issuer_name(x509, name);

    // Sign the certificate with the private key
    if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
      result.setError("Failed to sign certificate");
      goto cleanup;
    }

    Logger::getInstance().info(
        "SSLManager", "X.509 certificate created and signed successfully");

    // Write private key to file with secure permissions
    keyFile = fopen(keyPathStr.c_str(), "wb");
    if (!keyFile) {
      result.setError("Failed to open private key file for writing: " +
                      keyPathStr);
      goto cleanup;
    }

    // Set secure file permissions (0600) before writing
    if (chmod(keyPathStr.c_str(), S_IRUSR | S_IWUSR) != 0) {
      result.setError("Failed to set secure permissions on private key file");
      goto cleanup;
    }

    keyBio = BIO_new_fp(keyFile, BIO_NOCLOSE);
    if (!keyBio) {
      result.setError("Failed to create BIO for private key file");
      goto cleanup;
    }

    if (PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr,
                                 nullptr) != 1) {
      result.setError("Failed to write private key to file");
      goto cleanup;
    }

    Logger::getInstance().info("SSLManager",
                               "Private key written to: " + keyPathStr);

    // Write certificate to file
    certFile = fopen(certPathStr.c_str(), "wb");
    if (!certFile) {
      result.setError("Failed to open certificate file for writing: " +
                      certPathStr);
      goto cleanup;
    }

    certBio = BIO_new_fp(certFile, BIO_NOCLOSE);
    if (!certBio) {
      result.setError("Failed to create BIO for certificate file");
      goto cleanup;
    }

    if (PEM_write_bio_X509(certBio, x509) != 1) {
      result.setError("Failed to write certificate to file");
      goto cleanup;
    }

    Logger::getInstance().info("SSLManager",
                               "Certificate written to: " + certPathStr);

    result.addWarning("Generated self-signed certificate for development only");
    result.addWarning("Use proper CA-signed certificates in production");

    Logger::getInstance().info(
        "SSLManager",
        "Self-signed certificate generation completed successfully");

  cleanup:
    // Clean up resources
    if (certBio)
      BIO_free(certBio);
    if (keyBio)
      BIO_free(keyBio);
    if (certFile)
      fclose(certFile);
    if (keyFile)
      fclose(keyFile);
    if (x509)
      X509_free(x509);
    if (ctx)
      EVP_PKEY_CTX_free(ctx);
    if (pkey)
      EVP_PKEY_free(pkey);

    return result;

  } catch (const std::filesystem::filesystem_error &e) {
    result.setError("Filesystem error during certificate generation: " +
                    std::string(e.what()));
    return result;
  } catch (const std::exception &e) {
    result.setError("Failed to generate self-signed certificate: " +
                    std::string(e.what()));
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
      // Safely parse HSTS max-age with validation
      long long maxAge = 0;
      bool parseSuccess = false;

      try {
        // Use std::stoll for better range checking
        size_t pos = 0;
        maxAge = std::stoll(config_.hstsMaxAge, &pos);

        // Validate that the entire string was consumed
        if (pos != config_.hstsMaxAge.length()) {
          Logger::getInstance().warn("SSLManager",
                                     "Invalid HSTS max-age format: '" +
                                         config_.hstsMaxAge +
                                         "' - contains non-numeric characters");
        } else if (maxAge < 0) {
          Logger::getInstance().warn(
              "SSLManager", "Invalid HSTS max-age: '" + config_.hstsMaxAge +
                                "' - negative values not allowed");
        } else if (maxAge > std::numeric_limits<long>::max()) {
          Logger::getInstance().warn(
              "SSLManager", "HSTS max-age too large: '" + config_.hstsMaxAge +
                                "' - clamping to maximum safe value");
          maxAge = std::numeric_limits<long>::max();
          parseSuccess = true;
        } else {
          parseSuccess = true;
        }
      } catch (const std::invalid_argument &e) {
        Logger::getInstance().warn(
            "SSLManager", "Invalid HSTS max-age format: '" +
                              config_.hstsMaxAge + "' - not a valid number");
      } catch (const std::out_of_range &e) {
        Logger::getInstance().warn(
            "SSLManager", "HSTS max-age out of range: '" + config_.hstsMaxAge +
                              "' - using default value");
      }

      // Only add preload if parsing succeeded and requirements are met
      if (parseSuccess && maxAge >= 31536000 && config_.hstsIncludeSubDomains) {
        hstsValue += "; preload";
      } else if (!parseSuccess) {
        Logger::getInstance().warn(
            "SSLManager", "Skipping HSTS preload due to invalid max-age value");
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
    std::string fingerprint =
        getCertificateFingerprint(config_.certificatePath);
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
    // Get the TLS method (for mapping to OpenSSL constants)
    auto method = getTLSMethod(config_.minimumTLSVersion);

    // Map TLS version to OpenSSL protocol constants and set minimum version
    int minVersion = TLS1_2_VERSION; // Default to TLS 1.2
    if (config_.minimumTLSVersion == "TLSv1.2") {
      minVersion = TLS1_2_VERSION;
    } else if (config_.minimumTLSVersion == "TLSv1.3") {
      minVersion = TLS1_3_VERSION;
    }

    // Set minimum TLS version on existing context (preserves all other
    // configuration)
    int ret =
        SSL_CTX_set_min_proto_version(sslContext_.native_handle(), minVersion);
    if (ret != 1) {
      unsigned long err = ERR_get_error();
      char errBuf[256];
      ERR_error_string_n(err, errBuf, sizeof(errBuf));
      result.setError("Failed to set minimum TLS version '" +
                      config_.minimumTLSVersion + "': " + errBuf);
      return result;
    }

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to configure TLS version: " +
                    std::string(e.what()));
    return result;
  }
}

SSLManager::SSLResult SSLManager::configureCipherSuites() {
  SSLResult result;

  try {
    // Configure cipher suites
    int ret = SSL_CTX_set_cipher_list(sslContext_.native_handle(),
                                      config_.cipherSuites.c_str());

    if (ret == 0) {
      // Get OpenSSL error details
      unsigned long err = ERR_get_error();
      char errBuf[256];
      ERR_error_string_n(err, errBuf, sizeof(errBuf));

      std::string errorMsg = "Failed to set cipher suites '" +
                             config_.cipherSuites + "': " + errBuf;
      std::cerr << "SSL Error: " << errorMsg << std::endl;

      result.setError(errorMsg);
      return result;
    }

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to configure cipher suites: " +
                    std::string(e.what()));
    return result;
  }
}

SSLManager::SSLResult SSLManager::configureVerification() {
  SSLResult result;

  try {
    if (config_.verifyPeer) {
      sslContext_.set_verify_mode(
          boost::asio::ssl::verify_peer |
          boost::asio::ssl::verify_fail_if_no_peer_cert);
    } else {
      sslContext_.set_verify_mode(boost::asio::ssl::verify_none);
    }

    if (config_.verifyDepth > 0) {
      sslContext_.set_verify_depth(config_.verifyDepth);
    }

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to configure verification: " +
                    std::string(e.what()));
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
    result.setError("Failed to configure session caching: " +
                    std::string(e.what()));
    return result;
  }
}

boost::asio::ssl::context::method
SSLManager::getTLSMethod(const std::string &version) {
  if (version == "TLSv1.2")
    return boost::asio::ssl::context::tlsv12;
  if (version == "TLSv1.3")
    return boost::asio::ssl::context::tlsv13;

  // Default to TLS 1.3 for maximum security
  return boost::asio::ssl::context::tlsv13;
}

std::string SSLManager::getCertificateFingerprint(const std::string &certPath) {
  try {
    FILE *fp = fopen(certPath.c_str(), "r");
    if (!fp)
      return "";

    X509 *cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert)
      return "";

    unsigned char buffer[64];
    unsigned int len;
    const EVP_MD *digest = EVP_sha256();

    if (!X509_digest(cert, digest, buffer, &len)) {
      X509_free(cert);
      return "";
    }

    std::stringstream ss;
    for (unsigned int i = 0; i < len; ++i) {
      if (i > 0)
        ss << ":";
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
    if (!fp)
      return false;

    X509 *cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert)
      return false;

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

SSLManager::SSLResult
SSLManager::checkCertificatePermissions(const std::string &certPath,
                                        const std::string &keyPath) const {
  SSLResult result;

  try {
    // Check certificate file permissions (should be readable by owner only)
    std::filesystem::perms certPerms =
        std::filesystem::status(certPath).permissions();
    if ((certPerms & (std::filesystem::perms::group_read |
                      std::filesystem::perms::group_write |
                      std::filesystem::perms::others_read |
                      std::filesystem::perms::others_write)) !=
            std::filesystem::perms::none ||
        (certPerms & std::filesystem::perms::owner_read) ==
            std::filesystem::perms::none) {
      result.addWarning("Certificate file permissions are too permissive");
    }

    // Check private key file permissions (should be readable by owner only)
    std::filesystem::perms keyPerms =
        std::filesystem::status(keyPath).permissions();
    if ((keyPerms & (std::filesystem::perms::group_read |
                     std::filesystem::perms::group_write |
                     std::filesystem::perms::others_read |
                     std::filesystem::perms::others_write)) !=
            std::filesystem::perms::none ||
        (keyPerms & std::filesystem::perms::owner_read) ==
            std::filesystem::perms::none) {
      result.addWarning("Private key file permissions are too permissive");
    }

    return result;

  } catch (const std::exception &e) {
    result.setError("Failed to check certificate permissions: " +
                    std::string(e.what()));
    return result;
  }
}

} // namespace ETLPlus::SSL
