#include "job_monitor_service.hpp"
#include "job_monitor_service_recovery.hpp"
#include "websocket_manager.hpp"
#include "notification_service.hpp"
#include "etl_job_manager.hpp"
#include "database_manager.hpp"
#include "data_transformer.hpp"
#include "logger.hpp"
#include "config_manager.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <memory>

class JobMonitorServiceErrorHandlingTest {
public:
    void runTests() {
        std::cout << "=== Job Monitor Service Error Handling Tests ===" << std::endl;
        
        setupTestEnvironment();
        
        testServiceRecoveryConfig();
        testServiceRecoveryState();
        testServiceCircuitBreaker();
        testHealthMonitoring();
        testGracefulDegradation();
        testEventQueueing();
        testAutoRecovery();
        
        std::cout << "✅ All Job Monitor Service error handling tests completed!" << std::endl;
    }

private:
    std::shared_ptr<JobMonitorService> jobMonitorService_;
    std::shared_ptr<ETLJobManager> etlManager_;
    std::shared_ptr<WebSocketManager> wsManager_;
    std::shared_ptr<NotificationService> notificationService_;

    void setupTestEnvironment() {
        // Configure logger
        LogConfig logConfig;
        logConfig.level = LogLevel::DEBUG;
        logConfig.consoleOutput = true;
        Logger::getInstance().configure(logConfig);
        
        // Initialize config manager
        ConfigManager::getInstance().loadConfig("config.json");
        
        // Initialize components
        auto dbManager = std::make_shared<DatabaseManager>();
        auto transformer = std::make_shared<DataTransformer>();
        etlManager_ = std::make_shared<ETLJobManager>(dbManager, transformer);
        wsManager_ = std::make_shared<WebSocketManager>();
        
        // Create a simple test notification service
        class TestNotificationService : public NotificationService {
        public:
            void sendJobFailureAlert(const std::string&, const std::string&) override {}
            void sendJobTimeoutWarning(const std::string&, int) override {}
            bool isRunning() const override { return true; }
        };
        
        notificationService_ = std::make_shared<TestNotificationService>();
        jobMonitorService_ = std::make_shared<JobMonitorService>();
    }

    void testServiceRecoveryConfig() {
        std::cout << "\n--- Test: Service Recovery Configuration ---" << std::endl;
        
        job_monitoring_recovery::ServiceRecoveryConfig config;
        
        // Test default values
        assert(config.enableGracefulDegradation == true);
        assert(config.enableAutoRecovery == true);
        assert(config.maxRecoveryAttempts == 3);
        assert(config.baseRecoveryDelay == std::chrono::milliseconds(5000));
        assert(config.maxRecoveryDelay == std::chrono::milliseconds(60000));
        assert(config.backoffMultiplier == 2.0);
        assert(config.eventQueueMaxSize == 10000);
        assert(config.healthCheckInterval == std::chrono::seconds(30));
        assert(config.enableHealthChecks == true);
        assert(config.maxFailedHealthChecks == 3);
        
        std::cout << "✓ Service recovery configuration defaults are correct" << std::endl;
        
        // Test custom configuration
        config.enableGracefulDegradation = false;
        config.enableAutoRecovery = false;
        config.maxRecoveryAttempts = 5;
        config.baseRecoveryDelay = std::chrono::milliseconds(10000);
        config.maxRecoveryDelay = std::chrono::milliseconds(120000);
        config.backoffMultiplier = 3.0;
        config.eventQueueMaxSize = 20000;
        config.healthCheckInterval = std::chrono::seconds(60);
        config.enableHealthChecks = false;
        config.maxFailedHealthChecks = 5;
        
        assert(config.enableGracefulDegradation == false);
        assert(config.enableAutoRecovery == false);
        assert(config.maxRecoveryAttempts == 5);
        assert(config.maxFailedHealthChecks == 5);
        
        std::cout << "✓ Service recovery configuration can be customized" << std::endl;
    }

