#include "remaster/assets/PartyPortraitTextureCache.hpp"

#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>

#include "remaster/assets/AssetManifest.hpp"
#include "remaster/assets/ResourceSelectionHook.hpp"
#include "remaster/assets/VerifiedRasterSurface.hpp"

namespace {

namespace fs = std::filesystem;

using realmz::remaster::assets::AssetResolutionKind;
using realmz::remaster::assets::PartyPortraitCatalog;
using realmz::remaster::assets::PartyPortraitDescriptor;
using realmz::remaster::assets::PartyPortraitTextureCache;
using realmz::remaster::assets::PostSelectionResult;
using realmz::remaster::assets::ResourceKey;
using realmz::remaster::assets::VerifiedRasterSurfaceError;
using realmz::remaster::assets::VerifiedRasterSurfacePhase;
using realmz::remaster::assets::assetContentSha256Hex;

template <typename Cache>
concept HasIdOnlyDraw = requires(
    const Cache& cache,
    SDL_Renderer* renderer,
    std::int16_t resourceId,
    const SDL_FRect& destination) {
  cache.draw(renderer, resourceId, destination);
};

static_assert(!HasIdOnlyDraw<PartyPortraitTextureCache>);

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string loadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open public test fixture");
  }
  return {
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>(),
  };
}

void saveFile(const fs::path& path, std::string_view bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot create public test fixture");
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("cannot write public test fixture");
  }
}

void replaceExactCount(
    std::string& text,
    std::string_view from,
    std::string_view to,
    std::size_t expectedCount) {
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = text.find(from, position)) != std::string::npos) {
    text.replace(position, from.size(), to);
    position += to.size();
    ++count;
  }
  if (count != expectedCount) {
    throw std::runtime_error(
        "public test fixture replacement count changed");
  }
}

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = fs::temp_directory_path() /
        ("realmz-party-portrait-texture-test-" + unique) /
        fs::path(std::u8string(u8"public-\u00e9-\u754c"));
    fs::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path_.parent_path(), ignored);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  [[nodiscard]] const fs::path& path() const noexcept {
    return path_;
  }

private:
  fs::path path_;
};

class SdlVideoSession {
public:
  SdlVideoSession() {
    require(SDL_SetHintWithPriority(
                SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE),
        "could not select SDL dummy video driver");
    require(SDL_Init(SDL_INIT_VIDEO),
        "could not initialize SDL dummy video driver");
  }

  ~SdlVideoSession() {
    SDL_Quit();
  }

  SdlVideoSession(const SdlVideoSession&) = delete;
  SdlVideoSession& operator=(const SdlVideoSession&) = delete;
};

sdl_surface_ptr createRenderSurface() {
  sdl_surface_ptr surface(
      SDL_CreateSurface(160, 160, SDL_PIXELFORMAT_RGBA32));
  require(surface != nullptr,
      "could not create software-render target surface");
  return surface;
}

sdl_renderer_ptr createSoftwareRenderer(SDL_Surface* surface) {
  sdl_renderer_ptr renderer(SDL_CreateSoftwareRenderer(surface));
  require(renderer != nullptr, "could not create software renderer");
  return renderer;
}

void copyApprovedOutputs(
    const fs::path& assetRoot, const fs::path& targetRoot) {
  const auto source = assetRoot / "style-proof/generation/outputs";
  const auto destination =
      targetRoot / "style-proof/generation/outputs";
  fs::create_directories(destination);
  std::size_t copied = 0;
  for (const auto& item : fs::directory_iterator(source)) {
    if (item.is_regular_file() && item.path().extension() == ".png") {
      fs::copy_file(item.path(), destination / item.path().filename(),
          fs::copy_options::overwrite_existing);
      ++copied;
    }
  }
  require(copied == 11U,
      "isolated public fixture did not copy exactly 11 approved PNGs");
}

PostSelectionResult exactProof(
    const PartyPortraitDescriptor& portrait) {
  return {
      .key = portrait.key,
      .immutablePayloadSha256 = portrait.classicPayloadSha256,
      .resolution = {
          .kind = AssetResolutionKind::Override,
          .overridePath = portrait.path,
          .masterKey = portrait.key,
          .logicalDimensions = portrait.logicalDimensions,
          .approvedContentSha256 = portrait.approvedContentSha256,
          .diagnostic = "exact public portrait test proof",
      },
  };
}

