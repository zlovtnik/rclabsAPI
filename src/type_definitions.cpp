#include "type_definitions.hpp"
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace etl {

JobId IdGenerator::generateJobId() {
  return JobId(std::string("job_") + generateUuid());
}

ConnectionId IdGenerator::generateConnectionId() {
  return ConnectionId(std::string("conn_") + generateUuid());
}

UserId IdGenerator::generateUserId() {
  return UserId(std::string("user_") + generateUuid());
}

std::string IdGenerator::generateUuid() {
  // Simple UUID-like generator using random numbers and timestamp
  thread_local std::random_device rd;
  thread_local std::mt19937 gen(rd());
  thread_local std::uniform_int_distribution<> dis(0, 15);

  auto now = std::chrono::high_resolution_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                       now.time_since_epoch())
                       .count();

  std::stringstream ss;
  ss << std::hex << timestamp << "_";

  // Add random component
  for (int i = 0; i < 8; ++i) {
    ss << std::hex << dis(gen);
  }

  return ss.str();
}

} // namespace etl