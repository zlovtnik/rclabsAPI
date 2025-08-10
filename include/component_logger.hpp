#pragma once

#include <string>
#include <string_view>
#include <type_traits>

namespace etl {

// Forward declarations for existing components
class AuthManager;
class ConfigManager;
class DatabaseManager;
class DataTransformer;
class ETLJobManager;
class HttpServer;
class JobMonitorService;
class NotificationService;
class RequestHandler;
class WebSocketManager;
class WebSocketFilterManager;

// Component trait definition - specializations define component names
template<typename Component>
struct ComponentTrait {
    static constexpr const char* name = "Unknown";
};

// Component trait specializations for existing components
template<>
struct ComponentTrait<AuthManager> {
    static constexpr const char* name = "AuthManager";
};

template<>
struct ComponentTrait<ConfigManager> {
    static constexpr const char* name = "ConfigManager";
};

template<>
struct ComponentTrait<DatabaseManager> {
    static constexpr const char* name = "DatabaseManager";
};

template<>
struct ComponentTrait<DataTransformer> {
    static constexpr const char* name = "DataTransformer";
};

template<>
struct ComponentTrait<ETLJobManager> {
    static constexpr const char* name = "ETLJobManager";
};

template<>
struct ComponentTrait<HttpServer> {
    static constexpr const char* name = "HttpServer";
};

template<>
struct ComponentTrait<JobMonitorService> {
    static constexpr const char* name = "JobMonitorService";
};

template<>
struct ComponentTrait<NotificationService> {
    static constexpr const char* name = "NotificationService";
};

template<>
struct ComponentTrait<RequestHandler> {
    static constexpr const char* name = "RequestHandler";
};

template<>
struct ComponentTrait<WebSocketManager> {
    static constexpr const char* name = "WebSocket";
};

template<>
struct ComponentTrait<WebSocketFilterManager> {
    static constexpr const char* name = "WebSocketFilter";
};

} // namespace etl
