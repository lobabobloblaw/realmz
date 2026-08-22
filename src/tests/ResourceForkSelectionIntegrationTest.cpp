#include "remaster/assets/ResourceSelectionHook.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <phosg/Filesystem.hh>
#include <resource_file/IndexFormats/Formats.hh>
#include <resource_file/ResourceFile.hh>
#include <resource_file/ResourceTypes.hh>

namespace {

using realmz::presentation::PresentationMode;
using realmz::remaster::assets::AssetManifest;
using realmz::remaster::assets::AssetResolutionKind;
using realmz::remaster::assets::ResourceSelectionHook;

struct PackFixture {
  const char* pack;
  const char* path;
  std::size_t expectedCount;
};

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
    ResourceSelectionHook hook(
        remasteredRoot / "scopes/phase1.placeholder-manifest.json",
        remasteredRoot / "scopes/phase1.census.json", remasteredRoot);
    hook.setPresentationMode(PresentationMode::remastered);

    constexpr std::array selectedTypes = {
        ResourceDASM::RESOURCE_TYPE_PICT,
        ResourceDASM::RESOURCE_TYPE_cicn,
        ResourceDASM::RESOURCE_TYPE_crsr,
        ResourceDASM::RESOURCE_TYPE_ppat,
    };
    constexpr std::array packs = {
        PackFixture{"Data Files/The Family Jewels",
            "base/Realmz/Data Files/The Family Jewels.rsrc", 1145},
        PackFixture{"Data Files/Portraits",
            "base/Realmz/Data Files/Portraits.rsrc", 120},
        PackFixture{"Data Files/Tacticals",
            "base/Realmz/Data Files/Tacticals.rsrc", 240},
        PackFixture{"Scenarios/Tutorial/Scenario",
            "base/Realmz/Scenarios/Tutorial/Scenario.rsrc", 11},
        PackFixture{"Scenarios/City of Bywater/Scenario",
            "base/Realmz/Scenarios/City of Bywater/Scenario.rsrc", 4},
    };

    std::size_t total = 0;
    for (const auto& pack : packs) {
      const auto bytes = phosg::load_file(repositoryRoot / pack.path);
      const auto resourceFile = ResourceDASM::parse_resource_fork(bytes);
      std::size_t packCount = 0;
      for (const auto type : selectedTypes) {
        for (const auto id : resourceFile.all_resources_of_type(type)) {
          const auto source = resourceFile.get_resource(type, id);
          const auto selected = hook.inspect(pack.pack, type, id, source->data);
          require(selected.resolution.kind ==
                  AssetResolutionKind::ClassicPassthrough,
              "live placeholder selection failed: " +
                  selected.resolution.diagnostic);
          ++packCount;
        }
      }
      require(packCount == pack.expectedCount,
          std::string("selected resource count changed for ") + pack.pack);
      total += packCount;
    }

    require(total == 1520,
        "live resource-fork selection no longer covers all 1,520 phase-one keys");

    ResourceSelectionHook runtimeHook(
        remasteredRoot / "scopes/phase1.runtime-manifest.json",
        remasteredRoot / "scopes/phase1.census.json", remasteredRoot);
    const auto runtimeManifest = AssetManifest::load(
        remasteredRoot / "scopes/phase1.runtime-manifest.json",
        remasteredRoot / "scopes/phase1.census.json", remasteredRoot);
    runtimeHook.setPresentationMode(PresentationMode::remastered);
    std::set<std::string> overrides;
    std::size_t passthroughs = 0;
    for (const auto& pack : packs) {
      const auto bytes = phosg::load_file(repositoryRoot / pack.path);
      const auto resourceFile = ResourceDASM::parse_resource_fork(bytes);
      for (const auto type : selectedTypes) {
        for (const auto id : resourceFile.all_resources_of_type(type)) {
          const auto source = resourceFile.get_resource(type, id);
          const auto selected = runtimeHook.inspect(pack.pack, type, id, source->data);
          if (selected.resolution.kind == AssetResolutionKind::Override) {
            require(selected.resolution.logicalDimensions.has_value(),
                "runtime override lost its logical dimensions");
            const auto* manifestEntry = runtimeManifest.find(selected.key);
            require(manifestEntry != nullptr &&
                    selected.resolution.approvedContentSha256 ==
                        manifestEntry->sharedMasterSha256,
                "runtime override lost its exact approved content digest");
            overrides.emplace(selected.key.toString());
          } else {
            require(selected.resolution.kind == AssetResolutionKind::ClassicPassthrough,
                "runtime manifest produced a coverage failure: " +
                    selected.resolution.diagnostic);
            require(!selected.resolution.approvedContentSha256,
                "Classic passthrough carried an approved content digest");
            ++passthroughs;
          }
        }
      }
    }
    const std::set<std::string> expectedOverrides = {
        "Data Files/Portraits:cicn:257",
        "Data Files/Portraits:cicn:267",
        "Data Files/Portraits:cicn:297",
        "Data Files/Portraits:cicn:337",
        "Data Files/The Family Jewels:PICT:50",
        "Data Files/The Family Jewels:cicn:-167",
        "Data Files/The Family Jewels:ppat:128",
        "Data Files/The Family Jewels:ppat:129",
        "Data Files/The Family Jewels:ppat:130",
        "Data Files/The Family Jewels:ppat:131",
        "Scenarios/Tutorial/Scenario:PICT:32128",
    };
    require(overrides == expectedOverrides,
        "runtime override ResourceKey set does not match human approvals");
    require(passthroughs == 1509,
        "runtime manifest no longer has exactly 1,509 Classic passthroughs");

    std::cout << "ResourceForkSelectionIntegrationTest passed: 1,520 immutable payloads, "
              << overrides.size() << " overrides, " << passthroughs << " passthroughs\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ResourceForkSelectionIntegrationTest failed: " << error.what() << '\n';
    return 1;
  }
}
