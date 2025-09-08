#include "user_repository.hpp"
#include "database_manager.hpp"
#include "logger.hpp"
#include <sstream>
#include <iomanip>
#include <pqxx/pqxx>
#include <chrono>
#include <ctime>

UserRepository::UserRepository(std::shared_ptr<DatabaseManager> dbManager)
    : dbManager_(dbManager) {}

bool UserRepository::createUser(const User& user) {
    if (!dbManager_ || !dbManager_->isConnected()) {
        DB_LOG_ERROR("Database not connected");
        return false;
    }
    
    try {
        std::string rolesStr = rolesToString(user.roles);
        std::string createdAtStr = std::format("{:%Y-%m-%d %H:%M:%S}",
            std::chrono::floor<std::chrono::seconds>(user.createdAt));
        
        std::string query = "INSERT INTO users (id, username, email, password_hash, roles, created_at, is_active) "
                           "VALUES ('" + user.id + "', '" + user.username + "', '" + user.email + "', '" + 
                           user.passwordHash + "', '" + rolesStr + "', '" + createdAtStr + "', " + 
                           (user.isActive ? "true" : "false") + ")";
        
        return dbManager_->executeQuery(query);
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to create user: " + std::string(e.what()));
        return false;
    }
}

std::optional<User> UserRepository::getUserById(const std::string& userId) {
    if (!dbManager_ || !dbManager_->isConnected()) {
        DB_LOG_ERROR("Database not connected");
        return std::nullopt;
    }
    
    try {
        std::string query = "SELECT id, username, email, password_hash, roles, created_at, is_active "
                           "FROM users WHERE id = '" + userId + "'";
        
        auto result = dbManager_->selectQuery(query);
        if (result.size() <= 1) { // Only headers or no data
            return std::nullopt;
        }
        
        return userFromRow(result[1]); // First data row
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to get user by ID: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<User> UserRepository::getUserByUsername(const std::string& username) {
    if (!dbManager_ || !dbManager_->isConnected()) {
        DB_LOG_ERROR("Database not connected");
        return std::nullopt;
    }
    
    try {
        std::string query = "SELECT id, username, email, password_hash, roles, created_at, is_active "
                           "FROM users WHERE username = '" + username + "'";
        
        auto result = dbManager_->selectQuery(query);
        if (result.size() <= 1) {
            return std::nullopt;
        }
        
        return userFromRow(result[1]);
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to get user by username: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<User> UserRepository::getUserByEmail(const std::string& email) {
    if (!dbManager_ || !dbManager_->isConnected()) {
        DB_LOG_ERROR("Database not connected");
        return std::nullopt;
    }
    
    try {
        std::string query = "SELECT id, username, email, password_hash, roles, created_at, is_active "
                           "FROM users WHERE email = '" + email + "'";
        
        auto result = dbManager_->selectQuery(query);
        if (result.size() <= 1) {
            return std::nullopt;
        }
        
        return userFromRow(result[1]);
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to get user by email: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::vector<User> UserRepository::getAllUsers() {
    std::vector<User> users;
    
    if (!dbManager_ || !dbManager_->isConnected()) {
        DB_LOG_ERROR("Database not connected");
        return users;
    }
    
    try {
        std::string query = "SELECT id, username, email, password_hash, roles, created_at, is_active "
                           "FROM users ORDER BY created_at DESC";
        
        auto result = dbManager_->selectQuery(query);
        if (result.size() <= 1) {
            return users;
        }
        
        for (size_t i = 1; i < result.size(); ++i) {
            User user = userFromRow(result[i]);
            users.push_back(user);
        }
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to get all users: " + std::string(e.what()));
    }
    
    return users;
}

bool UserRepository::updateUser(const User& user) {
    if (!dbManager_ || !dbManager_->isConnected()) {
        DB_LOG_ERROR("Database not connected");
        return false;
    }
    
    try {
        std::string rolesStr = rolesToString(user.roles);
        
        std::string query = "UPDATE users SET username = '" + user.username + "', email = '" + user.email + 
                           "', password_hash = '" + user.passwordHash + "', roles = '" + rolesStr + 
                           "', is_active = " + (user.isActive ? "true" : "false") + " WHERE id = '" + user.id + "'";
        
        return dbManager_->executeQuery(query);
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to update user: " + std::string(e.what()));
        return false;
    }
}

bool UserRepository::deleteUser(const std::string& userId) {
    if (!dbManager_ || !dbManager_->isConnected()) {
        DB_LOG_ERROR("Database not connected");
        return false;
    }
    
    try {
        std::string query = "DELETE FROM users WHERE id = '" + userId + "'";
        return dbManager_->executeQuery(query);
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to delete user: " + std::string(e.what()));
        return false;
    }
}

bool UserRepository::userExists(const std::string& username, const std::string& email) {
    if (!dbManager_ || !dbManager_->isConnected()) {
        return false;
    }
    
    try {
        std::string query = "SELECT COUNT(*) FROM users WHERE username = '" + username + "'";
        if (!email.empty()) {
            query += " OR email = '" + email + "'";
        }
        
        auto result = dbManager_->selectQuery(query);
        if (result.size() > 1 && result[1].size() > 0) {
            return std::stoi(result[1][0]) > 0;
        }
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to check user existence: " + std::string(e.what()));
    }
    
    return false;
}

std::vector<User> UserRepository::getUsersByRole(const std::string& role) {
    std::vector<User> users;
    
    if (!dbManager_ || !dbManager_->isConnected()) {
        DB_LOG_ERROR("Database not connected");
        return users;
    }
    
    try {
        std::string query = "SELECT id, username, email, password_hash, roles, created_at, is_active "
                           "FROM users WHERE '" + role + "' = ANY(roles) ORDER BY created_at DESC";
        
        auto result = dbManager_->selectQuery(query);
        if (result.size() <= 1) {
            return users;
        }
        
        for (size_t i = 1; i < result.size(); ++i) {
            User user = userFromRow(result[i]);
            users.push_back(user);
        }
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Failed to get users by role: " + std::string(e.what()));
    }
    
    return users;
}

User UserRepository::userFromRow(const std::vector<std::string>& row) {
    if (row.size() < 7) {
        throw std::runtime_error("Invalid user row data");
    }
    
    User user;
    user.id = row[0];
    user.username = row[1];
    user.email = row[2];
    user.passwordHash = row[3];
    user.roles = stringToRoles(row[4]);
    
    // Parse timestamp
    std::tm tm = {};
    std::istringstream ss(row[5]);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        // Fallback to current time if parsing fails
        user.createdAt = std::chrono::system_clock::now();
    } else {
        user.createdAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
    
    user.isActive = (row[6] == "t" || row[6] == "true");
    
    return user;
}

std::string UserRepository::rolesToString(const std::vector<std::string>& roles) {
    if (roles.empty()) {
        return "{}";
    }
    
    std::string result = "{";
    for (size_t i = 0; i < roles.size(); ++i) {
        if (i > 0) result += ",";
        result += "\"" + roles[i] + "\"";
    }
    result += "}";
    return result;
}

std::vector<std::string> UserRepository::stringToRoles(const std::string& rolesStr) {
    std::vector<std::string> roles;
    
    if (rolesStr.empty() || rolesStr == "{}") {
        return roles;
    }
    
    std::string content = rolesStr.substr(1, rolesStr.size() - 2); // Remove { }
    std::stringstream ss(content);
    std::string role;
    
    while (std::getline(ss, role, ',')) {
        // Remove quotes
        if (!role.empty() && role.front() == '"') role = role.substr(1);
        if (!role.empty() && role.back() == '"') role = role.substr(0, role.size() - 1);
        if (!role.empty()) {
            roles.push_back(role);
        }
    }
    
    return roles;
}
