#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "presentation/GameSnapshot.hpp"
#include "presentation/LegacyCommandBridge.hpp"
#include "presentation/PresentationHost.hpp"
#include "presentation/PresentationMode.hpp"
#include "presentation/UIAction.hpp"
#include "remaster/assets/AssetResolver.hpp"

using namespace realmz::presentation;
using namespace realmz::remaster::assets;

namespace {

int checksRun = 0;

void check(bool condition, std::string_view expression, int line) {
  ++checksRun;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " +
        std::string(expression));
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

template <typename T>
concept CarriesPresentationMode = requires(T value) {
  value.presentation_mode;
};

static_assert(!CarriesPresentationMode<GameSnapshot>,
    "presentation mode must remain outside the engine snapshot DTO");
static_assert(std::is_copy_constructible_v<GameSnapshot>);
static_assert(std::is_same_v<decltype(SetPresentationModeAction::mode), PresentationMode>);
static_assert(std::is_same_v<
    decltype(SetDrawerPanelAction::panel),
    std::optional<DrawerPanel>>);
static_assert(std::is_same_v<
    decltype(OpenInventoryAction::member),
    PartyMemberId>);
static_assert(std::is_same_v<
    decltype(OpenSpellbookAction::member),
    PartyMemberId>);
static_assert(std::is_same_v<
    decltype(DelayCombatantAction::combatant),
    CombatantId>);

GameSnapshot makeCompleteSnapshot() {
  GameSnapshot snapshot{
      .revision = 0x1020304050607080ULL,
      .screen = ScreenContext::combat,
      .scenario_id = 7,
  };
  snapshot.party.members = {
      PartyMemberView{
          .id = 1,
          .name = "Ariella",
          .level = 9,
          .race_id = 2,
          .caste_id = 3,
          .portrait_id = 257,
          .tactical_id = 512,
          .stamina = {31, 36},
          .spell_points = {14, 19},
          .armor_class = 8,
          .movement = 5,
          .movement_maximum = 7,
          .conditions = {2, 9},
          .selected = true,
          .conscious = true,
      },
      PartyMemberView{
          .id = 2,
          .name = "Myr",
          .level = 8,
          .race_id = 1,
          .caste_id = 4,
          .portrait_id = 258,
          .tactical_id = 513,
          .stamina = {0, 29},
          .spell_points = {7, 11},
          .armor_class = 6,
          .movement = 0,
          .movement_maximum = 6,
          .conditions = {4},
          .selected = false,
          .conscious = false,
      },
  };
  snapshot.party.selected_member = 1;
  snapshot.party.pooled_money = {12, 345, 6789};
  snapshot.party.fatigue = 17;

  snapshot.world = WorldView{
      .presentation = WorldPresentation::dungeon_map,
      .party_x = 42,
      .party_y = 19,
      .land_level = 3,
      .dungeon_level = 2,
      .facing = Facing::west,
      .visible_columns = 2,
      .visible_rows = 2,
      .visible_tiles = {
          WorldTileView{11, 0, true, true, false},
          WorldTileView{12, 4, true, true, true},
          WorldTileView{13, 0, false, true, false},
          WorldTileView{14, 8, false, false, true},
      },
  };
  snapshot.combat = CombatView{
      .active = true,
      .round = 6,
      .visible_columns = 15,
      .visible_rows = 13,
      .acting_combatant = 1001,
      .combatants = {
          CombatantView{
              .id = 1001,
              .kind = CombatantKind::party_member,
              .name = "Ariella",
              .cell_x = 4,
              .cell_y = 9,
              .stamina = {31, 36},
              .conditions = {2},
              .active = true,
              .targetable = false,
          },
          CombatantView{
              .id = -44,
              .kind = CombatantKind::monster,
              .name = "Goblin",
              .cell_x = 8,
              .cell_y = 4,
              .stamina = {5, 12},
              .conditions = {},
              .active = false,
              .targetable = true,
          },
      },
  };
  snapshot.inventory = InventoryView{
      .owner = 1,
      .items = {
          InventoryItemView{
              .instance_id = 0xCAFEU,
              .item_id = 89,
              .name = "Silver Dagger",
              .slot = 2,
              .quantity = 1,
              .charges = 0,
              .equipped = true,
              .identified = true,
              .cursed = false,
              .usable = true,
          },
      },
      .carried_weight = 73,
      .maximum_weight = 125,
  };
  snapshot.encounter = EncounterView{
      .active = true,
      .encounter_id = 77,
      .prompt = "Parley with the guard?",
      .choices = {
          EncounterChoiceView{1, "Parley", true},
          EncounterChoiceView{2, "Leave", true},
      },
      .can_cancel = true,
  };
  return snapshot;
}

class GuardedLegacyRuntime final : public GameSnapshotSource {
public:
  GuardedLegacyRuntime()
      : snapshot_(makeCompleteSnapshot()),
        saveFacingBytes_{
            0x52, 0x45, 0x41, 0x4C, 0x4D, 0x5A, 0x00, 0x01,
            0x10, 0x20, 0x30, 0x40, 0x80, 0xFF, 0x7F, 0x55,
        } {}

