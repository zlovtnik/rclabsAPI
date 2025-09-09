#pragma once

#include "logger.hpp"
#include "transparent_string_hash.hpp"
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace etl {

// Forward declarations for component traits
template <typename Component> struct ComponentTrait;

// Component trait specializations for type safety and compile-time performance
template <> struct ComponentTrait<class AuthManager> {
  static constexpr const char *name = "AuthManager";
};

template <> struct ComponentTrait<class ConfigManager> {
  static constexpr const char *name = "ConfigManager";
};

template <> struct ComponentTrait<class DatabaseManager> {
  static constexpr const char *name = "DatabaseManager";
};

template <> struct ComponentTrait<class DataTransformer> {
  static constexpr const char *name = "DataTransformer";
};

template <> struct ComponentTrait<class ETLJobManager> {
  static constexpr const char *name = "ETLJobManager";
};

template <> struct ComponentTrait<class HttpServer> {
  static constexpr const char *name = "HttpServer";
};

template <> struct ComponentTrait<class JobMonitorService> {
  static constexpr const char *name = "JobMonitorService";
};

template <> struct ComponentTrait<class NotificationService> {
  static constexpr const char *name = "NotificationService";
};

template <> struct ComponentTrait<class RequestHandler> {
  static constexpr const char *name = "RequestHandler";
};

template <> struct ComponentTrait<class WebSocketManager> {
  static constexpr const char *name = "WebSocketManager";
};

template <> struct ComponentTrait<class WebSocketFilterManager> {
  static constexpr const char *name = "WebSocketFilterManager";
};

template <> struct ComponentTrait<class LogFileManager> {
  static constexpr const char *name = "LogFileManager";
};

template <> struct ComponentTrait<class LogHandler> {
  static constexpr const char *name = "LogHandler";
};

template <> struct ComponentTrait<class SystemMetrics> {
  static constexpr const char *name = "SystemMetrics";
};

template <> struct ComponentTrait<class InputValidator> {
  static constexpr const char *name = "InputValidator";
};

template <> struct ComponentTrait<class ExceptionHandler> {
  static constexpr const char *name = "ExceptionHandler";
};

template <> struct ComponentTrait<class ResourceManager> {
  static constexpr const char *name = "ResourceManager";
};

template <> struct ComponentTrait<class WebSocketConnection> {
  static constexpr const char *name = "WebSocketConnection";
};

template <> struct ComponentTrait<class RateLimiter> {
  static constexpr const char *name = "RateLimiter";
};

/**
 * ComponentLogger - Template-based logging system for compile-time type safety
 * and performance optimization. Replaces macro-based logging approach.
 *
 * Features:
 * - Compile-time component name resolution via ComponentTrait
 * - Type-safe logging calls with template parameter validation
 * - Zero-overhead abstraction when optimized
 * - Support for both standard and job-specific logging
 * - Context-aware logging with metadata support
 */
template <typename Component> class ComponentLogger {
private:
  static_assert(std::is_class_v<Component>, "Component must be a class type");

  // Ensure ComponentTrait is specialized for this component
  static constexpr const char *component_name = ComponentTrait<Component>::name;

  // Get logger instance once for better performance
  static Logger &getLogger() { return Logger::getInstance(); }

public:
  // Standard logging methods with compile-time component name resolution

  template <typename... Args>
  static void debug(const std::string &message, Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().debug(component_name,
                        format_message(message, std::forward<Args>(args)...));
    } else {
      getLogger().debug(component_name, message);
    }
  }

  template <typename... Args>
  static void info(const std::string &message, Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().info(component_name,
                       format_message(message, std::forward<Args>(args)...));
    } else {
      getLogger().info(component_name, message);
    }
  }

  template <typename... Args>
  static void warn(const std::string &message, Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().warn(component_name,
                       format_message(message, std::forward<Args>(args)...));
    } else {
      getLogger().warn(component_name, message);
    }
  }

  template <typename... Args>
  static void error(const std::string &message, Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().error(component_name,
                        format_message(message, std::forward<Args>(args)...));
    } else {
      getLogger().error(component_name, message);
    }
  }

  template <typename... Args>
  static void fatal(const std::string &message, Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().fatal(component_name,
                        format_message(message, std::forward<Args>(args)...));
    } else {
      getLogger().fatal(component_name, message);
    }
  }

  // Job-specific logging methods

  template <typename... Args>
  static void debugJob(const std::string &message, const std::string &jobId,
                       Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().debugForJob(
          component_name, format_message(message, std::forward<Args>(args)...),
          jobId);
    } else {
      getLogger().debugForJob(component_name, message, jobId);
    }
  }

  template <typename... Args>
  static void infoJob(const std::string &message, const std::string &jobId,
                      Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().infoForJob(
          component_name, format_message(message, std::forward<Args>(args)...),
          jobId);
    } else {
      getLogger().infoForJob(component_name, message, jobId);
    }
  }

  template <typename... Args>
  static void warnJob(const std::string &message, const std::string &jobId,
                      Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().warnForJob(
          component_name, format_message(message, std::forward<Args>(args)...),
          jobId);
    } else {
      getLogger().warnForJob(component_name, message, jobId);
    }
  }

  template <typename... Args>
  static void errorJob(const std::string &message, const std::string &jobId,
                       Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().errorForJob(
          component_name, format_message(message, std::forward<Args>(args)...),
          jobId);
    } else {
      getLogger().errorForJob(component_name, message, jobId);
    }
  }

  template <typename... Args>
  static void fatalJob(const std::string &message, const std::string &jobId,
                       Args &&...args) {
    if constexpr (sizeof...(args) > 0) {
      getLogger().fatalForJob(
          component_name, format_message(message, std::forward<Args>(args)...),
          jobId);
    } else {
      getLogger().fatalForJob(component_name, message, jobId);
    }
  }

  // Context-aware logging with metadata

  static void debugWithContext(
      const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().debug(component_name, message, context);
  }

  static void infoWithContext(
      const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().info(component_name, message, context);
  }

  static void warnWithContext(
      const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().warn(component_name, message, context);
  }

  static void errorWithContext(
      const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().error(component_name, message, context);
  }

  static void fatalWithContext(
      const std::string &message,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().fatal(component_name, message, context);
  }

  // Job-specific logging with context

  static void debugJobWithContext(
      const std::string &message, const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().debugForJob(component_name, message, jobId, context);
  }

  static void infoJobWithContext(
      const std::string &message, const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().infoForJob(component_name, message, jobId, context);
  }

  static void warnJobWithContext(
      const std::string &message, const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().warnForJob(component_name, message, jobId, context);
  }

  static void errorJobWithContext(
      const std::string &message, const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().errorForJob(component_name, message, jobId, context);
  }

  static void fatalJobWithContext(
      const std::string &message, const std::string &jobId,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().fatalForJob(component_name, message, jobId, context);
  }

  // Performance and metrics logging

  static void logMetric(const std::string &name, double value,
                        const std::string &unit = "") {
    getLogger().logMetric(name, value, unit);
  }

  static void logPerformance(
      const std::string &operation, double durationMs,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &context = {}) {
    getLogger().logPerformance(operation, durationMs, context);
  }

  // Utility methods

  static constexpr const char *getComponentName() { return component_name; }

  // Backward compatibility methods for existing test code
  static void log_debug(const char *component, const std::string &message) {
    getLogger().debug(component, message);
  }

  static void log_info(const char *component, const std::string &message) {
    getLogger().info(component, message);
  }

  static void log_warn(const char *component, const std::string &message) {
    getLogger().warn(component, message);
  }

  static void log_error(const char *component, const std::string &message) {
    getLogger().error(component, message);
  }

  static void log_fatal(const char *component, const std::string &message) {
    getLogger().fatal(component, message);
  }

  static void log_info_job(const char *component, const std::string &message,
                           const std::string &jobId) {
    getLogger().infoForJob(component, message, jobId);
  }

  static void log_error_job(const char *component, const std::string &message,
                            const std::string &jobId) {
    getLogger().errorForJob(component, message, jobId);
  }

  static void log_debug_job(const char *component, const std::string &message,
                            const std::string &jobId) {
    getLogger().debugForJob(component, message, jobId);
  }

  static void log_warn_job(const char *component, const std::string &message,
                           const std::string &jobId) {
    getLogger().warnForJob(component, message, jobId);
  }

  static void log_fatal_job(const char *component, const std::string &message,
                            const std::string &jobId) {
    getLogger().fatalForJob(component, message, jobId);
  }

