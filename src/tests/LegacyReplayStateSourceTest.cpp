#include "replay/LegacyReplayStateSource.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

extern "C" {
#include "realmz_orig/structs.h"

short currentscenario = 0;
short fat = 0;
short incombat = 0;
short inspell = 0;
short monsterturn = 0;
short nummon = 0;
short enctry = 0;
short reply = 0;
int32_t partyx = 0;
int32_t partyy = 0;
int32_t landlevel = 0;
int32_t dunglevel = 0;
int32_t lookx = 0;
int32_t looky = 0;
int32_t floorx = 0;
int32_t floory = 0;
int32_t moneypool[3] = {};
int32_t doorid = 0;
char charnum = -1;
char charselectnew = -1;
char charup = -1;
char monsterup = -1;
char combatround = 0;
char up = 0;
char head = 1;
char encountflag = 0;
char viewtype = 1;
char savedpostion[2] = {};
char savedlandtype = 0;
char savedlandlevel = 0;
char multiview = 0;
char canencounter = 0;
Boolean incamp = 0;
Boolean initems = 0;
Boolean inswap = 0;
Boolean inbooty = 0;
Boolean inshop = 0;
Boolean intemple = 0;
Boolean inscroll = 0;
Boolean indung = 0;
Boolean view = 0;
Boolean needdungeonupdate = 0;
struct character c[6] = {};
char pos[6][2] = {};
char monpos[100][2] = {};
struct tm tyme = {};
}

using namespace realmz::replay;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(                                                \
          std::string("check failed at line ") + std::to_string(__LINE__) +  \
          ": " #condition);                                                   \
    }                                                                          \
  } while (false)

template <typename Operation>
void check_capture_error(Operation&& operation) {
  bool threw = false;
  try {
    operation();
  } catch (const LegacyReplayStateCaptureError&) {
    threw = true;
  }
  CHECK(threw);
}

[[nodiscard]] char legacy_byte(int value) {
  return static_cast<char>(static_cast<unsigned char>(value & 0xFF));
}

void reset_legacy_state() {
  currentscenario = 0;
  fat = 0;
  incombat = 0;
  inspell = 0;
  monsterturn = 0;
  nummon = 0;
  enctry = 0;
  reply = 0;
  partyx = partyy = landlevel = dunglevel = 0;
  lookx = looky = floorx = floory = 0;
  std::memset(moneypool, 0, sizeof(moneypool));
  doorid = 0;
  charnum = legacy_byte(-1);
  charselectnew = charup = monsterup = legacy_byte(-1);
  combatround = 0;
  up = 0;
  head = 1;
  encountflag = 0;
  viewtype = 1;
  std::memset(savedpostion, 0, sizeof(savedpostion));
  savedlandtype = savedlandlevel = multiview = canencounter = 0;
  incamp = initems = inswap = inbooty = 0;
  inshop = intemple = inscroll = indung = view = 0;
  needdungeonupdate = 0;
  std::memset(c, 0, sizeof(c));
  std::memset(pos, 0, sizeof(pos));
  std::memset(monpos, 0, sizeof(monpos));
  std::memset(&tyme, 0, sizeof(tyme));
}

