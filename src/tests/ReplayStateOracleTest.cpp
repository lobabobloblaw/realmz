#include "replay/ReplayStateOracle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace realmz::replay;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("check failed: ") + #condition);   \
    }                                                                          \
  } while (false)

class ByteReader final {
public:
  explicit ByteReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  void expect_domain(std::string_view domain) {
    for (const char character : domain) {
      CHECK(read_u8() == static_cast<std::uint8_t>(character));
    }
    CHECK(read_u8() == 0U);
  }

  [[nodiscard]] std::uint8_t read_u8() {
    if (offset_ == bytes_.size()) {
      throw std::runtime_error("unexpected end of canonical state bytes");
    }
    return std::to_integer<std::uint8_t>(bytes_[offset_++]);
  }

  [[nodiscard]] bool read_bool() {
    const std::uint8_t value = read_u8();
    CHECK(value <= 1U);
    return value != 0U;
  }

  [[nodiscard]] std::uint32_t read_u32() {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
      value = static_cast<std::uint32_t>(value << 8U);
      value |= read_u8();
    }
    return value;
  }

  void expect_i32(std::int32_t expected) {
    CHECK(read_u32() == static_cast<std::uint32_t>(expected));
  }

  [[nodiscard]] bool finished() const noexcept {
    return offset_ == bytes_.size();
  }

private:
  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0;
};

template <typename Operation>
void check_state_error(Operation&& operation) {
  bool threw = false;
  try {
    operation();
  } catch (const ReplayStateOracleError&) {
    threw = true;
  }
  CHECK(threw);
}

void check_hash(const Sha256Digest& digest, std::string_view expected) {
  ++checks_run;
  const std::string actual = sha256_hex(digest);
  if (actual != expected) {
    throw std::runtime_error(
        "hash mismatch: expected " + std::string(expected) + ", got " +
        actual);
  }
}

void append_trace_u8(Sha256& hash, std::uint8_t value) {
  const std::array<std::byte, 1> bytes = {static_cast<std::byte>(value)};
  hash.update(bytes);
}

void append_trace_u32(Sha256& hash, std::uint32_t value) {
  const std::array<std::byte, 4> bytes = {
      static_cast<std::byte>((value >> 24U) & 0xFFU),
      static_cast<std::byte>((value >> 16U) & 0xFFU),
      static_cast<std::byte>((value >> 8U) & 0xFFU),
      static_cast<std::byte>(value & 0xFFU),
  };
  hash.update(bytes);
}

void append_reference_trace_record(
    Sha256& hash,
    std::uint8_t tag,
    std::uint32_t checkpoint_index,
    const ReplayStateSnapshot& snapshot) {
  append_trace_u8(hash, tag);
  append_trace_u32(hash, checkpoint_index);
  const Sha256Digest state = replay_state_sha256_v1(snapshot);
  hash.update(std::as_bytes(std::span(state)));
}

