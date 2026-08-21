#pragma once

#include "replay/ReplayChildConfig.hpp"
#include "replay/Sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace realmz::replay {

inline constexpr std::size_t kReplayPartyMemberCount = 6;
inline constexpr std::size_t kReplayInventoryCapacity = 30;
inline constexpr std::size_t kReplayEquipmentSlotCount = 20;
inline constexpr std::size_t kReplayScrollSlotCount = 5;
inline constexpr std::size_t kReplayMaximumCombatMonsters = 100;
inline constexpr std::uint32_t kMaximumReplayTraceActions =
    static_cast<std::uint32_t>(kMaximumReplayActions);

struct ReplayScreenState final {
  std::int32_t view_type = 0;
  bool in_camp = false;
  bool in_items = false;
  bool in_swap = false;
  bool in_booty = false;
  bool in_shop = false;
  bool in_temple = false;
  bool in_scroll = false;
  bool in_spell = false;

  bool operator==(const ReplayScreenState&) const = default;
};

struct ReplayWorldState final {
  std::int32_t world_x = 0;
  std::int32_t world_y = 0;
  std::int32_t saved_x = 0;
  std::int32_t saved_y = 0;
  std::int32_t party_x = 0;
  std::int32_t party_y = 0;
  std::int32_t facing_delta_x = 0;
  std::int32_t facing_delta_y = 0;
  std::int32_t land_level = 0;
  std::int32_t dungeon_level = 0;
  std::int32_t saved_land_type = 0;
  std::int32_t saved_land_level = 0;
  bool in_dungeon = false;
  bool dungeon_view = false;
  bool dungeon_update_pending = false;
  bool multi_view = false;

  bool operator==(const ReplayWorldState&) const = default;
};

struct ReplayItemState final {
  std::int32_t item_id = 0;
  bool equipped = false;
  bool identified = false;
  std::int32_t charges = 0;

  bool operator==(const ReplayItemState&) const = default;
};

struct ReplayScrollState final {
  std::int32_t caste = 0;
  std::int32_t level = 0;
  std::int32_t spell = 0;
  std::int32_t power = 0;

  bool operator==(const ReplayScrollState&) const = default;
};

struct ReplayPartyMemberState final {
  bool occupied = false;
  std::int32_t stamina = 0;
  std::int32_t maximum_stamina = 0;
  std::array<std::uint32_t, 3> money{};
  std::int32_t weapon_configuration = 0;
  std::int32_t missile_configuration = 0;
  bool alternate_weapon_set = false;
  std::uint32_t inventory_count = 0;
  std::array<ReplayItemState, kReplayInventoryCapacity> inventory{};
  std::array<std::int32_t, kReplayEquipmentSlotCount> equipment{};
  std::array<ReplayScrollState, kReplayScrollSlotCount> scrolls{};

  bool operator==(const ReplayPartyMemberState&) const = default;
};

struct ReplayPartyState final {
  std::int32_t selected_member = 0;
  std::int32_t fatigue = 0;
  std::array<std::int32_t, 3> pooled_money{};
  std::array<ReplayPartyMemberState, kReplayPartyMemberCount> members{};

  bool operator==(const ReplayPartyState&) const = default;
};

struct ReplayEncounterState final {
  std::int32_t marker_id = 0;
  std::int32_t active_kind = 0;
  std::int32_t remaining_uses = 0;
  std::int32_t result_code = 0;
  bool encounters_enabled = false;

  bool operator==(const ReplayEncounterState&) const = default;
};

struct ReplayGridPosition final {
  std::int32_t x = 0;
  std::int32_t y = 0;

  bool operator==(const ReplayGridPosition&) const = default;
};

struct ReplayCombatState final {
  bool active = false;
  bool monster_turn = false;
  std::int32_t round = 0;
  std::int32_t turn_queue_index = 0;
  std::int32_t active_party_member = 0;
  std::int32_t active_monster = 0;
  std::array<ReplayGridPosition, kReplayPartyMemberCount> party_positions{};
  std::uint32_t monster_count = 0;
  std::array<ReplayGridPosition, kReplayMaximumCombatMonsters>
      monster_positions{};

  bool operator==(const ReplayCombatState&) const = default;
};

struct ReplayTimeState final {
  std::int32_t seconds = 0;
  std::int32_t minutes = 0;
  std::int32_t hours = 0;
  std::int32_t day_of_month = 0;
  std::int32_t month = 0;
  std::int32_t years_since_1900 = 0;
  std::int32_t day_of_week = 0;
  std::int32_t day_of_year = 0;
  std::int32_t daylight_saving = 0;

  bool operator==(const ReplayTimeState&) const = default;
};

