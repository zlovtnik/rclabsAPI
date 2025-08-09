#include "data_transformer.hpp"
#include <iostream>
#include <algorithm>
#include <regex>

DataTransformer::DataTransformer() {
}

void DataTransformer::addTransformationRule(const TransformationRule& rule) {
    rules_.push_back(rule);
    std::cout << "Added transformation rule: " << rule.sourceField << " -> " << rule.targetField << std::endl;
}

void DataTransformer::removeTransformationRule(const std::string& ruleId) {
    // For simplicity, remove by source field name
    rules_.erase(std::remove_if(rules_.begin(), rules_.end(),
        [&ruleId](const TransformationRule& rule) {
            return rule.sourceField == ruleId;
        }), rules_.end());
}

void DataTransformer::clearRules() {
    rules_.clear();
}

std::vector<DataRecord> DataTransformer::transform(const std::vector<DataRecord>& inputData) const {
    std::vector<DataRecord> result;
    result.reserve(inputData.size());
    
    for (const auto& record : inputData) {
        result.push_back(transformRecord(record));
    }
    
    return result;
}

DataRecord DataTransformer::transformRecord(const DataRecord& record) const {
    DataRecord result = record; // Start with original data
    
    for (const auto& rule : rules_) {
        auto it = record.fields.find(rule.sourceField);
        if (it != record.fields.end()) {
            std::string transformedValue = applyTransformation(it->second, rule);
            result.fields[rule.targetField] = transformedValue;
        }
    }
    
    return result;
}

bool DataTransformer::validateData(const std::vector<DataRecord>& data) const {
    for (const auto& record : data) {
        auto errors = getValidationErrors(record);
        if (!errors.empty()) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> DataTransformer::getValidationErrors(const DataRecord& record) const {
    std::vector<std::string> errors;
    
    // Basic validation - check for empty required fields
    for (const auto& rule : rules_) {
        if (rule.parameters.count("required") && rule.parameters.at("required") == "true") {
            auto it = record.fields.find(rule.sourceField);
            if (it == record.fields.end() || it->second.empty()) {
                errors.push_back("Required field '" + rule.sourceField + "' is missing or empty");
            }
        }
    }
    
    return errors;
}

std::string DataTransformer::applyTransformation(const std::string& value, const TransformationRule& rule) const {
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

std::string DataTransformer::applyStringTransformation(const std::string& value, const std::string& type,
                                                     const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& params) const {
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
        result.erase(0, result.find_first_not_of(" \t\n\r"));
        result.erase(result.find_last_not_of(" \t\n\r") + 1);
        return result;
    }
    return value;
}

std::string DataTransformer::applyNumericTransformation(const std::string& value, const std::string& type,
                                                       const std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>& params) const {
    try {
        double numValue = std::stod(value);
        
        if (type == "multiply") {
            auto it = params.find("factor");
            if (it != params.end()) {
                double factor = std::stod(it->second);
                return std::to_string(numValue * factor);
            }
        } else if (type == "add") {
            auto it = params.find("addend");
            if (it != params.end()) {
                double addend = std::stod(it->second);
                return std::to_string(numValue + addend);
            }
        }
    } catch (const std::exception&) {
        // Return original value if transformation fails
        return value;
    }
    
    return value;
}