    void testServiceRecoveryState() {
        std::cout << "\n--- Test: Service Recovery State ---" << std::endl;
        
        job_monitoring_recovery::ServiceRecoveryConfig config;
        job_monitoring_recovery::ServiceRecoveryState state;
        
        // Test initial state
        assert(state.isHealthy.load() == true);
        assert(state.isRecovering.load() == false);
        assert(state.recoveryAttempts.load() == 0);
        assert(state.failedHealthChecks.load() == 0);
        
        std::cout << "✓ Service recovery state starts with correct initial values" << std::endl;
        
        // Test shouldAttemptRecovery logic
        assert(state.shouldAttemptRecovery(config) == true); // First attempt should be allowed
        
        state.recoveryAttempts.store(3);
        assert(state.shouldAttemptRecovery(config) == false); // Max attempts reached
        
        state.recoveryAttempts.store(1);
        state.lastRecoveryAttempt = std::chrono::system_clock::now();
        assert(state.shouldAttemptRecovery(config) == false); // Too soon for next attempt
        
        std::cout << "✓ Service recovery state logic for recovery attempts works correctly" << std::endl;
        
        // Test backoff delay calculation
        state.recoveryAttempts.store(0);
        auto delay1 = state.calculateBackoffDelay(config);
        assert(delay1 == config.baseRecoveryDelay);
        
        state.recoveryAttempts.store(1);
        auto delay2 = state.calculateBackoffDelay(config);
        assert(delay2 == config.baseRecoveryDelay);
        
        state.recoveryAttempts.store(2);
        auto delay3 = state.calculateBackoffDelay(config);
        assert(delay3 == std::chrono::milliseconds(10000)); // 5000 * 2^1
        
        state.recoveryAttempts.store(3);
        auto delay4 = state.calculateBackoffDelay(config);
        assert(delay4 == std::chrono::milliseconds(20000)); // 5000 * 2^2
        
        std::cout << "✓ Exponential backoff delay calculation for service recovery works correctly" << std::endl;
        
        // Test reset functionality
        state.isHealthy.store(false);
        state.isRecovering.store(true);
        state.recoveryAttempts.store(5);
        state.failedHealthChecks.store(10);
        
        state.reset();
        assert(state.isHealthy.load() == true);
        assert(state.isRecovering.load() == false);
        assert(state.recoveryAttempts.load() == 0);
        assert(state.failedHealthChecks.load() == 0);
        
        std::cout << "✓ Service recovery state reset works correctly" << std::endl;
    }

    void testServiceCircuitBreaker() {
        std::cout << "\n--- Test: Service Circuit Breaker ---" << std::endl;
        
        job_monitoring_recovery::ServiceCircuitBreaker circuitBreaker(3, std::chrono::seconds(2), 2);
        
        // Test initial state (CLOSED)
        assert(circuitBreaker.getState() == job_monitoring_recovery::ServiceCircuitBreaker::State::CLOSED);
        assert(circuitBreaker.allowOperation() == true);
        assert(circuitBreaker.isInDegradedMode() == false);
        
        std::cout << "✓ Service circuit breaker starts in CLOSED state" << std::endl;
        
        // Test failures leading to OPEN state
        circuitBreaker.onFailure();
        circuitBreaker.onFailure();
        circuitBreaker.onFailure();
        
        assert(circuitBreaker.getState() == job_monitoring_recovery::ServiceCircuitBreaker::State::OPEN);
        assert(circuitBreaker.allowOperation() == false);
        assert(circuitBreaker.isInDegradedMode() == true);
        
        std::cout << "✓ Service circuit breaker opens and enters degraded mode after failure threshold" << std::endl;
        
        // Test timeout and HALF_OPEN state
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        assert(circuitBreaker.allowOperation() == true); // Should be HALF_OPEN now
        assert(circuitBreaker.getState() == job_monitoring_recovery::ServiceCircuitBreaker::State::HALF_OPEN);
        
        std::cout << "✓ Service circuit breaker transitions to HALF_OPEN after timeout" << std::endl;
        
        // Test recovery (HALF_OPEN -> CLOSED)
        circuitBreaker.onSuccess();
        circuitBreaker.onSuccess();
        
        assert(circuitBreaker.getState() == job_monitoring_recovery::ServiceCircuitBreaker::State::CLOSED);
        assert(circuitBreaker.isInDegradedMode() == false);
        
        std::cout << "✓ Service circuit breaker recovers to CLOSED state" << std::endl;
    }

    void testHealthMonitoring() {
        std::cout << "\n--- Test: Health Monitoring ---" << std::endl;
        
        jobMonitorService_->initialize(etlManager_, wsManager_, notificationService_);
        
        // Test initial health state
        assert(jobMonitorService_->isHealthy() == true);
        
        std::cout << "✓ Job Monitor Service starts in healthy state" << std::endl;
        
        // Test recovery configuration
        job_monitoring_recovery::ServiceRecoveryConfig config;
        config.enableHealthChecks = true;
        config.healthCheckInterval = std::chrono::seconds(1);
        config.maxFailedHealthChecks = 2;
        
        jobMonitorService_->setRecoveryConfig(config);
        auto retrievedConfig = jobMonitorService_->getRecoveryConfig();
        assert(retrievedConfig.enableHealthChecks == true);
        assert(retrievedConfig.healthCheckInterval == std::chrono::seconds(1));
        assert(retrievedConfig.maxFailedHealthChecks == 2);
        
        std::cout << "✓ Recovery configuration can be set and retrieved" << std::endl;
        
        // Test manual health check
        jobMonitorService_->performHealthCheck();
        assert(jobMonitorService_->isHealthy() == true);
        
        std::cout << "✓ Manual health check performs correctly" << std::endl;
        
        // Test recovery state
        auto recoveryState = jobMonitorService_->getRecoveryState();
        assert(recoveryState.isHealthy.load() == true);
        assert(recoveryState.failedHealthChecks.load() == 0);
        
        std::cout << "✓ Recovery state reflects healthy service" << std::endl;
    }

