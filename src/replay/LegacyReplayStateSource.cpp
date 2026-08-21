#include "replay/LegacyReplayStateSource.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

extern "C" {
#include "realmz_orig/structs.h"

extern short currentscenario;
extern short fat;
extern short incombat;
extern short inspell;
extern short monsterturn;
extern short nummon;
extern short enctry;
extern short reply;
extern int32_t partyx;
extern int32_t partyy;
extern int32_t landlevel;
extern int32_t dunglevel;
extern int32_t lookx;
extern int32_t looky;
extern int32_t floorx;
extern int32_t floory;
extern int32_t moneypool[3];
extern int32_t doorid;
extern char charnum;
extern char charselectnew;
extern char charup;
extern char monsterup;
extern char combatround;
extern char up;
extern char head;
extern char encountflag;
extern char viewtype;
extern char savedpostion[2];
extern char savedlandtype;
extern char savedlandlevel;
extern char multiview;
extern char canencounter;
extern Boolean incamp;
extern Boolean initems;
extern Boolean inswap;
extern Boolean inbooty;
extern Boolean inshop;
extern Boolean intemple;
extern Boolean inscroll;
extern Boolean indung;
extern Boolean view;
extern Boolean needdungeonupdate;
extern struct character c[6];
extern char pos[6][2];
extern char monpos[100][2];
extern struct tm tyme;
}

