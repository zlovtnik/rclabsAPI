#pragma once

#include "session_model.hpp"
#include "user.hpp"
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
#ifdef ETL_ENABLE_POSTGRESQL
  AuthManager(std::shared_ptr<DatabaseManager> dbManager);
#endif
#ifndef ETL_ENABLE_POSTGRESQL
  AuthManager(std::shared_ptr<UserRepository> userRepo,
              std::shared_ptr<SessionRepository> sessionRepo);
#endif
  ~AuthManager();

  /**
   * @brief Deleted copy constructor to prevent copying of AuthManager
   * instances.
   *
   * Prevents accidental duplication of sensitive internal state (e.g., secret
   * key material). AuthManager instances are non-copyable.
   */
  AuthManager(const AuthManager &) = delete;
  /**
   * @brief Deleted copy-assignment operator to prevent copying of AuthManager.
   *
   * AuthManager holds sensitive key material and database-backed resources;
   * copying or assigning an instance is disallowed to avoid duplicating or
   * accidentally sharing those secrets and resources.
   */
  AuthManager &operator=(const AuthManager &) = delete;
  /**
   * @brief Deleted move constructor to prevent moving instances.
   *
   * Prevents transfer of ownership of internal sensitive resources (for
   * example, JWT secret key material). AuthManager objects are intentionally
   * neither copyable nor movable.
   */
  AuthManager(AuthManager &&) = delete;
  /**
   * @brief Deleted move assignment operator.
   *
   * AuthManager is intentionally non-movable to prevent duplication or
   * inadvertent transfer of sensitive key material and associated resources.
   */
  AuthManager &operator=(AuthManager &&) = delete;

  // User management
#ifdef ETL_ENABLE_POSTGRESQL
  bool createUser(const std::string &username, const std::string &email,
                  const std::string &password);
  bool userExists(std::string_view username) const;
  bool authenticateUser(std::string_view username,
                        std::string_view password) const;
  bool updateUser(const std::string &userId, const User &updatedUser);
  bool deleteUser(const std::string &userId);
  std::shared_ptr<User> getUser(const std::string &userId) const;
  std::optional<User> getUserByUsername(const std::string &username) const;
#endif

  // Session management
#ifdef ETL_ENABLE_POSTGRESQL
  std::string createSession(const std::string &userId);
  bool validateSession(const std::string &sessionId);
  void revokeSession(const std::string &sessionId);
  void cleanupExpiredSessions();
#endif

  // Authorization
#ifdef ETL_ENABLE_POSTGRESQL
  bool hasPermission(std::string_view userId, std::string_view resource,
                     std::string_view action) const;
  void assignRole(const std::string &userId, const std::string &role);
  void revokeRole(const std::string &userId, const std::string &role);
#endif

  // JWT configuration
#ifdef ETL_ENABLE_JWT
  std::chrono::hours getJWTExpiryHours() const;
#endif

private:
#ifdef ETL_ENABLE_POSTGRESQL
  std::shared_ptr<DatabaseManager> dbManager_;
#endif
  std::shared_ptr<UserRepository> userRepo_;
  std::shared_ptr<SessionRepository> sessionRepo_;
#ifdef ETL_ENABLE_JWT
  void loadJWTSecret();
#endif
  std::string hashPassword(std::string_view password,
                           std::string_view salt) const;
  std::string generateSalt() const;
  std::string generateSessionId() const;
  bool verifyPassword(std::string_view password,
                      std::string_view hashedPassword) const;
  bool constantTimeCompare(std::string_view a, std::string_view b) const;
};
