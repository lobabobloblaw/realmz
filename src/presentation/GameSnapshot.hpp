#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace realmz::presentation {

// These DTOs deliberately contain values only: no pointers, handles, FILEs,
// SDL objects, or references to the binary-compatible legacy structs. A UI can
// retain or copy a GameSnapshot without gaining a mutation path into the engine.

using SnapshotRevision = uint64_t;
using PartyMemberId = uint8_t;
using CombatantId = int32_t;
using ItemInstanceId = uint32_t;

// Identifies the exact party inventory position that Classic's party-scoped
// Torch control would consume first. The source is a freshness locator only;
// Classic remains authoritative for charge use, removal, and every resulting
// Light effect.
struct TorchSource {
  PartyMemberId member = 0;
  uint8_t slot = 0;

  bool operator==(const TorchSource&) const = default;
};

struct MeterView {
  int32_t current = 0;
  int32_t maximum = 0;

  bool operator==(const MeterView&) const = default;

  [[nodiscard]] bool is_valid() const noexcept {
    return (this->maximum >= 0) &&
        (this->current <= this->maximum);
  }
};

// Typed, value-only identities for Classic's eight party-wide conditions.
// The enum order deliberately matches partycondition[1] through [8]; the
// snapshot still captures each signed raw value explicitly rather than relying
// on the underlying enum representation.
enum class PartyEffectKind : uint8_t {
  waterworld = 1,
  dragon_hide = 2,
  discover_secret = 3,
  wizard_eye = 4,
  search = 5,
  free_fall_levitate = 6,
  sentry = 7,
  charm_resistance = 8,
};

struct PartyEffectView {
  PartyEffectKind kind = PartyEffectKind::waterworld;
  int16_t raw_value = 0;

  bool operator==(const PartyEffectView&) const = default;
};

struct PartyMemberView {
  PartyMemberId id = 0;
  std::string name;
  int16_t level = 0;
  int16_t race_id = 0;
  int16_t caste_id = 0;
  int32_t portrait_id = 0;
  int32_t tactical_id = 0;
  MeterView stamina;
  MeterView spell_points;
  int16_t armor_class = 0;
  int16_t movement = 0;
  int16_t movement_maximum = 0;
  std::vector<int16_t> conditions;
  bool selected = false;
  bool conscious = true;
  // Read-only projection of Classic's non-combat scroll-case opening gate:
  // not already in a spell flow, alive, and an equipped case in armor[13].
  // Case-slot contents deliberately remain Classic-owned.
  bool use_scroll_available = false;
  // Raw Classic attack cadence inputs. Presentation derives the displayed
  // cadence from these detached values and the active condition identifiers.
  int16_t normal_attacks = 0;
  int16_t attack_bonus = 0;

  bool operator==(const PartyMemberView&) const = default;
};

struct PartyView {
  std::vector<PartyMemberView> members;
  std::optional<PartyMemberId> selected_member;
  std::array<int32_t, 3> pooled_money{};
  int16_t fatigue = 0;
  // Fixed projection of partycondition[1] through [8]. Indices 0 and 9 are
  // intentionally outside this semantic party-status sequence.
  std::array<PartyEffectView, 8> effects{
      PartyEffectView{PartyEffectKind::waterworld, 0},
      PartyEffectView{PartyEffectKind::dragon_hide, 0},
      PartyEffectView{PartyEffectKind::discover_secret, 0},
      PartyEffectView{PartyEffectKind::wizard_eye, 0},
      PartyEffectView{PartyEffectKind::search, 0},
      PartyEffectView{PartyEffectKind::free_fall_levitate, 0},
      PartyEffectView{PartyEffectKind::sentry, 0},
      PartyEffectView{PartyEffectKind::charm_resistance, 0},
  };

  bool operator==(const PartyView&) const = default;

  [[nodiscard]] const PartyMemberView* member(PartyMemberId id) const noexcept {
    for (const auto& candidate : this->members) {
      if (candidate.id == id) {
        return &candidate;
      }
    }
    return nullptr;
  }
};

enum class WorldPresentation {
  none,
  outdoor,
  dungeon_map,
  dungeon_first_person,
};

enum class Facing {
  north,
  east,
  south,
  west,
};

struct WorldTileView {
  int32_t terrain_id = 0;
  int32_t overlay_id = 0;
  bool visible = false;
  bool explored = false;
  bool blocks_movement = false;

  bool operator==(const WorldTileView&) const = default;
};

// Value-only meaning of Classic's single contextual world-entry control. The
// unavailable state is presentation evidence only and must never be dispatched;
// the three executable modes preserve the branch selected by Classic's live
// shopavail/templeavail priority.
enum class ContextualWorldEntryMode {
  unavailable,
  shop,
  temple,
  encounter,
};

