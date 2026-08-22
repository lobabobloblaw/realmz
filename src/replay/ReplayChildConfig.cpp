#include "replay/ReplayChildConfig.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace realmz::replay {

namespace {

constexpr std::size_t kMaximumJsonDepth = 32;
constexpr std::size_t kMaximumArguments = 32;
constexpr std::size_t kMaximumIdentifierLength = 64;
constexpr std::size_t kMaximumArgumentStringLength = 1024;
constexpr std::size_t kMaximumPathBytes = 4096;
constexpr std::int64_t kMaximumExactJsonInteger = 9007199254740991LL;
constexpr std::size_t kTokenLength = 32;
constexpr std::size_t kRngTokenLength = 16;

struct JsonNumber final {
  std::string spelling;
};

class JsonValue final {
public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;
  using Storage = std::variant<
      std::nullptr_t,
      bool,
      JsonNumber,
      std::string,
      Array,
      Object>;

  explicit JsonValue(std::nullptr_t value) : value_(value) {}
  explicit JsonValue(bool value) : value_(value) {}
  explicit JsonValue(JsonNumber value) : value_(std::move(value)) {}
  explicit JsonValue(std::string value) : value_(std::move(value)) {}
  explicit JsonValue(Array value) : value_(std::move(value)) {}
  explicit JsonValue(Object value) : value_(std::move(value)) {}

  template <typename T>
  [[nodiscard]] const T* get_if() const noexcept {
    return std::get_if<T>(&value_);
  }

private:
  Storage value_;
};

[[noreturn]] void config_error(const std::string& message) {
  throw ReplayConfigError(message);
}

void append_utf8(std::string& output, std::uint32_t code_point) {
  if (code_point <= 0x7FU) {
    output.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FFU) {
    output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0xFFFFU) {
    output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0x10FFFFU) {
    output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    config_error("JSON string contains an invalid Unicode code point");
  }
}

[[nodiscard]] std::size_t validate_utf8_and_count(
    std::string_view value,
    std::string_view context,
    bool reject_controls) {
  std::size_t position = 0;
  std::size_t count = 0;
  while (position < value.size()) {
    const auto first = static_cast<unsigned char>(value[position]);
    std::uint32_t code_point = 0;
    std::size_t width = 0;
    if (first <= 0x7FU) {
      code_point = first;
      width = 1;
    } else if (first >= 0xC2U && first <= 0xDFU) {
      code_point = static_cast<std::uint32_t>(first & 0x1FU);
      width = 2;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      code_point = static_cast<std::uint32_t>(first & 0x0FU);
      width = 3;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      code_point = static_cast<std::uint32_t>(first & 0x07U);
      width = 4;
    } else {
      config_error(std::string(context) + " is not valid UTF-8");
    }
    if (position + width > value.size()) {
      config_error(std::string(context) + " is not valid UTF-8");
    }
    for (std::size_t offset = 1; offset < width; ++offset) {
      const auto continuation =
          static_cast<unsigned char>(value[position + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        config_error(std::string(context) + " is not valid UTF-8");
      }
      code_point = (code_point << 6U) |
          static_cast<std::uint32_t>(continuation & 0x3FU);
    }
    if ((width == 3 && first == 0xE0U &&
         static_cast<unsigned char>(value[position + 1]) < 0xA0U) ||
        (width == 3 && first == 0xEDU &&
         static_cast<unsigned char>(value[position + 1]) >= 0xA0U) ||
        (width == 4 && first == 0xF0U &&
         static_cast<unsigned char>(value[position + 1]) < 0x90U) ||
        (width == 4 && first == 0xF4U &&
         static_cast<unsigned char>(value[position + 1]) >= 0x90U)) {
      config_error(std::string(context) + " is not valid UTF-8");
    }
    if (reject_controls &&
        (code_point <= 0x1FU ||
         (code_point >= 0x7FU && code_point <= 0x9FU))) {
      config_error(std::string(context) + " contains a control character");
    }
    position += width;
    ++count;
  }
  return count;
}

class JsonParser final {
public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  [[nodiscard]] JsonValue parse() {
    JsonValue result = parse_value(0);
    skip_whitespace();
    if (position_ != input_.size()) {
      fail("unexpected data after the root value");
    }
    return result;
  }

private:
  std::string_view input_;
  std::size_t position_ = 0;

  [[noreturn]] void fail(const std::string& message) const {
    config_error(
        "invalid JSON at byte " + std::to_string(position_) + ": " + message);
  }

  void skip_whitespace() noexcept {
    while (position_ < input_.size()) {
      const char character = input_[position_];
      if (character != ' ' && character != '\t' && character != '\n' &&
          character != '\r') {
        return;
      }
      ++position_;
    }
  }

  [[nodiscard]] bool consume(char expected) noexcept {
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void expect_literal(std::string_view literal) {
    if (input_.substr(position_, literal.size()) != literal) {
      fail("expected " + std::string(literal));
    }
    position_ += literal.size();
  }

  [[nodiscard]] std::uint16_t parse_hex_quad() {
    if (position_ + 4 > input_.size()) {
      fail("truncated Unicode escape");
    }
    std::uint16_t result = 0;
    for (std::size_t index = 0; index < 4; ++index) {
      const auto character = static_cast<unsigned char>(input_[position_++]);
      result = static_cast<std::uint16_t>(result << 4U);
      if (character >= '0' && character <= '9') {
        result = static_cast<std::uint16_t>(result | (character - '0'));
      } else if (character >= 'a' && character <= 'f') {
        result = static_cast<std::uint16_t>(
            result | static_cast<unsigned int>(character - 'a' + 10));
      } else if (character >= 'A' && character <= 'F') {
        result = static_cast<std::uint16_t>(
            result | static_cast<unsigned int>(character - 'A' + 10));
      } else {
        fail("invalid Unicode escape");
      }
    }
    return result;
  }

  [[nodiscard]] std::string parse_string() {
    if (!consume('"')) {
      fail("expected string");
    }
    std::string result;
    while (position_ < input_.size()) {
      const auto character = static_cast<unsigned char>(input_[position_++]);
      if (character == '"') {
        static_cast<void>(validate_utf8_and_count(result, "JSON string", false));
        return result;
      }
      if (character < 0x20U) {
        fail("unescaped control byte in string");
      }
      if (character != '\\') {
        result.push_back(static_cast<char>(character));
        continue;
      }
      if (position_ == input_.size()) {
        fail("truncated escape sequence");
      }
      const char escaped = input_[position_++];
      switch (escaped) {
        case '"':
          result.push_back('"');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '/':
          result.push_back('/');
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u': {
          std::uint32_t code_point = parse_hex_quad();
          if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
            if (input_.substr(position_, 2) != "\\u") {
              fail("high surrogate is not followed by a low surrogate");
            }
            position_ += 2;
            const std::uint32_t low = parse_hex_quad();
            if (low < 0xDC00U || low > 0xDFFFU) {
              fail("invalid low surrogate");
            }
            code_point = 0x10000U +
                ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
          } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            fail("unexpected low surrogate");
          }
          append_utf8(result, code_point);
          break;
        }
        default:
          fail("invalid escape sequence");
      }
    }
    fail("unterminated string");
  }