void seed_exhaustive_outdoor_state() {
  reset_legacy_state();
  currentscenario = -1234;
  viewtype = legacy_byte(-7);
  incamp = 1;
  initems = 0;
  inswap = 2;
  inbooty = 0;
  inshop = 3;
  intemple = 0;
  inscroll = 4;
  inspell = -5;

  indung = 0;
  lookx = 1000;
  looky = -2000;
  partyx = 23;
  partyy = -41;
  floorx = 7777;
  floory = 8888;
  savedpostion[0] = legacy_byte(-12);
  savedpostion[1] = legacy_byte(13);
  head = 4;
  landlevel = 101;
  dunglevel = -202;
  savedlandtype = legacy_byte(-3);
  savedlandlevel = legacy_byte(44);
  view = 1;
  needdungeonupdate = 2;
  multiview = legacy_byte(-1);

  charselectnew = legacy_byte(-1);
  fat = -321;
  moneypool[0] = -10001;
  moneypool[1] = 20002;
  moneypool[2] = -30003;
  charnum = 2;
  for (std::size_t member_index = 0; member_index < 6; ++member_index) {
    character& member = c[member_index];
    member.stamina = static_cast<short>(100 + member_index);
    member.staminamax = static_cast<short>(200 + member_index);
    member.money[0] = static_cast<unsigned short>(1000 + member_index);
    member.money[1] = static_cast<unsigned short>(2000 + member_index);
    member.money[2] = static_cast<unsigned short>(3000 + member_index);
    member.weaponnum = static_cast<short>(-20 - member_index);
    member.missilenum = static_cast<short>(30 + member_index);
    member.toggle = static_cast<Boolean>((member_index % 2U) != 0U);
    member.numitems = static_cast<short>(member_index % 4U);
    for (int item_index = 0; item_index < member.numitems; ++item_index) {
      member.items[item_index] = item{
          .id = static_cast<short>(
              4000 + (member_index * 10U) +
              static_cast<std::size_t>(item_index)),
          .equip = legacy_byte(item_index % 2),
          .ident = legacy_byte((item_index + 1) % 2),
          .charge = static_cast<short>(-500 - item_index),
      };
    }
    for (std::size_t slot = 0; slot < 20; ++slot) {
      member.armor[slot] = static_cast<short>(
          -6000 + (member_index * 20U) + slot);
    }
    for (std::size_t slot = 0; slot < 5; ++slot) {
      member.scrollcase[slot] = scroll{
          .castcaste = legacy_byte(-1 - static_cast<int>(member_index)),
          .castlevel = legacy_byte(10 + static_cast<int>(slot)),
          .castnum = legacy_byte(-20 - static_cast<int>(slot)),
          .powerlevel = legacy_byte(
              30 + static_cast<int>(member_index + slot)),
      };
    }
  }

  doorid = -7654321;
  encountflag = 2;
  enctry = -17;
  reply = -4;
  canencounter = legacy_byte(-1);

  incombat = 2;
  monsterturn = -1;
  combatround = legacy_byte(-9);
  up = legacy_byte(-10);
  charup = legacy_byte(-1);
  monsterup = legacy_byte(-2);
  for (std::size_t index = 0; index < 6; ++index) {
    pos[index][0] = legacy_byte(-30 - static_cast<int>(index));
    pos[index][1] = legacy_byte(40 + static_cast<int>(index));
  }
  nummon = 3;
  for (int index = 0; index < nummon; ++index) {
    monpos[index][0] = legacy_byte(-50 - index);
    monpos[index][1] = legacy_byte(60 + index);
  }

  tyme.tm_sec = 59;
  tyme.tm_min = 58;
  tyme.tm_hour = 23;
  tyme.tm_mday = 31;
  tyme.tm_mon = 11;
  tyme.tm_year = 126;
  tyme.tm_wday = 4;
  tyme.tm_yday = 364;
  tyme.tm_isdst = -1;
}

