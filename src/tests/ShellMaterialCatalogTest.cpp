#include "remaster/assets/ShellMaterialCatalog.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

using realmz::remaster::assets::AssetDimensions;
using realmz::remaster::assets::AssetManifestError;
using realmz::remaster::assets::ResourceKey;
using realmz::remaster::assets::ShellMaterialCatalog;
using realmz::remaster::assets::ShellMaterialCatalogError;
using realmz::remaster::assets::ShellMaterialRole;
using realmz::remaster::assets::ShellSurfaceState;
using realmz::remaster::assets::shellMaterialRoleForSurfaceState;

namespace {

namespace fs = std::filesystem;

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

void replaceInManifestEntry(
    std::string& manifest,
    std::string_view assetPath,
    std::string_view from,
    std::string_view to) {
  const auto anchor = manifest.find(assetPath);
  if (anchor == std::string::npos) {
    throw std::runtime_error(
        "test manifest entry not found: " + std::string(assetPath));
  }
  const auto entryStart = manifest.rfind("\n    {", anchor);
  const auto entryEnd = manifest.find("\n    }", anchor);
  if (entryStart == std::string::npos || entryEnd == std::string::npos) {
    throw std::runtime_error(
        "cannot isolate test manifest entry: " + std::string(assetPath));
  }
  const auto position = manifest.find(from, entryStart);
  if (position == std::string::npos || position >= entryEnd) {
    throw std::runtime_error(
        "test manifest entry token not found: " + std::string(from));
  }
  manifest.replace(position, from.size(), to);
}

template <typename Callback>
void requireCatalogFailure(Callback&& callback, std::string_view message) {
  try {
    callback();
  } catch (const ShellMaterialCatalogError&) {
    return;
  } catch (const AssetManifestError&) {
    return;
  }
  throw std::runtime_error(std::string(message));
}

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = fs::temp_directory_path() /
        ("realmz-shell-material-catalog-test-" + unique);
    fs::create_directories(path_);
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

void copyApprovedOutputs(const fs::path& assetRoot, const fs::path& targetRoot) {
  const auto source = assetRoot / "style-proof/generation/outputs";
  const auto destination = targetRoot / "style-proof/generation/outputs";
  fs::create_directories(destination);
  for (const auto& item : fs::directory_iterator(source)) {
    if (item.is_regular_file() && item.path().extension() == ".png") {
      fs::copy_file(
          item.path(), destination / item.path().filename(),
          fs::copy_options::overwrite_existing);
    }
  }
}

struct ExpectedMaterial {
  ShellMaterialRole role;
  std::int16_t resourceId;
  std::string_view relativePath;
  std::string_view sharedMasterSha256;
};

constexpr std::array<ExpectedMaterial, 4> kExpectedMaterials{{
    {
        ShellMaterialRole::highlight,
        128,
        "style-proof/generation/outputs/01_ui_material_ppat_128.png",
        "3b3a30342aef0e49b0b43a04bec3592abda5d629963bc8a68c8cb296eef1961e",
    },
    {
        ShellMaterialRole::neutral,
        129,
        "style-proof/generation/outputs/02_ui_material_ppat_129.png",
        "dfe2dadfa74ef37ee4d6de5f8e607eb082285f6e1a994a5e0e26ba2463d7f52f",
    },
    {
        ShellMaterialRole::shadow,
        130,
        "style-proof/generation/outputs/03_ui_material_ppat_130.png",
        "df44c09cef3bd20e1c8de43e39c6c88573942a9f17ab5f4f3968f4c04ab72504",
    },
    {
        ShellMaterialRole::panel,
        131,
        "style-proof/generation/outputs/04_ui_material_ppat_131.png",
        "7081f60b8a7ea247fb5d181e6d8b2dfcaaf7a12046a58c31fef318c21f5fc3f9",
    },
}};

static_assert(
    shellMaterialRoleForSurfaceState(ShellSurfaceState::panel) ==
    std::optional{ShellMaterialRole::panel});
static_assert(
    shellMaterialRoleForSurfaceState(ShellSurfaceState::normal) ==
    std::optional{ShellMaterialRole::neutral});
static_assert(
    shellMaterialRoleForSurfaceState(ShellSurfaceState::selected) ==
    std::optional{ShellMaterialRole::highlight});
static_assert(
    shellMaterialRoleForSurfaceState(ShellSurfaceState::pressed) ==
    std::optional{ShellMaterialRole::shadow});
static_assert(
    shellMaterialRoleForSurfaceState(ShellSurfaceState::inactive) ==
    std::optional{ShellMaterialRole::shadow});
static_assert(!shellMaterialRoleForSurfaceState(
    static_cast<ShellSurfaceState>(-1)));

} // namespace

