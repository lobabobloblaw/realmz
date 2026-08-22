#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "presentation/GameplayChromeCoverage.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

using Surface = GameplayChromeSurface;
using Kind = GameplayChromeRoleKind;
using Status = GameplayChromeCoverageStatus;
using Entry = GameplayChromeCoverageEntry;
using Issue = GameplayChromeInventoryIssue;
using Activity = GameplayChromeActivityState;
using Variant = GameplayChromeContextVariant;

constexpr std::array<Surface, 3> kSurfaces{
    Surface::exploration,
    Surface::dungeon,
    Surface::combat,
};

constexpr std::array<Kind, 2> kKinds{
    Kind::interaction,
    Kind::essential_information,
};

struct ExpectedManifestRow {
  std::string_view stable_id;
  Surface surface;
  Kind kind;
  Status status;
};

// This is intentionally independent of the production manifest: a row added,
// removed, reordered, reclassified, or optimistically marked covered must be
// reviewed here as an explicit inventory-policy change.
constexpr auto kExpectedManifestRows = std::to_array<ExpectedManifestRow>({
    {"exploration.viewport.pointer_navigation", Surface::exploration,
        Kind::interaction, Status::retained_in_crop},
    {"exploration.action.eight_direction_movement", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.select_party_member", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.open_inventory", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.open_spellbook", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.save_game", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.load_game", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.character_sheet", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.rest", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.camp", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.search_toggle", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.use_torch", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.contextual_overview", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.selected_item_drilldown", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.contextual_shop_temple_encounter",
        Surface::exploration, Kind::interaction, Status::semantic_complete},
    {"exploration.action.pool_money", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.trade", Surface::exploration,
        Kind::interaction, Status::missing},
    {"exploration.action.heal", Surface::exploration,
        Kind::interaction, Status::missing},
    {"exploration.action.use_scroll", Surface::exploration,
        Kind::interaction, Status::semantic_complete},
    {"exploration.action.show_conditions", Surface::exploration,
        Kind::interaction, Status::missing},
    {"exploration.action.party_auto_toggles", Surface::exploration,
        Kind::interaction, Status::missing},
    {"exploration.info.world_view", Surface::exploration,
        Kind::essential_information, Status::retained_in_crop},
    {"exploration.info.party_vitals", Surface::exploration,
        Kind::essential_information, Status::semantic_complete},
    {"exploration.info.selected_member_details", Surface::exploration,
        Kind::essential_information, Status::semantic_complete},
    {"exploration.info.party_condition_indicators", Surface::exploration,
        Kind::essential_information, Status::semantic_complete},
    {"exploration.info.narrative_messages", Surface::exploration,
        Kind::essential_information, Status::missing},
    {"exploration.info.world_coordinates", Surface::exploration,
        Kind::essential_information, Status::missing},
    {"exploration.info.calendar_clock", Surface::exploration,
        Kind::essential_information, Status::missing},
    {"exploration.info.fatigue", Surface::exploration,
        Kind::essential_information, Status::semantic_complete},
    {"exploration.info.pooled_money", Surface::exploration,
        Kind::essential_information, Status::semantic_complete},
    {"exploration.info.search_and_torch_state", Surface::exploration,
        Kind::essential_information, Status::missing},

    {"dungeon.viewport.pointer_navigation", Surface::dungeon,
        Kind::interaction, Status::retained_in_crop},
    {"dungeon.action.relative_movement", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.select_party_member", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.open_inventory", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.open_spellbook", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.save_game", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.load_game", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.character_sheet", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.rest", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.camp", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.search_toggle", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.use_torch", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.contextual_overview", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.selected_item_drilldown", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.contextual_shop_temple_encounter", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.pool_money", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.trade", Surface::dungeon,
        Kind::interaction, Status::missing},
    {"dungeon.action.heal", Surface::dungeon,
        Kind::interaction, Status::missing},
    {"dungeon.action.use_scroll", Surface::dungeon,
        Kind::interaction, Status::semantic_complete},
    {"dungeon.action.show_conditions", Surface::dungeon,
        Kind::interaction, Status::missing},
    {"dungeon.action.party_auto_toggles", Surface::dungeon,
        Kind::interaction, Status::missing},
    {"dungeon.info.world_view", Surface::dungeon,
        Kind::essential_information, Status::retained_in_crop},
    {"dungeon.info.party_vitals", Surface::dungeon,
        Kind::essential_information, Status::semantic_complete},
    {"dungeon.info.selected_member_details", Surface::dungeon,
        Kind::essential_information, Status::semantic_complete},
    {"dungeon.info.party_condition_indicators", Surface::dungeon,
        Kind::essential_information, Status::semantic_complete},
    {"dungeon.info.narrative_messages", Surface::dungeon,
        Kind::essential_information, Status::missing},
    {"dungeon.info.world_coordinates", Surface::dungeon,
        Kind::essential_information, Status::missing},
    {"dungeon.info.calendar_clock", Surface::dungeon,
        Kind::essential_information, Status::missing},
    {"dungeon.info.fatigue", Surface::dungeon,
        Kind::essential_information, Status::semantic_complete},
    {"dungeon.info.pooled_money", Surface::dungeon,
        Kind::essential_information, Status::semantic_complete},
    {"dungeon.info.search_and_torch_state", Surface::dungeon,
        Kind::essential_information, Status::missing},

    {"combat.viewport.pointer_actions", Surface::combat,
        Kind::interaction, Status::retained_in_crop},
    {"combat.action.guard", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.finish", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.delay", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.center_active", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.switch_weapon", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.cycle_focus", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.open_items", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.auto_current", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.show_range", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.bandage", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.undo", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.cast_spell", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.target", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.escape", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.use_scroll", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.center_cursor", Surface::combat,
        Kind::interaction, Status::semantic_complete},
    {"combat.action.inspect_focused_combatant", Surface::combat,
        Kind::interaction, Status::missing},
    {"combat.action.inspect_party_member", Surface::combat,
        Kind::interaction, Status::missing},
    {"combat.action.inspect_items", Surface::combat,
        Kind::interaction, Status::missing},
    {"combat.action.inspect_conditions", Surface::combat,
        Kind::interaction, Status::missing},
    {"combat.action.inspect_attacks", Surface::combat,
        Kind::interaction, Status::missing},
    {"combat.action.turn_undead", Surface::combat,
        Kind::interaction, Status::missing},
    {"combat.action.party_auto_toggles", Surface::combat,
        Kind::interaction, Status::missing},
    {"combat.info.battlefield", Surface::combat,
        Kind::essential_information, Status::retained_in_crop},
    {"combat.info.party_vitals", Surface::combat,
        Kind::essential_information, Status::semantic_complete},
    {"combat.info.selected_member_details", Surface::combat,
        Kind::essential_information, Status::semantic_complete},
    {"combat.info.party_condition_indicators", Surface::combat,
        Kind::essential_information, Status::semantic_complete},
    {"combat.info.narrative_messages", Surface::combat,
        Kind::essential_information, Status::missing},
    {"combat.info.inspected_combatant", Surface::combat,
        Kind::essential_information, Status::missing},
    {"combat.info.conditions_and_attacks", Surface::combat,
        Kind::essential_information, Status::missing},
    {"combat.info.round", Surface::combat,
        Kind::essential_information, Status::missing},
    {"combat.info.enemies_remaining", Surface::combat,
        Kind::essential_information, Status::missing},
});

static_assert(kExpectedManifestRows.size() == 95U);

