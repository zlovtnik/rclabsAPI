#include "etl_job_manager.hpp"
#include "data_transformer.hpp"
#include "database_manager.hpp"
#include <iostream>
#include <random>
#include <sstream>

ETLJobManager::ETLJobManager(std::shared_ptr<DatabaseManager> dbManager,
                           std::shared_ptr<DataTransformer> transformer)
    : dbManager_(dbManager)
    , transformer_(transformer)
    , running_(false) {
}

ETLJobManager::~ETLJobManager() {
    stop();
}

std::string ETLJobManager::scheduleJob(const ETLJobConfig& config) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    
    auto job = std::make_shared<ETLJob>();
    job->jobId = config.jobId.empty() ? generateJobId() : config.jobId;
    job->type = config.type;
    job->status = JobStatus::PENDING;
    job->sourceConfig = config.sourceConfig;
    job->targetConfig = config.targetConfig;
    job->createdAt = std::chrono::system_clock::now();
    job->recordsProcessed = 0;
    job->recordsSuccessful = 0;
    job->recordsFailed = 0;
    
    jobs_.push_back(job);
    jobQueue_.push(job);
    
    jobCondition_.notify_one();
    
    std::cout << "Scheduled job: " << job->jobId << std::endl;
    return job->jobId;
}

bool ETLJobManager::cancelJob(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    
    for (auto& job : jobs_) {
        if (job->jobId == jobId && job->status == JobStatus::PENDING) {
            job->status = JobStatus::CANCELLED;
            std::cout << "Cancelled job: " << jobId << std::endl;
            return true;
        }
    }
    
    return false;
}

bool ETLJobManager::pauseJob(const std::string& jobId) {
    // For simplicity, not implementing pause/resume functionality
    std::cout << "Pause job not implemented: " << jobId << std::endl;
    return false;
}

bool ETLJobManager::resumeJob(const std::string& jobId) {
    // For simplicity, not implementing pause/resume functionality
    std::cout << "Resume job not implemented: " << jobId << std::endl;
    return false;
}

std::shared_ptr<ETLJob> ETLJobManager::getJob(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    
    for (const auto& job : jobs_) {
        if (job->jobId == jobId) {
            return job;
        }
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<ETLJob>> ETLJobManager::getAllJobs() {
    std::lock_guard<std::mutex> lock(jobMutex_);
    return jobs_;
}

std::vector<std::shared_ptr<ETLJob>> ETLJobManager::getJobsByStatus(JobStatus status) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    
    std::vector<std::shared_ptr<ETLJob>> result;
    for (const auto& job : jobs_) {
        if (job->status == status) {
            result.push_back(job);
        }
    }
    
    return result;
}

void ETLJobManager::start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    workerThread_ = std::thread(&ETLJobManager::workerLoop, this);
    std::cout << "ETL Job Manager started" << std::endl;
}

void ETLJobManager::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    jobCondition_.notify_all();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    std::cout << "ETL Job Manager stopped" << std::endl;
}

bool ETLJobManager::isRunning() const {
    return running_;
}

void ETLJobManager::workerLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(jobMutex_);
        
        jobCondition_.wait(lock, [this] { return !jobQueue_.empty() || !running_; });
        
        if (!running_) {
            break;
        }
        
        if (!jobQueue_.empty()) {
            auto job = jobQueue_.front();
            jobQueue_.pop();
            lock.unlock();
            
            if (job->status == JobStatus::PENDING) {
                executeJob(job);
            }
        }
    }
}

void ETLJobManager::executeJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Executing job: " << job->jobId << std::endl;
    
    job->status = JobStatus::RUNNING;
    job->startedAt = std::chrono::system_clock::now();
    
    try {
        switch (job->type) {
            case JobType::EXTRACT:
                executeExtractJob(job);
                break;
            case JobType::TRANSFORM:
                executeTransformJob(job);
                break;
            case JobType::LOAD:
                executeLoadJob(job);
                break;
            case JobType::FULL_ETL:
                executeFullETLJob(job);
                break;
        }
        
        job->status = JobStatus::COMPLETED;
        std::cout << "Job completed: " << job->jobId << std::endl;
        
    } catch (const std::exception& e) {
        job->status = JobStatus::FAILED;
        job->errorMessage = e.what();
        std::cerr << "Job failed: " << job->jobId << " - " << e.what() << std::endl;
    }
    
    job->completedAt = std::chrono::system_clock::now();
}

void ETLJobManager::executeExtractJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Extracting data from: " << job->sourceConfig << std::endl;
    
    // Simulate data extraction
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    job->recordsProcessed = 100;
    job->recordsSuccessful = 95;
    job->recordsFailed = 5;
}

void ETLJobManager::executeTransformJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Transforming data" << std::endl;
    
    // Create sample data
    std::vector<DataRecord> inputData;
    DataRecord record1;
    record1.fields["name"] = "John Doe";
    record1.fields["age"] = "30";
    inputData.push_back(record1);
    
    // Apply transformations
    auto transformedData = transformer_->transform(inputData);
    
    job->recordsProcessed = static_cast<int>(inputData.size());
    job->recordsSuccessful = static_cast<int>(transformedData.size());
    job->recordsFailed = 0;
}

void ETLJobManager::executeLoadJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Loading data to: " << job->targetConfig << std::endl;
    
    // Simulate data loading
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    if (dbManager_->isConnected()) {
        dbManager_->executeQuery("INSERT INTO processed_data VALUES (...)");
    }
    
    job->recordsProcessed = 95;
    job->recordsSuccessful = 90;
    job->recordsFailed = 5;
}

void ETLJobManager::executeFullETLJob(std::shared_ptr<ETLJob> job) {
    std::cout << "Executing full ETL pipeline" << std::endl;
    
    // Extract
    executeExtractJob(job);
    
    // Transform
    executeTransformJob(job);
    
    // Load
    executeLoadJob(job);
    
    std::cout << "Full ETL pipeline completed for job: " << job->jobId << std::endl;
}

std::string ETLJobManager::generateJobId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    return "job_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + 
           "_" + std::to_string(dis(gen));
}