ReplayStateSnapshot representative_snapshot() {
  ReplayStateSnapshot snapshot;
  snapshot.scenario_id = -123456789;
  snapshot.screen = ReplayScreenState{
      .view_type = 0x01020304,
      .in_camp = true,
      .in_items = false,
      .in_swap = true,
      .in_booty = false,
      .in_shop = true,
      .in_temple = false,
      .in_scroll = true,
      .in_spell = false,
  };
  snapshot.world = ReplayWorldState{
      .world_x = std::numeric_limits<std::int32_t>::min(),
      .world_y = std::numeric_limits<std::int32_t>::max(),
      .saved_x = -3,
      .saved_y = 4,
      .party_x = 5,
      .party_y = -6,
      .facing_delta_x = -1,
      .facing_delta_y = 1,
      .land_level = 17,
      .dungeon_level = -18,
      .saved_land_type = 19,
      .saved_land_level = -20,
      .in_dungeon = true,
      .dungeon_view = false,
      .dungeon_update_pending = true,
      .multi_view = false,
  };
  snapshot.party.selected_member = -1;
  snapshot.party.fatigue = -32768;
  snapshot.party.pooled_money = {101, -202, 303};
  for (std::size_t member_index = 0;
       member_index < snapshot.party.members.size();
       ++member_index) {
    ReplayPartyMemberState& member = snapshot.party.members[member_index];
    member.occupied = (member_index % 2U) == 0U;
    member.stamina = -1000 - static_cast<std::int32_t>(member_index);
    member.maximum_stamina = 2000 + static_cast<std::int32_t>(member_index);
    member.money = {
        static_cast<std::uint32_t>(member_index + 1U),
        static_cast<std::uint32_t>(0x80000000U + member_index),
        static_cast<std::uint32_t>(0xFFFFFFFFU - member_index),
    };
    member.weapon_configuration =
        3000 + static_cast<std::int32_t>(member_index);
    member.missile_configuration =
        -4000 - static_cast<std::int32_t>(member_index);
    member.alternate_weapon_set = (member_index % 2U) != 0U;
    member.inventory_count = static_cast<std::uint32_t>(member_index % 3U);
    for (std::uint32_t item_index = 0;
         item_index < member.inventory_count;
         ++item_index) {
      member.inventory[item_index] = ReplayItemState{
          .item_id = static_cast<std::int32_t>(
              5000 + (member_index * 100U) + item_index),
          .equipped = (item_index % 2U) == 0U,
          .identified = (item_index % 2U) != 0U,
          .charges = -6000 - static_cast<std::int32_t>(item_index),
      };
    }
    for (std::size_t slot = 0; slot < member.equipment.size(); ++slot) {
      member.equipment[slot] = static_cast<std::int32_t>(
          7000 + (member_index * 100U) + slot);
    }
    for (std::size_t slot = 0; slot < member.scrolls.size(); ++slot) {
      member.scrolls[slot] = ReplayScrollState{
          .caste = static_cast<std::int32_t>(8000 + member_index),
          .level = -static_cast<std::int32_t>(8100 + slot),
          .spell = static_cast<std::int32_t>(8200 + slot),
          .power = -static_cast<std::int32_t>(8300 + member_index + slot),
      };
    }
  }
  snapshot.encounter = ReplayEncounterState{
      .marker_id = -9001,
      .active_kind = 2,
      .remaining_uses = 3,
      .result_code = -4,
      .encounters_enabled = true,
  };
  snapshot.combat.active = true;
  snapshot.combat.monster_turn = false;
  snapshot.combat.round = 11;
  snapshot.combat.turn_queue_index = -12;
  snapshot.combat.active_party_member = 5;
  snapshot.combat.active_monster = 99;
  for (std::size_t index = 0;
       index < snapshot.combat.party_positions.size();
       ++index) {
    snapshot.combat.party_positions[index] = ReplayGridPosition{
        .x = static_cast<std::int32_t>(index),
        .y = -static_cast<std::int32_t>(index),
    };
  }
  snapshot.combat.monster_count = 3;
  snapshot.combat.monster_positions[0] = {.x = -13, .y = 14};
  snapshot.combat.monster_positions[1] = {.x = 15, .y = -16};
  snapshot.combat.monster_positions[2] = {.x = 17, .y = 18};
  snapshot.time = ReplayTimeState{
      .seconds = 59,
      .minutes = 58,
      .hours = 23,
      .day_of_month = 31,
      .month = 11,
      .years_since_1900 = 126,
      .day_of_week = 4,
      .day_of_year = 364,
      .daylight_saving = -1,
  };
  return snapshot;
}

void expect_member(
    ByteReader& reader,
    const ReplayPartyMemberState& member) {
  CHECK(reader.read_bool() == member.occupied);
  reader.expect_i32(member.stamina);
  reader.expect_i32(member.maximum_stamina);
  for (const std::uint32_t money : member.money) {
    CHECK(reader.read_u32() == money);
  }
  reader.expect_i32(member.weapon_configuration);
  reader.expect_i32(member.missile_configuration);
  CHECK(reader.read_bool() == member.alternate_weapon_set);
  CHECK(reader.read_u32() == member.inventory_count);
  for (std::uint32_t index = 0; index < member.inventory_count; ++index) {
    const ReplayItemState& item = member.inventory[index];
    reader.expect_i32(item.item_id);
    CHECK(reader.read_bool() == item.equipped);
    CHECK(reader.read_bool() == item.identified);
    reader.expect_i32(item.charges);
  }
  CHECK(reader.read_u32() == kReplayEquipmentSlotCount);
  for (const std::int32_t equipment : member.equipment) {
    reader.expect_i32(equipment);
  }
  CHECK(reader.read_u32() == kReplayScrollSlotCount);
  for (const ReplayScrollState& scroll : member.scrolls) {
    reader.expect_i32(scroll.caste);
    reader.expect_i32(scroll.level);
    reader.expect_i32(scroll.spell);
    reader.expect_i32(scroll.power);
  }
}

