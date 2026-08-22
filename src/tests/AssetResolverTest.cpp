#include "remaster/assets/AssetResolver.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using realmz::remaster::assets::AssetManifest;
using realmz::remaster::assets::AssetManifestError;
using realmz::remaster::assets::AssetResolutionKind;
using realmz::remaster::assets::AssetResolver;
using realmz::remaster::assets::ResourceKey;
using realmz::remaster::assets::assetContentSha256Hex;
using realmz::presentation::PresentationMode;

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string loadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void saveFile(const std::filesystem::path& path, std::string_view contents) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot create " + path.string());
  }
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output) {
    throw std::runtime_error("cannot write " + path.string());
  }
}

void replaceFirst(std::string& text, std::string_view from, std::string_view to) {
  const auto position = text.find(from);
  if (position == std::string::npos) {
    throw std::runtime_error("test fixture token not found: " + std::string(from));
  }
  text.replace(position, from.size(), to);
}

void removeManifestCoverage(std::string& text) {
  const auto entries = text.find("\"entries\": [");
  const auto arrayStart = text.find('[', entries);
  const auto endMarker = text.find("\n  ],\n  \"manifest_kind\"", arrayStart);
  if (entries == std::string::npos || arrayStart == std::string::npos ||
      endMarker == std::string::npos) {
    throw std::runtime_error("cannot locate top-level manifest entries fixture");
  }
  text.replace(arrayStart, endMarker + 4 - arrayStart, "[]");
}

template <typename Callback>
void requireManifestFailure(Callback&& callback, std::string_view message) {
  try {
    callback();
  } catch (const AssetManifestError&) {
    return;
  }
  throw std::runtime_error(std::string(message));
}

