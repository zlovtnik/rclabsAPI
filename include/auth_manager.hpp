#pragma once

#include "user.hpp"
#include "session_model.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#ifdef ETL_ENABLE_JWT
#include <chrono>
#endif

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
  bool authenticateUser(std::string_view username, std::string_view password) const;
  bool updateUser(const std::string &userId, const User &updatedUser);
  bool deleteUser(const std::string &userId);
  std::shared_ptr<User> getUser(const std::string &userId) const;
  std::optional<User> getUserByUsername(const std::string &username) const;

  // JWT Token management
#ifdef ETL_ENABLE_JWT
  std::string generateJWTToken(const std::string &userId);
  std::optional<std::string> validateJWTToken(const std::string &token);
  std::string refreshJWTToken(const std::string &token);
#endif

  // Session management (legacy - to be deprecated)
  std::string createSession(const std::string &userId);
  bool validateSession(const std::string &sessionId);
  void revokeSession(const std::string &sessionId);
  void cleanupExpiredSessions();

  // Authorization
  bool hasPermission(std::string_view userId, std::string_view resource,
                     std::string_view action) const;
  void assignRole(const std::string &userId, const std::string &role);
  void revokeRole(const std::string &userId, const std::string &role);

  // JWT configuration
#ifdef ETL_ENABLE_JWT
  std::chrono::hours getJWTExpiryHours() const;
#endif

private:
  std::shared_ptr<UserRepository> userRepo_;
  std::shared_ptr<SessionRepository> sessionRepo_;
#ifdef ETL_ENABLE_JWT
  std::string jwtSecretKey_;
#endif

  std::string hashPassword(std::string_view password,
                           std::string_view salt) const;
  std::string generateSalt() const;
  std::string generateSessionId() const;
  bool verifyPassword(std::string_view password, std::string_view hashedPassword) const;
  bool constantTimeCompare(std::string_view a, std::string_view b) const;
};
