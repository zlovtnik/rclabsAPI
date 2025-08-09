#include "notification_service.hpp"
#include "config_manager.hpp"
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <chrono>

// Only include curl and json if they are available
#ifdef CURL_FOUND
#include <curl/curl.h>
#endif

#ifdef JSONCPP_FOUND
#include <json/json.h>
#endif

// ===== NotificationMessage Implementation =====

std::string NotificationMessage::generateId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "notif_";
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    ss << std::hex << time_t << "_";
    
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

std::string NotificationMessage::toJson() const {
#ifdef JSONCPP_FOUND
    Json::Value json;
    json["id"] = id;
    json["type"] = static_cast<int>(type);
    json["priority"] = static_cast<int>(priority);
    json["jobId"] = jobId;
    json["subject"] = subject;
    json["message"] = message;
    
    // Convert timestamp to ISO 8601 string
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    json["timestamp"] = ss.str();
    
    json["retryCount"] = retryCount;
    json["maxRetries"] = maxRetries;
    
    Json::Value methodsArray(Json::arrayValue);
    for (auto method : methods) {
        methodsArray.append(static_cast<int>(method));
    }
    json["methods"] = methodsArray;
    
    Json::Value metadataObj;
    for (const auto& [key, value] : metadata) {
        metadataObj[key] = value;
    }
    json["metadata"] = metadataObj;
    
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, json);
#else
    // Simple JSON-like string without json library
    std::stringstream ss;
    ss << "{";
    ss << "\"id\":\"" << id << "\",";
    ss << "\"type\":" << static_cast<int>(type) << ",";
    ss << "\"priority\":" << static_cast<int>(priority) << ",";
    ss << "\"jobId\":\"" << jobId << "\",";
    ss << "\"subject\":\"" << subject << "\",";
    ss << "\"message\":\"" << message << "\",";
    ss << "\"retryCount\":" << retryCount << ",";
    ss << "\"maxRetries\":" << maxRetries;
    ss << "}";
    return ss.str();
#endif
}

NotificationMessage NotificationMessage::fromJson(const std::string& jsonStr) {
#ifdef JSONCPP_FOUND
    Json::Value json;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(jsonStr);
    
    if (!Json::parseFromStream(builder, stream, &json, &errors)) {
        throw std::runtime_error("Failed to parse notification JSON: " + errors);
    }
    
    NotificationMessage msg;
    msg.id = json["id"].asString();
    msg.type = static_cast<NotificationType>(json["type"].asInt());
    msg.priority = static_cast<NotificationPriority>(json["priority"].asInt());
    msg.jobId = json["jobId"].asString();
    msg.subject = json["subject"].asString();
    msg.message = json["message"].asString();
    msg.retryCount = json["retryCount"].asInt();
    msg.maxRetries = json["maxRetries"].asInt();
    
    // Parse methods
    for (const auto& method : json["methods"]) {
        msg.methods.push_back(static_cast<NotificationMethod>(method.asInt()));
    }
    
    // Parse metadata
    for (const auto& key : json["metadata"].getMemberNames()) {
        msg.metadata[key] = json["metadata"][key].asString();
    }
    
    return msg;
#else
    // Simple parsing for basic fields (not a complete JSON parser)
    NotificationMessage msg;
    msg.id = "simple_notification";
    msg.type = NotificationType::SYSTEM_ERROR;
    msg.priority = NotificationPriority::MEDIUM;
    msg.subject = "Notification";
    msg.message = "JSON parsing not available";
    msg.timestamp = std::chrono::system_clock::now();
    msg.retryCount = 0;
    msg.maxRetries = 3;
    return msg;
#endif
}

bool NotificationMessage::shouldRetry() const {
    return retryCount < maxRetries;
}

std::chrono::milliseconds NotificationMessage::getRetryDelay() const {
    // Exponential backoff: base_delay * 2^retry_count (capped at max)
    constexpr int BASE_DELAY_MS = 5000; // 5 seconds
    constexpr int MAX_DELAY_MS = 300000; // 5 minutes
    constexpr int MAX_RETRY_EXPONENT = 6; // Cap at 2^6 = 64
    
    int delay = BASE_DELAY_MS * (1 << std::min(retryCount, MAX_RETRY_EXPONENT));
    delay = std::min(delay, MAX_DELAY_MS);
    
    return std::chrono::milliseconds(delay);
}