void expect_exhaustive_outdoor_snapshot(
    const ReplayStateSnapshot& snapshot) {
  CHECK(snapshot.scenario_id == -1234);
  CHECK(snapshot.screen.view_type == -7);
  CHECK(snapshot.screen.in_camp);
  CHECK(!snapshot.screen.in_items);
  CHECK(snapshot.screen.in_swap);
  CHECK(!snapshot.screen.in_booty);
  CHECK(snapshot.screen.in_shop);
  CHECK(!snapshot.screen.in_temple);
  CHECK(snapshot.screen.in_scroll);
  CHECK(snapshot.screen.in_spell);

  CHECK(snapshot.world.world_x == 1023);
  CHECK(snapshot.world.world_y == -2041);
  CHECK(snapshot.world.saved_x == -12);
  CHECK(snapshot.world.saved_y == 13);
  CHECK(snapshot.world.party_x == 23);
  CHECK(snapshot.world.party_y == -41);
  CHECK(snapshot.world.facing_delta_x == -1);
  CHECK(snapshot.world.facing_delta_y == 0);
  CHECK(snapshot.world.land_level == 101);
  CHECK(snapshot.world.dungeon_level == -202);
  CHECK(snapshot.world.saved_land_type == -3);
  CHECK(snapshot.world.saved_land_level == 44);
  CHECK(!snapshot.world.in_dungeon);
  CHECK(snapshot.world.dungeon_view);
  CHECK(snapshot.world.dungeon_update_pending);
  CHECK(snapshot.world.multi_view);

  CHECK(snapshot.party.selected_member == -1);
  CHECK(snapshot.party.fatigue == -321);
  CHECK(snapshot.party.pooled_money[0] == -10001);
  CHECK(snapshot.party.pooled_money[1] == 20002);
  CHECK(snapshot.party.pooled_money[2] == -30003);
  for (std::size_t member_index = 0; member_index < 6; ++member_index) {
    const ReplayPartyMemberState& member =
        snapshot.party.members[member_index];
    CHECK(member.occupied == (member_index <= 2U));
    CHECK(member.stamina == static_cast<std::int32_t>(100 + member_index));
    CHECK(
        member.maximum_stamina ==
        static_cast<std::int32_t>(200 + member_index));
    CHECK(member.money[0] == 1000U + member_index);
    CHECK(member.money[1] == 2000U + member_index);
    CHECK(member.money[2] == 3000U + member_index);
    CHECK(
        member.weapon_configuration ==
        -20 - static_cast<std::int32_t>(member_index));
    CHECK(
        member.missile_configuration ==
        30 + static_cast<std::int32_t>(member_index));
    CHECK(member.alternate_weapon_set == ((member_index % 2U) != 0U));
    CHECK(member.inventory_count == member_index % 4U);
    for (std::uint32_t item_index = 0;
         item_index < member.inventory_count;
         ++item_index) {
      const ReplayItemState& captured_item = member.inventory[item_index];
      CHECK(
          captured_item.item_id ==
          static_cast<std::int32_t>(4000 + (member_index * 10U) + item_index));
      CHECK(captured_item.equipped == ((item_index % 2U) != 0U));
      CHECK(captured_item.identified == ((item_index % 2U) == 0U));
      CHECK(
          captured_item.charges ==
          -500 - static_cast<std::int32_t>(item_index));
    }
    for (std::size_t slot = 0; slot < 20; ++slot) {
      CHECK(
          member.equipment[slot] ==
          static_cast<std::int32_t>(
              -6000 + (member_index * 20U) + slot));
    }
    for (std::size_t slot = 0; slot < 5; ++slot) {
      CHECK(
          member.scrolls[slot].caste ==
          -1 - static_cast<std::int32_t>(member_index));
      CHECK(
          member.scrolls[slot].level ==
          10 + static_cast<std::int32_t>(slot));
      CHECK(
          member.scrolls[slot].spell ==
          -20 - static_cast<std::int32_t>(slot));
      CHECK(
          member.scrolls[slot].power ==
          30 + static_cast<std::int32_t>(member_index + slot));
    }
  }

  CHECK(snapshot.encounter.marker_id == -7654321);
  CHECK(snapshot.encounter.active_kind == 2);
  CHECK(snapshot.encounter.remaining_uses == -17);
  CHECK(snapshot.encounter.result_code == -4);
  CHECK(snapshot.encounter.encounters_enabled);

  CHECK(snapshot.combat.active);
  CHECK(snapshot.combat.monster_turn);
  CHECK(snapshot.combat.round == -9);
  CHECK(snapshot.combat.turn_queue_index == -10);
  CHECK(snapshot.combat.active_party_member == -1);
  CHECK(snapshot.combat.active_monster == -2);
  for (std::size_t index = 0; index < 6; ++index) {
    CHECK(
        snapshot.combat.party_positions[index].x ==
        -30 - static_cast<std::int32_t>(index));
    CHECK(
        snapshot.combat.party_positions[index].y ==
        40 + static_cast<std::int32_t>(index));
  }
  CHECK(snapshot.combat.monster_count == 3U);
  for (std::uint32_t index = 0; index < 3; ++index) {
    CHECK(
        snapshot.combat.monster_positions[index].x ==
        -50 - static_cast<std::int32_t>(index));
    CHECK(
        snapshot.combat.monster_positions[index].y ==
        60 + static_cast<std::int32_t>(index));
  }
  CHECK(snapshot.combat.monster_positions[3] == ReplayGridPosition{});

  CHECK(snapshot.time.seconds == 59);
  CHECK(snapshot.time.minutes == 58);
  CHECK(snapshot.time.hours == 23);
  CHECK(snapshot.time.day_of_month == 31);
  CHECK(snapshot.time.month == 11);
  CHECK(snapshot.time.years_since_1900 == 126);
  CHECK(snapshot.time.day_of_week == 4);
  CHECK(snapshot.time.day_of_year == 364);
  CHECK(snapshot.time.daylight_saving == -1);
}