  [[nodiscard]] JsonValue parse_number() {
    const std::size_t start = position_;
    static_cast<void>(consume('-'));
    if (position_ == input_.size() || input_[position_] < '0' ||
        input_[position_] > '9') {
      fail("invalid number");
    }
    if (input_[position_] == '0') {
      ++position_;
      if (position_ < input_.size() && input_[position_] >= '0' &&
          input_[position_] <= '9') {
        fail("number has a leading zero");
      }
    } else {
      while (position_ < input_.size() && input_[position_] >= '0' &&
          input_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      const std::size_t fraction_start = position_;
      while (position_ < input_.size() && input_[position_] >= '0' &&
          input_[position_] <= '9') {
        ++position_;
      }
      if (fraction_start == position_) {
        fail("fraction requires a digit");
      }
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      const std::size_t exponent_start = position_;
      while (position_ < input_.size() && input_[position_] >= '0' &&
          input_[position_] <= '9') {
        ++position_;
      }
      if (exponent_start == position_) {
        fail("exponent requires a digit");
      }
    }
    return JsonValue(JsonNumber{
        std::string(input_.substr(start, position_ - start))});
  }

  [[nodiscard]] JsonValue parse_array(std::size_t depth) {
    static_cast<void>(consume('['));
    JsonValue::Array result;
    skip_whitespace();
    if (consume(']')) {
      return JsonValue(std::move(result));
    }
    while (true) {
      result.emplace_back(parse_value(depth + 1));
      skip_whitespace();
      if (consume(']')) {
        return JsonValue(std::move(result));
      }
      if (!consume(',')) {
        fail("expected ',' or ']' in array");
      }
      skip_whitespace();
    }
  }

  [[nodiscard]] JsonValue parse_object(std::size_t depth) {
    static_cast<void>(consume('{'));
    JsonValue::Object result;
    skip_whitespace();
    if (consume('}')) {
      return JsonValue(std::move(result));
    }
    while (true) {
      std::string key = parse_string();
      skip_whitespace();
      if (!consume(':')) {
        fail("expected ':' after object key");
      }
      JsonValue value = parse_value(depth + 1);
      if (!result.emplace(std::move(key), std::move(value)).second) {
        fail("duplicate object key");
      }
      skip_whitespace();
      if (consume('}')) {
        return JsonValue(std::move(result));
      }
      if (!consume(',')) {
        fail("expected ',' or '}' in object");
      }
      skip_whitespace();
    }
  }

  [[nodiscard]] JsonValue parse_value(std::size_t depth) {
    if (depth > kMaximumJsonDepth) {
      fail("maximum nesting depth exceeded");
    }
    skip_whitespace();
    if (position_ == input_.size()) {
      fail("expected value");
    }
    switch (input_[position_]) {
      case 'n':
        expect_literal("null");
        return JsonValue(nullptr);
      case 't':
        expect_literal("true");
        return JsonValue(true);
      case 'f':
        expect_literal("false");
        return JsonValue(false);
      case '"':
        return JsonValue(parse_string());
      case '[':
        return parse_array(depth);
      case '{':
        return parse_object(depth);
      default:
        if (input_[position_] == '-' ||
            (input_[position_] >= '0' && input_[position_] <= '9')) {
          return parse_number();
        }
        fail("unexpected token");
    }
  }
};

[[nodiscard]] const JsonValue::Object& as_object(
    const JsonValue& value,
    std::string_view context) {
  const auto* result = value.get_if<JsonValue::Object>();
  if (result == nullptr) {
    config_error(std::string(context) + " must be an object");
  }
  return *result;
}

[[nodiscard]] const JsonValue::Array& as_array(
    const JsonValue& value,
    std::string_view context) {
  const auto* result = value.get_if<JsonValue::Array>();
  if (result == nullptr) {
    config_error(std::string(context) + " must be an array");
  }
  return *result;
}

[[nodiscard]] const std::string& as_string(
    const JsonValue& value,
    std::string_view context) {
  const auto* result = value.get_if<std::string>();
  if (result == nullptr) {
    config_error(std::string(context) + " must be a string");
  }
  return *result;
}

[[nodiscard]] std::int64_t as_integer(
    const JsonValue& value,
    std::string_view context) {
  const auto* number = value.get_if<JsonNumber>();
  if (number == nullptr || number->spelling.find_first_of(".eE") !=
      std::string::npos) {
    config_error(std::string(context) + " must be an integer");
  }
  std::int64_t result = 0;
  const auto parsed = std::from_chars(
      number->spelling.data(),
      number->spelling.data() + number->spelling.size(),
      result);
  if (parsed.ec != std::errc{} ||
      parsed.ptr != number->spelling.data() + number->spelling.size()) {
    config_error(std::string(context) + " integer is out of range");
  }
  return result;
}

[[nodiscard]] const JsonValue& field(
    const JsonValue::Object& object,
    std::string_view name,
    std::string_view context) {
  const auto iterator = object.find(std::string(name));
  if (iterator == object.end()) {
    config_error(
        std::string(context) + " is missing field: " + std::string(name));
  }
  return iterator->second;
}

void require_exact_fields(
    const JsonValue::Object& object,
    std::initializer_list<std::string_view> expected,
    std::string_view context) {
  std::set<std::string> remaining;
  for (const auto name : expected) {
    remaining.emplace(name);
  }
  for (const auto& [name, unused] : object) {
    static_cast<void>(unused);
    if (!remaining.erase(name)) {
      config_error(std::string(context) + " has unknown field: " + name);
    }
  }
  if (!remaining.empty()) {
    config_error(
        std::string(context) + " is missing field: " + *remaining.begin());
  }
}

[[nodiscard]] std::string require_exact_string(
    const JsonValue::Object& object,
    std::string_view name,
    std::string_view expected) {
  std::string value = as_string(field(object, name, "child config"), name);
  if (value != expected) {
    config_error(
        std::string(name) + " must be \"" + std::string(expected) + "\"");
  }
  return value;
}

[[nodiscard]] bool is_lower_hex(std::string_view value) noexcept {
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string require_token(
    const JsonValue::Object& object,
    std::string_view name,
    std::size_t length) {
  std::string value = as_string(field(object, name, "child config"), name);
  if (value.size() != length || !is_lower_hex(value)) {
    config_error(
        std::string(name) + " must be " + std::to_string(length) +
        " lowercase hexadecimal characters");
  }
  return value;
}

[[nodiscard]] std::uint64_t parse_hex64(
    std::string_view value,
    std::string_view context) {
  std::uint64_t result = 0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    config_error(std::string(context) + " is not a 64-bit hexadecimal token");
  }
  return result;
}

[[nodiscard]] bool is_identifier(std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaximumIdentifierLength ||
      value.front() < 'a' || value.front() > 'z' || value.back() == '_') {
    return false;
  }
  bool previous_underscore = false;
  for (const char character : value) {
    const bool underscore = character == '_';
    if (!((character >= 'a' && character <= 'z') ||
          (character >= '0' && character <= '9') || underscore) ||
        (underscore && previous_underscore)) {
      return false;
    }
    previous_underscore = underscore;
  }
  return true;
}

