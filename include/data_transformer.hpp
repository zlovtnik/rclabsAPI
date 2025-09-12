#pragma once

#include "transparent_string_hash.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct TransformationRule {
  std::string sourceField;
  std::string targetField;
  std::string transformationType;
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
      parameters;
};

struct DataRecord {
  std::unordered_map<std::string, std::string, TransparentStringHash,
                     std::equal_to<>>
      fields;
};

class DataTransformer {
public:
  DataTransformer();

  // Rule management
  void addTransformationRule(const TransformationRule &rule);
  void removeTransformationRule(const std::string &sourceField);
  void clearRules();

  // Data transformation
  std::vector<DataRecord>
  transform(const std::vector<DataRecord> &inputData) const;
  DataRecord transformRecord(const DataRecord &record) const;

  // Validation
  bool validateData(const std::vector<DataRecord> &data) const;
  std::vector<std::string> getValidationErrors(const DataRecord &record) const;

private:
  std::vector<TransformationRule> rules_;

  std::string applyTransformation(const std::string &value,
                                  const TransformationRule &rule) const;
  std::string applyStringTransformation(
      const std::string &value, const std::string &type,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &params) const;
  std::string applyNumericTransformation(
      const std::string &value, const std::string &type,
      const std::unordered_map<std::string, std::string, TransparentStringHash,
                               std::equal_to<>> &params) const;
};
