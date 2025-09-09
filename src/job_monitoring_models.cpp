#include "job_monitoring_models.hpp"
#include "logger.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>

// JobMetrics implementation
std::string JobMetrics::toJson() const {
    std::ostringstream json;
    json << "{"
         << "\"recordsProcessed\":" << recordsProcessed << ","
         << "\"recordsSuccessful\":" << recordsSuccessful << ","
         << "\"recordsFailed\":" << recordsFailed << ","
         << "\"processingRate\":" << std::fixed << std::setprecision(2) << processingRate << ","
         << "\"memoryUsage\":" << memoryUsage << ","
         << "\"cpuUsage\":" << std::fixed << std::setprecision(2) << cpuUsage << ","
         << "\"executionTime\":" << executionTime.count() << ","
         
         // Extended performance metrics
         << "\"peakMemoryUsage\":" << peakMemoryUsage << ","
         << "\"peakCpuUsage\":" << std::fixed << std::setprecision(2) << peakCpuUsage << ","
         << "\"averageProcessingRate\":" << std::fixed << std::setprecision(2) << averageProcessingRate << ","
         << "\"totalBytesProcessed\":" << totalBytesProcessed << ","
         << "\"totalBytesWritten\":" << totalBytesWritten << ","
         << "\"totalBatches\":" << totalBatches << ","
         << "\"averageBatchSize\":" << std::fixed << std::setprecision(2) << averageBatchSize << ","
         
         // Error statistics
         << "\"errorRate\":" << std::fixed << std::setprecision(2) << errorRate << ","
         << "\"consecutiveErrors\":" << consecutiveErrors << ","
         << "\"timeToFirstError\":" << timeToFirstError.count() << ","
         
         // Performance indicators
         << "\"throughputMBps\":" << std::fixed << std::setprecision(2) << throughputMBps << ","
         << "\"memoryEfficiency\":" << std::fixed << std::setprecision(2) << memoryEfficiency << ","
         << "\"cpuEfficiency\":" << std::fixed << std::setprecision(2) << cpuEfficiency
         << "}";
    return json.str();
}

JobMetrics JobMetrics::fromJson(const std::string& json) {
    JobMetrics metrics;
    
    // Simple JSON parsing for the metrics fields
    // In a production system, you'd use a proper JSON library
    std::regex recordsProcessedRegex("\"recordsProcessed\"\\s*:\\s*(\\d+)");
    std::regex recordsSuccessfulRegex("\"recordsSuccessful\"\\s*:\\s*(\\d+)");
    std::regex recordsFailedRegex("\"recordsFailed\"\\s*:\\s*(\\d+)");
    std::regex processingRateRegex("\"processingRate\"\\s*:\\s*([0-9.]+)");
    std::regex memoryUsageRegex("\"memoryUsage\"\\s*:\\s*(\\d+)");
    std::regex cpuUsageRegex("\"cpuUsage\"\\s*:\\s*([0-9.]+)");
    std::regex executionTimeRegex("\"executionTime\"\\s*:\\s*(\\d+)");
    
    // Extended metrics regexes
    std::regex peakMemoryUsageRegex("\"peakMemoryUsage\"\\s*:\\s*(\\d+)");
    std::regex peakCpuUsageRegex("\"peakCpuUsage\"\\s*:\\s*([0-9.]+)");
    std::regex averageProcessingRateRegex("\"averageProcessingRate\"\\s*:\\s*([0-9.]+)");
    std::regex totalBytesProcessedRegex("\"totalBytesProcessed\"\\s*:\\s*(\\d+)");
    std::regex totalBytesWrittenRegex("\"totalBytesWritten\"\\s*:\\s*(\\d+)");
    std::regex totalBatchesRegex("\"totalBatches\"\\s*:\\s*(\\d+)");
    std::regex averageBatchSizeRegex("\"averageBatchSize\"\\s*:\\s*([0-9.]+)");
    std::regex errorRateRegex("\"errorRate\"\\s*:\\s*([0-9.]+)");
    std::regex consecutiveErrorsRegex("\"consecutiveErrors\"\\s*:\\s*(\\d+)");
    std::regex timeToFirstErrorRegex("\"timeToFirstError\"\\s*:\\s*(\\d+)");
    std::regex throughputMBpsRegex("\"throughputMBps\"\\s*:\\s*([0-9.]+)");
    std::regex memoryEfficiencyRegex("\"memoryEfficiency\"\\s*:\\s*([0-9.]+)");
    std::regex cpuEfficiencyRegex("\"cpuEfficiency\"\\s*:\\s*([0-9.]+)");
    
    std::smatch match;
    
    // Parse basic metrics
    if (std::regex_search(json, match, recordsProcessedRegex)) {
        metrics.recordsProcessed = std::stoi(match[1].str());
    }
    if (std::regex_search(json, match, recordsSuccessfulRegex)) {
        metrics.recordsSuccessful = std::stoi(match[1].str());
    }
    if (std::regex_search(json, match, recordsFailedRegex)) {
        metrics.recordsFailed = std::stoi(match[1].str());
    }
    if (std::regex_search(json, match, processingRateRegex)) {
        metrics.processingRate = std::stod(match[1].str());
    }
    if (std::regex_search(json, match, memoryUsageRegex)) {
        metrics.memoryUsage = std::stoull(match[1].str());
    }
    if (std::regex_search(json, match, cpuUsageRegex)) {
        metrics.cpuUsage = std::stod(match[1].str());
    }
    if (std::regex_search(json, match, executionTimeRegex)) {
        metrics.executionTime = std::chrono::milliseconds(std::stoll(match[1].str()));
    }
    
    // Parse extended metrics
    if (std::regex_search(json, match, peakMemoryUsageRegex)) {
        metrics.peakMemoryUsage = std::stoull(match[1].str());
    }
    if (std::regex_search(json, match, peakCpuUsageRegex)) {
        metrics.peakCpuUsage = std::stod(match[1].str());
    }
    if (std::regex_search(json, match, averageProcessingRateRegex)) {
        metrics.averageProcessingRate = std::stod(match[1].str());
    }
    if (std::regex_search(json, match, totalBytesProcessedRegex)) {
        metrics.totalBytesProcessed = std::stoull(match[1].str());
    }
    if (std::regex_search(json, match, totalBytesWrittenRegex)) {
        metrics.totalBytesWritten = std::stoull(match[1].str());
    }
    if (std::regex_search(json, match, totalBatchesRegex)) {
        metrics.totalBatches = std::stoi(match[1].str());
    }
    if (std::regex_search(json, match, averageBatchSizeRegex)) {
        metrics.averageBatchSize = std::stod(match[1].str());
    }
    if (std::regex_search(json, match, errorRateRegex)) {
        metrics.errorRate = std::stod(match[1].str());
    }
    if (std::regex_search(json, match, consecutiveErrorsRegex)) {
        metrics.consecutiveErrors = std::stoi(match[1].str());
    }
    if (std::regex_search(json, match, timeToFirstErrorRegex)) {
        metrics.timeToFirstError = std::chrono::milliseconds(std::stoll(match[1].str()));
    }
    if (std::regex_search(json, match, throughputMBpsRegex)) {
        metrics.throughputMBps = std::stod(match[1].str());
    }
    if (std::regex_search(json, match, memoryEfficiencyRegex)) {
        metrics.memoryEfficiency = std::stod(match[1].str());
    }
    if (std::regex_search(json, match, cpuEfficiencyRegex)) {
        metrics.cpuEfficiency = std::stod(match[1].str());
    }
    
    return metrics;
}

