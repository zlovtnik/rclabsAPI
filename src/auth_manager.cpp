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

AuthManager::AuthManager(std::shared_ptr<DatabaseManager> dbManager)
    : userRepo_(std::make_shared<UserRepository>(dbManager)),
      sessionRepo_(std::make_shared<SessionRepository>(dbManager)) {
    AUTH_LOG_INFO("Initializing authentication manager");
    // Note: Default admin user creation is now handled by database schema initialization
    AUTH_LOG_DEBUG("Authentication manager initialization completed");
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

bool AuthManager::updateUser(const std::string& userId, const User& updatedUser) {
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

bool AuthManager::verifyPassword(std::string_view password, std::string_view hash, std::string_view salt) const {
    return hashPassword(password, salt) == hash;
}
