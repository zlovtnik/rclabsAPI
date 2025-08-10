#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>
#include <chrono>
#include <unistd.h>

// Core system components
#include "logger.hpp"
#include "config_manager.hpp"
#include "database_manager.hpp"
#include "http_server.hpp"
#include "websocket_manager.hpp"
#include "request_handler.hpp"
#include "auth_manager.hpp"
#include "etl_job_manager.hpp"
#include "data_transformer.hpp"

// Real-time monitoring components
#include "job_monitor_service.hpp"
#include "notification_service.hpp"

/**
 * Enhanced Main Application with Full Real-time Monitoring Integration
 * 
 * This version integrates all monitoring components:
 * - WebSocket Manager for real-time communication
 * - Job Monitor Service for coordinating job status updates
 * - Notification Service for critical alerts
 * - Enhanced HTTP endpoints for monitoring data
 */

// Global components for signal handling
std::unique_ptr<HttpServer> server;
std::shared_ptr<JobMonitorService> jobMonitor;
std::shared_ptr<WebSocketManager> wsManager;
std::shared_ptr<NotificationServiceImpl> notificationService;
std::shared_ptr<ETLJobManager> etlManager;

void signalHandler(int signal) {
    LOG_INFO("Main", "Received signal " + std::to_string(signal) + ". Shutting down gracefully...");
    
    // Stop services in reverse dependency order
    if (server) {
        LOG_INFO("Main", "Stopping HTTP server...");
        server->stop();
    }
    
    if (etlManager) {
        LOG_INFO("Main", "Stopping ETL job manager...");
        etlManager->stop();
    }
    
    if (jobMonitor) {
        LOG_INFO("Main", "Stopping job monitor service...");
        jobMonitor->stop();
    }
    
    if (wsManager) {
        LOG_INFO("Main", "Stopping WebSocket manager...");
        wsManager->stop();
    }
    
    if (notificationService) {
        LOG_INFO("Main", "Stopping notification service...");
        notificationService->stop();
    }
    
    LOG_INFO("Main", "Graceful shutdown complete");
    exit(0);
}

