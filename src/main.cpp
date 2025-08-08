#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>
#include <chrono>

#include "config_manager.hpp"
#include "database_manager.hpp"
#include "http_server.hpp"
#include "request_handler.hpp"
#include "auth_manager.hpp"
#include "etl_job_manager.hpp"
#include "data_transformer.hpp"

std::unique_ptr<HttpServer> server;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    if (server) {
        server->stop();
    }
    exit(0);
}

int main() {
    try {
        // Set up signal handling
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        std::cout << "Starting ETL Plus Backend..." << std::endl;
        
        // Load configuration
        auto& config = ConfigManager::getInstance();
        if (!config.loadConfig("config.json")) {
            std::cerr << "Failed to load configuration" << std::endl;
            return 1;
        }
        
        // Initialize database manager
        auto dbManager = std::make_shared<DatabaseManager>();
        ConnectionConfig dbConfig;
        dbConfig.host = config.getString("database.host", "localhost");
        dbConfig.port = config.getInt("database.port", 5432);
        dbConfig.database = config.getString("database.name", "etlplus");
        dbConfig.username = config.getString("database.username", "postgres");
        dbConfig.password = config.getString("database.password", "");
        
        std::cout << "Connecting to database..." << std::endl;
        if (!dbManager->connect(dbConfig)) {
            std::cout << "Warning: Failed to connect to database. Running in offline mode." << std::endl;
        } else {
            std::cout << "Database connected successfully." << std::endl;
        }
        
        // Initialize other managers
        auto authManager = std::make_shared<AuthManager>();
        auto dataTransformer = std::make_shared<DataTransformer>();
        auto etlManager = std::make_shared<ETLJobManager>(dbManager, dataTransformer);
        
        // Start ETL job manager
        etlManager->start();
        std::cout << "ETL Job Manager started." << std::endl;
        
        // Create request handler
        auto requestHandler = std::make_shared<RequestHandler>(dbManager, authManager, etlManager);
        
        // Create and configure HTTP server
        std::string address = config.getString("server.address", "0.0.0.0");
        int port = config.getInt("server.port", 8080);
        int threads = config.getInt("server.threads", 4);
        
        server = std::make_unique<HttpServer>(address, static_cast<unsigned short>(port), threads);
        server->setRequestHandler(requestHandler);
        
        std::cout << "Starting HTTP server on " << address << ":" << port << std::endl;
        std::cout << "Using " << threads << " worker threads." << std::endl;
        
        // Start the server
        server->start();
        
        std::cout << "ETL Plus Backend is running. Press Ctrl+C to stop." << std::endl;
        
        // Keep the main thread alive
        while (server->isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
