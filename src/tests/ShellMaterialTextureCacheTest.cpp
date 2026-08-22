#include "remaster/assets/ShellMaterialTextureCache.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

using realmz::remaster::assets::ShellMaterialCatalog;
using realmz::remaster::assets::ShellMaterialRole;
using realmz::remaster::assets::ShellMaterialTextureCache;

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::int16_t, 4> kMaterialIds{128, 129, 130, 131};
constexpr std::array<ShellMaterialRole, 4> kMaterialRoles{
    ShellMaterialRole::highlight,
    ShellMaterialRole::neutral,
    ShellMaterialRole::shadow,
    ShellMaterialRole::panel,
};

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string loadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return {std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()};
}

void saveFile(const fs::path& path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot create " + path.string());
  }
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output) {
    throw std::runtime_error("cannot write " + path.string());
  }
}

void replaceFirst(
    std::string& text, std::string_view from, std::string_view to) {
  const auto position = text.find(from);
  if (position == std::string::npos) {
    throw std::runtime_error(
        "test fixture token not found: " + std::string(from));
  }
  text.replace(position, from.size(), to);
}

struct TextSpan {
  std::size_t begin;
  std::size_t end;
};

std::vector<TextSpan> manifestEntrySpans(std::string_view manifest) {
  const auto entries = manifest.find("\"entries\"");
  const auto arrayStart = manifest.find('[', entries);
  if (entries == std::string_view::npos ||
      arrayStart == std::string_view::npos) {
    throw std::runtime_error("cannot locate manifest entries");
  }

  std::vector<TextSpan> spans;
  std::size_t objectStart = std::string_view::npos;
  std::size_t depth = 0;
  bool inString = false;
  bool escaped = false;
  for (std::size_t index = arrayStart + 1; index < manifest.size(); ++index) {
    const char value = manifest[index];
    if (inString) {
      if (!escaped && value == '"') {
        inString = false;
      }
      if (!escaped && value == '\\') {
        escaped = true;
      } else {
        escaped = false;
      }
      continue;
    }
    if (value == '"') {
      inString = true;
    } else if (value == '{') {
      if (depth++ == 0) {
        objectStart = index;
      }
    } else if (value == '}') {
      if (depth == 0) {
        throw std::runtime_error("unbalanced manifest entry object");
      }
      if (--depth == 0) {
        spans.emplace_back(TextSpan{objectStart, index + 1});
        objectStart = std::string_view::npos;
      }
    } else if (value == ']' && depth == 0) {
      return spans;
    }
  }
  throw std::runtime_error("unterminated manifest entries array");
}

bool entryMatchesMaterial(std::string_view entry, std::int16_t resourceId) {
  const auto keyStart = entry.find("\"key\"");
  const auto keyEnd = entry.find('}', keyStart);
  if (keyStart == std::string_view::npos ||
      keyEnd == std::string_view::npos) {
    return false;
  }
  const auto key = entry.substr(keyStart, keyEnd + 1 - keyStart);
  return key.find("\"id\": " + std::to_string(resourceId)) !=
          std::string_view::npos &&
      key.find("\"pack\": \"Data Files/The Family Jewels\"") !=
          std::string_view::npos &&
      key.find("\"type\": \"ppat\"") != std::string_view::npos;
}

TextSpan materialEntrySpan(
    std::string_view manifest, std::int16_t resourceId) {
  for (const auto span : manifestEntrySpans(manifest)) {
    const auto entry = manifest.substr(span.begin, span.end - span.begin);
    if (entryMatchesMaterial(entry, resourceId)) {
      return span;
    }
  }
  throw std::runtime_error(
      "material manifest entry not found: ppat " +
      std::to_string(resourceId));
}

std::string materialEntry(
    std::string_view manifest, std::int16_t resourceId) {
  const auto span = materialEntrySpan(manifest, resourceId);
  return std::string(manifest.substr(span.begin, span.end - span.begin));
}

