#include "auth_manager.hpp"
#include "logger.hpp"
#include "session_repository.hpp"
#include "user_repository.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#endif
#include <iostream>
#include <random>
#include <sstream>
#include <string_view>
#if ETL_ENABLE_JWT
#include <jwt-cpp/jwt.h>
#endif
#include <nlohmann/json.hpp>

AuthManager::AuthManager(std::shared_ptr<DatabaseManager> dbManager)
    : userRepo_(std::make_shared<UserRepository>(dbManager)),
      sessionRepo_(std::make_shared<SessionRepository>(dbManager)) {
  AUTH_LOG_INFO("Initializing authentication manager");

#if ETL_ENABLE_JWT
  // Load JWT secret from environment variable or file
  std::string jwtSecretKey;
  const char *secret = std::getenv("JWT_SECRET_KEY");
  if (secret) {
    jwtSecretKey = secret;
  } else {
    // Try loading from file
    const char *secretFile = std::getenv("JWT_SECRET_KEY_FILE");
    if (secretFile) {
      std::ifstream file(secretFile);
      if (file.is_open()) {
        std::getline(file, jwtSecretKey);
        file.close();
      }
    }
  }

  if (jwtSecretKey.empty()) {
    AUTH_LOG_ERROR("JWT_SECRET_KEY environment variable or JWT_SECRET_KEY_FILE "
                   "must be set");
    throw std::runtime_error("JWT_SECRET_KEY environment variable or "
                             "JWT_SECRET_KEY_FILE must be set");
  }
  AUTH_LOG_DEBUG("JWT secret key loaded successfully.");

  if (jwtSecretKey.length() < 32) {
    throw std::runtime_error(
        "JWT_SECRET_KEY must be at least 32 characters long for security");
  }
  jwtSecretKey_.assign(jwtSecretKey.begin(), jwtSecretKey.end());
  // Securely zero the temporary string
  std::fill(jwtSecretKey.begin(), jwtSecretKey.end(), '\0');
  // Lock the secret in memory
#if defined(__unix__) || defined(__APPLE__)
  if (!jwtSecretKey_.empty() &&
      mlock(jwtSecretKey_.data(), jwtSecretKey_.size()) != 0) {
    AUTH_LOG_WARN("Failed to lock JWT secret in memory");
  }
#endif
#endif

  // Note: Default admin user creation is now handled by database schema
  // initialization
  AUTH_LOG_DEBUG("Authentication manager initialization completed");
}

AuthManager::~AuthManager() {
#if ETL_ENABLE_JWT
  // Unlock and zero the JWT secret
#if defined(__unix__) || defined(__APPLE__)
  if (!jwtSecretKey_.empty()) {
    munlock(jwtSecretKey_.data(), jwtSecretKey_.size());
  }
#endif
  std::fill(jwtSecretKey_.begin(), jwtSecretKey_.end(), '\0');
#endif
}

#if ETL_ENABLE_JWT
std::chrono::hours AuthManager::getJWTExpiryHours() const {
  // Default to 24 hours, can be overridden by config in the future
  return std::chrono::hours(24);
}
#endif

bool AuthManager::createUser(const std::string &username,
                             const std::string &email,
                             const std::string &password) {
  AUTH_LOG_DEBUG("Creating user: " + username);

  // Check if user already exists
  if (userRepo_->userExists(username, email)) {
    AUTH_LOG_ERROR(
        "User creation failed: user already exists with username or email");
    return false;
  }

  User user;
  user.id = generateSessionId(); // Reuse session ID generator
  user.username = username;
  user.email = email;
  user.passwordHash = hashPassword(password, generateSalt());
  user.roles = {"user"}; // Default role
  user.createdAt = std::chrono::system_clock::now();
  user.isActive = true;

  if (userRepo_->createUser(user)) {
    AUTH_LOG_INFO("Created user: " + username + " with ID: " + user.id);
    return true;
  } else {
    AUTH_LOG_ERROR("Failed to create user in database");
    return false;
  }
}

