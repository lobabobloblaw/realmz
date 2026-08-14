#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "presentation/ShellKeyboardInteraction.hpp"

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

ShellControlPlacement control(
    uint32_t region,
    std::string identifier,
    int32_t tab_order,
    MovementCommand command,
    bool enabled = true) {
  return {
      .region = ShellRegionId{region},
      .kind = ShellControlKind::movement,
      .bounds = {static_cast<double>(region), 10.0, 48.0, 48.0},
      .label = identifier,
      .accessibility_label = "Activate " + identifier,
      .focus_identifier = std::move(identifier),
      .tab_order = tab_order,
      .enabled = enabled,
      .payload = MovePartyAction{command},
  };
}

ShellControlPlacement party_control(
    uint32_t region,
    std::string identifier,
    int32_t tab_order,
    PartyMemberId member,
    bool enabled = true) {
  return {
      .region = ShellRegionId{region},
      .kind = ShellControlKind::party_member,
      .bounds = {static_cast<double>(region), 10.0, 48.0, 48.0},
      .label = identifier,
      .accessibility_label = "Select " + identifier,
      .focus_identifier = std::move(identifier),
      .tab_order = tab_order,
      .enabled = enabled,
      .payload = SelectPartyMemberAction{member},
  };
}

std::vector<ShellControlPlacement> controls() {
  return {
      control(10, "west", 30, MovementCommand::west),
      control(11, "north", 10, MovementCommand::north),
      control(12, "east", 20, MovementCommand::east),
      control(13, "disabled", 5, MovementCommand::south, false),
  };
}

ShellKeyboardEvent down(
    ShellKeyboardKey key,
    bool shift = false,
    bool repeat = false,
    uint32_t scancode = 1,
    uint32_t keyboard = 7) {
  return {{keyboard, scancode}, key, ShellKeyboardPhase::down, shift, repeat};
}

ShellKeyboardEvent up(
    ShellKeyboardKey key,
    uint32_t scancode = 1,
    uint32_t keyboard = 7) {
  return {{keyboard, scancode}, key, ShellKeyboardPhase::up, false, false};
}

void test_tab_traversal_and_stable_recomposition() {
  auto items = controls();
  ShellKeyboardInteraction keyboard;

  auto result = keyboard.handle(down(ShellKeyboardKey::tab), items, true);
  CHECK(result.consumed);
  CHECK(result.visual_state_changed);
  CHECK(!result.invoked_control);
  CHECK(keyboard.focused_identifier() == std::optional<std::string>{"north"});
  CHECK(keyboard.owns_key(ShellKeyboardKey::tab));

  result = keyboard.handle(down(ShellKeyboardKey::tab, false, true), items, true);
  CHECK(result.consumed);
  CHECK(!result.visual_state_changed);
  CHECK(keyboard.focused_identifier() == std::optional<std::string>{"north"});

  result = keyboard.handle(up(ShellKeyboardKey::tab), items, true);
  CHECK(result.consumed);
  CHECK(!keyboard.owns_key(ShellKeyboardKey::tab));
  CHECK(!result.invoked_control);

  result = keyboard.handle(down(ShellKeyboardKey::tab), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.focused_identifier() == std::optional<std::string>{"east"});
  CHECK(keyboard.handle(up(ShellKeyboardKey::tab), items, true).consumed);

  result = keyboard.handle(down(ShellKeyboardKey::tab, true), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.focused_identifier() == std::optional<std::string>{"north"});
  CHECK(keyboard.handle(up(ShellKeyboardKey::tab), items, true).consumed);

  result = keyboard.handle(down(ShellKeyboardKey::tab, true), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.focused_identifier() == std::optional<std::string>{"west"});
  CHECK(keyboard.handle(up(ShellKeyboardKey::tab), items, true).consumed);

  std::ranges::reverse(items);
  CHECK(!keyboard.reconcile(items, true));
  CHECK(keyboard.focused_identifier() == std::optional<std::string>{"west"});
  result = keyboard.handle(down(ShellKeyboardKey::tab), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.focused_identifier() == std::optional<std::string>{"north"});
  CHECK(keyboard.handle(up(ShellKeyboardKey::tab), items, true).consumed);
}

