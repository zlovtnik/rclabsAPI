#pragma once

#include <chrono>
#include <string>

struct Session {
  std::string sessionId;
  std::string userId;
  std::chrono::system_clock::time_point createdAt;
  std::chrono::system_clock::time_point expiresAt;
  bool isValid;
};
