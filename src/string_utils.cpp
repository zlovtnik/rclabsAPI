#include "string_utils.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>

namespace etl {
namespace string_utils {

// ============================================================================
// String View Utilities Implementation
// ============================================================================

std::string_view trim_left(std::string_view str) noexcept {
  auto start = str.find_first_not_of(" \t\n\r\f\v");
  return start == std::string_view::npos ? std::string_view{}
                                         : str.substr(start);
}

std::string_view trim_right(std::string_view str) noexcept {
  auto end = str.find_last_not_of(" \t\n\r\f\v");
  return end == std::string_view::npos ? std::string_view{}
                                       : str.substr(0, end + 1);
}

std::string_view trim(std::string_view str) noexcept {
  return trim_left(trim_right(str));
}

bool iequals(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  return std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
  });
}

bool starts_with(std::string_view str, std::string_view prefix) noexcept {
  return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view str, std::string_view suffix) noexcept {
  return str.size() >= suffix.size() &&
         str.substr(str.size() - suffix.size()) == suffix;
}

bool istarts_with(std::string_view str, std::string_view prefix) noexcept {
  return str.size() >= prefix.size() &&
         iequals(str.substr(0, prefix.size()), prefix);
}

bool iends_with(std::string_view str, std::string_view suffix) noexcept {
  return str.size() >= suffix.size() &&
         iequals(str.substr(str.size() - suffix.size()), suffix);
}

std::size_t find(std::string_view str, std::string_view substr, std::size_t pos,
                 bool case_sensitive) noexcept {
  if (case_sensitive) {
    return str.find(substr, pos);
  }

  // Case-insensitive search
  if (substr.empty())
    return pos;
  if (pos >= str.size())
    return std::string_view::npos;

  for (std::size_t i = pos; i <= str.size() - substr.size(); ++i) {
    if (iequals(str.substr(i, substr.size()), substr)) {
      return i;
    }
  }

  return std::string_view::npos;
}

// ============================================================================
// StringBuilder Implementation
// ============================================================================

StringBuilder::StringBuilder(std::size_t reserve_size) {
  buffer_.reserve(reserve_size);
}

StringBuilder &StringBuilder::append(std::string_view str) {
  buffer_.append(str);
  return *this;
}

StringBuilder &StringBuilder::append(const std::string &str) {
  buffer_.append(str);
  return *this;
}

StringBuilder &StringBuilder::append(const char *str) {
  buffer_.append(str);
  return *this;
}

StringBuilder &StringBuilder::append(char c) {
  buffer_.push_back(c);
  return *this;
}

std::string StringBuilder::str() const { return buffer_; }

std::string_view StringBuilder::view() const noexcept { return buffer_; }

// ============================================================================
// String Splitting Implementation
// ============================================================================

std::vector<std::string_view> split_view(std::string_view str, char delimiter) {
  std::vector<std::string_view> result;

  if (str.empty()) {
    return result;
  }

  std::size_t start = 0;
  std::size_t pos = 0;

  while ((pos = str.find(delimiter, start)) != std::string_view::npos) {
    result.emplace_back(str.substr(start, pos - start));
    start = pos + 1;
  }

  // Add the last part
  result.emplace_back(str.substr(start));

  return result;
}

std::vector<std::string_view> split_view(std::string_view str,
                                         std::string_view delimiter) {
  std::vector<std::string_view> result;

  if (str.empty() || delimiter.empty()) {
    if (!str.empty()) {
      result.emplace_back(str);
    }
    return result;
  }

  std::size_t start = 0;
  std::size_t pos = 0;

  while ((pos = str.find(delimiter, start)) != std::string_view::npos) {
    result.emplace_back(str.substr(start, pos - start));
    start = pos + delimiter.size();
  }

  // Add the last part
  result.emplace_back(str.substr(start));

  return result;
}

std::vector<std::string> split(std::string_view str, char delimiter) {
  auto views = split_view(str, delimiter);
  std::vector<std::string> result;
  result.reserve(views.size());

  for (const auto &view : views) {
    result.emplace_back(view);
  }

  return result;
}

std::vector<std::string> split(std::string_view str,
                               std::string_view delimiter) {
  auto views = split_view(str, delimiter);
  std::vector<std::string> result;
  result.reserve(views.size());

  for (const auto &view : views) {
    result.emplace_back(view);
  }

  return result;
}

std::vector<std::string> split(std::string_view str, char delimiter,
                               std::size_t max_parts) {
  std::vector<std::string> result;

  if (str.empty() || max_parts == 0) {
    return result;
  }

  if (max_parts == 1) {
    result.emplace_back(str);
    return result;
  }

  std::size_t start = 0;
  std::size_t pos = 0;
  std::size_t parts = 0;

  while (parts < max_parts - 1 &&
         (pos = str.find(delimiter, start)) != std::string_view::npos) {
    result.emplace_back(str.substr(start, pos - start));
    start = pos + 1;
    ++parts;
  }

  // Add the remaining part
  result.emplace_back(str.substr(start));

  return result;
}

