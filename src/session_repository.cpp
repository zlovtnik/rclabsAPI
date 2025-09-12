#include "session_repository.hpp"
#include "database_manager.hpp"
#include "logger.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <pqxx/pqxx>
#include <sstream>

SessionRepository::SessionRepository(std::shared_ptr<DatabaseManager> dbManager)
    : dbManager_(dbManager) {}

bool SessionRepository::createSession(const Session &session) {
  if (!dbManager_ || !dbManager_->isConnected()) {
    AUTH_LOG_ERROR("Database not connected");
    return false;
  }

  try {
    std::string createdAtStr = timePointToString(session.createdAt);
    std::string expiresAtStr = timePointToString(session.expiresAt);

    std::string query = "INSERT INTO sessions (session_id, user_id, "
                        "created_at, expires_at, is_valid) "
                        "VALUES ('" +
                        session.sessionId + "', '" + session.userId + "', '" +
                        createdAtStr + "', '" + expiresAtStr + "', " +
                        (session.isValid ? "true" : "false") + ")";

    return dbManager_->executeQuery(query);
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to create session: " + std::string(e.what()));
    return false;
  }
}

std::optional<Session>
SessionRepository::getSessionById(const std::string &sessionId) {
  if (!dbManager_ || !dbManager_->isConnected()) {
    AUTH_LOG_ERROR("Database not connected");
    return std::nullopt;
  }

  try {
    std::string query =
        "SELECT session_id, user_id, created_at, expires_at, is_valid "
        "FROM sessions WHERE session_id = '" +
        sessionId + "'";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return std::nullopt;
    }

    return sessionFromRow(result[1]);
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to get session by ID: " + std::string(e.what()));
    return std::nullopt;
  }
}

std::vector<Session>
SessionRepository::getSessionsByUserId(const std::string &userId) {
  std::vector<Session> sessions;

  if (!dbManager_ || !dbManager_->isConnected()) {
    AUTH_LOG_ERROR("Database not connected");
    return sessions;
  }

  try {
    std::string query =
        "SELECT session_id, user_id, created_at, expires_at, is_valid "
        "FROM sessions WHERE user_id = '" +
        userId + "' ORDER BY created_at DESC";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return sessions;
    }

    for (size_t i = 1; i < result.size(); ++i) {
      Session session = sessionFromRow(result[i]);
      sessions.push_back(session);
    }
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to get sessions by user ID: " +
                   std::string(e.what()));
  }

  return sessions;
}

std::vector<Session> SessionRepository::getAllSessions() {
  std::vector<Session> sessions;

  if (!dbManager_ || !dbManager_->isConnected()) {
    AUTH_LOG_ERROR("Database not connected");
    return sessions;
  }

  try {
    std::string query =
        "SELECT session_id, user_id, created_at, expires_at, is_valid "
        "FROM sessions ORDER BY created_at DESC";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return sessions;
    }

    for (size_t i = 1; i < result.size(); ++i) {
      Session session = sessionFromRow(result[i]);
      sessions.push_back(session);
    }
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to get all sessions: " + std::string(e.what()));
  }

  return sessions;
}

bool SessionRepository::updateSession(const Session &session) {
  if (!dbManager_ || !dbManager_->isConnected()) {
    AUTH_LOG_ERROR("Database not connected");
    return false;
  }

  try {
    std::string expiresAtStr = timePointToString(session.expiresAt);

    std::string query =
        "UPDATE sessions SET expires_at = '" + expiresAtStr +
        "', is_valid = " + (session.isValid ? "true" : "false") +
        " WHERE session_id = '" + session.sessionId + "'";

    return dbManager_->executeQuery(query);
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to update session: " + std::string(e.what()));
    return false;
  }
}

bool SessionRepository::deleteSession(const std::string &sessionId) {
  if (!dbManager_ || !dbManager_->isConnected()) {
    AUTH_LOG_ERROR("Database not connected");
    return false;
  }

  try {
    std::string query =
        "DELETE FROM sessions WHERE session_id = '" + sessionId + "'";
    return dbManager_->executeQuery(query);
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to delete session: " + std::string(e.what()));
    return false;
  }
}

bool SessionRepository::deleteExpiredSessions() {
  if (!dbManager_ || !dbManager_->isConnected()) {
    AUTH_LOG_ERROR("Database not connected");
    return false;
  }

  try {
    std::string nowStr = timePointToString(std::chrono::system_clock::now());
    std::string query =
        "DELETE FROM sessions WHERE expires_at < '" + nowStr + "'";
    return dbManager_->executeQuery(query);
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to delete expired sessions: " +
                   std::string(e.what()));
    return false;
  }
}

std::vector<Session> SessionRepository::getValidSessions() {
  std::vector<Session> sessions;

  if (!dbManager_ || !dbManager_->isConnected()) {
    AUTH_LOG_ERROR("Database not connected");
    return sessions;
  }

  try {
    std::string nowStr = timePointToString(std::chrono::system_clock::now());
    std::string query =
        "SELECT session_id, user_id, created_at, expires_at, is_valid "
        "FROM sessions WHERE is_valid = true AND expires_at > '" +
        nowStr + "' ORDER BY created_at DESC";

    auto result = dbManager_->selectQuery(query);
    if (result.size() <= 1) {
      return sessions;
    }

    for (size_t i = 1; i < result.size(); ++i) {
      Session session = sessionFromRow(result[i]);
      sessions.push_back(session);
    }
  } catch (const std::exception &e) {
    AUTH_LOG_ERROR("Failed to get valid sessions: " + std::string(e.what()));
  }

  return sessions;
}

Session SessionRepository::sessionFromRow(const std::vector<std::string> &row) {
  if (row.size() < 5) {
    throw std::runtime_error("Invalid session row data");
  }

  Session session;
  session.sessionId = row[0];
  session.userId = row[1];
  session.createdAt = stringToTimePoint(row[2]);
  session.expiresAt = stringToTimePoint(row[3]);
  session.isValid = (row[4] == "t" || row[4] == "true");

  return session;
}

std::string SessionRepository::timePointToString(
    const std::chrono::system_clock::time_point &tp) {
  auto time = std::chrono::system_clock::to_time_t(tp);
  std::tm tm = *std::gmtime(&time);
  std::stringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

std::chrono::system_clock::time_point
SessionRepository::stringToTimePoint(const std::string &str) {
  std::tm tm = {};
  std::istringstream ss(str);
  ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
  if (ss.fail()) {
    return std::chrono::system_clock::now();
  }
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}