private:
  // Helper function to convert unordered_map to string representation
  template <typename K, typename V, typename H, typename E>
  static std::string to_string(const std::unordered_map<K, V, H, E> &map) {
    std::stringstream ss;
    ss << "{";
    bool first = true;
    for (const auto &pair : map) {
      if (!first)
        ss << ", ";
      ss << pair.first << ": " << pair.second;
      first = false;
    }
    ss << "}";
    return ss.str();
  }

  // Helper function to convert any type to string for logging
  template <typename T>
  static void stream_value(std::stringstream &ss, T &&value) {
    // Detect any unordered_map<std::string, std::string, H, E>
    if constexpr (std::is_same_v<std::string,
                                 typename std::decay_t<T>::key_type> &&
                  std::is_same_v<std::string,
                                 typename std::decay_t<T>::mapped_type> &&
                  std::is_same_v<
                      std::unordered_map<typename std::decay_t<T>::key_type,
                                         typename std::decay_t<T>::mapped_type,
                                         typename std::decay_t<T>::hasher,
                                         typename std::decay_t<T>::key_equal>,
                      std::decay_t<T>>) {
      ss << to_string(value);
    } else if constexpr (std::is_arithmetic_v<std::decay_t<T>> ||
                         std::is_convertible_v<T, std::string>) {
      ss << std::forward<T>(value);
    } else {
      ss << "[object]";
    }
  }

  // Helper function for message formatting with variadic templates
  template <typename... Args>
  static std::string format_message(const std::string &format, Args &&...args) {
    // Simple string formatting - could be enhanced with fmt library
    std::stringstream ss;
    format_impl(ss, format, std::forward<Args>(args)...);
    return ss.str();
  }

  template <typename T, typename... Args>
  static void format_impl(std::stringstream &ss, const std::string &format,
                          T &&arg, Args &&...args) {
    size_t pos = format.find("{}");
    if (pos != std::string::npos) {
      ss << format.substr(0, pos);
      stream_value(ss, std::forward<T>(arg));
      if constexpr (sizeof...(args) > 0) {
        format_impl(ss, format.substr(pos + 2), std::forward<Args>(args)...);
      } else {
        ss << format.substr(pos + 2);
      }
    } else {
      ss << format;
    }
  }

  static void format_impl(std::stringstream &ss, const std::string &format) {
    ss << format;
  }
};

