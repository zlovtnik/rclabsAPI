#include <iostream>
#include <optional>
#include <chrono>
#include <string>

int main() {
    std::cout << "Testing std::optional TTL functionality" << std::endl;
    std::cout << "======================================" << std::endl;

    // Test 1: Default behavior (std::nullopt)
    std::optional<std::chrono::seconds> noTTL = std::nullopt;
    std::cout << "1. No TTL specified: " << (noTTL.has_value() ? "has value" : "nullopt") << std::endl;

    // Test 2: Explicit TTL value
    std::optional<std::chrono::seconds> withTTL = std::chrono::seconds(300);
    std::cout << "2. TTL specified: " << (withTTL.has_value() ? std::to_string(withTTL.value().count()) + "s" : "nullopt") << std::endl;

    // Test 3: Zero TTL (should still be treated as a value)
    std::optional<std::chrono::seconds> zeroTTL = std::chrono::seconds(0);
    std::cout << "3. Zero TTL: " << (zeroTTL.has_value() ? std::to_string(zeroTTL.value().count()) + "s" : "nullopt") << std::endl;

    std::cout << "\n✅ std::optional TTL functionality test completed!" << std::endl;
    return 0;
}