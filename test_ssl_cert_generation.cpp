#include "ssl_manager.hpp"
#include <filesystem>
#include <iostream>
#include <logger.hpp>

int main() {
  // Initialize logger
  Logger::getInstance().configure(LogConfig{});
  Logger::getInstance().enableConsoleOutput(true);
  Logger::getInstance().setLogLevel(LogLevel::INFO);

  // Test SSL manager certificate generation
  ETLPlus::SSL::SSLManager::SSLConfig config;
  config.enableSSL = true;
  config.enableHSTS = true;
  config.hstsIncludeSubDomains = true;
  config.hstsPreload = true;

  ETLPlus::SSL::SSLManager manager(config);

  // Create a temporary directory for testing
  std::string testDir = "/tmp/ssl_test_" + std::to_string(time(nullptr));
  std::filesystem::create_directories(testDir);

  std::cout << "Testing certificate generation in: " << testDir << std::endl;

  auto result = manager.generateSelfSignedCertificate(testDir);

  if (!result.success) {
    std::cout << "ERROR: " << result.errorMessage << std::endl;
    return 1;
  }

  // Check if files were created
  std::string certPath = testDir + "/server.crt";
  std::string keyPath = testDir + "/server.key";

  if (std::filesystem::exists(certPath)) {
    std::cout << "✅ Certificate file created: " << certPath << std::endl;
  } else {
    std::cout << "❌ Certificate file NOT created" << std::endl;
  }

  if (std::filesystem::exists(keyPath)) {
    std::cout << "✅ Private key file created: " << keyPath << std::endl;

    // Check file permissions
    auto perms = std::filesystem::status(keyPath).permissions();
    if ((perms & std::filesystem::perms::owner_read) !=
            std::filesystem::perms::none &&
        (perms & std::filesystem::perms::owner_write) !=
            std::filesystem::perms::none &&
        (perms & std::filesystem::perms::group_read) ==
            std::filesystem::perms::none &&
        (perms & std::filesystem::perms::others_read) ==
            std::filesystem::perms::none) {
      std::cout << "✅ Private key has secure permissions (0600)" << std::endl;
    } else {
      std::cout << "❌ Private key permissions are NOT secure" << std::endl;
    }
  } else {
    std::cout << "❌ Private key file NOT created" << std::endl;
  }

  // Show warnings
  for (const auto &warning : result.warnings) {
    std::cout << "WARNING: " << warning << std::endl;
  }

  // Clean up
  std::filesystem::remove_all(testDir);

  std::cout << "\nSafe certificate generation test completed!" << std::endl;
  return 0;
}