template <typename Callback>
std::string manifestFailureMessage(Callback&& callback) {
  try {
    callback();
  } catch (const AssetManifestError& error) {
    return error.what();
  }
  throw std::runtime_error("expected AssetManifestError");
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto repositoryRoot =
        std::filesystem::weakly_canonical(argc > 1 ? argv[1] : std::filesystem::path("."));
    const auto censusPath =
        repositoryRoot / "assets/remastered/scopes/phase1.census.json";
    const auto manifestPath =
        repositoryRoot / "assets/remastered/scopes/phase1.placeholder-manifest.json";
    const auto assetRoot = repositoryRoot / "assets/remastered";

    auto manifest = AssetManifest::load(manifestPath, censusPath, assetRoot);
    require(manifest.scope() == "phase1", "scope was not retained");
    require(manifest.size() == 1520, "phase-one ResourceKey count changed unexpectedly");
    require(manifest.uniqueMasterCount() == 1497, "phase-one master count changed unexpectedly");
    require(manifest.duplicateReuseCount() == 23, "duplicate reuse count changed unexpectedly");

    const ResourceKey tutorialPicture{"Scenarios/Tutorial/Scenario", "PICT", 32128};
    const ResourceKey cityPicture{"Scenarios/City of Bywater/Scenario", "PICT", 32128};
    const auto* tutorialEntry = manifest.find(tutorialPicture);
    const auto* cityEntry = manifest.find(cityPicture);
    require(tutorialEntry != nullptr && cityEntry != nullptr,
        "scenario-local PICT:32128 keys were flattened");
    require(tutorialEntry->classicPayloadSha256 != cityEntry->classicPayloadSha256,
        "scenario collision test needs distinct classic payloads");
    const auto tutorialPayloadSha256 = tutorialEntry->classicPayloadSha256;

    AssetResolver resolver(std::move(manifest));
    const ResourceKey missing{"Scenarios/Not Installed/Scenario", "PICT", 32128};
    const auto classicMissing =
        resolver.resolve(PresentationMode::classic, missing);
    require(
        classicMissing.kind == AssetResolutionKind::ClassicBypass,
        "Classic mode did not bypass remastered coverage");
    require(!classicMissing.approvedContentSha256,
        "Classic bypass unexpectedly carried an approved content digest");
    const auto uncoveredMissing =
        resolver.resolve(PresentationMode::remastered, missing);
    require(
        uncoveredMissing.kind == AssetResolutionKind::CoverageFailure,
        "Remastered mode did not report missing coverage");
    require(!uncoveredMissing.approvedContentSha256,
        "coverage failure unexpectedly carried an approved content digest");
    const auto tutorialPassthrough =
        resolver.resolve(PresentationMode::remastered, tutorialPicture);
    require(
        tutorialPassthrough.kind == AssetResolutionKind::ClassicPassthrough,
        "placeholder coverage did not resolve as ClassicPassthrough");
    require(!tutorialPassthrough.approvedContentSha256,
        "Classic passthrough unexpectedly carried an approved content digest");
    require(
        resolver.resolve(
            PresentationMode::remastered, tutorialPicture, tutorialPayloadSha256).kind ==
            AssetResolutionKind::ClassicPassthrough,
        "matching immutable payload digest did not retain placeholder coverage");
    const auto mismatch = resolver.resolve(
        PresentationMode::remastered, tutorialPicture,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    require(mismatch.kind == AssetResolutionKind::CoverageFailure,
        "payload mismatch did not fail remastered coverage");
    require(mismatch.diagnostic ==
            "Classic payload SHA-256 mismatch for Scenarios/Tutorial/Scenario:PICT:32128: "
            "expected " + tutorialPayloadSha256 +
            ", got aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "payload mismatch diagnostic is not deterministic");
    require(
        resolver.resolve(
            PresentationMode::classic, tutorialPicture,
            "not-even-a-digest").kind == AssetResolutionKind::ClassicBypass,
        "Classic mode did not bypass immutable-payload validation");
    require(
        resolver.resolve(PresentationMode::remastered, cityPicture).kind ==
            AssetResolutionKind::ClassicPassthrough,
        "City placeholder coverage did not preserve its scenario pack");

    const ResourceKey duplicate{"Data Files/The Family Jewels", "cicn", 794};
    const ResourceKey canonical{"Data Files/The Family Jewels", "cicn", 486};
    const auto duplicateResult = resolver.resolve(PresentationMode::remastered, duplicate);
    require(duplicateResult.masterKey == canonical, "duplicate did not reuse its canonical master");

    require(AssetManifest::isSafeAssetPath("generated/ab/cd.png"), "safe path rejected");
    require(!AssetManifest::isSafeAssetPath("../escape.png"), "parent traversal accepted");
    require(!AssetManifest::isSafeAssetPath("a/../escape.png"), "nested traversal accepted");
    require(!AssetManifest::isSafeAssetPath("/absolute.png"), "absolute path accepted");
    require(!AssetManifest::isSafeAssetPath("a\\escape.png"), "backslash path accepted");
    require(!AssetManifest::isSafeAssetPath("asset.jpg"), "non-PNG path accepted");

    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temporaryRoot = std::filesystem::temp_directory_path() /
        ("realmz-asset-resolver-test-" + unique);
    std::filesystem::create_directories(temporaryRoot);

    auto unknownFieldManifest = loadFile(manifestPath);
    replaceFirst(unknownFieldManifest, "{\n", "{\n  \"unexpected\": true,\n");
    const auto unknownFieldPath = temporaryRoot / "unknown-field.json";
    saveFile(unknownFieldPath, unknownFieldManifest);
    requireManifestFailure(
        [&] { (void)AssetManifest::load(unknownFieldPath, censusPath, temporaryRoot); },
        "unknown manifest field was accepted");

    auto collisionManifest = loadFile(manifestPath);
    replaceFirst(collisionManifest,
        "\"pack\": \"Scenarios/Tutorial/Scenario\"",
        "\"pack\": \"Scenarios/City of Bywater/Scenario\"");
    const auto collisionPath = temporaryRoot / "collision.json";
    saveFile(collisionPath, collisionManifest);
    requireManifestFailure(
        [&] { (void)AssetManifest::load(collisionPath, censusPath, temporaryRoot); },
        "scenario ResourceKey collision was accepted");

    auto incompleteManifest = loadFile(manifestPath);
    removeManifestCoverage(incompleteManifest);
    const auto incompletePath = temporaryRoot / "incomplete.json";
    saveFile(incompletePath, incompleteManifest);
    requireManifestFailure(
        [&] { (void)AssetManifest::load(incompletePath, censusPath, temporaryRoot); },
        "manifest/census parity failure was accepted");

    auto approvedManifest = loadFile(manifestPath);
    replaceFirst(approvedManifest,
        "\"manifest_kind\": \"zero-cost-placeholder\"",
        "\"manifest_kind\": \"production\"");
    replaceFirst(approvedManifest, "\"asset_path\": null", "\"asset_path\": \"test-approved.png\"");
    replaceFirst(approvedManifest,
        "\"kind\": \"none\",\n        \"model\": null,\n        \"provider\": null",
        "\"kind\": \"imagegen\",\n        \"model\": \"test-model\",\n        \"provider\": \"test-provider\"");
    replaceFirst(approvedManifest, "\"prompt_sha256\": null",
        "\"prompt_sha256\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"");
    replaceFirst(approvedManifest, "\"reviewer\": null", "\"reviewer\": \"test-reviewer\"");
    replaceFirst(approvedManifest,
        "\"shared_master_sha256\": \"37c97669cc29f3366d06158e674e0d781ca7660e5ede33fc05325368f25a2c6d\"",
        "\"shared_master_sha256\": \"4c4b6a3be1314ab86138bef4314dde022e600960d8689a2c8f8631802d20dab6\"");
    replaceFirst(approvedManifest, "\"status\": \"classic_passthrough\"", "\"status\": \"approved\"");
    const auto approvedPath = temporaryRoot / "approved.json";
    saveFile(approvedPath, approvedManifest);
    saveFile(temporaryRoot / "test-approved.png", "\x89PNG\r\n\x1a\n");
    auto production = AssetManifest::load(approvedPath, censusPath, temporaryRoot);
    AssetResolver productionResolver(std::move(production));
    const ResourceKey firstEntry{"Data Files/Portraits", "cicn", 257};
    const auto overrideResult =
        productionResolver.resolve(PresentationMode::remastered, firstEntry);
    require(overrideResult.kind == AssetResolutionKind::Override,
        "approved entry did not resolve as Override");
    require(overrideResult.overridePath ==
            std::filesystem::weakly_canonical(temporaryRoot / "test-approved.png"),
        "override path did not resolve beneath the asset root");
    require(overrideResult.logicalDimensions ==
            realmz::remaster::assets::AssetDimensions{.width = 44, .height = 44},
        "override did not retain the Classic logical dimensions");
    require(overrideResult.approvedContentSha256 ==
            "4c4b6a3be1314ab86138bef4314dde022e600960d8689a2c8f8631802d20dab6",
        "override did not carry the exact approved content digest");

    auto unicodeManifest = approvedManifest;
    replaceFirst(unicodeManifest, "test-approved.png",
        "art-\\u00e9/portrait-\\u754c.png");
    const auto unicodeRelative =
        std::filesystem::path(std::u8string(u8"art-\u00e9/portrait-\u754c.png"));
    std::filesystem::create_directories(
        (temporaryRoot / unicodeRelative).parent_path());
    std::filesystem::copy_file(
        temporaryRoot / "test-approved.png", temporaryRoot / unicodeRelative);
    const auto unicodeManifestPath = temporaryRoot / "unicode-approved.json";
    saveFile(unicodeManifestPath, unicodeManifest);
    AssetResolver unicodeResolver(AssetManifest::load(
        unicodeManifestPath, censusPath, temporaryRoot));
    const auto unicodeOverride = unicodeResolver.resolve(
        PresentationMode::remastered, firstEntry);
    require(unicodeOverride.overridePath ==
            std::filesystem::weakly_canonical(temporaryRoot / unicodeRelative),
        "UTF-8 asset_path did not round-trip through the native path type");

    for (const auto& invalidPath : {
             std::string("invalid-\xFF.png", 13U),
             std::string("invalid-\xC0\xAF.png", 14U),
             std::string("invalid-\xED\xA0\x80.png", 15U),
             std::string("invalid-\xC2\x85.png", 14U),
         }) {
      auto invalidUtf8Manifest = approvedManifest;
      replaceFirst(invalidUtf8Manifest, "test-approved.png", invalidPath);
      const auto invalidUtf8ManifestPath =
          temporaryRoot / ("invalid-utf8-" +
              std::to_string(static_cast<unsigned char>(invalidPath[8])) +
              ".json");
      saveFile(invalidUtf8ManifestPath, invalidUtf8Manifest);
      const auto invalidUtf8Failure = manifestFailureMessage([&] {
        (void)AssetManifest::load(
            invalidUtf8ManifestPath, censusPath, temporaryRoot);
      });
      require(invalidUtf8Failure ==
              "manifest.entries[0].asset_path is not a portable UTF-8 path",
          "invalid UTF-8 asset_path did not fail closed");
      require(invalidUtf8Failure.find(temporaryRoot.string()) ==
              std::string::npos,
          "invalid UTF-8 asset_path diagnostic leaked its host path");
    }

    const auto oversizedManifestPath = temporaryRoot / "oversized.json";
    saveFile(oversizedManifestPath,
        std::string(8U * 1024U * 1024U + 1U, 'x'));
    const auto oversizedManifestFailure = manifestFailureMessage([&] {
      (void)AssetManifest::load(
          oversizedManifestPath, censusPath, temporaryRoot);
    });
    require(oversizedManifestFailure == "asset manifest has an invalid size",
        "oversized manifest did not fail with a stable bounded-read diagnostic");
    require(oversizedManifestFailure.find(temporaryRoot.string()) ==
            std::string::npos,
        "bounded manifest diagnostic leaked its host path");

    const std::string oversizedAsset(16U * 1024U * 1024U + 1U, 'x');
    auto oversizedAssetManifest = approvedManifest;
    replaceFirst(oversizedAssetManifest,
        "4c4b6a3be1314ab86138bef4314dde022e600960d8689a2c8f8631802d20dab6",
        assetContentSha256Hex(oversizedAsset));
    const auto oversizedAssetManifestPath =
        temporaryRoot / "oversized-asset.json";
    saveFile(oversizedAssetManifestPath, oversizedAssetManifest);
    saveFile(temporaryRoot / "test-approved.png", oversizedAsset);
    const auto oversizedAssetFailure = manifestFailureMessage([&] {
      (void)AssetManifest::load(
          oversizedAssetManifestPath, censusPath, temporaryRoot);
    });
    require(oversizedAssetFailure ==
            "manifest.entries[0].asset_path has an invalid size",
        "oversized approved asset did not fail before hashing");
    require(oversizedAssetFailure.find(temporaryRoot.string()) ==
            std::string::npos,
        "bounded asset diagnostic leaked its host path");

    saveFile(temporaryRoot / "test-approved.png", "tampered");
    requireManifestFailure(
        [&] { (void)AssetManifest::load(approvedPath, censusPath, temporaryRoot); },
        "approved PNG hash mismatch was accepted");

    std::filesystem::remove_all(temporaryRoot);
    std::cout << "AssetResolverTest passed: 1520 keys, 1497 masters, 23 duplicate reuses\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "AssetResolverTest failed: " << error.what() << '\n';
    return 1;
  }
}
