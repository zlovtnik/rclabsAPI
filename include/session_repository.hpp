#pragma once

#include "session_model.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

class DatabaseManager;

class SessionRepository {
public:
  explicit SessionRepository(std::shared_ptr<DatabaseManager> dbManager);

  // CRUD operations
  bool createSession(const Session &session);
  std::optional<Session> getSessionById(const std::string &sessionId);
  std::vector<Session> getSessionsByUserId(const std::string &userId);
  std::vector<Session> getAllSessions();
  bool updateSession(const Session &session);
  bool deleteSession(const std::string &sessionId);
  bool deleteExpiredSessions();

  // Additional operations
  std::vector<Session> getValidSessions();

private:
  std::shared_ptr<DatabaseManager> dbManager_;

  Session sessionFromRow(const std::vector<std::string> &row);
  std::string
  timePointToString(const std::chrono::system_clock::time_point &tp);
  std::chrono::system_clock::time_point
  stringToTimePoint(const std::string &str);
};
