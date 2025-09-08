#include <gtest/gtest.h>
#include "type_definitions.hpp"
#include "template_utils.hpp"
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <type_traits>

namespace etl {

// Test fixture for type safety tests
class TypeSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test StrongId basic functionality
TEST_F(TypeSafetyTest, StrongIdConstruction) {
    JobId jobId("job_123");
    ConnectionId connId("conn_456");
    UserId userId("user_789");

    EXPECT_EQ(jobId.value(), "job_123");
    EXPECT_EQ(connId.value(), "conn_456");
    EXPECT_EQ(userId.value(), "user_789");
}

TEST_F(TypeSafetyTest, StrongIdEmptyConstruction) {
    EXPECT_THROW(JobId(""), std::invalid_argument);
    EXPECT_THROW(ConnectionId(""), std::invalid_argument);
    EXPECT_THROW(UserId(""), std::invalid_argument);
}

TEST_F(TypeSafetyTest, StrongIdEquality) {
    JobId id1("test_123");
    JobId id2("test_123");
    JobId id3("test_456");

    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, id3);
    EXPECT_NE(id2, id3);
}

TEST_F(TypeSafetyTest, StrongIdOrdering) {
    JobId id1("aaa");
    JobId id2("bbb");
    JobId id3("ccc");

    EXPECT_LT(id1, id2);
    EXPECT_LT(id2, id3);
    EXPECT_LT(id1, id3);

    // Reverse comparisons
    EXPECT_GT(id2, id1);
    EXPECT_GT(id3, id2);
    EXPECT_GT(id3, id1);
}

TEST_F(TypeSafetyTest, StrongIdTypeSafety) {
    JobId jobId("job_123");
    ConnectionId connId("conn_456");

    // These should not compile (type safety test)
    // jobId = connId;  // Should be compilation error
    // if (jobId == connId) {}  // Should be compilation error

    // But these should work
    EXPECT_EQ(jobId, jobId);
    EXPECT_EQ(connId, connId);
}

// Test StrongId hashing
TEST_F(TypeSafetyTest, StrongIdHashing) {
    JobId id1("test_value");
    JobId id2("test_value");
    JobId id3("different_value");

    std::hash<JobId> hasher;

    EXPECT_EQ(hasher(id1), hasher(id2));
    EXPECT_NE(hasher(id1), hasher(id3));

    // Test use in unordered containers
    std::unordered_set<JobId> idSet;
    idSet.insert(id1);
    idSet.insert(id2);
    idSet.insert(id3);

    EXPECT_EQ(idSet.size(), 2); // id1 and id2 are the same, id3 is different
    EXPECT_TRUE(idSet.count(id1));
    EXPECT_TRUE(idSet.count(id2));
    EXPECT_TRUE(idSet.count(id3));
}

TEST_F(TypeSafetyTest, StrongIdInUnorderedMap) {
    std::unordered_map<JobId, std::string> idMap;

    JobId id1("key1");
    JobId id2("key2");

    idMap[id1] = "value1";
    idMap[id2] = "value2";

    EXPECT_EQ(idMap.size(), 2);
    EXPECT_EQ(idMap[id1], "value1");
    EXPECT_EQ(idMap[id2], "value2");
}

// Test IdGenerator
TEST_F(TypeSafetyTest, IdGenerator) {
    JobId jobId = IdGenerator::generateJobId();
    ConnectionId connId = IdGenerator::generateConnectionId();
    UserId userId = IdGenerator::generateUserId();

    // Generated IDs should not be empty
    EXPECT_FALSE(jobId.value().empty());
    EXPECT_FALSE(connId.value().empty());
    EXPECT_FALSE(userId.value().empty());

    // Generated IDs should be unique (with high probability)
    JobId jobId2 = IdGenerator::generateJobId();
    ConnectionId connId2 = IdGenerator::generateConnectionId();
    UserId userId2 = IdGenerator::generateUserId();

    EXPECT_NE(jobId, jobId2);
    EXPECT_NE(connId, connId2);
    EXPECT_NE(userId, userId2);
}

