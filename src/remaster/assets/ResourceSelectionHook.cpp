#include "remaster/assets/ResourceSelectionHook.hpp"

#include <array>
#include <exception>
#include <utility>

#include "remaster/assets/PayloadDigest.hpp"

namespace realmz::remaster::assets {
namespace {

constexpr std::uint32_t resourceType(std::string_view value) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(value[0])) << 24U) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(value[1])) << 16U) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(value[2])) << 8U) |
      static_cast<std::uint32_t>(static_cast<unsigned char>(value[3]));
}

constexpr std::array<std::uint32_t, 4> kPhaseOneRasterTypes = {
    resourceType("PICT"), resourceType("cicn"),
    resourceType("crsr"), resourceType("ppat"),
};

} // namespace

std::optional<std::string> logicalPackForOpenRequest(
    std::string_view classicPath, bool applicationRelative) {
  if (!applicationRelative || classicPath.empty()) {
    return std::nullopt;
  }

  // A single leading colon is the normal Classic spelling for an
  // application-relative path. Additional empty/parent components are not a
  // stable identity and are rejected below.
  if (classicPath.front() == ':') {
    classicPath.remove_prefix(1);
  }
  if (classicPath.empty() || classicPath.front() == ':' ||
      classicPath.front() == '/' || classicPath.front() == '\\') {
    return std::nullopt;
  }

  std::string pack;
  pack.reserve(classicPath.size());
  for (const char ch : classicPath) {
    pack.push_back(ch == ':' ? '/' : ch);
  }
  if (!ResourceKey::isValidPack(pack)) {
    return std::nullopt;
  }
  return pack;
}

std::string resourceTypeString(std::uint32_t type) {
  std::string result(4, '\0');
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto shift = static_cast<unsigned int>((3U - index) * 8U);
    result[index] = static_cast<char>((type >> shift) & 0xFFU);
  }
  return result;
}

bool isPhaseOneRasterResourceType(std::uint32_t type) noexcept {
  for (const auto candidate : kPhaseOneRasterTypes) {
    if (candidate == type) {
      return true;
    }
  }
  return false;
}

struct ResourceSelectionHook::State {
  State(
      std::filesystem::path manifest,
      std::filesystem::path census,
      std::filesystem::path assets)
      : manifestPath(std::move(manifest)),
        censusPath(std::move(census)),
        assetRoot(std::move(assets)) {}

  std::filesystem::path manifestPath;
  std::filesystem::path censusPath;
  std::filesystem::path assetRoot;
  presentation::PresentationMode mode = presentation::PresentationMode::classic;
  std::optional<AssetResolver> resolver;
  std::optional<std::string> initializationFailure;
  bool initializationAttempted = false;

  void initialize() {
    if (initializationAttempted) {
      return;
    }
    initializationAttempted = true;
    try {
      resolver.emplace(AssetManifest::load(manifestPath, censusPath, assetRoot));
    } catch (const std::exception& error) {
      initializationFailure = error.what();
    }
  }
};

ResourceSelectionHook::ResourceSelectionHook(
    std::filesystem::path manifestPath,
    std::filesystem::path censusPath,
    std::filesystem::path assetRoot)
    : state_(std::make_unique<State>(
          std::move(manifestPath), std::move(censusPath),
          std::move(assetRoot))) {}

ResourceSelectionHook::~ResourceSelectionHook() = default;
ResourceSelectionHook::ResourceSelectionHook(ResourceSelectionHook&&) noexcept = default;
ResourceSelectionHook& ResourceSelectionHook::operator=(ResourceSelectionHook&&) noexcept = default;

void ResourceSelectionHook::setPresentationMode(
    presentation::PresentationMode mode) noexcept {
  state_->mode = mode;
}

presentation::PresentationMode ResourceSelectionHook::presentationMode() const noexcept {
  return state_->mode;
}

PostSelectionResult ResourceSelectionHook::inspect(
    std::string_view logicalPack, std::uint32_t type, std::int16_t id,
    std::string_view immutableSourcePayload) {
  ResourceKey key{
      .pack = std::string(logicalPack),
      .type = resourceTypeString(type),
      .id = id,
  };

  if (state_->mode == presentation::PresentationMode::classic) {
    return {
        .key = std::move(key),
        .immutablePayloadSha256 = {},
        .resolution = {
            .kind = AssetResolutionKind::ClassicBypass,
            .overridePath = std::nullopt,
            .masterKey = std::nullopt,
            .diagnostic = "Classic presentation bypasses remastered overrides",
        },
    };
  }

  const auto digest = payloadSha256Hex(immutableSourcePayload);
  state_->initialize();
  if (!state_->resolver.has_value()) {
    return {
        .key = std::move(key),
        .immutablePayloadSha256 = digest,
        .resolution = {
            .kind = AssetResolutionKind::CoverageFailure,
            .overridePath = std::nullopt,
            .masterKey = std::nullopt,
            .diagnostic = "Remastered asset manifest unavailable: " +
                state_->initializationFailure.value_or("unknown initialization failure"),
        },
    };
  }

  return {
      .key = key,
      .immutablePayloadSha256 = digest,
      .resolution = state_->resolver->resolve(state_->mode, key, digest),
  };
}

} // namespace realmz::remaster::assets