[[nodiscard]] std::string require_identifier(
    const JsonValue& value,
    std::string_view context) {
  std::string result = as_string(value, context);
  if (!is_identifier(result)) {
    config_error(
        std::string(context) + " must be normalized lowercase snake_case ASCII");
  }
  return result;
}

[[nodiscard]] ReplayArgumentValue parse_argument_value(
    const JsonValue& value,
    std::string_view context) {
  if (const auto* boolean = value.get_if<bool>(); boolean != nullptr) {
    return *boolean;
  }
  if (value.get_if<JsonNumber>() != nullptr) {
    const std::int64_t integer = as_integer(value, context);
    if (integer < -kMaximumExactJsonInteger ||
        integer > kMaximumExactJsonInteger) {
      config_error(
          std::string(context) + " integer exceeds the exact JSON range");
    }
    return integer;
  }
  if (const auto* string = value.get_if<std::string>(); string != nullptr) {
    if (string->size() > kMaximumArgumentStringLength) {
      config_error(
          std::string(context) + " string exceeds 1024 printable ASCII bytes");
    }
    for (const unsigned char character : *string) {
      if (character < 0x20U || character > 0x7EU) {
        config_error(
            std::string(context) + " string must contain printable ASCII only");
      }
    }
    return *string;
  }
  config_error(std::string(context) + " must be a string, integer, or boolean");
}

