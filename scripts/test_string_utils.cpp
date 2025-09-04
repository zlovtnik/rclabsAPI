#include "../include/string_utils.hpp"
#include <iostream>
#include <cassert>
#include <vector>

using namespace etl::string_utils;

int main() {
    std::cout << "Testing String Utilities..." << std::endl;
    
    // Test 1: String trimming
    assert(trim("  hello world  ") == "hello world");
    assert(trim_left("  hello world  ") == "hello world  ");
    assert(trim_right("  hello world  ") == "  hello world");
    std::cout << "✓ String trimming working correctly" << std::endl;
    
    // Test 2: Case-insensitive comparison
    assert(iequals("Hello", "HELLO"));
    assert(iequals("Test", "test"));
    assert(!iequals("Hello", "World"));
    std::cout << "✓ Case-insensitive comparison working" << std::endl;
    
    // Test 3: String starts/ends with
    assert(starts_with("hello world", "hello"));
    assert(ends_with("hello world", "world"));
    assert(istarts_with("Hello World", "hello"));
    assert(iends_with("Hello World", "WORLD"));
    std::cout << "✓ String prefix/suffix checking working" << std::endl;
    
    // Test 4: StringBuilder
    StringBuilder builder;
    builder.append("Hello").append(" ").append("World").append("!");
    assert(builder.str() == "Hello World!");
    
    StringBuilder builder2;
    builder2 << "Number: " << 42 << ", Float: " << 3.14;
    std::string result = builder2.str();
    assert(result.find("Number: 42") != std::string::npos);
    std::cout << "✓ StringBuilder working correctly" << std::endl;
    
    // Test 5: String concatenation
    std::string concat_result = concat("Hello", " ", "World", "!");
    assert(concat_result == "Hello World!");
    std::cout << "✓ String concatenation working" << std::endl;
    
    // Test 6: String splitting
    auto parts = split("a,b,c,d", ',');
    assert(parts.size() == 4);
    assert(parts[0] == "a");
    assert(parts[3] == "d");
    
    auto views = split_view("x|y|z", '|');
    assert(views.size() == 3);
    assert(views[1] == "y");
    std::cout << "✓ String splitting working correctly" << std::endl;
    
    // Test 7: Number conversion
    auto int_result = to_number<int>("123");
    assert(int_result.success);
    assert(int_result.value == 123);
    
    auto float_result = to_number<double>("3.14");
    assert(float_result.success);
    assert(float_result.value > 3.13 && float_result.value < 3.15);
    
    auto invalid_result = to_number<int>("not_a_number");
    assert(!invalid_result.success);
    std::cout << "✓ Number conversion working correctly" << std::endl;
    
    // Test 8: String validation
    assert(is_numeric("123"));
    assert(is_numeric("3.14"));
    assert(is_integer("123"));
    assert(!is_integer("3.14"));
    assert(is_alpha("hello"));
    assert(!is_alpha("hello123"));
    assert(is_alphanumeric("hello123"));
    std::cout << "✓ String validation working correctly" << std::endl;
    
    // Test 9: Case conversion
    assert(to_lower("HELLO") == "hello");
    assert(to_upper("hello") == "HELLO");
    assert(to_title_case("hello world") == "Hello World");
    std::cout << "✓ Case conversion working correctly" << std::endl;
    
    // Test 10: String replacement
    assert(replace_all("hello world hello", "hello", "hi") == "hi world hi");
    assert(replace_first("hello world hello", "hello", "hi") == "hi world hello");
    std::cout << "✓ String replacement working correctly" << std::endl;
    
    std::cout << "All string utility tests passed!" << std::endl;
    return 0;
}