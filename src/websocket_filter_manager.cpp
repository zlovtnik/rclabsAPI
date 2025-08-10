#include "websocket_filter_manager.hpp"
#include "logger.hpp"
#include "etl_exceptions.hpp"
#include <regex>
#include <sstream>
#include <iomanip>
#include <algorithm>

#define WS_FILTER_LOG_DEBUG(msg) Logger::getInstance().log(LogLevel::DEBUG, "WebSocketFilterManager", msg)
#define WS_FILTER_LOG_INFO(msg) Logger::getInstance().log(LogLevel::INFO, "WebSocketFilterManager", msg)
#define WS_FILTER_LOG_WARN(msg) Logger::getInstance().log(LogLevel::WARN, "WebSocketFilterManager", msg)
#define WS_FILTER_LOG_ERROR(msg) Logger::getInstance().log(LogLevel::ERROR, "WebSocketFilterManager", msg)

WebSocketFilterManager::WebSocketFilterManager(std::shared_ptr<WebSocketManager> wsManager)
    : wsManager_(wsManager) {
    if (!wsManager_) {
        throw etl::ValidationException(etl::ErrorCode::INVALID_INPUT, 
                                       "WebSocket manager cannot be null", 
                                       "wsManager", "null");
    }
    
    initializeDefaultTemplates();
    WS_FILTER_LOG_INFO("WebSocket filter manager initialized");
}