void NotificationMessage::incrementRetry() {
    retryCount++;
    scheduledFor = std::chrono::system_clock::now() + getRetryDelay();
}

// ===== ResourceAlert Implementation =====

std::string ResourceAlert::toJson() const {
#ifdef JSONCPP_FOUND
    Json::Value json;
    json["type"] = static_cast<int>(type);
    json["description"] = description;
    json["currentValue"] = currentValue;
    json["thresholdValue"] = thresholdValue;
    json["unit"] = unit;
    
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    json["timestamp"] = ss.str();
    
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, json);
#else
    std::stringstream ss;
    ss << "{";
    ss << "\"type\":" << static_cast<int>(type) << ",";
    ss << "\"description\":\"" << description << "\",";
    ss << "\"currentValue\":" << currentValue << ",";
    ss << "\"thresholdValue\":" << thresholdValue << ",";
    ss << "\"unit\":\"" << unit << "\"";
    ss << "}";
    return ss.str();
#endif
}

ResourceAlert ResourceAlert::fromJson(const std::string& jsonStr) {
#ifdef JSONCPP_FOUND
    Json::Value json;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(jsonStr);
    
    if (!Json::parseFromStream(builder, stream, &json, &errors)) {
        throw std::runtime_error("Failed to parse resource alert JSON: " + errors);
    }
    
    ResourceAlert alert;
    alert.type = static_cast<ResourceAlertType>(json["type"].asInt());
    alert.description = json["description"].asString();
    alert.currentValue = json["currentValue"].asDouble();
    alert.thresholdValue = json["thresholdValue"].asDouble();
    alert.unit = json["unit"].asString();
    alert.timestamp = std::chrono::system_clock::now(); // Use current time for simplicity
    
    return alert;
#else
    ResourceAlert alert;
    alert.type = ResourceAlertType::HIGH_MEMORY_USAGE;
    alert.description = "Resource alert";
    alert.currentValue = 0.9;
    alert.thresholdValue = 0.8;
    alert.unit = "percentage";
    alert.timestamp = std::chrono::system_clock::now();
    return alert;
#endif
}

// ===== NotificationConfig Implementation =====

NotificationConfig NotificationConfig::fromConfig(const ConfigManager& config) {
    NotificationConfig notifConfig;
    
    notifConfig.enabled = config.getBool("monitoring.notifications.enabled", true);
    notifConfig.jobFailureAlerts = config.getBool("monitoring.notifications.job_failure_alerts", true);
    notifConfig.timeoutWarnings = config.getBool("monitoring.notifications.timeout_warnings", true);
    notifConfig.resourceAlerts = config.getBool("monitoring.notifications.resource_alerts", true);
    notifConfig.maxRetryAttempts = config.getInt("monitoring.notifications.retry_attempts", 3);
    notifConfig.baseRetryDelayMs = config.getInt("monitoring.notifications.retry_delay", 5000);
    notifConfig.timeoutWarningThresholdMinutes = config.getInt("monitoring.job_tracking.timeout_warning_threshold", 25);
    
    // Resource thresholds
    notifConfig.memoryUsageThreshold = config.getDouble("monitoring.notifications.memory_threshold", 0.85);
    notifConfig.cpuUsageThreshold = config.getDouble("monitoring.notifications.cpu_threshold", 0.90);
    notifConfig.diskSpaceThreshold = config.getDouble("monitoring.notifications.disk_threshold", 0.90);
    
    // Email configuration
    notifConfig.emailSmtpServer = config.getString("monitoring.notifications.email.smtp_server");
    notifConfig.emailSmtpPort = config.getInt("monitoring.notifications.email.smtp_port", 587);
    notifConfig.emailUsername = config.getString("monitoring.notifications.email.username");
    notifConfig.emailPassword = config.getString("monitoring.notifications.email.password");
    
    // Webhook configuration
    notifConfig.webhookUrl = config.getString("monitoring.notifications.webhook.url");
    notifConfig.webhookSecret = config.getString("monitoring.notifications.webhook.secret");
    notifConfig.webhookTimeoutMs = config.getInt("monitoring.notifications.webhook.timeout", 30000);
    
    // Default methods
    notifConfig.defaultMethods = {NotificationMethod::LOG_ONLY};
    
    // Set up priority-based methods
    notifConfig.priorityMethods[NotificationPriority::LOW] = {NotificationMethod::LOG_ONLY};
    notifConfig.priorityMethods[NotificationPriority::MEDIUM] = {NotificationMethod::LOG_ONLY, NotificationMethod::WEBHOOK};
    notifConfig.priorityMethods[NotificationPriority::HIGH] = {NotificationMethod::LOG_ONLY, NotificationMethod::EMAIL, NotificationMethod::WEBHOOK};
    notifConfig.priorityMethods[NotificationPriority::CRITICAL] = {NotificationMethod::LOG_ONLY, NotificationMethod::EMAIL, NotificationMethod::WEBHOOK};
    
    return notifConfig;
}

