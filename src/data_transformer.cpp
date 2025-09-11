#include "data_transformer.hpp"
#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>
#include <iomanip>

// Helper function to format doubles without trailing zeros
static std::string to_string_no_trailing_zeros(double v) {
  std::ostringstream oss;
  oss << std::setprecision(15) << v; // not fixed => no forced trailing zeros
  auto s = oss.str();
  auto dot = s.find('.');
  if (dot != std::string::npos) {
    // trim trailing zeros
    auto last = s.find_last_not_of('0');
    if (last != std::string::npos) {
      if (s[last] == '.') last--;
      s.erase(last + 1);
    }
  }
  return s;
}

DataTransformer::DataTransformer() {}

void DataTransformer::addTransformationRule(const TransformationRule &rule) {
  rules_.push_back(rule);
  std::cout << "Added transformation rule: " << rule.sourceField << " -> "
            << rule.targetField << std::endl;
}

void DataTransformer::removeTransformationRule(const std::string &sourceField) {
  // Removes by source field name
  rules_.erase(std::remove_if(rules_.begin(), rules_.end(),
                              [&sourceField](const TransformationRule &rule) {
                                return rule.sourceField == sourceField;
                              }),
               rules_.end());
}

void DataTransformer::clearRules() { rules_.clear(); }

std::vector<DataRecord>
DataTransformer::transform(const std::vector<DataRecord> &inputData) const {
  std::vector<DataRecord> result;
  result.reserve(inputData.size());

  for (const auto &record : inputData) {
    result.push_back(transformRecord(record));
  }

  return result;
}

DataRecord DataTransformer::transformRecord(const DataRecord &record) const {
  DataRecord result = record; // Start with original data

  for (const auto &rule : rules_) {
    auto it = result.fields.find(rule.sourceField);
    if (it != result.fields.end()) {
      std::string transformedValue = applyTransformation(it->second, rule);
      result.fields[rule.targetField] = transformedValue;
    }
  }

  return result;
}

bool DataTransformer::validateData(const std::vector<DataRecord> &data) const {
  for (const auto &record : data) {
    auto errors = getValidationErrors(record);
    if (!errors.empty()) {
      return false;
    }
  }
  return true;
}

std::vector<std::string>
DataTransformer::getValidationErrors(const DataRecord &record) const {
  std::vector<std::string> errors;

  // Basic validation - check for empty required fields
  for (const auto &rule : rules_) {
    auto itReq = rule.parameters.find("required");
    if (itReq != rule.parameters.end() && itReq->second == "true") {
      auto it = record.fields.find(rule.sourceField);
      if (it == record.fields.end() || it->second.empty()) {
        errors.push_back("Required field '" + rule.sourceField +
                         "' is missing or empty");
      }
    }
  }

  return errors;
}

std::string
DataTransformer::applyTransformation(const std::string &value,
                                     const TransformationRule &rule) const {
  if (rule.transformationType == "uppercase") {
    return applyStringTransformation(value, "uppercase", rule.parameters);
  } else if (rule.transformationType == "lowercase") {
    return applyStringTransformation(value, "lowercase", rule.parameters);
  } else if (rule.transformationType == "trim") {
    return applyStringTransformation(value, "trim", rule.parameters);
  } else if (rule.transformationType == "multiply") {
    return applyNumericTransformation(value, "multiply", rule.parameters);
  } else if (rule.transformationType == "add") {
    return applyNumericTransformation(value, "add", rule.parameters);
  } else {
    return value; // No transformation
  }
}

std::string DataTransformer::applyStringTransformation(
    const std::string &value, const std::string &type,
    const std::unordered_map<std::string, std::string, TransparentStringHash,
                             std::equal_to<>> &params) const {
  if (type == "uppercase") {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
  } else if (type == "lowercase") {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
  } else if (type == "trim") {
    std::string result = value;
    auto start = result.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return std::string{};
    result.erase(0, start);
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    return result;
  }
  return value;
}

std::string DataTransformer::applyNumericTransformation(
    const std::string &value, const std::string &type,
    const std::unordered_map<std::string, std::string, TransparentStringHash,
                             std::equal_to<>> &params) const {
  try {
    double numValue = std::stod(value);

    if (type == "multiply") {
      auto it = params.find("factor");
      if (it != params.end()) {
        double factor = std::stod(it->second);
        return to_string_no_trailing_zeros(numValue * factor);
      }
    } else if (type == "add") {
      auto it = params.find("addend");
      if (it != params.end()) {
        double addend = std::stod(it->second);
        return to_string_no_trailing_zeros(numValue + addend);
      }
    }
  } catch (const std::exception &e) {
    // Log the exception and return original value
    std::cerr << "Numeric transformation failed for value '" << value 
              << "' with type '" << type << "': " << e.what() << std::endl;
    return value;
  }

  return value;
}
