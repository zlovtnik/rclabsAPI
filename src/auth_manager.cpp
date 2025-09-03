#include "auth_manager.hpp"
#include "logger.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <string_view>
#include <format>
#include <ranges>

AuthManager::AuthManager() {
    AUTH_LOG_INFO("Initializing authentication manager");
    // Create a default admin user for testing
    createUser("admin", "admin@etlplus.com", "admin123");
    AUTH_LOG_DEBUG("Authentication manager initialization completed");
}

bool AuthManager::createUser(const std::string& username, const std::string& email, const std::string& password) {
    AUTH_LOG_DEBUG("Creating user: " + username + " with email: " + email);
    
    // Check if user already exists
    for (const auto& [id, user] : users_) {
        if (user->username == username || user->email == email) {
            AUTH_LOG_ERROR("User creation failed: user already exists with username or email");
            return false;
        }
    }
    
    auto user = std::make_shared<User>();
    user->id = generateSessionId(); // Reuse session ID generator
    user->username = username;
    user->email = email;
    user->passwordHash = hashPassword(password, generateSalt());
    user->roles = {"user"}; // Default role
    user->createdAt = std::chrono::system_clock::now();
    user->isActive = true;
    
    users_[user->id] = user;
    AUTH_LOG_INFO("Created user: " + username + " with ID: " + user->id);
    return true;
}

bool AuthManager::authenticateUser(std::string_view username) const {
    for (const auto& [id, user] : users_) {
        if (user->username == username && user->isActive) {
            // For simplicity, we're not implementing proper password hashing/verification
            // In a real implementation, you'd verify the hashed password
            std::cout << "Authenticated user: " << username << std::endl;
            return true;
        }
    }
    
    std::cerr << "Authentication failed for user: " << username << std::endl;
    return false;
}

bool AuthManager::updateUser(const std::string& userId, const User& updatedUser) {
    if (auto it = users_.find(userId); it != users_.end()) {
        *it->second = updatedUser;
        std::cout << "Updated user: " << userId << std::endl;
        return true;
    }
    return false;
}

bool AuthManager::deleteUser(const std::string& userId) {
    if (auto it = users_.find(userId); it != users_.end()) {
        it->second->isActive = false; // Soft delete
        std::cout << "Deleted user: " << userId << std::endl;
        return true;
    }
    return false;
}

std::shared_ptr<User> AuthManager::getUser(const std::string& userId) const {
    auto it = users_.find(userId);
    return (it != users_.end()) ? it->second : nullptr;
}

std::string AuthManager::createSession(const std::string& userId) {
    if (auto user = getUser(userId); !user || !user->isActive) {
        return "";
    }
    
    auto session = std::make_shared<Session>();
    session->sessionId = generateSessionId();
    session->userId = userId;
    session->createdAt = std::chrono::system_clock::now();
    session->expiresAt = session->createdAt + std::chrono::hours(24); // 24 hour expiry
    session->isValid = true;
    
    sessions_[session->sessionId] = session;
    std::cout << "Created session: " << session->sessionId << " for user: " << userId << std::endl;
    return session->sessionId;
}

bool AuthManager::validateSession(const std::string& sessionId) {
    if (auto it = sessions_.find(sessionId); it != sessions_.end()) {
        auto session = it->second;
        if (session->isValid && std::chrono::system_clock::now() < session->expiresAt) {
            return true;
        } else {
            session->isValid = false; // Mark as invalid
        }
    }
    return false;
}

void AuthManager::revokeSession(const std::string& sessionId) {
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second->isValid = false;
        std::cout << "Revoked session: " << sessionId << std::endl;
    }
}

void AuthManager::cleanupExpiredSessions() {
    auto now = std::chrono::system_clock::now();
    for (const auto& [id, session] : sessions_) {
        if (now >= session->expiresAt) {
            session->isValid = false;
        }
    }
    
    // Remove expired sessions
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (!it->second->isValid) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
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
    if (auto user = getUser(userId); user && std::find(user->roles.begin(), user->roles.end(), role) == user->roles.end()) {
        user->roles.push_back(role);
        std::cout << "Assigned role '" << role << "' to user: " << userId << std::endl;
    }
}

void AuthManager::revokeRole(const std::string& userId, const std::string& role) {
    auto user = getUser(userId);
    if (user) {
        auto it = std::find(user->roles.begin(), user->roles.end(), role);
        if (it != user->roles.end()) {
            user->roles.erase(it);
        }
        std::cout << "Revoked role '" << role << "' from user: " << userId << std::endl;
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
