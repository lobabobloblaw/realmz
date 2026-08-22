#include "remaster/assets/ShellMaterialCatalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "remaster/assets/AssetResolver.hpp"

namespace realmz::remaster::assets {
namespace {

constexpr std::string_view kShellMaterialPack =
    "Data Files/The Family Jewels";
constexpr std::string_view kShellMaterialType = "ppat";
constexpr AssetDimensions kShellMaterialDimensions{64U, 64U};

struct ShellMaterialRequirement {
  ShellMaterialRole role;
  std::int16_t resourceId;
};

constexpr std::array<ShellMaterialRequirement, 4> kRequirements{
    ShellMaterialRequirement{ShellMaterialRole::highlight, 128},
    ShellMaterialRequirement{ShellMaterialRole::neutral, 129},
    ShellMaterialRequirement{ShellMaterialRole::shadow, 130},
    ShellMaterialRequirement{ShellMaterialRole::panel, 131},
};

[[noreturn]] void fail(
    const ResourceKey& key, std::string_view reason) {
  throw ShellMaterialCatalogError(
      "Shell material " + key.toString() + " " + std::string(reason));
}

} // namespace

ShellMaterialCatalog::ShellMaterialCatalog(Materials materials)
    : materials_(std::move(materials)) {}

ShellMaterialCatalog ShellMaterialCatalog::load(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& censusPath,
    const std::filesystem::path& assetRoot) {
  AssetResolver resolver(
      AssetManifest::load(manifestPath, censusPath, assetRoot));
  const auto& manifest = resolver.manifest();
  Materials materials;

  for (std::size_t index = 0; index < kRequirements.size(); ++index) {
    const auto requirement = kRequirements[index];
    const ResourceKey key{
        .pack = std::string(kShellMaterialPack),
        .type = std::string(kShellMaterialType),
        .id = requirement.resourceId,
    };
    const auto* entry = manifest.find(key);
    if (entry == nullptr) {
      fail(key, "is missing from the manifest");
    }

    const auto resolution = resolver.resolve(
        presentation::PresentationMode::remastered, key);
    if (resolution.kind != AssetResolutionKind::Override) {
      fail(key, "did not resolve to an approved override");
    }
    if (entry->key != key || entry->masterKey != key ||
        !resolution.masterKey || *resolution.masterKey != entry->masterKey) {
      fail(key, "does not preserve its exact resource and master key");
    }
    if (entry->status != AssetStatus::Approved) {
      fail(key, "is not approved");
    }
    if (entry->semanticFamily != "ui_surface") {
      fail(key, "does not belong to the ui_surface semantic family");
    }
    if (entry->alphaPolicy != "opaque_tile") {
      fail(key, "does not use the opaque_tile alpha policy");
    }
    if (entry->logicalDimensions != kShellMaterialDimensions) {
      fail(key, "does not have 64x64 logical dimensions");
    }
    if (!entry->assetPath || !resolution.overridePath ||
        *resolution.overridePath != manifest.assetRoot() / *entry->assetPath) {
      fail(key, "does not preserve its manifest override path");
    }
    if (!resolution.logicalDimensions ||
        *resolution.logicalDimensions != entry->logicalDimensions) {
      fail(key, "does not preserve its manifest logical dimensions");
    }

    for (std::size_t previous = 0; previous < index; ++previous) {
      if (materials[previous].path == *resolution.overridePath) {
        fail(key, "reuses another shell material path");
      }
    }

    materials[index] = ShellMaterialDescriptor{
        .role = requirement.role,
        .key = key,
        .path = *resolution.overridePath,
        .logicalDimensions = *resolution.logicalDimensions,
        .sharedMasterSha256 = entry->sharedMasterSha256,
    };
  }

  return ShellMaterialCatalog(std::move(materials));
}

const ShellMaterialDescriptor* ShellMaterialCatalog::find(
    ShellMaterialRole role) const noexcept {
  for (const auto& material : this->materials_) {
    if (material.role == role) {
      return &material;
    }
  }
  return nullptr;
}

const ShellMaterialCatalog::Materials&
ShellMaterialCatalog::materials() const noexcept {
  return this->materials_;
}

} // namespace realmz::remaster::assets