[[nodiscard]] std::vector<ReplayAction> parse_actions(
    const JsonValue& value) {
  const auto& array = as_array(value, "actions");
  if (array.size() > kMaximumReplayActions) {
    config_error("actions exceeds the 4096-action limit");
  }
  std::vector<ReplayAction> result;
  result.reserve(array.size());
  for (std::size_t index = 0; index < array.size(); ++index) {
    const std::string context = "actions[" + std::to_string(index) + "]";
    const auto& object = as_object(array[index], context);
    require_exact_fields(object, {"ordinal", "kind", "arguments"}, context);
    const std::int64_t ordinal = as_integer(
        field(object, "ordinal", context), context + ".ordinal");
    if (ordinal < 0 || ordinal > 4095 ||
        static_cast<std::size_t>(ordinal) != index) {
      config_error(context + ".ordinal must equal its zero-based array index");
    }
    ReplayAction action;
    action.ordinal = static_cast<std::uint16_t>(ordinal);
    action.kind = require_identifier(
        field(object, "kind", context), context + ".kind");
    const auto& arguments = as_object(
        field(object, "arguments", context), context + ".arguments");
    if (arguments.size() > kMaximumArguments) {
      config_error(context + ".arguments exceeds the 32-argument limit");
    }
    for (const auto& [name, argument] : arguments) {
      if (!is_identifier(name)) {
        config_error(
            context + ".arguments key must be normalized lowercase "
            "snake_case ASCII");
      }
      action.arguments.emplace(
          name,
          parse_argument_value(argument, context + ".arguments." + name));
    }
    result.emplace_back(std::move(action));
  }
  return result;
}

