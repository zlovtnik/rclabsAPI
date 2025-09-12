#include "log_file_manager.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

int main() {
  std::cout << "=== LogFileManager Demo ===" << std::endl;

  // Create configuration using the new structure
  LogFileManagerConfig config;
  config.logDirectory = "demo_logs";
  config.defaultLogFile = "demo.log";
  config.archive.archiveDirectory = "demo_logs/archive";
  config.rotation.enabled = true;
  config.rotation.maxFileSize = 1024; // 1KB for demonstration
  config.rotation.maxBackupFiles = 3;
  config.archive.enabled = true;
  config.archive.compressOnArchive = true;
  config.archive.compressionType = CompressionType::GZIP;
  config.indexing.enabled = true;

  // Ensure directories exist for the demo
  std::error_code ec;
  std::filesystem::create_directories(config.logDirectory, ec);
  std::filesystem::create_directories(config.archive.archiveDirectory, ec);

  // Create LogFileManager
  LogFileManager manager(config);

  std::cout << "1. Initializing log file manager..." << std::endl;
  if (!manager.initializeLogFile(config.defaultLogFile)) {
    std::cerr << "Failed to initialize log file manager!" << std::endl;
    return 1;
  }

  std::cout << "2. Writing log messages..." << std::endl;
  for (int i = 1; i <= 10; ++i) {
    std::string message =
        "Demo log message #" + std::to_string(i) +
        " - This is a longer message to help demonstrate file rotation. " +
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
    manager.writeToFile(message);

    std::cout << "   Written message " << i
              << ", current file size: " << manager.getCurrentFileSize()
              << " bytes" << std::endl;

    // Small delay to make the demo visible
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "3. Listing log files..." << std::endl;
  auto files = manager.listLogFiles(false);
  for (const auto &file : files) {
    std::cout << "   File: " << file.filename << " (size: " << file.fileSize
              << " bytes, "
              << (file.isCompressed ? "compressed" : "uncompressed") << ")"
              << std::endl;
  }

  std::cout << "4. Testing rotation..." << std::endl;

  // Test if rotation is needed
  std::cout << "   Current file needs rotation? "
            << (manager.needsRotation() ? "YES" : "NO") << std::endl;

  // Force a rotation to demonstrate
  if (manager.rotateLogFile()) {
    std::cout << "   Successfully performed log rotation" << std::endl;
  }

  std::cout << "5. Testing archiver..." << std::endl;

  // Create a test file to archive
  std::string testFile = "demo_logs/test_archive.log";
  std::ofstream testFileStream(testFile);
  testFileStream << "This is a test file for archiving demonstration."
                 << std::endl;
  testFileStream.close();

  std::cout << "   Created test file: " << testFile << std::endl;

  if (manager.archiveLogFile(testFile)) {
    std::cout << "   Successfully archived test file" << std::endl;
  }

  // List all files including archived ones
  auto allFiles = manager.listLogFiles(true, true);
  std::cout << "   All files (including archived):" << std::endl;
  for (const auto &file : allFiles) {
    std::cout << "     " << file.filename << " (size: " << file.fileSize
              << " bytes, "
              << "type: " << file.getFileType() << ")" << std::endl;
  }

  std::cout << "6. Testing metrics..." << std::endl;
  auto metrics = manager.getMetrics();
  std::cout << "   Files created: " << metrics.totalFilesCreated.load()
            << std::endl;
  std::cout << "   Files rotated: " << metrics.totalFilesRotated.load()
            << std::endl;
  std::cout << "   Bytes written: " << metrics.totalBytesWritten.load()
            << std::endl;
  std::cout << "   Write operations: " << metrics.totalWriteOperations.load()
            << std::endl;

  std::cout << "7. Final cleanup..." << std::endl;
  manager.cleanupTempFiles();
  std::cout << "   Cleaned up temporary files" << std::endl;

  std::cout << "=== Demo Complete ===" << std::endl;
  std::cout << "Check the 'demo_logs' directory to see the created files."
            << std::endl;

  return 0;
}
