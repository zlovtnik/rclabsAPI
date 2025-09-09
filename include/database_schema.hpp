#pragma once

#include <string>
#include <vector>

class DatabaseSchema {
public:
  static std::vector<std::string> getCreateTableStatements();
  static std::vector<std::string> getIndexStatements();
  static std::vector<std::string> getInitialDataStatements();
};