void test_enter_and_space_activation() {
  auto items = controls();
  ShellKeyboardInteraction keyboard;
  CHECK(keyboard.focus_control("east", items, true));

  auto result = keyboard.handle(down(ShellKeyboardKey::enter), items, true);
  CHECK(result.consumed);
  CHECK(result.visual_state_changed);
  CHECK(!result.invoked_control);
  CHECK(keyboard.pressed_identifier() ==
      std::optional<std::string_view>{"east"});

  for (int repeat = 0; repeat < 8; ++repeat) {
    result = keyboard.handle(
        down(ShellKeyboardKey::enter, false, true), items, true);
    CHECK(result.consumed);
    CHECK(!result.invoked_control);
    CHECK(keyboard.pressed_identifier() ==
        std::optional<std::string_view>{"east"});
  }

  result = keyboard.handle(up(ShellKeyboardKey::enter, 1), items, true);
  CHECK(result.consumed);
  CHECK(result.visual_state_changed);
  CHECK(result.invoked_control.has_value());
  CHECK(result.invoked_control->focus_identifier == "east");
  CHECK(std::get<MovePartyAction>(result.invoked_control->payload).command ==
      MovementCommand::east);
  CHECK(!keyboard.pressed_identifier());
  CHECK(!keyboard.owns_key(ShellKeyboardKey::enter));

  result = keyboard.handle(up(ShellKeyboardKey::enter, 2), items, true);
  CHECK(!result.consumed);
  CHECK(!result.invoked_control);

  result = keyboard.handle(down(ShellKeyboardKey::space), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.pressed_identifier() ==
      std::optional<std::string_view>{"east"});
  result = keyboard.handle(up(ShellKeyboardKey::enter, 2), items, true);
  CHECK(!result.consumed);
  CHECK(!result.invoked_control);
  CHECK(keyboard.pressed_identifier() ==
      std::optional<std::string_view>{"east"});
  result = keyboard.handle(up(ShellKeyboardKey::space), items, true);
  CHECK(result.consumed);
  CHECK(result.invoked_control.has_value());
}

void test_fail_closed_cancellation_and_key_ownership() {
  auto items = controls();
  ShellKeyboardInteraction keyboard;

  auto result = keyboard.handle(down(ShellKeyboardKey::enter), items, true);
  CHECK(!result.consumed);
  CHECK(!keyboard.focused_identifier());

  CHECK(keyboard.focus_control("north", items, true));
  result = keyboard.handle(down(ShellKeyboardKey::enter), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.owns_key(ShellKeyboardKey::enter));
  CHECK(keyboard.reconcile(items, false));
  CHECK(!keyboard.focused_identifier());
  CHECK(!keyboard.pressed_identifier());
  CHECK(keyboard.owns_key(ShellKeyboardKey::enter));
  result = keyboard.handle(up(ShellKeyboardKey::enter), items, false);
  CHECK(result.consumed);
  CHECK(!result.invoked_control);
  CHECK(!keyboard.owns_key(ShellKeyboardKey::enter));

  CHECK(keyboard.focus_control("north", items, true));
  CHECK(keyboard.handle(down(ShellKeyboardKey::space), items, true).consumed);
  const auto north = std::ranges::find_if(items, [](const auto& item) {
    return item.focus_identifier == "north";
  });
  north->enabled = false;
  CHECK(keyboard.reconcile(items, true));
  CHECK(!keyboard.focused_identifier());
  CHECK(!keyboard.pressed_identifier());
  CHECK(keyboard.owns_key(ShellKeyboardKey::space));
  result = keyboard.handle(up(ShellKeyboardKey::space), items, true);
  CHECK(result.consumed);
  CHECK(!result.invoked_control);

  CHECK(!keyboard.handle(down(ShellKeyboardKey::tab), items, false).consumed);
  CHECK(!keyboard.handle(up(ShellKeyboardKey::tab), items, false).consumed);
}

void test_payload_replacement_and_reset() {
  auto items = controls();
  ShellKeyboardInteraction keyboard;
  CHECK(keyboard.focus_control("west", items, true));
  CHECK(keyboard.handle(down(ShellKeyboardKey::enter), items, true).consumed);
  auto west = std::ranges::find_if(items, [](const auto& item) {
    return item.focus_identifier == "west";
  });
  west->payload = MovePartyAction{MovementCommand::south};
  CHECK(keyboard.reconcile(items, true));
  CHECK(!keyboard.focused_identifier());
  CHECK(!keyboard.pressed_identifier());
  auto result = keyboard.handle(up(ShellKeyboardKey::enter), items, true);
  CHECK(result.consumed);
  CHECK(!result.invoked_control);

  CHECK(keyboard.handle(down(ShellKeyboardKey::tab), items, true).consumed);
  CHECK(keyboard.owns_key(ShellKeyboardKey::tab));
  keyboard.reset();
  CHECK(!keyboard.focused_identifier());
  CHECK(!keyboard.pressed_identifier());
  CHECK(!keyboard.owns_key(ShellKeyboardKey::tab));
  CHECK(!keyboard.handle(up(ShellKeyboardKey::tab), items, true).consumed);
}