bool AuthManager::userExists(std::string_view username) const {
  auto user = userRepo_->getUserByUsername(std::string(username));
  if (user && user->isActive) {
    // This function only checks for user existence and active status
    AUTH_LOG_INFO("User exists: " + std::string(username));
    return true;
  }

  AUTH_LOG_ERROR("User lookup failed for user: " + std::string(username));
  return false;
}

bool AuthManager::authenticateUser(std::string_view username,
                                   std::string_view password) const {
  if (password.empty()) {
    AUTH_LOG_ERROR("Authentication failed for user: " + std::string(username) +
                   " - empty password");
    return false;
  }

  auto user = userRepo_->getUserByUsername(std::string(username));
  if (!user || !user->isActive) {
    AUTH_LOG_ERROR("Authentication failed: invalid credentials");
    return false;
  }

  // Verify password using secure comparison
  if (!verifyPassword(std::string(password), user->passwordHash)) {
    AUTH_LOG_ERROR("Authentication failed: invalid credentials");
    return false;
  }

  AUTH_LOG_INFO("Authenticated user: " + std::string(username));
  return true;
}

bool AuthManager::updateUser(const std::string &userId,
                             const User &updatedUser) {
  if (updatedUser.id != userId) {
    AUTH_LOG_ERROR("User ID mismatch: expected " + userId + ", got " +
                   updatedUser.id);
    return false;
  }
  if (userRepo_->updateUser(updatedUser)) {
    AUTH_LOG_INFO("Updated user: " + userId);
    return true;
  } else {
    AUTH_LOG_ERROR("Failed to update user: " + userId);
    return false;
  }
}

bool AuthManager::deleteUser(const std::string &userId) {
  if (userRepo_->deleteUser(userId)) {
    AUTH_LOG_INFO("Deleted user: " + userId);
    return true;
  } else {
    AUTH_LOG_ERROR("Failed to delete user: " + userId);
    return false;
  }
}

std::shared_ptr<User> AuthManager::getUser(const std::string &userId) const {
  auto user = userRepo_->getUserById(userId);
  if (user) {
    return std::make_shared<User>(*user);
  }
  return nullptr;
}

std::optional<User>
AuthManager::getUserByUsername(const std::string &username) const {
  return userRepo_->getUserByUsername(username);
}

std::string AuthManager::createSession(const std::string &userId) {
  auto user = userRepo_->getUserById(userId);
  if (!user || !user->isActive) {
    AUTH_LOG_ERROR("Cannot create session for invalid or inactive user: " +
                   userId);
    return "";
  }

  Session session;
  session.sessionId = generateSessionId();
  session.userId = userId;
  session.createdAt = std::chrono::system_clock::now();
  session.expiresAt =
      session.createdAt + std::chrono::hours(24); // 24 hour expiry
  session.isValid = true;

  if (sessionRepo_->createSession(session)) {
    AUTH_LOG_INFO("Created session: " + session.sessionId +
                  " for user: " + userId);
    return session.sessionId;
  } else {
    AUTH_LOG_ERROR("Failed to create session in database");
    return "";
  }
}

bool AuthManager::validateSession(const std::string &sessionId) {
  auto session = sessionRepo_->getSessionById(sessionId);
  if (session && session->isValid &&
      std::chrono::system_clock::now() < session->expiresAt) {
    return true;
  } else if (session && session->isValid &&
             std::chrono::system_clock::now() >= session->expiresAt) {
    // Session exists but is expired, mark as invalid in database
    Session updatedSession = *session;
    updatedSession.isValid = false;
    sessionRepo_->updateSession(updatedSession);
  }
  return false;
}

