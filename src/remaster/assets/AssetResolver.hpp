#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "remaster/assets/AssetManifest.hpp"
#include "presentation/PresentationMode.hpp"

namespace realmz::remaster::assets {

enum class AssetResolutionKind {
  ClassicBypass,
  Override,
  ClassicPassthrough,
  CoverageFailure,
};

struct AssetResolution {
  AssetResolutionKind kind = AssetResolutionKind::CoverageFailure;
  std::optional<std::filesystem::path> overridePath;
  std::optional<ResourceKey> masterKey;
  std::optional<AssetDimensions> logicalDimensions;
  // Exact approved PNG bytes. Consumers must re-check this digest against
  // the bytes they decode instead of reopening an earlier-validated path.
  std::optional<std::string> approvedContentSha256;
  std::string diagnostic;

  [[nodiscard]] bool covered() const noexcept {
    return kind != AssetResolutionKind::CoverageFailure;
  }
};

class AssetResolver {
public:
  explicit AssetResolver(AssetManifest manifest);

  [[nodiscard]] AssetResolution resolve(
      presentation::PresentationMode mode, const ResourceKey& selectedResource) const;
  // Live resource selection supplies the digest of the immutable resource-fork
  // payload. This prevents a caller-mutated Classic Handle from changing the
  // identity used for manifest coverage.
  [[nodiscard]] AssetResolution resolve(
      presentation::PresentationMode mode, const ResourceKey& selectedResource,
      std::string_view classicPayloadSha256) const;
  [[nodiscard]] const AssetManifest& manifest() const noexcept;

private:
  AssetManifest manifest_;
};

} // namespace realmz::remaster::assets
