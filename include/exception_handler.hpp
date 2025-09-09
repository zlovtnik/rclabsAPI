#pragma once

#include "database_manager.hpp"
#include "etl_exceptions.hpp"
#include "logger.hpp"
#include <chrono>
#include <functional>
#include <memory>

namespace ETLPlus {
namespace ExceptionHandling {

// RAII wrapper for database transactions
class TransactionScope {
private:
  std::shared_ptr<DatabaseManager> dbManager_;
  bool committed_;
  bool rollbackOnDestroy_;
  std::string operationName_;
  etl::ErrorContext context_;

public:
  explicit TransactionScope(std::shared_ptr<DatabaseManager> dbManager,
                            const std::string &operationName = "");
  ~TransactionScope();

  // Disable copy constructor and assignment
  TransactionScope(const TransactionScope &) = delete;
  TransactionScope &operator=(const TransactionScope &) = delete;

  // Enable move constructor and assignment
  TransactionScope(TransactionScope &&) noexcept;
  TransactionScope &operator=(TransactionScope &&) noexcept;

  void commit();
  void rollback();
  void setRollbackOnDestroy(bool rollback) { rollbackOnDestroy_ = rollback; }

  const etl::ErrorContext &getContext() const { return context_; }
};

// Exception-safe resource wrapper
template <typename T> class ResourceGuard {
private:
  T resource_;
  std::function<void(T &)> cleanup_;
  bool released_;

public:
  template <typename... Args>
  ResourceGuard(std::function<void(T &)> cleanup, Args &&...args)
      : resource_(std::forward<Args>(args)...), cleanup_(cleanup),
        released_(false) {}

  ~ResourceGuard() {
    if (!released_ && cleanup_) {
      try {
        cleanup_(resource_);
      } catch (...) {
        // Log cleanup failure but don't throw from destructor
        LOG_ERROR("ResourceGuard", "Exception during resource cleanup");
      }
    }
  }

  // Disable copy
  ResourceGuard(const ResourceGuard &) = delete;
  ResourceGuard &operator=(const ResourceGuard &) = delete;

  // Enable move
  ResourceGuard(ResourceGuard &&other) noexcept
      : resource_(std::move(other.resource_)),
        cleanup_(std::move(other.cleanup_)), released_(other.released_) {
    other.released_ = true;
  }

  ResourceGuard &operator=(ResourceGuard &&other) noexcept {
    if (this != &other) {
      if (!released_ && cleanup_) {
        cleanup_(resource_);
      }
      resource_ = std::move(other.resource_);
      cleanup_ = std::move(other.cleanup_);
      released_ = other.released_;
      other.released_ = true;
    }
    return *this;
  }

  T &get() { return resource_; }
  const T &get() const { return resource_; }

  T &operator*() { return resource_; }
  const T &operator*() const { return resource_; }

  T *operator->() { return &resource_; }
  const T *operator->() const { return &resource_; }

  void release() { released_ = true; }
};

// Exception handling policy
enum class ExceptionPolicy {
  PROPAGATE,      // Let exceptions bubble up
  LOG_AND_IGNORE, // Log the exception but continue execution
  LOG_AND_RETURN, // Log the exception and return error status
  RETRY           // Attempt to retry the operation
};

// Retry configuration
struct RetryConfig {
  int maxAttempts = 3;
  std::chrono::milliseconds initialDelay{100};
  double backoffMultiplier = 2.0;
  std::chrono::milliseconds maxDelay{5000};
  std::function<bool(const etl::ETLException &)> shouldRetry;

  RetryConfig() {
    // Default retry policy: retry on transient errors
    shouldRetry = [](const etl::ETLException &ex) {
      switch (ex.getCode()) {
      case etl::ErrorCode::NETWORK_ERROR:
      case etl::ErrorCode::DATABASE_ERROR:
      case etl::ErrorCode::COMPONENT_UNAVAILABLE:
      case etl::ErrorCode::LOCK_TIMEOUT:
        return true;
      default:
        return false;
      }
    };
  }
};

// Exception handling utility class
class ExceptionHandler {
public:
  // Execute function with exception handling
  template <typename Func, typename ReturnType = std::invoke_result_t<Func>>
  static ReturnType
  executeWithHandling(Func &&func,
                      ExceptionPolicy policy = ExceptionPolicy::PROPAGATE,
                      const std::string &operationName = "",
                      const etl::ErrorContext &context = etl::ErrorContext()) {

    try {
      return func();
    } catch (const etl::ETLException &ex) {
      handleETLException(ex, policy, operationName, context);
      if constexpr (!std::is_void_v<ReturnType>) {
        return ReturnType{};
      }
    } catch (const std::exception &ex) {
      handleStandardException(ex, policy, operationName, context);
      if constexpr (!std::is_void_v<ReturnType>) {
        return ReturnType{};
      }
    } catch (...) {
      handleUnknownException(policy, operationName, context);
      if constexpr (!std::is_void_v<ReturnType>) {
        return ReturnType{};
      }
    }
  }

