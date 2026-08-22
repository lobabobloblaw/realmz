#pragma once

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "remaster/assets/AssetManifest.hpp"

namespace realmz::remaster::assets {

struct PostSelectionResult;

struct PartyPortraitDescriptor {
  ResourceKey key;
  std::filesystem::path path;
  AssetDimensions logicalDimensions;
  std::string classicPayloadSha256;
  std::string approvedContentSha256;

  bool operator==(const PartyPortraitDescriptor&) const = default;
};

class PartyPortraitCatalogError : public std::runtime_error {
public:
  explicit PartyPortraitCatalogError(const std::string& message)
      : std::runtime_error(message) {}
};

// Public metadata and live-resource authorization are deliberately separate.
// Loading proves that the four approved portrait records are internally valid;
// it does not authorize any portrait to be drawn for an ID alone. A renderer
// must pass the fresh post-selection result for the actual Resource Manager
// winner to authorizeSelectedPortrait immediately before using a descriptor.
class PartyPortraitCatalog {
public:
  using Portraits = std::array<PartyPortraitDescriptor, 4>;

  [[nodiscard]] static PartyPortraitCatalog load(
      const std::filesystem::path& manifestPath,
      const std::filesystem::path& censusPath,
      const std::filesystem::path& assetRoot);

  // Returns a descriptor only when every proof field still equals the loaded
  // approved record. There is intentionally no portrait-ID lookup or
  // authorization overload.
  [[nodiscard]] const PartyPortraitDescriptor* authorizeSelectedPortrait(
      const PostSelectionResult& selection) const noexcept;

  // Stable metadata order: cicn 257, 267, 297, 337. Enumeration is not draw
  // authorization; callers must still supply a matching post-selection proof.
  [[nodiscard]] const Portraits& portraits() const noexcept;

private:
  explicit PartyPortraitCatalog(Portraits portraits);

  Portraits portraits_;
};

} // namespace realmz::remaster::assets