std::vector<Entry> minimal_structurally_valid_inventory() {
  return {
      {"fixture.exploration.interaction", Surface::exploration,
          Kind::interaction, Status::semantic_complete,
          "fixture evidence", "fixture.cpp::exploration_interaction"},
      {"fixture.exploration.information", Surface::exploration,
          Kind::essential_information, Status::semantic_complete,
          "fixture evidence", "fixture.cpp::exploration_information"},
      {"fixture.dungeon.interaction", Surface::dungeon,
          Kind::interaction, Status::semantic_complete,
          "fixture evidence", "fixture.cpp::dungeon_interaction"},
      {"fixture.dungeon.information", Surface::dungeon,
          Kind::essential_information, Status::semantic_complete,
          "fixture evidence", "fixture.cpp::dungeon_information"},
      {"fixture.combat.interaction", Surface::combat,
          Kind::interaction, Status::semantic_complete,
          "fixture evidence", "fixture.cpp::combat_interaction"},
      {"fixture.combat.information", Surface::combat,
          Kind::essential_information, Status::semantic_complete,
          "fixture evidence", "fixture.cpp::combat_information"},
  };
}

std::vector<Entry> covered_canonical_inventory() {
  const auto manifest = gameplay_chrome_coverage_manifest();
  std::vector<Entry> result(manifest.begin(), manifest.end());
  for (auto& entry : result) {
    entry.status = Status::semantic_complete;
  }
  return result;
}

Variant standard_variant(Surface surface) {
  const auto context = screen_context_for_gameplay_chrome_surface(surface);
  CHECK(context.has_value());

  WorldPresentation presentation = WorldPresentation::outdoor;
  std::optional<CombatantId> acting_combatant;
  if (surface == Surface::dungeon) {
    presentation = WorldPresentation::dungeon_first_person;
  } else if (surface == Surface::combat) {
    acting_combatant = CombatantId{0};
  }

  return {
      .surface = surface,
      .screen = *context,
      .world_presentation = presentation,
      .camp_state = Activity::inactive,
      .nested_modal_state = Activity::inactive,
      .spell_state = Activity::inactive,
      .targeting_state = Activity::inactive,
      .text_entry_state = Activity::inactive,
      .selected_member_known = true,
      .selected_member = PartyMemberId{0},
      .acting_combatant_known = true,
      .acting_combatant = acting_combatant,
  };
}

GameplayCropRuntimePrerequisites ready_runtime(Surface surface) {
  const auto variant = standard_variant(surface);
  return {
      .expected_variant = variant,
      .live_variant = variant,
      .snapshot_context = *variant.screen,
      .shell_model_context = *variant.screen,
      .gameplay_window_active = true,
      .front_is_gameplay_surface = true,
      .legacy_requires_full_frame = false,
      .expected_inventory_revision = kGameplayChromeInventoryRevision,
      .snapshot_revision = 42U,
      .shell_model_revision = 42U,
      .shell_model_valid = true,
      .shell_layout_valid = true,
      .shell_font_valid = true,
      .expected_controls_present = true,
      .live_handlers_present = true,
      .informational_surfaces_complete = true,
  };
}

const Entry* find_manifest_entry(
    std::span<const Entry> manifest,
    std::string_view stable_id) {
  for (const auto& entry : manifest) {
    if (entry.stable_id == stable_id) {
      return &entry;
    }
  }
  return nullptr;
}

void expect_manifest_entry(
    std::span<const Entry> manifest,
    std::string_view stable_id,
    Surface surface,
    Kind kind,
    Status status) {
  const auto* entry = find_manifest_entry(manifest, stable_id);
  CHECK(entry != nullptr);
  CHECK(entry->surface == surface);
  CHECK(entry->kind == kind);
  CHECK(entry->status == status);
}

bool path_is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
  auto root_part = root.begin();
  auto candidate_part = candidate.begin();
  for (; root_part != root.end(); ++root_part, ++candidate_part) {
    if ((candidate_part == candidate.end()) ||
        (*candidate_part != *root_part)) {
      return false;
    }
  }
  return true;
}

std::string read_source_file(const std::filesystem::path& source_path) {
  std::ifstream source(source_path, std::ios::binary);
  CHECK(source.is_open());
  return {
      std::istreambuf_iterator<char>(source),
      std::istreambuf_iterator<char>(),
  };
}