struct WorldView {
  WorldPresentation presentation = WorldPresentation::none;
  int32_t party_x = 0;
  int32_t party_y = 0;
  int32_t land_level = 0;
  int32_t dungeon_level = 0;
  Facing facing = Facing::north;
  size_t visible_columns = 0;
  size_t visible_rows = 0;
  std::vector<WorldTileView> visible_tiles;
  // Value-only projection of Classic's authoritative camp state. Appending
  // it preserves positional source compatibility for existing snapshots.
  // Presentation may gate Rest with it, but cannot mutate Classic state.
  bool in_camp = false;
  // Value-only projection of Classic's persistent Search condition. Any
  // nonzero legacy value is active; presentation cannot normalize or mutate it.
  bool searching = false;
  // The first exact Torch item that Classic can currently consume, including
  // its party member and inventory slot. A disengaged value means no usable
  // first Torch source exists. Appending preserves aggregate source
  // compatibility for prior WorldView clients.
  std::optional<TorchSource> usable_torch_source;
  // Exact meaning of Classic's contextual Shop/Temple/Encounter entry point.
  // Appending preserves positional source compatibility for existing snapshots.
  ContextualWorldEntryMode contextual_world_entry_mode =
      ContextualWorldEntryMode::unavailable;

  bool operator==(const WorldView&) const = default;

  [[nodiscard]] bool has_complete_tile_grid() const noexcept {
    if ((this->visible_columns == 0) || (this->visible_rows == 0)) {
      return this->visible_tiles.empty();
    }
    return this->visible_columns <=
            (static_cast<size_t>(-1) / this->visible_rows) &&
        (this->visible_tiles.size() ==
            this->visible_columns * this->visible_rows);
  }

  [[nodiscard]] const WorldTileView* tile_at(size_t column, size_t row) const noexcept {
    if (!this->has_complete_tile_grid() ||
        (column >= this->visible_columns) ||
        (row >= this->visible_rows)) {
      return nullptr;
    }
    return &this->visible_tiles[row * this->visible_columns + column];
  }
};

enum class CombatantKind {
  party_member,
  ally,
  monster,
};

struct CombatantView {
  CombatantId id = 0;
  CombatantKind kind = CombatantKind::monster;
  std::string name;
  int16_t cell_x = 0;
  int16_t cell_y = 0;
  MeterView stamina;
  std::vector<int16_t> conditions;
  bool active = false;
  bool targetable = false;

  bool operator==(const CombatantView&) const = default;
};

struct CombatView {
  bool active = false;
  // Value-only copy of Classic's canundo gate, which also controls whether
  // the current acting party member may enter the Bandage target picker.
  bool bandage_available = false;
  // Independently named capability projection of Classic's canundo gate for
  // the preserved combat Undo command. Keeping this distinct prevents UI code
  // from coupling Undo eligibility to Bandage presentation state.
  bool undo_available = false;
  // Read-only projection of Classic's cancast gate for the current acting
  // party combatant. The preserved chooser repeats the authoritative checks.
  bool cast_spell_available = false;
  // Read-only projection of Classic's visible Target-button gate for the
  // current acting party combatant. Classic re-resolves equipment, targeting,
  // costs, RNG, and every mutation after the key handoff.
  bool target_available = false;
  // Read-only projection of Classic's visible combat Scroll gate. Classic
  // remains authoritative for the chooser, contents, targeting, consumption,
  // costs, RNG, and every mutation after the key handoff.
  bool use_scroll_available = false;
  int16_t round = 0;
  int32_t field_origin_x = 0;
  int32_t field_origin_y = 0;
  size_t visible_columns = 0;
  size_t visible_rows = 0;
  std::optional<CombatantId> acting_combatant;
  std::vector<CombatantView> combatants;

  bool operator==(const CombatView&) const = default;
};

struct InventoryItemView {
  ItemInstanceId instance_id = 0;
  int32_t item_id = 0;
  std::string name;
  int16_t slot = 0;
  int16_t quantity = 1;
  int16_t charges = 0;
  bool equipped = false;
  bool identified = false;
  bool cursed = false;
  bool usable = false;

  bool operator==(const InventoryItemView&) const = default;
};

struct InventoryView {
  std::optional<PartyMemberId> owner;
  std::vector<InventoryItemView> items;
  int32_t carried_weight = 0;
  int32_t maximum_weight = 0;

  bool operator==(const InventoryView&) const = default;
};

struct EncounterChoiceView {
  int32_t id = 0;
  std::string label;
  bool enabled = true;

  bool operator==(const EncounterChoiceView&) const = default;
};

struct EncounterView {
  bool active = false;
  int32_t encounter_id = 0;
  std::string prompt;
  std::vector<EncounterChoiceView> choices;
  bool can_cancel = false;

  bool operator==(const EncounterView&) const = default;
};

enum class ScreenContext {
  title,
  party_selection,
  party_creation,
  exploration,
  dungeon,
  combat,
  inventory,
  shop,
  encounter,
  ending,
};

struct GameSnapshot {
  SnapshotRevision revision = 0;
  ScreenContext screen = ScreenContext::title;
  int16_t scenario_id = 0;
  PartyView party;
  WorldView world;
  std::optional<CombatView> combat;
  std::optional<InventoryView> inventory;
  std::optional<EncounterView> encounter;

  bool operator==(const GameSnapshot&) const = default;
};

class GameSnapshotSource {
public:
  virtual ~GameSnapshotSource() = default;
  [[nodiscard]] virtual GameSnapshot capture() const = 0;
};

} // namespace realmz::presentation