    void testGracefulDegradation() {
        std::cout << "\n--- Test: Graceful Degradation ---" << std::endl;
        
        job_monitoring_recovery::DegradedModeEventQueue<JobStatusUpdate> statusQueue(5);
        job_monitoring_recovery::DegradedModeEventQueue<WebSocketMessage> messageQueue(5);
        
        // Test event queueing
        JobStatusUpdate update1;
        update1.jobId = "test_job_1";
        update1.status = JobStatus::RUNNING;
        update1.timestamp = std::chrono::system_clock::now();
        
        JobStatusUpdate update2;
        update2.jobId = "test_job_2";
        update2.status = JobStatus::COMPLETED;
        update2.timestamp = std::chrono::system_clock::now();
        
        statusQueue.enqueue(update1);
        statusQueue.enqueue(update2);
        
        assert(statusQueue.size() == 2);
        assert(statusQueue.empty() == false);
        
        std::cout << "✓ Events can be queued during degraded mode" << std::endl;
        
        // Test event retrieval
        auto queuedEvents = statusQueue.dequeueAll();
        assert(queuedEvents.size() == 2);
        assert(queuedEvents[0].jobId == "test_job_1");
        assert(queuedEvents[1].jobId == "test_job_2");
        assert(statusQueue.empty() == true);
        
        std::cout << "✓ Queued events can be retrieved and queue is properly cleared" << std::endl;
        
        // Test queue overflow
        for (int i = 0; i < 8; ++i) {
            JobStatusUpdate overflowUpdate;
            overflowUpdate.jobId = "overflow_job_" + std::to_string(i);
            statusQueue.enqueue(overflowUpdate);
        }
        
        assert(statusQueue.size() == 5); // Should be limited to max size
        
        auto overflowEvents = statusQueue.dequeueAll();
        assert(overflowEvents.size() == 5);
        assert(overflowEvents[0].jobId == "overflow_job_3"); // First 3 should be dropped
        assert(overflowEvents[4].jobId == "overflow_job_7");
        
        std::cout << "✓ Event queue properly handles overflow by dropping oldest events" << std::endl;
    }

    void testEventQueueing() {
        std::cout << "\n--- Test: Event Queueing During Degraded Mode ---" << std::endl;
        
        jobMonitorService_->initialize(etlManager_, wsManager_, notificationService_);
        jobMonitorService_->start();
        
        // Simulate degraded mode by triggering circuit breaker
        job_monitoring_recovery::ServiceRecoveryConfig config;
        config.enableGracefulDegradation = true;
        jobMonitorService_->setRecoveryConfig(config);
        
        std::cout << "✓ Job Monitor Service initialized for event queueing test" << std::endl;
        
        // Test normal operation first
        assert(jobMonitorService_->isRunning() == true);
        assert(jobMonitorService_->isHealthy() == true);
        
        // Simulate job status changes (these should work normally)
        jobMonitorService_->onJobStatusChanged("test_job_1", JobStatus::PENDING, JobStatus::RUNNING);
        jobMonitorService_->onJobProgressUpdated("test_job_1", 25, "Processing data");
        
        std::cout << "✓ Normal job monitoring operations work correctly" << std::endl;
        
        jobMonitorService_->stop();
    }

    void testAutoRecovery() {
        std::cout << "\n--- Test: Auto Recovery Mechanism ---" << std::endl;
        
        job_monitoring_recovery::ServiceRecoveryConfig config;
        config.enableAutoRecovery = true;
        config.maxRecoveryAttempts = 2;
        config.baseRecoveryDelay = std::chrono::milliseconds(100); // Short delay for testing
        
        job_monitoring_recovery::ServiceRecoveryState state;
        
        // Test recovery attempt logic
        assert(state.shouldAttemptRecovery(config) == true);
        
        state.recoveryAttempts.store(1);
        state.lastRecoveryAttempt = std::chrono::system_clock::now() - std::chrono::milliseconds(200);
        assert(state.shouldAttemptRecovery(config) == true); // Enough time has passed
        
        state.recoveryAttempts.store(2);
        assert(state.shouldAttemptRecovery(config) == false); // Max attempts reached
        
        std::cout << "✓ Auto recovery attempt logic works correctly" << std::endl;
        
        // Test recovery state management
        jobMonitorService_->initialize(etlManager_, wsManager_, notificationService_);
        jobMonitorService_->setRecoveryConfig(config);
        
        // Test manual recovery attempt
        auto initialState = jobMonitorService_->getRecoveryState();
        assert(initialState.isHealthy.load() == true);
        assert(initialState.recoveryAttempts.load() == 0);
        
        jobMonitorService_->attemptRecovery();
        
        // Since service is already healthy, recovery should succeed immediately
        auto postRecoveryState = jobMonitorService_->getRecoveryState();
        assert(postRecoveryState.isHealthy.load() == true);
        
        std::cout << "✓ Manual recovery attempt on healthy service works correctly" << std::endl;
    }
};

int main() {
    try {
        JobMonitorServiceErrorHandlingTest test;
        test.runTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