void test_manifest_matches_independent_oracle() {
  const auto manifest = gameplay_chrome_coverage_manifest();
  CHECK(manifest.size() == kExpectedManifestRows.size());
  for (size_t index = 0; index < kExpectedManifestRows.size(); ++index) {
    const auto& actual = manifest[index];
    const auto& expected = kExpectedManifestRows[index];
    CHECK(actual.stable_id == expected.stable_id);
    CHECK(actual.surface == expected.surface);
    CHECK(actual.kind == expected.kind);
    CHECK(actual.status == expected.status);
  }

  for (const auto& [stable_id, surface] : std::array{
           std::pair{"exploration.action.use_torch", Surface::exploration},
           std::pair{"dungeon.action.use_torch", Surface::dungeon},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::interaction, Status::semantic_complete);
    const auto* torch = find_manifest_entry(manifest, stable_id);
    CHECK(torch != nullptr);
    CHECK(torch->evidence.find("one-shot Torch command") !=
        std::string_view::npos);
    CHECK(torch->evidence.find("0x5754") != std::string_view::npos);
    CHECK(torch->evidence.find("member/slot locator") !=
        std::string_view::npos);
    CHECK(torch->evidence.find(
        "freshness token rather than a stable item identity") !=
        std::string_view::npos);
    CHECK(torch->evidence.find("late validation requires that same source") !=
        std::string_view::npos);
    CHECK(torch->evidence.find("live real Classic `torch` control") !=
        std::string_view::npos);
    CHECK(torch->evidence.find("neutral `app1Evt`") !=
        std::string_view::npos);
    CHECK(torch->evidence.find("No key, pointer") !=
        std::string_view::npos);
    CHECK(torch->evidence.find("direct inventory or Light mutation") !=
        std::string_view::npos);
    CHECK(torch->evidence.find("`timeclick`") !=
        std::string_view::npos);
    CHECK(torch->evidence.find(
        "Classic `buttonchoice` alone owns charge consumption") !=
        std::string_view::npos);
    CHECK(torch->evidence.find("RNG, sound, Light duration, darkness") !=
        std::string_view::npos);
    CHECK(torch->source_anchor ==
        "src/presentation/SemanticInputBoundary.cpp::"
        "semantic_use_torch_tag/RealmzConsumeSemanticUseTorchEvent");
  }
  CHECK(find_manifest_entry(
      manifest, "exploration.action.torch_toggle") == nullptr);
  CHECK(find_manifest_entry(
      manifest, "dungeon.action.torch_toggle") == nullptr);

  for (const auto& [stable_id, surface] : std::array{
           std::pair{
               "exploration.action.contextual_overview",
               Surface::exploration},
           std::pair{
               "dungeon.action.contextual_overview",
               Surface::dungeon},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::interaction, Status::semantic_complete);
    const auto* overview = find_manifest_entry(manifest, stable_id);
    CHECK(overview != nullptr);
    CHECK(overview->evidence.find("discriminated ContextualOverviewAction") !=
        std::string_view::npos);
    CHECK(overview->evidence.find("Area Search outside camp") !=
        std::string_view::npos);
    CHECK(overview->evidence.find("selected-member Make Scroll") !=
        std::string_view::npos);
    CHECK(overview->evidence.find("0x574F") != std::string_view::npos);
    CHECK(overview->evidence.find("explicit mode") != std::string_view::npos);
    CHECK(overview->evidence.find("absent-or-bounded member") !=
        std::string_view::npos);
    CHECK(overview->evidence.find("0x00000061") != std::string_view::npos);
    CHECK(overview->evidence.find("0x0000286B") != std::string_view::npos);
    CHECK(overview->evidence.find("only Area Search is rejected") !=
        std::string_view::npos);
    CHECK(overview->evidence.find("non-pumping cached SDL/Classic") !=
        std::string_view::npos);
    CHECK(overview->evidence.find("No Classic source or replay vocabulary") !=
        std::string_view::npos);
    CHECK(overview->evidence.find("Classic retains the contextual control") !=
        std::string_view::npos);
    CHECK(overview->evidence.find("complete scroll modal") !=
        std::string_view::npos);
    CHECK(overview->source_anchor ==
        "src/presentation/SemanticInputBoundary.cpp::"
        "semantic_contextual_overview_tag/"
        "RealmzConsumeSemanticContextualOverviewEvent");
  }

  expect_manifest_entry(manifest,
      "combat.action.inspect_focused_combatant",
      Surface::combat, Kind::interaction, Status::missing);
  expect_manifest_entry(manifest,
      "combat.action.inspect_party_member",
      Surface::combat, Kind::interaction, Status::missing);
  CHECK(find_manifest_entry(
      manifest, "combat.action.inspect_combatant") == nullptr);

  for (const auto& [stable_id, surface, context_evidence] : std::array{
           std::tuple{
               "exploration.info.party_vitals",
               Surface::exploration,
               "during outdoor exploration"},
           std::tuple{
               "dungeon.info.party_vitals",
               Surface::dungeon,
               "during dungeon map and first-person play"},
           std::tuple{
               "combat.info.party_vitals",
               Surface::combat,
               "during combat"},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::essential_information, Status::semantic_complete);
    const auto* vitals = find_manifest_entry(manifest, stable_id);
    CHECK(vitals != nullptr);
    for (const auto evidence : {
             "shared code-native party rail",
             context_evidence,
             "row 1 name, Lv, and AC",
             "row 2 stamina plus SP",
             "solely by nonzero Classic spellpointsmax",
             "including zero current SP",
             "ATK cadence for a noncaster",
             "row 3 state summary",
             "ac, spellpoints/max, normattacks, attackbonus, and conditions",
             "raw normattacks + attackbonus",
             "Speedy condition 23 doubles first",
             "Slow condition 6 integer-halves",
             "0..19 render as reduced half fractions",
             "every other value renders `> 10`",
             "Each layout retains complete unelided semantic accessibility text",
             "no action, tag, input, Classic-source, or replay-vocabulary",
         }) {
      CHECK(vitals->evidence.find(evidence) != std::string_view::npos);
    }
    CHECK(vitals->source_anchor ==
        "src/presentation/PartyRailLayout.cpp::"
        "compute_party_rail_layout/layout_member");
  }

  for (const auto& [stable_id, surface, context_evidence] : std::array{
           std::tuple{
               "exploration.info.party_condition_indicators",
               Surface::exploration,
               "during outdoor exploration"},
           std::tuple{
               "dungeon.info.party_condition_indicators",
               Surface::dungeon,
               "during dungeon map and first-person play"},
           std::tuple{
               "combat.info.party_condition_indicators",
               Surface::combat,
               "during combat"},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::essential_information, Status::semantic_complete);
    const auto* effects = find_manifest_entry(manifest, stable_id);
    CHECK(effects != nullptr);
    for (const auto evidence : {
             "shared code-native PARTY STATUS ribbon",
             context_evidence,
             "signed partycondition indices 1 through 8",
             "excluding Torch index 0 and unused index 9",
             "every nonzero value",
             "negative Search or equipment sentinels",
             "Classic index order",
             "Waterworld, Dragon Hide, Discover Secret, Wizard Eye, Search, "
             "Free Fall / Levitate, Sentry, and Charm Resistance",
             "without describing raw values as durations",
             "every complete token and unelided semantic accessibility",
             "deterministic visible +N elision",
             "no OS-publication claim",
             "no action, tag, input, Classic-source, or replay-vocabulary",
         }) {
      CHECK(effects->evidence.find(evidence) != std::string_view::npos);
    }
    CHECK(effects->source_anchor ==
        "src/presentation/PartyRailLayout.cpp::"
        "compute_party_rail_layout");
  }

  for (const auto& [stable_id, surface] : std::array{
           std::pair{"exploration.info.fatigue", Surface::exploration},
           std::pair{"dungeon.info.fatigue", Surface::dungeon},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::essential_information, Status::semantic_complete);
    const auto* fatigue = find_manifest_entry(manifest, stable_id);
    CHECK(fatigue != nullptr);
    for (const auto evidence : {
             "exact signed fatigue as FAT raw/135",
             "bounded meter",
             "baseline through 70",
             "elevated from 71 through 105",
             "critical above 105",
             "two strict thresholds",
             "without normalizing the retained raw value",
             "absent from the combat ribbon",
             "without claiming OS publication",
             "no action, tag, input, Classic-source, or replay-vocabulary",
         }) {
      CHECK(fatigue->evidence.find(evidence) != std::string_view::npos);
    }
    CHECK(fatigue->source_anchor ==
        "src/presentation/PartyRailLayout.cpp::"
        "compute_party_rail_layout");
  }

  for (const auto& [stable_id, surface] : std::array{
           std::pair{
               "exploration.info.pooled_money", Surface::exploration},
           std::pair{"dungeon.info.pooled_money", Surface::dungeon},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::essential_information, Status::semantic_complete);
    const auto* money = find_manifest_entry(manifest, stable_id);
    CHECK(money != nullptr);
    for (const auto evidence : {
             "exact signed moneypool indices 0, 1, and 2",
             "Gold, Gems, Jewelry order",
             "no clamp, denomination conversion",
             "member or bank holdings",
             "absent from combat",
             "without claiming OS publication",
             "no action, tag, input, Classic-source, or replay-vocabulary",
         }) {
      CHECK(money->evidence.find(evidence) != std::string_view::npos);
    }
    CHECK(money->source_anchor ==
        "src/presentation/PartyRailLayout.cpp::"
        "compute_party_rail_layout");
  }

  for (const auto& [stable_id, surface] : std::array{
           std::pair{
               "combat.info.inspected_combatant", Surface::combat},
           std::pair{
               "combat.info.conditions_and_attacks", Surface::combat},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::essential_information, Status::missing);
  }

  for (const auto& [stable_id, surface] : std::array{
           std::pair{"exploration.action.camp", Surface::exploration},
           std::pair{"dungeon.action.camp", Surface::dungeon},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::interaction, Status::semantic_complete);
    const auto* camp = find_manifest_entry(manifest, stable_id);
    CHECK(camp != nullptr);
    CHECK(camp->evidence.find("explicit desired-state") !=
        std::string_view::npos);
    CHECK(camp->evidence.find("stale activations cannot reverse") !=
        std::string_view::npos);
    CHECK(camp->evidence.find("exact c keyDown") !=
        std::string_view::npos);
    CHECK(camp->evidence.find("0x00000863") != std::string_view::npos);
    CHECK(camp->evidence.find("inverted cancamp") !=
        std::string_view::npos);
    CHECK(camp->source_anchor ==
        "src/presentation/SemanticInputBoundary.cpp::"
        "semantic_set_camp_state_tag/RealmzConsumeSemanticSetCampStateEvent");
  }

  for (const auto& [stable_id, surface] : std::array{
           std::pair{"exploration.action.search_toggle", Surface::exploration},
           std::pair{"dungeon.action.search_toggle", Surface::dungeon},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::interaction, Status::semantic_complete);
    const auto* search = find_manifest_entry(manifest, stable_id);
    CHECK(search != nullptr);
    CHECK(search->evidence.find("absolute desired searching-state") !=
        std::string_view::npos);
    CHECK(search->evidence.find("0x5753") != std::string_view::npos);
    CHECK(search->evidence.find("strict Boolean") !=
        std::string_view::npos);
    CHECK(search->evidence.find("late-validated") !=
        std::string_view::npos);
    CHECK(search->evidence.find("live real Classic `search` control") !=
        std::string_view::npos);
    CHECK(search->evidence.find("any-nonzero") !=
        std::string_view::npos);
    CHECK(search->evidence.find("neutral `app1Evt` sideband") !=
        std::string_view::npos);
    CHECK(search->evidence.find(
        "no key, pointer, or direct state mutation is forged") !=
        std::string_view::npos);
    CHECK(search->evidence.find(
        "Classic `buttonchoice` retains sound, state, and icon ownership") !=
        std::string_view::npos);
    CHECK(search->evidence.find(
        "`checkforsecret` and time effects occur only in subsequent Classic behavior") !=
        std::string_view::npos);
    CHECK(search->source_anchor ==
        "src/presentation/SemanticInputBoundary.cpp::"
        "semantic_set_search_state_tag/RealmzConsumeSemanticSetSearchStateEvent");
  }

  for (const auto stable_id : {
           "exploration.info.search_and_torch_state",
           "dungeon.info.search_and_torch_state",
       }) {
    const auto* state = find_manifest_entry(manifest, stable_id);
    CHECK(state != nullptr);
    CHECK(state->status == Status::missing);
    CHECK(state->evidence.find("Torch state") != std::string_view::npos);
  }
}

