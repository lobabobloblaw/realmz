#include "GameplayChromeCoverage.hpp"

#include <array>
#include <optional>

namespace realmz::presentation {
namespace {

using Surface = GameplayChromeSurface;
using Kind = GameplayChromeRoleKind;
using Status = GameplayChromeCoverageStatus;
using Entry = GameplayChromeCoverageEntry;

constexpr uint64_t kFnv1aOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnv1aPrime = 1099511628211ULL;

constexpr uint64_t append_revision_byte(uint64_t hash, uint8_t value) noexcept {
  return (hash ^ static_cast<uint64_t>(value)) * kFnv1aPrime;
}

constexpr uint64_t append_revision_text(
    uint64_t hash,
    std::string_view text) noexcept {
  for (const char value : text) {
    hash = append_revision_byte(
        hash, static_cast<uint8_t>(static_cast<unsigned char>(value)));
  }
  return append_revision_byte(hash, 0U);
}

constexpr GameplayChromeInventoryRevision compute_inventory_revision(
    std::span<const Entry> inventory) noexcept {
  uint64_t hash = kFnv1aOffsetBasis;
  for (const auto& entry : inventory) {
    hash = append_revision_text(hash, entry.stable_id);
    hash = append_revision_byte(
        hash, static_cast<uint8_t>(entry.surface));
    hash = append_revision_byte(hash, static_cast<uint8_t>(entry.kind));
    hash = append_revision_byte(hash, static_cast<uint8_t>(entry.status));
    hash = append_revision_byte(hash, 0xFFU);
    hash = append_revision_text(hash, entry.evidence);
    hash = append_revision_text(hash, entry.source_anchor);
  }
  return hash;
}

// Keep this inventory ordered by surface, then by the Classic role's visual
// grouping. Statuses describe the repository as it exists today, not intended
// future behavior. In particular, the missing entries are crop blockers even
// when their source values already exist in GameSnapshot.
constexpr auto kGameplayChromeCoverageManifest = std::to_array<Entry>({
    // Exploration interactions.
    {"exploration.viewport.pointer_navigation", Surface::exploration,
        Kind::interaction, Status::retained_in_crop,
        "Classic movelook remains inside the isolated outdoor viewport.",
        "src/realmz_orig/misc.c::mainscreeninit/movelook"},
    {"exploration.action.eight_direction_movement", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The action bar exposes typed commands for all eight outdoor moves.",
        "src/presentation/ShellControlLayout.cpp::kOutdoorMovement"},
    {"exploration.action.select_party_member", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "Party cards expose stable typed member-selection actions.",
        "src/presentation/PartyRailControlLayout.cpp::compute_party_rail_control_layout"},
    {"exploration.action.open_inventory", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a typed selected-member inventory command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"exploration.action.open_spellbook", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a typed selected-member spellbook command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"exploration.action.save_game", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a guarded typed Save command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"exploration.action.load_game", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a guarded typed Load command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"exploration.action.character_sheet", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The PARTY deck has a typed selected-member Character command with a guarded one-shot handoff into Classic's existing character-sheet path.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout/OpenCharacterSheetAction"},
    {"exploration.action.rest", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck has a typed Rest command whose single-use outdoor tag is late-validated as adaptive, outdoor, and in-camp before yielding one Classic Rest keyDown only when EventManager's non-pumping cached SDL/Classic held-mouse delivery gate is clear; Classic retains sound, fatigue, time, encounter, and revert handling.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_rest_party_tag/RealmzConsumeSemanticRestPartyEvent"},
    {"exploration.action.camp", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck has a typed explicit desired-state Camp/Break Camp command whose single-use outdoor tag is late-validated as adaptive, outdoor, and still opposite to the requested state before yielding Classic's exact c keyDown 0x00000863; stale activations cannot reverse newer state, and Classic retains inverted cancamp permission and feedback plus music, sound, state, movement, time, control refresh, and revert handling.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_set_camp_state_tag/RealmzConsumeSemanticSetCampStateEvent"},
    {"exploration.action.search_toggle", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck exposes SEARCH/STOP SEARCH as a typed absolute desired searching-state command. Its single-use 0x5753 tag carries the outdoor surface and a strict Boolean destination, is late-validated against adaptive outdoor presentation, the live real Classic `search` control, and Classic's current any-nonzero search state still opposite to the request, then authorizes only a neutral `app1Evt` sideband into `buttonchoice`; no key, pointer, or direct state mutation is forged. Classic `buttonchoice` retains sound, state, and icon ownership; `checkforsecret` and time effects occur only in subsequent Classic behavior.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_set_search_state_tag/RealmzConsumeSemanticSetSearchStateEvent"},
    {"exploration.action.use_torch", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck exposes a typed one-shot Torch command. Its single-use 0x5754 tag binds the outdoor surface to the member/slot locator of the first exact Torch item when that item has a positive charge. That locator is a freshness token rather than a stable item identity; late validation requires that same source, adaptive outdoor presentation, and the live real Classic `torch` control before a neutral `app1Evt` enters `buttonchoice`. No key, pointer, direct inventory or Light mutation, or `timeclick` is forged. Classic `buttonchoice` alone owns charge consumption, item dropping and slot shifts, item/spell loading, RNG, sound, Light duration, darkness, and icon updates.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_use_torch_tag/RealmzConsumeSemanticUseTorchEvent"},
    {"exploration.action.contextual_overview", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck has one discriminated ContextualOverviewAction that binds Area Search outside camp or the selected-member Make Scroll command in camp. Its strict single-use 0x574F tag preserves the outdoor surface, explicit mode, and canonical absent-or-bounded member so queued work cannot change meaning or retarget. Late validation yields only Classic's exact a keyDown 0x00000061 or k keyDown 0x0000286B; only Area Search is rejected by EventManager's non-pumping cached SDL/Classic held-mouse gate. No Classic source or replay vocabulary changes. Classic retains the contextual control, feedback, forced secret search, time, encounters, revert handling, complete scroll modal, spell and parchment effects, and every mutation.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_contextual_overview_tag/RealmzConsumeSemanticContextualOverviewEvent"},
    {"exploration.action.selected_item_drilldown", Surface::exploration,
        Kind::interaction, Status::missing,
        "No semantic control replaces Classic's selected-member item drilldown.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/showitembut"},
    {"exploration.action.contextual_shop_temple_encounter",
        Surface::exploration, Kind::interaction, Status::missing,
        "No semantic control replaces the context-sensitive Shop, Temple, and seamless-encounter entry point.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/shopbut"},
    {"exploration.action.pool_money", Surface::exploration,
        Kind::interaction, Status::missing,
        "No semantic control replaces Classic pooled-money handling.",
        "src/realmz_orig/misc.c::mainscreeninit/swapbut"},
    {"exploration.action.trade", Surface::exploration,
        Kind::interaction, Status::missing,
        "No semantic control replaces the Classic Trade command.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/tradebut"},
    {"exploration.action.heal", Surface::exploration,
        Kind::interaction, Status::missing,
        "No semantic control replaces the Classic party Heal command.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/barbut"},
    {"exploration.action.use_scroll", Surface::exploration,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a typed selected-member scroll-case command with a guarded outdoor handoff.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout/OpenScrollCaseAction"},
    {"exploration.action.show_conditions", Surface::exploration,
        Kind::interaction, Status::missing,
        "No semantic control opens the complete Classic condition view.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/showconditionbut"},
    {"exploration.action.party_auto_toggles", Surface::exploration,
        Kind::interaction, Status::missing,
        "No semantic controls replace Classic's per-member Auto toggles.",
        "src/realmz_orig/misc.c::mainscreeninit/autoone"},

    // Exploration essential information.
    {"exploration.info.world_view", Surface::exploration,
        Kind::essential_information, Status::retained_in_crop,
        "The outdoor map remains visible inside the isolated Classic viewport.",
        "src/presentation/AdaptiveShell.hpp::kClassicGameplayCrop"},
    {"exploration.info.party_vitals", Surface::exploration,
        Kind::essential_information, Status::missing,
        "Code-native party cards omit Classic's per-member AC and spell-point or noncaster attack-cadence readouts.",
        "src/realmz_orig/updatechar.c::updatechar/ac/spellpoints/normattacks"},
    {"exploration.info.selected_member_details", Surface::exploration,
        Kind::essential_information, Status::semantic_complete,
        "Wide and compact semantic details present selected-member vitals and states.",
        "src/presentation/SelectedPartyDetailsLayout.cpp::compute_selected_party_details_layout"},
    {"exploration.info.party_condition_indicators", Surface::exploration,
        Kind::essential_information, Status::missing,
        "The semantic shell does not present Classic's complete party-wide condition indicators.",
        "src/realmz_orig/tickcheck.c-updatetorch.c::updatetorch/partycondition"},
    {"exploration.info.narrative_messages", Surface::exploration,
        Kind::essential_information, Status::missing,
        "The semantic shell is not yet fed the authoritative Classic textrect messages.",
        "src/WindowManager.cpp::present_remastered_frame/event_log"},
    {"exploration.info.world_coordinates", Surface::exploration,
        Kind::essential_information, Status::missing,
        "Classic renders lookx + partyx and looky + partyy; those authoritative outdoor coordinates are not captured and presented semantically.",
        "src/realmz_orig/misc.c::xy/lookx/partyx"},
    {"exploration.info.calendar_clock", Surface::exploration,
        Kind::essential_information, Status::missing,
        "Day and time shown by Classic chrome are absent from the semantic snapshot and shell.",
        "src/realmz_orig/textbox-time.c::timeclick/tyme/strftime"},
    {"exploration.info.fatigue", Surface::exploration,
        Kind::essential_information, Status::missing,
        "Party fatigue is captured but has no complete semantic presentation.",
        "src/presentation/GameSnapshot.hpp::PartyView/fatigue"},
    {"exploration.info.pooled_money", Surface::exploration,
        Kind::essential_information, Status::missing,
        "Pooled money is captured but has no complete semantic presentation.",
        "src/presentation/GameSnapshot.hpp::PartyView/pooled_money"},
    {"exploration.info.search_and_torch_state", Surface::exploration,
        Kind::essential_information, Status::missing,
        "The Search control presents the current searching state, but the shell still lacks a complete presentation of Classic's persistent Torch state.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/search/torch"},

    // Dungeon interactions.
    {"dungeon.viewport.pointer_navigation", Surface::dungeon,
        Kind::interaction, Status::retained_in_crop,
        "Classic movelook remains inside the isolated dungeon viewport.",
        "src/realmz_orig/misc.c::mainscreeninit/movelook"},
    {"dungeon.action.relative_movement", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The action bar exposes typed turn-left, forward, back, and turn-right commands.",
        "src/presentation/ShellControlLayout.cpp::kDungeonMovement"},
    {"dungeon.action.select_party_member", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "Party cards expose stable typed member-selection actions.",
        "src/presentation/PartyRailControlLayout.cpp::compute_party_rail_control_layout"},
    {"dungeon.action.open_inventory", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a typed selected-member inventory command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"dungeon.action.open_spellbook", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a typed selected-member spellbook command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"dungeon.action.save_game", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a guarded typed Save command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"dungeon.action.load_game", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a guarded typed Load command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"dungeon.action.character_sheet", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The PARTY deck has a typed selected-member Character command with a guarded one-shot handoff into Classic's existing character-sheet path.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout/OpenCharacterSheetAction"},
    {"dungeon.action.rest", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck has a typed Rest command whose single-use dungeon tag is late-validated as adaptive, in-camp, and still in a dungeon map or first-person presentation before yielding one Classic Rest keyDown only when EventManager's non-pumping cached SDL/Classic held-mouse delivery gate is clear; Classic retains sound, fatigue, time, encounter, and revert handling.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_rest_party_tag/RealmzConsumeSemanticRestPartyEvent"},
    {"dungeon.action.camp", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck has a typed explicit desired-state Camp/Break Camp command whose single-use dungeon tag is late-validated as adaptive, still in a dungeon map or first-person presentation, and still opposite to the requested state before yielding Classic's exact c keyDown 0x00000863; stale activations cannot reverse newer state, and Classic retains inverted cancamp permission and feedback plus music, sound, state, movement, time, control refresh, and revert handling.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_set_camp_state_tag/RealmzConsumeSemanticSetCampStateEvent"},
    {"dungeon.action.search_toggle", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck exposes SEARCH/STOP SEARCH as a typed absolute desired searching-state command. Its single-use 0x5753 tag carries the dungeon surface and a strict Boolean destination, is late-validated against adaptive dungeon-map or first-person presentation, the live real Classic `search` control, and Classic's current any-nonzero search state still opposite to the request, then authorizes only a neutral `app1Evt` sideband into `buttonchoice`; no key, pointer, or direct state mutation is forged. Classic `buttonchoice` retains sound, state, and icon ownership; `checkforsecret` and time effects occur only in subsequent Classic behavior.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_set_search_state_tag/RealmzConsumeSemanticSetSearchStateEvent"},
    {"dungeon.action.use_torch", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck exposes a typed one-shot Torch command. Its single-use 0x5754 tag binds the dungeon surface to the member/slot locator of the first exact Torch item when that item has a positive charge. That locator is a freshness token rather than a stable item identity; late validation requires that same source, adaptive dungeon-map or first-person presentation, and the live real Classic `torch` control before a neutral `app1Evt` enters `buttonchoice`. No key, pointer, direct inventory or Light mutation, or `timeclick` is forged. Classic `buttonchoice` alone owns charge consumption, item dropping and slot shifts, item/spell loading, RNG, sound, Light duration, darkness, and icon updates.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_use_torch_tag/RealmzConsumeSemanticUseTorchEvent"},
    {"dungeon.action.contextual_overview", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The GAME deck has one discriminated ContextualOverviewAction that binds Area Search outside camp or the selected-member Make Scroll command in camp. Its strict single-use 0x574F tag preserves the dungeon surface, explicit mode, and canonical absent-or-bounded member so queued work cannot change meaning or retarget. Late validation in dungeon-map or first-person presentation yields only Classic's exact a keyDown 0x00000061 or k keyDown 0x0000286B; only Area Search is rejected by EventManager's non-pumping cached SDL/Classic held-mouse gate. No Classic source or replay vocabulary changes. Classic retains the contextual control, feedback, forced secret search, time, encounters, revert handling, complete scroll modal, spell and parchment effects, and every mutation.",
        "src/presentation/SemanticInputBoundary.cpp::semantic_contextual_overview_tag/RealmzConsumeSemanticContextualOverviewEvent"},
    {"dungeon.action.selected_item_drilldown", Surface::dungeon,
        Kind::interaction, Status::missing,
        "No semantic control replaces Classic's selected-member item drilldown.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/showitembut"},
    {"dungeon.action.contextual_shop_temple_encounter", Surface::dungeon,
        Kind::interaction, Status::missing,
        "No semantic control replaces the context-sensitive Shop, Temple, and seamless-encounter entry point.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/shopbut"},
    {"dungeon.action.pool_money", Surface::dungeon,
        Kind::interaction, Status::missing,
        "No semantic control replaces Classic pooled-money handling.",
        "src/realmz_orig/misc.c::mainscreeninit/swapbut"},
    {"dungeon.action.trade", Surface::dungeon,
        Kind::interaction, Status::missing,
        "No semantic control replaces the Classic Trade command.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/tradebut"},
    {"dungeon.action.heal", Surface::dungeon,
        Kind::interaction, Status::missing,
        "No semantic control replaces the Classic party Heal command.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/barbut"},
    {"dungeon.action.use_scroll", Surface::dungeon,
        Kind::interaction, Status::semantic_complete,
        "The action bar has a typed selected-member scroll-case command with guarded dungeon-map and first-person handoffs.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout/OpenScrollCaseAction"},
    {"dungeon.action.show_conditions", Surface::dungeon,
        Kind::interaction, Status::missing,
        "No semantic control opens the complete Classic condition view.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/showconditionbut"},
    {"dungeon.action.party_auto_toggles", Surface::dungeon,
        Kind::interaction, Status::missing,
        "No semantic controls replace Classic's per-member Auto toggles.",
        "src/realmz_orig/misc.c::mainscreeninit/autoone"},

    // Dungeon essential information.
    {"dungeon.info.world_view", Surface::dungeon,
        Kind::essential_information, Status::retained_in_crop,
        "The dungeon map or first-person view remains inside the isolated Classic viewport.",
        "src/presentation/AdaptiveShell.hpp::kClassicGameplayCrop"},
    {"dungeon.info.party_vitals", Surface::dungeon,
        Kind::essential_information, Status::missing,
        "Code-native party cards omit Classic's per-member AC and spell-point or noncaster attack-cadence readouts.",
        "src/realmz_orig/updatechar.c::updatechar/ac/spellpoints/normattacks"},
    {"dungeon.info.selected_member_details", Surface::dungeon,
        Kind::essential_information, Status::semantic_complete,
        "Wide and compact semantic details present selected-member vitals and states.",
        "src/presentation/SelectedPartyDetailsLayout.cpp::compute_selected_party_details_layout"},
    {"dungeon.info.party_condition_indicators", Surface::dungeon,
        Kind::essential_information, Status::missing,
        "The semantic shell does not present Classic's complete party-wide condition indicators.",
        "src/realmz_orig/tickcheck.c-updatetorch.c::updatetorch/partycondition"},
    {"dungeon.info.narrative_messages", Surface::dungeon,
        Kind::essential_information, Status::missing,
        "The semantic shell is not yet fed the authoritative Classic textrect messages.",
        "src/WindowManager.cpp::present_remastered_frame/event_log"},
    {"dungeon.info.world_coordinates", Surface::dungeon,
        Kind::essential_information, Status::missing,
        "Classic renders floorx and floory; those authoritative dungeon coordinates are not captured and presented semantically.",
        "src/realmz_orig/misc.c::xy/floorx/floory"},
    {"dungeon.info.calendar_clock", Surface::dungeon,
        Kind::essential_information, Status::missing,
        "Day and time shown by Classic chrome are absent from the semantic snapshot and shell.",
        "src/realmz_orig/textbox-time.c::timeclick/tyme/strftime"},
    {"dungeon.info.fatigue", Surface::dungeon,
        Kind::essential_information, Status::missing,
        "Party fatigue is captured but has no complete semantic presentation.",
        "src/presentation/GameSnapshot.hpp::PartyView/fatigue"},
    {"dungeon.info.pooled_money", Surface::dungeon,
        Kind::essential_information, Status::missing,
        "Pooled money is captured but has no complete semantic presentation.",
        "src/presentation/GameSnapshot.hpp::PartyView/pooled_money"},
    {"dungeon.info.search_and_torch_state", Surface::dungeon,
        Kind::essential_information, Status::missing,
        "The Search control presents the current searching state, but the shell still lacks a complete presentation of Classic's persistent Torch state.",
        "src/realmz_orig/buttonchoice.c::buttonchoice/search/torch"},

    // Combat interactions.
    {"combat.viewport.pointer_actions", Surface::combat,
        Kind::interaction, Status::retained_in_crop,
        "Battlefield pointer movement, attacks, and target selection remain in the cropped viewport.",
        "src/realmz_orig/combatinfo-combatchoice.c::combatchoice/movelook"},
    {"combat.action.guard", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded Guard command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.finish", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded Finish command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.delay", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded Delay command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.center_active", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded center-active command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.switch_weapon", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded weapon-switch command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.cycle_focus", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has typed previous and next focus commands.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.open_items", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded combat-items command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.auto_current", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded current-combatant Auto command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.show_range", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded range-display command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.bandage", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded Bandage command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.undo", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded Undo command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.cast_spell", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded combat spellbook command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.target", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded Target command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.escape", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded Escape command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.use_scroll", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded scroll-case command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.center_cursor", Surface::combat,
        Kind::interaction, Status::semantic_complete,
        "The combat deck has a typed guarded center-cursor command.",
        "src/presentation/ShellControlLayout.cpp::compute_shell_control_layout"},
    {"combat.action.inspect_focused_combatant", Surface::combat,
        Kind::interaction, Status::missing,
        "No semantic control opens Classic's focused party-member or monster inspector.",
        "src/realmz_orig/combatinfo-combatchoice.c::combatchoice/monsterbut/lastshown"},
    {"combat.action.inspect_party_member", Surface::combat,
        Kind::interaction, Status::missing,
        "No semantic control reproduces Classic's party-member selection and repeated-click character inspector.",
        "src/realmz_orig/combatinfo-combatchoice.c::combatchoice/charmainbut/charselectold"},
    {"combat.action.inspect_items", Surface::combat,
        Kind::interaction, Status::missing,
        "No semantic control replaces Classic's inspected-combatant items view.",
        "src/realmz_orig/combatinfo-combatchoice.c::combatchoice/showitems"},
    {"combat.action.inspect_conditions", Surface::combat,
        Kind::interaction, Status::missing,
        "No semantic control replaces Classic's inspected-combatant condition view.",
        "src/realmz_orig/combatinfo-combatchoice.c::combatchoice/condition"},
    {"combat.action.inspect_attacks", Surface::combat,
        Kind::interaction, Status::missing,
        "No semantic control replaces Classic's monster attack-list view.",
        "src/realmz_orig/combatinfo-combatchoice.c::combatchoice/attacks"},
    {"combat.action.turn_undead", Surface::combat,
        Kind::interaction, Status::missing,
        "No semantic control replaces Classic's conditional Turn Undead command.",
        "src/realmz_orig/combatinfo-combatchoice.c::combatchoice/turn"},
    {"combat.action.party_auto_toggles", Surface::combat,
        Kind::interaction, Status::missing,
        "The current-actor Auto command does not replace all Classic per-member Auto toggles.",
        "src/realmz_orig/misc.c::mainscreeninit/autoone"},

    // Combat essential information.
    {"combat.info.battlefield", Surface::combat,
        Kind::essential_information, Status::retained_in_crop,
        "The tactical battlefield remains visible inside the isolated Classic viewport.",
        "src/presentation/AdaptiveShell.hpp::kClassicGameplayCrop"},
    {"combat.info.party_vitals", Surface::combat,
        Kind::essential_information, Status::missing,
        "Code-native party cards omit Classic's per-member AC and spell-point or noncaster attack-cadence readouts.",
        "src/realmz_orig/updatechar.c::updatechar/ac/spellpoints/normattacks"},
    {"combat.info.selected_member_details", Surface::combat,
        Kind::essential_information, Status::semantic_complete,
        "Wide and compact semantic details present selected-member vitals and states.",
        "src/presentation/SelectedPartyDetailsLayout.cpp::compute_selected_party_details_layout"},
    {"combat.info.party_condition_indicators", Surface::combat,
        Kind::essential_information, Status::missing,
        "The semantic shell does not present Classic's complete party-wide condition indicators.",
        "src/realmz_orig/tickcheck.c-updatetorch.c::updatetorch/partycondition"},
    {"combat.info.narrative_messages", Surface::combat,
        Kind::essential_information, Status::missing,
        "The semantic shell is not yet fed authoritative Classic combat messages.",
        "src/WindowManager.cpp::present_remastered_frame/event_log"},
    {"combat.info.inspected_combatant", Surface::combat,
        Kind::essential_information, Status::missing,
        "The shell lacks Classic's complete focused party-or-monster statistics panel.",
        "src/realmz_orig/combatupdate-2.c::combatupdate2"},
    {"combat.info.conditions_and_attacks", Surface::combat,
        Kind::essential_information, Status::missing,
        "Complete conditions, carried items, and monster attacks are not presented semantically.",
        "src/realmz_orig/combatinfo-combatchoice.c::combatchoice"},
    {"combat.info.round", Surface::combat,
        Kind::essential_information, Status::missing,
        "Combat round is captured but has no complete semantic presentation.",
        "src/presentation/GameSnapshot.hpp::CombatView/round"},
    {"combat.info.enemies_remaining", Surface::combat,
        Kind::essential_information, Status::missing,
        "The semantic shell does not summarize remaining enemy combatants.",
        "src/presentation/GameSnapshot.hpp::CombatView/combatants"},
});

static_assert(
    compute_inventory_revision(kGameplayChromeCoverageManifest) ==
        kGameplayChromeInventoryRevision,
    "gameplay-chrome manifest changed without updating its reviewed "
    "content-addressed revision");

constexpr std::array<Surface, 3> kSurfaces{
    Surface::exploration,
    Surface::dungeon,
    Surface::combat,
};

constexpr std::array<Kind, 2> kRoleKinds{
    Kind::interaction,
    Kind::essential_information,
};

std::optional<size_t> surface_index(Surface surface) noexcept {
  switch (surface) {
    case Surface::exploration:
      return 0U;
    case Surface::dungeon:
      return 1U;
    case Surface::combat:
      return 2U;
  }
  return std::nullopt;
}

std::optional<size_t> role_kind_index(Kind kind) noexcept {
  switch (kind) {
    case Kind::interaction:
      return 0U;
    case Kind::essential_information:
      return 1U;
  }
  return std::nullopt;
}

bool matches_canonical_role_set(std::span<const Entry> inventory) noexcept {
  if (inventory.size() != kGameplayChromeCoverageManifest.size()) {
    return false;
  }
  for (const auto& canonical : kGameplayChromeCoverageManifest) {
    bool matched = false;
    for (const auto& candidate : inventory) {
      if ((candidate.stable_id == canonical.stable_id) &&
          (candidate.surface == canonical.surface) &&
          (candidate.kind == canonical.kind)) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      return false;
    }
  }
  return true;
}

bool is_known_activity_state(GameplayChromeActivityState state) noexcept {
  switch (state) {
    case GameplayChromeActivityState::inactive:
    case GameplayChromeActivityState::active:
      return true;
    case GameplayChromeActivityState::unknown:
      return false;
  }
  return false;
}

bool is_known_screen_context(ScreenContext screen) noexcept {
  switch (screen) {
    case ScreenContext::title:
    case ScreenContext::party_selection:
    case ScreenContext::party_creation:
    case ScreenContext::exploration:
    case ScreenContext::dungeon:
    case ScreenContext::combat:
    case ScreenContext::inventory:
    case ScreenContext::shop:
    case ScreenContext::encounter:
    case ScreenContext::ending:
      return true;
  }
  return false;
}

bool is_known_world_presentation(WorldPresentation presentation) noexcept {
  switch (presentation) {
    case WorldPresentation::none:
    case WorldPresentation::outdoor:
    case WorldPresentation::dungeon_map:
    case WorldPresentation::dungeon_first_person:
      return true;
  }
  return false;
}

bool is_valid_party_member_id(PartyMemberId member) noexcept {
  return member < 6U;
}

bool is_valid_combatant_id(CombatantId combatant) noexcept {
  return ((combatant >= 0) && (combatant < 6)) ||
      ((combatant >= 10) && (combatant < 110));
}

bool context_variant_values_known(
    const GameplayChromeContextVariant& variant) noexcept {
  if (!variant.surface ||
      !is_known_gameplay_chrome_surface(*variant.surface) ||
      !variant.screen || !is_known_screen_context(*variant.screen) ||
      !variant.world_presentation ||
      !is_known_world_presentation(*variant.world_presentation) ||
      !is_known_activity_state(variant.camp_state) ||
      !is_known_activity_state(variant.nested_modal_state) ||
      !is_known_activity_state(variant.spell_state) ||
      !is_known_activity_state(variant.targeting_state) ||
      !is_known_activity_state(variant.text_entry_state) ||
      !variant.selected_member_known || !variant.acting_combatant_known) {
    return false;
  }
  if (variant.selected_member &&
      !is_valid_party_member_id(*variant.selected_member)) {
    return false;
  }
  return !variant.acting_combatant ||
      is_valid_combatant_id(*variant.acting_combatant);
}

bool is_inactive(GameplayChromeActivityState state) noexcept {
  return state == GameplayChromeActivityState::inactive;
}

bool supports_standard_context_variant(
    const GameplayChromeContextVariant& variant) noexcept {
  if (!context_variant_values_known(variant) ||
      !is_inactive(variant.camp_state) ||
      !is_inactive(variant.nested_modal_state) ||
      !is_inactive(variant.spell_state) ||
      !is_inactive(variant.targeting_state) ||
      !is_inactive(variant.text_entry_state) ||
      !variant.selected_member) {
    return false;
  }

  const auto expected_screen =
      screen_context_for_gameplay_chrome_surface(*variant.surface);
  if (!expected_screen || (*variant.screen != *expected_screen)) {
    return false;
  }

  switch (*variant.surface) {
    case Surface::exploration:
      return (*variant.world_presentation == WorldPresentation::outdoor) &&
          !variant.acting_combatant;
    case Surface::dungeon:
      return ((*variant.world_presentation ==
                  WorldPresentation::dungeon_map) ||
                 (*variant.world_presentation ==
                  WorldPresentation::dungeon_first_person)) &&
          !variant.acting_combatant;
    case Surface::combat:
      return (*variant.world_presentation != WorldPresentation::none) &&
          variant.acting_combatant.has_value();
  }
  return false;
}

} // namespace

bool is_known_gameplay_chrome_surface(Surface surface) noexcept {
  return surface_index(surface).has_value();
}

bool is_known_gameplay_chrome_role_kind(Kind kind) noexcept {
  return role_kind_index(kind).has_value();
}

bool is_known_gameplay_chrome_coverage_status(Status status) noexcept {
  switch (status) {
    case Status::retained_in_crop:
    case Status::semantic_complete:
    case Status::missing:
      return true;
  }
  return false;
}

std::optional<ScreenContext> screen_context_for_gameplay_chrome_surface(
    Surface surface) noexcept {
  switch (surface) {
    case Surface::exploration:
      return ScreenContext::exploration;
    case Surface::dungeon:
      return ScreenContext::dungeon;
    case Surface::combat:
      return ScreenContext::combat;
  }
  return std::nullopt;
}

std::optional<Surface> gameplay_chrome_surface_for_screen_context(
    ScreenContext screen) noexcept {
  switch (screen) {
    case ScreenContext::exploration:
      return Surface::exploration;
    case ScreenContext::dungeon:
      return Surface::dungeon;
    case ScreenContext::combat:
      return Surface::combat;
    case ScreenContext::title:
    case ScreenContext::party_selection:
    case ScreenContext::party_creation:
    case ScreenContext::inventory:
    case ScreenContext::shop:
    case ScreenContext::encounter:
    case ScreenContext::ending:
      return std::nullopt;
  }
  return std::nullopt;
}

std::span<const Entry> gameplay_chrome_coverage_manifest() noexcept {
  return kGameplayChromeCoverageManifest;
}

GameplayChromeInventoryRevision gameplay_chrome_inventory_revision(
    std::span<const Entry> inventory) noexcept {
  return compute_inventory_revision(inventory);
}

GameplayChromeInventoryValidation validate_gameplay_chrome_coverage(
    std::span<const Entry> inventory) noexcept {
  if (inventory.empty()) {
    return {
        .valid = false,
        .issue = GameplayChromeInventoryIssue::empty_inventory,
    };
  }

  std::array<std::array<bool, kRoleKinds.size()>, kSurfaces.size()> coverage{};
  for (size_t index = 0; index < inventory.size(); ++index) {
    const auto& entry = inventory[index];
    if (entry.stable_id.empty()) {
      return {
          .valid = false,
          .issue = GameplayChromeInventoryIssue::empty_stable_id,
          .entry_index = index,
      };
    }
    const auto entry_surface_index = surface_index(entry.surface);
    if (!entry_surface_index) {
      return {
          .valid = false,
          .issue = GameplayChromeInventoryIssue::unknown_surface,
          .entry_index = index,
      };
    }
    const auto entry_kind_index = role_kind_index(entry.kind);
    if (!entry_kind_index) {
      return {
          .valid = false,
          .issue = GameplayChromeInventoryIssue::unknown_role_kind,
          .entry_index = index,
      };
    }
    if (!is_known_gameplay_chrome_coverage_status(entry.status)) {
      return {
          .valid = false,
          .issue = GameplayChromeInventoryIssue::unknown_status,
          .entry_index = index,
      };
    }
    if (entry.evidence.empty()) {
      return {
          .valid = false,
          .issue = GameplayChromeInventoryIssue::empty_evidence,
          .entry_index = index,
      };
    }
    if (entry.source_anchor.empty()) {
      return {
          .valid = false,
          .issue = GameplayChromeInventoryIssue::empty_source_anchor,
          .entry_index = index,
      };
    }

    for (size_t previous = 0; previous < index; ++previous) {
      if (entry == inventory[previous]) {
        return {
            .valid = false,
            .issue = GameplayChromeInventoryIssue::duplicate_entry,
            .entry_index = index,
            .conflicting_entry_index = previous,
        };
      }
      if (entry.stable_id == inventory[previous].stable_id) {
        return {
            .valid = false,
            .issue = GameplayChromeInventoryIssue::duplicate_stable_id,
            .entry_index = index,
            .conflicting_entry_index = previous,
        };
      }
    }

    coverage[*entry_surface_index][*entry_kind_index] = true;
  }

  for (size_t surface = 0; surface < kSurfaces.size(); ++surface) {
    for (size_t kind = 0; kind < kRoleKinds.size(); ++kind) {
      if (!coverage[surface][kind]) {
        return {
            .valid = false,
            .issue =
                GameplayChromeInventoryIssue::incomplete_surface_role_kind,
        };
      }
    }
  }

  return {
      .valid = true,
      .issue = GameplayChromeInventoryIssue::none,
  };
}

GameplayChromeCoverageAssessment assess_gameplay_chrome_coverage(
    Surface surface,
    std::span<const Entry> inventory) noexcept {
  const auto validation = validate_gameplay_chrome_coverage(inventory);
  GameplayChromeCoverageAssessment result{
      .surface = surface,
      .inventory_validation = validation,
      .surface_known = is_known_gameplay_chrome_surface(surface),
      .canonical_role_set_matches =
          validation.valid && matches_canonical_role_set(inventory),
      .inventory_revision = validation.valid
          ? gameplay_chrome_inventory_revision(inventory)
          : 0U,
  };
  if (!result.inventory_validation.valid) {
    return result;
  }

  for (const auto& entry : inventory) {
    if (entry.status == Status::missing) {
      ++result.inventory_missing_count;
      if (entry.kind == Kind::interaction) {
        ++result.inventory_missing_interaction_count;
      } else {
        ++result.inventory_missing_essential_information_count;
      }
    }

    if (result.surface_known && (entry.surface == surface)) {
      switch (entry.kind) {
        case Kind::interaction:
          ++result.interaction_count;
          break;
        case Kind::essential_information:
          ++result.essential_information_count;
          break;
      }

      switch (entry.status) {
        case Status::retained_in_crop:
          ++result.retained_in_crop_count;
          break;
        case Status::semantic_complete:
          ++result.semantic_complete_count;
          break;
        case Status::missing:
          ++result.missing_count;
          if (entry.kind == Kind::interaction) {
            ++result.missing_interaction_count;
          } else {
            ++result.missing_essential_information_count;
          }
          break;
      }
    }
  }

  result.complete =
      result.surface_known &&
      result.canonical_role_set_matches &&
      (result.interaction_count > 0U) &&
      (result.essential_information_count > 0U) &&
      (result.inventory_missing_count == 0U);
  return result;
}

GameplayCropReadiness evaluate_gameplay_crop_readiness(
    Surface surface,
    std::span<const Entry> inventory,
    const GameplayCropRuntimePrerequisites& runtime) noexcept {
  GameplayCropReadiness result{
      .coverage = assess_gameplay_chrome_coverage(surface, inventory),
      .expected_context_variant_known =
          context_variant_values_known(runtime.expected_variant),
      .live_context_variant_known =
          context_variant_values_known(runtime.live_variant),
      .gameplay_window_active = runtime.gameplay_window_active,
      .front_is_gameplay_surface = runtime.front_is_gameplay_surface,
      .legacy_requires_full_frame = runtime.legacy_requires_full_frame,
      .inventory_revision_matches =
          (runtime.expected_inventory_revision != 0U) &&
          (runtime.expected_inventory_revision ==
              kGameplayChromeInventoryRevision) &&
          (result.coverage.inventory_revision ==
              kGameplayChromeInventoryRevision),
      .snapshot_revision_nonzero = runtime.snapshot_revision != 0U,
      .snapshot_revision_matches_shell_model =
          (runtime.snapshot_revision != 0U) &&
          (runtime.snapshot_revision == runtime.shell_model_revision),
      .shell_model_valid = runtime.shell_model_valid,
      .shell_layout_valid = runtime.shell_layout_valid,
      .shell_font_valid = runtime.shell_font_valid,
      .expected_controls_present = runtime.expected_controls_present,
      .live_handlers_present = runtime.live_handlers_present,
      .informational_surfaces_complete =
          runtime.informational_surfaces_complete,
  };

  const auto expected_context =
      screen_context_for_gameplay_chrome_surface(surface);
  result.context_matches_surface = result.expected_context_variant_known &&
      expected_context && runtime.expected_variant.surface &&
      (*runtime.expected_variant.surface == surface) &&
      runtime.expected_variant.screen &&
      (*runtime.expected_variant.screen == *expected_context);
  result.context_variants_exact_match =
      result.expected_context_variant_known &&
      result.live_context_variant_known &&
      (runtime.expected_variant == runtime.live_variant);
  result.standard_context_variant_supported =
      result.context_variants_exact_match &&
      supports_standard_context_variant(runtime.expected_variant);
  result.snapshot_matches_context = result.live_context_variant_known &&
      runtime.live_variant.screen &&
      (runtime.snapshot_context == *runtime.live_variant.screen);
  result.shell_model_matches_snapshot_context =
      gameplay_chrome_surface_for_screen_context(runtime.snapshot_context)
          .has_value() &&
      (runtime.shell_model_context == runtime.snapshot_context);
  result.legacy_full_frame_clear = result.gameplay_window_active &&
      result.front_is_gameplay_surface &&
      !result.legacy_requires_full_frame;
  result.runtime_prerequisites_complete =
      result.expected_context_variant_known &&
      result.live_context_variant_known && result.context_matches_surface &&
      result.context_variants_exact_match &&
      result.standard_context_variant_supported &&
      result.snapshot_matches_context &&
      result.shell_model_matches_snapshot_context &&
      result.legacy_full_frame_clear &&
      result.inventory_revision_matches &&
      result.snapshot_revision_nonzero &&
      result.snapshot_revision_matches_shell_model &&
      result.shell_model_valid && result.shell_layout_valid &&
      result.shell_font_valid && result.expected_controls_present &&
      result.live_handlers_present &&
      result.informational_surfaces_complete;
  result.ready = result.coverage.complete &&
      result.runtime_prerequisites_complete;
  return result;
}

GameplayCropReadiness evaluate_current_gameplay_crop_readiness(
    Surface surface,
    const GameplayCropRuntimePrerequisites& runtime) noexcept {
  return evaluate_gameplay_crop_readiness(
      surface, gameplay_chrome_coverage_manifest(), runtime);
}

} // namespace realmz::presentation
