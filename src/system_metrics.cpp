#include "system_metrics.hpp"
#include "logger.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/mach_host.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#elif __linux__
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <fstream>
#elif _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#endif

namespace ETLPlus::Metrics {

// SystemMetrics implementation

SystemMetrics::SystemMetrics() = default;

SystemMetrics::~SystemMetrics() {
    stopMonitoring();
}

void SystemMetrics::startMonitoring() {
    if (monitoring_.load()) {
        return; // Already monitoring
    }
    
    monitoring_.store(true);
    setBaseline();
    
    monitoringThread_ = std::thread(&SystemMetrics::monitoringLoop, this);
    
    ETL_LOG_INFO("System metrics monitoring started");
}

void SystemMetrics::stopMonitoring() {
    if (!monitoring_.load()) {
        return; // Already stopped
    }
    
    monitoring_.store(false);
    
    if (monitoringThread_.joinable()) {
        monitoringThread_.join();
    }
    
    ETL_LOG_INFO("System metrics monitoring stopped");
}

bool SystemMetrics::isMonitoring() const {
    return monitoring_.load();
}

size_t SystemMetrics::getCurrentMemoryUsage() const {
    return currentMemoryUsage_.load();
}

double SystemMetrics::getCurrentCpuUsage() const {
    return currentCpuUsage_.load();
}

size_t SystemMetrics::getProcessMemoryUsage() const {
    return processMemoryUsage_.load();
}

double SystemMetrics::getProcessCpuUsage() const {
    return processCpuUsage_.load();
}

size_t SystemMetrics::getPeakMemoryUsage() const {
    return peakMemoryUsage_.load();
}

double SystemMetrics::getPeakCpuUsage() const {
    return peakCpuUsage_.load();
}

void SystemMetrics::resetPeakUsage() {
    peakMemoryUsage_.store(currentMemoryUsage_.load());
    peakCpuUsage_.store(currentCpuUsage_.load());
}

size_t SystemMetrics::getMemoryUsageDelta() const {
    if (!baselineSet_) {
        return 0;
    }
    size_t current = processMemoryUsage_.load();
    return current > baselineMemoryUsage_ ? current - baselineMemoryUsage_ : 0;
}

double SystemMetrics::getCpuUsageDelta() const {
    if (!baselineSet_) {
        return 0.0;
    }
    double current = processCpuUsage_.load();
    return current > baselineCpuUsage_ ? current - baselineCpuUsage_ : 0.0;
}

void SystemMetrics::setMonitoringInterval(std::chrono::milliseconds interval) {
    monitoringInterval_ = interval;
}

void SystemMetrics::setMemoryThreshold(size_t thresholdBytes) {
    memoryThreshold_ = thresholdBytes;
}

void SystemMetrics::setCpuThreshold(double thresholdPercent) {
    cpuThreshold_ = thresholdPercent;
}

void SystemMetrics::setMemoryAlertCallback(MemoryAlertCallback callback) {
    std::scoped_lock lock(metricsMutex_);
    memoryAlertCallback_ = std::move(callback);
}

void SystemMetrics::setCpuAlertCallback(CpuAlertCallback callback) {
    std::scoped_lock lock(metricsMutex_);
    cpuAlertCallback_ = std::move(callback);
}

void SystemMetrics::monitoringLoop() {
    while (monitoring_.load()) {
        try {
            // Update system metrics
            currentMemoryUsage_.store(getSystemMemoryUsage());
            currentCpuUsage_.store(getSystemCpuUsage());
            processMemoryUsage_.store(getCurrentProcessMemoryUsage());
            processCpuUsage_.store(getCurrentProcessCpuUsage());
            
            // Update peak values
            updatePeakValues();
            
            // Check alert thresholds
            checkAlertThresholds();
            
        } catch (const std::exception& e) {
            ETL_LOG_ERROR("Error in metrics monitoring loop: " + std::string(e.what()));
        } catch (...) {
            ETL_LOG_ERROR("Unknown error in metrics monitoring loop");
        }
        
        std::this_thread::sleep_for(monitoringInterval_);
    }
}

void SystemMetrics::updatePeakValues() {
    // Update peak memory usage
    size_t currentMem = processMemoryUsage_.load();
    size_t currentPeakMem = peakMemoryUsage_.load();
    while (currentMem > currentPeakMem && 
           !peakMemoryUsage_.compare_exchange_weak(currentPeakMem, currentMem)) {
        // Retry with updated value
        currentPeakMem = peakMemoryUsage_.load();
    }
    
    // Update peak CPU usage
    double currentCpu = processCpuUsage_.load();
    double currentPeakCpu = peakCpuUsage_.load();
    while (currentCpu > currentPeakCpu && 
           !peakCpuUsage_.compare_exchange_weak(currentPeakCpu, currentCpu)) {
        // Retry with updated value
        currentPeakCpu = peakCpuUsage_.load();
    }
}

void SystemMetrics::checkAlertThresholds() {
    std::scoped_lock lock(metricsMutex_);
    
    // Check memory threshold
    if (memoryThreshold_ > 0 && memoryAlertCallback_) {
        size_t currentMem = processMemoryUsage_.load();
        if (currentMem > memoryThreshold_) {
            memoryAlertCallback_(currentMem, memoryThreshold_);
        }
    }
    
    // Check CPU threshold
    if (cpuThreshold_ > 0.0 && cpuAlertCallback_) {
        double currentCpu = processCpuUsage_.load();
        if (currentCpu > cpuThreshold_) {
            cpuAlertCallback_(currentCpu, cpuThreshold_);
        }
    }
}

void SystemMetrics::setBaseline() {
    baselineMemoryUsage_ = getCurrentProcessMemoryUsage();
    baselineCpuUsage_ = getCurrentProcessCpuUsage();
    baselineSet_ = true;
    
    // Initialize peak values
    peakMemoryUsage_.store(baselineMemoryUsage_);
    peakCpuUsage_.store(baselineCpuUsage_);
}

// Platform-specific implementations

#ifdef __APPLE__

size_t SystemMetrics::getSystemMemoryUsage() const {
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t host_count = sizeof(vm_stat) / sizeof(natural_t);
    
    host_page_size(mach_host_self(), &page_size);
    host_statistics64(mach_host_self(), HOST_VM_INFO64, 
                     reinterpret_cast<host_info64_t>(&vm_stat), &host_count);
    
    return (vm_stat.active_count + vm_stat.inactive_count + 
            vm_stat.wire_count) * page_size;
}

double SystemMetrics::getSystemCpuUsage() const {
    host_cpu_load_info_data_t cpu_info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    
    kern_return_t kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                                      reinterpret_cast<host_info_t>(&cpu_info), &count);
    
