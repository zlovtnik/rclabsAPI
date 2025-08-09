#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

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

class AuthManager {
public:
  AuthManager();

  // User management
  bool createUser(const std::string &username, const std::string &email,
                  const std::string &password);
  bool authenticateUser(const std::string &username,
                        const std::string &password);
  bool updateUser(const std::string &userId, const User &updatedUser);
  bool deleteUser(const std::string &userId);
  std::shared_ptr<User> getUser(const std::string &userId);

  // Session management
  std::string createSession(const std::string &userId);
  bool validateSession(const std::string &sessionId);
  void revokeSession(const std::string &sessionId);
  void cleanupExpiredSessions();

  // Authorization
  bool hasPermission(const std::string &userId, const std::string &resource,
                     const std::string &action);
  void assignRole(const std::string &userId, const std::string &role);
  void revokeRole(const std::string &userId, const std::string &role);

private:
  std::unordered_map<std::string, std::shared_ptr<User>> users_;
  std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;

  std::string hashPassword(const std::string &password,
                           const std::string &salt);
  std::string generateSalt();
  std::string generateSessionId();
  bool verifyPassword(const std::string &password, const std::string &hash,
                      const std::string &salt);
};