[[nodiscard]] char require_slot(
    const JsonValue::Object& object,
    std::string_view name) {
  const std::string& value = as_string(field(object, name, "child config"), name);
  if (value.size() != 1 || value.front() < 'A' || value.front() > 'J') {
    config_error(
        std::string(name) + " must be one uppercase Classic slot letter A through J");
  }
  return value.front();
}

[[nodiscard]] std::filesystem::path require_normalized_absolute_path(
    const JsonValue::Object& object,
    std::string_view name) {
  const std::string& value = as_string(field(object, name, "child config"), name);
  if (value.empty()) {
    config_error(std::string(name) + " must be a non-empty path");
  }
  const std::size_t code_points =
      validate_utf8_and_count(value, name, true);
  if (code_points > kMaximumPathBytes || value.size() > kMaximumPathBytes) {
    config_error(std::string(name) + " exceeds the 4096-character path limit");
  }
  std::u8string utf8_path;
  utf8_path.reserve(value.size());
  for (const unsigned char byte : value) {
    utf8_path.push_back(static_cast<char8_t>(byte));
  }
  std::filesystem::path path;
  try {
    path = std::filesystem::path(utf8_path);
  } catch (const std::filesystem::filesystem_error&) {
    config_error(std::string(name) + " cannot be represented as a host path");
  }
  if (!path.is_absolute() || path.lexically_normal() != path ||
      (path != path.root_path() && path.filename().empty())) {
    config_error(std::string(name) + " must be an absolute normalized path");
  }
  return path;
}

[[nodiscard]] ReplayRoute require_route(const JsonValue::Object& object) {
  const std::string& value = as_string(
      field(object, "replay_route", "child config"), "replay_route");
  if (value == "classic") {
    return ReplayRoute::classic;
  }
  if (value == "semantic") {
    return ReplayRoute::semantic;
  }
  config_error("replay_route must be \"classic\" or \"semantic\"");
}

[[nodiscard]] ReplayPresentationMode require_presentation_mode(
    const JsonValue::Object& object) {
  const std::string& value = as_string(
      field(object, "presentation_mode", "child config"),
      "presentation_mode");
  if (value == "classic") {
    return ReplayPresentationMode::classic;
  }
  if (value == "remastered") {
    return ReplayPresentationMode::remastered;
  }
  config_error(
      "presentation_mode must be \"classic\" or \"remastered\"");
}

void append_bounded(
    std::string& output,
    const char* data,
    std::size_t size) {
  if (size > kMaximumChildConfigBytes - output.size()) {
    config_error("child config exceeds 4194304 bytes");
  }
  output.append(data, size);
}

#if defined(_WIN32)

class ScopedConfigHandle final {
public:
  explicit ScopedConfigHandle(HANDLE handle) noexcept : handle_(handle) {}

  ScopedConfigHandle(const ScopedConfigHandle&) = delete;
  ScopedConfigHandle& operator=(const ScopedConfigHandle&) = delete;

  ~ScopedConfigHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      static_cast<void>(CloseHandle(handle_));
    }
  }

  [[nodiscard]] HANDLE get() const noexcept {
    return handle_;
  }

private:
  HANDLE handle_;
};

[[nodiscard]] std::uint64_t windows_file_size(
    const BY_HANDLE_FILE_INFORMATION& information) noexcept {
  return (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) |
      information.nFileSizeLow;
}

[[nodiscard]] bool same_windows_file(
    const BY_HANDLE_FILE_INFORMATION& first,
    const BY_HANDLE_FILE_INFORMATION& second) noexcept {
  return first.dwVolumeSerialNumber == second.dwVolumeSerialNumber &&
      first.nFileIndexHigh == second.nFileIndexHigh &&
      first.nFileIndexLow == second.nFileIndexLow &&
      windows_file_size(first) == windows_file_size(second);
}

[[nodiscard]] bool same_windows_times(
    const FILE_BASIC_INFO& first,
    const FILE_BASIC_INFO& second) noexcept {
  return first.LastWriteTime.QuadPart == second.LastWriteTime.QuadPart &&
      first.ChangeTime.QuadPart == second.ChangeTime.QuadPart;
}