void test_selected_item_drilldown_rows_are_semantically_complete() {
  const auto manifest = gameplay_chrome_coverage_manifest();
  for (const auto& [stable_id, surface, surface_evidence] : std::array{
           std::tuple{
               "exploration.action.selected_item_drilldown",
               Surface::exploration,
               "outdoor surface byte 0x01"},
           std::tuple{
               "dungeon.action.selected_item_drilldown",
               Surface::dungeon,
               "dungeon surface byte 0x02"},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::interaction, Status::semantic_complete);
    const auto* drilldown = find_manifest_entry(manifest, stable_id);
    CHECK(drilldown != nullptr);
    for (const auto evidence : {
             "PARTY deck exposes EQUIPMENT",
             "selected-member Classic quick equip/unequip popup",
             "distinct from full ITEMS",
             "0x5349SSMM",
             surface_evidence,
             "member byte 0x00..0x05",
             "completed semantic scope",
             "same freshly selected member",
             "neutral app1Evt with zero modifiers",
             "no key or pointer",
             "no held-mouse gate",
             "real showitembut",
             "Classic buttonchoice and showcondition own",
             "wear/removeitem mutation",
             "No replay vocabulary",
         }) {
      CHECK(drilldown->evidence.find(evidence) != std::string_view::npos);
    }
    CHECK(drilldown->source_anchor ==
        "src/presentation/SemanticInputBoundary.cpp::"
        "semantic_selected_item_drilldown_tag/"
        "RealmzConsumeSemanticSelectedItemDrilldownEvent");
  }
}

void test_contextual_world_entry_rows_are_semantically_complete() {
  const auto manifest = gameplay_chrome_coverage_manifest();
  for (const auto& [stable_id, surface, surface_evidence] : std::array{
           std::tuple{
               "exploration.action.contextual_shop_temple_encounter",
               Surface::exploration,
               "outdoor surface 0x01"},
           std::tuple{
               "dungeon.action.contextual_shop_temple_encounter",
               Surface::dungeon,
               "dungeon surface 0x02"},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::interaction, Status::semantic_complete);
    const auto* entry = find_manifest_entry(manifest, stable_id);
    CHECK(entry != nullptr);
    for (const auto evidence : {
             "GAME deck uses one typed ContextualWorldEntryAction",
             "Rest's mutually exclusive outside-camp slot",
             "SHOP, TEMPLE, or ENCOUNTER",
             "shopavail > templeavail > encounter",
             "visual-only canshop",
             "0x5745SSMM",
             surface_evidence,
             "explicit mode 0/1/2",
             "non-camp",
             "exact-mode validation",
             "g 0x00000567",
             "e 0x00000E65",
             "no held-mouse gate",
             "Classic buttonchoice retains",
             "RNG, save, and seamless-transition ownership",
             "no Classic or replay vocabulary changes",
         }) {
      CHECK(entry->evidence.find(evidence) != std::string_view::npos);
    }
    CHECK(entry->source_anchor ==
        "src/presentation/SemanticInputBoundary.cpp::"
        "semantic_contextual_world_entry_tag/"
        "RealmzConsumeSemanticContextualWorldEntryEvent");
  }
}

void test_pool_money_action_rows_remain_distinct_from_information_rows() {
  const auto manifest = gameplay_chrome_coverage_manifest();
  for (const auto& [stable_id, surface, surface_evidence] : std::array{
           std::tuple{
               "exploration.action.pool_money",
               Surface::exploration,
               "outdoor SS=0x01"},
           std::tuple{
               "dungeon.action.pool_money",
               Surface::dungeon,
               "dungeon SS=0x02"},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::interaction, Status::semantic_complete);
    const auto* money = find_manifest_entry(manifest, stable_id);
    CHECK(money != nullptr);
    for (const auto evidence : {
             "one member-free MONEY control in its sixth slot",
             "empty OpenMoneyManagementAction",
             "0x574DSS00",
             surface_evidence,
             "reserved low byte zero",
             "nonempty party of at most six",
             "without carrying or binding member identity",
             "lowercase m keyDown 0x00002E6D",
             "neutral one-shot app1Evt",
             "no held-mouse gate",
             "Camp and noncamp are both valid",
             "dead swapavail",
             "funds/shop/temple/bank state do not gate opening",
             "swapbut/CNTL 157",
             "retain modal and effect ownership",
             "No Classic source or replay vocabulary changes",
             "private no-redistribution manual QA remains open",
             "does not cover pooled-money information",
         }) {
      CHECK(money->evidence.find(evidence) != std::string_view::npos);
    }
    CHECK(money->source_anchor ==
        "src/presentation/SemanticInputBoundary.cpp::"
        "semantic_open_money_management_tag/"
        "RealmzConsumeSemanticOpenMoneyManagementEvent");
  }

  for (const auto& [stable_id, surface] : std::array{
           std::pair{
               "exploration.info.pooled_money", Surface::exploration},
           std::pair{"dungeon.info.pooled_money", Surface::dungeon},
       }) {
    expect_manifest_entry(manifest, stable_id, surface,
        Kind::essential_information, Status::semantic_complete);
    const auto* pooled = find_manifest_entry(manifest, stable_id);
    CHECK(pooled != nullptr);
    CHECK(pooled->source_anchor ==
        "src/presentation/PartyRailLayout.cpp::"
        "compute_party_rail_layout");
  }
}

void test_manifest_source_anchors_resolve(
    const std::filesystem::path& repository_root) {
  namespace fs = std::filesystem;

  const auto canonical_root = fs::canonical(repository_root);
  CHECK(fs::is_directory(canonical_root));
  std::map<fs::path, std::string> source_cache;

  for (const auto& entry : gameplay_chrome_coverage_manifest()) {
    const auto separator = entry.source_anchor.find("::");
    CHECK(separator != std::string_view::npos);
    CHECK(separator > 0U);
    CHECK(entry.source_anchor.find("::", separator + 2U) ==
        std::string_view::npos);

    const auto path_text = entry.source_anchor.substr(0U, separator);
    const auto symbols = entry.source_anchor.substr(separator + 2U);
    CHECK(!symbols.empty());

    const fs::path relative_path{std::string(path_text)};
    CHECK(!relative_path.empty());
    CHECK(relative_path.is_relative());
    CHECK(!relative_path.has_root_name());
    CHECK(!relative_path.has_root_directory());
    for (const auto& component : relative_path) {
      CHECK(component != "..");
    }

    const auto unresolved_path = canonical_root / relative_path;
    CHECK(fs::is_regular_file(unresolved_path));
    const auto source_path = fs::canonical(unresolved_path);
    CHECK(path_is_within(canonical_root, source_path));
    CHECK(fs::is_regular_file(source_path));

    const auto [cached, inserted] = source_cache.try_emplace(source_path);
    if (inserted) {
      cached->second = read_source_file(source_path);
    }
    const auto& source_text = cached->second;

    bool saw_symbol = false;
    size_t symbol_start = 0U;
    while (symbol_start <= symbols.size()) {
      const auto slash = symbols.find('/', symbol_start);
      const auto symbol_end =
          (slash == std::string_view::npos) ? symbols.size() : slash;
      const auto symbol =
          symbols.substr(symbol_start, symbol_end - symbol_start);
      if (!symbol.empty()) {
        saw_symbol = true;
        CHECK(source_text.find(std::string(symbol)) != std::string::npos);
      }
      if (slash == std::string_view::npos) {
        break;
      }
      symbol_start = slash + 1U;
    }
    CHECK(saw_symbol);
  }
}