void replaceMaterialEntry(
    std::string& manifest,
    std::int16_t resourceId,
    std::string_view replacement) {
  const auto span = materialEntrySpan(manifest, resourceId);
  manifest.replace(span.begin, span.end - span.begin, replacement);
}

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = fs::temp_directory_path() /
        ("realmz-shell-material-texture-cache-test-" + unique);
    if (!fs::create_directories(path_)) {
      throw std::runtime_error(
          "cannot create temporary directory " + path_.string());
    }
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
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
        "could not select SDL's headless video driver");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      throw std::runtime_error(
          "could not initialize headless SDL video: " +
          std::string(SDL_GetError()));
    }
  }

  ~SdlVideoSession() {
    SDL_Quit();
  }

  SdlVideoSession(const SdlVideoSession&) = delete;
  SdlVideoSession& operator=(const SdlVideoSession&) = delete;
};

template <typename Exception, typename Callback>
void requireThrows(Callback&& callback, std::string_view message) {
  try {
    callback();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error(std::string(message));
}

fs::path materialRelativePath(std::size_t index) {
  return fs::path("style-proof/generation/outputs") /
      std::format("{:02}_ui_material_ppat_{}.png", index + 1,
          kMaterialIds[index]);
}

ShellMaterialCatalog createIsolatedPublicCatalog(
    const fs::path& publicAssetRoot,
    const fs::path& fixtureRoot) {
  const auto scopes = publicAssetRoot / "scopes";
  const auto censusPath = scopes / "phase1.census.json";
  const auto runtimePath = scopes / "phase1.runtime-manifest.json";
  const auto placeholderPath =
      scopes / "phase1.placeholder-manifest.json";

  auto fixtureManifest = loadFile(placeholderPath);
  const auto runtimeManifest = loadFile(runtimePath);
  replaceFirst(fixtureManifest,
      "\"manifest_kind\": \"zero-cost-placeholder\"",
      "\"manifest_kind\": \"production\"");
  for (const auto resourceId : kMaterialIds) {
    replaceMaterialEntry(
        fixtureManifest, resourceId,
        materialEntry(runtimeManifest, resourceId));
  }

  const auto fixtureManifestPath =
      fixtureRoot / "phase1.runtime-manifest.json";
  const auto fixtureCensusPath = fixtureRoot / "phase1.census.json";
  saveFile(fixtureManifestPath, fixtureManifest);
  saveFile(fixtureCensusPath, loadFile(censusPath));

  for (std::size_t index = 0; index < kMaterialIds.size(); ++index) {
    const auto relativePath = materialRelativePath(index);
    fs::create_directories((fixtureRoot / relativePath).parent_path());
    fs::copy_file(publicAssetRoot / relativePath, fixtureRoot / relativePath);
  }

  std::size_t pngCount = 0;
  for (const auto& item : fs::recursive_directory_iterator(fixtureRoot)) {
    if (item.is_regular_file() && item.path().extension() == ".png") {
      ++pngCount;
    }
  }
  require(pngCount == kMaterialIds.size(),
      "isolated fixture must contain exactly four public PNGs");

  return ShellMaterialCatalog::load(
      fixtureManifestPath, fixtureCensusPath, fixtureRoot);
}

sdl_surface_ptr createRenderSurface() {
  auto surface = sdl_make_unique(
      SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_RGBA32));
  if (!surface) {
    throw std::runtime_error(
        "could not create software render surface: " +
        std::string(SDL_GetError()));
  }
  return surface;
}