// Test type traits
TEST_F(TypeSafetyTest, TypeTraits) {
    // Test is_strong_id trait
    EXPECT_TRUE(template_utils::is_strong_id_v<JobId>);
    EXPECT_TRUE(template_utils::is_strong_id_v<ConnectionId>);
    EXPECT_TRUE(template_utils::is_strong_id_v<UserId>);

    EXPECT_FALSE(template_utils::is_strong_id_v<std::string>);
    EXPECT_FALSE(template_utils::is_strong_id_v<int>);
    EXPECT_FALSE(template_utils::is_strong_id_v<double>);

    // Test strong_id_value_type trait
    EXPECT_TRUE((std::is_same_v<template_utils::strong_id_value_type_t<JobId>, std::string>));
    EXPECT_TRUE((std::is_same_v<template_utils::strong_id_value_type_t<ConnectionId>, std::string>));
    EXPECT_TRUE((std::is_same_v<template_utils::strong_id_value_type_t<UserId>, std::string>));
}

// Test compile-time string hashing
TEST_F(TypeSafetyTest, CompileTimeStringHash) {
    using namespace template_utils;

    constexpr auto hash1 = fnv1a_hash("test_string");
    constexpr auto hash2 = fnv1a_hash("test_string");
    constexpr auto hash3 = fnv1a_hash("different_string");

    EXPECT_EQ(hash1, hash2);
    EXPECT_NE(hash1, hash3);

    // Test with string_literal
    constexpr string_literal testLiteral("compile_time_test");
    constexpr auto literalHash = testLiteral.hash();
    EXPECT_EQ(literalHash, fnv1a_hash("compile_time_test"));
}

// Test ConfigValue template
TEST_F(TypeSafetyTest, ConfigValue) {
    template_utils::ConfigValue<int> intConfig(42);
    template_utils::ConfigValue<std::string> stringConfig("test_value");

    EXPECT_EQ(intConfig.get(), 42);
    EXPECT_EQ(stringConfig.get(), "test_value");

    // Test default construction
    template_utils::ConfigValue<double> doubleConfig;
    EXPECT_THROW(doubleConfig.get(), std::runtime_error);

    // Test assignment
    doubleConfig = template_utils::ConfigValue<double>(3.14);
    EXPECT_DOUBLE_EQ(doubleConfig.get(), 3.14);
}

TEST_F(TypeSafetyTest, ConfigValueTypeSafety) {
    template_utils::ConfigValue<int> intConfig(42);
    template_utils::ConfigValue<std::string> stringConfig("test");

    // These should work
    int newInt = intConfig.get();
    std::string newString = stringConfig.get();

    EXPECT_EQ(newInt, 42);
    EXPECT_EQ(newString, "test");

    // Type mismatches should be caught at compile time
    // std::string wrongType = intConfig.get();  // Should not compile
    // int wrongType2 = stringConfig.get();      // Should not compile
}

// Test Hana-based type list operations
TEST_F(TypeSafetyTest, HanaTypeList) {
    // Test compile-time type checking with Hana
    using JobIdType = boost::hana::type<JobId>;
    using ConnectionIdType = boost::hana::type<ConnectionId>;
    using UserIdType = boost::hana::type<UserId>;
    using StringType = boost::hana::type<std::string>;
    using IntType = boost::hana::type<int>;

    // These should compile and work
    EXPECT_TRUE(JobIdType{} == JobIdType{});
    EXPECT_TRUE(ConnectionIdType{} == ConnectionIdType{});
    EXPECT_TRUE(UserIdType{} == UserIdType{});

    // Different types should not be equal
    EXPECT_FALSE(JobIdType{} == StringType{});
    EXPECT_FALSE(ConnectionIdType{} == IntType{});

    SUCCEED();
}

// Test component trait detection
TEST_F(TypeSafetyTest, ComponentTraitDetection) {
    // Test existing component traits
    EXPECT_TRUE(template_utils::has_component_trait_v<class AuthManager>);
    EXPECT_TRUE(template_utils::has_component_trait_v<class ConfigManager>);
    EXPECT_TRUE(template_utils::has_component_trait_v<class DatabaseManager>);

    // Test non-component types
    EXPECT_FALSE(template_utils::has_component_trait_v<std::string>);
    EXPECT_FALSE(template_utils::has_component_trait_v<int>);
    EXPECT_FALSE(template_utils::has_component_trait_v<JobId>);
}