// ============================================================================
// String Validation Implementation
// ============================================================================

bool is_numeric(std::string_view str) noexcept {
  if (str.empty())
    return false;

  std::size_t start = 0;
  if (str[0] == '+' || str[0] == '-') {
    start = 1;
    if (str.size() == 1)
      return false;
  }

  bool has_dot = false;
  bool has_digit = false;
  for (std::size_t i = start; i < str.size(); ++i) {
    if (str[i] == '.') {
      if (has_dot)
        return false; // Multiple dots
      has_dot = true;
    } else if (std::isdigit(static_cast<unsigned char>(str[i]))) {
      has_digit = true;
    } else {
      return false; // Invalid character
    }
  }

  // Must have at least one digit to be considered numeric
  return has_digit;
}

bool is_integer(std::string_view str) noexcept {
  if (str.empty())
    return false;

  std::size_t start = 0;
  if (str[0] == '+' || str[0] == '-') {
    start = 1;
    if (str.size() == 1)
      return false;
  }

  for (std::size_t i = start; i < str.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(str[i]))) {
      return false;
    }
  }

  return true;
}

bool is_float(std::string_view str) noexcept {
  if (str.empty())
    return false;

  std::size_t start = 0;
  if (str[0] == '+' || str[0] == '-') {
    start = 1;
    if (str.size() == 1)
      return false;
  }

  // Find the dot position (relative to the start after sign)
  std::size_t dot_pos = str.find('.', start);
  if (dot_pos == std::string_view::npos)
    return false; // No dot found

  // Check for multiple dots
  if (str.find('.', dot_pos + 1) != std::string_view::npos)
    return false;

  // Dot cannot be the first character (after sign) or the last character
  if (dot_pos == start || dot_pos == str.size() - 1)
    return false;

  // Check that all characters before dot are digits
  for (std::size_t i = start; i < dot_pos; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(str[i]))) {
      return false;
    }
  }

  // Check that all characters after dot are digits
  for (std::size_t i = dot_pos + 1; i < str.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(str[i]))) {
      return false;
    }
  }

  return true;
}

bool is_alpha(std::string_view str) noexcept {
  if (str.empty())
    return false;

  return std::all_of(str.begin(), str.end(), [](char c) {
    return std::isalpha(static_cast<unsigned char>(c));
  });
}

bool is_alphanumeric(std::string_view str) noexcept {
  if (str.empty())
    return false;

  return std::all_of(str.begin(), str.end(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c));
  });
}

bool is_whitespace(std::string_view str) noexcept {
  if (str.empty())
    return true;

  return std::all_of(str.begin(), str.end(), [](char c) {
    return std::isspace(static_cast<unsigned char>(c));
  });
}

// ============================================================================
// Case Conversion Implementation
// ============================================================================

std::string to_lower(std::string_view str) {
  std::string result;
  result.reserve(str.size());

  std::transform(
      str.begin(), str.end(), std::back_inserter(result),
      [](char c) { return std::tolower(static_cast<unsigned char>(c)); });

  return result;
}

std::string to_upper(std::string_view str) {
  std::string result;
  result.reserve(str.size());

  std::transform(
      str.begin(), str.end(), std::back_inserter(result),
      [](char c) { return std::toupper(static_cast<unsigned char>(c)); });

  return result;
}

std::string to_title_case(std::string_view str) {
  std::string result;
  result.reserve(str.size());

  bool capitalize_next = true;
  for (char c : str) {
    if (std::isalpha(static_cast<unsigned char>(c))) {
      if (capitalize_next) {
        result.push_back(std::toupper(static_cast<unsigned char>(c)));
        capitalize_next = false;
      } else {
        result.push_back(std::tolower(static_cast<unsigned char>(c)));
      }
    } else {
      result.push_back(c);
      capitalize_next = std::isspace(static_cast<unsigned char>(c));
    }
  }

  return result;
}

void to_lower_inplace(std::string &str) noexcept {
  std::transform(str.begin(), str.end(), str.begin(), [](char c) {
    return std::tolower(static_cast<unsigned char>(c));
  });
}

void to_upper_inplace(std::string &str) noexcept {
  std::transform(str.begin(), str.end(), str.begin(), [](char c) {
    return std::toupper(static_cast<unsigned char>(c));
  });
}

// ============================================================================
// String Replacement Implementation
// ============================================================================

