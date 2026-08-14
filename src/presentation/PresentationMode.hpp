#pragma once

#include <optional>
#include <string_view>

namespace realmz::presentation {

enum class PresentationMode {
  classic,
  remastered,
};

[[nodiscard]] constexpr std::string_view to_string(PresentationMode mode) noexcept {
  switch (mode) {
    case PresentationMode::classic:
      return "classic";
    case PresentationMode::remastered:
      return "remastered";
  }
  return "classic";
}

[[nodiscard]] constexpr std::optional<PresentationMode> presentation_mode_from_string(
    std::string_view value) noexcept {
  if (value == "classic") {
    return PresentationMode::classic;
  }
  if (value == "remastered") {
    return PresentationMode::remastered;
  }
  return std::nullopt;
}

} // namespace realmz::presentation