// Test string map and set type aliases
TEST_F(TypeSafetyTest, StringMapAndSet) {
    StringMap stringMap;
    StringSet stringSet;

    // Test StringMap
    stringMap["key1"] = "value1";
    stringMap["key2"] = "value2";

    EXPECT_EQ(stringMap.size(), 2);
    EXPECT_EQ(stringMap["key1"], "value1");
    EXPECT_EQ(stringMap["key2"], "value2");

    // Test StringSet
    stringSet.insert("item1");
    stringSet.insert("item2");
    stringSet.insert("item1"); // Duplicate

    EXPECT_EQ(stringSet.size(), 2);
    EXPECT_TRUE(stringSet.count("item1"));
    EXPECT_TRUE(stringSet.count("item2"));
    EXPECT_FALSE(stringSet.count("item3"));
}

// Test transparent string hashing
TEST_F(TypeSafetyTest, TransparentStringHash) {
    TransparentStringHash hasher;

    // Test basic hashing
    size_t hash1 = hasher("test_string");
    size_t hash2 = hasher(std::string("test_string"));
    size_t hash3 = hasher("different_string");

    EXPECT_EQ(hash1, hash2);
    EXPECT_NE(hash1, hash3);

    // Test with containers
    std::unordered_map<std::string, int, TransparentStringHash, std::equal_to<>> map;
    map["key1"] = 1;
    map["key2"] = 2;

    EXPECT_EQ(map["key1"], 1);
    EXPECT_EQ(map["key2"], 2);

    // Test heterogeneous lookup
    EXPECT_EQ(map.find("key1")->second, 1);
    EXPECT_EQ(map.find(std::string("key2"))->second, 2);
}

// Test StrongId copy and move operations
TEST_F(TypeSafetyTest, StrongIdCopyAndMove) {
    JobId original("original_id");

    // Test copy constructor
    JobId copy = original;
    EXPECT_EQ(copy, original);
    EXPECT_EQ(copy.value(), "original_id");

    // Test copy assignment
    JobId copyAssign("different_id");
    copyAssign = original;
    EXPECT_EQ(copyAssign, original);
    EXPECT_EQ(copyAssign.value(), "original_id");

    // Test move constructor
    JobId moveSource("move_source_id");
    std::string originalValue = moveSource.value();
    JobId moveDest = std::move(moveSource);
    EXPECT_EQ(moveDest.value(), originalValue);

    // Test move assignment
    JobId moveAssignSource("move_assign_source");
    JobId moveAssignDest("move_assign_dest");
    originalValue = moveAssignSource.value();
    moveAssignDest = std::move(moveAssignSource);
    EXPECT_EQ(moveAssignDest.value(), originalValue);
}

// Test StrongId with complex operations
TEST_F(TypeSafetyTest, StrongIdComplexOperations) {
    std::vector<JobId> ids;
    ids.emplace_back("job_001");
    ids.emplace_back("job_002");
    ids.emplace_back("job_003");

    // Test sorting
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids[0].value(), "job_001");
    EXPECT_EQ(ids[1].value(), "job_002");
    EXPECT_EQ(ids[2].value(), "job_003");

    // Test reverse sorting
    std::sort(ids.rbegin(), ids.rend());
    EXPECT_EQ(ids[0].value(), "job_003");
    EXPECT_EQ(ids[1].value(), "job_002");
    EXPECT_EQ(ids[2].value(), "job_001");
}

// Test type safety at compile time with SFINAE
TEST_F(TypeSafetyTest, CompileTimeTypeSafety) {
    // Test that we can use constexpr with our utilities
    constexpr auto hashValue = template_utils::fnv1a_hash("compile_time_test");
    EXPECT_NE(hashValue, 0);

    // Test that type traits work at compile time
    static_assert(template_utils::is_strong_id_v<JobId>);
    static_assert(!template_utils::is_strong_id_v<std::string>);
    static_assert(template_utils::has_component_trait_v<class AuthManager>);
    static_assert(!template_utils::has_component_trait_v<int>);
}

// Test error handling with StrongId
TEST_F(TypeSafetyTest, StrongIdErrorHandling) {
    // Test that exceptions are thrown for invalid IDs
    EXPECT_THROW(JobId(""), std::invalid_argument);
    EXPECT_THROW(ConnectionId(""), std::invalid_argument);
    EXPECT_THROW(UserId(""), std::invalid_argument);

    // Test that valid IDs work
    EXPECT_NO_THROW(JobId("valid_id"));
    EXPECT_NO_THROW(ConnectionId("valid_connection"));
    EXPECT_NO_THROW(UserId("valid_user"));
}

} // namespace etl