bool NotificationConfig::isValid() const {
    if (!enabled) return true; // If disabled, config is valid
    
    // Check if at least one delivery method is properly configured
    bool hasValidMethod = false;
    
    // Email validation
    if (!emailSmtpServer.empty() && !emailUsername.empty() && !emailPassword.empty()) {
        hasValidMethod = true;
    }
    
    // Webhook validation
    if (!webhookUrl.empty()) {
        hasValidMethod = true;
    }
    
    // Log delivery is always available
    hasValidMethod = true;
    
    return hasValidMethod && maxRetryAttempts >= 0 && baseRetryDelayMs > 0;
}

// ===== LogNotificationDelivery Implementation =====

LogNotificationDelivery::LogNotificationDelivery(Logger* logger)
    : logger_(logger) {}

bool LogNotificationDelivery::deliver(const NotificationMessage& message) {
    if (!logger_) return false;
    
    std::stringstream ss;
    ss << "[NOTIFICATION] " << message.subject << " - " << message.message;
    if (!message.jobId.empty()) {
        ss << " [Job: " << message.jobId << "]";
    }
    
    // Log based on priority
    switch (message.priority) {
        case NotificationPriority::CRITICAL:
            logger_->error("NotificationService", ss.str());
            break;
        case NotificationPriority::HIGH:
            logger_->warn("NotificationService", ss.str());
            break;
        case NotificationPriority::MEDIUM:
            logger_->info("NotificationService", ss.str());
            break;
        case NotificationPriority::LOW:
            logger_->debug("NotificationService", ss.str());
            break;
    }
    
    return true;
}

// ===== EmailNotificationDelivery Implementation =====

EmailNotificationDelivery::EmailNotificationDelivery(const NotificationConfig& config)
    : config_(config) {}

bool EmailNotificationDelivery::isConfigured() const {
    return !config_.emailSmtpServer.empty() && 
           !config_.emailUsername.empty() && 
           !config_.emailPassword.empty() &&
           !config_.emailRecipients.empty();
}

bool EmailNotificationDelivery::deliver(const NotificationMessage& message) {
    if (!isConfigured()) return false;
    
    // For now, we'll log the email content instead of actually sending
    // In production, this would integrate with an SMTP library
    std::stringstream ss;
    ss << "EMAIL NOTIFICATION:\n";
    ss << "To: ";
    for (size_t i = 0; i < config_.emailRecipients.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << config_.emailRecipients[i];
    }
    ss << "\nSubject: " << message.subject;
    ss << "\nBody: " << message.message;
    if (!message.jobId.empty()) {
        ss << "\nJob ID: " << message.jobId;
    }
    
    // Log the email content for now
    // TODO: Implement actual SMTP sending
    std::cout << ss.str() << std::endl;
    
    return true;
}

bool EmailNotificationDelivery::sendEmail(const std::string& to, const std::string& subject, const std::string& body) {
    // Placeholder for actual SMTP implementation
    // Would use libraries like libcurl with SMTP or dedicated email libraries
    return true;
}

