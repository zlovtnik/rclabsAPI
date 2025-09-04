#include "../include/template_utils.hpp"
#include "../include/type_definitions.hpp"
#include <iostream>
#include <cassert>
#include <string>

using namespace etl;
using namespace etl::template_utils;

// Test component for template utilities
class TestComponent {};

// Specialize ComponentTrait for TestComponent
namespace etl {
template<>
struct ComponentTrait<TestComponent> {
    static constexpr const char* name = "TestComponent";
};
}

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

int main() {
    std::cout << "Testing Template Utilities..." << std::endl;
    
    // Test 1: Type traits
    static_assert(has_component_trait_v<TestComponent>, "TestComponent should have trait");
    static_assert(is_strong_id_v<JobId>, "JobId should be strong ID");
    static_assert(is_strong_id_v<ConnectionId>, "ConnectionId should be strong ID");
    static_assert(!is_strong_id_v<std::string>, "std::string should not be strong ID");
    std::cout << "✓ Type traits working correctly" << std::endl;
    
    // Test 2: Compile-time string hashing
    auto hash1 = fnv1a_hash("test");
    auto hash2 = fnv1a_hash("test");
    auto hash3 = fnv1a_hash("different");
    assert(hash1 == hash2);
    assert(hash1 != hash3);
    std::cout << "✓ Compile-time string hashing working" << std::endl;
    
    // Test 3: ConfigValue
    ConfigValue<int> config_val;
    assert(!config_val.has_value());
    assert(config_val.get_or(42) == 42);
    
    config_val.set(100);
    assert(config_val.has_value());
    assert(config_val.get() == 100);
    std::cout << "✓ ConfigValue working correctly" << std::endl;
    
    // Test 4: Strong ID to_string conversion
    JobId job_id("test_job_123");
    ConnectionId conn_id("conn_456");
    
    std::string job_str = to_string(job_id);
    std::string conn_str = to_string(conn_id);
    
    assert(job_str == "test_job_123");
    assert(conn_str == "conn_456");
    std::cout << "✓ Strong ID to_string conversion working" << std::endl;
    
    // Test 5: Factory with duplicate registration prevention
    Factory<BaseClass> factory;
    
    // Register types successfully
    factory.register_type<DerivedA>("type_a");
    factory.register_type<DerivedB>("type_b");
    
    // Verify registration worked
    assert(factory.is_registered("type_a"));
    assert(factory.is_registered("type_b"));
    assert(!factory.is_registered("type_c"));
    
    // Test creation
    auto obj_a = factory.create("type_a");
    auto obj_b = factory.create("type_b");
    assert(obj_a != nullptr);
    assert(obj_b != nullptr);
    assert(obj_a->get_type() == "DerivedA");
    assert(obj_b->get_type() == "DerivedB");
    
    // Test duplicate registration throws exception
    bool exception_thrown = false;
    try {
        factory.register_type<DerivedA>("type_a"); // Try to register same key again
    } catch (const std::runtime_error& e) {
        exception_thrown = true;
        std::string error_msg = e.what();
        assert(error_msg.find("key 'type_a' is already registered") != std::string::npos);
    }
    assert(exception_thrown);
    
    std::cout << "✓ Factory duplicate registration prevention working" << std::endl;
    
    std::cout << "All template utility tests passed!" << std::endl;
    return 0;
}