void test_exact_canonical_framing_and_endianness() {
  const ReplayStateSnapshot snapshot = representative_snapshot();
  const std::vector<std::byte> encoded = encode_replay_state_v1(snapshot);
  ByteReader reader(encoded);
  reader.expect_domain("realmz.semantic-replay.state.v1");

  CHECK(reader.read_u8() == 0x01U);
  reader.expect_i32(snapshot.scenario_id);
  reader.expect_i32(snapshot.screen.view_type);
  CHECK(reader.read_bool() == snapshot.screen.in_camp);
  CHECK(reader.read_bool() == snapshot.screen.in_items);
  CHECK(reader.read_bool() == snapshot.screen.in_swap);
  CHECK(reader.read_bool() == snapshot.screen.in_booty);
  CHECK(reader.read_bool() == snapshot.screen.in_shop);
  CHECK(reader.read_bool() == snapshot.screen.in_temple);
  CHECK(reader.read_bool() == snapshot.screen.in_scroll);
  CHECK(reader.read_bool() == snapshot.screen.in_spell);

  CHECK(reader.read_u8() == 0x02U);
  reader.expect_i32(snapshot.world.world_x);
  reader.expect_i32(snapshot.world.world_y);
  reader.expect_i32(snapshot.world.saved_x);
  reader.expect_i32(snapshot.world.saved_y);
  reader.expect_i32(snapshot.world.party_x);
  reader.expect_i32(snapshot.world.party_y);
  reader.expect_i32(snapshot.world.facing_delta_x);
  reader.expect_i32(snapshot.world.facing_delta_y);
  reader.expect_i32(snapshot.world.land_level);
  reader.expect_i32(snapshot.world.dungeon_level);
  reader.expect_i32(snapshot.world.saved_land_type);
  reader.expect_i32(snapshot.world.saved_land_level);
  CHECK(reader.read_bool() == snapshot.world.in_dungeon);
  CHECK(reader.read_bool() == snapshot.world.dungeon_view);
  CHECK(reader.read_bool() == snapshot.world.dungeon_update_pending);
  CHECK(reader.read_bool() == snapshot.world.multi_view);

  CHECK(reader.read_u8() == 0x03U);
  reader.expect_i32(snapshot.party.selected_member);
  reader.expect_i32(snapshot.party.fatigue);
  for (const std::int32_t money : snapshot.party.pooled_money) {
    reader.expect_i32(money);
  }
  CHECK(reader.read_u32() == kReplayPartyMemberCount);
  for (const ReplayPartyMemberState& member : snapshot.party.members) {
    expect_member(reader, member);
  }

  CHECK(reader.read_u8() == 0x04U);
  reader.expect_i32(snapshot.encounter.marker_id);
  reader.expect_i32(snapshot.encounter.active_kind);
  reader.expect_i32(snapshot.encounter.remaining_uses);
  reader.expect_i32(snapshot.encounter.result_code);
  CHECK(reader.read_bool() == snapshot.encounter.encounters_enabled);

  CHECK(reader.read_u8() == 0x05U);
  CHECK(reader.read_bool() == snapshot.combat.active);
  CHECK(reader.read_bool() == snapshot.combat.monster_turn);
  reader.expect_i32(snapshot.combat.round);
  reader.expect_i32(snapshot.combat.turn_queue_index);
  reader.expect_i32(snapshot.combat.active_party_member);
  reader.expect_i32(snapshot.combat.active_monster);
  CHECK(reader.read_u32() == kReplayPartyMemberCount);
  for (const ReplayGridPosition& position :
       snapshot.combat.party_positions) {
    reader.expect_i32(position.x);
    reader.expect_i32(position.y);
  }
  CHECK(reader.read_u32() == snapshot.combat.monster_count);
  for (std::uint32_t index = 0; index < snapshot.combat.monster_count; ++index) {
    reader.expect_i32(snapshot.combat.monster_positions[index].x);
    reader.expect_i32(snapshot.combat.monster_positions[index].y);
  }

  CHECK(reader.read_u8() == 0x06U);
  reader.expect_i32(snapshot.time.seconds);
  reader.expect_i32(snapshot.time.minutes);
  reader.expect_i32(snapshot.time.hours);
  reader.expect_i32(snapshot.time.day_of_month);
  reader.expect_i32(snapshot.time.month);
  reader.expect_i32(snapshot.time.years_since_1900);
  reader.expect_i32(snapshot.time.day_of_week);
  reader.expect_i32(snapshot.time.day_of_year);
  reader.expect_i32(snapshot.time.daylight_saving);
  CHECK(reader.read_u8() == 0xFFU);
  CHECK(reader.finished());
}

