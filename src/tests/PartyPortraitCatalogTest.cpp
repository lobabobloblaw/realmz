#include "remaster/assets/PartyPortraitCatalog.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include "presentation/PresentationMode.hpp"
#include "remaster/assets/PayloadDigest.hpp"
#include "remaster/assets/ResourceSelectionHook.hpp"

using realmz::presentation::PresentationMode;
using realmz::remaster::assets::AssetDimensions;
using realmz::remaster::assets::AssetManifestError;
using realmz::remaster::assets::AssetResolutionKind;
using realmz::remaster::assets::PartyPortraitCatalog;
using realmz::remaster::assets::PartyPortraitCatalogError;
using realmz::remaster::assets::PostSelectionResult;
using realmz::remaster::assets::ResourceKey;
using realmz::remaster::assets::ResourceSelectionHook;
using realmz::remaster::assets::assetContentSha256Hex;
using realmz::remaster::assets::payloadSha256Hex;

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t typeCode(const char (&value)[5]) {
  return
      (static_cast<std::uint32_t>(
           static_cast<unsigned char>(value[0])) << 24U) |
      (static_cast<std::uint32_t>(
           static_cast<unsigned char>(value[1])) << 16U) |
      (static_cast<std::uint32_t>(
           static_cast<unsigned char>(value[2])) << 8U) |
      static_cast<std::uint32_t>(static_cast<unsigned char>(value[3]));
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string loadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open test fixture file");
  }
  return {
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>(),
  };
}

void saveFile(const fs::path& path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot create test fixture file");
  }
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output) {
    throw std::runtime_error("cannot write test fixture file");
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
        "test fixture replacement count changed for " + std::string(from));
  }
}

void replaceInManifestEntry(
    std::string& manifest,
    std::string_view assetPath,
    std::string_view from,
    std::string_view to) {
  const auto anchor = manifest.find(assetPath);
  if (anchor == std::string::npos) {
    throw std::runtime_error("test manifest entry anchor is missing");
  }
  const auto entryStart = manifest.rfind("\n    {", anchor);
  const auto entryEnd = manifest.find("\n    }", anchor);
  if (entryStart == std::string::npos || entryEnd == std::string::npos) {
    throw std::runtime_error("cannot isolate test manifest entry");
  }
  const auto position = manifest.find(from, entryStart);
  if (position == std::string::npos || position >= entryEnd) {
    throw std::runtime_error("test manifest entry token is missing");
  }
  manifest.replace(position, from.size(), to);
}

