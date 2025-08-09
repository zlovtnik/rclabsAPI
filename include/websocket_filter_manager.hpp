#pragma once

#include "websocket_manager.hpp"
#include "job_monitoring_models.hpp"
#include "transparent_string_hash.hpp"
#include <boost/beast/http.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace http = boost::beast::http;

/**
 * @brief Manages WebSocket connection filters and preferences
 * 
 * This class provides a high-level interface for managing WebSocket connection
 * filters, including REST API endpoints and advanced routing capabilities.
 */
class WebSocketFilterManager {
public:
    explicit WebSocketFilterManager(std::shared_ptr<WebSocketManager> wsManager);
    ~WebSocketFilterManager() = default;

    // REST API handlers for filter management
    http::response<http::string_body>
    handleGetConnectionFilters(const std::string& connectionId);
    
    http::response<http::string_body>
    handleSetConnectionFilters(const std::string& connectionId, const std::string& requestBody);
    
    http::response<http::string_body>
    handleUpdateConnectionFilters(const std::string& connectionId, const std::string& requestBody);
    
    http::response<http::string_body>
    handleAddJobFilter(const std::string& connectionId, const std::string& jobId);
    
    http::response<http::string_body>
    handleRemoveJobFilter(const std::string& connectionId, const std::string& jobId);
    
    http::response<http::string_body>
    handleAddMessageTypeFilter(const std::string& connectionId, const std::string& messageType);
    
    http::response<http::string_body>
    handleRemoveMessageTypeFilter(const std::string& connectionId, const std::string& messageType);
    
    http::response<http::string_body>
    handleAddLogLevelFilter(const std::string& connectionId, const std::string& logLevel);
    
    http::response<http::string_body>
    handleRemoveLogLevelFilter(const std::string& connectionId, const std::string& logLevel);
    
    http::response<http::string_body>
    handleClearConnectionFilters(const std::string& connectionId);
    
    http::response<http::string_body>
    handleGetConnectionStats();
    
    http::response<http::string_body>
    handleTestConnectionFilter(const std::string& connectionId, const std::string& requestBody);
    
    http::response<http::string_body>
    handleGetFilterStatistics();
    
    // Advanced filter management
    void saveConnectionPreferences(const std::string& connectionId, const ConnectionFilters& filters);
    bool loadConnectionPreferences(const std::string& connectionId, ConnectionFilters& filters);
    void clearStoredPreferences(const std::string& connectionId);
    
    // Batch operations
    void applyFiltersToMultipleConnections(const std::vector<std::string>& connectionIds, 
                                         const ConnectionFilters& filters);
    void clearFiltersFromMultipleConnections(const std::vector<std::string>& connectionIds);
    
    // Advanced routing and analytics
    std::vector<std::string> findConnectionsMatchingFilter(
        std::function<bool(const ConnectionFilters&)> predicate) const;
    
    void broadcastToFilteredConnections(
        const WebSocketMessage& message,
        std::function<bool(const ConnectionFilters&)> customFilter);
    
    // Filter template management
    void saveFilterTemplate(const std::string& templateName, const ConnectionFilters& filters);
    bool loadFilterTemplate(const std::string& templateName, ConnectionFilters& filters);
    void applyFilterTemplate(const std::string& connectionId, const std::string& templateName);
    std::vector<std::string> getAvailableFilterTemplates() const;
    
    // Statistics and monitoring
    struct FilterStatistics {
        size_t totalConnections;
        size_t filteredConnections;
        size_t unfilteredConnections;
        std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> jobFilterCounts;
        std::unordered_map<MessageType, size_t> messageTypeFilterCounts;
        std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> logLevelFilterCounts;
        double averageFiltersPerConnection;
    };
    
    FilterStatistics getFilterStatistics() const;

private:
    std::shared_ptr<WebSocketManager> wsManager_;
    
    // Persistent storage for preferences (could be file-based or database)
    std::unordered_map<std::string, ConnectionFilters, TransparentStringHash, std::equal_to<>> 
        storedPreferences_;
    
    // Filter templates
    std::unordered_map<std::string, ConnectionFilters, TransparentStringHash, std::equal_to<>> 
        filterTemplates_;
    
    mutable std::mutex preferencesLock_;
    mutable std::mutex templatesLock_;
    
    // Utility methods
    http::response<http::string_body>
    createSuccessResponse(const std::string& data);
    
    http::response<http::string_body>
    createErrorResponse(http::status status, const std::string& message);
    
    ConnectionFilters parseConnectionFiltersFromJson(const std::string& json);
    WebSocketMessage parseWebSocketMessageFromJson(const std::string& json);
    std::string connectionFiltersToJson(const ConnectionFilters& filters);
    std::string filterStatisticsToJson(const FilterStatistics& stats) const;
    
    bool validateConnectionExists(const std::string& connectionId);
    bool validateFilterData(const ConnectionFilters& filters, std::string& errorMessage);
    
    void initializeDefaultTemplates();
};