void test_state_golden_hashes_and_logical_prefixes() {
  const ReplayStateSnapshot empty;
  const ReplayStateSnapshot representative = representative_snapshot();
  check_hash(
      replay_state_sha256_v1(empty),
      "ec6bd8f6646d851754458689ee3039d4"
      "58c8c83f97c66725831966d7525e56fc");
  check_hash(
      replay_state_sha256_v1(representative),
      "ef49063414731b4844eae50c41d1618c"
      "32a101db7deea3d529975e44f32b06a2");

  ReplayStateSnapshot ignored_inventory_tail = representative;
  ReplayPartyMemberState& first_member =
      ignored_inventory_tail.party.members[0];
  CHECK(first_member.inventory_count == 0U);
  first_member.inventory.back().item_id = 12345;
  CHECK(
      replay_state_sha256_v1(ignored_inventory_tail) ==
      replay_state_sha256_v1(representative));

  ReplayStateSnapshot active_inventory = representative;
  CHECK(active_inventory.party.members[1].inventory_count == 1U);
  ++active_inventory.party.members[1].inventory[0].charges;
  CHECK(
      replay_state_sha256_v1(active_inventory) !=
      replay_state_sha256_v1(representative));

  ReplayStateSnapshot ignored_monster_tail = representative;
  ignored_monster_tail.combat.monster_positions.back().x = 54321;
  CHECK(
      replay_state_sha256_v1(ignored_monster_tail) ==
      replay_state_sha256_v1(representative));

  ReplayStateSnapshot active_monster = representative;
  ++active_monster.combat.monster_positions[2].y;
  CHECK(
      replay_state_sha256_v1(active_monster) !=
      replay_state_sha256_v1(representative));
}

void test_each_state_section_affects_the_digest() {
  const ReplayStateSnapshot baseline = representative_snapshot();
  const Sha256Digest expected = replay_state_sha256_v1(baseline);
  std::vector<ReplayStateSnapshot> mutations;

  mutations.push_back(baseline);
  ++mutations.back().scenario_id;
  mutations.push_back(baseline);
  mutations.back().screen.in_spell = !mutations.back().screen.in_spell;
  mutations.push_back(baseline);
  ++mutations.back().world.saved_x;
  mutations.push_back(baseline);
  ++mutations.back().world.facing_delta_y;
  mutations.push_back(baseline);
  ++mutations.back().party.selected_member;
  mutations.push_back(baseline);
  ++mutations.back().party.fatigue;
  mutations.push_back(baseline);
  ++mutations.back().party.pooled_money[2];
  mutations.push_back(baseline);
  ++mutations.back().party.members[2].money[0];
  mutations.push_back(baseline);
  ++mutations.back().party.members[2].inventory[0].item_id;
  mutations.push_back(baseline);
  ++mutations.back().party.members[2].equipment[7];
  mutations.push_back(baseline);
  ++mutations.back().party.members[2].scrolls[3].spell;
  mutations.push_back(baseline);
  ++mutations.back().encounter.marker_id;
  mutations.push_back(baseline);
  mutations.back().encounter.encounters_enabled = false;
  mutations.push_back(baseline);
  mutations.back().combat.monster_turn = true;
  mutations.push_back(baseline);
  ++mutations.back().combat.party_positions[4].x;
  mutations.push_back(baseline);
  ++mutations.back().combat.monster_positions[1].y;
  mutations.push_back(baseline);
  ++mutations.back().time.daylight_saving;

  for (const ReplayStateSnapshot& mutation : mutations) {
    CHECK(replay_state_sha256_v1(mutation) != expected);
  }
}

void test_count_bounds_fail_closed() {
  ReplayStateSnapshot bad_inventory = representative_snapshot();
  bad_inventory.party.members[4].inventory_count =
      kReplayInventoryCapacity + 1U;
  check_state_error([&] {
    static_cast<void>(encode_replay_state_v1(bad_inventory));
  });
  check_state_error([&] {
    static_cast<void>(replay_state_sha256_v1(bad_inventory));
  });

  ReplayStateSnapshot bad_monsters = representative_snapshot();
  bad_monsters.combat.monster_count =
      kReplayMaximumCombatMonsters + 1U;
  check_state_error([&] {
    static_cast<void>(encode_replay_state_v1(bad_monsters));
  });
}

