#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>

#include "auth_manager.hpp"
#include "config_manager.hpp"
#include "data_transformer.hpp"
#include "database_manager.hpp"
#include "etl_job_manager.hpp"
#include "http_server.hpp"
#include "log_aggregation_config.hpp"
#include "log_aggregator.hpp"
#include "logger.hpp"
#include "request_handler.hpp"
#include "websocket_manager.hpp"

std::unique_ptr<HttpServer> server;

void signalHandler(int signal) {
  LOG_INFO("Main", "Received signal " + std::to_string(signal) +
                       ". Shutting down gracefully...");
  if (server) {
    server->stop();
  }
  exit(0);
}

int main() {
  try {
    // Load configuration first (with basic logging)
    auto &config = ConfigManager::getInstance();
    if (!config.loadConfig("config.json")) {
      std::cerr << "Failed to load configuration" << std::endl;
      return 1;
    }

    std::cout << "Configuration loaded, initializing logger..." << std::endl;

    // Initialize enhanced logging system with configuration
    auto &logger = Logger::getInstance();
    LogConfig logConfig = config.getLoggingConfig();

    std::cout << "Logger config created, configuring logger..." << std::endl;
    logger.configure(logConfig);

    std::cout << "Logger configured, starting application..." << std::endl;

    LOG_INFO("Main", "Starting ETL Plus Backend with enhanced logging...");

    // Initialize structured logging and aggregation
    LOG_INFO("Main", "Initializing structured logging and aggregation...");
    try {
      auto &structuredLogger = StructuredLogger::getInstance();

      // Load structured logging configuration
      nlohmann::json fullConfig = config.getJsonConfig();
      auto structuredConfig =
          LogAggregationConfigLoader::loadStructuredLoggingConfig(
              fullConfig["logging"]);
      auto aggregationConfig =
          LogAggregationConfigLoader::loadAggregationConfig(
              fullConfig["logging"]);

      // Configure structured logging
      structuredLogger.configureStructuredLogging(
          structuredConfig.enabled, structuredConfig.default_component);

      // Configure aggregation if enabled
      if (aggregationConfig.enabled &&
          !aggregationConfig.destinations.empty()) {
        LOG_INFO("Main",
                 "Enabling log aggregation with " +
                     std::to_string(aggregationConfig.destinations.size()) +
                     " destinations");

        // Create aggregator with destinations
        auto aggregator =
            std::make_unique<LogAggregator>(aggregationConfig.destinations);
        if (aggregator->initialize()) {
          structuredLogger.setAggregationEnabled(true);
          LOG_INFO("Main", "Log aggregation initialized successfully");
        } else {
          LOG_WARN("Main", "Failed to initialize log aggregation");
        }
      } else {
        LOG_INFO("Main", "Log aggregation is disabled");
      }
    } catch (const std::exception &e) {
      LOG_WARN("Main", "Failed to initialize structured logging: " +
                           std::string(e.what()));
    }

    // Set up signal handling
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    LOG_INFO("Main", "Configuration loaded successfully");

    // Initialize database manager
    LOG_INFO("Main", "Initializing database manager...");
    auto dbManager = std::make_shared<DatabaseManager>();
    ConnectionConfig dbConfig;

    // Check environment variables first, then fall back to config file
    const char *dbHostEnv = std::getenv("DATABASE_HOST");
    dbConfig.host = dbHostEnv ? std::string(dbHostEnv)
                              : config.getString("database.host", "localhost");

    const char *dbPortEnv = std::getenv("DATABASE_PORT");
    if (dbPortEnv) {
      try {
        dbConfig.port = std::stoi(dbPortEnv);
      } catch (const std::invalid_argument &ia) {
        LOG_WARN("Main",
                 "Invalid DATABASE_PORT value. Falling back to config file.");
        dbConfig.port = config.getInt("database.port", 5432);
      } catch (const std::out_of_range &oor) {
        LOG_WARN(
            "Main",
            "DATABASE_PORT value out of range. Falling back to config file.");
        dbConfig.port = config.getInt("database.port", 5432);
      }
    } else {
      dbConfig.port = config.getInt("database.port", 5432);
    }

    const char *dbNameEnv = std::getenv("DATABASE_NAME");
    dbConfig.database = dbNameEnv
                            ? std::string(dbNameEnv)
                            : config.getString("database.name", "etlplus");

    const char *dbUserEnv = std::getenv("DATABASE_USER");
    dbConfig.username = dbUserEnv
                            ? std::string(dbUserEnv)
                            : config.getString("database.username", "postgres");

    const char *dbPassEnv = std::getenv("DATABASE_PASSWORD");
    dbConfig.password = dbPassEnv ? std::string(dbPassEnv)
                                  : config.getString("database.password", "");

    LOG_INFO("Main", "Connecting to database at " + dbConfig.host + ":" +
                         std::to_string(dbConfig.port));
    if (!dbManager->connect(dbConfig)) {
      LOG_WARN("Main",
               "Failed to connect to database. Running in offline mode.");
    } else {
      LOG_INFO("Main", "Database connected successfully");

      // Initialize database schema
      LOG_INFO("Main", "Initializing database schema...");
      if (!dbManager->initializeSchema()) {
        LOG_WARN("Main", "Failed to initialize database schema. Some features "
                         "may not work correctly.");
      } else {
        LOG_INFO("Main", "Database schema initialized successfully");
      }
    }

    // Initialize other managers
    LOG_INFO("Main", "Initializing authentication manager...");
    auto authManager = std::make_shared<AuthManager>(dbManager);

    LOG_INFO("Main", "Initializing data transformer...");
    auto dataTransformer = std::make_shared<DataTransformer>();

    LOG_INFO("Main", "Initializing ETL job manager...");
    auto etlManager =
        std::make_shared<ETLJobManager>(dbManager, dataTransformer);

    // Start ETL job manager
    LOG_INFO("Main", "Starting ETL job manager...");
    etlManager->start();
    LOG_INFO("Main", "ETL Job Manager started successfully");

    // Initialize WebSocket manager
    LOG_INFO("Main", "Initializing WebSocket manager...");
    auto wsManager = std::make_shared<WebSocketManager>();
    wsManager->start();
    LOG_INFO("Main", "WebSocket manager started successfully");

    // Create request handler
    LOG_INFO("Main", "Creating request handler...");
    auto requestHandler = std::make_shared<RequestHandler>(
        dbManager, authManager, etlManager, wsManager);

    // Create and configure HTTP server
    std::string address = config.getString("server.address", "0.0.0.0");
    int port = config.getInt("server.port", 8080);
    int threads = config.getInt("server.threads", 4);

    LOG_INFO("Main", "Initializing HTTP server on " + address + ":" +
                         std::to_string(port) + " with " +
                         std::to_string(threads) + " threads");
    server = std::make_unique<HttpServer>(
        address, static_cast<unsigned short>(port), threads);
    server->setRequestHandler(requestHandler);
    server->setWebSocketManager(wsManager);

    // Start the server
    LOG_INFO("Main", "Starting HTTP server...");
    server->start();

    LOG_INFO("Main", "ETL Plus Backend is running. Press Ctrl+C to stop.");

    // Keep the main thread alive
    while (server->isRunning()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

  } catch (const std::exception &e) {
    LOG_FATAL("Main", "Unhandled exception: " + std::string(e.what()));
    return 1;
  }

  LOG_INFO("Main", "ETL Plus Backend shutdown complete");
  return 0;
}