template <typename Callback>
void requireCatalogFailure(Callback&& callback, std::string_view message) {
  try {
    callback();
  } catch (const PartyPortraitCatalogError&) {
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
        ("realmz-party-portrait-catalog-test-" + unique) /
        fs::path(u8"public-é-界");
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

void copyApprovedOutputs(
    const fs::path& assetRoot,
    const fs::path& targetRoot) {
  const auto source = assetRoot / "style-proof/generation/outputs";
  const auto destination =
      targetRoot / "style-proof/generation/outputs";
  fs::create_directories(destination);
  for (const auto& item : fs::directory_iterator(source)) {
    if (item.is_regular_file() && item.path().extension() == ".png") {
      fs::copy_file(
          item.path(), destination / item.path().filename(),
          fs::copy_options::overwrite_existing);
    }
  }
}

struct ExpectedPortrait {
  std::int16_t resourceId;
  std::string_view relativePath;
  std::string_view classicPayloadSha256;
  std::string_view approvedContentSha256;
  std::string_view fixturePayload;
};

constexpr std::array<ExpectedPortrait, 4> kExpectedPortraits{{
    {
        257,
        "style-proof/generation/outputs/05_portrait_cicn_257.png",
        "37c97669cc29f3366d06158e674e0d781ca7660e5ede33fc05325368f25a2c6d",
        "8eae998f6e9d745911ed518f7ad2629abd95a72d5c66c5ba0e9655663e0f2cc1",
        "synthetic immutable portrait payload 257",
    },
    {
        267,
        "style-proof/generation/outputs/06_portrait_cicn_267.png",
        "c6b9a296b819c1fc4df139f564efaf1c5c8b0650a5abfd542d559d62dca71479",
        "5786f9cbf6901739f34acaa72f603cdc62a53423d94b31956fbe90f1af9be281",
        "synthetic immutable portrait payload 267",
    },
    {
        297,
        "style-proof/generation/outputs/07_portrait_cicn_297.png",
        "09874978f4424d6b1d6311b90b5125d79a2cf18a185f76b3aec3b2b54f5d3e08",
        "c5bc0a5c315cfc115d6d1a806f9396670e59734a84102dc492d547cced78aca0",
        "synthetic immutable portrait payload 297",
    },
    {
        337,
        "style-proof/generation/outputs/08_portrait_cicn_337.png",
        "9d7bfacdf07175ccdfb43084e165fd63160b45903b894079a1f6cb5009a73c5c",
        "3bc7ac4e0d6bb17cab9562f0f2f4db178c9eddc584f658e524ab947b59a3ec5e",
        "synthetic immutable portrait payload 337",
    },
}};

template <typename Catalog>
concept HasIdOnlyAuthorization = requires(
    const Catalog& catalog, std::int16_t resourceId) {
  catalog.authorizeSelectedPortrait(resourceId);
};

static_assert(!HasIdOnlyAuthorization<PartyPortraitCatalog>);

struct IsolatedFixture {
  fs::path manifestPath;
  fs::path censusPath;
};

IsolatedFixture makeIsolatedFixture(
    const fs::path& productionManifest,
    const fs::path& productionCensus,
    const fs::path& productionAssetRoot,
    const fs::path& fixtureRoot) {
  copyApprovedOutputs(productionAssetRoot, fixtureRoot);
  const auto originalCensus = loadFile(productionCensus);
  auto census = originalCensus;
  auto manifest = loadFile(productionManifest);
  for (const auto& expected : kExpectedPortraits) {
    const auto fixtureDigest = payloadSha256Hex(expected.fixturePayload);
    replaceExactCount(
        census,
        expected.classicPayloadSha256,
        fixtureDigest,
        2U);
    replaceExactCount(
        manifest,
        expected.classicPayloadSha256,
        fixtureDigest,
        2U);
  }
  replaceExactCount(
      manifest,
      assetContentSha256Hex(originalCensus),
      assetContentSha256Hex(census),
      1U);

  const IsolatedFixture result{
      .manifestPath = fixtureRoot / "phase1.runtime-manifest.json",
      .censusPath = fixtureRoot / "phase1.census.json",
  };
  saveFile(result.manifestPath, manifest);
  saveFile(result.censusPath, census);
  return result;
}

void verifyProductionCatalog(
    const PartyPortraitCatalog& catalog,
    const fs::path& assetRoot) {
  const auto& portraits = catalog.portraits();
  require(portraits.size() == kExpectedPortraits.size(),
      "party portrait catalog size changed");
  std::set<std::string> paths;
  std::set<std::string> classicDigests;
  std::set<std::string> approvedDigests;
  for (std::size_t index = 0; index < portraits.size(); ++index) {
    const auto& expected = kExpectedPortraits[index];
    const auto& portrait = portraits[index];
    require(portrait.key == ResourceKey{
        "Data Files/Portraits", "cicn", expected.resourceId},
        "party portrait ResourceKey order changed");
    require(portrait.path ==
            fs::weakly_canonical(assetRoot / expected.relativePath),
        "party portrait approved path changed");
    require(portrait.logicalDimensions == AssetDimensions{44U, 44U},
        "party portrait logical dimensions changed");
    require(portrait.classicPayloadSha256 ==
            expected.classicPayloadSha256,
        "party portrait Classic payload digest changed");
    require(portrait.approvedContentSha256 ==
            expected.approvedContentSha256,
        "party portrait approved output digest changed");
    require(paths.emplace(portrait.path.generic_string()).second,
        "party portrait paths are not unique");
    require(classicDigests.emplace(portrait.classicPayloadSha256).second,
        "party portrait Classic digests are not unique");
    require(approvedDigests.emplace(portrait.approvedContentSha256).second,
        "party portrait approved digests are not unique");
  }
}

void requireUnauthorized(
    const PartyPortraitCatalog& catalog,
    const PostSelectionResult& selection,
    std::string_view message) {
  require(catalog.authorizeSelectedPortrait(selection) == nullptr, message);
}

void verifyProofAuthorization(
    const PartyPortraitCatalog& catalog,
    ResourceSelectionHook& hook) {
  const auto& portraits = catalog.portraits();
  for (std::size_t index = 0; index < portraits.size(); ++index) {
    const auto& expected = kExpectedPortraits[index];
    const auto& portrait = portraits[index];
    const auto selection = hook.inspect(
        "Data Files/Portraits",
        typeCode("cicn"),
        expected.resourceId,
        expected.fixturePayload);
    require(selection.resolution.kind == AssetResolutionKind::Override,
        "correct immutable payload did not receive override coverage");
    require(catalog.authorizeSelectedPortrait(selection) == &portrait,
        "exact fresh post-selection proof was not authorized");

    auto mismatch = selection;
    mismatch.resolution.logicalDimensions = AssetDimensions{43U, 44U};
    requireUnauthorized(catalog, mismatch,
        "portrait width mismatch was authorized");
    mismatch = selection;
    mismatch.resolution.logicalDimensions = AssetDimensions{44U, 43U};
    requireUnauthorized(catalog, mismatch,
        "portrait height mismatch was authorized");
    mismatch = selection;
    mismatch.resolution.logicalDimensions.reset();
    requireUnauthorized(catalog, mismatch,
        "missing portrait dimensions were authorized");

    const auto wrongPayload = hook.inspect(
        "Data Files/Portraits",
        typeCode("cicn"),
        expected.resourceId,
        "wrong immutable Resource Manager payload");
    require(wrongPayload.resolution.kind ==
            AssetResolutionKind::CoverageFailure,
        "wrong immutable payload unexpectedly retained coverage");
    requireUnauthorized(catalog, wrongPayload,
        "selection token derived from a wrong immutable payload was authorized");
  }

  const auto exact = hook.inspect(
      "Data Files/Portraits",
      typeCode("cicn"),
      kExpectedPortraits[0].resourceId,
      kExpectedPortraits[0].fixturePayload);
  auto forged = exact;
  forged.immutablePayloadSha256 = payloadSha256Hex("custom payload");
  requireUnauthorized(catalog, forged,
      "forged custom-payload digest was authorized");

  forged = exact;
  forged.key.pack = "Scenarios/Tutorial/Scenario";
  requireUnauthorized(catalog, forged,
      "other-pack portrait proof was authorized");
  forged = exact;
  forged.key.id = kExpectedPortraits[1].resourceId;
  requireUnauthorized(catalog, forged,
      "ID-only portrait substitution was authorized");
  forged = exact;
  forged.key.type = "PICT";
  requireUnauthorized(catalog, forged,
      "other-type portrait proof was authorized");

  forged = exact;
  forged.resolution.overridePath =
      catalog.portraits()[1].path;
  requireUnauthorized(catalog, forged,
      "changed approved portrait path was authorized");
  forged = exact;
  forged.resolution.overridePath.reset();
  requireUnauthorized(catalog, forged,
      "missing approved portrait path was authorized");
  forged = exact;
  forged.resolution.masterKey = catalog.portraits()[1].key;
  requireUnauthorized(catalog, forged,
      "changed portrait master key was authorized");
  forged = exact;
  forged.resolution.masterKey.reset();
  requireUnauthorized(catalog, forged,
      "missing portrait master key was authorized");
  forged = exact;
  forged.resolution.approvedContentSha256 =
      catalog.portraits()[1].approvedContentSha256;
  requireUnauthorized(catalog, forged,
      "changed approved portrait digest was authorized");
  forged = exact;
  forged.resolution.approvedContentSha256.reset();
  requireUnauthorized(catalog, forged,
      "missing approved portrait digest was authorized");

  for (const auto kind : {
           AssetResolutionKind::ClassicBypass,
           AssetResolutionKind::ClassicPassthrough,
           AssetResolutionKind::CoverageFailure,
       }) {
    forged = exact;
    forged.resolution.kind = kind;
    requireUnauthorized(catalog, forged,
        "non-override portrait selection was authorized");
  }

  const PostSelectionResult passthrough{
      .key = ResourceKey{"Data Files/Portraits", "cicn", 258},
      .immutablePayloadSha256 = payloadSha256Hex("passthrough"),
      .resolution = {
          .kind = AssetResolutionKind::ClassicPassthrough,
          .overridePath = std::nullopt,
          .masterKey = ResourceKey{"Data Files/Portraits", "cicn", 258},
          .diagnostic = "test passthrough",
      },
  };
  requireUnauthorized(catalog, passthrough,
      "Classic passthrough portrait was authorized");
}

void verifyCatalogRejectsChangedBindings(
    const std::string& productionManifest,
    const fs::path& censusPath,
    const fs::path& assetRoot,
    const fs::path& fixtureRoot) {
  const auto& first = kExpectedPortraits[0];

  auto changedFamily = productionManifest;
  replaceInManifestEntry(
      changedFamily,
      first.relativePath,
      "\"semantic_family\": \"portrait\"",
      "\"semantic_family\": \"ui_surface\"");
  const auto changedFamilyPath = fixtureRoot / "changed-family.json";
  saveFile(changedFamilyPath, changedFamily);
  requireCatalogFailure(
      [&] {
        (void)PartyPortraitCatalog::load(
            changedFamilyPath, censusPath, assetRoot);
      },
      "changed portrait semantic family was accepted");

  auto changedAlpha = productionManifest;
  replaceInManifestEntry(
      changedAlpha,
      first.relativePath,
      "\"alpha_policy\": \"original_mask\"",
      "\"alpha_policy\": \"opaque_or_embedded_mask\"");
  const auto changedAlphaPath = fixtureRoot / "changed-alpha.json";
  saveFile(changedAlphaPath, changedAlpha);
  requireCatalogFailure(
      [&] {
        (void)PartyPortraitCatalog::load(
            changedAlphaPath, censusPath, assetRoot);
      },
      "changed portrait alpha policy was accepted");

  auto changedDimensions = productionManifest;
  replaceInManifestEntry(
      changedDimensions,
      first.relativePath,
      "\"height\": 44",
      "\"height\": 43");
  const auto changedDimensionsPath = fixtureRoot / "changed-dimensions.json";
  saveFile(changedDimensionsPath, changedDimensions);
  requireCatalogFailure(
      [&] {
        (void)PartyPortraitCatalog::load(
            changedDimensionsPath, censusPath, assetRoot);
      },
      "changed portrait logical dimensions were accepted");

  auto changedKey = productionManifest;
  replaceInManifestEntry(
      changedKey,
      first.relativePath,
      "\"key\": {\n        \"id\": 257",
      "\"key\": {\n        \"id\": 258");
  const auto changedKeyPath = fixtureRoot / "changed-key.json";
  saveFile(changedKeyPath, changedKey);
  requireCatalogFailure(
      [&] {
        (void)PartyPortraitCatalog::load(
            changedKeyPath, censusPath, assetRoot);
      },
      "changed portrait key was accepted");

  auto changedMaster = productionManifest;
  replaceInManifestEntry(
      changedMaster,
      first.relativePath,
      "\"master_key\": {\n        \"id\": 257",
      "\"master_key\": {\n        \"id\": 267");
  const auto changedMasterPath = fixtureRoot / "changed-master.json";
  saveFile(changedMasterPath, changedMaster);
  requireCatalogFailure(
      [&] {
        (void)PartyPortraitCatalog::load(
            changedMasterPath, censusPath, assetRoot);
      },
      "changed portrait master key was accepted");

  auto changedDigest = productionManifest;
  replaceInManifestEntry(
      changedDigest,
      first.relativePath,
      std::string("\"shared_master_sha256\": \"") +
          std::string(first.approvedContentSha256) + "\"",
      "\"shared_master_sha256\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"");
  const auto changedDigestPath = fixtureRoot / "changed-digest.json";
  saveFile(changedDigestPath, changedDigest);
  requireCatalogFailure(
      [&] {
        (void)PartyPortraitCatalog::load(
            changedDigestPath, censusPath, assetRoot);
      },
      "changed approved portrait digest was accepted");

  auto changedPath = productionManifest;
  replaceInManifestEntry(
      changedPath,
      first.relativePath,
      first.relativePath,
      "style-proof/generation/outputs/missing-portrait.png");
  const auto changedPathManifest = fixtureRoot / "changed-path.json";
  saveFile(changedPathManifest, changedPath);
  requireCatalogFailure(
      [&] {
        (void)PartyPortraitCatalog::load(
            changedPathManifest, censusPath, assetRoot);
      },
      "changed approved portrait path was accepted");

  // A manifest may intentionally reuse one approved master at one path for
  // duplicate Classic resources. These four exact portraits may not: their
  // catalog requires four distinct paths and output digests.
  auto duplicateBinding = productionManifest;
  replaceInManifestEntry(
      duplicateBinding,
      first.relativePath,
      std::string("\"shared_master_sha256\": \"") +
          std::string(first.approvedContentSha256) + "\"",
      std::string("\"shared_master_sha256\": \"") +
          std::string(kExpectedPortraits[1].approvedContentSha256) + "\"");
  replaceInManifestEntry(
      duplicateBinding,
      first.relativePath,
      first.relativePath,
      kExpectedPortraits[1].relativePath);
  const auto duplicateBindingPath = fixtureRoot / "duplicate-binding.json";
  saveFile(duplicateBindingPath, duplicateBinding);
  requireCatalogFailure(
      [&] {
        (void)PartyPortraitCatalog::load(
            duplicateBindingPath, censusPath, assetRoot);
      },
      "duplicate portrait path and approved digest were accepted");
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto repositoryRoot = fs::weakly_canonical(
        argc > 1 ? fs::path(argv[1]) : fs::path("."));
    const auto assetRoot = repositoryRoot / "assets/remastered";
    const auto manifestPath =
        assetRoot / "scopes/phase1.runtime-manifest.json";
    const auto censusPath = assetRoot / "scopes/phase1.census.json";

    const auto productionCatalog = PartyPortraitCatalog::load(
        manifestPath, censusPath, assetRoot);
    verifyProductionCatalog(productionCatalog, assetRoot);

    TemporaryDirectory temporary;
    const auto fixture = makeIsolatedFixture(
        manifestPath,
        censusPath,
        assetRoot,
        temporary.path());
    require(temporary.path().generic_string().find("é-界") !=
            std::string::npos,
        "isolated fixture root lost its non-ASCII components");
    require(!fs::exists(temporary.path() / "base"),
        "isolated public fixture unexpectedly contains Classic resource forks");

    const auto isolatedCatalog = PartyPortraitCatalog::load(
        fixture.manifestPath, fixture.censusPath, temporary.path());
    ResourceSelectionHook hook(
        fixture.manifestPath, fixture.censusPath, temporary.path());
    hook.setPresentationMode(PresentationMode::remastered);
    verifyProofAuthorization(isolatedCatalog, hook);

    ResourceSelectionHook classicHook(
        fixture.manifestPath, fixture.censusPath, temporary.path());
    const auto classicSelection = classicHook.inspect(
        "Data Files/Portraits",
        typeCode("cicn"),
        kExpectedPortraits[0].resourceId,
        kExpectedPortraits[0].fixturePayload);
    requireUnauthorized(isolatedCatalog, classicSelection,
        "Classic-bypass selection was authorized");

    verifyCatalogRejectsChangedBindings(
        loadFile(manifestPath),
        censusPath,
        assetRoot,
        temporary.path());

    std::cout <<
        "PartyPortraitCatalogTest passed: 4 proof-bound portraits\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "PartyPortraitCatalogTest failed: "
              << error.what() << '\n';
    return 1;
  }
}