    if (kr != KERN_SUCCESS) {
        return 0.0;
    }
    
    static unsigned int last_user = 0, last_system = 0, last_idle = 0, last_nice = 0;
    
    unsigned int user = cpu_info.cpu_ticks[CPU_STATE_USER];
    unsigned int system = cpu_info.cpu_ticks[CPU_STATE_SYSTEM];
    unsigned int idle = cpu_info.cpu_ticks[CPU_STATE_IDLE];
    unsigned int nice = cpu_info.cpu_ticks[CPU_STATE_NICE];
    
    unsigned int total_ticks = (user - last_user) + (system - last_system) + 
                              (idle - last_idle) + (nice - last_nice);
    
    if (total_ticks == 0) {
        return 0.0;
    }
    
    double cpu_usage = 100.0 * ((user - last_user) + (system - last_system)) / total_ticks;
    
    last_user = user;
    last_system = system;
    last_idle = idle;
    last_nice = nice;
    
    return cpu_usage;
}

size_t SystemMetrics::getCurrentProcessMemoryUsage() const {
    task_basic_info_data_t info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    
    kern_return_t kr = task_info(mach_task_self(), TASK_BASIC_INFO,
                                reinterpret_cast<task_info_t>(&info), &count);
    
    if (kr != KERN_SUCCESS) {
        return 0;
    }
    
    return info.resident_size;
}