// ===== WebhookNotificationDelivery Implementation =====

WebhookNotificationDelivery::WebhookNotificationDelivery(const NotificationConfig& config)
    : config_(config) {}

bool WebhookNotificationDelivery::isConfigured() const {
    return !config_.webhookUrl.empty();
}

#ifdef CURL_FOUND
// Callback function for libcurl to capture response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}
#endif

bool WebhookNotificationDelivery::deliver(const NotificationMessage& message) {
    if (!isConfigured()) return false;
    
#ifdef JSONCPP_FOUND
    // Create JSON payload
    Json::Value payload;
    payload["id"] = message.id;
    payload["type"] = static_cast<int>(message.type);
    payload["priority"] = static_cast<int>(message.priority);
    payload["subject"] = message.subject;
    payload["message"] = message.message;
    payload["jobId"] = message.jobId;
    payload["timestamp"] = message.toJson(); // Include full message data
    
    Json::StreamWriterBuilder builder;
    std::string jsonPayload = Json::writeString(builder, payload);
#else
    // Simple payload without JSON library
    std::string jsonPayload = "{\"subject\":\"" + message.subject + "\",\"message\":\"" + message.message + "\"}";
#endif
    
    return sendWebhook(jsonPayload);
}

bool WebhookNotificationDelivery::sendWebhook(const std::string& payload) {
#ifdef CURL_FOUND
    CURL* curl;
    CURLcode res;
    std::string response;
    
    curl = curl_easy_init();
    if (!curl) return false;
    
    // Set up headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    if (!config_.webhookSecret.empty()) {
        std::string authHeader = "Authorization: Bearer " + config_.webhookSecret;
        headers = curl_slist_append(headers, authHeader.c_str());
    }
    
    // Configure curl
    curl_easy_setopt(curl, CURLOPT_URL, config_.webhookUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.webhookTimeoutMs / 1000);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ETLPlus-NotificationService/1.0");
    
    // Perform the request
    res = curl_easy_perform(curl);
    
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    // Consider 2xx status codes as success
    return (res == CURLE_OK && httpCode >= 200 && httpCode < 300);
#else
    // Mock webhook delivery when curl is not available
    std::cout << "[WEBHOOK MOCK] Would send to " << config_.webhookUrl << ": " << payload << std::endl;
    return true;
#endif
}

// ===== NotificationServiceImpl Implementation =====

NotificationServiceImpl::NotificationServiceImpl()
    : NotificationServiceImpl(&Logger::getInstance()) {}

NotificationServiceImpl::NotificationServiceImpl(Logger* logger)
    : logger_(logger), running_(false), testMode_(false), 
      processedCount_(0), failedCount_(0) {
    
    if (!logger_) {
        logger_ = &Logger::getInstance();
    }
}

NotificationServiceImpl::~NotificationServiceImpl() {
    stop();
}

void NotificationServiceImpl::configure(const NotificationConfig& config) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    config_ = config;
    setupDeliveryMethods();
    
    if (logger_) {
        logger_->info("NotificationService", "Configuration updated - enabled: " + 
                     std::string(config_.enabled ? "true" : "false"));
    }
}

void NotificationServiceImpl::start() {
    if (running_.load()) return;
    
    running_.store(true);
    processingThread_ = std::thread(&NotificationServiceImpl::processNotifications, this);
    
    if (logger_) {
        logger_->info("NotificationService", "Notification service started");
    }
}

void NotificationServiceImpl::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    queueCondition_.notify_all();
    
    if (processingThread_.joinable()) {
        processingThread_.join();
    }
    
    if (logger_) {
        logger_->info("NotificationService", "Notification service stopped");
    }
}

bool NotificationServiceImpl::isRunning() const {
    return running_.load();
}

void NotificationServiceImpl::sendJobFailureAlert(const std::string& jobId, const std::string& error) {
    if (!config_.enabled || !config_.jobFailureAlerts) return;
    
    auto notification = createJobFailureNotification(jobId, error);
    queueNotification(notification);
}

