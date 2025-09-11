#include "jwt_key_manager.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef ETL_ENABLE_JWT
#include <jwt-cpp/jwt.h>
#endif

namespace ETLPlus::Auth {

JWTKeyManager::JWTKeyManager(const KeyConfig &config) : config_(config) {
}

bool JWTKeyManager::initialize() {
  std::lock_guard<std::mutex> lock(keyMutex_);

  if (!validateConfiguration()) {
    return false;
  }

  try {
    // Load or generate keys based on algorithm
    if (config_.algorithm == Algorithm::HS256 ||
        config_.algorithm == Algorithm::HS384 ||
        config_.algorithm == Algorithm::HS512) {
      // HMAC algorithm - use secret key
      if (!config_.secretKey.empty()) {
        currentSecretKey_ = config_.secretKey;
      } else {
        // Generate random secret key
        generateKeyPair();
      }
    } else {
      // RSA/ECDSA algorithms - load from files or generate
      if (!loadKeysFromFiles()) {
        if (!generateKeyPair()) {
          return false;
        }
      }
    }

    currentKeyId_ = generateKeyId();
    keyCreatedAt_ = std::chrono::system_clock::now();
    lastRotation_ = keyCreatedAt_;

    initialized_ = true;
    return true;

  } catch (const std::exception &e) {
    std::cerr << "JWT Key Manager initialization failed: " << e.what() << std::endl;
    return false;
  }
}

std::optional<JWTKeyManager::TokenInfo> JWTKeyManager::generateToken(
    const std::unordered_map<std::string, std::string> &claims,
    std::chrono::hours expiryHours) {

#ifndef ETL_ENABLE_JWT
  return std::nullopt;
#else

  std::lock_guard<std::mutex> lock(keyMutex_);

  if (!initialized_) {
    return std::nullopt;
  }

  try {
    auto now = std::chrono::system_clock::now();
    auto expiresAt = now + expiryHours;

    // Create JWT builder
    auto builder = jwt::create()
                      .set_issuer(config_.issuer)
                      .set_issued_at(now)
                      .set_expires_at(expiresAt)
                      .set_key_id(currentKeyId_);

    // Add custom claims
    for (const auto &[key, value] : claims) {
      builder.set_payload_claim(key, jwt::claim(value));
    }

    // Sign with appropriate algorithm
    std::string token;
    if (config_.algorithm == Algorithm::HS256 ||
        config_.algorithm == Algorithm::HS384 ||
        config_.algorithm == Algorithm::HS512) {
      token = signToken(builder, currentSecretKey_, config_.algorithm);
    } else {
      token = signToken(builder, currentPrivateKey_, config_.algorithm);
    }

    TokenInfo info;
    info.token = token;
    info.keyId = currentKeyId_;
    info.algorithm = config_.algorithm;
    info.issuedAt = now;
    info.expiresAt = expiresAt;
    info.claims = claims;

    return info;

  } catch (const std::exception &e) {
    std::cerr << "Token generation failed: " << e.what() << std::endl;
    return std::nullopt;
  }
#endif
}

std::optional<JWTKeyManager::TokenInfo> JWTKeyManager::validateToken(const std::string &token) {
#ifndef ETL_ENABLE_JWT
  return std::nullopt;
#else

  std::lock_guard<std::mutex> lock(keyMutex_);

  if (!initialized_) {
    return std::nullopt;
  }

  try {
    // Try to validate with current key first
    auto decoded = jwt::decode(token);

    std::string keyToUse;
    if (config_.algorithm == Algorithm::HS256 ||
        config_.algorithm == Algorithm::HS384 ||
        config_.algorithm == Algorithm::HS512) {
      keyToUse = currentSecretKey_;
    } else {
      keyToUse = currentPublicKey_;
    }

    // Verify token
    if (!verifyToken(decoded, keyToUse, config_.algorithm)) {
      return std::nullopt;
    }

    // Extract claims
    TokenInfo info;
    info.token = token;
    info.keyId = decoded.get_key_id();
    info.algorithm = config_.algorithm;
    info.issuedAt = decoded.get_issued_at();
    info.expiresAt = decoded.get_expires_at();

  // Extract custom claims
  try {
    // Get payload as JSON and extract string claims
    auto payload = decoded.get_payload_json();
    for (const auto& [key, value] : payload) {
      if (value.is<std::string>()) {
        info.claims[key] = value.get<std::string>();
      }
    }
  } catch (const std::exception& e) {
    // Ignore claim extraction errors
  }

    return info;

  } catch (const std::exception &e) {
    // Try with previous key if current key fails
    if (!previousSecretKey_.empty() || !previousPublicKey_.empty()) {
      try {
        auto decoded = jwt::decode(token);

        std::string keyToUse;
        if (config_.algorithm == Algorithm::HS256 ||
            config_.algorithm == Algorithm::HS384 ||
            config_.algorithm == Algorithm::HS512) {
          keyToUse = previousSecretKey_;
        } else {
          keyToUse = previousPublicKey_;
        }

        if (!verifyToken(decoded, keyToUse, config_.algorithm)) {
          return std::nullopt;
        }

        // Extract claims
        TokenInfo info;
        info.token = token;
        info.keyId = decoded.get_key_id();
        info.algorithm = config_.algorithm;
        info.issuedAt = decoded.get_issued_at();
        info.expiresAt = decoded.get_expires_at();

        // Extract claims
        auto payload = decoded.get_payload_json();
        for (const auto& [key, value] : payload) {
          if (value.is<std::string>()) {
            info.claims[key] = value.get<std::string>();
          }
        }

        return info;

      } catch (const std::exception &e2) {
        std::cerr << "Token validation failed: " << e2.what() << std::endl;
        return std::nullopt;
      }
    }

    std::cerr << "Token validation failed: " << e.what() << std::endl;
    return std::nullopt;
  }
#endif
}

std::optional<JWTKeyManager::TokenInfo> JWTKeyManager::refreshToken(const std::string &token) {
#ifndef ETL_ENABLE_JWT
  return std::nullopt;
#else

  // Validate the existing token
  auto existingToken = validateToken(token);
  if (!existingToken) {
    return std::nullopt;
  }

  // Generate a new token with the same claims
  return generateToken(existingToken->claims);
#endif
}

std::optional<JWTKeyManager::JWKS> JWTKeyManager::getJWKS() {
#ifndef ETL_ENABLE_JWT
  return std::nullopt;
#else

  std::lock_guard<std::mutex> lock(keyMutex_);

  if (!initialized_) {
    return std::nullopt;
  }

  // Only provide JWKS for asymmetric algorithms
  if (config_.algorithm == Algorithm::HS256 ||
      config_.algorithm == Algorithm::HS384 ||
      config_.algorithm == Algorithm::HS512) {
    return std::nullopt;
  }

  try {
    JWKS jwks;

    // Add current key
    std::string currentKeyEntry = createJWKSKeyEntry(currentKeyId_,
                                                   currentPublicKey_,
                                                   config_.algorithm);
    if (!currentKeyEntry.empty()) {
      jwks.keys.push_back({{"kid", currentKeyId_},
                          {"kty", "RSA"}, // Simplified - should detect key type
                          {"use", "sig"},
                          {"n", currentPublicKey_}}); // Simplified
    }

    // Build JSON
    std::stringstream ss;
    ss << "{\"keys\":[";
    for (size_t i = 0; i < jwks.keys.size(); ++i) {
      if (i > 0) ss << ",";
      ss << "{";
      for (const auto &[key, value] : jwks.keys[i]) {
        ss << "\"" << key << "\":\"" << value << "\"";
      }
      ss << "}";
    }
    ss << "]}";

    jwks.jsonString = ss.str();
    return jwks;

  } catch (const std::exception &e) {
    std::cerr << "JWKS generation failed: " << e.what() << std::endl;
    return std::nullopt;
  }
#endif
}

bool JWTKeyManager::rotateKeys() {
  std::lock_guard<std::mutex> lock(keyMutex_);

  if (!config_.enableRotation) {
    return false;
  }

  try {
    // Move current keys to previous
    previousSecretKey_ = currentSecretKey_;
    previousPublicKey_ = currentPublicKey_;
    previousPrivateKey_ = currentPrivateKey_;
    previousKeyId_ = currentKeyId_;

    // Generate new keys
    if (!generateKeyPair()) {
      return false;
    }

    currentKeyId_ = generateKeyId();
    lastRotation_ = std::chrono::system_clock::now();

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Key rotation failed: " << e.what() << std::endl;
    return false;
  }
}

bool JWTKeyManager::shouldRotateKeys() const {
  if (!config_.enableRotation) {
    return false;
  }

  auto now = std::chrono::system_clock::now();
  auto timeSinceRotation = now - lastRotation_;
  return timeSinceRotation > config_.rotationInterval;
}

std::unordered_map<std::string, std::string> JWTKeyManager::getKeyInfo() {
  std::lock_guard<std::mutex> lock(keyMutex_);

  std::unordered_map<std::string, std::string> info;

  if (!initialized_) {
    info["status"] = "not_initialized";
    return info;
  }

  info["status"] = "initialized";
  info["algorithm"] = getAlgorithmString(config_.algorithm);
  info["current_key_id"] = currentKeyId_;
  info["issuer"] = config_.issuer;
  info["rotation_enabled"] = config_.enableRotation ? "true" : "false";

  if (config_.enableRotation) {
    auto now = std::chrono::system_clock::now();
    auto timeSinceRotation = now - lastRotation_;
    auto hoursSinceRotation = std::chrono::duration_cast<std::chrono::hours>(timeSinceRotation);
    info["hours_since_rotation"] = std::to_string(hoursSinceRotation.count());

    auto rotationIntervalHours = std::chrono::duration_cast<std::chrono::hours>(config_.rotationInterval);
    info["rotation_interval_hours"] = std::to_string(rotationIntervalHours.count());
  }

  return info;
}

bool JWTKeyManager::loadKeysFromFiles() {
  try {
    if (!config_.publicKeyPath.empty() && std::filesystem::exists(config_.publicKeyPath)) {
      currentPublicKey_ = loadKeyFromFile(config_.publicKeyPath);
    }

    if (!config_.privateKeyPath.empty() && std::filesystem::exists(config_.privateKeyPath)) {
      currentPrivateKey_ = loadKeyFromFile(config_.privateKeyPath);
    }

    return !currentPublicKey_.empty() && !currentPrivateKey_.empty();

  } catch (const std::exception &e) {
    std::cerr << "Failed to load keys from files: " << e.what() << std::endl;
    return false;
  }
}

bool JWTKeyManager::generateKeyPair() {
  try {
    if (config_.algorithm == Algorithm::HS256 ||
        config_.algorithm == Algorithm::HS384 ||
        config_.algorithm == Algorithm::HS512) {
      // Generate random secret key
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, 255);

      std::stringstream ss;
      for (int i = 0; i < 32; ++i) { // 256-bit key
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
      }

      currentSecretKey_ = ss.str();
      return true;

    } else {
      // For RSA/ECDSA, you would typically use OpenSSL commands or libraries
      // This is a simplified implementation
      std::cerr << "RSA/ECDSA key generation not fully implemented" << std::endl;
      return false;
    }

  } catch (const std::exception &e) {
    std::cerr << "Key generation failed: " << e.what() << std::endl;
    return false;
  }
}