void JobMetrics::updateProcessingRate(std::chrono::milliseconds elapsed) {
    if (elapsed.count() > 0) {
        processingRate = static_cast<double>(recordsProcessed) / (elapsed.count() / 1000.0);
    }
}

void JobMetrics::updatePerformanceIndicators() {
    lastUpdateTime = std::chrono::system_clock::now();
    
    // Update processing rate based on execution time
    if (executionTime.count() > 0) {
        processingRate = static_cast<double>(recordsProcessed) / (executionTime.count() / 1000.0);
    }
    
    // Calculate error rate - protect against divide-by-zero
    if (recordsProcessed > 0) {
        errorRate = (static_cast<double>(recordsFailed) / recordsProcessed) * 100.0;
    } else {
        errorRate = 0.0; // No records processed means no error rate
    }
    
    // Calculate throughput in MB/s
    if (executionTime.count() > 0 && totalBytesProcessed > 0) {
        double seconds = executionTime.count() / 1000.0;
        double megabytes = totalBytesProcessed / (1024.0 * 1024.0);
        throughputMBps = megabytes / seconds;
    } else {
        throughputMBps = 0.0;
    }
    
    // Calculate memory efficiency (records per MB) - protect against divide-by-zero
    if (memoryUsage > 0 && recordsProcessed > 0) {
        double memoryMB = memoryUsage / (1024.0 * 1024.0);
        memoryEfficiency = recordsProcessed / memoryMB;
    } else {
        memoryEfficiency = 0.0;
    }
    
    // Calculate CPU efficiency (records per CPU percentage) - protect against divide-by-zero
    if (cpuUsage > 0.0 && recordsProcessed > 0) {
        cpuEfficiency = recordsProcessed / cpuUsage;
    } else {
        cpuEfficiency = 0.0;
    }
    
    // Update peak values
    if (memoryUsage > peakMemoryUsage) {
        peakMemoryUsage = memoryUsage;
    }
    if (cpuUsage > peakCpuUsage) {
        peakCpuUsage = cpuUsage;
    }
}

void JobMetrics::recordError() {
    consecutiveErrors++;
    recordsFailed++;

    // Record time to first error if this is the first error - ensure monotonicity
    if (recordsFailed == 1 && timeToFirstError.count() == 0) {
        timeToFirstError = executionTime;
        firstErrorTime = std::chrono::system_clock::now();
    }
}