  [[nodiscard]] GameSnapshot capture() const override {
    return snapshot_;
  }

  [[nodiscard]] const std::array<std::uint8_t, 16>& saveFacingBytes() const noexcept {
    return saveFacingBytes_;
  }

  [[nodiscard]] int engineMutationCount() const noexcept {
    return engineMutationCount_;
  }

  void simulateEngineMutation() {
    ++engineMutationCount_;
    ++snapshot_.revision;
    ++snapshot_.party.fatigue;
    saveFacingBytes_[8] ^= 0xFFU;
  }

private:
  GameSnapshot snapshot_;
  std::array<std::uint8_t, 16> saveFacingBytes_;
  int engineMutationCount_ = 0;
};

class PresentationStateFixture {
public:
  [[nodiscard]] DispatchResult apply(const SetPresentationModeAction& action) {
    if (action.mode == host_.mode()) {
      return DispatchResult::handled();
    }
    host_.set_mode(action.mode);
    ++recompositions_;
    ++preferenceWrites_;
    return DispatchResult::handled();
  }

  [[nodiscard]] PresentationMode mode() const noexcept {
    return host_.mode();
  }

  [[nodiscard]] int recompositions() const noexcept {
    return recompositions_;
  }

  [[nodiscard]] int preferenceWrites() const noexcept {
    return preferenceWrites_;
  }

private:
  PresentationHost host_;
  int recompositions_ = 0;
  int preferenceWrites_ = 0;
};

void testModeSwitchIsOutsideEngineAndSaveState() {
  GuardedLegacyRuntime engine;
  PresentationStateFixture presentation;

  LegacyActionHandlers handlers;
  const auto mutateEngine = [&engine](const auto&) {
    engine.simulateEngineMutation();
    return DispatchResult::handled();
  };
  handlers.move_party = mutateEngine;
  handlers.select_party_member = mutateEngine;
  handlers.open_inventory = mutateEngine;
  handlers.open_spellbook = mutateEngine;
  handlers.open_save_game = mutateEngine;
  handlers.open_load_game = mutateEngine;
  handlers.guard_combatant = mutateEngine;
  handlers.finish_combatant = mutateEngine;
  handlers.delay_combatant = mutateEngine;
  handlers.inventory = mutateEngine;
  handlers.cast_spell = mutateEngine;
  handlers.trade = mutateEngine;
  handlers.save_game = mutateEngine;
  handlers.load_game = mutateEngine;
  handlers.confirm = mutateEngine;
  handlers.cancel = mutateEngine;
  handlers.set_presentation_mode = [&presentation](
      const SetPresentationModeAction& action) {
    return presentation.apply(action);
  };
  InjectedLegacyCommandBridge bridge(std::move(handlers));

  const GameSnapshot baselineSnapshot = engine.capture();
  const auto baselineSaveBytes = engine.saveFacingBytes();
  CHECK(baselineSnapshot.world.has_complete_tile_grid());

  constexpr std::array modes{
      PresentationMode::remastered,
      PresentationMode::classic,
      PresentationMode::remastered,
      PresentationMode::classic,
  };
  ActionSequence sequence = 1;
  for (int pass = 0; pass < 32; ++pass) {
    for (const auto mode : modes) {
      const UIAction action{
          .sequence = sequence++,
          .payload = SetPresentationModeAction{mode},
      };
      CHECK(action_name(action.payload) == "set_presentation_mode");
      const auto result = bridge.dispatch(action);
      CHECK(result.status == DispatchStatus::handled);
      CHECK(presentation.mode() == mode);
      CHECK(engine.capture() == baselineSnapshot);
      CHECK(engine.capture().revision == baselineSnapshot.revision);
      CHECK(engine.saveFacingBytes() == baselineSaveBytes);
      CHECK(engine.engineMutationCount() == 0);
    }
  }
  CHECK(presentation.recompositions() == 128);
  CHECK(presentation.preferenceWrites() == 128);

  const auto idempotent = bridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = SetPresentationModeAction{PresentationMode::classic},
  });
  CHECK(idempotent.was_handled());
  CHECK(presentation.recompositions() == 128);
  CHECK(presentation.preferenceWrites() == 128);
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  InjectedLegacyCommandBridge unsupportedBridge(LegacyActionHandlers{});
  const auto unsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = SetPresentationModeAction{PresentationMode::remastered},
  });
  CHECK(unsupported.status == DispatchStatus::unsupported);
  CHECK(unsupported.detail ==
      "No legacy handler registered for set_presentation_mode");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  const auto openInventoryUnsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenInventoryAction{1},
  });
  CHECK(openInventoryUnsupported.status == DispatchStatus::unsupported);
  CHECK(openInventoryUnsupported.detail ==
      "No legacy handler registered for open_inventory");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  const auto openSpellbookUnsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenSpellbookAction{1},
  });
  CHECK(openSpellbookUnsupported.status == DispatchStatus::unsupported);
  CHECK(openSpellbookUnsupported.detail ==
      "No legacy handler registered for open_spellbook");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  const auto openSaveUnsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenSaveGameAction{},
  });
  CHECK(openSaveUnsupported.status == DispatchStatus::unsupported);
  CHECK(openSaveUnsupported.detail ==
      "No legacy handler registered for open_save_game");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  const auto openLoadUnsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = OpenLoadGameAction{},
  });
  CHECK(openLoadUnsupported.status == DispatchStatus::unsupported);
  CHECK(openLoadUnsupported.detail ==
      "No legacy handler registered for open_load_game");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  const auto guardCombatantUnsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = GuardCombatantAction{1},
  });
  CHECK(guardCombatantUnsupported.status == DispatchStatus::unsupported);
  CHECK(guardCombatantUnsupported.detail ==
      "No legacy handler registered for guard_combatant");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  const auto finishCombatantUnsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = FinishCombatantAction{1},
  });
  CHECK(finishCombatantUnsupported.status == DispatchStatus::unsupported);
  CHECK(finishCombatantUnsupported.detail ==
      "No legacy handler registered for finish_combatant");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  const auto delayCombatantUnsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = DelayCombatantAction{1},
  });
  CHECK(delayCombatantUnsupported.status == DispatchStatus::unsupported);
  CHECK(delayCombatantUnsupported.detail ==
      "No legacy handler registered for delay_combatant");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  const auto localDrawerUnsupported = unsupportedBridge.dispatch(UIAction{
      .sequence = sequence++,
      .payload = SetDrawerPanelAction{DrawerPanel::details},
  });
  CHECK(localDrawerUnsupported.status == DispatchStatus::unsupported);
  CHECK(localDrawerUnsupported.detail ==
      "Presentation-local set_drawer_panel cannot cross the legacy bridge");
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);

  LegacyActionHandlers failingHandlers;
  failingHandlers.set_presentation_mode = [](const SetPresentationModeAction&) ->
      DispatchResult {
    throw std::runtime_error("presentation fixture failure");
  };
  InjectedLegacyCommandBridge failingBridge(std::move(failingHandlers));
  const auto failed = failingBridge.dispatch(UIAction{
      .sequence = sequence,
      .payload = SetPresentationModeAction{PresentationMode::remastered},
  });
  CHECK(failed.status == DispatchStatus::failed);
  CHECK(failed.detail.find("presentation fixture failure") != std::string::npos);
  CHECK(engine.capture() == baselineSnapshot);
  CHECK(engine.saveFacingBytes() == baselineSaveBytes);
}

