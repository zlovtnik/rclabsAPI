#pragma once

#include "job_monitoring_models.hpp"
#include "system_metrics.hpp"
#include <chrono>
#include <memory>
#include <string>

struct ETLJobConfig {
  std::string jobId;
  JobType type;
  std::string sourceConfig;
  std::string targetConfig;
  std::string transformationRules;
  std::chrono::system_clock::time_point scheduledTime;
  bool isRecurring;
  std::chrono::minutes recurringInterval;
};

struct ETLJob {
  std::string jobId;
  JobType type = JobType::FULL_ETL;
  JobStatus status = JobStatus::PENDING;
  std::string sourceConfig;
  std::string targetConfig;
  std::chrono::system_clock::time_point createdAt = std::chrono::system_clock::now();
  std::chrono::system_clock::time_point startedAt{};
  std::chrono::system_clock::time_point completedAt{};
  std::string errorMessage;
  int recordsProcessed = 0;
  int recordsSuccessful = 0;
  int recordsFailed = 0;

  // Enhanced metrics tracking
  JobMetrics metrics;
  std::shared_ptr<ETLPlus::Metrics::JobMetricsCollector> metricsCollector;

  // Default constructor
  ETLJob() = default;
};
