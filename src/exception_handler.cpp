#include "exception_handler.hpp"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>

namespace ETLPlus {
namespace ExceptionHandling {

// TransactionScope implementation
TransactionScope::TransactionScope(std::shared_ptr<DatabaseManager> dbManager,
                                   const std::string &operationName)
    : dbManager_(dbManager), committed_(false), rollbackOnDestroy_(true),
      operationName_(operationName), context_() {
  context_["operation"] = operationName;

  if (!dbManager_) {
    throw etl::SystemException(etl::ErrorCode::COMPONENT_UNAVAILABLE,
                               "Database manager is null in transaction scope",
                               "TransactionScope", context_);
  }

  if (!dbManager_->isConnected()) {
    throw etl::SystemException(
        etl::ErrorCode::DATABASE_ERROR,
        "Database not connected when starting transaction", "TransactionScope",
        context_);
  }

  try {
    if (!dbManager_->beginTransaction()) {
      throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                                 "Failed to begin database transaction",
                                 "TransactionScope", context_);
    }
    DB_LOG_DEBUG("Transaction started for operation: " + operationName_);
  } catch (const std::exception &ex) {
    throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                               "Exception during transaction begin: " +
                                   std::string(ex.what()),
                               "TransactionScope", context_);
  }
}

TransactionScope::~TransactionScope() {
  if (!committed_ && rollbackOnDestroy_ && dbManager_ &&
      dbManager_->isConnected()) {
    try {
      dbManager_->rollbackTransaction();
      DB_LOG_WARN("Transaction rolled back for operation: " + operationName_);
    } catch (const std::exception &ex) {
      DB_LOG_ERROR("Failed to rollback transaction in destructor: " +
                   std::string(ex.what()));
    }
  }
}

TransactionScope::TransactionScope(TransactionScope &&other) noexcept
    : dbManager_(std::move(other.dbManager_)), committed_(other.committed_),
      rollbackOnDestroy_(other.rollbackOnDestroy_),
      operationName_(std::move(other.operationName_)),
      context_(std::move(other.context_)) {
  other.committed_ = true; // Prevent other from rolling back
}

TransactionScope &
TransactionScope::operator=(TransactionScope &&other) noexcept {
  if (this != &other) {
    // Clean up current transaction
    if (!committed_ && rollbackOnDestroy_ && dbManager_ &&
        dbManager_->isConnected()) {
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
    throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                               "Transaction already committed",
                               "TransactionScope", context_);
  }

  if (!dbManager_ || !dbManager_->isConnected()) {
    throw etl::SystemException(
        etl::ErrorCode::DATABASE_ERROR,
        "Database not connected when committing transaction",
        "TransactionScope", context_);
  }

  try {
    if (!dbManager_->commitTransaction()) {
      throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                                 "Failed to commit database transaction",
                                 "TransactionScope", context_);
    }
    committed_ = true;
    DB_LOG_DEBUG("Transaction committed for operation: " + operationName_);
  } catch (const std::exception &ex) {
    throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                               "Exception during transaction commit: " +
                                   std::string(ex.what()),
                               "TransactionScope", context_);
  }
}

void TransactionScope::rollback() {
  if (committed_) {
    throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                               "Cannot rollback committed transaction",
                               "TransactionScope", context_);
  }

  if (!dbManager_ || !dbManager_->isConnected()) {
    DB_LOG_WARN("Database not connected when rolling back transaction");
    return;
  }

  try {
    if (!dbManager_->rollbackTransaction()) {
      throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                                 "Failed to rollback database transaction",
                                 "TransactionScope", context_);
    }
    committed_ = true; // Prevent destructor from trying to rollback again
    DB_LOG_WARN("Transaction rolled back for operation: " + operationName_);
  } catch (const std::exception &ex) {
    throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                               "Exception during transaction rollback: " +
                                   std::string(ex.what()),
                               "TransactionScope", context_);
  }
}

