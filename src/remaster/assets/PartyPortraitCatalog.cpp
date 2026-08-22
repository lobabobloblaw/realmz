#include "remaster/assets/PartyPortraitCatalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "presentation/PresentationMode.hpp"
#include "remaster/assets/AssetResolver.hpp"
#include "remaster/assets/ResourceSelectionHook.hpp"

namespace realmz::remaster::assets {
namespace {

constexpr std::string_view kPortraitPack = "Data Files/Portraits";
constexpr std::string_view kPortraitType = "cicn";
constexpr AssetDimensions kPortraitDimensions{44U, 44U};
constexpr std::array<std::int16_t, 4> kPortraitIds{257, 267, 297, 337};

[[noreturn]] void fail(
    const ResourceKey& key, std::string_view reason) {
  throw PartyPortraitCatalogError(
      "Party portrait " + key.toString() + " " + std::string(reason));
}

} // namespace

PartyPortraitCatalog::PartyPortraitCatalog(Portraits portraits)
    : portraits_(std::move(portraits)) {}

PartyPortraitCatalog PartyPortraitCatalog::load(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& censusPath,
    const std::filesystem::path& assetRoot) {
  AssetResolver resolver(
      AssetManifest::load(manifestPath, censusPath, assetRoot));
  const auto& manifest = resolver.manifest();
  Portraits portraits;

  for (std::size_t index = 0; index < kPortraitIds.size(); ++index) {
    const ResourceKey key{
        .pack = std::string(kPortraitPack),
        .type = std::string(kPortraitType),
        .id = kPortraitIds[index],
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
        !resolution.masterKey || *resolution.masterKey != key) {
      fail(key, "does not preserve its exact resource and master key");
    }
    if (entry->status != AssetStatus::Approved) {
      fail(key, "is not approved");
    }
    if (entry->semanticFamily != "portrait") {
      fail(key, "does not belong to the portrait semantic family");
    }
    if (entry->alphaPolicy != "original_mask") {
      fail(key, "does not use the original_mask alpha policy");
    }
    if (entry->logicalDimensions != kPortraitDimensions) {
      fail(key, "does not have 44x44 logical dimensions");
    }
    if (!entry->assetPath || !resolution.overridePath ||
        *resolution.overridePath != manifest.assetRoot() / *entry->assetPath) {
      fail(key, "does not preserve its manifest override path");
    }
    if (!resolution.logicalDimensions ||
        *resolution.logicalDimensions != entry->logicalDimensions) {
      fail(key, "does not preserve its manifest logical dimensions");
    }
    if (!resolution.approvedContentSha256 ||
        *resolution.approvedContentSha256 != entry->sharedMasterSha256) {
      fail(key, "does not preserve its approved content digest");
    }

    for (std::size_t previous = 0; previous < index; ++previous) {
      if (portraits[previous].path == *resolution.overridePath) {
        fail(key, "reuses another party portrait path");
      }
      if (portraits[previous].classicPayloadSha256 ==
          entry->classicPayloadSha256) {
        fail(key, "reuses another Classic payload digest");
      }
      if (portraits[previous].approvedContentSha256 ==
          *resolution.approvedContentSha256) {
        fail(key, "reuses another approved output digest");
      }
    }

    portraits[index] = PartyPortraitDescriptor{
        .key = key,
        .path = *resolution.overridePath,
        .logicalDimensions = *resolution.logicalDimensions,
        .classicPayloadSha256 = entry->classicPayloadSha256,
        .approvedContentSha256 = *resolution.approvedContentSha256,
    };
  }

  return PartyPortraitCatalog(std::move(portraits));
}

const PartyPortraitDescriptor*
PartyPortraitCatalog::authorizeSelectedPortrait(
    const PostSelectionResult& selection) const noexcept {
  if (selection.resolution.kind != AssetResolutionKind::Override) {
    return nullptr;
  }

  for (const auto& portrait : this->portraits_) {
    if (selection.key != portrait.key) {
      continue;
    }
    if (selection.immutablePayloadSha256 !=
            portrait.classicPayloadSha256 ||
        !selection.resolution.overridePath ||
        *selection.resolution.overridePath != portrait.path ||
        !selection.resolution.masterKey ||
        *selection.resolution.masterKey != portrait.key ||
        !selection.resolution.logicalDimensions ||
        *selection.resolution.logicalDimensions !=
            portrait.logicalDimensions ||
        !selection.resolution.approvedContentSha256 ||
        *selection.resolution.approvedContentSha256 !=
            portrait.approvedContentSha256) {
      return nullptr;
    }
    return &portrait;
  }
  return nullptr;
}

const PartyPortraitCatalog::Portraits&
PartyPortraitCatalog::portraits() const noexcept {
  return this->portraits_;
}

} // namespace realmz::remaster::assets