void test_duplicate_identifier_is_not_activatable() {
  auto items = controls();
  items.emplace_back(control(
      99, "north", 15, MovementCommand::south));
  ShellKeyboardInteraction keyboard;
  CHECK(!keyboard.focus_control("north", items, true));
  CHECK(!keyboard.handle(down(ShellKeyboardKey::enter), items, true).consumed);
}

void test_sticky_cancellation_and_reappearance() {
  auto items = controls();
  ShellKeyboardInteraction keyboard;
  CHECK(keyboard.focus_control("east", items, true));
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::space, false, false, 21), items, true)
      .consumed);

  const auto east = std::ranges::find_if(items, [](const auto& item) {
    return item.focus_identifier == "east";
  });
  const auto saved = *east;
  items.erase(east);
  CHECK(keyboard.reconcile(items, true));
  items.emplace_back(saved);
  CHECK(!keyboard.reconcile(items, true));
  const auto release = keyboard.handle(
      up(ShellKeyboardKey::space, 21), items, true);
  CHECK(release.consumed);
  CHECK(!release.invoked_control);

  CHECK(keyboard.focus_control("west", items, true));
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::enter, false, false, 22), items, true)
      .consumed);
  CHECK(keyboard.focus_control("north", items, true));
  CHECK(keyboard.handle(
      up(ShellKeyboardKey::enter, 22), items, true)
      .consumed);

  CHECK(keyboard.focus_control("west", items, true));
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::space, false, false, 23), items, true)
      .consumed);
  CHECK(keyboard.clear_focus());
  const auto cleared_release = keyboard.handle(
      up(ShellKeyboardKey::space, 23), items, true);
  CHECK(cleared_release.consumed);
  CHECK(!cleared_release.invoked_control);
}

void test_simultaneous_tokens_and_keycode_remap() {
  auto items = controls();
  ShellKeyboardInteraction keyboard;
  CHECK(keyboard.focus_control("north", items, true));
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::enter, false, false, 30), items, true)
      .consumed);
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::space, false, false, 31), items, true)
      .consumed);
  CHECK(keyboard.owns_token({7, 30}));
  CHECK(keyboard.owns_token({7, 31}));
  auto result = keyboard.handle(
      up(ShellKeyboardKey::space, 31), items, true);
  CHECK(result.consumed);
  CHECK(!result.invoked_control);
  result = keyboard.handle(up(ShellKeyboardKey::tab, 30), items, true);
  CHECK(result.consumed);
  CHECK(result.invoked_control.has_value());
  CHECK(result.invoked_control->focus_identifier == "north");

  CHECK(keyboard.handle(
      down(ShellKeyboardKey::tab, false, false, 40, 1), items, true)
      .consumed);
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::tab, false, false, 40, 2), items, true)
      .consumed);
  CHECK(keyboard.owns_token({1, 40}));
  CHECK(keyboard.owns_token({2, 40}));
  CHECK(keyboard.handle(
      up(ShellKeyboardKey::tab, 40, 1), items, true)
      .consumed);
  CHECK(keyboard.owns_token({2, 40}));
  CHECK(keyboard.handle(
      up(ShellKeyboardKey::tab, 40, 2), items, true)
      .consumed);
}