void test_inventory_revision_covers_every_ordered_manifest_field() {
  const auto manifest = gameplay_chrome_coverage_manifest();
  const auto baseline = gameplay_chrome_inventory_revision(manifest);
  CHECK(kGameplayChromeInventoryRevision == 0xB4CBF4B4E225642BULL);
  CHECK(baseline == kGameplayChromeInventoryRevision);

  for (size_t index = 0; index < manifest.size(); ++index) {
    std::vector<Entry> candidate(manifest.begin(), manifest.end());
    candidate[index].stable_id = "mutation.stable_id";
    CHECK(gameplay_chrome_inventory_revision(candidate) != baseline);

    candidate.assign(manifest.begin(), manifest.end());
    candidate[index].surface =
        (candidate[index].surface == Surface::exploration)
        ? Surface::dungeon
        : Surface::exploration;
    CHECK(gameplay_chrome_inventory_revision(candidate) != baseline);

    candidate.assign(manifest.begin(), manifest.end());
    candidate[index].kind =
        (candidate[index].kind == Kind::interaction)
        ? Kind::essential_information
        : Kind::interaction;
    CHECK(gameplay_chrome_inventory_revision(candidate) != baseline);

    candidate.assign(manifest.begin(), manifest.end());
    candidate[index].status =
        (candidate[index].status == Status::missing)
        ? Status::semantic_complete
        : Status::missing;
    CHECK(gameplay_chrome_inventory_revision(candidate) != baseline);

    candidate.assign(manifest.begin(), manifest.end());
    candidate[index].evidence = "mutation evidence";
    CHECK(gameplay_chrome_inventory_revision(candidate) != baseline);

    candidate.assign(manifest.begin(), manifest.end());
    candidate[index].source_anchor = "mutation.cpp::anchor";
    CHECK(gameplay_chrome_inventory_revision(candidate) != baseline);
  }

  std::vector<Entry> reordered(manifest.begin(), manifest.end());
  std::swap(reordered[0], reordered[1]);
  CHECK(gameplay_chrome_inventory_revision(reordered) != baseline);
}

void test_known_values_and_context_mapping() {
  for (const auto surface : kSurfaces) {
    CHECK(is_known_gameplay_chrome_surface(surface));
    const auto context = screen_context_for_gameplay_chrome_surface(surface);
    CHECK(context.has_value());
    CHECK(gameplay_chrome_surface_for_screen_context(*context) == surface);
  }
  CHECK(!is_known_gameplay_chrome_surface(static_cast<Surface>(99)));
  CHECK(!screen_context_for_gameplay_chrome_surface(
      static_cast<Surface>(99)));

  for (const auto kind : kKinds) {
    CHECK(is_known_gameplay_chrome_role_kind(kind));
  }
  CHECK(!is_known_gameplay_chrome_role_kind(static_cast<Kind>(99)));

  CHECK(is_known_gameplay_chrome_coverage_status(
      Status::retained_in_crop));
  CHECK(is_known_gameplay_chrome_coverage_status(
      Status::semantic_complete));
  CHECK(is_known_gameplay_chrome_coverage_status(Status::missing));
  CHECK(!is_known_gameplay_chrome_coverage_status(
      static_cast<Status>(99)));

  const std::array non_gameplay_contexts{
      ScreenContext::title,
      ScreenContext::party_selection,
      ScreenContext::party_creation,
      ScreenContext::inventory,
      ScreenContext::shop,
      ScreenContext::encounter,
      ScreenContext::ending,
  };
  for (const auto context : non_gameplay_contexts) {
    CHECK(!gameplay_chrome_surface_for_screen_context(context));
  }
  CHECK(!gameplay_chrome_surface_for_screen_context(
      static_cast<ScreenContext>(99)));
}

void test_manifest_is_deterministic_explicit_and_valid() {
  const auto first = gameplay_chrome_coverage_manifest();
  const auto second = gameplay_chrome_coverage_manifest();
  CHECK(first.data() == second.data());
  CHECK(first.size() == second.size());
  CHECK(first.size() == 95U);
  CHECK(kGameplayChromeInventoryRevision == 0xB4CBF4B4E225642BULL);

  const auto validation = validate_gameplay_chrome_coverage(first);
  CHECK(validation.valid);
  CHECK(validation.issue == Issue::none);
  CHECK(validation.entry_index == kNoGameplayChromeEntry);
  CHECK(validation.conflicting_entry_index == kNoGameplayChromeEntry);

  std::set<std::string_view> stable_ids;
  std::array<std::array<size_t, kKinds.size()>, kSurfaces.size()> coverage{};
  std::array<bool, 3> statuses_seen{};
  std::array<size_t, 3> status_counts{};
  size_t missing_interaction_count = 0U;
  size_t missing_information_count = 0U;
  size_t previous_surface = 0U;
  bool first_entry = true;
  for (size_t index = 0; index < first.size(); ++index) {
    const auto& entry = first[index];
    CHECK(entry == second[index]);
    CHECK(!entry.stable_id.empty());
    CHECK(!entry.evidence.empty());
    CHECK(!entry.source_anchor.empty());
    CHECK(entry.source_anchor.find("::") != std::string_view::npos);
    CHECK(is_known_gameplay_chrome_surface(entry.surface));
    CHECK(is_known_gameplay_chrome_role_kind(entry.kind));
    CHECK(is_known_gameplay_chrome_coverage_status(entry.status));
    CHECK(stable_ids.insert(entry.stable_id).second);

    const size_t surface = static_cast<size_t>(entry.surface);
    const size_t kind = static_cast<size_t>(entry.kind);
    CHECK(surface < kSurfaces.size());
    CHECK(kind < kKinds.size());
    if (!first_entry) {
      CHECK(previous_surface <= surface);
    }
    first_entry = false;
    previous_surface = surface;
    ++coverage[surface][kind];
    const size_t status = static_cast<size_t>(entry.status);
    statuses_seen[status] = true;
    ++status_counts[status];
    if (entry.status == Status::missing) {
      if (entry.kind == Kind::interaction) {
        ++missing_interaction_count;
      } else {
        ++missing_information_count;
      }
    }
  }
  CHECK(stable_ids.size() == first.size());
  for (size_t surface = 0; surface < kSurfaces.size(); ++surface) {
    for (size_t kind = 0; kind < kKinds.size(); ++kind) {
      CHECK(coverage[surface][kind] > 0U);
    }
  }
  for (const bool seen : statuses_seen) {
    CHECK(seen);
  }
  CHECK(status_counts[static_cast<size_t>(Status::retained_in_crop)] == 6U);
  CHECK(status_counts[static_cast<size_t>(Status::semantic_complete)] == 61U);
  CHECK(status_counts[static_cast<size_t>(Status::missing)] == 28U);
  CHECK(missing_interaction_count == 15U);
  CHECK(missing_information_count == 13U);
}

void expect_issue(
    const std::vector<Entry>& inventory,
    Issue issue,
    size_t entry_index = kNoGameplayChromeEntry) {
  const auto result = validate_gameplay_chrome_coverage(inventory);
  CHECK(!result.valid);
  CHECK(result.issue == issue);
  CHECK(result.entry_index == entry_index);
}

