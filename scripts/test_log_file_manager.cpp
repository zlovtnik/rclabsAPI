#include <gtest/gtest.h>
#include "log_file_manager.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

class LogFileManagerTest : public ::testing::Test {
private:
    std::string testDir;
    std::string archiveDir;
    FileConfig config;

protected:
    void SetUp() override {
        testDir = "/tmp/etlplus_test_logs";
        archiveDir = testDir + "/archive";
        
        // Clean up from previous tests
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }
        
        // Create test configuration
        config.logFile = testDir + "/test.log";
        config.archiveDirectory = archiveDir;
        config.enableFileOutput = true;
        config.maxFileSize = 150; // Very small size for testing rotation
        config.maxBackupFiles = 3;
        config.enableRotation = true;
        config.enableHistoricalAccess = true;
        config.enableLogIndexing = true;
    }
    
    void TearDown() override {
        // Clean up test files
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }
    }
    
    const FileConfig& getConfig() const { return config; }
    const std::string& getTestDir() const { return testDir; }
    const std::string& getArchiveDir() const { return archiveDir; }
};

TEST_F(LogFileManagerTest, InitializeFileCreatesDirectoriesAndFile) {
    LogFileManager manager(getConfig());
    
    EXPECT_TRUE(manager.initializeFile());
    EXPECT_TRUE(std::filesystem::exists(getConfig().logFile));
    EXPECT_TRUE(std::filesystem::exists(getArchiveDir()));
    EXPECT_TRUE(manager.isFileOpen());
}

TEST_F(LogFileManagerTest, WriteToFileIncreasesFileSize) {
    LogFileManager manager(getConfig());
    manager.initializeFile();
    
    size_t initialSize = manager.getCurrentFileSize();
    
    // Write a small message that won't trigger rotation
    manager.writeToFile("Small message");
    
    // If rotation didn't happen, size should increase
    // If rotation happened, check the backup file was created
    if (manager.getCurrentFileSize() == 0) {
        // Rotation occurred, check backup exists
        std::string backupFile = getConfig().logFile + ".1";
        EXPECT_TRUE(std::filesystem::exists(backupFile));
    } else {
        // No rotation, file size should have increased
        EXPECT_GT(manager.getCurrentFileSize(), initialSize);
    }
}

TEST_F(LogFileManagerTest, RotationPolicyWorks) {
    // Use size-based rotation policy
    auto policy = std::make_unique<SizeBasedRotationPolicy>(100); // Very small size
    
    auto now = std::chrono::system_clock::now();
    EXPECT_FALSE(policy->shouldRotate(50, now));
    EXPECT_TRUE(policy->shouldRotate(150, now));
}

TEST_F(LogFileManagerTest, TimeBasedRotationPolicyWorks) {
    auto policy = std::make_unique<TimeBasedRotationPolicy>(std::chrono::hours(1));
    
    auto now = std::chrono::system_clock::now();
    auto twoHoursAgo = now - std::chrono::hours(2);
    
    EXPECT_FALSE(policy->shouldRotate(50, now));
    EXPECT_TRUE(policy->shouldRotate(50, twoHoursAgo));
}

TEST_F(LogFileManagerTest, FileRotationCreatesBackup) {
    LogFileManager manager(getConfig());
    manager.initializeFile();
    
    // Write enough data to trigger rotation
    std::string longMessage(200, 'A'); // Larger than maxFileSize
    manager.writeToFile(longMessage);
    
    // Check that backup file was created
    std::string backupFile = getConfig().logFile + ".1";
    EXPECT_TRUE(std::filesystem::exists(backupFile));
}

TEST_F(LogFileManagerTest, ListLogFilesReturnsCorrectFiles) {
    LogFileManager manager(getConfig());
    manager.initializeFile();
    
    // Write some data and force rotation
    manager.writeToFile(std::string(200, 'A'));
    manager.writeToFile(std::string(200, 'B'));
    
    auto files = manager.listLogFiles(false);
    EXPECT_GE(files.size(), 1); // At least the current file
    
    // Check that file info is populated correctly
    for (const auto& file : files) {
        EXPECT_FALSE(file.filename.empty());
        EXPECT_FALSE(file.fullPath.empty());
        EXPECT_GE(file.fileSize, 0);
    }
}

TEST_F(LogFileManagerTest, ArchiveFileMovesToArchiveDirectory) {
    LogFileManager manager(getConfig());
    manager.initializeFile();
    
    // Create a test file to archive
    std::string testFile = getTestDir() + "/test_archive.log";
    std::ofstream file(testFile);
    file << "Test content for archiving";
    file.close();
    
    EXPECT_TRUE(manager.archiveFile(testFile));
    EXPECT_FALSE(std::filesystem::exists(testFile)); // Original should be moved
    
    // Check that file exists in archive directory
    auto archivedFiles = manager.listLogFiles(true);
    bool foundArchived = false;
    for (const auto& f : archivedFiles) {
        if (f.isArchived && f.filename.find("test_archive") != std::string::npos) {
            foundArchived = true;
            break;
        }
    }
    EXPECT_TRUE(foundArchived);
}

TEST_F(LogFileManagerTest, CleanupRemovesOldBackupFiles) {
    LogFileManager manager(getConfig());
    manager.initializeFile();
    
    // Create more backup files than maxBackupFiles
    for (int i = 1; i <= 5; ++i) {
        std::string backupFile = getConfig().logFile + "." + std::to_string(i);
        std::ofstream file(backupFile);
        file << "Backup " << i;
        file.close();
    }
    
    manager.cleanupOldFiles();
    
    // Check that only maxBackupFiles remain
    int backupCount = 0;
    for (int i = 1; i <= 5; ++i) {
        std::string backupFile = getConfig().logFile + "." + std::to_string(i);
        if (std::filesystem::exists(backupFile)) {
            backupCount++;
        }
    }
    EXPECT_LE(backupCount, getConfig().maxBackupFiles);
}

TEST_F(LogFileManagerTest, ConfigurationUpdateWorks) {
    LogFileManager manager(getConfig());
    
    FileConfig newConfig = getConfig();
    newConfig.maxFileSize = 2048;
    newConfig.maxBackupFiles = 5;
    
    manager.updateConfig(newConfig);
    
    EXPECT_EQ(manager.getConfig().maxFileSize, 2048);
    EXPECT_EQ(manager.getConfig().maxBackupFiles, 5);
}

TEST_F(LogFileManagerTest, LogFileArchiverCompression) {
    LogFileArchiver archiver(getConfig());
    
    // Create a test file
    std::string testFile = getTestDir() + "/test_compress.log";
    std::filesystem::create_directories(getTestDir());
    std::ofstream file(testFile);
    file << "Test content for compression";
    file.close();
    
    EXPECT_TRUE(archiver.compressFile(testFile, "gzip"));
    EXPECT_TRUE(std::filesystem::exists(testFile + ".gz"));
}

TEST_F(LogFileManagerTest, LogFileIndexerIndexing) {
    LogFileIndexer indexer(getConfig());
    
    std::filesystem::create_directories(getArchiveDir());
    
    std::string testFile = getTestDir() + "/test_index.log";
    indexer.indexFile(testFile);
    
    std::string indexFile = getArchiveDir() + "/log_index.txt";
    EXPECT_TRUE(std::filesystem::exists(indexFile));
    
    // Check that the file is actually indexed
    std::ifstream index(indexFile);
    std::string line;
    bool found = false;
    while (std::getline(index, line)) {
        if (line.find("test_index.log") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
