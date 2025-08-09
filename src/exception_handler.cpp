#include "exception_handler.hpp"
#include <thread>
#include <algorithm>

namespace ETLPlus {
    namespace ExceptionHandling {

        // TransactionScope implementation
        TransactionScope::TransactionScope(std::shared_ptr<DatabaseManager> dbManager, 
                                         const std::string& operationName)
            : dbManager_(dbManager)
            , committed_(false)
            , rollbackOnDestroy_(true)
            , operationName_(operationName)
            , context_(operationName) {
            
            if (!dbManager_) {
                throw Exceptions::SystemException(
                    Exceptions::ErrorCode::COMPONENT_UNAVAILABLE,
                    "Database manager is null in transaction scope",
                    context_);
            }

            if (!dbManager_->isConnected()) {
                throw Exceptions::DatabaseException(
                    Exceptions::ErrorCode::CONNECTION_FAILED,
                    "Database not connected when starting transaction",
                    context_);
            }

            try {
                if (!dbManager_->beginTransaction()) {
                    throw Exceptions::DatabaseException(
                        Exceptions::ErrorCode::TRANSACTION_FAILED,
                        "Failed to begin database transaction",
                        context_);
                }
                DB_LOG_DEBUG("Transaction started for operation: " + operationName_);
            } catch (const std::exception& ex) {
                throw Exceptions::DatabaseException(
                    Exceptions::ErrorCode::TRANSACTION_FAILED,
                    "Exception during transaction begin: " + std::string(ex.what()),
                    context_);
            }
        }

        TransactionScope::~TransactionScope() {
            if (!committed_ && rollbackOnDestroy_ && dbManager_ && dbManager_->isConnected()) {
                try {
                    dbManager_->rollbackTransaction();
                    DB_LOG_WARN("Transaction rolled back for operation: " + operationName_);
                } catch (const std::exception& ex) {
                    DB_LOG_ERROR("Failed to rollback transaction in destructor: " + std::string(ex.what()));
                }
            }
        }

        TransactionScope::TransactionScope(TransactionScope&& other) noexcept
            : dbManager_(std::move(other.dbManager_))
            , committed_(other.committed_)
            , rollbackOnDestroy_(other.rollbackOnDestroy_)
            , operationName_(std::move(other.operationName_))
            , context_(std::move(other.context_)) {
            other.committed_ = true; // Prevent other from rolling back
        }

        TransactionScope& TransactionScope::operator=(TransactionScope&& other) noexcept {
            if (this != &other) {
                // Clean up current transaction
                if (!committed_ && rollbackOnDestroy_ && dbManager_ && dbManager_->isConnected()) {
                    try {
                        dbManager_->rollbackTransaction();
                    } catch (...) {
                        // Ignore exceptions in assignment operator
                    }
                }

                // Move from other
                dbManager_ = std::move(other.dbManager_);
                committed_ = other.committed_;
                rollbackOnDestroy_ = other.rollbackOnDestroy_;
                operationName_ = std::move(other.operationName_);
                context_ = std::move(other.context_);
                
                other.committed_ = true; // Prevent other from rolling back
            }
            return *this;
        }

        void TransactionScope::commit() {
            if (committed_) {
                throw Exceptions::DatabaseException(
                    Exceptions::ErrorCode::TRANSACTION_FAILED,
                    "Transaction already committed",
                    context_);
            }

            if (!dbManager_ || !dbManager_->isConnected()) {
                throw Exceptions::DatabaseException(
                    Exceptions::ErrorCode::CONNECTION_FAILED,
                    "Database not connected when committing transaction",
                    context_);
            }

            try {
                if (!dbManager_->commitTransaction()) {
                    throw Exceptions::DatabaseException(
                        Exceptions::ErrorCode::TRANSACTION_FAILED,
                        "Failed to commit database transaction",
                        context_);
                }
                committed_ = true;
                DB_LOG_DEBUG("Transaction committed for operation: " + operationName_);
            } catch (const std::exception& ex) {
                throw Exceptions::DatabaseException(
                    Exceptions::ErrorCode::TRANSACTION_FAILED,
                    "Exception during transaction commit: " + std::string(ex.what()),
                    context_);
            }
        }

        void TransactionScope::rollback() {
            if (committed_) {
                throw Exceptions::DatabaseException(
                    Exceptions::ErrorCode::TRANSACTION_FAILED,
                    "Cannot rollback committed transaction",
                    context_);
            }

            if (!dbManager_ || !dbManager_->isConnected()) {
                DB_LOG_WARN("Database not connected when rolling back transaction");
                return;
            }

            try {
                if (!dbManager_->rollbackTransaction()) {
                    throw Exceptions::DatabaseException(
                        Exceptions::ErrorCode::TRANSACTION_FAILED,
                        "Failed to rollback database transaction",
                        context_);
                }
                committed_ = true; // Prevent destructor from trying to rollback again
                DB_LOG_WARN("Transaction rolled back for operation: " + operationName_);
            } catch (const std::exception& ex) {
                throw Exceptions::DatabaseException(
                    Exceptions::ErrorCode::TRANSACTION_FAILED,
                    "Exception during transaction rollback: " + std::string(ex.what()),
                    context_);
            }
        }

