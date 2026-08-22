#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "GameSnapshot.hpp"

namespace realmz::presentation {

using GameplayChromeInventoryRevision = uint64_t;

// This content-addressed revision is the FNV-1a digest of every ordered
// manifest field: stable role ID, surface, role kind, coverage status,
// evidence, and source anchor. The implementation statically verifies the
// digest. A cited Classic UI/control-map behavior change must update its
// evidence or anchor, which necessarily produces a new reviewed revision.
inline constexpr GameplayChromeInventoryRevision
    kGameplayChromeInventoryRevision = 0x1FB74F42D95EB551ULL;

// The three Classic gameplay surfaces whose 480x416 viewport can eventually
// be isolated from the surrounding 800x600 chrome.
enum class GameplayChromeSurface {
  exploration,
  dungeon,
  combat,
};

// Interactions are commands a player must be able to invoke. Essential
// information is state the player must still be able to perceive after the
// Classic frame is cropped.
enum class GameplayChromeRoleKind {
  interaction,
  essential_information,
};

// A covered role either remains inside the isolated Classic viewport or has a
// complete semantic replacement. Missing is deliberately a first-class state:
// an unclassified or aspirational role must never make crop readiness true.
enum class GameplayChromeCoverageStatus {
  retained_in_crop,
  semantic_complete,
  missing,
};

struct GameplayChromeCoverageEntry {
  std::string_view stable_id;
  GameplayChromeSurface surface = GameplayChromeSurface::exploration;
  GameplayChromeRoleKind kind = GameplayChromeRoleKind::interaction;
  GameplayChromeCoverageStatus status =
      GameplayChromeCoverageStatus::missing;
  std::string_view evidence;
  std::string_view source_anchor;

  bool operator==(const GameplayChromeCoverageEntry&) const = default;
};

enum class GameplayChromeInventoryIssue {
  none,
  empty_inventory,
  empty_stable_id,
  empty_evidence,
  empty_source_anchor,
  unknown_surface,
  unknown_role_kind,
  unknown_status,
  duplicate_entry,
  duplicate_stable_id,
  incomplete_surface_role_kind,
};

inline constexpr size_t kNoGameplayChromeEntry = static_cast<size_t>(-1);

struct GameplayChromeInventoryValidation {
  bool valid = false;
  GameplayChromeInventoryIssue issue =
      GameplayChromeInventoryIssue::empty_inventory;
  size_t entry_index = kNoGameplayChromeEntry;
  size_t conflicting_entry_index = kNoGameplayChromeEntry;

  bool operator==(const GameplayChromeInventoryValidation&) const = default;
};

[[nodiscard]] bool is_known_gameplay_chrome_surface(
    GameplayChromeSurface surface) noexcept;
[[nodiscard]] bool is_known_gameplay_chrome_role_kind(
    GameplayChromeRoleKind kind) noexcept;
[[nodiscard]] bool is_known_gameplay_chrome_coverage_status(
    GameplayChromeCoverageStatus status) noexcept;

[[nodiscard]] std::optional<ScreenContext>
screen_context_for_gameplay_chrome_surface(
    GameplayChromeSurface surface) noexcept;
[[nodiscard]] std::optional<GameplayChromeSurface>
gameplay_chrome_surface_for_screen_context(ScreenContext screen) noexcept;

// The returned span has static storage duration and stable order. It is a
// read-only migration ledger, not runtime state.
[[nodiscard]] std::span<const GameplayChromeCoverageEntry>
gameplay_chrome_coverage_manifest() noexcept;

// Returns the ordered, full-field content revision used by the reviewed
// manifest. Invalid candidate inventories may still be hashed for diagnostics;
// validation remains a separate fail-closed operation.
[[nodiscard]] GameplayChromeInventoryRevision
gameplay_chrome_inventory_revision(
    std::span<const GameplayChromeCoverageEntry> inventory) noexcept;

// Validation is intentionally independent of the built-in manifest so tests
// and future migration tooling can audit candidate inventories before use.
[[nodiscard]] GameplayChromeInventoryValidation
validate_gameplay_chrome_coverage(
    std::span<const GameplayChromeCoverageEntry> inventory) noexcept;

struct GameplayChromeCoverageAssessment {
  GameplayChromeSurface surface = GameplayChromeSurface::exploration;
  GameplayChromeInventoryValidation inventory_validation;
  bool surface_known = false;
  bool canonical_role_set_matches = false;
  GameplayChromeInventoryRevision inventory_revision = 0U;
  size_t interaction_count = 0;
  size_t essential_information_count = 0;
  size_t retained_in_crop_count = 0;
  size_t semantic_complete_count = 0;
  size_t missing_count = 0;
  size_t missing_interaction_count = 0;
  size_t missing_essential_information_count = 0;
  // Active-surface counts above remain useful diagnostics. These inventory-wide
  // counts are the fail-closed coverage gate: no surface may hide a missing row
  // merely because another surface is currently active.
  size_t inventory_missing_count = 0;
  size_t inventory_missing_interaction_count = 0;
  size_t inventory_missing_essential_information_count = 0;
  bool complete = false;

