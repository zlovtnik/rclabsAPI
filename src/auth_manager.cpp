#include "auth_manager.hpp"
#include "user_repository.hpp"
#include "session_repository.hpp"
#include "logger.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <string_view>
#include <format>
#include <ranges>
#include <chrono>
#include <jwt-cpp/jwt.h>
#include <nlohmann/json.hpp>

AuthManager::AuthManager(std::shared_ptr<DatabaseManager> dbManager)
    : userRepo_(std::make_shared<UserRepository>(dbManager)),
      sessionRepo_(std::make_shared<SessionRepository>(dbManager)) {
    AUTH_LOG_INFO("Initializing authentication manager");
    
    // Load JWT secret from environment variable
    const char* secret = std::getenv("JWT_SECRET_KEY");
    if (!secret || std::string(secret).empty()) {
        throw std::runtime_error("JWT_SECRET_KEY environment variable must be set and non-empty");
    }
    jwtSecretKey_ = secret;
    
    // Note: Default admin user creation is now handled by database schema initialization
    AUTH_LOG_DEBUG("Authentication manager initialization completed");
}

int AuthManager::getJWTExpiryHours() const {
    // Default to 24 hours, can be overridden by config in the future
    return 24;
}

bool AuthManager::createUser(const std::string& username, const std::string& email, const std::string& password) {
    AUTH_LOG_DEBUG("Creating user: " + username + " with email: " + email);
    
    // Check if user already exists
    if (userRepo_->userExists(username, email)) {
        AUTH_LOG_ERROR("User creation failed: user already exists with username or email");
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

bool AuthManager::authenticateUser(std::string_view username) const {
    auto user = userRepo_->getUserByUsername(std::string(username));
    if (user && user->isActive) {
        // For simplicity, we're not implementing proper password hashing/verification
        // In a real implementation, you'd verify the hashed password
        AUTH_LOG_INFO("Authenticated user: " + std::string(username));
        return true;
    }
    
    AUTH_LOG_ERROR("Authentication failed for user: " + std::string(username));
    return false;
}

bool AuthManager::authenticateUser(std::string_view username, std::string_view password) const {
    if (password.empty()) {
        AUTH_LOG_ERROR("Authentication failed for user: " + std::string(username) + " - empty password");
        return false;
    }
    
    auto user = userRepo_->getUserByUsername(std::string(username));
    if (!user || !user->isActive) {
        AUTH_LOG_ERROR("Authentication failed for user: " + std::string(username) + " - user not found or inactive");
        return false;
    }
    
    // For now, do a simple string comparison (in production, use proper password hashing)
    if (user->passwordHash != std::string(password)) {
        AUTH_LOG_ERROR("Authentication failed for user: " + std::string(username) + " - invalid password");
        return false;
    }
    
    AUTH_LOG_INFO("Authenticated user: " + std::string(username) + " with password");
    return true;
}

bool AuthManager::updateUser(const std::string& userId, const User& updatedUser) {
    if (updatedUser.id != userId) {
        AUTH_LOG_ERROR("User ID mismatch: expected " + userId + ", got " + updatedUser.id);
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

bool AuthManager::deleteUser(const std::string& userId) {
    if (userRepo_->deleteUser(userId)) {
        AUTH_LOG_INFO("Deleted user: " + userId);
        return true;
    } else {
        AUTH_LOG_ERROR("Failed to delete user: " + userId);
        return false;
    }
}

std::shared_ptr<User> AuthManager::getUser(const std::string& userId) const {
    auto user = userRepo_->getUserById(userId);
    if (user) {
        return std::make_shared<User>(*user);
    }
    return nullptr;
}

std::optional<User> AuthManager::getUserByUsername(const std::string& username) const {
    return userRepo_->getUserByUsername(username);
}

std::string AuthManager::createSession(const std::string& userId) {
    auto user = userRepo_->getUserById(userId);
    if (!user || !user->isActive) {
        AUTH_LOG_ERROR("Cannot create session for invalid or inactive user: " + userId);
        return "";
    }
    
    Session session;
    session.sessionId = generateSessionId();
    session.userId = userId;
    session.createdAt = std::chrono::system_clock::now();
    session.expiresAt = session.createdAt + std::chrono::hours(24); // 24 hour expiry
    session.isValid = true;
    
    if (sessionRepo_->createSession(session)) {
        AUTH_LOG_INFO("Created session: " + session.sessionId + " for user: " + userId);
        return session.sessionId;
    } else {
        AUTH_LOG_ERROR("Failed to create session in database");
        return "";
    }
}

bool AuthManager::validateSession(const std::string& sessionId) {
    auto session = sessionRepo_->getSessionById(sessionId);
    if (session && session->isValid && std::chrono::system_clock::now() < session->expiresAt) {
        return true;
    } else if (session && !session->isValid) {
        // Session exists but is invalid, update it in database
        Session updatedSession = *session;
        updatedSession.isValid = false;
        sessionRepo_->updateSession(updatedSession);
    }
    return false;
}

void AuthManager::revokeSession(const std::string& sessionId) {
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

bool AuthManager::hasPermission(std::string_view userId, std::string_view resource, std::string_view action) const {
    auto user = getUser(std::string(userId));
    if (!user || !user->isActive) {
        return false;
    }
    
    // Simple role-based permissions
    return std::any_of(user->roles.begin(), user->roles.end(), [&](const std::string& role) {
        if (role == "admin") {
            return true; // Admin has all permissions
        } else if (role == "user" && resource == "jobs" && (action == "read" || action == "create")) {
            return true;
        }
        return false;
    });
}

void AuthManager::assignRole(const std::string& userId, const std::string& role) {
    auto userOpt = userRepo_->getUserById(userId);
    if (!userOpt) {
        AUTH_LOG_ERROR("User not found: " + userId);
        return;
    }
    
    User user = *userOpt;
    if (std::find(user.roles.begin(), user.roles.end(), role) == user.roles.end()) {
        user.roles.push_back(role);
        if (userRepo_->updateUser(user)) {
            AUTH_LOG_INFO("Assigned role '" + role + "' to user: " + userId);
        } else {
            AUTH_LOG_ERROR("Failed to assign role to user: " + userId);
        }
    }
}

void AuthManager::revokeRole(const std::string& userId, const std::string& role) {
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

std::string AuthManager::hashPassword(std::string_view password, std::string_view salt) const {
    // Simple hash implementation (not secure - use bcrypt in production)
    return std::string(password) + "_hashed_with_" + std::string(salt);
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

std::string AuthManager::generateJWTToken(const std::string &userId) {
    auto user = userRepo_->getUserById(userId);
    if (!user || !user->isActive) {
        AUTH_LOG_ERROR("Cannot generate JWT token for invalid or inactive user: " + userId);
        return "";
    }

    try {
        auto now = std::chrono::system_clock::now();
        auto expiry = now + std::chrono::hours(getJWTExpiryHours());

        auto token = jwt::create()
            .set_type("JWT")
            .set_issuer("etl-backend")
            .set_subject(userId)
            .set_issued_at(now)
            .set_expires_at(expiry)
            .set_payload_claim("username", jwt::claim(std::string(user->username)))
            .set_payload_claim("roles", jwt::claim(nlohmann::json(user->roles).dump()))
            .sign(jwt::algorithm::hs256{jwtSecretKey_});

        AUTH_LOG_INFO("Generated JWT token for user: " + userId);
        return token;
    } catch (const std::exception& e) {
        AUTH_LOG_ERROR("Failed to generate JWT token for user " + userId + ": " + e.what());
        return "";
    }
}

std::optional<std::string> AuthManager::validateJWTToken(const std::string &token) {
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .with_issuer("etl-backend")
            .allow_algorithm(jwt::algorithm::hs256{jwtSecretKey_});

        verifier.verify(decoded);

        // Check if token is expired
        auto exp = decoded.get_expires_at();
        if (exp != jwt::date{}) {
            auto now = std::chrono::system_clock::now();
            if (now > exp) {
                AUTH_LOG_WARN("JWT token expired for user: " + decoded.get_subject());
                return std::nullopt;
            }
        }

        AUTH_LOG_DEBUG("JWT token validated for user: " + decoded.get_subject());
        return decoded.get_subject();
    } catch (const std::exception& e) {
        AUTH_LOG_WARN("JWT token validation failed: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::string AuthManager::refreshJWTToken(const std::string &token) {
    auto userId = validateJWTToken(token);
    if (!userId.has_value()) {
        AUTH_LOG_ERROR("Cannot refresh invalid JWT token");
        return "";
    }

    // Generate a new token with fresh expiry
    return generateJWTToken(userId.value());
}