void NotificationServiceImpl::sendJobTimeoutWarning(const std::string& jobId, int executionTimeMinutes) {
    if (!config_.enabled || !config_.timeoutWarnings) return;
    
    auto notification = createTimeoutWarningNotification(jobId, executionTimeMinutes);
    queueNotification(notification);
}

void NotificationServiceImpl::sendResourceAlert(const ResourceAlert& alert) {
    if (!config_.enabled || !config_.resourceAlerts) return;
    
    // Check if we should send this alert (prevent spam)
    if (!shouldSendResourceAlert(alert.type)) {
        return;
    }
    
    auto notification = createResourceAlertNotification(alert);
    queueNotification(notification);
    recordResourceAlert(alert.type);
}

void NotificationServiceImpl::sendSystemErrorAlert(const std::string& component, const std::string& error) {
    if (!config_.enabled) return;
    
    auto notification = createSystemErrorNotification(component, error);
    queueNotification(notification);
}

void NotificationServiceImpl::sendCustomNotification(const NotificationMessage& message) {
    if (!config_.enabled) return;
    
    queueNotification(message);
}

void NotificationServiceImpl::queueNotification(const NotificationMessage& message) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    if (notificationQueue_.size() >= config_.queueMaxSize) {
        if (logger_) {
            logger_->warn("NotificationService", "Notification queue full, dropping message: " + message.id);
        }
        return;
    }
    
    notificationQueue_.push(message);
    queueCondition_.notify_one();
    
    if (logger_) {
        logger_->debug("NotificationService", "Queued notification: " + message.id + " (queue size: " + 
                      std::to_string(notificationQueue_.size()) + ")");
    }
}

size_t NotificationServiceImpl::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return notificationQueue_.size() + retryQueue_.size();
}

size_t NotificationServiceImpl::getProcessedCount() const {
    return processedCount_.load();
}

size_t NotificationServiceImpl::getFailedCount() const {
    return failedCount_.load();
}

void NotificationServiceImpl::checkMemoryUsage(double currentUsage) {
    if (currentUsage > config_.memoryUsageThreshold) {
        ResourceAlert alert;
        alert.type = ResourceAlertType::HIGH_MEMORY_USAGE;
        alert.currentValue = currentUsage;
        alert.thresholdValue = config_.memoryUsageThreshold;
        alert.unit = "percentage";
        alert.description = "Memory usage is above threshold";
        alert.timestamp = std::chrono::system_clock::now();
        
        sendResourceAlert(alert);
    }
}

void NotificationServiceImpl::checkCpuUsage(double currentUsage) {
    if (currentUsage > config_.cpuUsageThreshold) {
        ResourceAlert alert;
        alert.type = ResourceAlertType::HIGH_CPU_USAGE;
        alert.currentValue = currentUsage;
        alert.thresholdValue = config_.cpuUsageThreshold;
        alert.unit = "percentage";
        alert.description = "CPU usage is above threshold";
        alert.timestamp = std::chrono::system_clock::now();
        
        sendResourceAlert(alert);
    }
}

void NotificationServiceImpl::checkDiskSpace(double currentUsage) {
    if (currentUsage > config_.diskSpaceThreshold) {
        ResourceAlert alert;
        alert.type = ResourceAlertType::DISK_SPACE_LOW;
        alert.currentValue = currentUsage;
        alert.thresholdValue = config_.diskSpaceThreshold;
        alert.unit = "percentage";
        alert.description = "Disk space usage is above threshold";
        alert.timestamp = std::chrono::system_clock::now();
        
        sendResourceAlert(alert);
    }
}

void NotificationServiceImpl::checkConnectionLimit(int currentConnections, int maxConnections) {
    double usage = static_cast<double>(currentConnections) / maxConnections;
    if (usage > (config_.connectionLimitThreshold / 100.0)) {
        ResourceAlert alert;
        alert.type = ResourceAlertType::CONNECTION_LIMIT_REACHED;
        alert.currentValue = currentConnections;
        alert.thresholdValue = maxConnections * (config_.connectionLimitThreshold / 100.0);
        alert.unit = "connections";
        alert.description = "Connection limit is being approached";
        alert.timestamp = std::chrono::system_clock::now();
        
        sendResourceAlert(alert);
    }
}

