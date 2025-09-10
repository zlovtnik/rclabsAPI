#include "database_manager.hpp"
#include "database_schema.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <pqxx/pqxx>

struct DatabaseManager::Impl {
    bool connected = false;
    ConnectionConfig config;
    int maxConnections = 10;
    std::unique_ptr<pqxx::connection> conn;
};

DatabaseManager::DatabaseManager() : pImpl(std::make_unique<Impl>()) {}

DatabaseManager::~DatabaseManager() {
    disconnect();
}

bool DatabaseManager::connect(const ConnectionConfig& config) {
    pImpl->config = config;
    
    DB_LOG_INFO("Attempting to connect to PostgreSQL database: " + config.host + ":" + std::to_string(config.port) + "/" + config.database);
    DB_LOG_DEBUG("Using username: " + config.username);
    
    try {
        // Create connection string
        std::string conn_str = "host=" + config.host + 
                              " port=" + std::to_string(config.port) + 
                              " dbname=" + config.database + 
                              " user=" + config.username + 
                              " password=" + config.password;
        
        pImpl->conn = std::make_unique<pqxx::connection>(conn_str);
        
        if (pImpl->conn->is_open()) {
            pImpl->connected = true;
            DB_LOG_INFO("PostgreSQL database connection established successfully");
            return true;
        } else {
            DB_LOG_ERROR("Failed to open PostgreSQL database connection");
            return false;
        }
    } catch (const std::exception& e) {
        DB_LOG_ERROR("PostgreSQL connection failed: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::initializeSchema() {
    if (!isConnected()) {
        DB_LOG_ERROR("Cannot initialize schema: database not connected");
        return false;
    }
    
    DB_LOG_INFO("Initializing PostgreSQL database schema");
    
    try {
        // Create tables
        auto createStatements = DatabaseSchema::getCreateTableStatements();
        for (const auto& stmt : createStatements) {
            if (!executeQuery(stmt)) {
                DB_LOG_ERROR("Failed to create table: " + stmt.substr(0, 50) + "...");
                return false;
            }
        }
        
        // Create indexes
        auto indexStatements = DatabaseSchema::getIndexStatements();
        for (const auto& stmt : indexStatements) {
            if (!executeQuery(stmt)) {
                DB_LOG_ERROR("Failed to create index: " + stmt.substr(0, 50) + "...");
                return false;
            }
        }
        
        // Insert initial data
        auto initialDataStatements = DatabaseSchema::getInitialDataStatements();
        for (const auto& stmt : initialDataStatements) {
            if (!executeQuery(stmt)) {
                DB_LOG_ERROR("Failed to insert initial data: " + stmt.substr(0, 50) + "...");
                return false;
            }
        }
        
        DB_LOG_INFO("Database schema initialized successfully");
        return true;
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Schema initialization failed: " + std::string(e.what()));
        return false;
    }
}

void DatabaseManager::disconnect() {
    if (pImpl->connected && pImpl->conn) {
        DB_LOG_INFO("Disconnecting from PostgreSQL database");
        pImpl->conn.reset(); // Let the connection close automatically
        pImpl->connected = false;
        DB_LOG_DEBUG("PostgreSQL database disconnection completed");
    }
}

bool DatabaseManager::isConnected() const {
    return pImpl->connected && pImpl->conn && pImpl->conn->is_open();
}

bool DatabaseManager::executeQuery(const std::string& query) {
    if (!isConnected()) {
        DB_LOG_ERROR("Cannot execute query: database not connected");
        return false;
    }
    
    DB_LOG_DEBUG("Executing query: " + query.substr(0, 100) + (query.length() > 100 ? "..." : ""));
    
    try {
        pqxx::work txn(*pImpl->conn);
        txn.exec(query);
        txn.commit();
        DB_LOG_DEBUG("Query executed successfully");
        return true;
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Query execution failed: " + std::string(e.what()));
        return false;
    }
}

std::vector<std::vector<std::string>> DatabaseManager::selectQuery(const std::string& query) {
    if (!isConnected()) {
        DB_LOG_ERROR("Cannot execute select query: database not connected");
        return {};
    }
    
    DB_LOG_DEBUG("Executing select query: " + query.substr(0, 100) + (query.length() > 100 ? "..." : ""));
    
    try {
        pqxx::work txn(*pImpl->conn);
        pqxx::result result = txn.exec(query);
        txn.commit();
        
        std::vector<std::vector<std::string>> rows;
        
        // Add column headers
        if (!result.empty()) {
            std::vector<std::string> headers;
            for (size_t col = 0; col < result.columns(); ++col) {
                headers.push_back(result.column_name(col));
            }
            rows.push_back(headers);
        }
        
        // Add data rows
        for (const auto& row : result) {
            std::vector<std::string> dataRow;
            for (const auto& field : row) {
                dataRow.push_back(field.as<std::string>());
            }
            rows.push_back(dataRow);
        }
        
        DB_LOG_DEBUG("Select query completed, returning " + std::to_string(rows.size()) + " rows");
        return rows;
    } catch (const std::exception& e) {
        DB_LOG_ERROR("Select query failed: " + std::string(e.what()));
        return {};
    }
}

bool DatabaseManager::beginTransaction() {
    if (!isConnected()) {
        DB_LOG_ERROR("Cannot begin transaction: database not connected");
        return false;
    }
    
    DB_LOG_DEBUG("Beginning PostgreSQL database transaction");
    // Note: Transaction handling is done in executeQuery and selectQuery
    return true;
}

bool DatabaseManager::commitTransaction() {
    if (!isConnected()) {
        DB_LOG_ERROR("Cannot commit transaction: database not connected");
        return false;
    }
    
    DB_LOG_DEBUG("Committing PostgreSQL database transaction");
    // Note: Transaction handling is done in executeQuery and selectQuery
    return true;
}

bool DatabaseManager::rollbackTransaction() {
    if (!isConnected()) {
        DB_LOG_ERROR("Cannot rollback transaction: database not connected");
        return false;
    }
    
    DB_LOG_WARN("Rolling back PostgreSQL database transaction");
    // Note: Transaction handling is done in executeQuery and selectQuery
    return true;
}

void DatabaseManager::setMaxConnections(int maxConn) {
    pImpl->maxConnections = maxConn;
    DB_LOG_INFO("Set maximum PostgreSQL database connections to " + std::to_string(maxConn));
}
