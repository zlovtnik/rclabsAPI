#pragma once

#ifdef ETL_ENABLE_POSTGRESQL
#include "database_connection_pool.hpp"
#endif
#include <future>
#include <memory>
#include <string>
#include <vector>

#ifdef ETL_ENABLE_POSTGRESQL

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
  bool executeQuery(const std::string &query,
                    const std::vector<std::string> &params);
  std::vector<std::vector<std::string>> selectQuery(const std::string &query);
  std::vector<std::vector<std::string>>
  selectQuery(const std::string &query, const std::vector<std::string> &params);

  // Transaction support
  bool beginTransaction();
  bool commitTransaction();
  bool rollbackTransaction();

  // Connection pooling
  void setMaxConnections(int maxConn);
  DatabaseConnectionPool::PoolMetrics getPoolMetrics() const;
  bool isPoolHealthy() const;

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;

  // Helper method to execute parameterized queries
  pqxx::result
  executeParameterizedQuery(pqxx::transaction_base &txn,
                            const std::string &query,
                            const std::vector<std::string> &params);
};

#endif // ETL_ENABLE_POSTGRESQL