void test_exhaustive_outdoor_capture_and_detachment() {
  seed_exhaustive_outdoor_state();
  const std::array<character, 6> characters_before = {
      c[0], c[1], c[2], c[3], c[4], c[5],
  };
  const std::array<std::array<char, 2>, 6> positions_before = [] {
    std::array<std::array<char, 2>, 6> result{};
    std::memcpy(result.data(), pos, sizeof(pos));
    return result;
  }();
  const std::array<std::array<char, 2>, 100> monster_positions_before = [] {
    std::array<std::array<char, 2>, 100> result{};
    std::memcpy(result.data(), monpos, sizeof(monpos));
    return result;
  }();
  const tm time_before = tyme;

  LegacyReplayStateSource source;
  const ReplayStateSnapshot snapshot = source.capture();
  expect_exhaustive_outdoor_snapshot(snapshot);
  CHECK(std::memcmp(c, characters_before.data(), sizeof(c)) == 0);
  CHECK(std::memcmp(pos, positions_before.data(), sizeof(pos)) == 0);
  CHECK(
      std::memcmp(
          monpos, monster_positions_before.data(), sizeof(monpos)) == 0);
  CHECK(std::memcmp(&tyme, &time_before, sizeof(tyme)) == 0);

  c[0].stamina = 999;
  c[1].items[0].id = -999;
  pos[0][0] = 99;
  monpos[0][0] = 98;
  tyme.tm_year = 999;
  CHECK(snapshot.party.members[0].stamina == 100);
  CHECK(snapshot.party.members[1].inventory[0].item_id == 4010);
  CHECK(snapshot.combat.party_positions[0].x == -30);
  CHECK(snapshot.combat.monster_positions[0].x == -50);
  CHECK(snapshot.time.years_since_1900 == 126);
}

