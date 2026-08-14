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
    require(
        resolver.resolve(PresentationMode::classic, missing).kind ==
            AssetResolutionKind::ClassicBypass,
        "Classic mode did not bypass remastered coverage");
    require(
        resolver.resolve(PresentationMode::remastered, missing).kind ==
            AssetResolutionKind::CoverageFailure,
        "Remastered mode did not report missing coverage");
    require(
        resolver.resolve(PresentationMode::remastered, tutorialPicture).kind ==
            AssetResolutionKind::ClassicPassthrough,
        "placeholder coverage did not resolve as ClassicPassthrough");
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
