#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#ifdef ETL_ENABLE_JWT
#include <jwt-cpp/jwt.h>
#endif

namespace ETLPlus::Auth {

/**
 * @brief Enhanced JWT key management system
 *
 * This class provides comprehensive JWT key management including:
 * - Multiple key algorithms (HS256, RS256, ES256)
 * - Key rotation and versioning
 * - JWKS (JSON Web Key Set) endpoint support
 * - Key storage and retrieval
 * - Security best practices
 */
class JWTKeyManager {
public:
  /**
   * @brief JWT algorithm types
   */
  enum class Algorithm {
    HS256,  // HMAC SHA-256
    HS384,  // HMAC SHA-384
    HS512,  // HMAC SHA-512
    RS256,  // RSA SHA-256
    RS384,  // RSA SHA-384
    RS512,  // RSA SHA-512
    ES256,  // ECDSA SHA-256
    ES384,  // ECDSA SHA-384
    ES512   // ECDSA SHA-512
  };

  /**
   * @brief Key configuration
   */
  struct KeyConfig {
    Algorithm algorithm;
    std::string secretKey;
    std::string publicKeyPath;
    std::string privateKeyPath;
    std::string keyId;
    std::chrono::hours rotationInterval;
    bool enableRotation;
    std::string issuer;

    KeyConfig()
        : algorithm(Algorithm::HS256),
          keyId("default"),
          rotationInterval(std::chrono::hours(24 * 30)), // 30 days
          enableRotation(true),
          issuer("etl-backend") {}
  };  /**
   * @brief JWT token information
   */
  struct TokenInfo {
    std::string token;
    std::string keyId;
    Algorithm algorithm;
    std::chrono::system_clock::time_point issuedAt;
    std::chrono::system_clock::time_point expiresAt;
    std::unordered_map<std::string, std::string> claims;

    bool isExpired() const {
      return std::chrono::system_clock::now() > expiresAt;
    }
  };

  /**
   * @brief JWKS (JSON Web Key Set) for public key distribution
   */
  struct JWKS {
    std::string jsonString;
    std::vector<std::unordered_map<std::string, std::string>> keys;
  };

  JWTKeyManager(const KeyConfig &config = KeyConfig());
  ~JWTKeyManager() = default;

  /**
   * @brief Initialize key management system
   */
  bool initialize();

  /**
   * @brief Generate JWT token
   */
  std::optional<TokenInfo> generateToken(
      const std::unordered_map<std::string, std::string> &claims,
      std::chrono::hours expiryHours = std::chrono::hours(1));

  /**
   * @brief Validate JWT token
   */
  std::optional<TokenInfo> validateToken(const std::string &token);

  /**
   * @brief Refresh JWT token
   */
  std::optional<TokenInfo> refreshToken(const std::string &token);

  /**
   * @brief Get JWKS for public key distribution
   */
  std::optional<JWKS> getJWKS();

  /**
   * @brief Rotate keys (generate new key pair)
   */
  bool rotateKeys();

  /**
   * @brief Check if keys need rotation
   */
  bool shouldRotateKeys() const;

  /**
   * @brief Get current key information
   */
  std::unordered_map<std::string, std::string> getKeyInfo();

  /**
   * @brief Load keys from files
   */
  bool loadKeysFromFiles();

  /**
   * @brief Generate new key pair
   */
  bool generateKeyPair();

  /**
   * @brief Validate key configuration
   */
  bool validateConfiguration();

private:
  KeyConfig config_;
  std::mutex keyMutex_;
  bool initialized_ = false;

  // Utility helper methods (don't depend on jwt-cpp)
  std::string generateKeyId();
  std::string loadKeyFromFile(const std::string &filePath);
  bool saveKeyToFile(const std::string &key, const std::string &filePath);
  std::string getAlgorithmString(Algorithm alg) const;

#ifdef ETL_ENABLE_JWT
  // Current keys
  std::string currentSecretKey_;
  std::string currentPublicKey_;
  std::string currentPrivateKey_;
  std::string currentKeyId_;

  // Previous keys (for validation during rotation)
  std::string previousSecretKey_;
  std::string previousPublicKey_;
  std::string previousPrivateKey_;
  std::string previousKeyId_;

  // Key metadata
  std::chrono::system_clock::time_point keyCreatedAt_;
  std::chrono::system_clock::time_point lastRotation_;

  // Helper methods that depend on jwt-cpp
  std::string signToken(const auto& builder, const std::string& key, Algorithm alg);
  bool verifyToken(const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoded, const std::string& key, Algorithm alg);
  std::string createJWKSKeyEntry(const std::string &keyId,
                                const std::string &publicKey,
                                Algorithm alg);
  bool isValidKeyFormat(const std::string &key, Algorithm alg);
#endif
};

} // namespace ETLPlus::Auth