void test_tab_cancels_activation_and_route_tombstones() {
  auto items = controls();
  ShellKeyboardInteraction keyboard;
  CHECK(keyboard.focus_control("west", items, true));
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::space, false, false, 50), items, true)
      .consumed);
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::tab, false, false, 51), items, true)
      .consumed);
  CHECK(!keyboard.pressed_identifier());
  CHECK(keyboard.focused_identifier() ==
      std::optional<std::string>{"north"});
  auto result = keyboard.handle(
      up(ShellKeyboardKey::space, 50), items, true);
  CHECK(result.consumed);
  CHECK(!result.invoked_control);
  CHECK(keyboard.handle(
      up(ShellKeyboardKey::tab, 51), items, true)
      .consumed);

  CHECK(keyboard.focus_control("east", items, true));
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::enter, false, false, 52), items, true)
      .consumed);
  keyboard.cancel_route();
  CHECK(keyboard.owns_token({7, 52}));
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::enter, false, true, 52), items, true)
      .consumed);
  CHECK(!keyboard.handle(
      down(ShellKeyboardKey::enter, false, false, 52), items, false)
      .consumed);
  CHECK(!keyboard.owns_token({7, 52}));
  CHECK(!keyboard.handle(
      up(ShellKeyboardKey::enter, 52), items, false)
      .consumed);

  CHECK(keyboard.focus_control("east", items, true));
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::space, false, false, 53), items, true)
      .consumed);
  keyboard.cancel_route();
  result = keyboard.handle(
      up(ShellKeyboardKey::space, 53), items, false);
  CHECK(result.consumed);
  CHECK(!result.invoked_control);

  CHECK(!keyboard.handle(
      down(ShellKeyboardKey::tab, false, true, 60), items, true)
      .consumed);
}

void test_equal_order_and_initial_reverse() {
  auto items = controls();
  items[0].tab_order = 10;
  items[1].tab_order = 10;
  items[2].tab_order = 10;
  ShellKeyboardInteraction keyboard;
  auto result = keyboard.handle(
      down(ShellKeyboardKey::tab, true, false, 70), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.focused_identifier() ==
      std::optional<std::string>{"east"});
  CHECK(keyboard.handle(
      up(ShellKeyboardKey::tab, 70), items, true)
      .consumed);
  result = keyboard.handle(
      down(ShellKeyboardKey::tab, false, false, 71), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.focused_identifier() ==
      std::optional<std::string>{"west"});
}

void test_party_cards_precede_movement_and_dispatch_exact_payload() {
  std::vector<ShellControlPlacement> items{
      control(10, "north", 1000, MovementCommand::north),
      party_control(2001, "party-member-1", 101, 1),
      party_control(2000, "party-member-0", 100, 0),
  };
  ShellKeyboardInteraction keyboard;

  auto result = keyboard.handle(
      down(ShellKeyboardKey::tab, false, false, 80), items, true);
  CHECK(result.consumed);
  CHECK(keyboard.focused_identifier() ==
      std::optional<std::string>{"party-member-0"});
  CHECK(keyboard.handle(
      up(ShellKeyboardKey::tab, 80), items, true)
      .consumed);

  result = keyboard.handle(
      down(ShellKeyboardKey::space, false, false, 81), items, true);
  CHECK(result.consumed);
  CHECK(!result.invoked_control);
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::space, false, true, 81), items, true)
      .consumed);
  result = keyboard.handle(
      up(ShellKeyboardKey::space, 81), items, true);
  CHECK(result.consumed);
  CHECK(result.invoked_control.has_value());
  CHECK(result.invoked_control->kind == ShellControlKind::party_member);
  CHECK(std::get<SelectPartyMemberAction>(
            result.invoked_control->payload)
            .member == 0);

  CHECK(keyboard.handle(
      down(ShellKeyboardKey::tab, false, false, 82), items, true)
      .consumed);
  CHECK(keyboard.focused_identifier() ==
      std::optional<std::string>{"party-member-1"});
  CHECK(keyboard.handle(
      up(ShellKeyboardKey::tab, 82), items, true)
      .consumed);
  CHECK(keyboard.handle(
      down(ShellKeyboardKey::tab, false, false, 83), items, true)
      .consumed);
  CHECK(keyboard.focused_identifier() ==
      std::optional<std::string>{"north"});
}

} // namespace

int main() {
  try {
    test_tab_traversal_and_stable_recomposition();
    test_enter_and_space_activation();
    test_fail_closed_cancellation_and_key_ownership();
    test_payload_replacement_and_reset();
    test_duplicate_identifier_is_not_activatable();
    test_sticky_cancellation_and_reappearance();
    test_simultaneous_tokens_and_keycode_remap();
    test_tab_cancels_activation_and_route_tombstones();
    test_equal_order_and_initial_reverse();
    test_party_cards_precede_movement_and_dispatch_exact_payload();
    std::cout << "ShellKeyboardInteractionTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ShellKeyboardInteractionTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