sdl_renderer_ptr createSoftwareRenderer(SDL_Surface* surface) {
  auto renderer = sdl_make_unique(SDL_CreateSoftwareRenderer(surface));
  if (!renderer) {
    throw std::runtime_error(
        "could not create SDL software renderer: " +
        std::string(SDL_GetError()));
  }
  return renderer;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error(
          "usage: ShellMaterialTextureCacheTest <repository-root>");
    }
    const auto repositoryRoot = fs::weakly_canonical(fs::path(argv[1]));
    require(fs::is_directory(repositoryRoot),
        "repository root argument is not a directory");
    const auto publicAssetRoot = repositoryRoot / "assets/remastered";
    const auto productionCatalog = ShellMaterialCatalog::load(
        publicAssetRoot / "scopes/phase1.runtime-manifest.json",
        publicAssetRoot / "scopes/phase1.census.json", publicAssetRoot);

    requireThrows<std::invalid_argument>(
        [&] {
          (void)ShellMaterialTextureCache(nullptr, productionCatalog);
        },
        "null-renderer shell material cache construction did not throw");

    SdlVideoSession sdl;
    TemporaryDirectory temporary;
    const auto unicodeFixtureRoot = temporary.path() /
        fs::path(std::u8string(u8"public-\u00e9-\u754c"));
    const auto isolatedCatalog =
        createIsolatedPublicCatalog(publicAssetRoot, unicodeFixtureRoot);

    {
      auto primarySurface = createRenderSurface();
      auto primaryRenderer = createSoftwareRenderer(primarySurface.get());
      auto otherSurface = createRenderSurface();
      auto otherRenderer = createSoftwareRenderer(otherSurface.get());

      {
        ShellMaterialTextureCache cache(
            primaryRenderer.get(), productionCatalog);
        for (std::size_t index = 0; index < kMaterialRoles.size(); ++index) {
          const SDL_FRect destination{
              static_cast<float>((index % 2) * 112),
              static_cast<float>((index / 2) * 112),
              96.0f,
              96.0f,
          };
          require(cache.draw(
                      primaryRenderer.get(), kMaterialRoles[index], destination),
              "production shell material draw failed");
        }

        const SDL_FRect destination{0.0f, 0.0f, 64.0f, 64.0f};
        require(!cache.draw(nullptr, ShellMaterialRole::panel, destination),
            "shell material draw accepted a null renderer");
        require(!cache.draw(
                    otherRenderer.get(), ShellMaterialRole::panel, destination),
            "shell material draw accepted a mismatched renderer");
        require(!cache.draw(primaryRenderer.get(),
                    static_cast<ShellMaterialRole>(-1), destination),
            "shell material draw accepted an invalid role");
        require(!cache.draw(primaryRenderer.get(),
                    static_cast<ShellMaterialRole>(4), destination),
            "shell material draw accepted an out-of-range role");
      }

      {
        ShellMaterialTextureCache isolatedCache(
            primaryRenderer.get(), isolatedCatalog);
        const SDL_FRect destination{0.0f, 0.0f, 96.0f, 96.0f};
        for (const auto role : kMaterialRoles) {
          require(isolatedCache.draw(
                      primaryRenderer.get(), role, destination),
              "isolated Unicode-path shell material draw failed");
        }
      }

      const auto changedMaterial =
          unicodeFixtureRoot / materialRelativePath(2);
      fs::copy_file(
          unicodeFixtureRoot / materialRelativePath(1), changedMaterial,
          fs::copy_options::overwrite_existing);
      requireThrows<std::runtime_error>(
          [&] {
            (void)ShellMaterialTextureCache(
                primaryRenderer.get(), isolatedCatalog);
          },
          "cache construction accepted a valid substituted PNG");

      require(fs::remove(changedMaterial),
          "could not remove substituted shell material fixture");
      requireThrows<std::runtime_error>(
          [&] {
            (void)ShellMaterialTextureCache(
                primaryRenderer.get(), isolatedCatalog);
          },
          "cache construction accepted a material removed after validation");
    }

    std::cout <<
        "ShellMaterialTextureCacheTest passed: 4 roles, headless software renderer\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ShellMaterialTextureCacheTest failed: " << error.what()
              << '\n';
    return 1;
  }
}
