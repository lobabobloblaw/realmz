#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include "remaster/assets/AssetManifest.hpp"

namespace realmz::remaster::assets {

enum class ShellMaterialRole {
  highlight,
  neutral,
  shadow,
  panel,
};

enum class ShellSurfaceState {
  panel,
  normal,
  selected,
  pressed,
  inactive,
};

// This policy is deliberately independent of rendering and asset loading. An
// unknown enum value has no material role and therefore cannot select a path.
[[nodiscard]] constexpr std::optional<ShellMaterialRole>
shellMaterialRoleForSurfaceState(ShellSurfaceState state) noexcept {
  switch (state) {
    case ShellSurfaceState::panel:
      return ShellMaterialRole::panel;
    case ShellSurfaceState::normal:
      return ShellMaterialRole::neutral;
    case ShellSurfaceState::selected:
      return ShellMaterialRole::highlight;
    case ShellSurfaceState::pressed:
    case ShellSurfaceState::inactive:
      return ShellMaterialRole::shadow;
  }
  return std::nullopt;
}

struct ShellMaterialDescriptor {
  ShellMaterialRole role = ShellMaterialRole::neutral;
  ResourceKey key;
  std::filesystem::path path;
  AssetDimensions logicalDimensions;
  std::string sharedMasterSha256;

  bool operator==(const ShellMaterialDescriptor&) const = default;
};

class ShellMaterialCatalogError : public std::runtime_error {
public:
  explicit ShellMaterialCatalogError(const std::string& message)
      : std::runtime_error(message) {}
};

class ShellMaterialCatalog {
public:
  using Materials = std::array<ShellMaterialDescriptor, 4>;

  [[nodiscard]] static ShellMaterialCatalog load(
      const std::filesystem::path& manifestPath,
      const std::filesystem::path& censusPath,
      const std::filesystem::path& assetRoot);

  // Returns null for an invalid role instead of indexing by its enum value.
  [[nodiscard]] const ShellMaterialDescriptor* find(
      ShellMaterialRole role) const noexcept;

  // Stable order: highlight, neutral, shadow, panel.
  [[nodiscard]] const Materials& materials() const noexcept;

private:
  explicit ShellMaterialCatalog(Materials materials);

  Materials materials_;
};

} // namespace realmz::remaster::assets