void JobMetrics::recordBatch(int batchSize, int successful, int failed, size_t bytesProcessed) {
    totalBatches++;
    totalBytesProcessed += bytesProcessed;
    
    // Reset consecutive errors if this batch had some successes
    if (successful > 0) {
        consecutiveErrors = 0;
    }
    
    // Update average batch size
    calculateAverages();
}

void JobMetrics::calculateAverages() {
    // Protect against divide-by-zero for average batch size
    if (totalBatches > 0) {
        averageBatchSize = static_cast<double>(recordsProcessed) / totalBatches;
    } else {
        averageBatchSize = 0.0;
    }
    
    // Calculate average processing rate over job lifetime - protect against divide-by-zero
    if (executionTime.count() > 0) {
        averageProcessingRate = static_cast<double>(recordsProcessed) / (executionTime.count() / 1000.0);
    } else {
        averageProcessingRate = 0.0;
    }
}

void JobMetrics::reset() {
    recordsProcessed = 0;
    recordsSuccessful = 0;
    recordsFailed = 0;
    processingRate = 0.0;
    memoryUsage = 0;
    cpuUsage = 0.0;
    executionTime = std::chrono::milliseconds(0);
    
    // Reset extended metrics
    peakMemoryUsage = 0;
    peakCpuUsage = 0.0;
    averageProcessingRate = 0.0;
    totalBytesProcessed = 0;
    totalBytesWritten = 0;
    totalBatches = 0;
    averageBatchSize = 0.0;
    
    errorRate = 0.0;
    consecutiveErrors = 0;
    timeToFirstError = std::chrono::milliseconds(0);
    
    throughputMBps = 0.0;
    memoryEfficiency = 0.0;
    cpuEfficiency = 0.0;
    
    startTime = std::chrono::system_clock::time_point{};
    lastUpdateTime = std::chrono::system_clock::time_point{};
    firstErrorTime = std::chrono::system_clock::time_point{};
}

double JobMetrics::getOverallEfficiency() const {
    // Composite efficiency score based on multiple factors
    double efficiency = 0.0;
    int factors = 0;
    
    // Processing rate efficiency (normalized to 0-1)
    if (averageProcessingRate > 0) {
        efficiency += std::min(1.0, averageProcessingRate / 1000.0); // Assume 1000 records/sec is excellent
        factors++;
    }
    
    // Error rate efficiency (inverted - lower error rate is better)
    if (recordsProcessed > 0) {
        efficiency += (100.0 - errorRate) / 100.0;
        factors++;
    }
    
    // Memory efficiency
    if (memoryEfficiency > 0) {
        efficiency += std::min(1.0, memoryEfficiency / 1000.0); // Assume 1000 records/MB is excellent
        factors++;
    }
    
    // CPU efficiency
    if (cpuEfficiency > 0) {
        efficiency += std::min(1.0, cpuEfficiency / 100.0); // Assume 100 records per CPU% is excellent
        factors++;
    }
    
    return factors > 0 ? efficiency / factors : 0.0;
}

bool JobMetrics::isPerformingWell(const JobMetrics& baseline) const {
    // Performance comparison thresholds
    const double PERFORMANCE_THRESHOLD = 0.8; // 80% of baseline performance
    
    bool performanceGood = true;
    
    // Check processing rate
    if (baseline.averageProcessingRate > 0) {
        performanceGood &= (averageProcessingRate >= baseline.averageProcessingRate * PERFORMANCE_THRESHOLD);
    }
    
    // Check error rate (should be lower than baseline)
    if (baseline.recordsProcessed > 0) {
        performanceGood &= (errorRate <= baseline.errorRate * 1.2); // Allow 20% more errors
    }
    
    // Check memory efficiency
    if (baseline.memoryEfficiency > 0) {
        performanceGood &= (memoryEfficiency >= baseline.memoryEfficiency * PERFORMANCE_THRESHOLD);
    }
    
    // Check CPU efficiency
    if (baseline.cpuEfficiency > 0) {
        performanceGood &= (cpuEfficiency >= baseline.cpuEfficiency * PERFORMANCE_THRESHOLD);
    }
    
    return performanceGood;
}

std::string JobMetrics::getPerformanceSummary() const {
    std::ostringstream summary;
    summary << "Performance Summary: ";
    summary << recordsProcessed << " records processed";
    summary << " (" << std::fixed << std::setprecision(1) << processingRate << " rec/sec)";
    summary << ", " << std::fixed << std::setprecision(1) << errorRate << "% error rate";
    
    if (throughputMBps > 0) {
        summary << ", " << std::fixed << std::setprecision(2) << throughputMBps << " MB/s throughput";
    }
    
    if (memoryEfficiency > 0) {
        summary << ", " << std::fixed << std::setprecision(1) << memoryEfficiency << " rec/MB memory efficiency";
    }
    
    double efficiency = getOverallEfficiency();
    summary << ", Overall efficiency: " << std::fixed << std::setprecision(1) << (efficiency * 100) << "%";
    
    return summary.str();
}