template <typename Exception, typename Callback>
void requireThrows(Callback&& callback, std::string_view message) {
  try {
    callback();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error(std::string(message));
}

template <typename Callback>
void requireConstructionFailure(
    Callback&& callback,
    VerifiedRasterSurfacePhase expectedPhase,
    std::string_view message) {
  try {
    callback();
  } catch (const VerifiedRasterSurfaceError& error) {
    require(error.phase() == expectedPhase,
        "portrait-cache construction failed in the wrong raster phase");
    return;
  }
  throw std::runtime_error(std::string(message));
}

std::string pngIhdrOnly(std::uint32_t width, std::uint32_t height) {
  std::string bytes(33U, '\0');
  constexpr unsigned char signature[] = {
      0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
  for (std::size_t index = 0; index < sizeof(signature); ++index) {
    bytes[index] = static_cast<char>(signature[index]);
  }
  const auto writeBigEndian32 = [&bytes](
      std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<char>((value >> 24U) & 0xFFU);
    bytes[offset + 1U] = static_cast<char>((value >> 16U) & 0xFFU);
    bytes[offset + 2U] = static_cast<char>((value >> 8U) & 0xFFU);
    bytes[offset + 3U] = static_cast<char>(value & 0xFFU);
  };
  writeBigEndian32(8U, 13U);
  bytes.replace(12U, 4U, "IHDR");
  writeBigEndian32(16U, width);
  writeBigEndian32(20U, height);
  bytes[24] = 8;
  bytes[25] = 6;
  return bytes;
}

void requireRejectedProof(
    const PartyPortraitTextureCache& cache,
    SDL_Renderer* renderer,
    const PostSelectionResult& selection,
    std::string_view message) {
  const SDL_FRect destination{4.0F, 4.0F, 44.0F, 44.0F};
  require(!cache.draw(renderer, selection, destination), message);
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error(
          "usage: PartyPortraitTextureCacheTest <repository-root>");
    }
    const auto repositoryRoot = fs::weakly_canonical(fs::path(argv[1]));
    const auto productionRoot = repositoryRoot / "assets/remastered";
    const auto productionManifest =
        productionRoot / "scopes/phase1.runtime-manifest.json";
    const auto productionCensus =
        productionRoot / "scopes/phase1.census.json";

    TemporaryDirectory temporary;
    const auto fixtureRoot = temporary.path();
    const auto fixtureManifest =
        fixtureRoot / "phase1.runtime-manifest.json";
    const auto fixtureCensus = fixtureRoot / "phase1.census.json";
    copyApprovedOutputs(productionRoot, fixtureRoot);
    const auto manifestBytes = loadFile(productionManifest);
    saveFile(fixtureManifest, manifestBytes);
    saveFile(fixtureCensus, loadFile(productionCensus));
    require(!fs::exists(fixtureRoot / "base"),
        "isolated public fixture unexpectedly contains Classic resources");
    require(fixtureRoot.generic_u8string().find(u8"\u00e9-\u754c") !=
            std::u8string::npos,
        "isolated fixture root lost its Unicode components");

    const auto isolatedCatalog = PartyPortraitCatalog::load(
        fixtureManifest, fixtureCensus, fixtureRoot);
    const auto& lastPortrait = isolatedCatalog.portraits().back();
    const auto originalLastPortrait = productionRoot /
        "style-proof/generation/outputs/08_portrait_cicn_337.png";
    const auto substitutePortrait = productionRoot /
        "style-proof/generation/outputs/05_portrait_cicn_257.png";

    SdlVideoSession sdl;
    auto primarySurface = createRenderSurface();
    auto primaryRenderer = createSoftwareRenderer(primarySurface.get());
    auto otherSurface = createRenderSurface();
    auto otherRenderer = createSoftwareRenderer(otherSurface.get());

    requireThrows<std::invalid_argument>(
        [&] {
          (void)PartyPortraitTextureCache(nullptr, isolatedCatalog);
        },
        "null-renderer portrait cache construction did not throw");

    // Mutate the final record so three prior textures must be cleaned up when
    // each all-or-nothing construction attempt fails.
    fs::copy_file(substitutePortrait, lastPortrait.path,
        fs::copy_options::overwrite_existing);
    requireConstructionFailure(
        [&] {
          (void)PartyPortraitTextureCache(
              primaryRenderer.get(), isolatedCatalog);
        },
        VerifiedRasterSurfacePhase::digest,
        "valid substituted portrait PNG was accepted");
    fs::copy_file(originalLastPortrait, lastPortrait.path,
        fs::copy_options::overwrite_existing);

    require(fs::remove(lastPortrait.path),
        "could not remove final portrait fixture");
    requireConstructionFailure(
        [&] {
          (void)PartyPortraitTextureCache(
              primaryRenderer.get(), isolatedCatalog);
        },
        VerifiedRasterSurfacePhase::open,
        "deleted portrait PNG was accepted");
    fs::copy_file(originalLastPortrait, lastPortrait.path,
        fs::copy_options::overwrite_existing);

    const auto malformed = pngIhdrOnly(1U, 1U);
    saveFile(lastPortrait.path, malformed);
    auto decodeManifestBytes = manifestBytes;
    replaceExactCount(
        decodeManifestBytes,
        lastPortrait.approvedContentSha256,
        assetContentSha256Hex(malformed),
        1U);
    const auto decodeManifest =
        fixtureRoot / "decode-failure.runtime-manifest.json";
    saveFile(decodeManifest, decodeManifestBytes);
    const auto decodeFailureCatalog = PartyPortraitCatalog::load(
        decodeManifest, fixtureCensus, fixtureRoot);
    requireConstructionFailure(
        [&] {
          (void)PartyPortraitTextureCache(
              primaryRenderer.get(), decodeFailureCatalog);
        },
        VerifiedRasterSurfacePhase::decode,
        "matching-digest undecodable portrait PNG was accepted");
    fs::copy_file(originalLastPortrait, lastPortrait.path,
        fs::copy_options::overwrite_existing);

    {
      // Cache scope is deliberately nested inside both renderer scopes.
      PartyPortraitTextureCache cache(
          primaryRenderer.get(), isolatedCatalog);
      const auto& portraits = isolatedCatalog.portraits();
      for (std::size_t index = 0; index < portraits.size(); ++index) {
        const SDL_FRect destination{
            static_cast<float>((index % 2U) * 64U + 8U),
            static_cast<float>((index / 2U) * 64U + 8U),
            44.0F,
            44.0F,
        };
        require(cache.draw(primaryRenderer.get(),
                    exactProof(portraits[index]), destination),
            "exact party portrait proof did not draw");
      }

      const auto exact = exactProof(portraits.front());
      const SDL_FRect validDestination{4.0F, 4.0F, 44.0F, 44.0F};
      require(!cache.draw(nullptr, exact, validDestination),
          "portrait cache accepted a null renderer");
      require(!cache.draw(otherRenderer.get(), exact, validDestination),
          "portrait cache accepted a mismatched renderer");

      for (const auto& malformedDestination : {
               SDL_FRect{0.0F, 0.0F, 0.0F, 44.0F},
               SDL_FRect{0.0F, 0.0F, 44.0F, -1.0F},
               SDL_FRect{std::numeric_limits<float>::quiet_NaN(),
                   0.0F, 44.0F, 44.0F},
               SDL_FRect{0.0F, 0.0F,
                   std::numeric_limits<float>::infinity(), 44.0F},
           }) {
        require(!cache.draw(primaryRenderer.get(), exact,
                    malformedDestination),
            "portrait cache accepted malformed destination bounds");
      }

      auto forged = exact;
      forged.immutablePayloadSha256 =
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
      requireRejectedProof(cache, primaryRenderer.get(), forged,
          "portrait cache accepted a forged payload proof");

      auto custom = exact;
      custom.key.id = 999;
      requireRejectedProof(cache, primaryRenderer.get(), custom,
          "portrait cache accepted a custom portrait ID");

      auto otherPack = exact;
      otherPack.key.pack = "Scenarios/Tutorial/Scenario";
      requireRejectedProof(cache, primaryRenderer.get(), otherPack,
          "portrait cache accepted another pack's portrait proof");

      auto missing = exact;
      missing.resolution.approvedContentSha256.reset();
      requireRejectedProof(cache, primaryRenderer.get(), missing,
          "portrait cache accepted a proof missing its approved digest");

      requireRejectedProof(cache, primaryRenderer.get(), PostSelectionResult{},
          "portrait cache accepted an empty selection proof");
      SDL_RenderPresent(primaryRenderer.get());
    }

    // Explicit reset after cache destruction documents and exercises the
    // required texture-before-renderer lifetime ordering.
    primaryRenderer.reset();
    otherRenderer.reset();
    primarySurface.reset();
    otherSurface.reset();

    std::cout <<
        "PartyPortraitTextureCacheTest passed: 4 proof-gated textures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "PartyPortraitTextureCacheTest failed: "
              << error.what() << '\n';
    return 1;
  }
}