http::response<http::string_body>
WebSocketFilterManager::handleGetConnectionFilters(const std::string& connectionId) {
    WS_FILTER_LOG_DEBUG("Getting filters for connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    try {
        ConnectionFilters filters = wsManager_->getConnectionFilters(connectionId);
        std::string json = connectionFiltersToJson(filters);
        return createSuccessResponse(json);
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to get connection filters: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to retrieve connection filters");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleSetConnectionFilters(const std::string& connectionId, 
                                                  const std::string& requestBody) {
    WS_FILTER_LOG_DEBUG("Setting filters for connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    try {
        ConnectionFilters filters = parseConnectionFiltersFromJson(requestBody);
        
        std::string errorMessage;
        if (!validateFilterData(filters, errorMessage)) {
            return createErrorResponse(http::status::bad_request, 
                                     "Invalid filter data: " + errorMessage);
        }
        
        wsManager_->setConnectionFilters(connectionId, filters);
        saveConnectionPreferences(connectionId, filters);
        
        WS_FILTER_LOG_INFO("Filters set for connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"Filters updated successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to set connection filters: " + std::string(e.what()));
        return createErrorResponse(http::status::bad_request, 
                                 "Failed to parse filter data: " + std::string(e.what()));
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleUpdateConnectionFilters(const std::string& connectionId, 
                                                     const std::string& requestBody) {
    WS_FILTER_LOG_DEBUG("Updating filters for connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    try {
        ConnectionFilters newFilters = parseConnectionFiltersFromJson(requestBody);
        
        std::string errorMessage;
        if (!validateFilterData(newFilters, errorMessage)) {
            return createErrorResponse(http::status::bad_request, 
                                     "Invalid filter data: " + errorMessage);
        }
        
        wsManager_->updateConnectionFilters(connectionId, newFilters);
        saveConnectionPreferences(connectionId, newFilters);
        
        WS_FILTER_LOG_INFO("Filters updated for connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"Filters updated successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to update connection filters: " + std::string(e.what()));
        return createErrorResponse(http::status::bad_request, 
                                 "Failed to parse filter data: " + std::string(e.what()));
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleAddJobFilter(const std::string& connectionId, const std::string& jobId) {
    WS_FILTER_LOG_DEBUG("Adding job filter '" + jobId + "' to connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    if (!validateJobId(jobId)) {
        return createErrorResponse(http::status::bad_request, 
                                 "Invalid job ID: " + jobId);
    }
    
    try {
        wsManager_->addJobFilterToConnection(connectionId, jobId);
        
        // Update stored preferences
        ConnectionFilters currentFilters = wsManager_->getConnectionFilters(connectionId);
        saveConnectionPreferences(connectionId, currentFilters);
        
        WS_FILTER_LOG_INFO("Job filter '" + jobId + "' added to connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"Job filter added successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to add job filter: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to add job filter");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleRemoveJobFilter(const std::string& connectionId, const std::string& jobId) {
    WS_FILTER_LOG_DEBUG("Removing job filter '" + jobId + "' from connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    try {
        wsManager_->removeJobFilterFromConnection(connectionId, jobId);
        
        // Update stored preferences
        ConnectionFilters currentFilters = wsManager_->getConnectionFilters(connectionId);
        saveConnectionPreferences(connectionId, currentFilters);
        
        WS_FILTER_LOG_INFO("Job filter '" + jobId + "' removed from connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"Job filter removed successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to remove job filter: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to remove job filter");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleAddMessageTypeFilter(const std::string& connectionId, 
                                                  const std::string& messageType) {
    WS_FILTER_LOG_DEBUG("Adding message type filter '" + messageType + "' to connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    if (!validateMessageType(messageType)) {
        return createErrorResponse(http::status::bad_request, 
                                 "Invalid message type: " + messageType);
    }
    
    try {
        MessageType msgType = stringToMessageType(messageType);
        wsManager_->addMessageTypeFilterToConnection(connectionId, msgType);
        
        // Update stored preferences
        ConnectionFilters currentFilters = wsManager_->getConnectionFilters(connectionId);
        saveConnectionPreferences(connectionId, currentFilters);
        
        WS_FILTER_LOG_INFO("Message type filter '" + messageType + "' added to connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"Message type filter added successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to add message type filter: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to add message type filter");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleRemoveMessageTypeFilter(const std::string& connectionId, 
                                                     const std::string& messageType) {
    WS_FILTER_LOG_DEBUG("Removing message type filter '" + messageType + "' from connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    try {
        MessageType msgType = stringToMessageType(messageType);
        wsManager_->removeMessageTypeFilterFromConnection(connectionId, msgType);
        
        // Update stored preferences
        ConnectionFilters currentFilters = wsManager_->getConnectionFilters(connectionId);
        saveConnectionPreferences(connectionId, currentFilters);
        
        WS_FILTER_LOG_INFO("Message type filter '" + messageType + "' removed from connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"Message type filter removed successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to remove message type filter: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to remove message type filter");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleAddLogLevelFilter(const std::string& connectionId, 
                                               const std::string& logLevel) {
    WS_FILTER_LOG_DEBUG("Adding log level filter '" + logLevel + "' to connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    if (!validateLogLevel(logLevel)) {
        return createErrorResponse(http::status::bad_request, 
                                 "Invalid log level: " + logLevel);
    }
    
    try {
        wsManager_->addLogLevelFilterToConnection(connectionId, logLevel);
        
        // Update stored preferences
        ConnectionFilters currentFilters = wsManager_->getConnectionFilters(connectionId);
        saveConnectionPreferences(connectionId, currentFilters);
        
        WS_FILTER_LOG_INFO("Log level filter '" + logLevel + "' added to connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"Log level filter added successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to add log level filter: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to add log level filter");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleRemoveLogLevelFilter(const std::string& connectionId, 
                                                  const std::string& logLevel) {
    WS_FILTER_LOG_DEBUG("Removing log level filter '" + logLevel + "' from connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    try {
        wsManager_->removeLogLevelFilterFromConnection(connectionId, logLevel);
        
        // Update stored preferences
        ConnectionFilters currentFilters = wsManager_->getConnectionFilters(connectionId);
        saveConnectionPreferences(connectionId, currentFilters);
        
        WS_FILTER_LOG_INFO("Log level filter '" + logLevel + "' removed from connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"Log level filter removed successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to remove log level filter: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to remove log level filter");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleClearConnectionFilters(const std::string& connectionId) {
    WS_FILTER_LOG_DEBUG("Clearing all filters for connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    try {
        wsManager_->clearConnectionFilters(connectionId);
        clearStoredPreferences(connectionId);
        
        WS_FILTER_LOG_INFO("All filters cleared for connection: " + connectionId);
        return createSuccessResponse(R"({"status":"success","message":"All filters cleared successfully"})");
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to clear connection filters: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to clear connection filters");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleGetConnectionStats() {
    WS_FILTER_LOG_DEBUG("Getting connection statistics");
    
    try {
        FilterStatistics stats = getFilterStatistics();
        std::string json = filterStatisticsToJson(stats);
        return createSuccessResponse(json);
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to get connection statistics: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to retrieve connection statistics");
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleTestConnectionFilter(const std::string& connectionId, 
                                                  const std::string& requestBody) {
    WS_FILTER_LOG_DEBUG("Testing filter for connection: " + connectionId);
    
    if (!validateConnectionExists(connectionId)) {
        return createErrorResponse(http::status::not_found, 
                                 "Connection not found: " + connectionId);
    }
    
    try {
        WebSocketMessage testMessage = parseWebSocketMessageFromJson(requestBody);
        bool shouldReceive = wsManager_->testConnectionFilter(connectionId, testMessage);
        
        std::ostringstream json;
        json << R"({"connectionId":")" << connectionId << R"(",)"
             << R"("shouldReceive":)" << (shouldReceive ? "true" : "false") << R"(,)"
             << R"("messageType":")" << messageTypeToString(testMessage.type) << R"(")"
             << "}";
        
        return createSuccessResponse(json.str());
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to test connection filter: " + std::string(e.what()));
        return createErrorResponse(http::status::bad_request, 
                                 "Failed to parse test message: " + std::string(e.what()));
    }
}

http::response<http::string_body>
WebSocketFilterManager::handleGetFilterStatistics() {
    WS_FILTER_LOG_DEBUG("Getting detailed filter statistics");
    
    try {
        FilterStatistics stats = getFilterStatistics();
        std::string json = filterStatisticsToJson(stats);
        return createSuccessResponse(json);
        
    } catch (const std::exception& e) {
        WS_FILTER_LOG_ERROR("Failed to get filter statistics: " + std::string(e.what()));
        return createErrorResponse(http::status::internal_server_error, 
                                 "Failed to retrieve filter statistics");
    }
}

void WebSocketFilterManager::saveConnectionPreferences(const std::string& connectionId, 
                                                      const ConnectionFilters& filters) {
    std::lock_guard<std::mutex> lock(preferencesLock_);
    storedPreferences_[connectionId] = filters;
    WS_FILTER_LOG_DEBUG("Preferences saved for connection: " + connectionId);
}

bool WebSocketFilterManager::loadConnectionPreferences(const std::string& connectionId, 
                                                      ConnectionFilters& filters) {
    std::lock_guard<std::mutex> lock(preferencesLock_);
    auto it = storedPreferences_.find(connectionId);
    if (it != storedPreferences_.end()) {
        filters = it->second;
        WS_FILTER_LOG_DEBUG("Preferences loaded for connection: " + connectionId);
        return true;
    }
    return false;
}

void WebSocketFilterManager::clearStoredPreferences(const std::string& connectionId) {
    std::lock_guard<std::mutex> lock(preferencesLock_);
    storedPreferences_.erase(connectionId);
    WS_FILTER_LOG_DEBUG("Preferences cleared for connection: " + connectionId);
}

void WebSocketFilterManager::applyFiltersToMultipleConnections(
    const std::vector<std::string>& connectionIds, 
    const ConnectionFilters& filters) {
    
    WS_FILTER_LOG_INFO("Applying filters to " + std::to_string(connectionIds.size()) + " connections");
    
    for (const auto& connectionId : connectionIds) {
        if (validateConnectionExists(connectionId)) {
            wsManager_->setConnectionFilters(connectionId, filters);
            saveConnectionPreferences(connectionId, filters);
        } else {
            WS_FILTER_LOG_WARN("Skipping non-existent connection: " + connectionId);
        }
    }
}

void WebSocketFilterManager::clearFiltersFromMultipleConnections(
    const std::vector<std::string>& connectionIds) {
    
    WS_FILTER_LOG_INFO("Clearing filters from " + std::to_string(connectionIds.size()) + " connections");
    
    for (const auto& connectionId : connectionIds) {
        if (validateConnectionExists(connectionId)) {
            wsManager_->clearConnectionFilters(connectionId);
            clearStoredPreferences(connectionId);
        } else {
            WS_FILTER_LOG_WARN("Skipping non-existent connection: " + connectionId);
        }
    }
}

std::vector<std::string> WebSocketFilterManager::findConnectionsMatchingFilter(
    std::function<bool(const ConnectionFilters&)> predicate) const {
    
    std::vector<std::string> connectionIds = wsManager_->getConnectionIds();
    std::vector<std::string> matchingConnections;
    
    for (const auto& connectionId : connectionIds) {
        ConnectionFilters filters = wsManager_->getConnectionFilters(connectionId);
        if (predicate(filters)) {
            matchingConnections.push_back(connectionId);
        }
    }
    
    return matchingConnections;
}

void WebSocketFilterManager::broadcastToFilteredConnections(
    const WebSocketMessage& message,
    std::function<bool(const ConnectionFilters&)> customFilter) {
    
    wsManager_->sendToMatchingConnections(message, 
        [customFilter](const ConnectionFilters& filters, const WebSocketMessage& msg) -> bool {
            return customFilter(filters) && filters.shouldReceiveMessage(msg);
        });
}

void WebSocketFilterManager::saveFilterTemplate(const std::string& templateName, 
                                               const ConnectionFilters& filters) {
    std::lock_guard<std::mutex> lock(templatesLock_);
    filterTemplates_[templateName] = filters;
    WS_FILTER_LOG_INFO("Filter template saved: " + templateName);
}

bool WebSocketFilterManager::loadFilterTemplate(const std::string& templateName, 
                                               ConnectionFilters& filters) {
    std::lock_guard<std::mutex> lock(templatesLock_);
    auto it = filterTemplates_.find(templateName);
    if (it != filterTemplates_.end()) {
        filters = it->second;
        WS_FILTER_LOG_DEBUG("Filter template loaded: " + templateName);
        return true;
    }
    return false;
}

void WebSocketFilterManager::applyFilterTemplate(const std::string& connectionId, 
                                                const std::string& templateName) {
    ConnectionFilters filters;
    if (loadFilterTemplate(templateName, filters)) {
        if (validateConnectionExists(connectionId)) {
            wsManager_->setConnectionFilters(connectionId, filters);
            saveConnectionPreferences(connectionId, filters);
            WS_FILTER_LOG_INFO("Filter template '" + templateName + "' applied to connection: " + connectionId);
        }
    } else {
        WS_FILTER_LOG_WARN("Filter template not found: " + templateName);
    }
}

std::vector<std::string> WebSocketFilterManager::getAvailableFilterTemplates() const {
    std::lock_guard<std::mutex> lock(templatesLock_);
    std::vector<std::string> templateNames;
    templateNames.reserve(filterTemplates_.size());
    
    for (const auto& [name, _] : filterTemplates_) {
        templateNames.push_back(name);
    }
    
    return templateNames;
}

WebSocketFilterManager::FilterStatistics WebSocketFilterManager::getFilterStatistics() const {
    FilterStatistics stats;
    
    stats.totalConnections = wsManager_->getConnectionCount();
    stats.filteredConnections = wsManager_->getFilteredConnectionCount();
    stats.unfilteredConnections = wsManager_->getUnfilteredConnectionCount();
    
    std::vector<std::string> connectionIds = wsManager_->getConnectionIds();
    size_t totalFilters = 0;
    
    for (const auto& connectionId : connectionIds) {
        ConnectionFilters filters = wsManager_->getConnectionFilters(connectionId);
        
        totalFilters += filters.getTotalFilterCount();
        
        // Count job filters
        for (const auto& jobId : filters.jobIds) {
            stats.jobFilterCounts[jobId]++;
        }
        
        // Count message type filters
        for (const auto& messageType : filters.messageTypes) {
            stats.messageTypeFilterCounts[messageType]++;
        }
        
        // Count log level filters
        for (const auto& logLevel : filters.logLevels) {
            stats.logLevelFilterCounts[logLevel]++;
        }
    }
    
    stats.averageFiltersPerConnection = stats.totalConnections > 0 ? 
        static_cast<double>(totalFilters) / stats.totalConnections : 0.0;
    
    return stats;
}

// Private utility methods implementation

http::response<http::string_body>
WebSocketFilterManager::createSuccessResponse(const std::string& data) {
    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::content_type, "application/json");
    res.body() = data;
    res.prepare_payload();
    return res;
}

http::response<http::string_body>
WebSocketFilterManager::createErrorResponse(http::status status, const std::string& message) {
    http::response<http::string_body> res{status, 11};
    res.set(http::field::content_type, "application/json");
    
    std::ostringstream json;
    json << R"({"error":")" << escapeJsonString(message) << R"("})";
    res.body() = json.str();
    res.prepare_payload();
    return res;
}

ConnectionFilters WebSocketFilterManager::parseConnectionFiltersFromJson(const std::string& json) {
    // This is a simplified JSON parser for the ConnectionFilters
    // In a production system, you'd use a proper JSON library
    return ConnectionFilters::fromJson(json);
}

WebSocketMessage WebSocketFilterManager::parseWebSocketMessageFromJson(const std::string& json) {
    // This is a simplified JSON parser for the WebSocketMessage
    // In a production system, you'd use a proper JSON library
    return WebSocketMessage::fromJson(json);
}

std::string WebSocketFilterManager::connectionFiltersToJson(const ConnectionFilters& filters) {
    return filters.toJson();
}

std::string WebSocketFilterManager::filterStatisticsToJson(const FilterStatistics& stats) const {
    std::ostringstream json;
    json << "{"
         << R"("totalConnections":)" << stats.totalConnections << ","
         << R"("filteredConnections":)" << stats.filteredConnections << ","
         << R"("unfilteredConnections":)" << stats.unfilteredConnections << ","
         << R"("averageFiltersPerConnection":)" << std::fixed << std::setprecision(2) 
         << stats.averageFiltersPerConnection << ","
         << R"("jobFilterCounts":{)";
    
    bool first = true;
    for (const auto& [jobId, count] : stats.jobFilterCounts) {
        if (!first) json << ",";
        json << R"(")" << escapeJsonString(jobId) << R"(":)" << count;
        first = false;
    }
    
    json << R"(},"messageTypeFilterCounts":{)";
    first = true;
    for (const auto& [messageType, count] : stats.messageTypeFilterCounts) {
        if (!first) json << ",";
        json << R"(")" << messageTypeToString(messageType) << R"(":)" << count;
        first = false;
    }
    
    json << R"(},"logLevelFilterCounts":{)";
    first = true;
    for (const auto& [logLevel, count] : stats.logLevelFilterCounts) {
        if (!first) json << ",";
        json << R"(")" << escapeJsonString(logLevel) << R"(":)" << count;
        first = false;
    }
    
    json << "}}";
    return json.str();
}

bool WebSocketFilterManager::validateConnectionExists(const std::string& connectionId) {
    std::vector<std::string> connectionIds = wsManager_->getConnectionIds();
    return std::find(connectionIds.begin(), connectionIds.end(), connectionId) != connectionIds.end();
}

bool WebSocketFilterManager::validateFilterData(const ConnectionFilters& filters, 
                                               std::string& errorMessage) {
    if (!filters.isValid()) {
        errorMessage = filters.getValidationErrors();
        return false;
    }
    return true;
}

void WebSocketFilterManager::initializeDefaultTemplates() {
    std::lock_guard<std::mutex> lock(templatesLock_);
    
    // Error-only template
    ConnectionFilters errorOnlyTemplate;
    errorOnlyTemplate.logLevels.push_back("ERROR");
    errorOnlyTemplate.logLevels.push_back("FATAL");
    filterTemplates_["error-only"] = errorOnlyTemplate;
    
    // Job status updates template
    ConnectionFilters jobStatusTemplate;
    jobStatusTemplate.messageTypes.push_back(MessageType::JOB_STATUS_UPDATE);
    jobStatusTemplate.messageTypes.push_back(MessageType::JOB_PROGRESS_UPDATE);
    filterTemplates_["job-status"] = jobStatusTemplate;
    
    // System notifications template
    ConnectionFilters systemNotificationsTemplate;
    systemNotificationsTemplate.messageTypes.push_back(MessageType::SYSTEM_NOTIFICATION);
    systemNotificationsTemplate.messageTypes.push_back(MessageType::ERROR_MESSAGE);
    systemNotificationsTemplate.includeSystemNotifications = true;
    filterTemplates_["system-notifications"] = systemNotificationsTemplate;
    
    // Verbose logging template
    ConnectionFilters verboseTemplate;
    verboseTemplate.logLevels.push_back("DEBUG");
    verboseTemplate.logLevels.push_back("INFO");
    verboseTemplate.logLevels.push_back("WARN");
    verboseTemplate.logLevels.push_back("ERROR");
    filterTemplates_["verbose"] = verboseTemplate;
    
    WS_FILTER_LOG_INFO("Default filter templates initialized");
}