// JobStatusUpdate implementation
std::string JobStatusUpdate::toJson() const {
    std::ostringstream json;
    json << "{"
         << "\"jobId\":\"" << escapeJsonString(jobId) << "\","
         << "\"status\":\"" << jobStatusToString(status) << "\","
         << "\"previousStatus\":\"" << jobStatusToString(previousStatus) << "\","
         << "\"timestamp\":\"" << formatTimestamp(timestamp) << "\","
         << "\"progressPercent\":" << progressPercent << ","
         << "\"currentStep\":\"" << escapeJsonString(currentStep) << "\","
         << "\"metrics\":" << metrics.toJson();
    
    if (errorMessage.has_value()) {
        json << ",\"errorMessage\":\"" << escapeJsonString(errorMessage.value()) << "\"";
    }
    
    json << "}";
    return json.str();
}

JobStatusUpdate JobStatusUpdate::fromJson(const std::string& json) {
    JobStatusUpdate update;
    
    std::regex jobIdRegex("\"jobId\"\\s*:\\s*\"([^\"]*)\"");
    std::regex statusRegex("\"status\"\\s*:\\s*\"([^\"]*)\"");
    std::regex previousStatusRegex("\"previousStatus\"\\s*:\\s*\"([^\"]*)\"");
    std::regex timestampRegex("\"timestamp\"\\s*:\\s*\"([^\"]*)\"");
    std::regex progressRegex("\"progressPercent\"\\s*:\\s*(\\d+)");
    std::regex currentStepRegex("\"currentStep\"\\s*:\\s*\"([^\"]*)\"");
    std::regex errorMessageRegex("\"errorMessage\"\\s*:\\s*\"([^\"]*)\"");
    
    std::smatch match;
    
    if (std::regex_search(json, match, jobIdRegex)) {
        update.jobId = match[1].str();
    }
    if (std::regex_search(json, match, statusRegex)) {
        update.status = stringToJobStatus(match[1].str());
    }
    if (std::regex_search(json, match, previousStatusRegex)) {
        update.previousStatus = stringToJobStatus(match[1].str());
    }
    if (std::regex_search(json, match, timestampRegex)) {
        update.timestamp = parseTimestamp(match[1].str());
    }
    if (std::regex_search(json, match, progressRegex)) {
        update.progressPercent = std::stoi(match[1].str());
    }
    if (std::regex_search(json, match, currentStepRegex)) {
        update.currentStep = match[1].str();
    }
    if (std::regex_search(json, match, errorMessageRegex)) {
        update.errorMessage = match[1].str();
    }
    
    // Extract metrics JSON and parse it
    std::regex metricsRegex("\"metrics\"\\s*:\\s*(\\{[^}]*\\})");
    if (std::regex_search(json, match, metricsRegex)) {
        update.metrics = JobMetrics::fromJson(match[1].str());
    }
    
    return update;
}

bool JobStatusUpdate::isStatusChange() const {
    return status != previousStatus;
}

bool JobStatusUpdate::isProgressUpdate() const {
    return !isStatusChange() && progressPercent > 0;
}

// JobMonitoringData implementation
std::string JobMonitoringData::toJson() const {
    std::ostringstream json;
    json << "{"
         << "\"jobId\":\"" << escapeJsonString(jobId) << "\","
         << "\"jobType\":\"" << getJobTypeString() << "\","
         << "\"status\":\"" << getStatusString() << "\","
         << "\"progressPercent\":" << progressPercent << ","
         << "\"currentStep\":\"" << escapeJsonString(currentStep) << "\","
         << "\"startTime\":\"" << formatTimestamp(startTime) << "\","
         << "\"createdAt\":\"" << formatTimestamp(createdAt) << "\","
         << "\"completedAt\":\"" << formatTimestamp(completedAt) << "\","
         << "\"executionTime\":" << executionTime.count() << ","
         << "\"metrics\":" << metrics.toJson() << ","
         << "\"recentLogs\":[";
    
    for (size_t i = 0; i < recentLogs.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << escapeJsonString(recentLogs[i]) << "\"";
    }
    json << "]";
    
    if (errorMessage.has_value()) {
        json << ",\"errorMessage\":\"" << escapeJsonString(errorMessage.value()) << "\"";
    }
    
    json << "}";
    return json.str();
}