void test_trace_hash_and_checkpoint_contract() {
  ReplayStateSnapshot initial = representative_snapshot();
  ReplayStateSnapshot after_zero = initial;
  ++after_zero.world.world_x;
  ReplayStateSnapshot after_one = after_zero;
  ++after_one.time.minutes;
  const std::vector<ReplayStateSnapshot> checkpoints = {
      after_zero,
      after_one,
  };

  ReplayStateTraceHasher incremental(2);
  CHECK(incremental.expected_action_count() == 2U);
  CHECK(incremental.appended_action_count() == 0U);
  CHECK(!incremental.has_initial());
  CHECK(!incremental.complete());
  incremental.append_initial(initial);
  CHECK(incremental.has_initial());
  CHECK(!incremental.complete());
  incremental.append_post_action(0, after_zero);
  CHECK(incremental.appended_action_count() == 1U);
  incremental.append_post_action(1, after_one);
  CHECK(incremental.complete());
  const Sha256Digest digest = incremental.finalize();
  CHECK(incremental.finalize() == digest);
  CHECK(
      digest == replay_state_trace_sha256_v1(initial, checkpoints));

  Sha256 reference;
  reference.update("realmz.semantic-replay.state-trace.v1");
  append_trace_u8(reference, 0U);
  append_trace_u32(reference, 2U);
  append_trace_u32(reference, 3U);
  append_reference_trace_record(reference, 0U, 0U, initial);
  append_reference_trace_record(reference, 1U, 1U, after_zero);
  append_reference_trace_record(reference, 1U, 2U, after_one);
  CHECK(reference.finalize() == digest);
  check_hash(
      digest,
      "737edea99c7361894f19ccd57054797a"
      "1acdcab8a71370e541ab63ac7a3b6659");

  ReplayStateTraceHasher zero_actions(0);
  zero_actions.append_initial(initial);
  CHECK(zero_actions.complete());
  check_hash(
      zero_actions.finalize(),
      "091b51e456f525aa93501226c31befab"
      "035736fff4ed376515262bdef81d228f");

  ReplayStateSnapshot changed_middle = after_zero;
  ++changed_middle.party.fatigue;
  const std::vector<ReplayStateSnapshot> alternate_checkpoints = {
      changed_middle,
      after_one,
  };
  CHECK(
      replay_state_trace_sha256_v1(initial, alternate_checkpoints) != digest);
  CHECK(replay_state_trace_sha256_v1(initial, {}) != digest);
}

void test_trace_order_and_completeness_fail_closed() {
  const ReplayStateSnapshot snapshot = representative_snapshot();

  ReplayStateTraceHasher no_initial(1);
  check_state_error([&] { no_initial.append_post_action(0, snapshot); });
  check_state_error([&] { static_cast<void>(no_initial.finalize()); });

  ReplayStateTraceHasher duplicate_initial(0);
  duplicate_initial.append_initial(snapshot);
  check_state_error([&] { duplicate_initial.append_initial(snapshot); });
  check_state_error(
      [&] { duplicate_initial.append_post_action(0, snapshot); });

  ReplayStateTraceHasher skipped(2);
  skipped.append_initial(snapshot);
  check_state_error([&] { skipped.append_post_action(1, snapshot); });
  skipped.append_post_action(0, snapshot);
  check_state_error([&] { skipped.append_post_action(0, snapshot); });
  check_state_error([&] { static_cast<void>(skipped.finalize()); });
  skipped.append_post_action(1, snapshot);
  CHECK(skipped.complete());

  check_state_error([] {
    ReplayStateTraceHasher oversized(kMaximumReplayTraceActions + 1U);
    static_cast<void>(oversized);
  });

  ReplayStateSnapshot invalid = snapshot;
  invalid.party.members[0].inventory_count =
      kReplayInventoryCapacity + 1U;
  ReplayStateTraceHasher recovered(1);
  check_state_error([&] { recovered.append_initial(invalid); });
  CHECK(!recovered.has_initial());
  recovered.append_initial(snapshot);
  check_state_error([&] { recovered.append_post_action(0, invalid); });
  CHECK(recovered.appended_action_count() == 0U);
  recovered.append_post_action(0, snapshot);
  const std::vector<ReplayStateSnapshot> one_checkpoint = {snapshot};
  CHECK(
      recovered.finalize() ==
      replay_state_trace_sha256_v1(snapshot, one_checkpoint));
}

} // namespace

int main() {
  try {
    test_exact_canonical_framing_and_endianness();
    test_state_golden_hashes_and_logical_prefixes();
    test_each_state_section_affects_the_digest();
    test_count_bounds_fail_closed();
    test_trace_hash_and_checkpoint_contract();
    test_trace_order_and_completeness_fail_closed();
    std::cout << "ReplayStateOracleTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayStateOracleTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
