#pragma once

#include "user.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

class DatabaseManager;

class UserRepository {
public:
  explicit UserRepository(std::shared_ptr<DatabaseManager> dbManager);

  // CRUD operations
  bool createUser(const User &user);
  std::optional<User> getUserById(const std::string &userId);
  std::optional<User> getUserByUsername(const std::string &username);
  std::optional<User> getUserByEmail(const std::string &email);
  std::vector<User> getAllUsers();
  bool updateUser(const User &user);
  bool deleteUser(const std::string &userId);

  // Additional operations
  bool userExists(const std::string &username, const std::string &email = "");
  std::vector<User> getUsersByRole(const std::string &role);

private:
  std::shared_ptr<DatabaseManager> dbManager_;

  User userFromRow(const std::vector<std::string> &row);
  std::string rolesToString(const std::vector<std::string> &roles);
  std::vector<std::string> stringToRoles(const std::string &rolesStr);
  std::chrono::system_clock::time_point
  parseTimestamp(const std::string &timestampStr);
};