JobMonitoringData JobMonitoringData::fromJson(const std::string& json) {
    JobMonitoringData data;
    
    std::regex jobIdRegex("\"jobId\"\\s*:\\s*\"([^\"]*)\"");
    std::regex jobTypeRegex("\"jobType\"\\s*:\\s*\"([^\"]*)\"");
    std::regex statusRegex("\"status\"\\s*:\\s*\"([^\"]*)\"");
    std::regex progressRegex("\"progressPercent\"\\s*:\\s*(\\d+)");
    std::regex currentStepRegex("\"currentStep\"\\s*:\\s*\"([^\"]*)\"");
    std::regex startTimeRegex("\"startTime\"\\s*:\\s*\"([^\"]*)\"");
    std::regex createdAtRegex("\"createdAt\"\\s*:\\s*\"([^\"]*)\"");
    std::regex completedAtRegex("\"completedAt\"\\s*:\\s*\"([^\"]*)\"");
    std::regex executionTimeRegex("\"executionTime\"\\s*:\\s*(\\d+)");
    std::regex errorMessageRegex("\"errorMessage\"\\s*:\\s*\"([^\"]*)\"");
    
    std::smatch match;
    
    if (std::regex_search(json, match, jobIdRegex)) {
        data.jobId = match[1].str();
    }
    if (std::regex_search(json, match, jobTypeRegex)) {
        data.jobType = stringToJobType(match[1].str());
    }
    if (std::regex_search(json, match, statusRegex)) {
        data.status = stringToJobStatus(match[1].str());
    }
    if (std::regex_search(json, match, progressRegex)) {
        data.progressPercent = std::stoi(match[1].str());
    }
    if (std::regex_search(json, match, currentStepRegex)) {
        data.currentStep = match[1].str();
    }
    if (std::regex_search(json, match, startTimeRegex)) {
        data.startTime = parseTimestamp(match[1].str());
    }
    if (std::regex_search(json, match, createdAtRegex)) {
        data.createdAt = parseTimestamp(match[1].str());
    }
    if (std::regex_search(json, match, completedAtRegex)) {
        data.completedAt = parseTimestamp(match[1].str());
    }
    if (std::regex_search(json, match, executionTimeRegex)) {
        data.executionTime = std::chrono::milliseconds(std::stoll(match[1].str()));
    }
    if (std::regex_search(json, match, errorMessageRegex)) {
        data.errorMessage = match[1].str();
    }
    
    // Extract metrics JSON and parse it
    std::regex metricsRegex("\"metrics\"\\s*:\\s*(\\{[^}]*\\})");
    if (std::regex_search(json, match, metricsRegex)) {
        data.metrics = JobMetrics::fromJson(match[1].str());
    }
    
    return data;
}

void JobMonitoringData::updateExecutionTime() {
    if (startTime != std::chrono::system_clock::time_point{}) {
        auto now = std::chrono::system_clock::now();
        executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
    }
}

bool JobMonitoringData::isActive() const {
    return status == JobStatus::RUNNING || status == JobStatus::PENDING;
}

std::string JobMonitoringData::getStatusString() const {
    return jobStatusToString(status);
}

std::string JobMonitoringData::getJobTypeString() const {
    return jobTypeToString(jobType);
}

// LogMessage implementation
std::string LogMessage::toJson() const {
    std::ostringstream json;
    json << "{"
         << "\"jobId\":\"" << escapeJsonString(jobId) << "\","
         << "\"level\":\"" << escapeJsonString(level) << "\","
         << "\"component\":\"" << escapeJsonString(component) << "\","
         << "\"message\":\"" << escapeJsonString(message) << "\","
         << "\"timestamp\":\"" << formatTimestamp(timestamp) << "\","
         << "\"context\":{";
    
    bool first = true;
    for (const auto& [key, value] : context) {
        if (!first) json << ",";
        json << "\"" << escapeJsonString(key) << "\":\"" << escapeJsonString(value) << "\"";
        first = false;
    }
    
    json << "}}";
    return json.str();
}

LogMessage LogMessage::fromJson(const std::string& json) {
    LogMessage logMsg;
    
    std::regex jobIdRegex("\"jobId\"\\s*:\\s*\"([^\"]*)\"");
    std::regex levelRegex("\"level\"\\s*:\\s*\"([^\"]*)\"");
    std::regex componentRegex("\"component\"\\s*:\\s*\"([^\"]*)\"");
    std::regex messageRegex("\"message\"\\s*:\\s*\"([^\"]*)\"");
    std::regex timestampRegex("\"timestamp\"\\s*:\\s*\"([^\"]*)\"");
    
    std::smatch match;
    
    if (std::regex_search(json, match, jobIdRegex)) {
        logMsg.jobId = match[1].str();
    }
    if (std::regex_search(json, match, levelRegex)) {
        logMsg.level = match[1].str();
    }
    if (std::regex_search(json, match, componentRegex)) {
        logMsg.component = match[1].str();
    }
    if (std::regex_search(json, match, messageRegex)) {
        logMsg.message = match[1].str();
    }
    if (std::regex_search(json, match, timestampRegex)) {
        logMsg.timestamp = parseTimestamp(match[1].str());
    }
    
    return logMsg;
}

bool LogMessage::matchesFilter(const std::string& jobIdFilter, const std::string& levelFilter) const {
    if (!jobIdFilter.empty() && jobId != jobIdFilter) {
        return false;
    }
    if (!levelFilter.empty() && level != levelFilter) {
        return false;
    }
    return true;
}
// WebSocketMessage implementation
std::string WebSocketMessage::toJson() const {
    std::ostringstream json;
    json << "{"
         << "\"type\":\"" << messageTypeToString(type) << "\","
         << "\"timestamp\":\"" << formatTimestamp(timestamp) << "\","
         << "\"data\":" << data;
    
    if (targetJobId.has_value()) {
        json << ",\"targetJobId\":\"" << escapeJsonString(targetJobId.value()) << "\"";
    }
    if (targetLevel.has_value()) {
        json << ",\"targetLevel\":\"" << escapeJsonString(targetLevel.value()) << "\"";
    }
    
    json << "}";
    return json.str();
}

