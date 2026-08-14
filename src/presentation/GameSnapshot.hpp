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

struct MeterView {
  int32_t current = 0;
  int32_t maximum = 0;

  bool operator==(const MeterView&) const = default;

  [[nodiscard]] bool is_valid() const noexcept {
    return (this->maximum >= 0) &&
        (this->current <= this->maximum);
  }
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

  bool operator==(const PartyMemberView&) const = default;
};

struct PartyView {
  std::vector<PartyMemberView> members;
  std::optional<PartyMemberId> selected_member;
  std::array<int32_t, 3> pooled_money{};
  int16_t fatigue = 0;

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
  int16_t round = 0;
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
