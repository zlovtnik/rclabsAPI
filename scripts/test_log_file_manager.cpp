#include "log_file_manager.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

class LogFileManagerTest : public ::testing::Test {
private:
  std::string testDir;
  std::string archiveDir;
  LogFileManagerConfig config;

protected:
  void SetUp() override {
    // Use platform-agnostic temporary directory instead of hard-coded /tmp
    testDir = std::filesystem::temp_directory_path() / "etlplus_test_logs";
    archiveDir = testDir + "/archive";

    // Clean up from previous tests
    if (std::filesystem::exists(testDir)) {
      std::filesystem::remove_all(testDir);
    }

    // Create test configuration using new structure
    config.logDirectory = testDir;
    config.defaultLogFile = "test.log";
    config.archive.archiveDirectory = archiveDir;
    config.rotation.enabled = true;
    config.rotation.maxFileSize = 150; // Very small size for testing rotation
    config.rotation.maxBackupFiles = 3;
    config.archive.enabled = true;
    config.indexing.enabled = true;
  }

  void TearDown() override {
    // Clean up test files
    if (std::filesystem::exists(testDir)) {
      std::filesystem::remove_all(testDir);
    }
  }

  const LogFileManagerConfig &getConfig() const { return config; }
  const std::string &getTestDir() const { return testDir; }
  const std::string &getArchiveDir() const { return archiveDir; }
};

TEST_F(LogFileManagerTest, InitializeFileCreatesDirectoriesAndFile) {
  LogFileManager manager(getConfig());

  EXPECT_TRUE(manager.initializeLogFile(getConfig().defaultLogFile));
  std::string fullPath = getTestDir() + "/" + getConfig().defaultLogFile;
  EXPECT_TRUE(std::filesystem::exists(fullPath));
  EXPECT_TRUE(std::filesystem::exists(getArchiveDir()));
}

TEST_F(LogFileManagerTest, WriteToFileIncreasesFileSize) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  size_t initialSize = manager.getCurrentFileSize();

  // Write a small message that won't trigger rotation
  manager.writeToFile("Small message");

  // If rotation didn't happen, size should increase
  // If rotation happened, check the backup file was created
  if (manager.getCurrentFileSize() == 0) {
    // Rotation occurred, check backup exists
    std::string backupFile =
        getConfig().logDirectory + "/" + getConfig().defaultLogFile + ".1";
    EXPECT_TRUE(std::filesystem::exists(backupFile));
  } else {
    // No rotation, file size should have increased
    EXPECT_GT(manager.getCurrentFileSize(), initialSize);
  }
}

TEST_F(LogFileManagerTest, RotationBasedOnSize) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  // Test if rotation is needed based on current file size
  EXPECT_FALSE(manager.needsRotation()); // Should not need rotation initially

  // Write enough data to potentially trigger rotation
  std::string longMessage(200, 'A'); // Larger than maxFileSize (150)
  manager.writeToFile(longMessage);

  // Check if rotation occurred or is needed
  bool rotationOccurred = manager.getCurrentFileSize() < longMessage.size();
  if (!rotationOccurred) {
    EXPECT_TRUE(manager.needsRotation()); // Should need rotation now
  }
}

TEST_F(LogFileManagerTest, ManualRotationWorks) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  // Write some data
  manager.writeToFile("Test data for rotation");

  // Force rotation
  EXPECT_TRUE(manager.rotateLogFile());

  // Check that backup file was created
  std::string backupFile =
      getConfig().logDirectory + "/" + getConfig().defaultLogFile + ".1";
  EXPECT_TRUE(std::filesystem::exists(backupFile));
}

TEST_F(LogFileManagerTest, FileRotationCreatesBackup) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  // Write enough data to trigger rotation
  std::string longMessage(200, 'A'); // Larger than maxFileSize
  manager.writeToFile(longMessage);

  // Check that backup file was created
  std::string backupFile =
      getConfig().logDirectory + "/" + getConfig().defaultLogFile + ".1";
  EXPECT_TRUE(std::filesystem::exists(backupFile));
}

TEST_F(LogFileManagerTest, ListLogFilesReturnsCorrectFiles) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  // Write some data and force rotation
  manager.writeToFile(std::string(200, 'A'));
  manager.writeToFile(std::string(200, 'B'));

  auto files = manager.listLogFiles(false);
  EXPECT_GE(files.size(), 1); // At least the current file

  // Check that file info is populated correctly
  for (const auto &file : files) {
    EXPECT_FALSE(file.filename.empty());
    EXPECT_FALSE(file.fullPath.empty());
    EXPECT_GE(file.fileSize, 0);
  }
}

TEST_F(LogFileManagerTest, ArchiveFileMovesToArchiveDirectory) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  // Create a test file to archive
  std::string testFile = getTestDir() + "/test_archive.log";
  std::ofstream file(testFile);
  file << "Test content for archiving";
  file.close();

  EXPECT_TRUE(manager.archiveLogFile(testFile));

  // Check that file exists in archive directory
  auto archivedFiles = manager.listLogFiles(true);
  bool foundArchived = false;
  for (const auto &f : archivedFiles) {
    if (f.isArchived && f.filename.find("test_archive") != std::string::npos) {
      foundArchived = true;
      break;
    }
  }
  EXPECT_TRUE(foundArchived);
}

TEST_F(LogFileManagerTest, CleanupRemovesTempFiles) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  // Create some temporary files
  std::filesystem::create_directories(getConfig().logDirectory);
  for (int i = 1; i <= 3; ++i) {
    std::string tempFile =
        getConfig().logDirectory + "/temp" + std::to_string(i) + ".tmp";
    std::ofstream file(tempFile);
    file << "Temp file " << i;
    file.close();
  }

  size_t cleanedFiles = manager.cleanupTempFiles();
  EXPECT_EQ(cleanedFiles, 3);

  // Verify temp files are gone
  for (int i = 1; i <= 3; ++i) {
    std::string tempFile =
        getConfig().logDirectory + "/temp" + std::to_string(i) + ".tmp";
    EXPECT_FALSE(std::filesystem::exists(tempFile));
  }
}

TEST_F(LogFileManagerTest, ConfigurationUpdateWorks) {
  LogFileManager manager(getConfig());

  LogFileManagerConfig newConfig = getConfig();
  newConfig.rotation.maxFileSize = 2048;
  newConfig.rotation.maxBackupFiles = 5;

  manager.updateConfig(newConfig);

  EXPECT_EQ(manager.getConfig().rotation.maxFileSize, 2048);
  EXPECT_EQ(manager.getConfig().rotation.maxBackupFiles, 5);
}

TEST_F(LogFileManagerTest, MetricsTracking) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  // Get initial metrics
  auto initialMetrics = manager.getMetrics();

  // Write some data
  manager.writeToFile("Test message for metrics");

  // Check that metrics were updated
  auto finalMetrics = manager.getMetrics();
  EXPECT_GT(finalMetrics.totalWriteOperations.load(),
            initialMetrics.totalWriteOperations.load());
  EXPECT_GT(finalMetrics.totalBytesWritten.load(),
            initialMetrics.totalBytesWritten.load());
}

TEST_F(LogFileManagerTest, HealthStatus) {
  LogFileManager manager(getConfig());
  manager.initializeLogFile(getConfig().defaultLogFile);

  // Manager should be healthy after initialization
  EXPECT_TRUE(manager.isHealthy());

  // Get status string
  std::string status = manager.getStatus();
  EXPECT_FALSE(status.empty());
  EXPECT_NE(status.find("healthy"), std::string::npos);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