// ExceptionHandler implementation
std::shared_ptr<etl::ETLException>
ExceptionHandler::convertException(const std::exception &ex,
                                   const std::string &operationName,
                                   const etl::ErrorContext &context) {

  // Check well-known exception types first for better accuracy
  if (dynamic_cast<const std::bad_alloc*>(&ex)) {
    return std::make_shared<etl::SystemException>(
        etl::ErrorCode::MEMORY_ERROR,
        "Memory allocation failed: " + std::string(ex.what()), "MemorySystem",
        context);
  }

  if (auto se = dynamic_cast<const std::system_error*>(&ex)) {
    // Inspect system error code for more specific mapping
    auto code = se->code();
    if (code.category() == std::system_category()) {
      if (code.value() == ETIMEDOUT || code.value() == ECONNABORTED) {
        return std::make_shared<etl::SystemException>(
            etl::ErrorCode::NETWORK_ERROR,
            "Network timeout/connection error: " + std::string(ex.what()),
            "NetworkSystem", context);
      }
      if (code.value() == ECONNREFUSED || code.value() == EHOSTUNREACH) {
        return std::make_shared<etl::SystemException>(
            etl::ErrorCode::NETWORK_ERROR,
            "Connection refused/unreachable: " + std::string(ex.what()),
            "NetworkSystem", context);
      }
      if (code.value() == ENOENT || code.value() == EACCES) {
        return std::make_shared<etl::SystemException>(
            etl::ErrorCode::FILE_ERROR,
            "File access error: " + std::string(ex.what()), "FileSystem",
            context);
      }
    }
    // Default system error mapping
    return std::make_shared<etl::SystemException>(
        etl::ErrorCode::INTERNAL_ERROR,
        "System error: " + std::string(ex.what()), "System", context);
  }

  // Fall back to message-based heuristics for other exception types
  std::string message = ex.what();
  std::string lowerMessage = message;
  std::transform(lowerMessage.begin(), lowerMessage.end(), lowerMessage.begin(),
                 ::tolower);

  // Database-related errors
  if (lowerMessage.find("database") != std::string::npos ||
      lowerMessage.find("connection") != std::string::npos ||
      lowerMessage.find("query") != std::string::npos ||
      lowerMessage.find("sql") != std::string::npos) {
    return std::make_shared<etl::SystemException>(
        etl::ErrorCode::DATABASE_ERROR, "Database error: " + message,
        "DatabaseSystem", context);
  }

  // Network-related errors
  if (lowerMessage.find("network") != std::string::npos ||
      lowerMessage.find("socket") != std::string::npos ||
      lowerMessage.find("timeout") != std::string::npos ||
      lowerMessage.find("refused") != std::string::npos) {
    return std::make_shared<etl::SystemException>(etl::ErrorCode::NETWORK_ERROR,
                                                  "Network error: " + message,
                                                  "NetworkSystem", context);
  }

  // Memory-related errors
  if (lowerMessage.find("memory") != std::string::npos ||
      lowerMessage.find("allocation") != std::string::npos ||
      lowerMessage.find("bad_alloc") != std::string::npos) {
    return std::make_shared<etl::SystemException>(etl::ErrorCode::MEMORY_ERROR,
                                                  "Memory error: " + message,
                                                  "MemorySystem", context);
  }

  // File-related errors
  if (lowerMessage.find("file") != std::string::npos ||
      lowerMessage.find("permission") != std::string::npos ||
      lowerMessage.find("access") != std::string::npos) {
    return std::make_shared<etl::SystemException>(
        etl::ErrorCode::FILE_ERROR, "File/Resource error: " + message,
        "FileSystem", context);
  }

  // Default to system exception
  return std::make_shared<etl::SystemException>(
      etl::ErrorCode::INTERNAL_ERROR,
      "Converted standard exception: " + message, "UnknownSystem", context);
}

void ExceptionHandler::logException(const etl::ETLException &ex,
                                    const std::string &operationName) {

  std::string logMessage;
  try {
    logMessage = ex.toLogString();
  } catch (const std::exception &fmtEx) {
    logMessage =
        std::string("ETLException (toLogString failed): ") + fmtEx.what();
  }
  if (!operationName.empty()) {
    logMessage = "[" + operationName + "] " + logMessage;
  }
  // Log all exceptions as errors for now (simplified from severity-based logging)
  LOG_ERROR("ExceptionHandler", logMessage);
}

void ExceptionHandler::handleETLException(const etl::ETLException &ex,
                                          ExceptionPolicy policy,
                                          const std::string &operationName,
                                          const etl::ErrorContext &context) {

  switch (policy) {
  case ExceptionPolicy::PROPAGATE:
    logException(ex, operationName);
    throw;

  case ExceptionPolicy::LOG_AND_IGNORE:
    logException(ex, operationName);
    LOG_WARN("ExceptionHandler",
             "Exception ignored for operation: " + operationName);
    break;

  case ExceptionPolicy::LOG_AND_RETURN:
    logException(ex, operationName);
    LOG_INFO("ExceptionHandler",
             "Exception handled, returning error status for operation: " +
                 operationName);
    break;

  case ExceptionPolicy::RETRY:
    logException(ex, operationName);
    LOG_INFO("ExceptionHandler",
             "Exception will trigger retry for operation: " + operationName);
    throw;
  }
}

void ExceptionHandler::handleStandardException(
    const std::exception &ex, ExceptionPolicy policy,
    const std::string &operationName, const etl::ErrorContext &context) {

  auto etlEx = convertException(ex, operationName, context);
  handleETLException(*etlEx, policy, operationName, context);
}

void ExceptionHandler::handleUnknownException(
    ExceptionPolicy policy, const std::string &operationName,
    const etl::ErrorContext &context) {

  auto unknownEx = etl::SystemException(etl::ErrorCode::INTERNAL_ERROR,
                                        "Unknown exception caught",
                                        "UnknownSystem", context);

  handleETLException(unknownEx, policy, operationName, context);
}

std::chrono::milliseconds
ExceptionHandler::calculateDelay(int attempt, const RetryConfig &config) {

  auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
      config.initialDelay * std::pow(config.backoffMultiplier, attempt - 1));

  return std::min(delay, config.maxDelay);
}

} // namespace ExceptionHandling
} // namespace ETLPlus