void test_validation_rejects_every_invariant_failure() {
  const auto valid = minimal_structurally_valid_inventory();
  CHECK(validate_gameplay_chrome_coverage(valid).valid);

  expect_issue({}, Issue::empty_inventory);

  auto candidate = valid;
  candidate[1].stable_id = {};
  expect_issue(candidate, Issue::empty_stable_id, 1U);

  candidate = valid;
  candidate[2].surface = static_cast<Surface>(99);
  expect_issue(candidate, Issue::unknown_surface, 2U);

  candidate = valid;
  candidate[3].kind = static_cast<Kind>(99);
  expect_issue(candidate, Issue::unknown_role_kind, 3U);

  candidate = valid;
  candidate[4].status = static_cast<Status>(99);
  expect_issue(candidate, Issue::unknown_status, 4U);

  candidate = valid;
  candidate[5].evidence = {};
  expect_issue(candidate, Issue::empty_evidence, 5U);

  candidate = valid;
  candidate[0].source_anchor = {};
  expect_issue(candidate, Issue::empty_source_anchor, 0U);

  candidate = valid;
  candidate.push_back(candidate[2]);
  const auto duplicate = validate_gameplay_chrome_coverage(candidate);
  CHECK(!duplicate.valid);
  CHECK(duplicate.issue == Issue::duplicate_entry);
  CHECK(duplicate.entry_index == valid.size());
  CHECK(duplicate.conflicting_entry_index == 2U);

  candidate = valid;
  auto duplicate_id = candidate[1];
  duplicate_id.surface = Surface::combat;
  duplicate_id.evidence = "different evidence";
  duplicate_id.source_anchor = "fixture.cpp::different";
  candidate.push_back(duplicate_id);
  const auto duplicate_stable_id =
      validate_gameplay_chrome_coverage(candidate);
  CHECK(!duplicate_stable_id.valid);
  CHECK(duplicate_stable_id.issue == Issue::duplicate_stable_id);
  CHECK(duplicate_stable_id.entry_index == valid.size());
  CHECK(duplicate_stable_id.conflicting_entry_index == 1U);

  // Every surface/kind cell is independently required.
  for (size_t removed = 0; removed < valid.size(); ++removed) {
    candidate = valid;
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(removed));
    expect_issue(candidate, Issue::incomplete_surface_role_kind);
  }
}

void test_assessment_requires_the_exact_canonical_role_set() {
  auto covered = covered_canonical_inventory();
  for (const auto surface : kSurfaces) {
    const auto assessment =
        assess_gameplay_chrome_coverage(surface, covered);
    CHECK(assessment.inventory_validation.valid);
    CHECK(assessment.surface_known);
    CHECK(assessment.canonical_role_set_matches);
    CHECK(assessment.interaction_count > 0U);
    CHECK(assessment.essential_information_count > 0U);
    CHECK(assessment.missing_count == 0U);
    CHECK(assessment.missing_interaction_count == 0U);
    CHECK(assessment.missing_essential_information_count == 0U);
    CHECK(assessment.inventory_missing_count == 0U);
    CHECK(assessment.inventory_missing_interaction_count == 0U);
    CHECK(assessment.inventory_missing_essential_information_count == 0U);
    CHECK(assessment.complete);
  }

  // Both role kinds independently contribute to the active-surface diagnostic
  // and to the inventory-wide fail-closed coverage gate.
  for (const auto surface : kSurfaces) {
    for (const auto missing_kind : kKinds) {
      auto one_missing = covered_canonical_inventory();
      bool changed = false;
      for (auto& entry : one_missing) {
        if ((entry.surface == surface) && (entry.kind == missing_kind)) {
          entry.status = Status::missing;
          changed = true;
          break;
        }
      }
      CHECK(changed);
      const auto assessment =
          assess_gameplay_chrome_coverage(surface, one_missing);
      CHECK(assessment.canonical_role_set_matches);
      CHECK(assessment.missing_count == 1U);
      CHECK(assessment.missing_interaction_count ==
          (missing_kind == Kind::interaction ? 1U : 0U));
      CHECK(assessment.missing_essential_information_count ==
          (missing_kind == Kind::essential_information ? 1U : 0U));
      CHECK(assessment.inventory_missing_count == 1U);
      CHECK(assessment.inventory_missing_interaction_count ==
          (missing_kind == Kind::interaction ? 1U : 0U));
      CHECK(assessment.inventory_missing_essential_information_count ==
          (missing_kind == Kind::essential_information ? 1U : 0U));
      CHECK(!assessment.complete);
    }
  }

  // A missing role on another surface must block the selected surface while
  // leaving its active-surface diagnostics clear.
  for (size_t active_index = 0; active_index < kSurfaces.size(); ++active_index) {
    const auto active_surface = kSurfaces[active_index];
    const auto other_surface =
        kSurfaces[(active_index + 1U) % kSurfaces.size()];
    for (const auto missing_kind : kKinds) {
      auto one_missing = covered_canonical_inventory();
      bool changed = false;
      for (auto& entry : one_missing) {
        if ((entry.surface == other_surface) &&
            (entry.kind == missing_kind)) {
          entry.status = Status::missing;
          changed = true;
          break;
        }
      }
      CHECK(changed);
      const auto assessment =
          assess_gameplay_chrome_coverage(active_surface, one_missing);
      CHECK(assessment.canonical_role_set_matches);
      CHECK(assessment.missing_count == 0U);
      CHECK(assessment.missing_interaction_count == 0U);
      CHECK(assessment.missing_essential_information_count == 0U);
      CHECK(assessment.inventory_missing_count == 1U);
      CHECK(assessment.inventory_missing_interaction_count ==
          (missing_kind == Kind::interaction ? 1U : 0U));
      CHECK(assessment.inventory_missing_essential_information_count ==
          (missing_kind == Kind::essential_information ? 1U : 0U));
      CHECK(!assessment.complete);
    }
  }

  // Role-set matching remains an identity diagnostic. The revision contract
  // separately requires a revision bump for changes to status, evidence,
  // source anchors, or their cited Classic behavior.
  covered.front().status = Status::retained_in_crop;
  covered.front().evidence = "updated evidence";
  covered.front().source_anchor = "updated.cpp::anchor";
  CHECK(assess_gameplay_chrome_coverage(
      Surface::exploration, covered).canonical_role_set_matches);

  const auto subset = minimal_structurally_valid_inventory();
  const auto subset_assessment =
      assess_gameplay_chrome_coverage(Surface::exploration, subset);
  CHECK(subset_assessment.inventory_validation.valid);
  CHECK(!subset_assessment.canonical_role_set_matches);
  CHECK(!subset_assessment.complete);

  covered = covered_canonical_inventory();
  covered.erase(covered.begin() + 1);
  const auto omitted =
      assess_gameplay_chrome_coverage(Surface::exploration, covered);
  CHECK(omitted.inventory_validation.valid);
  CHECK(!omitted.canonical_role_set_matches);
  CHECK(!omitted.complete);

  covered = covered_canonical_inventory();
  covered[1].stable_id = "replacement.exploration.role";
  const auto replaced =
      assess_gameplay_chrome_coverage(Surface::exploration, covered);
  CHECK(replaced.inventory_validation.valid);
  CHECK(!replaced.canonical_role_set_matches);
  CHECK(!replaced.complete);

  covered = covered_canonical_inventory();
  covered[1].surface = Surface::dungeon;
  const auto moved =
      assess_gameplay_chrome_coverage(Surface::exploration, covered);
  CHECK(moved.inventory_validation.valid);
  CHECK(!moved.canonical_role_set_matches);
  CHECK(!moved.complete);

  covered = covered_canonical_inventory();
  covered[1].kind = Kind::essential_information;
  const auto reclassified =
      assess_gameplay_chrome_coverage(Surface::exploration, covered);
  CHECK(reclassified.inventory_validation.valid);
  CHECK(!reclassified.canonical_role_set_matches);
  CHECK(!reclassified.complete);

  covered = covered_canonical_inventory();
  covered.push_back(covered.front());
  const auto invalid =
      assess_gameplay_chrome_coverage(Surface::exploration, covered);
  CHECK(!invalid.inventory_validation.valid);
  CHECK(!invalid.canonical_role_set_matches);
  CHECK(!invalid.complete);

  const auto unknown = assess_gameplay_chrome_coverage(
      static_cast<Surface>(99), covered_canonical_inventory());
  CHECK(!unknown.surface_known);
  CHECK(unknown.inventory_missing_count == 0U);
  CHECK(!unknown.complete);
}