std::vector<NotificationMessage> NotificationServiceImpl::getRecentNotifications(size_t limit) const {
    std::lock_guard<std::mutex> lock(recentMutex_);
    
    size_t start = 0;
    if (recentNotifications_.size() > limit) {
        start = recentNotifications_.size() - limit;
    }
    
    return std::vector<NotificationMessage>(
        recentNotifications_.begin() + start, 
        recentNotifications_.end()
    );
}

void NotificationServiceImpl::clearQueue() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!notificationQueue_.empty()) {
        notificationQueue_.pop();
    }
    while (!retryQueue_.empty()) {
        retryQueue_.pop();
    }
}

void NotificationServiceImpl::setTestMode(bool enabled) {
    testMode_.store(enabled);
    if (logger_) {
        logger_->info("NotificationService", "Test mode " + std::string(enabled ? "enabled" : "disabled"));
    }
}

// Private implementation methods

void NotificationServiceImpl::processNotifications() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        
        // Wait for notifications or stop signal
        queueCondition_.wait(lock, [this] { 
            return !running_.load() || !notificationQueue_.empty() || !retryQueue_.empty(); 
        });
        
        if (!running_.load()) break;
        
        // Process regular queue
        std::queue<NotificationMessage> currentQueue;
        currentQueue.swap(notificationQueue_);
        
        // Process retry queue
        std::queue<NotificationMessage> currentRetryQueue;
        auto now = std::chrono::system_clock::now();
        while (!retryQueue_.empty()) {
            auto& msg = retryQueue_.front();
            if (msg.scheduledFor <= now) {
                currentRetryQueue.push(msg);
                retryQueue_.pop();
            } else {
                break; // Retry queue is time-ordered
            }
        }
        
        lock.unlock();
        
        // Process notifications outside of lock
        while (!currentQueue.empty()) {
            auto message = currentQueue.front();
            currentQueue.pop();
            
            if (deliverNotification(message)) {
                processedCount_++;
                addToRecentNotifications(message);
            } else {
                failedCount_++;
                scheduleRetry(message);
            }
        }
        
        while (!currentRetryQueue.empty()) {
            auto message = currentRetryQueue.front();
            currentRetryQueue.pop();
            
            if (deliverNotification(message)) {
                processedCount_++;
                addToRecentNotifications(message);
            } else {
                failedCount_++;
                scheduleRetry(message);
            }
        }
        
        // Small delay to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool NotificationServiceImpl::deliverNotification(const NotificationMessage& message) {
    if (testMode_.load()) {
        // In test mode, just log and return success
        if (logger_) {
            logger_->info("NotificationService", "[TEST MODE] Would deliver: " + message.subject);
        }
        return true;
    }
    
    auto methods = getMethodsForPriority(message.priority);
    bool anySuccess = false;
    
    for (auto method : methods) {
        for (const auto& delivery : deliveryMethods_) {
            if (delivery->getMethod() == method && delivery->isConfigured()) {
                try {
                    if (delivery->deliver(message)) {
                        anySuccess = true;
                        if (logger_) {
                            logger_->debug("NotificationService", 
                                "Successfully delivered notification " + message.id + " via " + 
                                std::to_string(static_cast<int>(method)));
                        }
                    }
                } catch (const std::exception& e) {
                    if (logger_) {
                        logger_->error("NotificationService", 
                            "Failed to deliver notification " + message.id + " via " + 
                            std::to_string(static_cast<int>(method)) + ": " + e.what());
                    }
                }
                break;
            }
        }
    }
    
    return anySuccess;
}

