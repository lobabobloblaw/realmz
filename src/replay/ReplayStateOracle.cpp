#include "replay/ReplayStateOracle.hpp"

#include <array>
#include <span>
#include <string_view>

namespace realmz::replay {

namespace {

constexpr std::string_view kStateDomain =
    "realmz.semantic-replay.state.v1";
constexpr std::string_view kTraceDomain =
    "realmz.semantic-replay.state-trace.v1";

void append_u8(std::vector<std::byte>& output, std::uint8_t value) {
  output.push_back(static_cast<std::byte>(value));
}

void append_bool(std::vector<std::byte>& output, bool value) {
  append_u8(output, value ? 1U : 0U);
}

void append_u32(std::vector<std::byte>& output, std::uint32_t value) {
  for (unsigned shift : {24U, 16U, 8U, 0U}) {
    append_u8(output, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
  }
}

void append_i32(std::vector<std::byte>& output, std::int32_t value) {
  append_u32(output, static_cast<std::uint32_t>(value));
}

void append_u32(Sha256& hash, std::uint32_t value) {
  std::array<std::byte, 4> encoded{};
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    const unsigned shift = static_cast<unsigned>((3U - index) * 8U);
    encoded[index] = static_cast<std::byte>((value >> shift) & 0xFFU);
  }
  hash.update(encoded);
}

void append_u8(Sha256& hash, std::uint8_t value) {
  const std::array<std::byte, 1> encoded = {
      static_cast<std::byte>(value),
  };
  hash.update(encoded);
}

void append_domain(std::vector<std::byte>& output, std::string_view domain) {
  const std::span<const std::byte> bytes =
      std::as_bytes(std::span(domain.data(), domain.size()));
  output.insert(output.end(), bytes.begin(), bytes.end());
  append_u8(output, 0U);
}

void append_domain(Sha256& hash, std::string_view domain) {
  hash.update(domain);
  append_u8(hash, 0U);
}

void append_item(std::vector<std::byte>& output, const ReplayItemState& item) {
  append_i32(output, item.item_id);
  append_bool(output, item.equipped);
  append_bool(output, item.identified);
  append_i32(output, item.charges);
}

void append_scroll(
    std::vector<std::byte>& output,
    const ReplayScrollState& scroll) {
  append_i32(output, scroll.caste);
  append_i32(output, scroll.level);
  append_i32(output, scroll.spell);
  append_i32(output, scroll.power);
}

void append_member(
    std::vector<std::byte>& output,
    const ReplayPartyMemberState& member) {
  if (member.inventory_count > member.inventory.size()) {
    throw ReplayStateOracleError(
        "replay party inventory count exceeds its fixed capacity");
  }

  append_bool(output, member.occupied);
  append_i32(output, member.stamina);
  append_i32(output, member.maximum_stamina);
  for (const std::uint32_t money : member.money) {
    append_u32(output, money);
  }
  append_i32(output, member.weapon_configuration);
  append_i32(output, member.missile_configuration);
  append_bool(output, member.alternate_weapon_set);

  append_u32(output, member.inventory_count);
  for (std::uint32_t index = 0; index < member.inventory_count; ++index) {
    append_item(output, member.inventory[index]);
  }

  append_u32(
      output, static_cast<std::uint32_t>(member.equipment.size()));
  for (const std::int32_t equipment : member.equipment) {
    append_i32(output, equipment);
  }

  append_u32(output, static_cast<std::uint32_t>(member.scrolls.size()));
  for (const ReplayScrollState& scroll : member.scrolls) {
    append_scroll(output, scroll);
  }
}

void append_state_digest_record(
    Sha256& hash,
    std::uint8_t tag,
    std::uint32_t checkpoint_index,
    const ReplayStateSnapshot& snapshot) {
  // Compute before touching the trace so a rejected snapshot does not poison
  // an otherwise reusable incremental hasher.
  const Sha256Digest state_digest = replay_state_sha256_v1(snapshot);
  append_u8(hash, tag);
  append_u32(hash, checkpoint_index);
  hash.update(std::as_bytes(std::span(state_digest)));
}

} // namespace

std::vector<std::byte> encode_replay_state_v1(
    const ReplayStateSnapshot& snapshot) {
  if (snapshot.combat.monster_count >
      snapshot.combat.monster_positions.size()) {
    throw ReplayStateOracleError(
        "replay combat monster count exceeds its fixed capacity");
  }

  std::vector<std::byte> output;
  output.reserve(2048);
  append_domain(output, kStateDomain);

  append_u8(output, 0x01U);
  append_i32(output, snapshot.scenario_id);
  append_i32(output, snapshot.screen.view_type);
  append_bool(output, snapshot.screen.in_camp);
  append_bool(output, snapshot.screen.in_items);
  append_bool(output, snapshot.screen.in_swap);
  append_bool(output, snapshot.screen.in_booty);
  append_bool(output, snapshot.screen.in_shop);
  append_bool(output, snapshot.screen.in_temple);
  append_bool(output, snapshot.screen.in_scroll);
  append_bool(output, snapshot.screen.in_spell);

  append_u8(output, 0x02U);
  append_i32(output, snapshot.world.world_x);
  append_i32(output, snapshot.world.world_y);
  append_i32(output, snapshot.world.saved_x);
  append_i32(output, snapshot.world.saved_y);
  append_i32(output, snapshot.world.party_x);
  append_i32(output, snapshot.world.party_y);
  append_i32(output, snapshot.world.facing_delta_x);
  append_i32(output, snapshot.world.facing_delta_y);
  append_i32(output, snapshot.world.land_level);
  append_i32(output, snapshot.world.dungeon_level);
  append_i32(output, snapshot.world.saved_land_type);
  append_i32(output, snapshot.world.saved_land_level);
  append_bool(output, snapshot.world.in_dungeon);
  append_bool(output, snapshot.world.dungeon_view);
  append_bool(output, snapshot.world.dungeon_update_pending);
  append_bool(output, snapshot.world.multi_view);

  append_u8(output, 0x03U);
  append_i32(output, snapshot.party.selected_member);
  append_i32(output, snapshot.party.fatigue);
  for (const std::int32_t money : snapshot.party.pooled_money) {
    append_i32(output, money);
  }
  append_u32(
      output, static_cast<std::uint32_t>(snapshot.party.members.size()));
  for (const ReplayPartyMemberState& member : snapshot.party.members) {
    append_member(output, member);
  }

  append_u8(output, 0x04U);
  append_i32(output, snapshot.encounter.marker_id);
  append_i32(output, snapshot.encounter.active_kind);
  append_i32(output, snapshot.encounter.remaining_uses);
  append_i32(output, snapshot.encounter.result_code);
  append_bool(output, snapshot.encounter.encounters_enabled);

  append_u8(output, 0x05U);
  append_bool(output, snapshot.combat.active);
  append_bool(output, snapshot.combat.monster_turn);
  append_i32(output, snapshot.combat.round);
  append_i32(output, snapshot.combat.turn_queue_index);
  append_i32(output, snapshot.combat.active_party_member);
  append_i32(output, snapshot.combat.active_monster);
  append_u32(
      output,
      static_cast<std::uint32_t>(snapshot.combat.party_positions.size()));
  for (const ReplayGridPosition& position : snapshot.combat.party_positions) {
    append_i32(output, position.x);
    append_i32(output, position.y);
  }
  append_u32(output, snapshot.combat.monster_count);
  for (std::uint32_t index = 0; index < snapshot.combat.monster_count; ++index) {
    append_i32(output, snapshot.combat.monster_positions[index].x);
    append_i32(output, snapshot.combat.monster_positions[index].y);
  }

  append_u8(output, 0x06U);
  append_i32(output, snapshot.time.seconds);
  append_i32(output, snapshot.time.minutes);
  append_i32(output, snapshot.time.hours);
  append_i32(output, snapshot.time.day_of_month);
  append_i32(output, snapshot.time.month);
  append_i32(output, snapshot.time.years_since_1900);
  append_i32(output, snapshot.time.day_of_week);
  append_i32(output, snapshot.time.day_of_year);
  append_i32(output, snapshot.time.daylight_saving);

  append_u8(output, 0xFFU);
  return output;
}

Sha256Digest replay_state_sha256_v1(const ReplayStateSnapshot& snapshot) {
  return sha256(encode_replay_state_v1(snapshot));
}

ReplayStateTraceHasher::ReplayStateTraceHasher(
    std::uint32_t expected_action_count)
    : expected_action_count_(expected_action_count) {
  if (expected_action_count > kMaximumReplayTraceActions) {
    throw ReplayStateOracleError(
        "replay state trace action count exceeds the v1 limit");
  }
  append_domain(hash_, kTraceDomain);
  append_u32(hash_, expected_action_count_);
  append_u32(hash_, expected_action_count_ + 1U);
}

void ReplayStateTraceHasher::append_initial(
    const ReplayStateSnapshot& snapshot) {
  if (has_initial_) {
    throw ReplayStateOracleError(
        "replay state trace already has its initial checkpoint");
  }
  if (appended_action_count_ != 0U) {
    throw ReplayStateOracleError(
        "replay state trace initial checkpoint is out of order");
  }
  append_state_digest_record(hash_, 0x00U, 0U, snapshot);
  has_initial_ = true;
}

void ReplayStateTraceHasher::append_post_action(
    std::uint32_t action_index,
    const ReplayStateSnapshot& snapshot) {
  if (!has_initial_) {
    throw ReplayStateOracleError(
        "replay state trace requires its initial checkpoint first");
  }
  if (action_index != appended_action_count_) {
    throw ReplayStateOracleError(
        "replay state trace action checkpoints are not contiguous");
  }
  if (appended_action_count_ == expected_action_count_) {
    throw ReplayStateOracleError(
        "replay state trace has more action checkpoints than declared");
  }
  append_state_digest_record(hash_, 0x01U, action_index + 1U, snapshot);
  ++appended_action_count_;
}

std::uint32_t ReplayStateTraceHasher::expected_action_count() const noexcept {
  return expected_action_count_;
}

std::uint32_t ReplayStateTraceHasher::appended_action_count() const noexcept {
  return appended_action_count_;
}

bool ReplayStateTraceHasher::has_initial() const noexcept {
  return has_initial_;
}

bool ReplayStateTraceHasher::complete() const noexcept {
  return has_initial_ && appended_action_count_ == expected_action_count_;
}

Sha256Digest ReplayStateTraceHasher::finalize() const {
  if (!complete()) {
    throw ReplayStateOracleError(
        "replay state trace is missing one or more checkpoints");
  }
  return hash_.finalize();
}

Sha256Digest replay_state_trace_sha256_v1(
    const ReplayStateSnapshot& initial,
    std::span<const ReplayStateSnapshot> post_action_checkpoints) {
  if (post_action_checkpoints.size() > kMaximumReplayTraceActions) {
    throw ReplayStateOracleError(
        "replay state trace action count exceeds the v1 limit");
  }
  ReplayStateTraceHasher trace(
      static_cast<std::uint32_t>(post_action_checkpoints.size()));
  trace.append_initial(initial);
  for (std::size_t index = 0; index < post_action_checkpoints.size(); ++index) {
    trace.append_post_action(
        static_cast<std::uint32_t>(index), post_action_checkpoints[index]);
  }
  return trace.finalize();
}

} // namespace realmz::replay
