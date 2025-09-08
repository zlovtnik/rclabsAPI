#pragma once

#include <functional>
#include <string_view>

// Custom transparent hasher for string types
struct TransparentStringHash {
  using is_transparent = void; // Enables heterogeneous lookup

  template <typename StringType>
  std::size_t operator()(const StringType &str) const {
    return std::hash<std::string_view>{}(str);
  }
};
