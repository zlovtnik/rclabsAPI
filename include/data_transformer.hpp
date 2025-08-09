#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct TransformationRule {
  std::string sourceField;
  std::string targetField;
  std::string transformationType;
  std::unordered_map<std::string, std::string> parameters;
};

struct DataRecord {
  std::unordered_map<std::string, std::string> fields;
};

class DataTransformer {
public:
  DataTransformer();

  // Rule management
  void addTransformationRule(const TransformationRule &rule);
  void removeTransformationRule(const std::string &ruleId);
  void clearRules();

  // Data transformation
  std::vector<DataRecord> transform(const std::vector<DataRecord> &inputData);
  DataRecord transformRecord(const DataRecord &record);

  // Validation
  bool validateData(const std::vector<DataRecord> &data);
  std::vector<std::string> getValidationErrors(const DataRecord &record);

private:
  std::vector<TransformationRule> rules_;

  std::string applyTransformation(const std::string &value,
                                  const TransformationRule &rule);
  std::string applyStringTransformation(
      const std::string &value, const std::string &type,
      const std::unordered_map<std::string, std::string> &params);
  std::string applyNumericTransformation(
      const std::string &value, const std::string &type,
      const std::unordered_map<std::string, std::string> &params);
};
