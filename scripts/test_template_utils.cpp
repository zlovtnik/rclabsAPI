#include "../include/template_utils.hpp"
#include "../include/type_definitions.hpp"
#include <iostream>
#include <cassert>

using namespace etl;
using namespace etl::template_utils;

// Test component for template utilities
class TestComponent {};

// Specialize ComponentTrait for TestComponent
template<>
struct etl::ComponentTrait<TestComponent> {
    static constexpr const char* name = "TestComponent";
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
    
    std::cout << "All template utility tests passed!" << std::endl;
    return 0;
}