  bool operator==(const GameplayChromeCoverageAssessment&) const = default;
};

[[nodiscard]] GameplayChromeCoverageAssessment
assess_gameplay_chrome_coverage(
    GameplayChromeSurface surface,
    std::span<const GameplayChromeCoverageEntry> inventory) noexcept;

// Tri-state flow facts preserve unknown as a fail-closed value instead of
// collapsing it into inactive. Only the standard, non-nested gameplay variant
// is currently supported by the crop-readiness contract.
enum class GameplayChromeActivityState {
  unknown,
  inactive,
  active,
};

// Expected and live variants are compared exactly. Optional surface, screen,
// and world-presentation fields represent unknown values, not wildcards.
// Identity-known flags distinguish a known absence from an uncaptured value.
struct GameplayChromeContextVariant {
  std::optional<GameplayChromeSurface> surface;
  std::optional<ScreenContext> screen;
  std::optional<WorldPresentation> world_presentation;
  GameplayChromeActivityState camp_state =
      GameplayChromeActivityState::unknown;
  GameplayChromeActivityState nested_modal_state =
      GameplayChromeActivityState::unknown;
  GameplayChromeActivityState spell_state =
      GameplayChromeActivityState::unknown;
  GameplayChromeActivityState targeting_state =
      GameplayChromeActivityState::unknown;
  GameplayChromeActivityState text_entry_state =
      GameplayChromeActivityState::unknown;
  bool selected_member_known = false;
  std::optional<PartyMemberId> selected_member;
  bool acting_combatant_known = false;
  std::optional<CombatantId> acting_combatant;

  bool operator==(const GameplayChromeContextVariant&) const = default;
};

// Runtime facts are value-only inputs. Defaults are deliberately insufficient
// for readiness: both typed context variants and every granular host/model fact
// must be supplied and pass derived checks.
struct GameplayCropRuntimePrerequisites {
  GameplayChromeContextVariant expected_variant;
  GameplayChromeContextVariant live_variant;
  ScreenContext snapshot_context = ScreenContext::title;
  ScreenContext shell_model_context = ScreenContext::title;
  bool gameplay_window_active = false;
  bool front_is_gameplay_surface = false;
  bool legacy_requires_full_frame = true;
  GameplayChromeInventoryRevision expected_inventory_revision = 0U;
  SnapshotRevision snapshot_revision = 0U;
  SnapshotRevision shell_model_revision = 0U;
  bool shell_model_valid = false;
  bool shell_layout_valid = false;
  bool shell_font_valid = false;
  bool expected_controls_present = false;
  bool live_handlers_present = false;
  bool informational_surfaces_complete = false;

  bool operator==(const GameplayCropRuntimePrerequisites&) const = default;
};

struct GameplayCropReadiness {
  GameplayChromeCoverageAssessment coverage;
  bool expected_context_variant_known = false;
  bool live_context_variant_known = false;
  bool context_matches_surface = false;
  bool context_variants_exact_match = false;
  bool standard_context_variant_supported = false;
  bool snapshot_matches_context = false;
  bool shell_model_matches_snapshot_context = false;
  bool gameplay_window_active = false;
  bool front_is_gameplay_surface = false;
  bool legacy_requires_full_frame = true;
  bool legacy_full_frame_clear = false;
  bool inventory_revision_matches = false;
  bool snapshot_revision_nonzero = false;
  bool snapshot_revision_matches_shell_model = false;
  bool shell_model_valid = false;
  bool shell_layout_valid = false;
  bool shell_font_valid = false;
  bool expected_controls_present = false;
  bool live_handlers_present = false;
  bool informational_surfaces_complete = false;
  bool runtime_prerequisites_complete = false;
  bool ready = false;

  bool operator==(const GameplayCropReadiness&) const = default;
};

[[nodiscard]] GameplayCropReadiness evaluate_gameplay_crop_readiness(
    GameplayChromeSurface surface,
    std::span<const GameplayChromeCoverageEntry> inventory,
    const GameplayCropRuntimePrerequisites& runtime) noexcept;

[[nodiscard]] GameplayCropReadiness evaluate_current_gameplay_crop_readiness(
    GameplayChromeSurface surface,
    const GameplayCropRuntimePrerequisites& runtime) noexcept;

} // namespace realmz::presentation
