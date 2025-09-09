#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

struct ConnectionConfig {
  std::string host;
  int port;
  std::string database;
  std::string username;
  std::string password;
};

class DatabaseManager {
public:
  DatabaseManager();
  ~DatabaseManager();

  bool connect(const ConnectionConfig &config);
  void disconnect();
  bool isConnected() const;

  // Schema initialization
  bool initializeSchema();

  // Query operations
  bool executeQuery(const std::string &query);
  std::vector<std::vector<std::string>> selectQuery(const std::string &query);

  // Transaction support
  bool beginTransaction();
  bool commitTransaction();
  bool rollbackTransaction();

  // Connection pooling
  void setMaxConnections(int maxConn);

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};