void checkSameResolution(
    const AssetResolution& left,
    const AssetResolution& right) {
  CHECK(left.kind == right.kind);
  CHECK(left.overridePath == right.overridePath);
  CHECK(left.masterKey == right.masterKey);
  CHECK(left.diagnostic == right.diagnostic);
}

AssetResolver loadPhaseOneResolver(const std::filesystem::path& repositoryRoot) {
  const auto assetRoot = repositoryRoot / "assets/remastered";
  return AssetResolver(AssetManifest::load(
      assetRoot / "scopes/phase1.placeholder-manifest.json",
      assetRoot / "scopes/phase1.census.json",
      assetRoot));
}

void testCoverageFailuresAreDeterministic(
    const std::filesystem::path& repositoryRoot) {
  auto firstResolver = loadPhaseOneResolver(repositoryRoot);
  auto secondResolver = loadPhaseOneResolver(repositoryRoot);
  CHECK(firstResolver.manifest().size() == 1520);
  CHECK(secondResolver.manifest().size() == 1520);

  const std::vector<ResourceKey> missingKeys{
      {"Scenarios/Not Installed/Scenario", "PICT", 32128},
      {"Data Files/Portraits", "cicn", 0},
      {"Scenarios/Tutorial/Scenario", "PICT", -32768},
      {"Scenarios/Tutorial/Scenario", "PICT", 32767},
      {"Scenarios/City of Bywater/Scenario", "PICT", -32768},
  };

  std::vector<std::string> diagnostics;
  diagnostics.reserve(missingKeys.size());
  for (const auto& key : missingKeys) {
    const auto expectedDiagnostic =
        "No approved manifest coverage for " + key.toString();
    const auto first = firstResolver.resolve(PresentationMode::remastered, key);
    CHECK(first.kind == AssetResolutionKind::CoverageFailure);
    CHECK(!first.covered());
    CHECK(!first.overridePath.has_value());
    CHECK(!first.masterKey.has_value());
    CHECK(first.diagnostic == expectedDiagnostic);
    diagnostics.emplace_back(first.diagnostic);

    for (int repeat = 0; repeat < 16; ++repeat) {
      const auto repeated = firstResolver.resolve(
          PresentationMode::remastered, key);
      checkSameResolution(first, repeated);
    }
    const auto independent = secondResolver.resolve(
        PresentationMode::remastered, key);
    checkSameResolution(first, independent);

    const auto classic = firstResolver.resolve(PresentationMode::classic, key);
    CHECK(classic.kind == AssetResolutionKind::ClassicBypass);
    CHECK(classic.covered());
    CHECK(!classic.overridePath.has_value());
    CHECK(!classic.masterKey.has_value());
    CHECK(classic.diagnostic ==
        "Classic presentation bypasses remastered overrides");

    const auto afterClassic = firstResolver.resolve(
        PresentationMode::remastered, key);
    checkSameResolution(first, afterClassic);
  }
  CHECK(diagnostics.size() == missingKeys.size());
  CHECK(diagnostics[2] != diagnostics[4]);
  CHECK(diagnostics[2].find("Scenarios/Tutorial/Scenario") != std::string::npos);
  CHECK(diagnostics[4].find("Scenarios/City of Bywater/Scenario") !=
      std::string::npos);

  const ResourceKey tutorialCovered{
      "Scenarios/Tutorial/Scenario", "PICT", 32128};
  const ResourceKey cityCovered{
      "Scenarios/City of Bywater/Scenario", "PICT", 32128};
  CHECK(firstResolver.resolve(PresentationMode::remastered, tutorialCovered).kind ==
      AssetResolutionKind::ClassicPassthrough);
  CHECK(firstResolver.resolve(PresentationMode::remastered, cityCovered).kind ==
      AssetResolutionKind::ClassicPassthrough);
  CHECK(firstResolver.manifest().size() == 1520);
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto repositoryRoot = std::filesystem::weakly_canonical(
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("."));
    testModeSwitchIsOutsideEngineAndSaveState();
    testCoverageFailuresAreDeterministic(repositoryRoot);
    std::cout << "RuntimePresentationContractTest passed (" << checksRun
              << " checks; 128 mode switches; 5 deterministic failures)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "RuntimePresentationContractTest failed after " << checksRun
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