int main(int argc, char** argv) {
  try {
    const auto repositoryRoot = fs::weakly_canonical(
        argc > 1 ? fs::path(argv[1]) : fs::path("."));
    const auto assetRoot = repositoryRoot / "assets/remastered";
    const auto manifestPath =
        assetRoot / "scopes/phase1.runtime-manifest.json";
    const auto censusPath = assetRoot / "scopes/phase1.census.json";

    const auto catalog =
        ShellMaterialCatalog::load(manifestPath, censusPath, assetRoot);
    const auto& materials = catalog.materials();
    require(materials.size() == kExpectedMaterials.size(),
        "shell material catalog size changed");

    std::set<std::string> paths;
    for (std::size_t index = 0; index < kExpectedMaterials.size(); ++index) {
      const auto& expected = kExpectedMaterials[index];
      const auto& material = materials[index];
      const auto expectedPath =
          fs::weakly_canonical(assetRoot / expected.relativePath);
      require(material.role == expected.role,
          "shell material role order changed");
      require(material.key == ResourceKey{
          "Data Files/The Family Jewels", "ppat", expected.resourceId},
          "shell material ResourceKey changed");
      require(material.path == expectedPath,
          "shell material approved output path changed");
      require(material.logicalDimensions ==
              AssetDimensions{.width = 64, .height = 64},
          "shell material logical dimensions changed");
      require(material.sharedMasterSha256 == expected.sharedMasterSha256,
          "shell material shared master SHA-256 changed");
      require(fs::is_regular_file(material.path),
          "shell material approved output is not a regular file");
      require(paths.emplace(material.path.generic_string()).second,
          "shell material approved output paths are not unique");
      const auto* found = catalog.find(expected.role);
      require(found != nullptr && *found == material,
          "role lookup did not return its exact material descriptor");
    }

    // The production manifest contains additional ui_surface ppat keys. The
    // catalog is intentionally the exact 128-131 key set, not a family scan.
    require(catalog.find(static_cast<ShellMaterialRole>(-1)) == nullptr,
        "invalid shell material role was accepted");
    require(catalog.find(static_cast<ShellMaterialRole>(4)) == nullptr,
        "out-of-range shell material role was accepted");
    require(!shellMaterialRoleForSurfaceState(
        static_cast<ShellSurfaceState>(5)),
        "out-of-range shell surface state was accepted");

    TemporaryDirectory temporary;
    copyApprovedOutputs(assetRoot, temporary.path());
    const auto runtimeManifest = loadFile(manifestPath);
    const auto isolatedManifest =
        temporary.path() / "phase1.runtime-manifest.json";
    const auto isolatedCensus = temporary.path() / "phase1.census.json";
    saveFile(isolatedManifest, runtimeManifest);
    saveFile(isolatedCensus, loadFile(censusPath));
    require(!fs::exists(temporary.path() / "base"),
        "isolated key-only fixture unexpectedly contains Classic payloads");
    const auto isolatedCatalog = ShellMaterialCatalog::load(
        isolatedManifest, isolatedCensus, temporary.path());
    for (std::size_t index = 0; index < kExpectedMaterials.size(); ++index) {
      const auto& expected = kExpectedMaterials[index];
      const auto& material = isolatedCatalog.materials()[index];
      require(material.role == expected.role &&
              material.key == ResourceKey{
                  "Data Files/The Family Jewels", "ppat",
                  expected.resourceId} &&
              material.path == fs::weakly_canonical(
                  temporary.path() / expected.relativePath) &&
              material.sharedMasterSha256 == expected.sharedMasterSha256,
          "isolated public-only catalog changed an exact material binding");
    }
    const auto material128 = temporary.path() /
        "style-proof/generation/outputs/01_ui_material_ppat_128.png";
    const auto productionMaterial128 = assetRoot /
        "style-proof/generation/outputs/01_ui_material_ppat_128.png";

    fs::remove(material128);
    requireCatalogFailure(
        [&] {
          (void)ShellMaterialCatalog::load(
              manifestPath, censusPath, temporary.path());
        },
        "missing approved shell material output was accepted");
    fs::copy_file(productionMaterial128, material128);

    saveFile(material128, "tampered");
    requireCatalogFailure(
        [&] {
          (void)ShellMaterialCatalog::load(
              manifestPath, censusPath, temporary.path());
        },
        "tampered approved shell material output was accepted");
    fs::copy_file(productionMaterial128, material128,
        fs::copy_options::overwrite_existing);

    auto wrongFamily = runtimeManifest;
    replaceInManifestEntry(
        wrongFamily, kExpectedMaterials[0].relativePath,
        "\"semantic_family\": \"ui_surface\"",
        "\"semantic_family\": \"portrait\"");
    const auto wrongFamilyPath = temporary.path() / "wrong-family.json";
    saveFile(wrongFamilyPath, wrongFamily);
    requireCatalogFailure(
        [&] {
          (void)ShellMaterialCatalog::load(
              wrongFamilyPath, censusPath, temporary.path());
        },
        "wrong shell material semantic family was accepted");

    auto wrongAlpha = runtimeManifest;
    replaceInManifestEntry(
        wrongAlpha, kExpectedMaterials[0].relativePath,
        "\"alpha_policy\": \"opaque_tile\"",
        "\"alpha_policy\": \"original_mask\"");
    const auto wrongAlphaPath = temporary.path() / "wrong-alpha.json";
    saveFile(wrongAlphaPath, wrongAlpha);
    requireCatalogFailure(
        [&] {
          (void)ShellMaterialCatalog::load(
              wrongAlphaPath, censusPath, temporary.path());
        },
        "wrong shell material alpha policy was accepted");

    auto wrongDimensions = runtimeManifest;
    replaceInManifestEntry(
        wrongDimensions, kExpectedMaterials[0].relativePath,
        "\"height\": 64", "\"height\": 63");
    const auto wrongDimensionsPath =
        temporary.path() / "wrong-dimensions.json";
    saveFile(wrongDimensionsPath, wrongDimensions);
    requireCatalogFailure(
        [&] {
          (void)ShellMaterialCatalog::load(
              wrongDimensionsPath, censusPath, temporary.path());
        },
        "wrong shell material dimensions were accepted");

    auto wrongMaster = runtimeManifest;
    replaceInManifestEntry(
        wrongMaster, kExpectedMaterials[0].relativePath,
        "\"master_key\": {\n        \"id\": 128",
        "\"master_key\": {\n        \"id\": 129");
    const auto wrongMasterPath = temporary.path() / "wrong-master.json";
    saveFile(wrongMasterPath, wrongMaster);
    requireCatalogFailure(
        [&] {
          (void)ShellMaterialCatalog::load(
              wrongMasterPath, censusPath, temporary.path());
        },
        "wrong shell material master key was accepted");

    auto duplicatePath = runtimeManifest;
    replaceFirst(duplicatePath,
        "style-proof/generation/outputs/02_ui_material_ppat_129.png",
        "style-proof/generation/outputs/01_ui_material_ppat_128.png");
    replaceFirst(duplicatePath,
        "dfe2dadfa74ef37ee4d6de5f8e607eb082285f6e1a994a5e0e26ba2463d7f52f",
        "3b3a30342aef0e49b0b43a04bec3592abda5d629963bc8a68c8cb296eef1961e");
    const auto duplicatePathManifest =
        temporary.path() / "duplicate-path.json";
    saveFile(duplicatePathManifest, duplicatePath);
    requireCatalogFailure(
        [&] {
          (void)ShellMaterialCatalog::load(
              duplicatePathManifest, censusPath, temporary.path());
        },
        "duplicate shell material output path was accepted");

    requireCatalogFailure(
        [&] {
          (void)ShellMaterialCatalog::load(
              temporary.path() / "missing-manifest.json",
              censusPath, temporary.path());
        },
        "missing runtime manifest was accepted");

    std::cout << "ShellMaterialCatalogTest passed: 4 exact production materials\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ShellMaterialCatalogTest failed: " << error.what() << '\n';
    return 1;
  }
}