double SystemMetrics::getCurrentProcessCpuUsage() const {
    task_thread_times_info_data_t info;
    mach_msg_type_number_t count = TASK_THREAD_TIMES_INFO_COUNT;
    
    kern_return_t kr = task_info(mach_task_self(), TASK_THREAD_TIMES_INFO,
                                reinterpret_cast<task_info_t>(&info), &count);
    
    if (kr != KERN_SUCCESS) {
        return 0.0;
    }
    
    static double last_user_time = 0.0, last_system_time = 0.0;
    static auto last_time = std::chrono::high_resolution_clock::now();
    
    double user_time = info.user_time.seconds + info.user_time.microseconds / 1000000.0;
    double system_time = info.system_time.seconds + info.system_time.microseconds / 1000000.0;
    auto current_time = std::chrono::high_resolution_clock::now();
    
    double elapsed = std::chrono::duration<double>(current_time - last_time).count();
    
    if (elapsed > 0 && last_user_time > 0) {
        double cpu_usage = ((user_time - last_user_time) + (system_time - last_system_time)) / elapsed * 100.0;
        
        last_user_time = user_time;
        last_system_time = system_time;
        last_time = current_time;
        
        return std::max(0.0, std::min(100.0, cpu_usage));
    }
    
    last_user_time = user_time;
    last_system_time = system_time;
    last_time = current_time;
    
    return 0.0;
}

#elif __linux__

size_t SystemMetrics::getSystemMemoryUsage() const {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        return 0;
    }
    
    std::string line;
    size_t total_mem = 0, free_mem = 0, buffers = 0, cached = 0;
    
    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        size_t value;
        std::string unit;
        
        if (iss >> key >> value >> unit) {
            if (key == "MemTotal:") total_mem = value * 1024;
            else if (key == "MemFree:") free_mem = value * 1024;
            else if (key == "Buffers:") buffers = value * 1024;
            else if (key == "Cached:") cached = value * 1024;
        }
    }
    
    return total_mem - free_mem - buffers - cached;
}

double SystemMetrics::getSystemCpuUsage() const {
    std::ifstream stat("/proc/stat");
    if (!stat.is_open()) {
        return 0.0;
    }
    
    std::string line;
    std::getline(stat, line);
    
    std::istringstream iss(line);
    std::string cpu_label;
    long user, nice, system, idle, iowait, irq, softirq, steal;
    
    iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    
    static long last_user = 0, last_nice = 0, last_system = 0, last_idle = 0;
    static long last_iowait = 0, last_irq = 0, last_softirq = 0, last_steal = 0;
    
    long total_ticks = (user - last_user) + (nice - last_nice) + (system - last_system) + 
                      (idle - last_idle) + (iowait - last_iowait) + (irq - last_irq) + 
                      (softirq - last_softirq) + (steal - last_steal);
    
    if (total_ticks == 0) {
        return 0.0;
    }
    
    long active_ticks = (user - last_user) + (nice - last_nice) + (system - last_system) + 
                       (irq - last_irq) + (softirq - last_softirq) + (steal - last_steal);
    
    double cpu_usage = 100.0 * active_ticks / total_ticks;
    
    last_user = user; last_nice = nice; last_system = system; last_idle = idle;
    last_iowait = iowait; last_irq = irq; last_softirq = softirq; last_steal = steal;
    
    return cpu_usage;
}

size_t SystemMetrics::getCurrentProcessMemoryUsage() const {
    std::ifstream status("/proc/self/status");
    if (!status.is_open()) {
        return 0;
    }
    
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmRSS:") == 0) {
            std::istringstream iss(line);
            std::string key, unit;
            size_t value;
            
            if (iss >> key >> value >> unit) {
                return value * 1024; // Convert kB to bytes
            }
        }
    }
    
    return 0;
}