void AuthManager::revokeSession(const std::string &sessionId) {
  auto session = sessionRepo_->getSessionById(sessionId);
  if (session) {
    Session updatedSession = *session;
    updatedSession.isValid = false;
    if (sessionRepo_->updateSession(updatedSession)) {
      AUTH_LOG_INFO("Revoked session: " + sessionId);
    } else {
      AUTH_LOG_ERROR("Failed to revoke session: " + sessionId);
    }
  }
}

void AuthManager::cleanupExpiredSessions() {
  if (sessionRepo_->deleteExpiredSessions()) {
    AUTH_LOG_INFO("Cleaned up expired sessions");
  } else {
    AUTH_LOG_ERROR("Failed to cleanup expired sessions");
  }
}

bool AuthManager::hasPermission(std::string_view userId,
                                std::string_view resource,
                                std::string_view action) const {
  auto user = getUser(std::string(userId));
  if (!user || !user->isActive) {
    return false;
  }

  // Simple role-based permissions
  return std::any_of(user->roles.begin(), user->roles.end(),
                     [&](const std::string &role) {
                       if (role == "admin") {
                         return true; // Admin has all permissions
                       } else if (role == "user" && resource == "jobs" &&
                                  (action == "read" || action == "create")) {
                         return true;
                       }
                       return false;
                     });
}

void AuthManager::assignRole(const std::string &userId,
                             const std::string &role) {
  auto userOpt = userRepo_->getUserById(userId);
  if (!userOpt) {
    AUTH_LOG_ERROR("User not found: " + userId);
    return;
  }

  User user = *userOpt;
  if (std::find(user.roles.begin(), user.roles.end(), role) ==
      user.roles.end()) {
    user.roles.push_back(role);
    if (userRepo_->updateUser(user)) {
      AUTH_LOG_INFO("Assigned role '" + role + "' to user: " + userId);
    } else {
      AUTH_LOG_ERROR("Failed to assign role to user: " + userId);
    }
  }
}

void AuthManager::revokeRole(const std::string &userId,
                             const std::string &role) {
  auto userOpt = userRepo_->getUserById(userId);
  if (!userOpt) {
    AUTH_LOG_ERROR("User not found: " + userId);
    return;
  }

  User user = *userOpt;
  auto it = std::find(user.roles.begin(), user.roles.end(), role);
  if (it != user.roles.end()) {
    user.roles.erase(it);
    if (userRepo_->updateUser(user)) {
      AUTH_LOG_INFO("Revoked role '" + role + "' from user: " + userId);
    } else {
      AUTH_LOG_ERROR("Failed to revoke role from user: " + userId);
    }
  }
}

std::string AuthManager::hashPassword(std::string_view password,
                                      std::string_view salt) const {
  // TODO: Implement proper bcrypt hashing in production
  // For now, use a simple but better-than-plaintext approach
  std::string combined = std::string(password) + std::string(salt);
  // Simple hash simulation (replace with real bcrypt)
  std::hash<std::string> hasher;
  size_t hash_value = hasher(combined);
  std::stringstream ss;
  ss << "$2b$12$" << std::hex << hash_value
     << "abcdefghijklmnopqrstuv"; // Mock bcrypt format
  return ss.str();
}

bool AuthManager::verifyPassword(std::string_view password,
                                 std::string_view hashedPassword) const {
  // TODO: Implement proper bcrypt verification in production
  // For now, extract salt from hash and verify
  std::string hash_str(hashedPassword);
  if (hash_str.find("$2b$12$") != 0) {
    return false; // Invalid hash format
  }

  // Extract the hash part (simplified)
  size_t dollar_pos = hash_str.find('$', 7);
  if (dollar_pos == std::string::npos) {
    return false;
  }

  std::string stored_hash = hash_str.substr(dollar_pos + 1);
  std::string salt = "fixed_salt"; // In real implementation, extract from hash

  // Re-hash the provided password with the same salt
  std::string new_hash = hashPassword(password, salt);
  size_t new_dollar_pos = new_hash.find('$', 7);
  if (new_dollar_pos == std::string::npos) {
    return false;
  }

  std::string computed_hash = new_hash.substr(new_dollar_pos + 1);

  // Use constant-time comparison to prevent timing attacks
  return constantTimeCompare(stored_hash, computed_hash);
}

