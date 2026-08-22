#include "remaster/assets/PayloadDigest.hpp"
#include "remaster/assets/ResourceSelectionHook.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using realmz::presentation::PresentationMode;
using realmz::remaster::assets::AssetResolutionKind;
using realmz::remaster::assets::ResourceSelectionHook;
using realmz::remaster::assets::isPhaseOneRasterResourceType;
using realmz::remaster::assets::logicalPackForOpenRequest;
using realmz::remaster::assets::payloadSha256Hex;
using realmz::remaster::assets::resourceTypeString;

constexpr std::uint32_t typeCode(const char (&value)[5]) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(value[0])) << 24U) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(value[1])) << 16U) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(value[2])) << 8U) |
      static_cast<std::uint32_t>(static_cast<unsigned char>(value[3]));
}

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto repositoryRoot = std::filesystem::weakly_canonical(
        argc > 1 ? argv[1] : std::filesystem::path("."));
    const auto remasteredRoot = repositoryRoot / "assets/remastered";
    const auto manifestPath =
        remasteredRoot / "scopes/phase1.placeholder-manifest.json";
    const auto censusPath = remasteredRoot / "scopes/phase1.census.json";

    require(logicalPackForOpenRequest(
                ":Data Files:The Family Jewels", true) ==
            "Data Files/The Family Jewels",
        "core FSSpec request did not produce its stable pack identity");
    require(logicalPackForOpenRequest(
                ":Scenarios:Tutorial:Scenario", true) ==
            "Scenarios/Tutorial/Scenario",
        "scenario FSSpec request did not preserve scenario-local identity");
    require(logicalPackForOpenRequest(
                ":Scenarios:City of Bywater:Scenario", true) ==
            "Scenarios/City of Bywater/Scenario",
        "City FSSpec request did not preserve its distinct scenario identity");
    require(!logicalPackForOpenRequest("Realmz Preferences", false).has_value(),
        "user-preferences FSSpec was assigned a content-pack identity");
    require(!logicalPackForOpenRequest("::escape", true).has_value(),
        "parent/empty Classic path component was accepted");
    require(!logicalPackForOpenRequest(":Data Files::Portraits", true).has_value(),
        "empty nested Classic path component was accepted");
    require(!logicalPackForOpenRequest("/absolute", true).has_value(),
        "absolute host path was accepted as an open-request identity");

    require(resourceTypeString(typeCode("PICT")) == "PICT",
        "PICT type serialization changed byte order");
    require(resourceTypeString(typeCode("cicn")) == "cicn",
        "cicn type serialization changed byte order");
    require(isPhaseOneRasterResourceType(typeCode("PICT")),
        "PICT was not recognized as a selected raster type");
    require(isPhaseOneRasterResourceType(typeCode("cicn")),
        "cicn was not recognized as a selected raster type");
    require(isPhaseOneRasterResourceType(typeCode("crsr")),
        "crsr was not recognized as a selected raster type");
    require(isPhaseOneRasterResourceType(typeCode("ppat")),
        "ppat was not recognized as a selected raster type");
    require(!isPhaseOneRasterResourceType(typeCode("STR#")),
        "non-raster STR# resource entered remastered coverage");

    require(payloadSha256Hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "empty SHA-256 vector failed");
    require(payloadSha256Hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "abc SHA-256 vector failed");

    // Missing paths prove Classic mode is a true bypass: no manifest access
    // and no payload hash are needed to preserve legacy behavior.
    ResourceSelectionHook classicHook(
        repositoryRoot / "does-not-exist/manifest.json",
        repositoryRoot / "does-not-exist/census.json",
        repositoryRoot / "does-not-exist");
    const auto classic = classicHook.inspect(
        "Data Files/Portraits", typeCode("cicn"), 257, "mutable later");
    require(classic.resolution.kind == AssetResolutionKind::ClassicBypass,
        "Classic selection did not bypass remastered assets");
    require(classic.immutablePayloadSha256.empty(),
        "Classic selection unnecessarily hashed its payload");

    ResourceSelectionHook failingHook(
        repositoryRoot / "private-location/manifest.json",
        repositoryRoot / "private-location/census.json",
        repositoryRoot / "private-location");
    failingHook.setPresentationMode(PresentationMode::remastered);
    const auto unavailable = failingHook.inspect(
        "Data Files/Portraits", typeCode("cicn"), 257, "abc");
    require(unavailable.resolution.kind ==
            AssetResolutionKind::CoverageFailure,
        "unavailable manifest did not fail closed");
    require(unavailable.resolution.diagnostic ==
            "Remastered asset manifest unavailable: validation failed",
        "manifest initialization failure was not sanitized");
    require(unavailable.resolution.diagnostic.find(
                repositoryRoot.string()) == std::string::npos,
        "manifest initialization failure leaked a host path");

    ResourceSelectionHook hook(manifestPath, censusPath, remasteredRoot);
    hook.setPresentationMode(PresentationMode::remastered);
    const auto missing = hook.inspect(
        "Scenarios/Not Installed/Scenario", typeCode("PICT"), 32128, "abc");
    require(missing.resolution.kind == AssetResolutionKind::CoverageFailure,
        "missing post-selection key did not report a coverage failure");
    require(missing.resolution.diagnostic ==
            "No approved manifest coverage for Scenarios/Not Installed/Scenario:PICT:32128",
        "missing-key diagnostic is not deterministic");
    require(missing.immutablePayloadSha256 == payloadSha256Hex("abc"),
        "post-selection hook did not hash immutable source bytes");

    const auto mismatch = hook.inspect(
        "Scenarios/Tutorial/Scenario", typeCode("PICT"), 32128, "abc");
    require(mismatch.resolution.kind == AssetResolutionKind::CoverageFailure,
        "wrong selected source payload retained coverage");
    require(mismatch.resolution.diagnostic ==
            "Classic payload SHA-256 mismatch for Scenarios/Tutorial/Scenario:PICT:32128: "
            "expected 44a37fbd12719698c8457d109105b61b93304e259318aa969b6afc7a8cf02eef, "
            "got ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "selected-payload diagnostic is not deterministic");

    hook.setPresentationMode(PresentationMode::classic);
    const auto switched = hook.inspect(
        "Scenarios/Tutorial/Scenario", typeCode("PICT"), 32128, "abc");
    require(switched.resolution.kind == AssetResolutionKind::ClassicBypass,
        "mid-session switch back to Classic did not bypass manifest coverage");

    std::cout << "ResourceSelectionHookTest passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ResourceSelectionHookTest failed: " << error.what() << '\n';
    return 1;
  }
}