bool JWTKeyManager::validateConfiguration() {
  if (config_.algorithm == Algorithm::HS256 ||
      config_.algorithm == Algorithm::HS384 ||
      config_.algorithm == Algorithm::HS512) {
    if (config_.secretKey.empty() && config_.privateKeyPath.empty()) {
      std::cerr << "HMAC algorithm requires secret key or key file" << std::endl;
      return false;
    }
  } else {
    if (config_.publicKeyPath.empty() || config_.privateKeyPath.empty()) {
      std::cerr << "RSA/ECDSA algorithms require public and private key files" << std::endl;
      return false;
    }
  }

  return true;
}

#ifdef ETL_ENABLE_JWT

std::string JWTKeyManager::generateKeyId() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (int i = 0; i < 16; ++i) {
    ss << dis(gen);
  }

  return ss.str();
}

std::string JWTKeyManager::loadKeyFromFile(const std::string &filePath) {
  std::ifstream file(filePath);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open key file: " + filePath);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

bool JWTKeyManager::saveKeyToFile(const std::string &key, const std::string &filePath) {
  std::ofstream file(filePath);
  if (!file.is_open()) {
    return false;
  }

  file << key;
  return true;
}

std::string JWTKeyManager::getAlgorithmString(Algorithm alg) const {
  switch (alg) {
    case Algorithm::HS256: return "HS256";
    case Algorithm::HS384: return "HS384";
    case Algorithm::HS512: return "HS512";
    case Algorithm::RS256: return "RS256";
    case Algorithm::RS384: return "RS384";
    case Algorithm::RS512: return "RS512";
    case Algorithm::ES256: return "ES256";
    case Algorithm::ES384: return "ES384";
    case Algorithm::ES512: return "ES512";
    default: return "UNKNOWN";
  }
}

std::string JWTKeyManager::signToken(const auto& builder, const std::string& key, Algorithm alg) {
  switch (alg) {
    case Algorithm::HS256: return builder.sign(jwt::algorithm::hs256(key));
    case Algorithm::HS384: return builder.sign(jwt::algorithm::hs384(key));
    case Algorithm::HS512: return builder.sign(jwt::algorithm::hs512(key));
    case Algorithm::RS256: return builder.sign(jwt::algorithm::rs256("", key));
    case Algorithm::RS384: return builder.sign(jwt::algorithm::rs384("", key));
    case Algorithm::RS512: return builder.sign(jwt::algorithm::rs512("", key));
    case Algorithm::ES256: return builder.sign(jwt::algorithm::es256("", key));
    case Algorithm::ES384: return builder.sign(jwt::algorithm::es384("", key));
    case Algorithm::ES512: return builder.sign(jwt::algorithm::es512("", key));
    default: throw std::runtime_error("Unsupported algorithm");
  }
}

bool JWTKeyManager::verifyToken(const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoded, const std::string& key, Algorithm alg) {
  try {
    auto verifier = jwt::verify().with_issuer(config_.issuer);

    switch (alg) {
      case Algorithm::HS256: verifier.allow_algorithm(jwt::algorithm::hs256(key)); break;
      case Algorithm::HS384: verifier.allow_algorithm(jwt::algorithm::hs384(key)); break;
      case Algorithm::HS512: verifier.allow_algorithm(jwt::algorithm::hs512(key)); break;
      case Algorithm::RS256: verifier.allow_algorithm(jwt::algorithm::rs256(key, "")); break;
      case Algorithm::RS384: verifier.allow_algorithm(jwt::algorithm::rs384(key, "")); break;
      case Algorithm::RS512: verifier.allow_algorithm(jwt::algorithm::rs512(key, "")); break;
      case Algorithm::ES256: verifier.allow_algorithm(jwt::algorithm::es256(key, "")); break;
      case Algorithm::ES384: verifier.allow_algorithm(jwt::algorithm::es384(key, "")); break;
      case Algorithm::ES512: verifier.allow_algorithm(jwt::algorithm::es512(key, "")); break;
      default: return false;
    }

    verifier.verify(decoded);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::string JWTKeyManager::createJWKSKeyEntry(const std::string &keyId,
                                            const std::string &publicKey,
                                            Algorithm alg) {
  // Simplified JWKS key entry - in production you'd use proper JWKS format
  std::stringstream ss;
  ss << "{\"kid\":\"" << keyId << "\",\"kty\":\"RSA\",\"use\":\"sig\",\"n\":\""
     << publicKey << "\"}";
  return ss.str();
}

bool JWTKeyManager::isValidKeyFormat(const std::string &key, Algorithm alg) {
  // Basic validation - in production you'd do more thorough checks
  return !key.empty() && key.length() >= 16;
}

#endif

} // namespace ETLPlus::Auth
