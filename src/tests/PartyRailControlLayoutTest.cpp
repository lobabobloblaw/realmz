#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "presentation/PartyRailControlLayout.hpp"
#include "presentation/ResponsiveLayout.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

[[nodiscard]] TypographyModel typography() {
  return {
      .scale = 1.0,
      .caption = {13.0, 17.0},
      .body = {16.0, 22.0},
      .heading = {20.0, 26.0},
  };
}

[[nodiscard]] PartyRailModel party(size_t count, size_t selected = 0U) {
  PartyRailModel result;
  result.selected_member = static_cast<PartyMemberId>(selected);
  result.members.reserve(count);
  for (size_t index = 0; index < count; ++index) {
    result.members.emplace_back(PartyRailMemberModel{
        .id = static_cast<PartyMemberId>(index),
        .name = "Member " + std::to_string(index + 1U),
        .level = static_cast<int16_t>(index + 1U),
        .stamina = {
            .current = 10,
            .maximum = 20,
            .fill_fraction = 0.5,
        },
        .selected = index == selected,
        .conscious = true,
        .focus_identifier = "focus.party.member." +
            std::to_string(index) + "." + std::to_string(index),
        .select_command = "party.select." + std::to_string(index),
        .tab_order = 100 + static_cast<int32_t>(index),
    });
  }
  return result;
}

[[nodiscard]] LogicalRect wide_party_panel() {
  return compute_responsive_layout({
      .window_size = {1360.0, 768.0},
      .gameplay_content_size = {480.0, 416.0},
      .visible_tiles = {15, 13},
      .backing_scale = 1.0,
  }).party_rail;
}

[[nodiscard]] PartyRailLayout layout_for(const PartyRailModel& model) {
  return compute_party_rail_layout({
      .party_panel = wide_party_panel(),
      .members = model.members,
      .typography = typography(),
  });
}

void verify_controls(ScreenContext screen, size_t count) {
  const auto model = party(count, count - 1U);
  const auto layout = layout_for(model);
  const auto controls = compute_party_rail_control_layout(
      {.screen = screen, .selection_available = true}, model, layout);
  CHECK(controls.size() == count);

  std::set<uint32_t> regions;
  std::set<std::string> focus_identifiers;
  for (size_t index = 0; index < count; ++index) {
    const auto& member = model.members[index];
    const auto& control = controls[index];
    CHECK(control.bounds == layout.members[index].card_bounds);
    CHECK(control.bounds.width >= 44.0);
    CHECK(control.bounds.height >= 44.0);
    CHECK(control.region == party_member_region_id(member.id));
    CHECK(control.region.value == 2000U + member.id);
    CHECK(regions.emplace(control.region.value).second);
    CHECK(control.member_id == member.id);
    CHECK(control.label == member.name);
    CHECK(control.accessibility_label == "Select " + member.name);
    CHECK(control.focus_identifier == member.focus_identifier);
    CHECK(focus_identifiers.emplace(control.focus_identifier).second);
    CHECK(control.command_identifier == member.select_command);
    CHECK(control.tab_order == member.tab_order);
    CHECK(control.enabled);
    CHECK(control.selected == member.selected);
    CHECK(std::holds_alternative<SelectPartyMemberAction>(control.payload));
    CHECK(std::get<SelectPartyMemberAction>(control.payload).member ==
        member.id);
  }

  // Recomposition is deterministic, and the selected card remains an enabled
  // idempotent target with its ordinary SelectPartyMemberAction payload.
  CHECK(controls == compute_party_rail_control_layout(
      {.screen = screen, .selection_available = true}, model, layout));
  CHECK(controls.back().selected);
  CHECK(controls.back().enabled);
  CHECK(std::get<SelectPartyMemberAction>(controls.back().payload).member ==
      model.selected_member);
}

void test_one_to_six_controls_on_top_level_gameplay_screens() {
  for (const auto screen : {
           ScreenContext::exploration,
           ScreenContext::dungeon,
       }) {
    for (size_t count = 1; count <= 6; ++count) {
      verify_controls(screen, count);
    }
  }
}

void test_exact_six_member_1360_by_768_draw_and_hit_geometry() {
  const auto model = party(6, 3);
  const auto layout = layout_for(model);
  const auto controls = compute_party_rail_control_layout(
      {.screen = ScreenContext::exploration,
       .selection_available = true},
      model,
      layout);
  CHECK(controls.size() == 6);
  for (size_t index = 0; index < controls.size(); ++index) {
    CHECK(controls[index].bounds == layout.members[index].card_bounds);
    CHECK(controls[index].bounds.width >= 44.0);
    CHECK(controls[index].bounds.height >= 44.0);
  }
}

void test_availability_and_screen_exclusions_are_fail_closed() {
  const auto model = party(3, 1);
  const auto layout = layout_for(model);
  const auto unavailable = compute_party_rail_control_layout(
      {.screen = ScreenContext::exploration,
       .selection_available = false},
      model,
      layout);
  CHECK(unavailable.size() == model.members.size());
  CHECK(std::ranges::none_of(
      unavailable, &PartyRailControlPlacement::enabled));

  constexpr std::array excluded{
      ScreenContext::title,
      ScreenContext::party_selection,
      ScreenContext::party_creation,
      ScreenContext::combat,
      ScreenContext::inventory,
      ScreenContext::shop,
      ScreenContext::encounter,
      ScreenContext::ending,
  };
  for (const auto screen : excluded) {
    CHECK(compute_party_rail_control_layout(
        {.screen = screen, .selection_available = true}, model, layout)
        .empty());
  }
}

template <typename Mutation>
void verify_malformed_input_returns_no_partial_controls(Mutation mutation) {
  auto model = party(3, 1);
  auto layout = layout_for(model);
  mutation(model, layout);
  CHECK(compute_party_rail_control_layout(
      {.screen = ScreenContext::exploration,
       .selection_available = true},
      model,
      layout)
      .empty());
}

void test_malformed_semantics_and_geometry_never_return_partials() {
  verify_malformed_input_returns_no_partial_controls(
      [](auto&, auto& layout) { layout.members.pop_back(); });
  verify_malformed_input_returns_no_partial_controls(
      [](auto&, auto& layout) { layout.members[1].member_id = 5; });
  verify_malformed_input_returns_no_partial_controls(
      [](auto&, auto& layout) { layout.members[1].member_index = 0; });
  verify_malformed_input_returns_no_partial_controls(
      [](auto&, auto& layout) { layout.members[1].card_bounds.height = 43.99; });
  verify_malformed_input_returns_no_partial_controls(
      [](auto& model, auto&) {
        model.members[1].id = model.members[0].id;
      });
  verify_malformed_input_returns_no_partial_controls(
      [](auto& model, auto&) {
        model.members[1].focus_identifier =
            model.members[0].focus_identifier;
      });
  verify_malformed_input_returns_no_partial_controls(
      [](auto& model, auto&) {
        model.members[1].select_command = model.members[0].select_command;
      });
  verify_malformed_input_returns_no_partial_controls(
      [](auto& model, auto&) {
        model.members[1].tab_order = model.members[0].tab_order;
      });
  verify_malformed_input_returns_no_partial_controls(
      [](auto& model, auto&) { model.members[1].selected = false; });
}

} // namespace

int main() {
  try {
    test_one_to_six_controls_on_top_level_gameplay_screens();
    test_exact_six_member_1360_by_768_draw_and_hit_geometry();
    test_availability_and_screen_exclusions_are_fail_closed();
    test_malformed_semantics_and_geometry_never_return_partials();
    std::cout << "PartyRailControlLayoutTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "PartyRailControlLayoutTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