void test_current_manifest_honestly_blocks_every_surface() {
  for (const auto surface : kSurfaces) {
    const auto assessment = assess_gameplay_chrome_coverage(
        surface, gameplay_chrome_coverage_manifest());
    CHECK(assessment.inventory_validation.valid);
    CHECK(assessment.canonical_role_set_matches);
    CHECK(assessment.missing_count > 0U);
    CHECK(assessment.missing_interaction_count > 0U);
    CHECK(assessment.missing_essential_information_count > 0U);
    CHECK(assessment.inventory_missing_count > 0U);
    CHECK(assessment.inventory_missing_interaction_count > 0U);
    CHECK(assessment.inventory_missing_essential_information_count > 0U);
    CHECK(!assessment.complete);

    const auto readiness = evaluate_current_gameplay_crop_readiness(
        surface, ready_runtime(surface));
    CHECK(readiness.runtime_prerequisites_complete);
    CHECK(!readiness.coverage.complete);
    CHECK(!readiness.ready);
  }
}

void check_runtime_rejected(const GameplayCropReadiness& result) {
  CHECK(!result.runtime_prerequisites_complete);
  CHECK(!result.ready);
}

void expect_unknown_expected_variant(
    const std::vector<Entry>& covered,
    const GameplayCropRuntimePrerequisites& baseline_runtime,
    const Variant& unknown_variant) {
  auto runtime = baseline_runtime;
  runtime.expected_variant = unknown_variant;
  const auto result = evaluate_gameplay_crop_readiness(
      Surface::exploration, covered, runtime);
  CHECK(!result.expected_context_variant_known);
  CHECK(result.live_context_variant_known);
  CHECK(!result.context_variants_exact_match);
  CHECK(!result.standard_context_variant_supported);
  check_runtime_rejected(result);
}

void expect_live_variant_mismatch(
    const std::vector<Entry>& covered,
    const GameplayCropRuntimePrerequisites& baseline_runtime,
    const Variant& mismatched_variant) {
  auto runtime = baseline_runtime;
  runtime.live_variant = mismatched_variant;
  const auto result = evaluate_gameplay_crop_readiness(
      Surface::exploration, covered, runtime);
  CHECK(result.expected_context_variant_known);
  CHECK(result.live_context_variant_known);
  CHECK(!result.context_variants_exact_match);
  CHECK(!result.standard_context_variant_supported);
  check_runtime_rejected(result);
}

void expect_unsupported_exact_variant(
    const std::vector<Entry>& covered,
    Surface surface,
    const Variant& unsupported_variant) {
  auto runtime = ready_runtime(surface);
  runtime.expected_variant = unsupported_variant;
  runtime.live_variant = unsupported_variant;
  runtime.snapshot_context = *unsupported_variant.screen;
  runtime.shell_model_context = *unsupported_variant.screen;
  const auto result =
      evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(result.expected_context_variant_known);
  CHECK(result.live_context_variant_known);
  CHECK(result.context_matches_surface);
  CHECK(result.context_variants_exact_match);
  CHECK(!result.standard_context_variant_supported);
  CHECK(result.snapshot_matches_context);
  CHECK(result.shell_model_matches_snapshot_context);
  check_runtime_rejected(result);
}

void test_standard_context_variants_are_explicit_and_supported() {
  const auto covered = covered_canonical_inventory();
  for (const auto surface : kSurfaces) {
    const auto runtime = ready_runtime(surface);
    const auto result =
        evaluate_gameplay_crop_readiness(surface, covered, runtime);
    CHECK(result.coverage.complete);
    CHECK(result.expected_context_variant_known);
    CHECK(result.live_context_variant_known);
    CHECK(result.context_matches_surface);
    CHECK(result.context_variants_exact_match);
    CHECK(result.standard_context_variant_supported);
    CHECK(result.snapshot_matches_context);
    CHECK(result.shell_model_matches_snapshot_context);
    CHECK(result.gameplay_window_active);
    CHECK(result.front_is_gameplay_surface);
    CHECK(!result.legacy_requires_full_frame);
    CHECK(result.legacy_full_frame_clear);
    CHECK(!result.inventory_revision_matches);
    CHECK(result.snapshot_revision_nonzero);
    CHECK(result.snapshot_revision_matches_shell_model);
    CHECK(result.shell_model_valid);
    CHECK(result.shell_layout_valid);
    CHECK(result.shell_font_valid);
    CHECK(result.expected_controls_present);
    CHECK(result.live_handlers_present);
    CHECK(result.informational_surfaces_complete);
    CHECK(!result.runtime_prerequisites_complete);
    CHECK(!result.ready);
  }

  auto runtime = ready_runtime(Surface::dungeon);
  runtime.expected_variant.world_presentation =
      WorldPresentation::dungeon_map;
  runtime.live_variant = runtime.expected_variant;
  auto result = evaluate_gameplay_crop_readiness(
      Surface::dungeon, covered, runtime);
  CHECK(result.coverage.complete);
  CHECK(result.standard_context_variant_supported);
  CHECK(!result.inventory_revision_matches);
  CHECK(!result.runtime_prerequisites_complete);
  CHECK(!result.ready);

  runtime = ready_runtime(Surface::combat);
  runtime.expected_variant.acting_combatant = CombatantId{10};
  runtime.live_variant = runtime.expected_variant;
  result = evaluate_gameplay_crop_readiness(
      Surface::combat, covered, runtime);
  CHECK(result.coverage.complete);
  CHECK(result.standard_context_variant_supported);
  CHECK(!result.inventory_revision_matches);
  CHECK(!result.runtime_prerequisites_complete);
  CHECK(!result.ready);
}

void test_context_variant_defaults_unknowns_and_mismatches_fail_closed() {
  const auto covered = covered_canonical_inventory();
  const auto surface = Surface::exploration;
  const auto baseline_runtime = ready_runtime(surface);

  const auto defaults = evaluate_gameplay_crop_readiness(
      surface, covered, GameplayCropRuntimePrerequisites{});
  CHECK(!defaults.expected_context_variant_known);
  CHECK(!defaults.live_context_variant_known);
  CHECK(!defaults.context_matches_surface);
  CHECK(!defaults.context_variants_exact_match);
  CHECK(!defaults.standard_context_variant_supported);
  CHECK(!defaults.snapshot_matches_context);
  CHECK(!defaults.shell_model_matches_snapshot_context);
  CHECK(!defaults.gameplay_window_active);
  CHECK(!defaults.front_is_gameplay_surface);
  CHECK(defaults.legacy_requires_full_frame);
  CHECK(!defaults.legacy_full_frame_clear);
  CHECK(!defaults.inventory_revision_matches);
  CHECK(!defaults.snapshot_revision_nonzero);
  CHECK(!defaults.snapshot_revision_matches_shell_model);
  CHECK(!defaults.shell_model_valid);
  CHECK(!defaults.shell_layout_valid);
  CHECK(!defaults.shell_font_valid);
  CHECK(!defaults.expected_controls_present);
  CHECK(!defaults.live_handlers_present);
  CHECK(!defaults.informational_surfaces_complete);
  check_runtime_rejected(defaults);

  auto candidate = baseline_runtime.expected_variant;
  candidate.surface.reset();
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.surface = static_cast<Surface>(99);
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.screen.reset();
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.screen = static_cast<ScreenContext>(99);
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.world_presentation.reset();
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.world_presentation = static_cast<WorldPresentation>(99);
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.camp_state = Activity::unknown;
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.nested_modal_state = static_cast<Activity>(99);
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.selected_member_known = false;
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.selected_member = PartyMemberId{6};
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.acting_combatant_known = false;
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.expected_variant;
  candidate.acting_combatant = CombatantId{6};
  expect_unknown_expected_variant(covered, baseline_runtime, candidate);

  auto runtime = baseline_runtime;
  runtime.live_variant.world_presentation.reset();
  auto result =
      evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(result.expected_context_variant_known);
  CHECK(!result.live_context_variant_known);
  CHECK(!result.context_variants_exact_match);
  CHECK(!result.standard_context_variant_supported);
  check_runtime_rejected(result);

  candidate = baseline_runtime.live_variant;
  candidate.surface = Surface::dungeon;
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.screen = ScreenContext::dungeon;
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.world_presentation = WorldPresentation::dungeon_map;
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.camp_state = Activity::active;
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.nested_modal_state = Activity::active;
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.spell_state = Activity::active;
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.targeting_state = Activity::active;
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.text_entry_state = Activity::active;
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.selected_member = PartyMemberId{1};
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  candidate = baseline_runtime.live_variant;
  candidate.acting_combatant = CombatantId{10};
  expect_live_variant_mismatch(covered, baseline_runtime, candidate);

  runtime = baseline_runtime;
  runtime.expected_variant.surface = Surface::dungeon;
  runtime.live_variant = runtime.expected_variant;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(result.expected_context_variant_known);
  CHECK(result.live_context_variant_known);
  CHECK(!result.context_matches_surface);
  CHECK(result.context_variants_exact_match);
  CHECK(!result.standard_context_variant_supported);
  check_runtime_rejected(result);
}

