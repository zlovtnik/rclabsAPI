#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct User {
  std::string id;
  std::string username;
  std::string email;
  std::string passwordHash;
  std::vector<std::string> roles;
  std::chrono::system_clock::time_point createdAt;
  bool isActive;
};

struct Session {
  std::string sessionId;
  std::string userId;
  std::chrono::system_clock::time_point createdAt;
  std::chrono::system_clock::time_point expiresAt;
  bool isValid;
};

class DatabaseManager;
class UserRepository;
class SessionRepository;

class AuthManager {
public:
  AuthManager(std::shared_ptr<DatabaseManager> dbManager);

  // User management
  bool createUser(const std::string &username, const std::string &email,
                  const std::string &password);
  bool authenticateUser(std::string_view username) const;
  bool updateUser(const std::string &userId, const User &updatedUser);
  bool deleteUser(const std::string &userId);
  std::shared_ptr<User> getUser(const std::string &userId) const;

  // Session management
  std::string createSession(const std::string &userId);
  bool validateSession(const std::string &sessionId);
  void revokeSession(const std::string &sessionId);
  void cleanupExpiredSessions();

  // Authorization
  bool hasPermission(std::string_view userId, std::string_view resource,
                     std::string_view action) const;
  void assignRole(const std::string &userId, const std::string &role);
  void revokeRole(const std::string &userId, const std::string &role);

private:
  std::shared_ptr<UserRepository> userRepo_;
  std::shared_ptr<SessionRepository> sessionRepo_;

  std::string hashPassword(std::string_view password,
                           std::string_view salt) const;
  std::string generateSalt() const;
  std::string generateSessionId() const;
  bool verifyPassword(std::string_view password, std::string_view hash,
                      std::string_view salt) const;
};