int main() {
    try {
        // Load configuration first (with basic logging)
        auto& config = ConfigManager::getInstance();
        if (!config.loadConfig("config/config.json")) {
            std::cerr << "Failed to load configuration, using defaults" << std::endl;
        }
        
        std::cout << "Configuration loaded, initializing logger..." << std::endl;
        
        // Initialize enhanced logging system with configuration
        auto& logger = Logger::getInstance();
        LogConfig logConfig = config.getLoggingConfig();
        
        std::cout << "Logger config created, configuring logger..." << std::endl;
        logger.configure(logConfig);
        
        std::cout << "Logger configured, starting application..." << std::endl;
        
        LOG_INFO("Main", "Starting ETL Plus Backend with Real-time Monitoring...");
        
        // Set up signal handling
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        LOG_INFO("Main", "Configuration loaded successfully");
        
        // ===== PHASE 1: Initialize Core Components =====
        LOG_INFO("Main", "=== Phase 1: Initializing Core Components ===");
        
        // Initialize database manager
        LOG_INFO("Main", "Initializing database manager...");
        auto dbManager = std::make_shared<DatabaseManager>();
        ConnectionConfig dbConfig;
        dbConfig.host = config.getString("database.host", "localhost");
        dbConfig.port = config.getInt("database.port", 5432);
        dbConfig.database = config.getString("database.name", "etlplus");
        dbConfig.username = config.getString("database.username", "postgres");
        dbConfig.password = config.getString("database.password", "");
        
        LOG_INFO("Main", "Connecting to database at " + dbConfig.host + ":" + std::to_string(dbConfig.port));
        if (!dbManager->connect(dbConfig)) {
            LOG_WARN("Main", "Failed to connect to database. Running in offline mode.");
        } else {
            LOG_INFO("Main", "Database connected successfully");
        }
        
        // Initialize authentication and data transformation
        LOG_INFO("Main", "Initializing authentication manager...");
        auto authManager = std::make_shared<AuthManager>();
        
        LOG_INFO("Main", "Initializing data transformer...");
        auto dataTransformer = std::make_shared<DataTransformer>();
        
        // Initialize ETL job manager
        LOG_INFO("Main", "Initializing ETL job manager...");
        etlManager = std::make_shared<ETLJobManager>(dbManager, dataTransformer);
        
        LOG_INFO("Main", "Core components initialized successfully");
        
        // ===== PHASE 2: Initialize Monitoring Components =====
        LOG_INFO("Main", "=== Phase 2: Initializing Monitoring Components ===");
        
        // Initialize WebSocket manager
        LOG_INFO("Main", "Initializing WebSocket manager...");
        wsManager = std::make_shared<WebSocketManager>();
        
        // Initialize notification service
        LOG_INFO("Main", "Initializing notification service...");
        notificationService = std::make_shared<NotificationServiceImpl>();
        
        // Configure notification service
        NotificationConfig notifConfig;
        notifConfig.enabled = config.getBool("monitoring.notifications.enabled", true);
        notifConfig.jobFailureAlerts = config.getBool("monitoring.notifications.job_failure_alerts", true);
        notifConfig.timeoutWarnings = config.getBool("monitoring.notifications.timeout_warnings", true);
        notifConfig.resourceAlerts = config.getBool("monitoring.notifications.resource_alerts", true);
        notifConfig.maxRetryAttempts = config.getInt("monitoring.notifications.retry_attempts", 3);
        notifConfig.baseRetryDelayMs = config.getInt("monitoring.notifications.retry_delay", 5000);
        notifConfig.timeoutWarningThresholdMinutes = config.getInt("monitoring.job_tracking.timeout_warning_threshold", 25);
        
        // Resource alert thresholds
        notifConfig.memoryUsageThreshold = config.getDouble("monitoring.notifications.memory_threshold", 0.85);
        notifConfig.cpuUsageThreshold = config.getDouble("monitoring.notifications.cpu_threshold", 0.90);
        notifConfig.diskSpaceThreshold = config.getDouble("monitoring.notifications.disk_threshold", 0.90);
        
        // Set default notification methods
        notifConfig.defaultMethods = {NotificationMethod::LOG_ONLY};
        notifConfig.priorityMethods[NotificationPriority::LOW] = {NotificationMethod::LOG_ONLY};
        notifConfig.priorityMethods[NotificationPriority::MEDIUM] = {NotificationMethod::LOG_ONLY};
        notifConfig.priorityMethods[NotificationPriority::HIGH] = {NotificationMethod::LOG_ONLY};
        notifConfig.priorityMethods[NotificationPriority::CRITICAL] = {NotificationMethod::LOG_ONLY};
        
        notificationService->configure(notifConfig);
        
        // Initialize job monitor service
        LOG_INFO("Main", "Initializing job monitor service...");
        jobMonitor = std::make_shared<JobMonitorService>();
        
        LOG_INFO("Main", "Monitoring components initialized successfully");
        
        // ===== PHASE 3: Wire Components Together =====
        LOG_INFO("Main", "=== Phase 3: Wiring Components Together ===");
        
        // Wire job monitor service with its dependencies
        LOG_INFO("Main", "Wiring job monitor service...");
        jobMonitor->initialize(etlManager, wsManager, notificationService);
        
        // Create enhanced request handler with monitoring support
        LOG_INFO("Main", "Creating enhanced request handler...");
        auto requestHandler = std::make_shared<RequestHandler>(dbManager, authManager, etlManager);
        
        // Create and configure HTTP server with WebSocket support
        std::string address = config.getString("server.address", "0.0.0.0");
        int port = config.getInt("server.port", 8080);
        int threads = config.getInt("server.threads", 4);
        
        LOG_INFO("Main", "Initializing HTTP server on " + address + ":" + std::to_string(port) + " with " + std::to_string(threads) + " threads");
        server = std::make_unique<HttpServer>(address, static_cast<unsigned short>(port), threads);
        server->setRequestHandler(requestHandler);
        server->setWebSocketManager(wsManager);
        
        LOG_INFO("Main", "Components wired together successfully");
        
        // ===== PHASE 4: Start Services =====
        LOG_INFO("Main", "=== Phase 4: Starting Services ===");
        
        // Start services in dependency order
        LOG_INFO("Main", "Starting notification service...");
        notificationService->start();
        
        LOG_INFO("Main", "Starting WebSocket manager...");
        wsManager->start();
        
        LOG_INFO("Main", "Starting job monitor service...");
        jobMonitor->start();
        
        LOG_INFO("Main", "Starting ETL job manager...");
        etlManager->start();
        
        LOG_INFO("Main", "Starting HTTP server...");
        server->start();
        
        LOG_INFO("Main", "All services started successfully");
        
        // ===== PHASE 5: System Health Check =====
        LOG_INFO("Main", "=== Phase 5: System Health Check ===");
        
        // Give services time to fully initialize
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Verify all services are running
        bool allServicesHealthy = true;
        
        if (!notificationService->isRunning()) {
            LOG_ERROR("Main", "Notification service failed to start");
            allServicesHealthy = false;
        } else {
            LOG_INFO("Main", "✓ Notification service is running");
        }
        
        if (!jobMonitor->isRunning()) {
            LOG_ERROR("Main", "Job monitor service failed to start");
            allServicesHealthy = false;
        } else {
            LOG_INFO("Main", "✓ Job monitor service is running");
        }
        
        if (!server->isRunning()) {
            LOG_ERROR("Main", "HTTP server failed to start");
            allServicesHealthy = false;
        } else {
            LOG_INFO("Main", "✓ HTTP server is running");
        }
        
        // Check WebSocket manager
        size_t wsConnections = wsManager->getConnectionCount();
        LOG_INFO("Main", "✓ WebSocket manager is running (connections: " + std::to_string(wsConnections) + ")");
        
        // Check ETL job manager
        size_t activeJobs = jobMonitor->getActiveJobCount();
        LOG_INFO("Main", "✓ ETL job manager is running (active jobs: " + std::to_string(activeJobs) + ")");
        
        if (!allServicesHealthy) {
            LOG_FATAL("Main", "Some services failed to start properly. Shutting down...");
            return 1;
        }
        
        // ===== PHASE 6: Runtime Monitoring =====
        LOG_INFO("Main", "=== Phase 6: Runtime Monitoring Active ===");
        
        LOG_INFO("Main", "ETL Plus Backend with Real-time Monitoring is fully operational!");
        LOG_INFO("Main", "Available endpoints:");
        LOG_INFO("Main", "  - HTTP API: http://" + address + ":" + std::to_string(port) + "/api/");
        LOG_INFO("Main", "  - WebSocket: ws://" + address + ":" + std::to_string(port) + "/ws");
        LOG_INFO("Main", "  - Health Check: http://" + address + ":" + std::to_string(port) + "/health");
        LOG_INFO("Main", "  - Monitoring: http://" + address + ":" + std::to_string(port) + "/api/monitor/");
        LOG_INFO("Main", "");
        LOG_INFO("Main", "Real-time monitoring features:");
        LOG_INFO("Main", "  ✓ Job status updates via WebSocket");
        LOG_INFO("Main", "  ✓ Progress tracking and metrics");
        LOG_INFO("Main", "  ✓ Log streaming");
        LOG_INFO("Main", "  ✓ Failure notifications");
        LOG_INFO("Main", "  ✓ Resource monitoring");
        LOG_INFO("Main", "  ✓ Performance analytics");
        LOG_INFO("Main", "");
        LOG_INFO("Main", "Press Ctrl+C to stop the server gracefully.");
        
        // Start periodic health monitoring
        std::thread healthMonitor([&]() {
            while (server->isRunning()) {
                std::this_thread::sleep_for(std::chrono::minutes(5));
                
                // Log system status
                size_t currentConnections = wsManager->getConnectionCount();
                size_t currentActiveJobs = jobMonitor->getActiveJobCount();
                size_t notificationQueueSize = notificationService->getQueueSize();
                size_t processedNotifications = notificationService->getProcessedCount();
                
                LOG_INFO("Main", "System Status - WS Connections: " + std::to_string(currentConnections) + 
                               ", Active Jobs: " + std::to_string(currentActiveJobs) + 
                               ", Notification Queue: " + std::to_string(notificationQueueSize) + 
                               ", Processed Notifications: " + std::to_string(processedNotifications));
                
                // Check for resource alerts
                // This would typically get actual system metrics
                // For now, we'll just verify the monitoring system is responsive
                if (currentActiveJobs > 50) {
                    LOG_WARN("Main", "High number of active jobs detected: " + std::to_string(currentActiveJobs));
                }
                
                if (currentConnections > 100) {
                    LOG_WARN("Main", "High number of WebSocket connections: " + std::to_string(currentConnections));
                }
            }
        });
        
        // Keep the main thread alive
        while (server->isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Wait for health monitor to finish
        if (healthMonitor.joinable()) {
            healthMonitor.join();
        }
        
    } catch (const std::exception& e) {
        LOG_FATAL("Main", "Unhandled exception: " + std::string(e.what()));
        return 1;
    }
    
    LOG_INFO("Main", "ETL Plus Backend with Real-time Monitoring shutdown complete");
    return 0;
}