void test_nonstandard_context_flows_fail_closed() {
  const auto covered = covered_canonical_inventory();
  auto variant = standard_variant(Surface::exploration);

  variant.camp_state = Activity::active;
  expect_unsupported_exact_variant(
      covered, Surface::exploration, variant);

  variant = standard_variant(Surface::exploration);
  variant.nested_modal_state = Activity::active;
  expect_unsupported_exact_variant(
      covered, Surface::exploration, variant);

  variant = standard_variant(Surface::exploration);
  variant.spell_state = Activity::active;
  expect_unsupported_exact_variant(
      covered, Surface::exploration, variant);

  variant = standard_variant(Surface::exploration);
  variant.targeting_state = Activity::active;
  expect_unsupported_exact_variant(
      covered, Surface::exploration, variant);

  variant = standard_variant(Surface::exploration);
  variant.text_entry_state = Activity::active;
  expect_unsupported_exact_variant(
      covered, Surface::exploration, variant);

  variant = standard_variant(Surface::exploration);
  variant.world_presentation = WorldPresentation::dungeon_map;
  expect_unsupported_exact_variant(
      covered, Surface::exploration, variant);

  variant = standard_variant(Surface::exploration);
  variant.selected_member.reset();
  expect_unsupported_exact_variant(
      covered, Surface::exploration, variant);

  variant = standard_variant(Surface::exploration);
  variant.acting_combatant = CombatantId{10};
  expect_unsupported_exact_variant(
      covered, Surface::exploration, variant);

  variant = standard_variant(Surface::dungeon);
  variant.world_presentation = WorldPresentation::outdoor;
  expect_unsupported_exact_variant(covered, Surface::dungeon, variant);

  variant = standard_variant(Surface::combat);
  variant.world_presentation = WorldPresentation::none;
  expect_unsupported_exact_variant(covered, Surface::combat, variant);

  variant = standard_variant(Surface::combat);
  variant.acting_combatant.reset();
  expect_unsupported_exact_variant(covered, Surface::combat, variant);
}

void test_runtime_predicates_fail_closed_one_at_a_time() {
  const auto covered = covered_canonical_inventory();
  const auto surface = Surface::exploration;
  const auto baseline_runtime = ready_runtime(surface);
  auto runtime = baseline_runtime;

  runtime.snapshot_context = ScreenContext::dungeon;
  runtime.shell_model_context = ScreenContext::dungeon;
  auto result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.snapshot_matches_context);
  CHECK(result.shell_model_matches_snapshot_context);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.shell_model_context = ScreenContext::dungeon;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(result.snapshot_matches_context);
  CHECK(!result.shell_model_matches_snapshot_context);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.gameplay_window_active = false;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.gameplay_window_active);
  CHECK(result.front_is_gameplay_surface);
  CHECK(!result.legacy_requires_full_frame);
  CHECK(!result.legacy_full_frame_clear);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.front_is_gameplay_surface = false;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(result.gameplay_window_active);
  CHECK(!result.front_is_gameplay_surface);
  CHECK(!result.legacy_requires_full_frame);
  CHECK(!result.legacy_full_frame_clear);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.legacy_requires_full_frame = true;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(result.gameplay_window_active);
  CHECK(result.front_is_gameplay_surface);
  CHECK(result.legacy_requires_full_frame);
  CHECK(!result.legacy_full_frame_clear);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.expected_inventory_revision =
      kGameplayChromeInventoryRevision + 1U;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.inventory_revision_matches);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.snapshot_revision = 0U;
  runtime.shell_model_revision = 0U;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.snapshot_revision_nonzero);
  CHECK(!result.snapshot_revision_matches_shell_model);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  ++runtime.shell_model_revision;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(result.snapshot_revision_nonzero);
  CHECK(!result.snapshot_revision_matches_shell_model);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.shell_model_valid = false;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.shell_model_valid);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.shell_layout_valid = false;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.shell_layout_valid);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.shell_font_valid = false;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.shell_font_valid);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.expected_controls_present = false;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.expected_controls_present);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.live_handlers_present = false;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.live_handlers_present);
  check_runtime_rejected(result);

  runtime = baseline_runtime;
  runtime.informational_surfaces_complete = false;
  result = evaluate_gameplay_crop_readiness(surface, covered, runtime);
  CHECK(!result.informational_surfaces_complete);
  check_runtime_rejected(result);

  auto omitted = covered;
  omitted.erase(omitted.begin());
  result = evaluate_gameplay_crop_readiness(
      surface, omitted, baseline_runtime);
  CHECK(!result.inventory_revision_matches);
  CHECK(!result.runtime_prerequisites_complete);
  CHECK(!result.coverage.canonical_role_set_matches);
  CHECK(!result.ready);

  auto replaced = covered;
  replaced.front().stable_id = "replacement.role";
  result = evaluate_gameplay_crop_readiness(
      surface, replaced, baseline_runtime);
  CHECK(!result.inventory_revision_matches);
  CHECK(!result.runtime_prerequisites_complete);
  CHECK(!result.coverage.canonical_role_set_matches);
  CHECK(!result.ready);
}

void test_evaluation_is_mutation_free_and_deterministic() {
  auto inventory = covered_canonical_inventory();
  const auto before = inventory;
  const auto runtime = ready_runtime(Surface::combat);

  const auto validation_first =
      validate_gameplay_chrome_coverage(inventory);
  const auto assessment_first =
      assess_gameplay_chrome_coverage(Surface::combat, inventory);
  const auto readiness_first = evaluate_gameplay_crop_readiness(
      Surface::combat, inventory, runtime);
  CHECK(inventory == before);

  const auto validation_second =
      validate_gameplay_chrome_coverage(inventory);
  const auto assessment_second =
      assess_gameplay_chrome_coverage(Surface::combat, inventory);
  const auto readiness_second = evaluate_gameplay_crop_readiness(
      Surface::combat, inventory, runtime);
  CHECK(inventory == before);
  CHECK(validation_first == validation_second);
  CHECK(assessment_first == assessment_second);
  CHECK(readiness_first == readiness_second);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "usage: GameplayChromeCoverageTest <repository-root>\n";
    return 2;
  }

  try {
    test_known_values_and_context_mapping();
    test_manifest_matches_independent_oracle();
    test_selected_item_drilldown_rows_are_semantically_complete();
    test_contextual_world_entry_rows_are_semantically_complete();
    test_pool_money_action_rows_remain_distinct_from_information_rows();
    test_manifest_source_anchors_resolve(argv[1]);
    test_inventory_revision_covers_every_ordered_manifest_field();
    test_manifest_is_deterministic_explicit_and_valid();
    test_validation_rejects_every_invariant_failure();
    test_assessment_requires_the_exact_canonical_role_set();
    test_current_manifest_honestly_blocks_every_surface();
    test_standard_context_variants_are_explicit_and_supported();
    test_context_variant_defaults_unknowns_and_mismatches_fail_closed();
    test_nonstandard_context_flows_fail_closed();
    test_runtime_predicates_fail_closed_one_at_a_time();
    test_evaluation_is_mutation_free_and_deterministic();
    std::cout << "GameplayChromeCoverageTest: " << checks_run
              << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "GameplayChromeCoverageTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