// Convenience type aliases for commonly used component loggers
using ConfigLogger = ComponentLogger<class ConfigManager>;
using DatabaseLogger = ComponentLogger<class DatabaseManager>;
using ETLJobLogger = ComponentLogger<class ETLJobManager>;
using WebSocketLogger = ComponentLogger<class WebSocketManager>;
using AuthLogger = ComponentLogger<class AuthManager>;
using HttpLogger = ComponentLogger<class HttpServer>;
using JobMonitorLogger = ComponentLogger<class JobMonitorService>;
using NotificationLogger = ComponentLogger<class NotificationService>;
using DataTransformerLogger = ComponentLogger<class DataTransformer>;
using WebSocketFilterLogger = ComponentLogger<class WebSocketFilterManager>;
using LogFileLogger = ComponentLogger<class LogFileManager>;
using LogHandlerLogger = ComponentLogger<class LogHandler>;
using SystemMetricsLogger = ComponentLogger<class SystemMetrics>;
using InputValidatorLogger = ComponentLogger<class InputValidator>;
using ExceptionHandlerLogger = ComponentLogger<class ExceptionHandler>;
using WebSocketConnectionLogger = ComponentLogger<class WebSocketConnection>;

} // namespace etl

// Template-based macros for easier migration from existing macro-based code
#define COMPONENT_LOG_DEBUG(ComponentClass, message, ...)                      \
  etl::ComponentLogger<ComponentClass>::debug(message, ##__VA_ARGS__)

#define COMPONENT_LOG_INFO(ComponentClass, message, ...)                       \
  etl::ComponentLogger<ComponentClass>::info(message, ##__VA_ARGS__)

#define COMPONENT_LOG_WARN(ComponentClass, message, ...)                       \
  etl::ComponentLogger<ComponentClass>::warn(message, ##__VA_ARGS__)

#define COMPONENT_LOG_ERROR(ComponentClass, message, ...)                      \
  etl::ComponentLogger<ComponentClass>::error(message, ##__VA_ARGS__)

#define COMPONENT_LOG_FATAL(ComponentClass, message, ...)                      \
  etl::ComponentLogger<ComponentClass>::fatal(message, ##__VA_ARGS__)

#define COMPONENT_LOG_DEBUG_JOB(ComponentClass, message, jobId, ...)           \
  etl::ComponentLogger<ComponentClass>::debugJob(message, jobId, ##__VA_ARGS__)

#define COMPONENT_LOG_INFO_JOB(ComponentClass, message, jobId, ...)            \
  etl::ComponentLogger<ComponentClass>::infoJob(message, jobId, ##__VA_ARGS__)

#define COMPONENT_LOG_WARN_JOB(ComponentClass, message, jobId, ...)            \
  etl::ComponentLogger<ComponentClass>::warnJob(message, jobId, ##__VA_ARGS__)

#define COMPONENT_LOG_ERROR_JOB(ComponentClass, message, jobId, ...)           \
  etl::ComponentLogger<ComponentClass>::errorJob(message, jobId, ##__VA_ARGS__)

#define COMPONENT_LOG_FATAL_JOB(ComponentClass, message, jobId, ...)           \
  etl::ComponentLogger<ComponentClass>::fatalJob(message, jobId, ##__VA_ARGS__)

// Convenient component-specific macros that replace old hardcoded string macros
#define CONFIG_LOG_DEBUG(message, ...)                                         \
  etl::ConfigLogger::debug(message, ##__VA_ARGS__)
#define CONFIG_LOG_INFO(message, ...)                                          \
  etl::ConfigLogger::info(message, ##__VA_ARGS__)
#define CONFIG_LOG_WARN(message, ...)                                          \
  etl::ConfigLogger::warn(message, ##__VA_ARGS__)
#define CONFIG_LOG_ERROR(message, ...)                                         \
  etl::ConfigLogger::error(message, ##__VA_ARGS__)
#define CONFIG_LOG_FATAL(message, ...)                                         \
  etl::ConfigLogger::fatal(message, ##__VA_ARGS__)

#define DB_LOG_DEBUG(message, ...)                                             \
  etl::DatabaseLogger::debug(message, ##__VA_ARGS__)
#define DB_LOG_INFO(message, ...)                                              \
  etl::DatabaseLogger::info(message, ##__VA_ARGS__)
#define DB_LOG_WARN(message, ...)                                              \
  etl::DatabaseLogger::warn(message, ##__VA_ARGS__)
#define DB_LOG_ERROR(message, ...)                                             \
  etl::DatabaseLogger::error(message, ##__VA_ARGS__)
#define DB_LOG_FATAL(message, ...)                                             \
  etl::DatabaseLogger::fatal(message, ##__VA_ARGS__)

#define ETL_LOG_DEBUG(message, ...)                                            \
  etl::ETLJobLogger::debug(message, ##__VA_ARGS__)
#define ETL_LOG_INFO(message, ...)                                             \
  etl::ETLJobLogger::info(message, ##__VA_ARGS__)
#define ETL_LOG_WARN(message, ...)                                             \
  etl::ETLJobLogger::warn(message, ##__VA_ARGS__)
#define ETL_LOG_ERROR(message, ...)                                            \
  etl::ETLJobLogger::error(message, ##__VA_ARGS__)
#define ETL_LOG_FATAL(message, ...)                                            \
  etl::ETLJobLogger::fatal(message, ##__VA_ARGS__)

#define ETL_LOG_DEBUG_JOB(message, jobId, ...)                                 \
  etl::ETLJobLogger::debugJob(message, jobId, ##__VA_ARGS__)
#define ETL_LOG_INFO_JOB(message, jobId, ...)                                  \
  etl::ETLJobLogger::infoJob(message, jobId, ##__VA_ARGS__)
#define ETL_LOG_WARN_JOB(message, jobId, ...)                                  \
  etl::ETLJobLogger::warnJob(message, jobId, ##__VA_ARGS__)
#define ETL_LOG_ERROR_JOB(message, jobId, ...)                                 \
  etl::ETLJobLogger::errorJob(message, jobId, ##__VA_ARGS__)
#define ETL_LOG_FATAL_JOB(message, jobId, ...)                                 \
  etl::ETLJobLogger::fatalJob(message, jobId, ##__VA_ARGS__)

#define WS_LOG_DEBUG(message, ...)                                             \
  etl::WebSocketLogger::debug(message, ##__VA_ARGS__)
#define WS_LOG_INFO(message, ...)                                              \
  etl::WebSocketLogger::info(message, ##__VA_ARGS__)
#define WS_LOG_WARN(message, ...)                                              \
  etl::WebSocketLogger::warn(message, ##__VA_ARGS__)
#define WS_LOG_ERROR(message, ...)                                             \
  etl::WebSocketLogger::error(message, ##__VA_ARGS__)
#define WS_LOG_FATAL(message, ...)                                             \
  etl::WebSocketLogger::fatal(message, ##__VA_ARGS__)

#define AUTH_LOG_DEBUG(message, ...)                                           \
  etl::AuthLogger::debug(message, ##__VA_ARGS__)
#define AUTH_LOG_INFO(message, ...)                                            \
  etl::AuthLogger::info(message, ##__VA_ARGS__)
#define AUTH_LOG_WARN(message, ...)                                            \
  etl::AuthLogger::warn(message, ##__VA_ARGS__)
#define AUTH_LOG_ERROR(message, ...)                                           \
  etl::AuthLogger::error(message, ##__VA_ARGS__)
#define AUTH_LOG_FATAL(message, ...)                                           \
  etl::AuthLogger::fatal(message, ##__VA_ARGS__)

#define HTTP_LOG_DEBUG(message, ...)                                           \
  etl::HttpLogger::debug(message, ##__VA_ARGS__)
#define HTTP_LOG_INFO(message, ...)                                            \
  etl::HttpLogger::info(message, ##__VA_ARGS__)
#define HTTP_LOG_WARN(message, ...)                                            \
  etl::HttpLogger::warn(message, ##__VA_ARGS__)
#define HTTP_LOG_ERROR(message, ...)                                           \
  etl::HttpLogger::error(message, ##__VA_ARGS__)
#define HTTP_LOG_FATAL(message, ...)                                           \
  etl::HttpLogger::fatal(message, ##__VA_ARGS__)