double SystemMetrics::getCurrentProcessCpuUsage() const {
    std::ifstream stat("/proc/self/stat");
    if (!stat.is_open()) {
        return 0.0;
    }
    
    std::string line;
    std::getline(stat, line);
    
    std::istringstream iss(line);
    std::string token;
    
    // Skip to utime (14th field) and stime (15th field)
    for (int i = 0; i < 13; ++i) {
        iss >> token;
    }
    
    long utime, stime;
    iss >> utime >> stime;
    
    static long last_utime = 0, last_stime = 0;
    static auto last_time = std::chrono::high_resolution_clock::now();
    
    auto current_time = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(current_time - last_time).count();
    
    if (elapsed > 0 && last_utime > 0) {
        long ticks_per_second = sysconf(_SC_CLK_TCK);
        double cpu_usage = ((utime - last_utime) + (stime - last_stime)) / 
                          (elapsed * ticks_per_second) * 100.0;
        
        last_utime = utime;
        last_stime = stime;
        last_time = current_time;
        
        return std::max(0.0, std::min(100.0, cpu_usage));
    }
    
    last_utime = utime;
    last_stime = stime;
    last_time = current_time;
    
    return 0.0;
}

#else // Windows or other platforms

size_t SystemMetrics::getSystemMemoryUsage() const {
    // Fallback implementation for unsupported platforms
    return 0;
}

double SystemMetrics::getSystemCpuUsage() const {
    // Fallback implementation for unsupported platforms
    return 0.0;
}

size_t SystemMetrics::getCurrentProcessMemoryUsage() const {
    // Fallback implementation for unsupported platforms
    return 0;
}

double SystemMetrics::getCurrentProcessCpuUsage() const {
    // Fallback implementation for unsupported platforms
    return 0.0;
}

#endif

// JobMetricsCollector implementation

JobMetricsCollector::JobMetricsCollector(const std::string& jobId) 
    : jobId_(jobId), systemMetrics_(std::make_shared<SystemMetrics>()) {
}

JobMetricsCollector::~JobMetricsCollector() {
    stopCollection();
}

void JobMetricsCollector::startCollection() {
    if (collecting_.load()) {
        return; // Already collecting
    }
    
    collecting_.store(true);
    startTime_ = std::chrono::system_clock::now();
    lastRateUpdate_.store(startTime_);
    recordsAtLastUpdate_.store(0);
    
    // Start system metrics monitoring
    systemMetrics_->startMonitoring();
    
    // Capture baseline resource usage
    baselineMemoryUsage_ = systemMetrics_->getProcessMemoryUsage();
    baselineCpuUsage_ = systemMetrics_->getProcessCpuUsage();
    
    // Start real-time update thread if callback is set
    if (updateCallback_) {
        shouldStopUpdates_.store(false);
        updateThread_ = std::thread(&JobMetricsCollector::updateLoop, this);
    }
    
    ETL_LOG_INFO("Started metrics collection for job: " + jobId_);
}

void JobMetricsCollector::stopCollection() {
    if (!collecting_.load()) {
        return; // Already stopped
    }
    
    collecting_.store(false);
    shouldStopUpdates_.store(true);
    
    // Stop update thread
    if (updateThread_.joinable()) {
        updateThread_.join();
    }
    
    // Stop system metrics monitoring
    systemMetrics_->stopMonitoring();
    
    ETL_LOG_INFO("Stopped metrics collection for job: " + jobId_);
}

bool JobMetricsCollector::isCollecting() const {
    return collecting_.load();
}

void JobMetricsCollector::recordProcessedRecord() {
    recordsProcessed_.fetch_add(1);
}

void JobMetricsCollector::recordSuccessfulRecord() {
    recordsSuccessful_.fetch_add(1);
}

void JobMetricsCollector::recordFailedRecord() {
    recordsFailed_.fetch_add(1);
}