bool AuthManager::constantTimeCompare(std::string_view a,
                                      std::string_view b) const {
  if (a.length() != b.length()) {
    return false;
  }

  volatile unsigned char result = 0;
  for (size_t i = 0; i < a.length(); ++i) {
    result |= static_cast<unsigned char>(a[i] ^ b[i]);
  }

  return result == 0;
}

std::string AuthManager::generateSalt() const {
  std::stringstream ss;
  ss << "salt_" << std::chrono::system_clock::now().time_since_epoch().count();
  return ss.str();
}

std::string AuthManager::generateSessionId() const {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution dis(0, 15);

  std::stringstream ss;
  for (int i = 0; i < 32; ++i) {
    ss << std::hex << dis(gen);
  }

  return ss.str();
}

#if ETL_ENABLE_JWT
std::string AuthManager::generateJWTToken(const std::string &userId) {
  auto user = userRepo_->getUserById(userId);
  if (!user || !user->isActive) {
    AUTH_LOG_ERROR("Cannot generate JWT token for invalid or inactive user: " +
                   userId);
    return "";
  }

  try {
    auto now = std::chrono::system_clock::now();
    auto expiry = now + getJWTExpiryHours();

    auto token =
        jwt::create()
            .set_type("JWT")
            .set_issuer("etl-backend")
            .set_subject(userId)
            .set_issued_at(now)
            .set_expires_at(expiry)
            .set_audience("etl-api")
            .set_payload_claim("username",
                               jwt::claim(std::string(user->username)))
            .set_payload_claim("roles",
                               jwt::claim(picojson::array(user->roles.begin(),
                                                          user->roles.end())))
            .sign(jwt::algorithm::hs256{
                std::string(jwtSecretKey_.data(), jwtSecretKey_.size())});

    AUTH_LOG_INFO("Generated JWT token for user: " + userId);
    return token;
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to generate JWT token for user " + userId + ": " +
                   e.what());
    return "";
  }
}
#endif

#if ETL_ENABLE_JWT
std::optional<std::string>
AuthManager::validateJWTToken(const std::string &token) {
  try {
    auto decoded = jwt::decode(token);
    auto verifier = jwt::verify()
                        .with_issuer("etl-backend")
                        .with_audience("etl-api")
                        .allow_algorithm(jwt::algorithm::hs256{std::string(
                            jwtSecretKey_.data(), jwtSecretKey_.size())});

    verifier.verify(decoded);

    // Check if token is expired with clock skew tolerance
    auto exp = decoded.get_expires_at();
    if (exp != jwt::date{}) {
      auto now = std::chrono::system_clock::now();
      // Allow 30 seconds clock skew tolerance
      auto skew_tolerance = std::chrono::seconds(30);
      if (now > (exp + skew_tolerance)) {
        AUTH_LOG_WARN("JWT token expired for user: " + decoded.get_subject());
        return std::nullopt;
      }
    }

    AUTH_LOG_DEBUG("JWT token validated for user: " + decoded.get_subject());
    return decoded.get_subject();
  } catch (const std::exception &e) {
    AUTH_LOG_WARN("JWT token validation failed: " + std::string(e.what()));
    return std::nullopt;
  }
}
#endif

#if ETL_ENABLE_JWT
std::string AuthManager::refreshJWTToken(const std::string &token) {
  auto userId = validateJWTToken(token);
  if (!userId.has_value()) {
    AUTH_LOG_ERROR("Cannot refresh invalid JWT token");
    return "";
  }

  // Generate a new token with fresh expiry
  return generateJWTToken(userId.value());
}
#endif
