#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "remaster/assets/ResourceKey.hpp"

namespace realmz::remaster::assets {

// Returns the lowercase SHA-256 digest of exact public asset bytes. This is
// separate from Classic resource-payload hashing so native shell consumers do
// not need to use the private resource-selection path.
[[nodiscard]] std::string assetContentSha256Hex(std::string_view content);

struct AssetPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;

  auto operator<=>(const AssetPoint&) const = default;
};

struct AssetDimensions {
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  auto operator<=>(const AssetDimensions&) const = default;
};

enum class AssetStatus {
  ClassicPassthrough,
  Approved,
};

struct GenerationProvenance {
  std::string kind;
  std::optional<std::string> model;
  std::optional<std::string> provider;
};

struct AssetManifestEntry {
  ResourceKey key;
  ResourceKey masterKey;
  std::string classicPayloadSha256;
  std::string sharedMasterSha256;
  std::string inputSha256;
  std::string semanticFamily;
  std::string alphaPolicy;
  AssetDimensions logicalDimensions;
  AssetPoint anchor;
  std::optional<AssetPoint> hotspot;
  std::optional<std::uint32_t> atlasOrder;
  std::optional<std::filesystem::path> assetPath;
  std::optional<std::string> promptSha256;
  GenerationProvenance provenance;
  std::vector<std::string> postProcessing;
  std::optional<std::string> reviewer;
  AssetStatus status = AssetStatus::ClassicPassthrough;
};

class AssetManifestError : public std::runtime_error {
public:
  explicit AssetManifestError(const std::string& message) : std::runtime_error(message) {}
};

class AssetManifest {
public:
  [[nodiscard]] static AssetManifest load(
      const std::filesystem::path& manifestPath,
      const std::filesystem::path& censusPath,
      const std::filesystem::path& assetRoot);

  [[nodiscard]] const AssetManifestEntry* find(const ResourceKey& key) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t uniqueMasterCount() const noexcept;
  [[nodiscard]] std::size_t duplicateReuseCount() const noexcept;
  [[nodiscard]] const std::string& scope() const noexcept;
  [[nodiscard]] const std::filesystem::path& assetRoot() const noexcept;

  [[nodiscard]] static bool isSafeAssetPath(std::string_view path);

private:
  std::string scope_;
  std::filesystem::path assetRoot_;
  std::size_t uniqueMasterCount_ = 0;
  std::unordered_map<ResourceKey, AssetManifestEntry, ResourceKeyHash> entries_;
};

} // namespace realmz::remaster::assets
