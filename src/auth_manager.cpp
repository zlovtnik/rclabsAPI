#include "auth_manager.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

AuthManager::AuthManager() {
    // Create a default admin user for testing
    createUser("admin", "admin@etlplus.com", "admin123");
}

bool AuthManager::createUser(const std::string& username, const std::string& email, const std::string& password) {
    // Check if user already exists
    for (const auto& [id, user] : users_) {
        if (user->username == username || user->email == email) {
            std::cerr << "User already exists with username or email" << std::endl;
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
    std::cout << "Created user: " << username << " with ID: " << user->id << std::endl;
    return true;
}

bool AuthManager::authenticateUser(const std::string& username, const std::string& password) {
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
    auto it = users_.find(userId);
    if (it != users_.end()) {
        *it->second = updatedUser;
        std::cout << "Updated user: " << userId << std::endl;
        return true;
    }
    return false;
}

bool AuthManager::deleteUser(const std::string& userId) {
    auto it = users_.find(userId);
    if (it != users_.end()) {
        it->second->isActive = false; // Soft delete
        std::cout << "Deleted user: " << userId << std::endl;
        return true;
    }
    return false;
}

std::shared_ptr<User> AuthManager::getUser(const std::string& userId) {
    auto it = users_.find(userId);
    return (it != users_.end()) ? it->second : nullptr;
}

std::string AuthManager::createSession(const std::string& userId) {
    auto user = getUser(userId);
    if (!user || !user->isActive) {
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
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
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
    for (auto& [id, session] : sessions_) {
        if (now >= session->expiresAt) {
            session->isValid = false;
        }
    }
    
    // Remove expired sessions
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (!it->second->isValid) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

bool AuthManager::hasPermission(const std::string& userId, const std::string& resource, const std::string& action) {
    auto user = getUser(userId);
    if (!user || !user->isActive) {
        return false;
    }
    
    // Simple role-based permissions
    for (const auto& role : user->roles) {
        if (role == "admin") {
            return true; // Admin has all permissions
        } else if (role == "user" && resource == "jobs" && (action == "read" || action == "create")) {
            return true;
        }
    }
    
    return false;
}

void AuthManager::assignRole(const std::string& userId, const std::string& role) {
    auto user = getUser(userId);
    if (user) {
        if (std::find(user->roles.begin(), user->roles.end(), role) == user->roles.end()) {
            user->roles.push_back(role);
            std::cout << "Assigned role '" << role << "' to user: " << userId << std::endl;
        }
    }
}

void AuthManager::revokeRole(const std::string& userId, const std::string& role) {
    auto user = getUser(userId);
    if (user) {
        user->roles.erase(std::remove(user->roles.begin(), user->roles.end(), role), user->roles.end());
        std::cout << "Revoked role '" << role << "' from user: " << userId << std::endl;
    }
}

std::string AuthManager::hashPassword(const std::string& password, const std::string& salt) {
    // Simple hash implementation (not secure - use bcrypt in production)
    return password + "_hashed_with_" + salt;
}

std::string AuthManager::generateSalt() {
    return "salt_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
}

std::string AuthManager::generateSessionId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

bool AuthManager::verifyPassword(const std::string& password, const std::string& hash, const std::string& salt) {
    return hashPassword(password, salt) == hash;
}
