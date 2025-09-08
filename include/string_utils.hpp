#pragma once

#include <algorithm>
#include <cctype>
#include <cstring>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace etl {
namespace string_utils {

// ============================================================================
// Type Traits for String Joining
// ============================================================================

// Trait to detect if a type has a size() method
template <typename T, typename = void>
struct has_size_method : std::false_type {};

template <typename T>
struct has_size_method<T, decltype(std::declval<T>().size(), void())>
    : std::true_type {};

// Trait to detect string-like types (string, string_view, const char*, or has
// size())
template <typename T> struct is_string_like {
  static constexpr bool value =
      std::is_same_v<std::decay_t<T>, std::string> ||
      std::is_same_v<std::decay_t<T>, std::string_view> ||
      std::is_same_v<std::decay_t<T>, const char *> ||
      (has_size_method<T>::value && !std::is_arithmetic_v<T>);
};

// ============================================================================
// String View Utilities for Performance
// ============================================================================

/**
 * Efficient string trimming using string_view to avoid allocations
 */
std::string_view trim_left(std::string_view str) noexcept;
std::string_view trim_right(std::string_view str) noexcept;
std::string_view trim(std::string_view str) noexcept;

/**
 * Case-insensitive string comparison using string_view
 */
bool iequals(std::string_view lhs, std::string_view rhs) noexcept;

/**
 * Check if string starts/ends with prefix/suffix (case-sensitive and
 * insensitive)
 */
bool starts_with(std::string_view str, std::string_view prefix) noexcept;
bool ends_with(std::string_view str, std::string_view suffix) noexcept;
bool istarts_with(std::string_view str, std::string_view prefix) noexcept;
bool iends_with(std::string_view str, std::string_view suffix) noexcept;

/**
 * Find substring with case-insensitive option
 */
std::size_t find(std::string_view str, std::string_view substr,
                 std::size_t pos = 0, bool case_sensitive = true) noexcept;

// ============================================================================
// Optimized String Concatenation
// ============================================================================

/**
 * String builder for efficient concatenation
 * Avoids multiple allocations by pre-calculating required size
 */
class StringBuilder {
public:
  StringBuilder() = default;
  explicit StringBuilder(std::size_t reserve_size);

  // Append various types efficiently
  StringBuilder &append(std::string_view str);
  StringBuilder &append(const std::string &str);
  StringBuilder &append(const char *str);
  StringBuilder &append(char c);

  template <typename T> StringBuilder &append(const T &value) {
    if constexpr (std::is_arithmetic_v<T>) {
      append(std::to_string(value));
    } else {
      std::stringstream ss;
      ss << value;
      append(ss.str());
    }
    return *this;
  }

  // Operator overloads for convenience
  StringBuilder &operator<<(std::string_view str) { return append(str); }
  StringBuilder &operator<<(const std::string &str) { return append(str); }
  StringBuilder &operator<<(const char *str) { return append(str); }
  StringBuilder &operator<<(char c) { return append(c); }

  template <typename T> StringBuilder &operator<<(const T &value) {
    return append(value);
  }

  // Get result
  std::string str() const;
  std::string_view view() const noexcept;

  // Utility methods
  std::size_t size() const noexcept { return buffer_.size(); }
  std::size_t capacity() const noexcept { return buffer_.capacity(); }
  bool empty() const noexcept { return buffer_.empty(); }
  void clear() noexcept { buffer_.clear(); }
  void reserve(std::size_t size) { buffer_.reserve(size); }

private:
  std::string buffer_;
};

/**
 * Efficient string joining with separator
 */
template <typename Container>
std::string join(const Container &container, std::string_view separator) {
  if (container.empty()) {
    return {};
  }

  using ValueType = typename Container::value_type;

  // Calculate total size to avoid reallocations
  std::size_t total_size = 0;
  std::size_t count = 0;
  bool can_precompute_size = true;

  for (const auto &item : container) {
    if constexpr (is_string_like<ValueType>::value) {
      // String-like types: use size() method or strlen for const char*
      if constexpr (std::is_same_v<std::decay_t<ValueType>, const char *>) {
        total_size += std::strlen(item);
      } else {
        total_size += item.size();
      }
    } else if constexpr (std::is_arithmetic_v<ValueType>) {
      // Arithmetic types: use std::to_string
      total_size += std::to_string(item).size();
    } else {
      // Other types: cannot precompute size efficiently
      can_precompute_size = false;
      break;
    }
    ++count;
  }

  if (count > 1 && can_precompute_size) {
    total_size += separator.size() * (count - 1);
  }

  StringBuilder builder(can_precompute_size ? total_size : 0);
  bool first = true;
  for (const auto &item : container) {
    if (!first) {
      builder.append(separator);
    }
    builder.append(item);
    first = false;
  }

  return builder.str();
}

/**
 * Variadic string concatenation with optimal allocation
 */
template <typename... Args> std::string concat(Args &&...args) {
  // Calculate total size
  std::size_t total_size = 0;
  ((total_size += [](const auto &arg) -> std::size_t {
     if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, std::string>) {
       return arg.size();
     } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>,
                                         std::string_view>) {
       return arg.size();
     } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>,
                                         const char *>) {
       return std::strlen(arg);
     } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, char>) {
       return 1;
     } else if constexpr (std::is_arithmetic_v<std::decay_t<decltype(arg)>>) {
       return std::to_string(arg).size();
     } else {
       return 0; // Fallback
     }
   }(args)),
   ...);

  StringBuilder builder(total_size);
  (builder.append(args), ...);
  return builder.str();
}

// ============================================================================
// String Formatting Utilities
// ============================================================================