WebSocketMessage WebSocketMessage::fromJson(const std::string& json) {
    WebSocketMessage message;
    
    std::regex typeRegex("\"type\"\\s*:\\s*\"([^\"]*)\"");
    std::regex timestampRegex("\"timestamp\"\\s*:\\s*\"([^\"]*)\"");
    std::regex dataRegex("\"data\"\\s*:\\s*(\\{.*\\})");
    std::regex targetJobIdRegex("\"targetJobId\"\\s*:\\s*\"([^\"]*)\"");
    std::regex targetLevelRegex("\"targetLevel\"\\s*:\\s*\"([^\"]*)\"");
    
    std::smatch match;
    
    if (std::regex_search(json, match, typeRegex)) {
        message.type = stringToMessageType(match[1].str());
    }
    if (std::regex_search(json, match, timestampRegex)) {
        message.timestamp = parseTimestamp(match[1].str());
    }
    if (std::regex_search(json, match, dataRegex)) {
        message.data = match[1].str();
    }
    if (std::regex_search(json, match, targetJobIdRegex)) {
        message.targetJobId = match[1].str();
    }
    if (std::regex_search(json, match, targetLevelRegex)) {
        message.targetLevel = match[1].str();
    }
    
    return message;
}

WebSocketMessage WebSocketMessage::createJobStatusUpdate(const JobStatusUpdate& update) {
    WebSocketMessage message;
    message.type = MessageType::JOB_STATUS_UPDATE;
    message.timestamp = std::chrono::system_clock::now();
    message.data = update.toJson();
    message.targetJobId = update.jobId;
    return message;
}

WebSocketMessage WebSocketMessage::createLogMessage(const LogMessage& logMsg) {
    WebSocketMessage message;
    message.type = MessageType::JOB_LOG_MESSAGE;
    message.timestamp = std::chrono::system_clock::now();
    message.data = logMsg.toJson();
    message.targetJobId = logMsg.jobId;
    message.targetLevel = logMsg.level;
    return message;
}

WebSocketMessage WebSocketMessage::createMetricsUpdate(const std::string& jobId, const JobMetrics& metrics) {
    WebSocketMessage message;
    message.type = MessageType::JOB_METRICS_UPDATE;
    message.timestamp = std::chrono::system_clock::now();
    
    std::ostringstream data;
    data << "{\"jobId\":\"" << escapeJsonString(jobId) << "\",\"metrics\":" << metrics.toJson() << "}";
    message.data = data.str();
    message.targetJobId = jobId;
    return message;
}

WebSocketMessage WebSocketMessage::createErrorMessage(const std::string& error) {
    WebSocketMessage message;
    message.type = MessageType::ERROR_MESSAGE;
    message.timestamp = std::chrono::system_clock::now();
    
    std::ostringstream data;
    data << "{\"error\":\"" << escapeJsonString(error) << "\"}";
    message.data = data.str();
    return message;
}

WebSocketMessage WebSocketMessage::createConnectionAck() {
    WebSocketMessage message;
    message.type = MessageType::CONNECTION_ACK;
    message.timestamp = std::chrono::system_clock::now();
    message.data = "{\"status\":\"connected\"}";
    return message;
}

// ConnectionFilters implementation
std::string ConnectionFilters::toJson() const {
    std::ostringstream json;
    json << "{"
         << "\"jobIds\":[";
    
    for (size_t i = 0; i < jobIds.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << escapeJsonString(jobIds[i]) << "\"";
    }
    
    json << "],\"logLevels\":[";
    
    for (size_t i = 0; i < logLevels.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << escapeJsonString(logLevels[i]) << "\"";
    }
    
    json << "],\"messageTypes\":[";
    
    for (size_t i = 0; i < messageTypes.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << messageTypeToString(messageTypes[i]) << "\"";
    }
    
    json << "],\"includeSystemNotifications\":" << (includeSystemNotifications ? "true" : "false")
         << "}";
    
    return json.str();
}

