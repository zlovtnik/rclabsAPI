#include "../include/template_utils.hpp"
#include "../include/type_definitions.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace etl;
using namespace etl::template_utils;

// Test component for template utilities
class TestComponent {};

// Specialize ComponentTrait for TestComponent
namespace etl {
template <> struct ComponentTrait<TestComponent> {
  static constexpr const char *name = "TestComponent";
};
} // namespace etl

// Test classes for Factory
class BaseClass {
public:
  virtual ~BaseClass() = default;
  virtual std::string get_type() const = 0;
};

class DerivedA : public BaseClass {
public:
  std::string get_type() const override { return "DerivedA"; }
};

class DerivedB : public BaseClass {
public:
  std::string get_type() const override { return "DerivedB"; }
};

// Test fixture for template utilities
class TemplateUtilsTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Setup common test data if needed
  }
};

TEST_F(TemplateUtilsTest, TypeTraits) {
  EXPECT_TRUE(has_component_trait_v<TestComponent>);
  EXPECT_TRUE(is_strong_id_v<JobId>);
  EXPECT_TRUE(is_strong_id_v<ConnectionId>);
  EXPECT_FALSE(is_strong_id_v<std::string>);
}

TEST_F(TemplateUtilsTest, CompileTimeStringHashing) {
  auto hash1 = fnv1a_hash("test");
  auto hash2 = fnv1a_hash("test");
  auto hash3 = fnv1a_hash("different");

  EXPECT_EQ(hash1, hash2);
  EXPECT_NE(hash1, hash3);
}

TEST_F(TemplateUtilsTest, ConfigValue) {
  ConfigValue<int> config_val;

  EXPECT_FALSE(config_val.has_value());
  EXPECT_EQ(config_val.get_or(42), 42);

  config_val.set(100);
  EXPECT_TRUE(config_val.has_value());
  EXPECT_EQ(config_val.get(), 100);
}

TEST_F(TemplateUtilsTest, StrongIdToString) {
  JobId job_id("test_job_123");
  ConnectionId conn_id("conn_456");

  EXPECT_EQ(to_string(job_id), "test_job_123");
  EXPECT_EQ(to_string(conn_id), "conn_456");
}

class FactoryTest : public ::testing::Test {
protected:
  Factory<BaseClass> factory;

  void SetUp() override {
    factory.register_type<DerivedA>("type_a");
    factory.register_type<DerivedB>("type_b");
  }
};

TEST_F(FactoryTest, Registration) {
  EXPECT_TRUE(factory.is_registered("type_a"));
  EXPECT_TRUE(factory.is_registered("type_b"));
  EXPECT_FALSE(factory.is_registered("type_c"));
}

TEST_F(FactoryTest, Creation) {
  auto obj_a = factory.create("type_a");
  auto obj_b = factory.create("type_b");

  ASSERT_NE(obj_a, nullptr);
  ASSERT_NE(obj_b, nullptr);
  EXPECT_EQ(obj_a->get_type(), "DerivedA");
  EXPECT_EQ(obj_b->get_type(), "DerivedB");
}

TEST_F(FactoryTest, DuplicateRegistrationThrows) {
  EXPECT_THROW(
      { factory.register_type<DerivedA>("type_a"); }, std::runtime_error);
}