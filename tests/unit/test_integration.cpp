#include "error_codes.hpp"
#include "etl_exceptions.hpp"
#include "lock_utils.hpp"
#include "type_definitions.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace etl_plus {

// Test fixture for integration tests
class IntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Setup code if needed
  }

  void TearDown() override {
    // Cleanup code if needed
  }
};

// Test complete request processing workflow
TEST_F(IntegrationTest, CompleteRequestWorkflow) {
  // Create components
  ConfigMutex configMutex;
  ContainerMutex containerMutex;

  // Simulate a complete request workflow with proper locking
  {
    ScopedTimedLock<ConfigMutex> configLock(configMutex);
    ScopedTimedLock<ContainerMutex> containerLock(containerMutex);

    // Simulate processing a request that might throw an exception
    try {
      // Simulate some processing that might fail
      throw etl::ValidationException(etl::ErrorCode::INVALID_INPUT,
                                     "Invalid request data", "email",
                                     "invalid@email");
    } catch (const etl::ETLException &e) {
      // Verify the exception details
      EXPECT_TRUE(std::string(e.what()).find("Invalid request data") !=
                  std::string::npos);
      EXPECT_EQ(e.getCode(), etl::ErrorCode::INVALID_INPUT);
    }
  }
}

// Test concurrent access with proper locking
TEST_F(IntegrationTest, ConcurrentAccessWithLocking) {
  ConfigMutex configMutex;
  ContainerMutex containerMutex;
  std::atomic<int> sharedCounter{0};
  std::vector<std::thread> threads;
  const int numThreads = 10;
  const int iterationsPerThread = 100;

  // Launch multiple threads that access shared resources
  for (int i = 0; i < numThreads; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < iterationsPerThread; ++j) {
        {
          ScopedTimedLock<ConfigMutex> configLock(configMutex);
          ScopedTimedLock<ContainerMutex> containerLock(containerMutex);
          sharedCounter++;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    });
  }

  // Wait for all threads to complete
  for (auto &thread : threads) {
    thread.join();
  }

  // Verify all operations completed
  EXPECT_EQ(sharedCounter.load(), numThreads * iterationsPerThread);
}

// Test exception handling across components
TEST_F(IntegrationTest, CrossComponentExceptionHandling) {
  ConfigMutex configMutex;

  // Test that exceptions from different components are handled consistently
  {
    ScopedTimedLock<ConfigMutex> lock(configMutex);

    // Test ValidationException
    try {
      throw etl::ValidationException(etl::ErrorCode::INVALID_INPUT,
                                     "Validation failed", "field", "value");
      FAIL() << "Expected ValidationException was not thrown";
    } catch (const etl::ValidationException &e) {
      EXPECT_FALSE(std::string(e.what()).empty());
      EXPECT_EQ(e.getCode(), etl::ErrorCode::INVALID_INPUT);
    }
  }

  {
    ScopedTimedLock<ConfigMutex> lock(configMutex);

    // Test SystemException
    try {
      throw etl::SystemException(etl::ErrorCode::DATABASE_ERROR,
                                 "System error occurred");
      FAIL() << "Expected SystemException was not thrown";
    } catch (const etl::SystemException &e) {
      EXPECT_FALSE(std::string(e.what()).empty());
      EXPECT_EQ(e.getCode(), etl::ErrorCode::DATABASE_ERROR);
    }
  }

  {
    ScopedTimedLock<ConfigMutex> lock(configMutex);

    // Test unknown exception
    try {
      throw std::runtime_error("Unknown error");
      FAIL() << "Expected runtime_error was not thrown";
    } catch (const std::exception &e) {
      EXPECT_FALSE(std::string(e.what()).empty());
    }
  }
}

// Test StrongId usage in concurrent scenarios
TEST_F(IntegrationTest, StrongIdConcurrentUsage) {
  using JobId = etl::StrongId<struct JobTag>;
  using ConnectionId = etl::StrongId<struct ConnectionTag>;

  std::unordered_map<JobId, std::string> jobData;
  std::unordered_map<ConnectionId, std::string> connectionData;
  ConfigMutex dataMutex;

  // Generate some IDs
  JobId job1("job1");
  JobId job2("job2");
  ConnectionId conn1("conn1");

  // Test concurrent access to ID-based data structures
  std::vector<std::thread> threads;
  for (int i = 0; i < 5; ++i) {
    threads.emplace_back([&]() {
      ScopedTimedLock<ConfigMutex> lock(dataMutex);
      jobData[job1] = "Job data from thread " + std::to_string(i);
      connectionData[conn1] =
          "Connection data from thread " + std::to_string(i);
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  // Verify data integrity
  EXPECT_EQ(jobData.size(), 1);
  EXPECT_EQ(connectionData.size(), 1);
  EXPECT_TRUE(jobData[job1].find("Job data") != std::string::npos);
  EXPECT_TRUE(connectionData[conn1].find("Connection data") !=
              std::string::npos);
}

// Test error recovery and logging integration
TEST_F(IntegrationTest, ErrorRecoveryAndLogging) {
  ConfigMutex configMutex;

  // Test that the system can recover from errors and continue processing
  bool errorOccurred = false;
  std::string errorMessage;

  {
    ScopedTimedLock<ConfigMutex> lock(configMutex);

    try {
      // Simulate an operation that fails
      throw etl::SystemException(etl::ErrorCode::NETWORK_ERROR,
                                 "Temporary failure");
    } catch (const etl::ETLException &e) {
      errorOccurred = true;
      errorMessage = e.what();
    }
  }

  // Verify error was captured
  EXPECT_TRUE(errorOccurred);
  EXPECT_FALSE(errorMessage.empty());
  EXPECT_TRUE(errorMessage.find("Temporary failure") != std::string::npos);
}

// Test timeout handling in concurrent operations
TEST_F(IntegrationTest, TimeoutHandling) {
  ConfigMutex configMutex;
  std::atomic<bool> operationCompleted{false};

  // Test that timeouts are handled properly
  auto future = std::async(std::launch::async, [&]() {
    try {
      ScopedTimedLock<ConfigMutex> lock(configMutex,
                                        std::chrono::milliseconds(100));
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      operationCompleted = true;
      return true;
    } catch (const LockTimeoutException &) {
      return false;
    }
  });

  // Wait for completion
  EXPECT_TRUE(future.get());
  EXPECT_TRUE(operationCompleted.load());
}

// Test memory safety with RAII patterns
TEST_F(IntegrationTest, MemorySafetyRAII) {
  ConfigMutex configMutex;
  std::atomic<int> resourceCount{0};

  // Test that resources are properly managed with RAII
  class TestResource {
  public:
    TestResource(std::atomic<int> &counter) : counter_(counter) { counter_++; }
    ~TestResource() { counter_--; }

  private:
    std::atomic<int> &counter_;
  };

  {
    ScopedTimedLock<ConfigMutex> lock(configMutex);
    TestResource resource1(resourceCount);
    TestResource resource2(resourceCount);
    EXPECT_EQ(resourceCount.load(), 2);
  } // Resources should be automatically cleaned up

  EXPECT_EQ(resourceCount.load(), 0);
}

} // namespace etl_plus