[[nodiscard]] std::string read_config_file(const std::filesystem::path& path) {
  const ScopedConfigHandle handle(CreateFileW(
      path.c_str(),
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
      nullptr));
  if (handle.get() == INVALID_HANDLE_VALUE) {
    config_error("cannot open child config");
  }
  BY_HANDLE_FILE_INFORMATION before{};
  if (!GetFileInformationByHandle(handle.get(), &before)) {
    config_error("cannot inspect child config");
  }
  if ((before.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
      GetFileType(handle.get()) != FILE_TYPE_DISK) {
    config_error("child config must be a regular non-reparse file");
  }
  FILE_BASIC_INFO before_times{};
  if (!GetFileInformationByHandleEx(
          handle.get(), FileBasicInfo, &before_times, sizeof(before_times))) {
    config_error("cannot inspect child config timestamps");
  }
  if (windows_file_size(before) > kMaximumChildConfigBytes) {
    config_error("child config exceeds 4194304 bytes");
  }

  std::string contents;
  contents.reserve(static_cast<std::size_t>(windows_file_size(before)));
  std::array<char, 64U * 1024U> buffer{};
  while (true) {
    const std::size_t remaining =
        kMaximumChildConfigBytes - contents.size();
    const DWORD requested = static_cast<DWORD>(
        std::min(buffer.size(), remaining + static_cast<std::size_t>(1)));
    DWORD count = 0;
    if (!ReadFile(handle.get(), buffer.data(), requested, &count, nullptr)) {
      config_error("cannot read child config");
    }
    if (count == 0) {
      break;
    }
    append_bounded(contents, buffer.data(), count);
  }

  BY_HANDLE_FILE_INFORMATION after{};
  FILE_BASIC_INFO after_times{};
  if (!GetFileInformationByHandle(handle.get(), &after) ||
      !GetFileInformationByHandleEx(
          handle.get(), FileBasicInfo, &after_times, sizeof(after_times)) ||
      !same_windows_file(before, after) ||
      !same_windows_times(before_times, after_times)) {
    config_error("child config changed while it was read");
  }
  return contents;
}

#else

class ScopedConfigDescriptor final {
public:
  explicit ScopedConfigDescriptor(int descriptor) noexcept
      : descriptor_(descriptor) {}

  ScopedConfigDescriptor(const ScopedConfigDescriptor&) = delete;
  ScopedConfigDescriptor& operator=(const ScopedConfigDescriptor&) = delete;

  ~ScopedConfigDescriptor() {
    if (descriptor_ >= 0) {
      static_cast<void>(close(descriptor_));
    }
  }

  [[nodiscard]] int get() const noexcept {
    return descriptor_;
  }

private:
  int descriptor_;
};

[[nodiscard]] bool same_posix_file(
    const struct stat& first,
    const struct stat& second) noexcept {
  if (!S_ISREG(second.st_mode) || first.st_dev != second.st_dev ||
      first.st_ino != second.st_ino || first.st_size != second.st_size) {
    return false;
  }
#if defined(__APPLE__)
  return first.st_mtimespec.tv_sec == second.st_mtimespec.tv_sec &&
      first.st_mtimespec.tv_nsec == second.st_mtimespec.tv_nsec &&
      first.st_ctimespec.tv_sec == second.st_ctimespec.tv_sec &&
      first.st_ctimespec.tv_nsec == second.st_ctimespec.tv_nsec;
#else
  return first.st_mtim.tv_sec == second.st_mtim.tv_sec &&
      first.st_mtim.tv_nsec == second.st_mtim.tv_nsec &&
      first.st_ctim.tv_sec == second.st_ctim.tv_sec &&
      first.st_ctim.tv_nsec == second.st_ctim.tv_nsec;
#endif
}

[[nodiscard]] std::string read_config_file(const std::filesystem::path& path) {
  int flags = O_RDONLY;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
#ifdef O_NONBLOCK
  flags |= O_NONBLOCK;
#endif
  const ScopedConfigDescriptor descriptor(open(path.c_str(), flags));
  if (descriptor.get() < 0) {
    config_error("cannot open child config");
  }

  struct stat before {};
  if (fstat(descriptor.get(), &before) != 0) {
    config_error("cannot inspect child config");
  }
  if (!S_ISREG(before.st_mode)) {
    config_error("child config must be a regular file");
  }
  if (before.st_size < 0 ||
      static_cast<std::uint64_t>(before.st_size) > kMaximumChildConfigBytes) {
    config_error("child config exceeds 4194304 bytes");
  }

  std::string contents;
  contents.reserve(static_cast<std::size_t>(before.st_size));
  std::array<char, 64U * 1024U> buffer{};
  while (true) {
    const std::size_t remaining =
        kMaximumChildConfigBytes - contents.size();
    const std::size_t requested =
        std::min(buffer.size(), remaining + static_cast<std::size_t>(1));
    const ssize_t count = read(descriptor.get(), buffer.data(), requested);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      config_error("cannot read child config");
    }
    if (count == 0) {
      break;
    }
    append_bounded(contents, buffer.data(), static_cast<std::size_t>(count));
  }

  struct stat after {};
  if (fstat(descriptor.get(), &after) != 0 ||
      !same_posix_file(before, after)) {
    config_error("child config changed while it was read");
  }
  return contents;
}