  // Execute function with retry logic
  template <typename Func, typename ReturnType = std::invoke_result_t<Func>>
  static ReturnType
  executeWithRetry(Func &&func, const RetryConfig &config = RetryConfig(),
                   const std::string &operationName = "",
                   const etl::ErrorContext &context = etl::ErrorContext()) {

    std::shared_ptr<etl::ETLException> lastException;

    for (int attempt = 1; attempt <= config.maxAttempts; ++attempt) {
      try {
        return func();
      } catch (const etl::ETLException &ex) {
        lastException = std::make_shared<etl::ETLException>(ex);

        if (attempt == config.maxAttempts || !config.shouldRetry(ex)) {
          LOG_ERROR("ExceptionHandler", "Operation '" + operationName +
                                            "' failed after " +
                                            std::to_string(attempt) +
                                            " attempts: " + ex.toLogString());
          throw;
        }

        auto delay = calculateDelay(attempt, config);
        LOG_WARN("ExceptionHandler",
                 "Operation '" + operationName + "' failed (attempt " +
                     std::to_string(attempt) + "/" +
                     std::to_string(config.maxAttempts) + "), retrying in " +
                     std::to_string(delay.count()) + "ms: " + ex.getMessage());

        std::this_thread::sleep_for(delay);
      } catch (const std::exception &ex) {
        // Convert to ETLException for consistent handling
        auto baseEx = std::make_shared<etl::SystemException>(
            etl::ErrorCode::INTERNAL_ERROR,
            "Standard exception caught: " + std::string(ex.what()), "",
            context);

        if (attempt == config.maxAttempts) {
          LOG_ERROR("ExceptionHandler",
                    "Operation '" + operationName + "' failed after " +
                        std::to_string(attempt) +
                        " attempts: " + std::string(ex.what()));
          throw *baseEx;
        }

        auto delay = calculateDelay(attempt, config);
        LOG_WARN("ExceptionHandler",
                 "Operation '" + operationName + "' failed (attempt " +
                     std::to_string(attempt) + "/" +
                     std::to_string(config.maxAttempts) + "), retrying in " +
                     std::to_string(delay.count()) +
                     "ms: " + std::string(ex.what()));

        std::this_thread::sleep_for(delay);
      }
    }

    // This should never be reached, but just in case
    if (lastException) {
      throw *lastException;
    }
    throw etl::SystemException(etl::ErrorCode::INTERNAL_ERROR,
                               "Unexpected error in retry logic", "", context);
  }

  // Convert standard exceptions to ETLException
  static std::shared_ptr<etl::ETLException>
  convertException(const std::exception &ex,
                   const std::string &operationName = "",
                   const etl::ErrorContext &context = etl::ErrorContext());

  // Log exception with appropriate level
  static void logException(const etl::ETLException &ex,
                           const std::string &operationName = "");

private:
  static void handleETLException(const etl::ETLException &ex,
                                 ExceptionPolicy policy,
                                 const std::string &operationName,
                                 const etl::ErrorContext &context);

  static void handleStandardException(const std::exception &ex,
                                      ExceptionPolicy policy,
                                      const std::string &operationName,
                                      const etl::ErrorContext &context);

  static void handleUnknownException(ExceptionPolicy policy,
                                     const std::string &operationName,
                                     const etl::ErrorContext &context);

  static std::chrono::milliseconds calculateDelay(int attempt,
                                                  const RetryConfig &config);
};

// Convenience macros for exception handling
#define EXECUTE_WITH_EXCEPTION_HANDLING(func, policy, operation)               \
  ETLPlus::ExceptionHandling::ExceptionHandler::executeWithHandling(           \
      [&]() { return func; }, policy, operation, etl::ErrorContext())

#define EXECUTE_WITH_RETRY(func, config, operation)                            \
  ETLPlus::ExceptionHandling::ExceptionHandler::executeWithRetry(              \
      [&]() { return func; }, config, operation, etl::ErrorContext())

#define TRY_CATCH_LOG(operation, func)                                         \
  try {                                                                        \
    func;                                                                      \
  } catch (const etl::ETLException &ex) {                                      \
    ETLPlus::ExceptionHandling::ExceptionHandler::logException(ex, operation); \
    throw;                                                                     \
  } catch (const std::exception &ex) {                                         \
    auto etlEx =                                                               \
        ETLPlus::ExceptionHandling::ExceptionHandler::convertException(        \
            ex, operation);                                                    \
    ETLPlus::ExceptionHandling::ExceptionHandler::logException(*etlEx,         \
                                                               operation);     \
    throw *etlEx;                                                              \
  }

// Database transaction helpers
#define WITH_DATABASE_TRANSACTION(dbManager, operation, code)                  \
  do {                                                                         \
    ETLPlus::ExceptionHandling::TransactionScope transaction(dbManager,        \
                                                             operation);       \
    try {                                                                      \
      code;                                                                    \
      transaction.commit();                                                    \
    } catch (...) {                                                            \
      transaction.rollback();                                                  \
      throw;                                                                   \
    }                                                                          \
  } while (0)

} // namespace ExceptionHandling
} // namespace ETLPlus