namespace realmz::replay {

namespace {

static_assert(
    sizeof(c) / sizeof(c[0]) == kReplayPartyMemberCount);
static_assert(
    sizeof(c[0].items) / sizeof(c[0].items[0]) ==
    kReplayInventoryCapacity);
static_assert(
    sizeof(c[0].armor) / sizeof(c[0].armor[0]) ==
    kReplayEquipmentSlotCount);
static_assert(
    sizeof(c[0].scrollcase) / sizeof(c[0].scrollcase[0]) ==
    kReplayScrollSlotCount);
static_assert(
    sizeof(monpos) / sizeof(monpos[0]) ==
    kReplayMaximumCombatMonsters);

[[nodiscard]] constexpr std::int32_t signed_legacy_byte(
    char value) noexcept {
  const auto raw = static_cast<std::uint8_t>(
      static_cast<unsigned char>(value));
  if (raw <= 0x7FU) {
    return static_cast<std::int32_t>(raw);
  }
  return static_cast<std::int32_t>(raw) - 0x100;
}

[[nodiscard]] std::int32_t checked_coordinate_sum(
    std::int32_t left,
    std::int32_t right,
    const char* coordinate_name) {
  const std::int64_t sum =
      static_cast<std::int64_t>(left) + static_cast<std::int64_t>(right);
  if (sum < std::numeric_limits<std::int32_t>::min() ||
      sum > std::numeric_limits<std::int32_t>::max()) {
    throw LegacyReplayStateCaptureError(
        std::string("legacy replay ") + coordinate_name +
        " coordinate is outside int32 range");
  }
  return static_cast<std::int32_t>(sum);
}

[[nodiscard]] ReplayGridPosition facing_delta_for_head(
    char raw_head,
    bool reject_invalid) {
  switch (signed_legacy_byte(raw_head)) {
    case 1:
      return {.x = 0, .y = -1};
    case 2:
      return {.x = 1, .y = 0};
    case 3:
      return {.x = 0, .y = 1};
    case 4:
      return {.x = -1, .y = 0};
    default:
      if (reject_invalid) {
        throw LegacyReplayStateCaptureError(
            "legacy replay facing head must be in [1, 4]");
      }
      return {.x = 0, .y = -1};
  }
}

[[nodiscard]] int checked_party_last_index() {
  const std::int32_t value = signed_legacy_byte(charnum);
  if (value < -1 ||
      value >= static_cast<std::int32_t>(kReplayPartyMemberCount)) {
    throw LegacyReplayStateCaptureError(
        "legacy replay charnum must be in [-1, 5]");
  }
  return static_cast<int>(value);
}

[[nodiscard]] std::uint32_t checked_inventory_count(
    short value,
    std::size_t member_index) {
  if (value < 0 ||
      value > static_cast<short>(kReplayInventoryCapacity)) {
    throw LegacyReplayStateCaptureError(
        "legacy replay party member " + std::to_string(member_index) +
        " inventory count must be in [0, 30]");
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t checked_monster_count() {
  if (nummon < 0 ||
      nummon > static_cast<short>(kReplayMaximumCombatMonsters)) {
    throw LegacyReplayStateCaptureError(
        "legacy replay monster count must be in [0, 100]");
  }
  return static_cast<std::uint32_t>(nummon);
}

} // namespace

ReplayStateSnapshot LegacyReplayStateSource::capture() const {
  const int party_last_index = checked_party_last_index();
  const std::uint32_t monster_count = checked_monster_count();
  const bool is_in_dungeon = indung != 0;
  // `head` is meaningful only for the first-person dungeon renderer. Outdoor
  // saves can legitimately persist its zero-initialized value, so use the
  // production snapshot adapter's north fallback outside a dungeon while
  // still rejecting corrupt live dungeon orientation.
  const ReplayGridPosition facing =
      facing_delta_for_head(head, is_in_dungeon);

  ReplayStateSnapshot snapshot;
  snapshot.scenario_id = static_cast<std::int32_t>(currentscenario);
  snapshot.screen = ReplayScreenState{
      .view_type = signed_legacy_byte(viewtype),
      .in_camp = incamp != 0,
      .in_items = initems != 0,
      .in_swap = inswap != 0,
      .in_booty = inbooty != 0,
      .in_shop = inshop != 0,
      .in_temple = intemple != 0,
      .in_scroll = inscroll != 0,
      .in_spell = inspell != 0,
  };

  snapshot.world = ReplayWorldState{
      .world_x = is_in_dungeon
          ? floorx
          : checked_coordinate_sum(lookx, partyx, "outdoor x"),
      .world_y = is_in_dungeon
          ? floory
          : checked_coordinate_sum(looky, partyy, "outdoor y"),
      .saved_x = signed_legacy_byte(savedpostion[0]),
      .saved_y = signed_legacy_byte(savedpostion[1]),
      .party_x = partyx,
      .party_y = partyy,
      .facing_delta_x = facing.x,
      .facing_delta_y = facing.y,
      .land_level = landlevel,
      .dungeon_level = dunglevel,
      .saved_land_type = signed_legacy_byte(savedlandtype),
      .saved_land_level = signed_legacy_byte(savedlandlevel),
      .in_dungeon = is_in_dungeon,
      .dungeon_view = view != 0,
      .dungeon_update_pending = needdungeonupdate != 0,
      .multi_view = multiview != 0,
  };

  snapshot.party.selected_member = signed_legacy_byte(charselectnew);
  snapshot.party.fatigue = static_cast<std::int32_t>(fat);
  for (std::size_t index = 0; index < snapshot.party.pooled_money.size();
       ++index) {
    snapshot.party.pooled_money[index] = moneypool[index];
  }

  for (std::size_t member_index = 0;
       member_index < snapshot.party.members.size();
       ++member_index) {
    const character& legacy = c[member_index];
    ReplayPartyMemberState& member = snapshot.party.members[member_index];
    member.occupied =
        static_cast<int>(member_index) <= party_last_index;
    member.stamina = static_cast<std::int32_t>(legacy.stamina);
    member.maximum_stamina = static_cast<std::int32_t>(legacy.staminamax);
    for (std::size_t money_index = 0; money_index < member.money.size();
         ++money_index) {
      member.money[money_index] =
          static_cast<std::uint32_t>(legacy.money[money_index]);
    }
    member.weapon_configuration =
        static_cast<std::int32_t>(legacy.weaponnum);
    member.missile_configuration =
        static_cast<std::int32_t>(legacy.missilenum);
    member.alternate_weapon_set = legacy.toggle != 0;
    member.inventory_count =
        checked_inventory_count(legacy.numitems, member_index);
    for (std::uint32_t item_index = 0;
         item_index < member.inventory_count;
         ++item_index) {
      const item& legacy_item = legacy.items[item_index];
      member.inventory[item_index] = ReplayItemState{
          .item_id = static_cast<std::int32_t>(legacy_item.id),
          .equipped = legacy_item.equip != 0,
          .identified = legacy_item.ident != 0,
          .charges = static_cast<std::int32_t>(legacy_item.charge),
      };
    }
    for (std::size_t slot = 0; slot < member.equipment.size(); ++slot) {
      member.equipment[slot] =
          static_cast<std::int32_t>(legacy.armor[slot]);
    }
    for (std::size_t slot = 0; slot < member.scrolls.size(); ++slot) {
      const scroll& legacy_scroll = legacy.scrollcase[slot];
      member.scrolls[slot] = ReplayScrollState{
          .caste = signed_legacy_byte(legacy_scroll.castcaste),
          .level = signed_legacy_byte(legacy_scroll.castlevel),
          .spell = signed_legacy_byte(legacy_scroll.castnum),
          .power = signed_legacy_byte(legacy_scroll.powerlevel),
      };
    }
  }

  snapshot.encounter = ReplayEncounterState{
      .marker_id = doorid,
      .active_kind = signed_legacy_byte(encountflag),
      .remaining_uses = static_cast<std::int32_t>(enctry),
      // `reply` is a global scratch value used throughout unrelated legacy
      // paths. It is meaningful here only while an encounter is active.
      .result_code = encountflag
          ? static_cast<std::int32_t>(reply)
          : 0,
      .encounters_enabled = canencounter != 0,
  };

  snapshot.combat.active = incombat != 0;
  snapshot.combat.monster_turn = monsterturn != 0;
  snapshot.combat.round = signed_legacy_byte(combatround);
  snapshot.combat.turn_queue_index = signed_legacy_byte(up);
  snapshot.combat.active_party_member = signed_legacy_byte(charup);
  snapshot.combat.active_monster = signed_legacy_byte(monsterup);
  for (std::size_t index = 0;
       index < snapshot.combat.party_positions.size();
       ++index) {
    snapshot.combat.party_positions[index] = ReplayGridPosition{
        .x = signed_legacy_byte(pos[index][0]),
        .y = signed_legacy_byte(pos[index][1]),
    };
  }
  snapshot.combat.monster_count = monster_count;
  for (std::uint32_t index = 0; index < monster_count; ++index) {
    snapshot.combat.monster_positions[index] = ReplayGridPosition{
        .x = signed_legacy_byte(monpos[index][0]),
        .y = signed_legacy_byte(monpos[index][1]),
    };
  }

  snapshot.time = ReplayTimeState{
      .seconds = static_cast<std::int32_t>(tyme.tm_sec),
      .minutes = static_cast<std::int32_t>(tyme.tm_min),
      .hours = static_cast<std::int32_t>(tyme.tm_hour),
      .day_of_month = static_cast<std::int32_t>(tyme.tm_mday),
      .month = static_cast<std::int32_t>(tyme.tm_mon),
      .years_since_1900 = static_cast<std::int32_t>(tyme.tm_year),
      .day_of_week = static_cast<std::int32_t>(tyme.tm_wday),
      .day_of_year = static_cast<std::int32_t>(tyme.tm_yday),
      .daylight_saving = static_cast<std::int32_t>(tyme.tm_isdst),
  };

  return snapshot;
}

} // namespace realmz::replay
