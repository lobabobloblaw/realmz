#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "presentation/PresentationMode.hpp"
#include "remaster/assets/AssetResolver.hpp"

namespace realmz::remaster::assets {

// Converts the application-relative Classic open request captured from an
// FSSpec into the stable, host-location-independent pack name used by the
// census. User-preferences FSSpecs deliberately have no content-pack identity.
[[nodiscard]] std::optional<std::string> logicalPackForOpenRequest(
    std::string_view classicPath, bool applicationRelative);

[[nodiscard]] std::string resourceTypeString(std::uint32_t type);
[[nodiscard]] bool isPhaseOneRasterResourceType(std::uint32_t type) noexcept;

struct PostSelectionResult {
  ResourceKey key;
  // Empty for ClassicBypass: Classic mode neither loads the manifest nor
  // spends time hashing resources it will return unchanged.
  std::string immutablePayloadSha256;
  AssetResolution resolution;
};

// A post-selection boundary: callers invoke inspect only after legacy search
// precedence has chosen the actual ResourceFile::Resource. The input payload
// must be that immutable source object's data, never its mutable Handle copy.
class ResourceSelectionHook {
public:
  ResourceSelectionHook(
      std::filesystem::path manifestPath,
      std::filesystem::path censusPath,
      std::filesystem::path assetRoot);
  ~ResourceSelectionHook();

  ResourceSelectionHook(ResourceSelectionHook&&) noexcept;
  ResourceSelectionHook& operator=(ResourceSelectionHook&&) noexcept;
  ResourceSelectionHook(const ResourceSelectionHook&) = delete;
  ResourceSelectionHook& operator=(const ResourceSelectionHook&) = delete;

  void setPresentationMode(presentation::PresentationMode mode) noexcept;
  [[nodiscard]] presentation::PresentationMode presentationMode() const noexcept;

  [[nodiscard]] PostSelectionResult inspect(
      std::string_view logicalPack, std::uint32_t type, std::int16_t id,
      std::string_view immutableSourcePayload);

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace realmz::remaster::assets
