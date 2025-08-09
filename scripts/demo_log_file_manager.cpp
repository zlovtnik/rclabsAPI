#include "log_file_manager.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== LogFileManager Demo ===" << std::endl;
    
    // Create configuration
    FileConfig config;
    config.logFile = "demo_logs/demo.log";
    config.archiveDirectory = "demo_logs/archive";
    config.enableFileOutput = true;
    config.maxFileSize = 1024; // 1KB for demonstration
    config.maxBackupFiles = 3;
    config.enableRotation = true;
    config.enableHistoricalAccess = true;
    config.enableLogIndexing = true;
    config.compressOldLogs = true;
    config.compressionFormat = "gzip";
    
    // Create LogFileManager
    LogFileManager manager(config);
    
    std::cout << "1. Initializing log file manager..." << std::endl;
    if (!manager.initializeFile()) {
        std::cerr << "Failed to initialize log file manager!" << std::endl;
        return 1;
    }
    
    std::cout << "2. Writing log messages..." << std::endl;
    for (int i = 1; i <= 10; ++i) {
        std::string message = "Demo log message #" + std::to_string(i) + 
                             " - This is a longer message to help demonstrate file rotation. " +
                             "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
        manager.writeToFile(message);
        
        std::cout << "   Written message " << i << ", current file size: " 
                  << manager.getCurrentFileSize() << " bytes" << std::endl;
        
        // Small delay to make the demo visible
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "3. Listing log files..." << std::endl;
    auto files = manager.listLogFiles(false);
    for (const auto& file : files) {
        std::cout << "   File: " << file.filename 
                  << " (size: " << file.fileSize << " bytes, "
                  << (file.isCompressed ? "compressed" : "uncompressed") << ")" << std::endl;
    }
    
    std::cout << "4. Testing rotation policies..." << std::endl;
    
    // Test size-based policy
    auto sizePolicy = std::make_unique<SizeBasedRotationPolicy>(500);
    auto now = std::chrono::system_clock::now();
    
    std::cout << "   Size-based policy (500 bytes):" << std::endl;
    std::cout << "     Should rotate 600 bytes? " << (sizePolicy->shouldRotate(600, now) ? "YES" : "NO") << std::endl;
    std::cout << "     Should rotate 300 bytes? " << (sizePolicy->shouldRotate(300, now) ? "YES" : "NO") << std::endl;
    
    // Test time-based policy
    auto timePolicy = std::make_unique<TimeBasedRotationPolicy>(std::chrono::hours(1));
    auto twoHoursAgo = now - std::chrono::hours(2);
    
    std::cout << "   Time-based policy (1 hour):" << std::endl;
    std::cout << "     Should rotate after 2 hours? " << (timePolicy->shouldRotate(100, twoHoursAgo) ? "YES" : "NO") << std::endl;
    std::cout << "     Should rotate after 30 minutes? " << (timePolicy->shouldRotate(100, now - std::chrono::minutes(30)) ? "YES" : "NO") << std::endl;
    
    std::cout << "5. Testing archiver..." << std::endl;
    LogFileArchiver archiver(config);
    
    // Create a test file to archive
    std::string testFile = "demo_logs/test_archive.log";
    std::ofstream testFileStream(testFile);
    testFileStream << "This is a test file for archiving demonstration." << std::endl;
    testFileStream.close();
    
    std::cout << "   Created test file: " << testFile << std::endl;
    
    if (archiver.archiveFile(testFile, config.archiveDirectory)) {
        std::cout << "   Successfully archived test file" << std::endl;
    }
    
    // List archived files
    auto archivedFiles = archiver.listArchivedFiles(config.archiveDirectory);
    std::cout << "   Archived files:" << std::endl;
    for (const auto& file : archivedFiles) {
        std::cout << "     " << file.filename 
                  << " (size: " << file.fileSize << " bytes)" << std::endl;
    }
    
    std::cout << "6. Testing indexer..." << std::endl;
    LogFileIndexer indexer(config);
    
    indexer.indexFile("demo_logs/demo.log");
    indexer.indexFile("demo_logs/demo.log.1");
    
    std::cout << "   Indexed log files" << std::endl;
    
    std::cout << "7. Final cleanup..." << std::endl;
    manager.cleanupOldFiles();
    std::cout << "   Cleaned up old files" << std::endl;
    
    std::cout << "=== Demo Complete ===" << std::endl;
    std::cout << "Check the 'demo_logs' directory to see the created files." << std::endl;
    
    return 0;
}
