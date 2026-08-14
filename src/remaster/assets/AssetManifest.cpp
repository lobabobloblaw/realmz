#include "remaster/assets/AssetManifest.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>
#include <variant>

namespace realmz::remaster::assets {
namespace {

constexpr std::int64_t kSchemaVersion = 1;

class JsonValue {
public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;
  using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object>;

  explicit JsonValue(std::nullptr_t value) : value_(value) {}
  explicit JsonValue(bool value) : value_(value) {}
  explicit JsonValue(std::int64_t value) : value_(value) {}
  explicit JsonValue(std::string value) : value_(std::move(value)) {}
  explicit JsonValue(Array value) : value_(std::move(value)) {}
  explicit JsonValue(Object value) : value_(std::move(value)) {}

  [[nodiscard]] bool isNull() const noexcept {
    return std::holds_alternative<std::nullptr_t>(value_);
  }

  template <typename T>
  [[nodiscard]] const T* getIf() const noexcept {
    return std::get_if<T>(&value_);
  }

private:
  Storage value_;
};

void appendUtf8(std::string& output, std::uint32_t codePoint) {
  if (codePoint <= 0x7FU) {
    output.push_back(static_cast<char>(codePoint));
  } else if (codePoint <= 0x7FFU) {
    output.push_back(static_cast<char>(0xC0U | (codePoint >> 6)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
  } else if (codePoint <= 0xFFFFU) {
    output.push_back(static_cast<char>(0xE0U | (codePoint >> 12)));
    output.push_back(static_cast<char>(0x80U | ((codePoint >> 6) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
  } else if (codePoint <= 0x10FFFFU) {
    output.push_back(static_cast<char>(0xF0U | (codePoint >> 18)));
    output.push_back(static_cast<char>(0x80U | ((codePoint >> 12) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | ((codePoint >> 6) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
  } else {
    throw AssetManifestError("JSON string contains an invalid Unicode code point");
  }
}

class JsonParser {
public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  [[nodiscard]] JsonValue parse() {
    auto result = parseValue(0);
    skipWhitespace();
    if (position_ != input_.size()) {
      fail("unexpected data after the root value");
    }
    return result;
  }

private:
  std::string_view input_;
  std::size_t position_ = 0;

  [[noreturn]] void fail(const std::string& message) const {
    throw AssetManifestError(
        "invalid JSON at byte " + std::to_string(position_) + ": " + message);
  }

  void skipWhitespace() {
    while (position_ < input_.size()) {
      const char ch = input_[position_];
      if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
        break;
      }
      ++position_;
    }
  }

  bool consume(char expected) {
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void expectLiteral(std::string_view literal) {
    if (input_.substr(position_, literal.size()) != literal) {
      fail("expected " + std::string(literal));
    }
    position_ += literal.size();
  }

  [[nodiscard]] std::uint16_t parseHexQuad() {
    if (position_ + 4 > input_.size()) {
      fail("truncated Unicode escape");
    }
    std::uint16_t result = 0;
    for (int index = 0; index < 4; ++index) {
      const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
      result = static_cast<std::uint16_t>(result << 4);
      if (ch >= '0' && ch <= '9') {
        result = static_cast<std::uint16_t>(result | (ch - '0'));
      } else if (ch >= 'a' && ch <= 'f') {
        result = static_cast<std::uint16_t>(result | (ch - 'a' + 10));
      } else if (ch >= 'A' && ch <= 'F') {
        result = static_cast<std::uint16_t>(result | (ch - 'A' + 10));
      } else {
        fail("invalid Unicode escape");
      }
    }
    return result;
  }

  [[nodiscard]] std::string parseString() {
    if (!consume('"')) {
      fail("expected string");
    }
    std::string result;
    while (position_ < input_.size()) {
      const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
      if (ch == '"') {
        return result;
      }
      if (ch < 0x20U) {
        fail("unescaped control byte in string");
      }
      if (ch != '\\') {
        result.push_back(static_cast<char>(ch));
        continue;
      }
      if (position_ == input_.size()) {
        fail("truncated escape sequence");
      }
      const char escaped = input_[position_++];
      switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
          std::uint32_t codePoint = parseHexQuad();
          if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
            if (input_.substr(position_, 2) != "\\u") {
              fail("high surrogate is not followed by a low surrogate");
            }
            position_ += 2;
            const std::uint32_t low = parseHexQuad();
            if (low < 0xDC00U || low > 0xDFFFU) {
              fail("invalid low surrogate");
            }
            codePoint = 0x10000U + ((codePoint - 0xD800U) << 10) + (low - 0xDC00U);
          } else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
            fail("unexpected low surrogate");
          }
          appendUtf8(result, codePoint);
          break;
        }
        default: fail("invalid escape sequence");
      }
    }
    fail("unterminated string");
  }

  [[nodiscard]] JsonValue parseInteger() {
    const std::size_t start = position_;
    consume('-');
    if (position_ == input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
      fail("invalid number");
    }
    if (input_[position_] == '0') {
      ++position_;
      if (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        fail("integer has a leading zero");
      }
    } else {
      while (position_ < input_.size() &&
          std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        ++position_;
      }
    }
    if (position_ < input_.size() &&
        (input_[position_] == '.' || input_[position_] == 'e' || input_[position_] == 'E')) {
      fail("non-integral numbers are not allowed in asset metadata");
    }
    std::int64_t result = 0;
    const auto number = input_.substr(start, position_ - start);
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) {
      fail("integer is out of range");
    }
    return JsonValue(result);
  }

  [[nodiscard]] JsonValue parseArray(std::size_t depth) {
    consume('[');
    JsonValue::Array result;
    skipWhitespace();
    if (consume(']')) {
      return JsonValue(std::move(result));
    }
    while (true) {
      result.emplace_back(parseValue(depth + 1));
      skipWhitespace();
      if (consume(']')) {
        return JsonValue(std::move(result));
      }
      if (!consume(',')) {
        fail("expected ',' or ']' in array");
      }
      skipWhitespace();
    }
  }

  [[nodiscard]] JsonValue parseObject(std::size_t depth) {
    consume('{');
    JsonValue::Object result;
    skipWhitespace();
    if (consume('}')) {
      return JsonValue(std::move(result));
    }
    while (true) {
      const auto key = parseString();
      skipWhitespace();
      if (!consume(':')) {
        fail("expected ':' after object key");
      }
      auto value = parseValue(depth + 1);
      if (!result.emplace(key, std::move(value)).second) {
        fail("duplicate object key " + key);
      }
      skipWhitespace();
      if (consume('}')) {
        return JsonValue(std::move(result));
      }
      if (!consume(',')) {
        fail("expected ',' or '}' in object");
      }
      skipWhitespace();
    }
  }

  [[nodiscard]] JsonValue parseValue(std::size_t depth) {
    if (depth > 128) {
      fail("maximum nesting depth exceeded");
    }
    skipWhitespace();
    if (position_ == input_.size()) {
      fail("expected value");
    }
    switch (input_[position_]) {
      case 'n': expectLiteral("null"); return JsonValue(nullptr);
      case 't': expectLiteral("true"); return JsonValue(true);
      case 'f': expectLiteral("false"); return JsonValue(false);
      case '"': return JsonValue(parseString());
      case '[': return parseArray(depth);
      case '{': return parseObject(depth);
      default:
        if (input_[position_] == '-' ||
            std::isdigit(static_cast<unsigned char>(input_[position_]))) {
          return parseInteger();
        }
        fail("unexpected token");
    }
  }
};

[[nodiscard]] std::string loadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw AssetManifestError("cannot open " + path.string());
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    throw AssetManifestError("cannot read " + path.string());
  }
  return contents.str();
}

[[nodiscard]] const JsonValue::Object& asObject(const JsonValue& value, std::string_view where) {
  const auto* result = value.getIf<JsonValue::Object>();
  if (result == nullptr) {
    throw AssetManifestError(std::string(where) + " must be an object");
  }
  return *result;
}

[[nodiscard]] const JsonValue::Array& asArray(const JsonValue& value, std::string_view where) {
  const auto* result = value.getIf<JsonValue::Array>();
  if (result == nullptr) {
    throw AssetManifestError(std::string(where) + " must be an array");
  }
  return *result;
}

[[nodiscard]] const std::string& asString(const JsonValue& value, std::string_view where) {
  const auto* result = value.getIf<std::string>();
  if (result == nullptr) {
    throw AssetManifestError(std::string(where) + " must be a string");
  }
  return *result;
}

[[nodiscard]] std::int64_t asInteger(const JsonValue& value, std::string_view where) {
  const auto* result = value.getIf<std::int64_t>();
  if (result == nullptr) {
    throw AssetManifestError(std::string(where) + " must be an integer");
  }
  return *result;
}

[[nodiscard]] const JsonValue& field(
    const JsonValue::Object& object, std::string_view name, std::string_view where) {
  const auto iterator = object.find(std::string(name));
  if (iterator == object.end()) {
    throw AssetManifestError(std::string(where) + " is missing field " + std::string(name));
  }
  return iterator->second;
}

void requireExactFields(
    const JsonValue::Object& object,
    std::initializer_list<std::string_view> expected,
    std::string_view where) {
  std::set<std::string> names;
  for (const auto name : expected) {
    names.emplace(name);
  }
  for (const auto& [name, unused] : object) {
    (void)unused;
    if (!names.erase(name)) {
      throw AssetManifestError(std::string(where) + " has unknown field " + name);
    }
  }
  if (!names.empty()) {
    throw AssetManifestError(std::string(where) + " is missing field " + *names.begin());
  }
}

[[nodiscard]] bool isSha256(std::string_view value) {
  return value.size() == 64 && std::ranges::all_of(value, [](unsigned char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
  });
}

[[nodiscard]] const std::string& asSha256(const JsonValue& value, std::string_view where) {
  const auto& result = asString(value, where);
  if (!isSha256(result)) {
    throw AssetManifestError(std::string(where) + " must be a lowercase SHA-256 digest");
  }
  return result;
}

[[nodiscard]] ResourceKey parseResourceKey(const JsonValue& value, std::string_view where) {
  const auto& object = asObject(value, where);
  requireExactFields(object, {"id", "pack", "type"}, where);
  ResourceKey result;
  result.pack = asString(field(object, "pack", where), std::string(where) + ".pack");
  result.type = asString(field(object, "type", where), std::string(where) + ".type");
  const auto id = asInteger(field(object, "id", where), std::string(where) + ".id");
  if (!ResourceKey::isValidPack(result.pack)) {
    throw AssetManifestError(std::string(where) + ".pack is not a safe stable pack name");
  }
  if (!ResourceKey::isValidType(result.type)) {
    throw AssetManifestError(std::string(where) + ".type must be four printable bytes");
  }
  if (id < std::numeric_limits<std::int16_t>::min() ||
      id > std::numeric_limits<std::int16_t>::max()) {
    throw AssetManifestError(std::string(where) + ".id is outside signed 16-bit range");
  }
  result.id = static_cast<std::int16_t>(id);
  return result;
}

[[nodiscard]] AssetPoint parsePoint(const JsonValue& value, std::string_view where) {
  const auto& object = asObject(value, where);
  requireExactFields(object, {"x", "y"}, where);
  const auto x = asInteger(field(object, "x", where), std::string(where) + ".x");
  const auto y = asInteger(field(object, "y", where), std::string(where) + ".y");
  if (x < std::numeric_limits<std::int32_t>::min() ||
      x > std::numeric_limits<std::int32_t>::max() ||
      y < std::numeric_limits<std::int32_t>::min() ||
      y > std::numeric_limits<std::int32_t>::max()) {
    throw AssetManifestError(std::string(where) + " coordinate is outside 32-bit range");
  }
  return {.x = static_cast<std::int32_t>(x), .y = static_cast<std::int32_t>(y)};
}

[[nodiscard]] std::optional<AssetPoint> parseOptionalPoint(
    const JsonValue& value, std::string_view where) {
  if (value.isNull()) {
    return std::nullopt;
  }
  return parsePoint(value, where);
}

[[nodiscard]] AssetDimensions parseDimensions(const JsonValue& value, std::string_view where) {
  const auto& object = asObject(value, where);
  requireExactFields(object, {"height", "width"}, where);
  const auto width = asInteger(field(object, "width", where), std::string(where) + ".width");
  const auto height = asInteger(field(object, "height", where), std::string(where) + ".height");
  if (width < 1 || width > 16384 || height < 1 || height > 16384) {
    throw AssetManifestError(std::string(where) + " must be within 1..16384");
  }
  return {
      .width = static_cast<std::uint32_t>(width),
      .height = static_cast<std::uint32_t>(height),
  };
}

[[nodiscard]] std::optional<std::string> parseOptionalString(
    const JsonValue& value, std::string_view where) {
  if (value.isNull()) {
    return std::nullopt;
  }
  return asString(value, where);
}

[[nodiscard]] std::vector<std::string> parseStringArray(
    const JsonValue& value, std::string_view where) {
  std::vector<std::string> result;
  const auto& array = asArray(value, where);
  result.reserve(array.size());
  for (std::size_t index = 0; index < array.size(); ++index) {
    const auto itemWhere = std::string(where) + "[" + std::to_string(index) + "]";
    auto item = asString(array[index], itemWhere);
    if (item.empty()) {
      throw AssetManifestError(itemWhere + " must be non-empty");
    }
    result.emplace_back(std::move(item));
  }
  return result;
}

[[nodiscard]] std::uint32_t rotateRight(std::uint32_t value, int amount) {
  return std::rotr(value, amount);
}

[[nodiscard]] std::string sha256(std::string_view input) {
  constexpr std::array<std::uint32_t, 64> constants = {
      0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U,
      0x923F82A4U, 0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
      0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U,
      0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
      0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U,
      0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
      0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
      0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
      0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU,
      0x5B9CCA4FU, 0x682E6FF3U, 0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
      0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
  };
  std::array<std::uint32_t, 8> hash = {
      0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
      0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
  };
  std::vector<std::uint8_t> bytes(input.begin(), input.end());
  const auto inputBits = static_cast<std::uint64_t>(bytes.size()) * 8U;
  bytes.push_back(0x80U);
  while ((bytes.size() % 64U) != 56U) {
    bytes.push_back(0);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<std::uint8_t>(inputBits >> shift));
  }

  for (std::size_t block = 0; block < bytes.size(); block += 64) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      const auto offset = block + index * 4;
      words[index] = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
          (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
          (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
          static_cast<std::uint32_t>(bytes[offset + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      const auto s0 = rotateRight(words[index - 15], 7) ^
          rotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
      const auto s1 = rotateRight(words[index - 2], 17) ^
          rotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    auto a = hash[0];
    auto b = hash[1];
    auto c = hash[2];
    auto d = hash[3];
    auto e = hash[4];
    auto f = hash[5];
    auto g = hash[6];
    auto h = hash[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
      const auto sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
      const auto choose = (e & f) ^ ((~e) & g);
      const auto temporary1 = h + sum1 + choose + constants[index] + words[index];
      const auto sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (const auto word : hash) {
    result << std::setw(8) << word;
  }
  return result.str();
}

struct CityBaseline {
  std::string declaredVersion;
  std::string kind;
  std::string pack;
  std::string sourceSha256;

  auto operator<=>(const CityBaseline&) const = default;
};

[[nodiscard]] CityBaseline parseCityBaseline(const JsonValue& value, std::string_view where) {
  const auto& object = asObject(value, where);
  requireExactFields(object, {"declared_version", "kind", "pack", "source_sha256"}, where);
  CityBaseline result{
      .declaredVersion = asString(field(object, "declared_version", where),
          std::string(where) + ".declared_version"),
      .kind = asString(field(object, "kind", where), std::string(where) + ".kind"),
      .pack = asString(field(object, "pack", where), std::string(where) + ".pack"),
      .sourceSha256 = asSha256(field(object, "source_sha256", where),
          std::string(where) + ".source_sha256"),
  };
  if (result.declaredVersion.empty() || !ResourceKey::isValidPack(result.pack)) {
    throw AssetManifestError(std::string(where) + " contains invalid baseline metadata");
  }
  if (result.kind != "repository-bundled" &&
      result.kind != "user-supplied-sha256-gated") {
    throw AssetManifestError(std::string(where) + ".kind is invalid");
  }
  if (result.kind == "repository-bundled" && result.declaredVersion.find("7.1.2") != std::string::npos) {
    throw AssetManifestError("repository City data cannot be declared as Mac 7.1.2");
  }
  return result;
}

struct CensusEntry {
  ResourceKey key;
  ResourceKey masterKey;
  std::string classicPayloadSha256;
  std::string alphaPolicy;
  AssetDimensions dimensions;
  AssetPoint anchor;
  std::optional<AssetPoint> hotspot;
};

[[nodiscard]] CensusEntry parseCensusEntry(const JsonValue& value, std::string_view where) {
  const auto& object = asObject(value, where);
  requireExactFields(object,
      {"alpha_policy", "anchor", "classic_payload_bytes", "classic_payload_sha256",
          "content_address", "flags", "hotspot", "key", "logical_dimensions", "master_key",
          "source_file_sha256", "source_resource_name"},
      where);
  CensusEntry result{
      .key = parseResourceKey(field(object, "key", where), std::string(where) + ".key"),
      .masterKey = parseResourceKey(
          field(object, "master_key", where), std::string(where) + ".master_key"),
      .classicPayloadSha256 = asSha256(field(object, "classic_payload_sha256", where),
          std::string(where) + ".classic_payload_sha256"),
      .alphaPolicy = asString(
          field(object, "alpha_policy", where), std::string(where) + ".alpha_policy"),
      .dimensions = parseDimensions(
          field(object, "logical_dimensions", where), std::string(where) + ".logical_dimensions"),
      .anchor = parsePoint(field(object, "anchor", where), std::string(where) + ".anchor"),
      .hotspot = parseOptionalPoint(
          field(object, "hotspot", where), std::string(where) + ".hotspot"),
  };
  const auto payloadBytes = asInteger(
      field(object, "classic_payload_bytes", where), std::string(where) + ".classic_payload_bytes");
  const auto flags = asInteger(field(object, "flags", where), std::string(where) + ".flags");
  if (payloadBytes < 0 || flags < 0 || flags > 255) {
    throw AssetManifestError(std::string(where) + " has invalid byte count or flags");
  }
  const auto& contentAddress = asString(
      field(object, "content_address", where), std::string(where) + ".content_address");
  if (contentAddress != "sha256:" + result.classicPayloadSha256) {
    throw AssetManifestError(std::string(where) + ".content_address does not match payload hash");
  }
  (void)asSha256(field(object, "source_file_sha256", where),
      std::string(where) + ".source_file_sha256");
  (void)asString(field(object, "source_resource_name", where),
      std::string(where) + ".source_resource_name");
  if (result.alphaPolicy != "opaque_or_embedded_mask" &&
      result.alphaPolicy != "original_mask" && result.alphaPolicy != "opaque_tile") {
    throw AssetManifestError(std::string(where) + ".alpha_policy is invalid");
  }
  if (result.hotspot &&
      (result.hotspot->x < 0 || result.hotspot->y < 0 ||
          static_cast<std::uint32_t>(result.hotspot->x) >= result.dimensions.width ||
          static_cast<std::uint32_t>(result.hotspot->y) >= result.dimensions.height)) {
    throw AssetManifestError(std::string(where) + ".hotspot is outside logical dimensions");
  }
  return result;
}

struct ParsedCensus {
  std::string scope;
  CityBaseline cityBaseline;
  std::map<ResourceKey, CensusEntry> entries;
  std::size_t uniqueMasters = 0;
};

[[nodiscard]] ParsedCensus parseCensus(const JsonValue& root) {
  const auto& object = asObject(root, "census");
  requireExactFields(object,
      {"city_baseline", "entries", "generated_by", "schema_version", "scope",
          "selected_resource_types", "source_files", "statistics"},
      "census");
  if (asInteger(field(object, "schema_version", "census"), "census.schema_version") !=
      kSchemaVersion) {
    throw AssetManifestError("census.schema_version is unsupported");
  }
  if (asString(field(object, "generated_by", "census"), "census.generated_by") !=
      "scripts/remaster_asset_census.py") {
    throw AssetManifestError("census.generated_by is invalid");
  }
  ParsedCensus result{
      .scope = asString(field(object, "scope", "census"), "census.scope"),
      .cityBaseline = parseCityBaseline(
          field(object, "city_baseline", "census"), "census.city_baseline"),
  };
  if (result.scope.empty()) {
    throw AssetManifestError("census.scope must be non-empty");
  }
  const auto& selectedTypes = asArray(
      field(object, "selected_resource_types", "census"), "census.selected_resource_types");
  constexpr std::array<std::string_view, 4> expectedTypes = {"PICT", "cicn", "crsr", "ppat"};
  if (selectedTypes.size() != expectedTypes.size()) {
    throw AssetManifestError("census.selected_resource_types is invalid");
  }
  for (std::size_t index = 0; index < expectedTypes.size(); ++index) {
    if (asString(selectedTypes[index], "census.selected_resource_types") != expectedTypes[index]) {
      throw AssetManifestError("census.selected_resource_types is invalid");
    }
  }

  const auto& sourceFiles = asArray(field(object, "source_files", "census"), "census.source_files");
  if (sourceFiles.size() != 5) {
    throw AssetManifestError("census.source_files must contain exactly five forks");
  }
  std::set<std::string> sourcePacks;
  std::size_t sourceSelectedCount = 0;
  for (std::size_t index = 0; index < sourceFiles.size(); ++index) {
    const auto where = "census.source_files[" + std::to_string(index) + "]";
    const auto& source = asObject(sourceFiles[index], where);
    requireExactFields(source,
        {"pack", "role", "selected_resource_count", "source", "source_kind", "source_sha256",
            "source_version"},
        where);
    const auto& pack = asString(field(source, "pack", where), where + ".pack");
    if (!ResourceKey::isValidPack(pack) || !sourcePacks.emplace(pack).second) {
      throw AssetManifestError(where + ".pack is invalid or duplicated");
    }
    const auto count = asInteger(
        field(source, "selected_resource_count", where), where + ".selected_resource_count");
    if (count < 0) {
      throw AssetManifestError(where + ".selected_resource_count is negative");
    }
    sourceSelectedCount += static_cast<std::size_t>(count);
    (void)asString(field(source, "role", where), where + ".role");
    (void)asString(field(source, "source", where), where + ".source");
    (void)asString(field(source, "source_kind", where), where + ".source_kind");
    (void)asSha256(field(source, "source_sha256", where), where + ".source_sha256");
    (void)asString(field(source, "source_version", where), where + ".source_version");
  }

  const auto& entries = asArray(field(object, "entries", "census"), "census.entries");
  std::optional<ResourceKey> previous;
  std::map<std::string, ResourceKey> masterForHash;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto where = "census.entries[" + std::to_string(index) + "]";
    auto entry = parseCensusEntry(entries[index], where);
    if (previous && !(previous.value() < entry.key)) {
      throw AssetManifestError("census entries must be strictly ResourceKey-sorted");
    }
    previous = entry.key;
    const auto canonical = masterForHash.emplace(entry.classicPayloadSha256, entry.key).first->second;
    if (entry.masterKey != canonical) {
      throw AssetManifestError(where + ".master_key is not the canonical duplicate master");
    }
    if (!result.entries.emplace(entry.key, std::move(entry)).second) {
      throw AssetManifestError(where + " duplicates a ResourceKey");
    }
  }
  result.uniqueMasters = masterForHash.size();
  if (sourceSelectedCount != result.entries.size()) {
    throw AssetManifestError("census source-file counts do not equal census entry count");
  }

  const auto& statistics = asObject(field(object, "statistics", "census"), "census.statistics");
  requireExactFields(statistics,
      {"by_pack", "by_type", "duplicate_resource_keys", "resource_keys",
          "unique_classic_payloads"},
      "census.statistics");
  const auto declaredKeys = asInteger(
      field(statistics, "resource_keys", "census.statistics"), "census.statistics.resource_keys");
  const auto declaredMasters = asInteger(field(statistics, "unique_classic_payloads", "census.statistics"),
      "census.statistics.unique_classic_payloads");
  const auto declaredDuplicates = asInteger(field(statistics, "duplicate_resource_keys", "census.statistics"),
      "census.statistics.duplicate_resource_keys");
  if (declaredKeys != static_cast<std::int64_t>(result.entries.size()) ||
      declaredMasters != static_cast<std::int64_t>(result.uniqueMasters) ||
      declaredDuplicates != static_cast<std::int64_t>(result.entries.size() - result.uniqueMasters)) {
    throw AssetManifestError("census.statistics totals do not match entries");
  }
  (void)asObject(field(statistics, "by_pack", "census.statistics"), "census.statistics.by_pack");
  (void)asObject(field(statistics, "by_type", "census.statistics"), "census.statistics.by_type");
  return result;
}

[[nodiscard]] GenerationProvenance parseProvenance(
    const JsonValue& value, std::string_view where) {
  const auto& object = asObject(value, where);
  requireExactFields(object, {"kind", "model", "provider"}, where);
  return {
      .kind = asString(field(object, "kind", where), std::string(where) + ".kind"),
      .model = parseOptionalString(field(object, "model", where), std::string(where) + ".model"),
      .provider = parseOptionalString(
          field(object, "provider", where), std::string(where) + ".provider"),
  };
}

[[nodiscard]] AssetManifestEntry parseManifestEntry(
    const JsonValue& value, std::string_view where) {
  const auto& object = asObject(value, where);
  requireExactFields(object,
      {"alpha_policy", "anchor", "asset_path", "atlas_order", "classic_payload_sha256",
          "generation_provenance", "hotspot", "input_sha256", "key", "logical_dimensions",
          "master_key", "post_processing", "prompt_sha256", "reviewer", "semantic_family",
          "shared_master_sha256", "status"},
      where);
  AssetManifestEntry result{
      .key = parseResourceKey(field(object, "key", where), std::string(where) + ".key"),
      .masterKey = parseResourceKey(
          field(object, "master_key", where), std::string(where) + ".master_key"),
      .classicPayloadSha256 = asSha256(field(object, "classic_payload_sha256", where),
          std::string(where) + ".classic_payload_sha256"),
      .sharedMasterSha256 = asSha256(field(object, "shared_master_sha256", where),
          std::string(where) + ".shared_master_sha256"),
      .inputSha256 = asSha256(
          field(object, "input_sha256", where), std::string(where) + ".input_sha256"),
      .semanticFamily = asString(
          field(object, "semantic_family", where), std::string(where) + ".semantic_family"),
      .alphaPolicy = asString(
          field(object, "alpha_policy", where), std::string(where) + ".alpha_policy"),
      .logicalDimensions = parseDimensions(
          field(object, "logical_dimensions", where), std::string(where) + ".logical_dimensions"),
      .anchor = parsePoint(field(object, "anchor", where), std::string(where) + ".anchor"),
      .hotspot = parseOptionalPoint(
          field(object, "hotspot", where), std::string(where) + ".hotspot"),
      .promptSha256 = parseOptionalString(
          field(object, "prompt_sha256", where), std::string(where) + ".prompt_sha256"),
      .provenance = parseProvenance(field(object, "generation_provenance", where),
          std::string(where) + ".generation_provenance"),
      .postProcessing = parseStringArray(
          field(object, "post_processing", where), std::string(where) + ".post_processing"),
      .reviewer = parseOptionalString(
          field(object, "reviewer", where), std::string(where) + ".reviewer"),
  };
  if (result.semanticFamily.empty()) {
    throw AssetManifestError(std::string(where) + ".semantic_family must be non-empty");
  }
  if (field(object, "atlas_order", where).isNull()) {
    result.atlasOrder = std::nullopt;
  } else {
    const auto order = asInteger(
        field(object, "atlas_order", where), std::string(where) + ".atlas_order");
    if (order < 0 || order > std::numeric_limits<std::uint32_t>::max()) {
      throw AssetManifestError(std::string(where) + ".atlas_order is out of range");
    }
    result.atlasOrder = static_cast<std::uint32_t>(order);
  }
  if (field(object, "asset_path", where).isNull()) {
    result.assetPath = std::nullopt;
  } else {
    result.assetPath = asString(
        field(object, "asset_path", where), std::string(where) + ".asset_path");
  }
  const auto& status = asString(field(object, "status", where), std::string(where) + ".status");
  if (status == "classic_passthrough") {
    result.status = AssetStatus::ClassicPassthrough;
  } else if (status == "approved") {
    result.status = AssetStatus::Approved;
  } else {
    throw AssetManifestError(std::string(where) + ".status is invalid");
  }
  return result;
}

[[nodiscard]] bool pathIsWithin(
    const std::filesystem::path& child, const std::filesystem::path& parent) {
  auto childIterator = child.begin();
  for (auto parentIterator = parent.begin(); parentIterator != parent.end();
       ++parentIterator, ++childIterator) {
    if (childIterator == child.end() || *childIterator != *parentIterator) {
      return false;
    }
  }
  return true;
}

} // namespace

std::string ResourceKey::toString() const {
  return pack + ":" + type + ":" + std::to_string(id);
}

bool ResourceKey::isValidPack(std::string_view pack) {
  if (pack.empty() || pack.front() == '/' || pack.back() == '/' || pack.find('\\') != pack.npos) {
    return false;
  }
  std::size_t start = 0;
  while (start < pack.size()) {
    const auto end = pack.find('/', start);
    const auto component = pack.substr(start, end == pack.npos ? pack.size() - start : end - start);
    if (component.empty() || component == "." || component == "..") {
      return false;
    }
    if (std::ranges::any_of(component, [](unsigned char ch) { return ch < 0x20U; })) {
      return false;
    }
    if (end == pack.npos) {
      break;
    }
    start = end + 1;
  }
  return true;
}

bool ResourceKey::isValidType(std::string_view type) {
  return type.size() == 4 && std::ranges::all_of(type, [](unsigned char ch) {
    return ch >= 0x20U && ch <= 0x7EU;
  });
}

bool AssetManifest::isSafeAssetPath(std::string_view path) {
  if (!ResourceKey::isValidPack(path) || path.size() < 4 || !path.ends_with(".png")) {
    return false;
  }
  return path.find(':') == path.npos;
}

AssetManifest AssetManifest::load(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& censusPath,
    const std::filesystem::path& assetRoot) {
  const auto censusContents = loadFile(censusPath);
  const auto parsedCensus = parseCensus(JsonParser(censusContents).parse());
  const auto manifestContents = loadFile(manifestPath);
  const auto parsedManifest = JsonParser(manifestContents).parse();
  const auto& root = asObject(parsedManifest, "manifest");
  requireExactFields(root,
      {"census_sha256", "city_baseline", "entries", "manifest_kind", "schema_version", "scope"},
      "manifest");
  if (asInteger(field(root, "schema_version", "manifest"), "manifest.schema_version") !=
      kSchemaVersion) {
    throw AssetManifestError("manifest.schema_version is unsupported");
  }
  const auto& manifestScope = asString(field(root, "scope", "manifest"), "manifest.scope");
  if (manifestScope != parsedCensus.scope) {
    throw AssetManifestError("manifest.scope does not match census.scope");
  }
  if (asSha256(field(root, "census_sha256", "manifest"), "manifest.census_sha256") !=
      sha256(censusContents)) {
    throw AssetManifestError("manifest.census_sha256 does not match the exact census bytes");
  }
  if (parseCityBaseline(field(root, "city_baseline", "manifest"), "manifest.city_baseline") !=
      parsedCensus.cityBaseline) {
    throw AssetManifestError("manifest.city_baseline does not match census.city_baseline");
  }
  const auto& manifestKind = asString(
      field(root, "manifest_kind", "manifest"), "manifest.manifest_kind");
  if (manifestKind != "zero-cost-placeholder" && manifestKind != "production") {
    throw AssetManifestError("manifest.manifest_kind is invalid");
  }

  AssetManifest result;
  result.scope_ = manifestScope;
  std::error_code error;
  result.assetRoot_ = std::filesystem::weakly_canonical(assetRoot, error);
  if (error) {
    throw AssetManifestError("cannot canonicalize asset root: " + error.message());
  }
  result.uniqueMasterCount_ = parsedCensus.uniqueMasters;
  const auto& entries = asArray(field(root, "entries", "manifest"), "manifest.entries");
  std::map<std::string, std::filesystem::path> approvedPathForMaster;
  std::optional<ResourceKey> previousManifestKey;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto where = "manifest.entries[" + std::to_string(index) + "]";
    auto entry = parseManifestEntry(entries[index], where);
    if (previousManifestKey && !(previousManifestKey.value() < entry.key)) {
      throw AssetManifestError("manifest entries must be strictly ResourceKey-sorted");
    }
    previousManifestKey = entry.key;
    const auto censusIterator = parsedCensus.entries.find(entry.key);
    if (censusIterator == parsedCensus.entries.end()) {
      throw AssetManifestError(where + " is not present in the selected census");
    }
    const auto& censusEntry = censusIterator->second;
    if (entry.masterKey != censusEntry.masterKey ||
        entry.classicPayloadSha256 != censusEntry.classicPayloadSha256 ||
        entry.alphaPolicy != censusEntry.alphaPolicy ||
        entry.logicalDimensions != censusEntry.dimensions || entry.anchor != censusEntry.anchor ||
        entry.hotspot != censusEntry.hotspot) {
      throw AssetManifestError(where + " does not preserve its census constraints");
    }
    if (entry.inputSha256 != entry.classicPayloadSha256) {
      throw AssetManifestError(where + " input hash does not match the Classic payload");
    }
    if (entry.status == AssetStatus::ClassicPassthrough) {
      if (entry.sharedMasterSha256 != entry.classicPayloadSha256) {
        throw AssetManifestError(where + " passthrough master hash is not the Classic payload");
      }
      if (entry.assetPath || entry.promptSha256 || entry.reviewer || entry.provenance.kind != "none" ||
          entry.provenance.model || entry.provenance.provider) {
        throw AssetManifestError(where + " passthrough entry claims override or generation metadata");
      }
    } else {
      if (!entry.assetPath || !isSafeAssetPath(entry.assetPath->generic_string())) {
        throw AssetManifestError(where + ".asset_path is unsafe or is not a PNG path");
      }
      if (!entry.promptSha256 || !isSha256(*entry.promptSha256) || !entry.reviewer ||
          entry.reviewer->empty() || entry.provenance.kind != "imagegen" ||
          !entry.provenance.model || entry.provenance.model->empty() ||
          !entry.provenance.provider || entry.provenance.provider->empty()) {
        throw AssetManifestError(where + " approved entry lacks review or ImageGen provenance");
      }
      const auto diskPath = std::filesystem::weakly_canonical(result.assetRoot_ / *entry.assetPath, error);
      if (error || !pathIsWithin(diskPath, result.assetRoot_) ||
          !std::filesystem::is_regular_file(diskPath)) {
        throw AssetManifestError(where + ".asset_path is missing or escapes the asset root");
      }
      if (sha256(loadFile(diskPath)) != entry.sharedMasterSha256) {
        throw AssetManifestError(where + ".asset_path SHA-256 does not match shared_master_sha256");
      }
      const auto [iterator, inserted] = approvedPathForMaster.emplace(
          entry.sharedMasterSha256, entry.assetPath.value());
      if (!inserted && iterator->second != entry.assetPath.value()) {
        throw AssetManifestError(where + " does not reuse its approved duplicate master path");
      }
    }
    if (manifestKind == "zero-cost-placeholder" &&
        entry.status != AssetStatus::ClassicPassthrough) {
      throw AssetManifestError("zero-cost-placeholder manifest contains an approved override");
    }
    if (!result.entries_.emplace(entry.key, std::move(entry)).second) {
      throw AssetManifestError(where + " duplicates a manifest ResourceKey");
    }
  }
  if (result.entries_.size() != parsedCensus.entries.size()) {
    throw AssetManifestError(
        "manifest/census parity failure: manifest has " + std::to_string(result.entries_.size()) +
        " entries; census has " + std::to_string(parsedCensus.entries.size()));
  }
  return result;
}

const AssetManifestEntry* AssetManifest::find(const ResourceKey& key) const noexcept {
  const auto iterator = entries_.find(key);
  return iterator == entries_.end() ? nullptr : &iterator->second;
}

std::size_t AssetManifest::size() const noexcept {
  return entries_.size();
}

std::size_t AssetManifest::uniqueMasterCount() const noexcept {
  return uniqueMasterCount_;
}

std::size_t AssetManifest::duplicateReuseCount() const noexcept {
  return entries_.size() - uniqueMasterCount_;
}

const std::string& AssetManifest::scope() const noexcept {
  return scope_;
}

const std::filesystem::path& AssetManifest::assetRoot() const noexcept {
  return assetRoot_;
}

} // namespace realmz::remaster::assets