void NotificationServiceImpl::scheduleRetry(const NotificationMessage& message) {
    if (!message.shouldRetry()) {
        if (logger_) {
            logger_->warn("NotificationService", 
                "Notification " + message.id + " exceeded max retries (" + 
                std::to_string(message.maxRetries) + ")");
        }
        return;
    }
    
    NotificationMessage retryMessage = message;
    retryMessage.incrementRetry();
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    retryQueue_.push(retryMessage);
    
    if (logger_) {
        logger_->info("NotificationService", 
            "Scheduled retry for notification " + message.id + " (attempt " + 
            std::to_string(retryMessage.retryCount) + "/" + std::to_string(retryMessage.maxRetries) + ")");
    }
}

void NotificationServiceImpl::addToRecentNotifications(const NotificationMessage& message) {
    std::lock_guard<std::mutex> lock(recentMutex_);
    
    recentNotifications_.push_back(message);
    
    // Keep only last 1000 notifications
    if (recentNotifications_.size() > 1000) {
        recentNotifications_.erase(recentNotifications_.begin());
    }
}

bool NotificationServiceImpl::shouldSendResourceAlert(ResourceAlertType type) {
    std::lock_guard<std::mutex> lock(alertTrackingMutex_);
    
    auto it = lastAlertTime_.find(type);
    auto now = std::chrono::system_clock::now();
    
    if (it == lastAlertTime_.end()) {
        return true; // First alert of this type
    }
    
    // Don't send same alert type more than once every 5 minutes
    auto timeSinceLastAlert = std::chrono::duration_cast<std::chrono::minutes>(now - it->second);
    return timeSinceLastAlert.count() >= 5;
}

void NotificationServiceImpl::recordResourceAlert(ResourceAlertType type) {
    std::lock_guard<std::mutex> lock(alertTrackingMutex_);
    lastAlertTime_[type] = std::chrono::system_clock::now();
}

NotificationMessage NotificationServiceImpl::createJobFailureNotification(const std::string& jobId, const std::string& error) {
    NotificationMessage msg;
    msg.id = NotificationMessage::generateId();
    msg.type = NotificationType::JOB_FAILURE;
    msg.priority = NotificationPriority::HIGH;
    msg.jobId = jobId;
    msg.subject = "ETL Job Failed: " + jobId;
    msg.message = "Job " + jobId + " has failed with error: " + error;
    msg.timestamp = std::chrono::system_clock::now();
    msg.scheduledFor = msg.timestamp;
    msg.retryCount = 0;
    msg.maxRetries = config_.maxRetryAttempts;
    msg.methods = getMethodsForPriority(msg.priority);
    
    // Add metadata
    msg.metadata["job_url"] = formatJobUrl(jobId);
    msg.metadata["error_message"] = error;
    msg.metadata["alert_type"] = "job_failure";
    
    return msg;
}

NotificationMessage NotificationServiceImpl::createTimeoutWarningNotification(const std::string& jobId, int executionTimeMinutes) {
    NotificationMessage msg;
    msg.id = NotificationMessage::generateId();
    msg.type = NotificationType::JOB_TIMEOUT_WARNING;
    msg.priority = NotificationPriority::MEDIUM;
    msg.jobId = jobId;
    msg.subject = "ETL Job Timeout Warning: " + jobId;
    msg.message = "Job " + jobId + " has been running for " + formatDuration(executionTimeMinutes) + 
                  ", which exceeds the warning threshold of " + formatDuration(config_.timeoutWarningThresholdMinutes);
    msg.timestamp = std::chrono::system_clock::now();
    msg.scheduledFor = msg.timestamp;
    msg.retryCount = 0;
    msg.maxRetries = config_.maxRetryAttempts;
    msg.methods = getMethodsForPriority(msg.priority);
    
    // Add metadata
    msg.metadata["job_url"] = formatJobUrl(jobId);
    msg.metadata["execution_time_minutes"] = std::to_string(executionTimeMinutes);
    msg.metadata["threshold_minutes"] = std::to_string(config_.timeoutWarningThresholdMinutes);
    msg.metadata["alert_type"] = "timeout_warning";
    
    return msg;
}