std::string replace_all(std::string_view str, std::string_view from,
                        std::string_view to) {
  if (from.empty()) {
    return std::string(str);
  }

  std::string result;
  result.reserve(str.size()); // Initial reservation

  std::size_t start = 0;
  std::size_t pos = 0;

  while ((pos = str.find(from, start)) != std::string_view::npos) {
    result.append(str.substr(start, pos - start));
    result.append(to);
    start = pos + from.size();
  }

  result.append(str.substr(start));
  return result;
}

std::string replace_first(std::string_view str, std::string_view from,
                          std::string_view to) {
  if (from.empty()) {
    return std::string(str);
  }

  std::size_t pos = str.find(from);
  if (pos == std::string_view::npos) {
    return std::string(str);
  }

  std::string result;
  result.reserve(str.size() + to.size() - from.size());

  result.append(str.substr(0, pos));
  result.append(to);
  result.append(str.substr(pos + from.size()));

  return result;
}

std::string replace_last(std::string_view str, std::string_view from,
                         std::string_view to) {
  if (from.empty()) {
    return std::string(str);
  }

  std::size_t pos = str.rfind(from);
  if (pos == std::string_view::npos) {
    return std::string(str);
  }

  std::string result;
  result.reserve(str.size() + to.size() - from.size());

  result.append(str.substr(0, pos));
  result.append(to);
  result.append(str.substr(pos + from.size()));

  return result;
}

void replace_all_inplace(std::string &str, std::string_view from,
                         std::string_view to) {
  if (from.empty()) {
    return;
  }

  std::size_t pos = 0;
  while ((pos = str.find(from, pos)) != std::string::npos) {
    str.replace(pos, from.size(), to);
    pos += to.size();
  }
}

// ============================================================================
// URL and Path Utilities Implementation
// ============================================================================

std::string url_encode(std::string_view str) {
  std::ostringstream encoded;
  encoded.fill('0');
  encoded << std::hex;

  for (char c : str) {
    // Keep alphanumeric and some safe characters
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      encoded << c;
    } else {
      encoded << std::uppercase;
      encoded << '%' << std::setw(2)
              << static_cast<int>(static_cast<unsigned char>(c));
      encoded << std::nouppercase;
    }
  }

  return encoded.str();
}

std::string url_decode(std::string_view str) {
  std::string decoded;
  decoded.reserve(str.size());

  for (std::size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '%' && i + 2 < str.size()) {
      // Parse hex digits
      char hex_str[3] = {str[i + 1], str[i + 2], '\0'};
      char *end_ptr;
      long value = std::strtol(hex_str, &end_ptr, 16);

      if (end_ptr == hex_str + 2) { // Successfully parsed
        decoded.push_back(static_cast<char>(value));
        i += 2; // Skip the hex digits
      } else {
        decoded.push_back(str[i]); // Invalid encoding, keep as-is
      }
    } else if (str[i] == '+') {
      decoded.push_back(' '); // URL encoding uses + for space
    } else {
      decoded.push_back(str[i]);
    }
  }

  return decoded;
}

std::string normalize_path(std::string_view path) {
  if (path.empty()) {
    return ".";
  }

  std::vector<std::string> components;
  auto parts = split_view(path, '/');

  for (const auto &part : parts) {
    if (part.empty() || part == ".") {
      continue; // Skip empty and current directory
    } else if (part == "..") {
      if (!components.empty() && components.back() != "..") {
        components.pop_back(); // Go up one directory
      } else if (path[0] != '/') {
        components.emplace_back(part); // Relative path, keep ..
      }
    } else {
      components.emplace_back(part);
    }
  }

  std::string result;
  if (path[0] == '/') {
    result = "/";
  }

  result += join(components, "/");

  return result.empty() ? "." : result;
}

std::string join_paths(std::string_view path1, std::string_view path2) {
  if (path1.empty()) {
    return std::string(path2);
  }
  if (path2.empty()) {
    return std::string(path1);
  }

  bool path1_ends_with_slash = path1.back() == '/';
  bool path2_starts_with_slash = path2.front() == '/';

  if (path1_ends_with_slash && path2_starts_with_slash) {
    return concat(path1.substr(0, path1.size() - 1), path2);
  } else if (!path1_ends_with_slash && !path2_starts_with_slash) {
    return concat(path1, "/", path2);
  } else {
    return concat(path1, path2);
  }
}

std::pair<std::string_view, std::string_view>
split_path(std::string_view path) {
  std::size_t pos = path.find_last_of('/');

  if (pos == std::string_view::npos) {
    return {"", path}; // No directory part
  }

  if (pos == 0) {
    return {"/", path.substr(1)}; // Root directory
  }

  return {path.substr(0, pos), path.substr(pos + 1)};
}

} // namespace string_utils
} // namespace etl