        // ExceptionHandler implementation
        std::shared_ptr<Exceptions::BaseException> ExceptionHandler::convertException(
            const std::exception& ex,
            const std::string& operationName,
            const Exceptions::ErrorContext& context) {
            
            // Try to determine the most appropriate exception type based on the message
            std::string message = ex.what();
            std::string lowerMessage = message;
            std::transform(lowerMessage.begin(), lowerMessage.end(), lowerMessage.begin(), ::tolower);

            // Database-related errors
            if (lowerMessage.find("database") != std::string::npos ||
                lowerMessage.find("connection") != std::string::npos ||
                lowerMessage.find("query") != std::string::npos ||
                lowerMessage.find("sql") != std::string::npos) {
                return std::make_shared<Exceptions::DatabaseException>(
                    Exceptions::ErrorCode::QUERY_FAILED,
                    "Database error: " + message,
                    context);
            }
            
            // Network-related errors
            if (lowerMessage.find("network") != std::string::npos ||
                lowerMessage.find("socket") != std::string::npos ||
                lowerMessage.find("timeout") != std::string::npos ||
                lowerMessage.find("refused") != std::string::npos) {
                return std::make_shared<Exceptions::NetworkException>(
                    Exceptions::ErrorCode::CONNECTION_REFUSED,
                    "Network error: " + message,
                    context);
            }
            
            // Memory-related errors
            if (lowerMessage.find("memory") != std::string::npos ||
                lowerMessage.find("allocation") != std::string::npos ||
                lowerMessage.find("bad_alloc") != std::string::npos) {
                return std::make_shared<Exceptions::ResourceException>(
                    Exceptions::ErrorCode::OUT_OF_MEMORY,
                    "Memory error: " + message,
                    context);
            }
            
            // File-related errors
            if (lowerMessage.find("file") != std::string::npos ||
                lowerMessage.find("permission") != std::string::npos ||
                lowerMessage.find("access") != std::string::npos) {
                return std::make_shared<Exceptions::ResourceException>(
                    Exceptions::ErrorCode::FILE_NOT_FOUND,
                    "File/Resource error: " + message,
                    context);
            }

            // Default to system exception
            return std::make_shared<Exceptions::SystemException>(
                Exceptions::ErrorCode::INTERNAL_ERROR,
                "Converted standard exception: " + message,
                context);
        }

        void ExceptionHandler::logException(
            const Exceptions::BaseException& ex,
            const std::string& operationName) {
            
            std::string logMessage = ex.toLogString();
            if (!operationName.empty()) {
                logMessage = "[" + operationName + "] " + logMessage;
            }

            switch (ex.getSeverity()) {
                case Exceptions::ErrorSeverity::LOW:
                    LOG_WARN("ExceptionHandler", logMessage);
                    break;
                case Exceptions::ErrorSeverity::MEDIUM:
                    LOG_ERROR("ExceptionHandler", logMessage);
                    break;
                case Exceptions::ErrorSeverity::HIGH:
                    LOG_ERROR("ExceptionHandler", logMessage);
                    break;
                case Exceptions::ErrorSeverity::CRITICAL:
                    LOG_FATAL("ExceptionHandler", logMessage);
                    break;
            }
        }

        void ExceptionHandler::handleBaseException(
            const Exceptions::BaseException& ex,
            ExceptionPolicy policy,
            const std::string& operationName,
            const Exceptions::ErrorContext& context) {
            
            switch (policy) {
                case ExceptionPolicy::PROPAGATE:
                    logException(ex, operationName);
                    throw;
                
                case ExceptionPolicy::LOG_AND_IGNORE:
                    logException(ex, operationName);
                    LOG_WARN("ExceptionHandler", "Exception ignored for operation: " + operationName);
                    break;
                
                case ExceptionPolicy::LOG_AND_RETURN:
                    logException(ex, operationName);
                    LOG_INFO("ExceptionHandler", "Exception handled, returning error status for operation: " + operationName);
                    break;
                
                case ExceptionPolicy::RETRY:
                    logException(ex, operationName);
                    LOG_INFO("ExceptionHandler", "Exception will trigger retry for operation: " + operationName);
                    throw;
            }
        }

        void ExceptionHandler::handleStandardException(
            const std::exception& ex,
            ExceptionPolicy policy,
            const std::string& operationName,
            const Exceptions::ErrorContext& context) {
            
            auto baseEx = convertException(ex, operationName, context);
            handleBaseException(*baseEx, policy, operationName, context);
        }

        void ExceptionHandler::handleUnknownException(
            ExceptionPolicy policy,
            const std::string& operationName,
            const Exceptions::ErrorContext& context) {
            
            auto unknownEx = Exceptions::SystemException(
                Exceptions::ErrorCode::UNKNOWN_ERROR,
                "Unknown exception caught",
                context);
            
            handleBaseException(unknownEx, policy, operationName, context);
        }

        std::chrono::milliseconds ExceptionHandler::calculateDelay(
            int attempt,
            const RetryConfig& config) {
            
            auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                config.initialDelay * std::pow(config.backoffMultiplier, attempt - 1));
            
            return std::min(delay, config.maxDelay);
        }

    } // namespace ExceptionHandling
} // namespace ETLPlus