ConnectionFilters ConnectionFilters::fromJson(const std::string& json) {
    ConnectionFilters filters;
    
    // Parse jobIds array
    std::regex jobIdsRegex("\"jobIds\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (std::regex_search(json, match, jobIdsRegex)) {
        std::string jobIdsStr = match[1].str();
        std::regex jobIdRegex("\"([^\"]*)\"");
        std::sregex_iterator iter(jobIdsStr.begin(), jobIdsStr.end(), jobIdRegex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            filters.jobIds.push_back((*iter)[1].str());
        }
    }
    
    // Parse logLevels array
    std::regex logLevelsRegex("\"logLevels\"\\s*:\\s*\\[([^\\]]*)\\]");
    if (std::regex_search(json, match, logLevelsRegex)) {
        std::string logLevelsStr = match[1].str();
        std::regex logLevelRegex("\"([^\"]*)\"");
        std::sregex_iterator iter(logLevelsStr.begin(), logLevelsStr.end(), logLevelRegex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            filters.logLevels.push_back((*iter)[1].str());
        }
    }
    
    // Parse messageTypes array
    std::regex messageTypesRegex("\"messageTypes\"\\s*:\\s*\\[([^\\]]*)\\]");
    if (std::regex_search(json, match, messageTypesRegex)) {
        std::string messageTypesStr = match[1].str();
        std::regex messageTypeRegex("\"([^\"]*)\"");
        std::sregex_iterator iter(messageTypesStr.begin(), messageTypesStr.end(), messageTypeRegex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            filters.messageTypes.push_back(stringToMessageType((*iter)[1].str()));
        }
    }
    
    // Parse includeSystemNotifications
    std::regex systemNotificationsRegex("\"includeSystemNotifications\"\\s*:\\s*(true|false)");
    if (std::regex_search(json, match, systemNotificationsRegex)) {
        filters.includeSystemNotifications = (match[1].str() == "true");
    }
    
    return filters;
}

bool ConnectionFilters::shouldReceiveMessage(const WebSocketMessage& message) const {
    // Check message type filter
    if (!shouldReceiveMessageType(message.type)) {
        return false;
    }
    
    // Check job ID filter
    if (message.targetJobId.has_value() && !shouldReceiveJob(message.targetJobId.value())) {
        return false;
    }
    
    // Check log level filter
    if (message.targetLevel.has_value() && !shouldReceiveLogLevel(message.targetLevel.value())) {
        return false;
    }
    
    // Check system notifications
    if (message.type == MessageType::SYSTEM_NOTIFICATION && !includeSystemNotifications) {
        return false;
    }
    
    return true;
}

bool ConnectionFilters::shouldReceiveJob(const std::string& jobId) const {
    return jobIds.empty() || std::find(jobIds.begin(), jobIds.end(), jobId) != jobIds.end();
}

bool ConnectionFilters::shouldReceiveLogLevel(const std::string& level) const {
    return logLevels.empty() || std::find(logLevels.begin(), logLevels.end(), level) != logLevels.end();
}

bool ConnectionFilters::shouldReceiveMessageType(MessageType type) const {
    return messageTypes.empty() || std::find(messageTypes.begin(), messageTypes.end(), type) != messageTypes.end();
}

void ConnectionFilters::addJobId(const std::string& jobId) {
    if (std::find(jobIds.begin(), jobIds.end(), jobId) == jobIds.end()) {
        jobIds.push_back(jobId);
    }
}

void ConnectionFilters::removeJobId(const std::string& jobId) {
    auto it = std::find(jobIds.begin(), jobIds.end(), jobId);
    if (it != jobIds.end()) {
        jobIds.erase(it);
    }
}

void ConnectionFilters::addMessageType(MessageType messageType) {
    if (std::find(messageTypes.begin(), messageTypes.end(), messageType) == messageTypes.end()) {
        messageTypes.push_back(messageType);
    }
}

void ConnectionFilters::removeMessageType(MessageType messageType) {
    auto it = std::find(messageTypes.begin(), messageTypes.end(), messageType);
    if (it != messageTypes.end()) {
        messageTypes.erase(it);
    }
}

void ConnectionFilters::addLogLevel(const std::string& logLevel) {
    if (std::find(logLevels.begin(), logLevels.end(), logLevel) == logLevels.end()) {
        logLevels.push_back(logLevel);
    }
}

void ConnectionFilters::removeLogLevel(const std::string& logLevel) {
    auto it = std::find(logLevels.begin(), logLevels.end(), logLevel);
    if (it != logLevels.end()) {
        logLevels.erase(it);
    }
}

void ConnectionFilters::clear() {
    jobIds.clear();
    messageTypes.clear();
    logLevels.clear();
    includeSystemNotifications = true;
}

bool ConnectionFilters::hasFilters() const {
    return !jobIds.empty() || !messageTypes.empty() || !logLevels.empty();
}

bool ConnectionFilters::hasJobFilters() const {
    return !jobIds.empty();
}

bool ConnectionFilters::hasMessageTypeFilters() const {
    return !messageTypes.empty();
}

bool ConnectionFilters::hasLogLevelFilters() const {
    return !logLevels.empty();
}

size_t ConnectionFilters::getTotalFilterCount() const {
    return jobIds.size() + messageTypes.size() + logLevels.size();
}

bool ConnectionFilters::isValid() const {
    // Validate job IDs
    for (const auto& jobId : jobIds) {
        if (!validateJobId(jobId)) {
            return false;
        }
    }
    
    // Validate log levels
    for (const auto& level : logLevels) {
        if (!validateLogLevel(level)) {
            return false;
        }
    }
    
    // Message types are enum values, so they're inherently valid
    return true;
}

std::string ConnectionFilters::getValidationErrors() const {
    std::vector<std::string> errors;
    
    // Check job IDs
    for (const auto& jobId : jobIds) {
        if (!validateJobId(jobId)) {
            errors.push_back("Invalid job ID: " + jobId);
        }
    }
    
    // Check log levels
    for (const auto& level : logLevels) {
        if (!validateLogLevel(level)) {
            errors.push_back("Invalid log level: " + level);
        }
    }
    
    // Join errors with semicolons
    std::string result;
    for (size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) result += "; ";
        result += errors[i];
    }
    
    return result;
}