NotificationMessage NotificationServiceImpl::createResourceAlertNotification(const ResourceAlert& alert) {
    NotificationMessage msg;
    msg.id = NotificationMessage::generateId();
    msg.type = NotificationType::RESOURCE_ALERT;
    msg.priority = NotificationPriority::HIGH;
    msg.subject = "Resource Alert: " + alert.description;
    msg.message = alert.description + ". Current value: " + std::to_string(alert.currentValue) + 
                  " " + alert.unit + ", threshold: " + std::to_string(alert.thresholdValue) + " " + alert.unit;
    msg.timestamp = alert.timestamp;
    msg.scheduledFor = msg.timestamp;
    msg.retryCount = 0;
    msg.maxRetries = config_.maxRetryAttempts;
    msg.methods = getMethodsForPriority(msg.priority);
    
    // Add metadata
    msg.metadata["resource_type"] = std::to_string(static_cast<int>(alert.type));
    msg.metadata["current_value"] = std::to_string(alert.currentValue);
    msg.metadata["threshold_value"] = std::to_string(alert.thresholdValue);
    msg.metadata["unit"] = alert.unit;
    msg.metadata["alert_type"] = "resource_alert";
    
    return msg;
}

NotificationMessage NotificationServiceImpl::createSystemErrorNotification(const std::string& component, const std::string& error) {
    NotificationMessage msg;
    msg.id = NotificationMessage::generateId();
    msg.type = NotificationType::SYSTEM_ERROR;
    msg.priority = NotificationPriority::CRITICAL;
    msg.subject = "System Error in " + component;
    msg.message = "A critical error occurred in component " + component + ": " + error;
    msg.timestamp = std::chrono::system_clock::now();
    msg.scheduledFor = msg.timestamp;
    msg.retryCount = 0;
    msg.maxRetries = config_.maxRetryAttempts;
    msg.methods = getMethodsForPriority(msg.priority);
    
    // Add metadata
    msg.metadata["component"] = component;
    msg.metadata["error_message"] = error;
    msg.metadata["alert_type"] = "system_error";
    
    return msg;
}

void NotificationServiceImpl::setupDeliveryMethods() {
    deliveryMethods_.clear();
    
    // Always add log delivery
    deliveryMethods_.push_back(std::make_unique<LogNotificationDelivery>(logger_));
    
    // Add email delivery if configured
    if (!config_.emailSmtpServer.empty()) {
        deliveryMethods_.push_back(std::make_unique<EmailNotificationDelivery>(config_));
    }
    
    // Add webhook delivery if configured
    if (!config_.webhookUrl.empty()) {
        deliveryMethods_.push_back(std::make_unique<WebhookNotificationDelivery>(config_));
    }
}

std::vector<NotificationMethod> NotificationServiceImpl::getMethodsForPriority(NotificationPriority priority) {
    auto it = config_.priorityMethods.find(priority);
    if (it != config_.priorityMethods.end()) {
        return it->second;
    }
    return config_.defaultMethods;
}

std::string NotificationServiceImpl::formatJobUrl(const std::string& jobId) {
    // This would be configured based on your web dashboard URL
    return "http://localhost:8080/jobs/" + jobId;
}

std::string NotificationServiceImpl::formatDuration(int minutes) {
    if (minutes < 60) {
        return std::to_string(minutes) + " minutes";
    } else {
        int hours = minutes / 60;
        int remainingMinutes = minutes % 60;
        return std::to_string(hours) + " hours " + std::to_string(remainingMinutes) + " minutes";
    }
}

std::string NotificationServiceImpl::getNotificationTypeString(NotificationType type) {
    switch (type) {
        case NotificationType::JOB_FAILURE: return "Job Failure";
        case NotificationType::JOB_TIMEOUT_WARNING: return "Job Timeout Warning";
        case NotificationType::RESOURCE_ALERT: return "Resource Alert";
        case NotificationType::SYSTEM_ERROR: return "System Error";
        default: return "Unknown";
    }
}

std::string NotificationServiceImpl::getPriorityString(NotificationPriority priority) {
    switch (priority) {
        case NotificationPriority::LOW: return "Low";
        case NotificationPriority::MEDIUM: return "Medium";
        case NotificationPriority::HIGH: return "High";
        case NotificationPriority::CRITICAL: return "Critical";
        default: return "Unknown";
    }
}