void test_world_modes_facing_and_independent_view_flag() {
  reset_legacy_state();
  charnum = legacy_byte(-1);
  lookx = 70;
  looky = 80;
  partyx = 9;
  partyy = 10;
  floorx = -111;
  floory = 222;
  viewtype = legacy_byte(-1);
  view = 0;

  LegacyReplayStateSource source;
  ReplayStateSnapshot snapshot = source.capture();
  CHECK(snapshot.world.world_x == 79);
  CHECK(snapshot.world.world_y == 90);
  CHECK(snapshot.screen.view_type == -1);
  CHECK(!snapshot.world.dungeon_view);

  indung = 1;
  view = 1;
  snapshot = source.capture();
  CHECK(snapshot.world.world_x == -111);
  CHECK(snapshot.world.world_y == 222);
  CHECK(snapshot.world.dungeon_view);

  struct FacingCase final {
    int head_value;
    std::int32_t x;
    std::int32_t y;
  };
  constexpr std::array cases{
      FacingCase{1, 0, -1},
      FacingCase{2, 1, 0},
      FacingCase{3, 0, 1},
      FacingCase{4, -1, 0},
  };
  for (const FacingCase& test_case : cases) {
    head = legacy_byte(test_case.head_value);
    snapshot = source.capture();
    CHECK(snapshot.world.facing_delta_x == test_case.x);
    CHECK(snapshot.world.facing_delta_y == test_case.y);
  }
}

void test_bounds_fail_closed_and_valid_limits_recover() {
  LegacyReplayStateSource source;

  reset_legacy_state();
  charnum = legacy_byte(-2);
  check_capture_error([&] { static_cast<void>(source.capture()); });
  charnum = 6;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  charnum = 5;
  CHECK(source.capture().party.members[5].occupied);

  c[3].numitems = -1;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  c[3].numitems = 31;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  c[3].numitems = 30;
  c[3].items[29] = {.id = 4321, .equip = 1, .ident = 1, .charge = -9};
  ReplayStateSnapshot snapshot = source.capture();
  CHECK(snapshot.party.members[3].inventory_count == 30U);
  CHECK(snapshot.party.members[3].inventory[29].item_id == 4321);

  nummon = -1;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  nummon = 101;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  nummon = 100;
  monpos[99][0] = legacy_byte(-127);
  monpos[99][1] = legacy_byte(127);
  snapshot = source.capture();
  CHECK(snapshot.combat.monster_count == 100U);
  CHECK(snapshot.combat.monster_positions[99].x == -127);
  CHECK(snapshot.combat.monster_positions[99].y == 127);

  encountflag = 0;
  reply = 1234;
  snapshot = source.capture();
  CHECK(snapshot.encounter.result_code == 0);
  encountflag = 1;
  CHECK(source.capture().encounter.result_code == 1234);

  indung = 0;
  head = 0;
  snapshot = source.capture();
  CHECK(snapshot.world.facing_delta_x == 0);
  CHECK(snapshot.world.facing_delta_y == -1);
  head = 5;
  snapshot = source.capture();
  CHECK(snapshot.world.facing_delta_x == 0);
  CHECK(snapshot.world.facing_delta_y == -1);

  indung = 1;
  head = 0;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  head = 5;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  head = 1;

  indung = 0;
  lookx = std::numeric_limits<std::int32_t>::max();
  partyx = 1;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  lookx = std::numeric_limits<std::int32_t>::min();
  partyx = -1;
  check_capture_error([&] { static_cast<void>(source.capture()); });
  lookx = std::numeric_limits<std::int32_t>::max();
  partyx = 0;
  looky = std::numeric_limits<std::int32_t>::min();
  partyy = 0;
  snapshot = source.capture();
  CHECK(snapshot.world.world_x == std::numeric_limits<std::int32_t>::max());
  CHECK(snapshot.world.world_y == std::numeric_limits<std::int32_t>::min());

  // Dungeon coordinates are already canonical and do not evaluate the
  // irrelevant outdoor sums.
  indung = 1;
  partyx = 1;
  floorx = 17;
  floory = -18;
  snapshot = source.capture();
  CHECK(snapshot.world.world_x == 17);
  CHECK(snapshot.world.world_y == -18);
}

} // namespace

int main() {
  try {
    test_exhaustive_outdoor_capture_and_detachment();
    test_world_modes_facing_and_independent_view_flag();
    test_bounds_fail_closed_and_valid_limits_recover();
    std::cout << "LegacyReplayStateSourceTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "LegacyReplayStateSourceTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
