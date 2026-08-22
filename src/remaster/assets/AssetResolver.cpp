#include "remaster/assets/AssetResolver.hpp"

#include <utility>

namespace realmz::remaster::assets {

AssetResolver::AssetResolver(AssetManifest manifest) : manifest_(std::move(manifest)) {}

AssetResolution AssetResolver::resolve(
    presentation::PresentationMode mode, const ResourceKey& selectedResource) const {
  if (mode == presentation::PresentationMode::classic) {
    return {
        .kind = AssetResolutionKind::ClassicBypass,
        .overridePath = std::nullopt,
        .masterKey = std::nullopt,
        .diagnostic = "Classic presentation bypasses remastered overrides",
    };
  }

  const auto* entry = manifest_.find(selectedResource);
  if (entry == nullptr) {
    return {
        .kind = AssetResolutionKind::CoverageFailure,
        .overridePath = std::nullopt,
        .masterKey = std::nullopt,
        .diagnostic = "No approved manifest coverage for " + selectedResource.toString(),
    };
  }
  if (entry->status == AssetStatus::ClassicPassthrough) {
    return {
        .kind = AssetResolutionKind::ClassicPassthrough,
        .overridePath = std::nullopt,
        .masterKey = entry->masterKey,
        .diagnostic = "Zero-cost placeholder uses the selected Classic resource",
    };
  }
  return {
      .kind = AssetResolutionKind::Override,
      .overridePath = manifest_.assetRoot() / *entry->assetPath,
      .masterKey = entry->masterKey,
      .logicalDimensions = entry->logicalDimensions,
      .approvedContentSha256 = entry->sharedMasterSha256,
      .diagnostic = "Approved remastered override",
  };
}

AssetResolution AssetResolver::resolve(
    presentation::PresentationMode mode, const ResourceKey& selectedResource,
    std::string_view classicPayloadSha256) const {
  if (mode == presentation::PresentationMode::classic) {
    return resolve(mode, selectedResource);
  }

  const auto* entry = manifest_.find(selectedResource);
  if (entry == nullptr) {
    return resolve(mode, selectedResource);
  }
  if (classicPayloadSha256 != entry->classicPayloadSha256) {
    return {
        .kind = AssetResolutionKind::CoverageFailure,
        .overridePath = std::nullopt,
        .masterKey = entry->masterKey,
        .diagnostic = "Classic payload SHA-256 mismatch for " +
            selectedResource.toString() + ": expected " +
            entry->classicPayloadSha256 + ", got " +
            std::string(classicPayloadSha256),
    };
  }
  return resolve(mode, selectedResource);
}

const AssetManifest& AssetResolver::manifest() const noexcept {
  return manifest_;
}

} // namespace realmz::remaster::assets