#endif

} // namespace

struct ReplayChildConfig::Storage final {
  std::uint32_t schema_version = 1;
  std::string run_id;
  std::string child_nonce;
  ReplayRoute replay_route = ReplayRoute::classic;
  ReplayPresentationMode presentation_mode = ReplayPresentationMode::classic;
  std::filesystem::path user_data_root;
  ReplayUserDataRootPolicy user_data_root_policy =
      ReplayUserDataRootPolicy::set_once_before_toolbox_init;
  ReplayPreferencesWritePolicy preferences_write_policy =
      ReplayPreferencesWritePolicy::disabled;
  char input_slot = 'A';
  ReplayInputSlotPolicy input_slot_policy =
      ReplayInputSlotPolicy::user_data_root_only_no_bundled_fallback;
  char output_slot = 'B';
  ReplayOutputSlotPolicy output_slot_policy =
      ReplayOutputSlotPolicy::fresh_nonexistent;
  std::vector<ReplayAction> actions;
  ReplaySettlementBarrier settlement_barrier =
      ReplaySettlementBarrier::next_semantic_gameplay_poll;
  std::filesystem::path result_path;
  std::uint64_t rng_seed = 0;
  std::uint64_t rng_stream = 0;
  std::string rng_seed_token;
  std::string rng_stream_token;
};

ReplayChildConfig::ReplayChildConfig(
    std::shared_ptr<const Storage> storage) noexcept
    : storage_(std::move(storage)) {}

std::uint32_t ReplayChildConfig::schema_version() const noexcept {
  return storage_->schema_version;
}

const std::string& ReplayChildConfig::run_id() const noexcept {
  return storage_->run_id;
}

const std::string& ReplayChildConfig::child_nonce() const noexcept {
  return storage_->child_nonce;
}

ReplayRoute ReplayChildConfig::replay_route() const noexcept {
  return storage_->replay_route;
}

ReplayPresentationMode ReplayChildConfig::presentation_mode() const noexcept {
  return storage_->presentation_mode;
}

const std::filesystem::path& ReplayChildConfig::user_data_root() const noexcept {
  return storage_->user_data_root;
}

ReplayUserDataRootPolicy ReplayChildConfig::user_data_root_policy() const noexcept {
  return storage_->user_data_root_policy;
}

ReplayPreferencesWritePolicy ReplayChildConfig::preferences_write_policy() const noexcept {
  return storage_->preferences_write_policy;
}

char ReplayChildConfig::input_slot() const noexcept {
  return storage_->input_slot;
}

ReplayInputSlotPolicy ReplayChildConfig::input_slot_policy() const noexcept {
  return storage_->input_slot_policy;
}

char ReplayChildConfig::output_slot() const noexcept {
  return storage_->output_slot;
}

ReplayOutputSlotPolicy ReplayChildConfig::output_slot_policy() const noexcept {
  return storage_->output_slot_policy;
}

const std::vector<ReplayAction>& ReplayChildConfig::actions() const noexcept {
  return storage_->actions;
}

ReplaySettlementBarrier ReplayChildConfig::settlement_barrier() const noexcept {
  return storage_->settlement_barrier;
}

const std::filesystem::path& ReplayChildConfig::result_path() const noexcept {
  return storage_->result_path;
}

std::uint64_t ReplayChildConfig::rng_seed() const noexcept {
  return storage_->rng_seed;
}

std::uint64_t ReplayChildConfig::rng_stream() const noexcept {
  return storage_->rng_stream;
}