/**
 * Simple format function similar to Python's str.format()
 * Replaces {} placeholders with arguments in order
 */
template <typename... Args>
std::string format(std::string_view format_str, Args &&...args) {
  StringBuilder result;
  std::size_t pos = 0;
  std::size_t arg_index = 0;

  auto append_arg = [&](auto &&arg) {
    if constexpr (std::is_arithmetic_v<std::decay_t<decltype(arg)>>) {
      result.append(std::to_string(arg));
    } else {
      result.append(arg);
    }
  };

  while (pos < format_str.size()) {
    std::size_t placeholder_pos = format_str.find("{}", pos);

    if (placeholder_pos == std::string_view::npos) {
      // No more placeholders, append rest of string
      result.append(format_str.substr(pos));
      break;
    }

    // Append text before placeholder
    result.append(format_str.substr(pos, placeholder_pos - pos));

    // Append argument if available
    if (arg_index < sizeof...(args)) {
      ((arg_index == 0
            ? (append_arg(args), ++arg_index)
            : (--arg_index == 0 ? (append_arg(args), ++arg_index) : 0)),
       ...);
    }

    pos = placeholder_pos + 2; // Skip "{}"
  }

  return result.str();
}

/**
 * Printf-style formatting wrapper for type safety
 */
template <typename... Args>
std::string sprintf(const char *format, Args &&...args) {
  // Calculate required buffer size
  int size = std::snprintf(nullptr, 0, format, args...);
  if (size <= 0) {
    return {};
  }

  std::string result(size, '\0');
  std::snprintf(result.data(), size + 1, format, args...);
  return result;
}

// ============================================================================
// String Splitting and Parsing
// ============================================================================

/**
 * Efficient string splitting using string_view to avoid allocations
 */
std::vector<std::string_view> split_view(std::string_view str, char delimiter);
std::vector<std::string_view> split_view(std::string_view str,
                                         std::string_view delimiter);

/**
 * String splitting that returns actual strings
 */
std::vector<std::string> split(std::string_view str, char delimiter);
std::vector<std::string> split(std::string_view str,
                               std::string_view delimiter);

/**
 * Split with maximum number of parts
 */
std::vector<std::string> split(std::string_view str, char delimiter,
                               std::size_t max_parts);

// ============================================================================
// String Validation and Conversion
// ============================================================================

/**
 * Safe string to number conversion with validation
 */
template <typename T> struct ConversionResult {
  T value{};
  bool success{false};
  std::string error_message;
};

template <typename T>
ConversionResult<T> to_number(std::string_view str) noexcept {
  static_assert(std::is_arithmetic_v<T>, "T must be an arithmetic type");

  ConversionResult<T> result;

  if (str.empty()) {
    result.error_message = "Empty string";
    return result;
  }

  try {
    if constexpr (std::is_integral_v<T>) {
      if constexpr (std::is_signed_v<T>) {
        if constexpr (sizeof(T) <= sizeof(int)) {
          result.value = static_cast<T>(std::stoi(std::string(str)));
        } else if constexpr (sizeof(T) <= sizeof(long)) {
          result.value = static_cast<T>(std::stol(std::string(str)));
        } else {
          result.value = static_cast<T>(std::stoll(std::string(str)));
        }
      } else {
        if constexpr (sizeof(T) <= sizeof(unsigned int)) {
          result.value = static_cast<T>(std::stoul(std::string(str)));
        } else {
          result.value = static_cast<T>(std::stoull(std::string(str)));
        }
      }
    } else {
      if constexpr (std::is_same_v<T, float>) {
        result.value = std::stof(std::string(str));
      } else if constexpr (std::is_same_v<T, double>) {
        result.value = std::stod(std::string(str));
      } else {
        result.value = static_cast<T>(std::stold(std::string(str)));
      }
    }
    result.success = true;
  } catch (const std::exception &e) {
    result.error_message = e.what();
  }

  return result;
}

/**
 * String validation utilities
 */
bool is_numeric(std::string_view str) noexcept;
bool is_integer(std::string_view str) noexcept;
bool is_float(std::string_view str) noexcept;
bool is_alpha(std::string_view str) noexcept;
bool is_alphanumeric(std::string_view str) noexcept;
bool is_whitespace(std::string_view str) noexcept;

// ============================================================================
// Case Conversion Utilities
// ============================================================================

/**
 * Efficient case conversion
 */
std::string to_lower(std::string_view str);
std::string to_upper(std::string_view str);
std::string to_title_case(std::string_view str);

/**
 * In-place case conversion for existing strings
 */
void to_lower_inplace(std::string &str) noexcept;
void to_upper_inplace(std::string &str) noexcept;

// ============================================================================
// String Replacement Utilities
// ============================================================================

/**
 * Efficient string replacement
 */
std::string replace_all(std::string_view str, std::string_view from,
                        std::string_view to);
std::string replace_first(std::string_view str, std::string_view from,
                          std::string_view to);
std::string replace_last(std::string_view str, std::string_view from,
                         std::string_view to);

/**
 * In-place string replacement for existing strings
 */
void replace_all_inplace(std::string &str, std::string_view from,
                         std::string_view to);

// ============================================================================
// URL and Path Utilities
// ============================================================================

/**
 * URL encoding/decoding
 */
std::string url_encode(std::string_view str);
std::string url_decode(std::string_view str);

/**
 * Path manipulation utilities
 */
std::string normalize_path(std::string_view path);
std::string join_paths(std::string_view path1, std::string_view path2);
std::pair<std::string_view, std::string_view> split_path(std::string_view path);

} // namespace string_utils
} // namespace etl