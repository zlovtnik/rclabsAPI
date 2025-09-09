#pragma once

#include <chrono>
#include <string>
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
