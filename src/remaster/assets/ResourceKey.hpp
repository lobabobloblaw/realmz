#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace realmz::remaster::assets {

struct ResourceKey {
  std::string pack;
  std::string type;
  std::int16_t id = 0;

  auto operator<=>(const ResourceKey&) const = default;

  [[nodiscard]] std::string toString() const;
  [[nodiscard]] static bool isValidPack(std::string_view pack);
  [[nodiscard]] static bool isValidType(std::string_view type);
};

struct ResourceKeyHash {
  [[nodiscard]] std::size_t operator()(const ResourceKey& key) const noexcept {
    std::size_t value = std::hash<std::string>{}(key.pack);
    value ^= std::hash<std::string>{}(key.type) + 0x9E3779B9U + (value << 6) + (value >> 2);
    value ^= std::hash<std::int16_t>{}(key.id) + 0x9E3779B9U + (value << 6) + (value >> 2);
    return value;
  }
};

} // namespace realmz::remaster::assets
