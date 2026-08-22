#include "PartyRailControlLayout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>

namespace realmz::presentation {
namespace {

constexpr size_t kMaximumPartyMembers = 6;
constexpr double kMinimumTargetExtent = 44.0;

[[nodiscard]] bool eligible_screen(ScreenContext screen) noexcept {
  return (screen == ScreenContext::exploration) ||
      (screen == ScreenContext::dungeon);
}

[[nodiscard]] bool valid_target(LogicalRect bounds) noexcept {
  return bounds.is_finite_and_nonnegative() &&
      (bounds.width >= kMinimumTargetExtent) &&
      (bounds.height >= kMinimumTargetExtent);
}

[[nodiscard]] bool interiors_overlap(
    LogicalRect first,
    LogicalRect second) noexcept {
  return std::max(first.x, second.x) <
          std::min(first.right(), second.right()) &&
      std::max(first.y, second.y) <
          std::min(first.bottom(), second.bottom());
}

} // namespace

std::vector<PartyRailControlPlacement> compute_party_rail_control_layout(
    const PartyRailControlLayoutRequest& request,
    const PartyRailModel& party,
    const PartyRailLayout& layout) {
  if (!eligible_screen(request.screen) || party.members.empty() ||
      (party.members.size() > kMaximumPartyMembers) ||
      (party.members.size() != layout.members.size()) ||
      !layout.panel_bounds.is_finite_and_nonnegative()) {
    return {};
  }

  std::set<PartyMemberId> member_ids;
  std::set<uint32_t> region_ids;
  std::set<std::string_view> focus_identifiers;
  std::set<std::string_view> command_identifiers;
  std::set<int32_t> tab_orders;
  size_t selected_count = 0;
  for (size_t index = 0; index < party.members.size(); ++index) {
    const auto& member = party.members[index];
    const auto& placed = layout.members[index];
    const auto region = party_member_region_id(member.id);
    if ((member.id >= kMaximumPartyMembers) ||
        (placed.member_index != index) ||
        (placed.member_id != member.id) ||
        !valid_target(placed.card_bounds) ||
        !layout.panel_bounds.contains(placed.card_bounds) ||
        placed.accessibility_text.empty() ||
        member.focus_identifier.empty() || member.select_command.empty() ||
        !member_ids.emplace(member.id).second ||
        !region_ids.emplace(region.value).second ||
        !focus_identifiers.emplace(member.focus_identifier).second ||
        !command_identifiers.emplace(member.select_command).second ||
        !tab_orders.emplace(member.tab_order).second) {
      return {};
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (interiors_overlap(
              placed.card_bounds, layout.members[prior].card_bounds)) {
        return {};
      }
    }
    if (member.selected) {
      ++selected_count;
      if (!party.selected_member ||
          (*party.selected_member != member.id)) {
        return {};
      }
    }
  }
  if ((selected_count > 1U) ||
      (party.selected_member.has_value() != (selected_count == 1U))) {
    return {};
  }

  std::vector<PartyRailControlPlacement> controls;
  controls.reserve(party.members.size());
  for (size_t index = 0; index < party.members.size(); ++index) {
    const auto& member = party.members[index];
    const auto& placed = layout.members[index];
    const std::string visible_name = member.name.empty()
        ? placed.name_text
        : member.name;
    controls.emplace_back(PartyRailControlPlacement{
        .region = party_member_region_id(member.id),
        .bounds = placed.card_bounds,
        .member_id = member.id,
        .label = visible_name,
        .accessibility_label = "Select " + visible_name + "; " +
            placed.accessibility_text,
        .focus_identifier = member.focus_identifier,
        .command_identifier = member.select_command,
        .tab_order = member.tab_order,
        // Selection is intentionally idempotent. The currently selected card
        // remains enabled and dispatches the same typed action as every peer.
        .enabled = request.selection_available,
        .selected = member.selected,
        .payload = SelectPartyMemberAction{member.id},
    });
  }
  return controls;
}

} // namespace realmz::presentation