const std::string& ReplayChildConfig::rng_seed_token() const noexcept {
  return storage_->rng_seed_token;
}

const std::string& ReplayChildConfig::rng_stream_token() const noexcept {
  return storage_->rng_stream_token;
}

ReplayChildConfig parse_child_config(std::string_view json) {
  if (json.size() > kMaximumChildConfigBytes) {
    config_error("child config exceeds 4194304 bytes");
  }
  JsonValue document = JsonParser(json).parse();
  const auto& object = as_object(document, "child config");
  require_exact_fields(
      object,
      {
          "schema_version",
          "run_id",
          "child_nonce",
          "replay_route",
          "presentation_mode",
          "user_data_root",
          "user_data_root_policy",
          "preferences_write_policy",
          "input_slot",
          "input_slot_policy",
          "output_slot",
          "output_slot_policy",
          "actions",
          "settlement_barrier",
          "result_path",
          "rng_seed",
          "rng_stream",
      },
      "child config");

  const std::int64_t schema_version = as_integer(
      field(object, "schema_version", "child config"), "schema_version");
  if (schema_version != 1 && schema_version != 2) {
    config_error("schema_version must be 1 or 2");
  }

  auto storage = std::make_shared<ReplayChildConfig::Storage>();
  storage->schema_version = static_cast<std::uint32_t>(schema_version);
  storage->run_id = require_token(object, "run_id", kTokenLength);
  storage->child_nonce = require_token(object, "child_nonce", kTokenLength);
  storage->replay_route = require_route(object);
  storage->presentation_mode = require_presentation_mode(object);
  if ((storage->replay_route == ReplayRoute::classic) !=
      (storage->presentation_mode == ReplayPresentationMode::classic)) {
    config_error(
        "replay_route and presentation_mode must be classic/classic or "
        "semantic/remastered");
  }
  storage->user_data_root =
      require_normalized_absolute_path(object, "user_data_root");
  static_cast<void>(require_exact_string(
      object, "user_data_root_policy", "set_once_before_toolbox_init"));
  static_cast<void>(require_exact_string(
      object, "preferences_write_policy", "disabled"));
  storage->input_slot = require_slot(object, "input_slot");
  static_cast<void>(require_exact_string(
      object,
      "input_slot_policy",
      "user_data_root_only_no_bundled_fallback"));
  storage->output_slot = require_slot(object, "output_slot");
  if (storage->input_slot == storage->output_slot) {
    config_error("input_slot and output_slot must be distinct");
  }
  static_cast<void>(require_exact_string(
      object, "output_slot_policy", "fresh_nonexistent"));
  storage->actions = parse_actions(field(object, "actions", "child config"));
  static_cast<void>(require_exact_string(
      object,
      "settlement_barrier",
      "next_semantic_gameplay_poll"));
  storage->result_path = require_normalized_absolute_path(object, "result_path");
  storage->rng_seed_token = require_token(object, "rng_seed", kRngTokenLength);
  storage->rng_stream_token =
      require_token(object, "rng_stream", kRngTokenLength);
  storage->rng_seed = parse_hex64(storage->rng_seed_token, "rng_seed");
  storage->rng_stream = parse_hex64(storage->rng_stream_token, "rng_stream");
  return ReplayChildConfig(std::move(storage));
}

ReplayChildConfig parse_child_config_v1(std::string_view json) {
  ReplayChildConfig config = parse_child_config(json);
  if (config.schema_version() != 1U) {
    config_error("schema_version must be 1");
  }
  return config;
}

ReplayChildConfig parse_child_config_v2(std::string_view json) {
  ReplayChildConfig config = parse_child_config(json);
  if (config.schema_version() != 2U) {
    config_error("schema_version must be 2");
  }
  return config;
}

ReplayChildConfig load_child_config_v1(const std::filesystem::path& path) {
  return parse_child_config_v1(read_config_file(path));
}

ReplayChildConfig load_child_config_v2(const std::filesystem::path& path) {
  return parse_child_config_v2(read_config_file(path));
}

ReplayChildConfig load_child_config(const std::filesystem::path& path) {
  return parse_child_config(read_config_file(path));
}

std::string canonical_hex64(std::uint64_t value) {
  std::ostringstream output;
  output << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << value;
  return output.str();
}

} // namespace realmz::replay