// Utility functions for message type conversion
std::string messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::JOB_STATUS_UPDATE: return "job_status_update";
        case MessageType::JOB_PROGRESS_UPDATE: return "job_progress_update";
        case MessageType::JOB_LOG_MESSAGE: return "job_log_message";
        case MessageType::JOB_METRICS_UPDATE: return "job_metrics_update";
        case MessageType::SYSTEM_NOTIFICATION: return "system_notification";
        case MessageType::CONNECTION_ACK: return "connection_ack";
        case MessageType::ERROR_MESSAGE: return "error_message";
        default: return "unknown";
    }
}

MessageType stringToMessageType(const std::string& typeStr) {
    if (typeStr == "job_status_update") return MessageType::JOB_STATUS_UPDATE;
    if (typeStr == "job_progress_update") return MessageType::JOB_PROGRESS_UPDATE;
    if (typeStr == "job_log_message") return MessageType::JOB_LOG_MESSAGE;
    if (typeStr == "job_metrics_update") return MessageType::JOB_METRICS_UPDATE;
    if (typeStr == "system_notification") return MessageType::SYSTEM_NOTIFICATION;
    if (typeStr == "connection_ack") return MessageType::CONNECTION_ACK;
    if (typeStr == "error_message") return MessageType::ERROR_MESSAGE;
    return MessageType::ERROR_MESSAGE; // Default fallback
}

// Utility functions for job status/type conversion
std::string jobStatusToString(JobStatus status) {
    switch (status) {
        case JobStatus::PENDING: return "pending";
        case JobStatus::RUNNING: return "running";
        case JobStatus::COMPLETED: return "completed";
        case JobStatus::FAILED: return "failed";
        case JobStatus::CANCELLED: return "cancelled";
        default: return "unknown";
    }
}

JobStatus stringToJobStatus(const std::string& statusStr) {
    if (statusStr == "pending") return JobStatus::PENDING;
    if (statusStr == "running") return JobStatus::RUNNING;
    if (statusStr == "completed") return JobStatus::COMPLETED;
    if (statusStr == "failed") return JobStatus::FAILED;
    if (statusStr == "cancelled") return JobStatus::CANCELLED;
    return JobStatus::PENDING; // Default fallback
}

std::string jobTypeToString(JobType type) {
    switch (type) {
        case JobType::EXTRACT: return "extract";
        case JobType::TRANSFORM: return "transform";
        case JobType::LOAD: return "load";
        case JobType::FULL_ETL: return "full_etl";
        default: return "unknown";
    }
}

JobType stringToJobType(const std::string& typeStr) {
    if (typeStr == "extract") return JobType::EXTRACT;
    if (typeStr == "transform") return JobType::TRANSFORM;
    if (typeStr == "load") return JobType::LOAD;
    if (typeStr == "full_etl") return JobType::FULL_ETL;
    return JobType::FULL_ETL; // Default fallback
}

std::string formatTimestamp(const std::chrono::system_clock::time_point& timePoint) {
    auto time_t = std::chrono::system_clock::to_time_t(timePoint);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timePoint.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

std::chrono::system_clock::time_point parseTimestamp(const std::string& timestampStr) {
    // Simple ISO 8601 parsing - in production, use a proper date/time library
    std::tm tm = {};
    std::istringstream ss(timestampStr);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    // Use timegm instead of mktime to handle UTC properly
    auto time_point = std::chrono::system_clock::from_time_t(timegm(&tm));
    
    // Parse milliseconds if present
    size_t dotPos = timestampStr.find('.');
    if (dotPos != std::string::npos) {
        size_t zPos = timestampStr.find('Z', dotPos);
        if (zPos != std::string::npos) {
            std::string msStr = timestampStr.substr(dotPos + 1, zPos - dotPos - 1);
            if (!msStr.empty()) {
                int ms = std::stoi(msStr);
                time_point += std::chrono::milliseconds(ms);
            }
        }
    }
    
    return time_point;
}

// Validation functions
bool validateJobId(const std::string& jobId) {
    if (jobId.empty() || jobId.length() > 100) {
        return false;
    }
    
    // Job ID should contain only alphanumeric characters, underscores, and hyphens
    std::regex jobIdPattern("^[a-zA-Z0-9_-]+$");
    return std::regex_match(jobId, jobIdPattern);
}

bool validateLogLevel(const std::string& level) {
    return level == "DEBUG" || level == "INFO" || level == "WARN" || 
           level == "ERROR" || level == "FATAL";
}

bool validateMessageType(const std::string& typeStr) {
    return typeStr == "job_status_update" || typeStr == "job_progress_update" ||
           typeStr == "job_log_message" || typeStr == "job_metrics_update" ||
           typeStr == "system_notification" || typeStr == "connection_ack" ||
           typeStr == "error_message";
}