void JobMetricsCollector::recordBatchProcessed(int batchSize, int successful, int failed) {
    recordsProcessed_.fetch_add(batchSize);
    recordsSuccessful_.fetch_add(successful);
    recordsFailed_.fetch_add(failed);
    updateProcessingRate();
}

int JobMetricsCollector::getRecordsProcessed() const {
    return recordsProcessed_.load();
}

int JobMetricsCollector::getRecordsSuccessful() const {
    return recordsSuccessful_.load();
}

int JobMetricsCollector::getRecordsFailed() const {
    return recordsFailed_.load();
}

double JobMetricsCollector::getProcessingRate() const {
    return processingRate_.load();
}

std::chrono::milliseconds JobMetricsCollector::getExecutionTime() const {
    return calculateExecutionTime();
}

size_t JobMetricsCollector::getMemoryUsage() const {
    if (!systemMetrics_->isMonitoring()) {
        return 0;
    }
    size_t currentMemory = systemMetrics_->getProcessMemoryUsage();
    return currentMemory > baselineMemoryUsage_ ? currentMemory - baselineMemoryUsage_ : 0;
}

double JobMetricsCollector::getCpuUsage() const {
    if (!systemMetrics_->isMonitoring()) {
        return 0.0;
    }
    double currentCpu = systemMetrics_->getProcessCpuUsage();
    return currentCpu > baselineCpuUsage_ ? currentCpu - baselineCpuUsage_ : 0.0;
}

void JobMetricsCollector::updateProcessingRate() {
    auto now = std::chrono::system_clock::now();
    auto lastUpdate = lastRateUpdate_.load();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);

    if (elapsed.count() > 1000) { // Update every second
        int currentRecords = recordsProcessed_.load();
        int lastRecords = recordsAtLastUpdate_.load();
        int recordsDelta = currentRecords - lastRecords;

        if (elapsed.count() > 0) {
            double rate = static_cast<double>(recordsDelta) / (elapsed.count() / 1000.0);
            processingRate_.store(rate);
        }
        
        lastRateUpdate_.store(now);
        recordsAtLastUpdate_.store(currentRecords);
    }
}

JobMetricsCollector::MetricsSnapshot JobMetricsCollector::getMetricsSnapshot() const {
    MetricsSnapshot snapshot;
    snapshot.recordsProcessed = recordsProcessed_.load();
    snapshot.recordsSuccessful = recordsSuccessful_.load();
    snapshot.recordsFailed = recordsFailed_.load();
    snapshot.processingRate = processingRate_.load();
    snapshot.executionTime = calculateExecutionTime();
    snapshot.memoryUsage = getMemoryUsage();
    snapshot.cpuUsage = getCpuUsage();
    snapshot.timestamp = std::chrono::system_clock::now();
    
    return snapshot;
}

void JobMetricsCollector::setMetricsUpdateCallback(MetricsUpdateCallback callback) {
    updateCallback_ = std::move(callback);
}

void JobMetricsCollector::setUpdateInterval(std::chrono::milliseconds interval) {
    updateInterval_ = interval;
}

void JobMetricsCollector::updateLoop() {
    while (!shouldStopUpdates_.load() && collecting_.load()) {
        try {
            if (updateCallback_) {
                auto snapshot = getMetricsSnapshot();
                updateCallback_(jobId_, snapshot);
            }
        } catch (const std::exception& e) {
            ETL_LOG_ERROR("Error in metrics update loop for job " + jobId_ + ": " + e.what());
        } catch (...) {
            ETL_LOG_ERROR("Unknown error in metrics update loop for job " + jobId_);
        }
        
        std::this_thread::sleep_for(updateInterval_);
    }
}

std::chrono::milliseconds JobMetricsCollector::calculateExecutionTime() const {
    if (!collecting_.load()) {
        return std::chrono::milliseconds(0);
    }
    
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_);
}

} // namespace ETLPlus::Metrics