// A value-only representation of live state relevant to semantic replay.
// Capturing this value from Realmz's legacy globals is deliberately a separate
// integration concern: this layer never includes or hashes a legacy struct.
struct ReplayStateSnapshot final {
  std::int32_t scenario_id = 0;
  ReplayScreenState screen;
  ReplayWorldState world;
  ReplayPartyState party;
  ReplayEncounterState encounter;
  ReplayCombatState combat;
  ReplayTimeState time;

  bool operator==(const ReplayStateSnapshot&) const = default;
};

class ReplayStateOracleError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Produces the exact canonical state-v1 byte stream. Every numeric field is
// explicitly encoded; object representation, padding, host byte order, and
// sizeof(bool) never enter the stream. Signed int32 values are encoded modulo
// 2^32, most-significant byte first, and booleans are one byte (00 or 01).
//
// Framing, in exact order:
//
//   ASCII "realmz.semantic-replay.state.v1", byte 00
//   byte 01 (scenario/screen section)
//     int32_be scenario_id, int32_be view_type
//     8 bool bytes: camp, items, swap, booty, shop, temple, scroll, spell
//   byte 02 (world section)
//     12 int32_be values: world x/y, saved x/y, party x/y, facing dx/dy,
//       land level, dungeon level, saved land type, saved land level
//     4 bool bytes: in dungeon, dungeon view, update pending, multi-view
//   byte 03 (party section)
//     int32_be selected member, int32_be fatigue, 3 int32_be pooled money
//     uint32_be party count (always 6)
//     for each party slot in index order:
//       bool occupied, int32_be stamina, int32_be maximum stamina
//       3 uint32_be personal money values
//       int32_be weapon configuration, int32_be missile configuration
//       bool alternate weapon set
//       uint32_be logical inventory count, then that many items in slot order:
//         int32_be id, bool equipped, bool identified, int32_be charges
//       uint32_be equipment slot count (always 20), then 20 int32_be item ids
//       uint32_be scroll slot count (always 5), then for each scroll:
//         int32_be caste, int32_be level, int32_be spell, int32_be power
//   byte 04 (encounter section)
//     4 int32_be values: marker id, active kind, remaining uses, result code
//     bool encounters enabled
//   byte 05 (combat section)
//     bool active, bool monster turn
//     4 int32_be values: round, turn queue index, active party, active monster
//     uint32_be party position count (always 6), then x/y int32_be pairs
//     uint32_be logical monster count, then that many x/y int32_be pairs
//   byte 06 (time section)
//     9 int32_be values in std::tm order: sec, min, hour, mday, mon, year,
//       wday, yday, isdst
//   byte FF (end marker)
//
// Only inventory[0..inventory_count) and
// monster_positions[0..monster_count) are logical and encoded. Oversized
// counts throw ReplayStateOracleError.
[[nodiscard]] std::vector<std::byte> encode_replay_state_v1(
    const ReplayStateSnapshot& snapshot);

[[nodiscard]] Sha256Digest replay_state_sha256_v1(
    const ReplayStateSnapshot& snapshot);

// Incrementally hashes one complete replay state trace. The constructor emits:
//
//   ASCII "realmz.semantic-replay.state-trace.v1", byte 00
//   uint32_be expected settled-action count
//   uint32_be expected checkpoint count (action count + 1)
//
// append_initial emits tag 00, uint32_be checkpoint index 0, and the raw
// 32-byte state-v1 digest. Each append_post_action(N, ...) must use the next
// zero-based action index and emits tag 01, uint32_be checkpoint index N + 1,
// and the raw digest. finalize() succeeds only after the initial checkpoint
// and every expected post-action checkpoint have been appended. Thus skipped,
// duplicated, reordered, or extra checkpoints fail closed.
class ReplayStateTraceHasher final {
public:
  explicit ReplayStateTraceHasher(std::uint32_t expected_action_count);

  void append_initial(const ReplayStateSnapshot& snapshot);
  void append_post_action(
      std::uint32_t action_index,
      const ReplayStateSnapshot& snapshot);

  [[nodiscard]] std::uint32_t expected_action_count() const noexcept;
  [[nodiscard]] std::uint32_t appended_action_count() const noexcept;
  [[nodiscard]] bool has_initial() const noexcept;
  [[nodiscard]] bool complete() const noexcept;
  [[nodiscard]] Sha256Digest finalize() const;

private:
  Sha256 hash_;
  std::uint32_t expected_action_count_ = 0;
  std::uint32_t appended_action_count_ = 0;
  bool has_initial_ = false;
};

[[nodiscard]] Sha256Digest replay_state_trace_sha256_v1(
    const ReplayStateSnapshot& initial,
    std::span<const ReplayStateSnapshot> post_action_checkpoints);

} // namespace realmz::replay
