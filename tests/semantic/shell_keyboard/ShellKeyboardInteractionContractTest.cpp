#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "presentation/LegacyCommandBridge.hpp"
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

class RecordingBridge final : public LegacyCommandBridge {
public:
  RecordingBridge()
      : delegate_(make_handlers()) {}

  [[nodiscard]] DispatchResult dispatch(const UIAction& action) override {
    actions_.emplace_back(action);
    return delegate_.dispatch(action);
  }

  [[nodiscard]] const std::vector<UIAction>& actions() const noexcept {
    return actions_;
  }

private:
  [[nodiscard]] static LegacyActionHandlers make_handlers() {
    LegacyActionHandlers handlers;
    handlers.move_party = [](const MovePartyAction&) {
      return DispatchResult::handled();
    };
    handlers.open_inventory = [](const OpenInventoryAction&) {
      return DispatchResult::handled();
    };
    handlers.open_spellbook = [](const OpenSpellbookAction&) {
      return DispatchResult::handled();
    };
    handlers.open_save_game = [](const OpenSaveGameAction&) {
      return DispatchResult::handled();
    };
    handlers.open_load_game = [](const OpenLoadGameAction&) {
      return DispatchResult::handled();
    };
    handlers.guard_combatant = [](const GuardCombatantAction&) {
      return DispatchResult::handled();
    };
    handlers.finish_combatant = [](const FinishCombatantAction&) {
      return DispatchResult::handled();
    };
    handlers.delay_combatant = [](const DelayCombatantAction&) {
      return DispatchResult::handled();
    };
    handlers.center_active_combatant = [](
        const CenterActiveCombatantAction&) {
      return DispatchResult::handled();
    };
    handlers.switch_weapon_set = [](const SwitchWeaponSetAction&) {
      return DispatchResult::handled();
    };
    handlers.cycle_combat_focus = [](const CycleCombatFocusAction&) {
      return DispatchResult::handled();
    };
    handlers.open_combat_items = [](const OpenCombatItemsAction&) {
      return DispatchResult::handled();
    };
    handlers.auto_combatant = [](const AutoCombatantAction&) {
      return DispatchResult::handled();
    };
    handlers.show_combat_range = [](const ShowCombatRangeAction&) {
      return DispatchResult::handled();
    };
    handlers.bandage_combatant = [](const BandageCombatantAction&) {
      return DispatchResult::handled();
    };
    handlers.undo_combatant = [](const UndoCombatantAction&) {
      return DispatchResult::handled();
    };
    handlers.open_combat_spellbook = [](const OpenCombatSpellbookAction&) {
      return DispatchResult::handled();
    };
    handlers.open_combat_targeting = [](const OpenCombatTargetingAction&) {
      return DispatchResult::handled();
    };
    return handlers;
  }

  InjectedLegacyCommandBridge delegate_;
  std::vector<UIAction> actions_;
};

[[nodiscard]] ShellControlPlacement movement_control(
    uint32_t region,
    std::string identifier,
    int32_t tab_order,
    MovementCommand command,
    bool enabled = true) {
  return ShellControlPlacement{
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

[[nodiscard]] std::vector<ShellControlPlacement> canonical_controls() {
  return {
      movement_control(10, "move.west", 30, MovementCommand::west),
      movement_control(11, "move.north", 10, MovementCommand::north),
      movement_control(12, "move.east", 20, MovementCommand::east),
      movement_control(
          13, "move.disabled", 5, MovementCommand::south, false),
      // Equal order deliberately proves stable insertion ordering.
      movement_control(
          14, "move.forward", 30, MovementCommand::step_forward),
  };
}

constexpr ShellPhysicalKeyToken kTabToken{7, 43};
constexpr ShellPhysicalKeyToken kEnterToken{7, 40};
constexpr ShellPhysicalKeyToken kSpaceToken{7, 44};
constexpr ShellPhysicalKeyToken kOtherToken{7, 41};
// Same scancode as Enter, but a distinct physical keyboard.
constexpr ShellPhysicalKeyToken kSecondKeyboardToken{8, 40};

[[nodiscard]] ShellKeyboardEvent key_down(
    ShellKeyboardKey key,
    ShellPhysicalKeyToken token,
    bool shift = false,
    bool repeat = false) {
  return ShellKeyboardEvent{
      .token = token,
      .key = key,
      .phase = ShellKeyboardPhase::down,
      .shift = shift,
      .repeat = repeat,
  };
}

[[nodiscard]] ShellKeyboardEvent key_up(
    ShellKeyboardKey key,
    ShellPhysicalKeyToken token) {
  return ShellKeyboardEvent{
      .token = token,
      .key = key,
      .phase = ShellKeyboardPhase::up,
  };
}

struct RoutedKeyResult {
  ShellKeyboardResult shell;
  std::optional<DispatchResult> dispatch;
};

// This is only an observation harness. All keyboard ownership, traversal,
// capture, cancellation, and release decisions are made by the production
// ShellKeyboardInteraction instance.
class ProductionKeyboardHarness {
public:
  explicit ProductionKeyboardHarness(RecordingBridge& bridge)
      : bridge_(bridge), controls_(canonical_controls()) {
    (void)keyboard_.reconcile(controls_, route_enabled_);
  }

  [[nodiscard]] RoutedKeyResult handle(
      const ShellKeyboardEvent& event,
      bool dispatch_invoked_control = true) {
    RoutedKeyResult routed{
        .shell = keyboard_.handle(event, controls_, route_enabled_),
        .dispatch = std::nullopt,
    };
    if (routed.shell.invoked_control && dispatch_invoked_control) {
      routed.dispatch = bridge_.dispatch(UIAction{
          .sequence = next_sequence_++,
          .payload = routed.shell.invoked_control->payload,
      });
    }
    return routed;
  }

  [[nodiscard]] bool recompose(
      std::vector<ShellControlPlacement> controls,
      bool route_enabled = true) {
    controls_ = std::move(controls);
    route_enabled_ = route_enabled;
    return keyboard_.reconcile(controls_, route_enabled_);
  }

  [[nodiscard]] bool set_route_enabled(bool enabled) {
    route_enabled_ = enabled;
    return keyboard_.reconcile(controls_, route_enabled_);
  }

  [[nodiscard]] bool focus(std::string_view identifier) {
    return keyboard_.focus_control(
        identifier, controls_, route_enabled_);
  }

  [[nodiscard]] ShellKeyboardInteraction& keyboard() noexcept {
    return keyboard_;
  }

  [[nodiscard]] const std::vector<ShellControlPlacement>& controls()
      const noexcept {
    return controls_;
  }

private:
  RecordingBridge& bridge_;
  ShellKeyboardInteraction keyboard_;
  std::vector<ShellControlPlacement> controls_;
  bool route_enabled_ = true;
  ActionSequence next_sequence_ = 1;
};

void release_tab(ProductionKeyboardHarness& harness) {
  const auto release = harness.handle(
      key_up(ShellKeyboardKey::tab, kTabToken));
  CHECK(release.shell.consumed);
  CHECK(!release.shell.invoked_control);
  CHECK(!release.dispatch);
}

void test_forward_reverse_traversal_and_disabled_skipping() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  CHECK(!harness.keyboard().focused_identifier());

  for (const char* const expected : {
           "move.north", "move.east", "move.west", "move.forward",
           "move.north"}) {
    const auto down = harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken));
    CHECK(down.shell.consumed);
    CHECK(harness.keyboard().focused_identifier() == expected);
    release_tab(harness);
  }

  CHECK(harness.keyboard().clear_focus());
  for (const char* const expected : {
           "move.forward", "move.west", "move.east", "move.north",
           "move.forward"}) {
    const auto down = harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken, true));
    CHECK(down.shell.consumed);
    CHECK(harness.keyboard().focused_identifier() == expected);
    release_tab(harness);
  }
  CHECK(bridge.actions().empty());
}

void test_tab_repeat_and_release_ownership() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);

  CHECK(harness.handle(
      key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
  CHECK(harness.keyboard().focused_identifier() == "move.north");
  for (int repeat = 0; repeat < 5; ++repeat) {
    const auto result = harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken, false, true));
    CHECK(result.shell.consumed);
    CHECK(!result.shell.invoked_control);
    CHECK(harness.keyboard().focused_identifier() == "move.north");
  }
  CHECK(harness.keyboard().owns_token(kTabToken));
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::enter, kOtherToken)).shell.consumed);
  release_tab(harness);
  CHECK(!harness.keyboard().owns_token(kTabToken));
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
  CHECK(bridge.actions().empty());
}

void test_focus_survives_nonsemantic_recomposition_by_identifier() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  CHECK(harness.focus("move.west"));

  auto rebuilt = harness.controls();
  std::ranges::reverse(rebuilt);
  const auto west = std::ranges::find_if(rebuilt, [](const auto& control) {
    return control.focus_identifier == "move.west";
  });
  west->tab_order = 500;
  west->bounds = {900.0, 700.0, 80.0, 52.0};
  west->label = "WEST";
  west->accessibility_label = "Move west now";
  CHECK(!harness.recompose(std::move(rebuilt)));
  CHECK(harness.keyboard().focused_identifier() == "move.west");

  const auto next = harness.handle(
      key_down(ShellKeyboardKey::tab, kTabToken));
  CHECK(next.shell.consumed);
  CHECK(harness.keyboard().focused_identifier() == "move.north");
  release_tab(harness);
  CHECK(bridge.actions().empty());
}

void test_enter_space_exactly_once_and_physical_release_pairing() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  CHECK(harness.focus("move.east"));

  const auto enter_down = harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken));
  CHECK(enter_down.shell.consumed);
  CHECK(!enter_down.shell.invoked_control);
  CHECK(!enter_down.dispatch);
  CHECK(bridge.actions().empty());
  for (int repeat = 0; repeat < 8; ++repeat) {
    const auto result = harness.handle(key_down(
        ShellKeyboardKey::enter, kEnterToken, false, true));
    CHECK(result.shell.consumed);
    CHECK(!result.shell.invoked_control);
    CHECK(bridge.actions().empty());
  }

  // A different physical token cannot release the capture.
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::enter, kOtherToken)).shell.consumed);
  CHECK(bridge.actions().empty());

  // Physical token is authoritative: a layout/keycode remap between down and
  // up must still consume and complete the original Enter activation.
  const auto remapped_release = harness.handle(
      key_up(ShellKeyboardKey::tab, kEnterToken));
  CHECK(remapped_release.shell.consumed);
  CHECK(remapped_release.shell.invoked_control.has_value());
  CHECK(remapped_release.dispatch.has_value());
  CHECK(remapped_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1);
  CHECK(bridge.actions()[0].sequence == 1);
  CHECK(std::get<MovePartyAction>(bridge.actions()[0].payload).command ==
      MovementCommand::east);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1);

  const auto space_down = harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken));
  CHECK(space_down.shell.consumed);
  CHECK(!space_down.dispatch);
  CHECK(harness.handle(key_down(
      ShellKeyboardKey::space, kSpaceToken, false, true)).shell.consumed);
  const auto space_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(space_release.shell.consumed);
  CHECK(space_release.shell.invoked_control.has_value());
  CHECK(space_release.dispatch.has_value());
  CHECK(bridge.actions().size() == 2);
  CHECK(bridge.actions()[1].sequence == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
}

void test_first_activation_wins_across_simultaneous_physical_keys() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  CHECK(harness.focus("move.north"));

  const auto first = harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken));
  CHECK(first.shell.consumed);
  CHECK(harness.keyboard().pressed_identifier() == "move.north");
  CHECK(harness.keyboard().owns_token(kEnterToken));

  const auto candidate = harness.handle(key_down(
      ShellKeyboardKey::space, kSecondKeyboardToken));
  CHECK(candidate.shell.consumed);
  CHECK(!candidate.shell.invoked_control);
  CHECK(!candidate.dispatch);
  CHECK(harness.keyboard().pressed_identifier() == "move.north");
  CHECK(harness.keyboard().owns_token(kSecondKeyboardToken));
  CHECK(harness.keyboard().owns_key(ShellKeyboardKey::enter));
  CHECK(harness.keyboard().owns_key(ShellKeyboardKey::space));

  CHECK(harness.handle(key_down(
      ShellKeyboardKey::space, kSecondKeyboardToken, false, true))
      .shell.consumed);
  const auto candidate_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSecondKeyboardToken));
  CHECK(candidate_release.shell.consumed);
  CHECK(!candidate_release.shell.invoked_control);
  CHECK(!candidate_release.dispatch);
  CHECK(!harness.keyboard().owns_token(kSecondKeyboardToken));
  CHECK(harness.keyboard().owns_token(kEnterToken));
  CHECK(harness.keyboard().pressed_identifier() == "move.north");
  CHECK(bridge.actions().empty());

  const auto first_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(first_release.shell.consumed);
  CHECK(first_release.shell.invoked_control.has_value());
  CHECK(first_release.dispatch.has_value());
  CHECK(bridge.actions().size() == 1);
  CHECK(std::get<MovePartyAction>(bridge.actions()[0].payload).command ==
      MovementCommand::north);
  CHECK(!harness.keyboard().owns_token(kEnterToken));
}

void test_open_inventory_payload_activates_exactly_once() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement inventory{
      .region = ShellRegionId{1100},
      .kind = ShellControlKind::open_inventory,
      .bounds = {20.0, 20.0, 80.0, 48.0},
      .label = "ITEMS",
      .accessibility_label = "Open inventory",
      .focus_identifier = "focus.action.inventory.open",
      .tab_order = 1100,
      .enabled = true,
      .payload = OpenInventoryAction{2},
  };
  CHECK(!harness.recompose({inventory}));
  CHECK(harness.focus(inventory.focus_identifier));

  const auto down = harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken));
  CHECK(down.shell.consumed);
  CHECK(!down.shell.invoked_control);
  CHECK(bridge.actions().empty());

  const auto up = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(up.shell.consumed);
  CHECK(up.shell.invoked_control.has_value());
  CHECK(up.shell.invoked_control->kind ==
      ShellControlKind::open_inventory);
  CHECK(up.dispatch.has_value());
  CHECK(up.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1);
  CHECK(std::get<OpenInventoryAction>(bridge.actions()[0].payload).member == 2);

  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1);
}

void test_open_spellbook_payload_activates_exactly_once() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement spellbook{
      .region = ShellRegionId{1101},
      .kind = ShellControlKind::open_spellbook,
      .bounds = {108.0, 20.0, 80.0, 48.0},
      .label = "SPELLS",
      .accessibility_label = "Cast spell",
      .focus_identifier = "focus.action.spellbook.open",
      .tab_order = 1101,
      .enabled = true,
      .payload = OpenSpellbookAction{2},
  };
  CHECK(!harness.recompose({spellbook}));
  CHECK(harness.focus(spellbook.focus_identifier));

  const auto down = harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken));
  CHECK(down.shell.consumed);
  CHECK(!down.shell.invoked_control);
  CHECK(bridge.actions().empty());

  const auto up = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(up.shell.consumed);
  CHECK(up.shell.invoked_control.has_value());
  CHECK(up.shell.invoked_control->kind ==
      ShellControlKind::open_spellbook);
  CHECK(up.dispatch.has_value());
  CHECK(up.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1);
  CHECK(std::get<OpenSpellbookAction>(bridge.actions()[0].payload).member == 2);

  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1);
}

void test_open_save_payload_activates_exactly_once() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement save{
      .region = ShellRegionId{1102},
      .kind = ShellControlKind::open_save_game,
      .bounds = {196.0, 20.0, 80.0, 48.0},
      .label = "SAVE",
      .accessibility_label = "Open save dialog",
      .focus_identifier = "focus.action.save.open",
      .tab_order = 1102,
      .enabled = true,
      .payload = OpenSaveGameAction{},
  };
  CHECK(!harness.recompose({save}));
  CHECK(harness.focus(save.focus_identifier));

  const auto down = harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken));
  CHECK(down.shell.consumed);
  CHECK(!down.shell.invoked_control);
  CHECK(bridge.actions().empty());

  const auto up = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(up.shell.consumed);
  CHECK(up.shell.invoked_control.has_value());
  CHECK(up.shell.invoked_control->kind ==
      ShellControlKind::open_save_game);
  CHECK(up.dispatch.has_value());
  CHECK(up.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1);
  CHECK(std::holds_alternative<OpenSaveGameAction>(
      bridge.actions()[0].payload));

  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1);
}

void test_open_load_payload_activates_exactly_once() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement load{
      .region = ShellRegionId{1103},
      .kind = ShellControlKind::open_load_game,
      .bounds = {284.0, 20.0, 80.0, 48.0},
      .label = "LOAD",
      .accessibility_label = "Open load dialog",
      .focus_identifier = "focus.action.load.open",
      .tab_order = 1103,
      .enabled = true,
      .payload = OpenLoadGameAction{},
  };
  CHECK(!harness.recompose({load}));
  CHECK(harness.focus(load.focus_identifier));

  const auto down = harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken));
  CHECK(down.shell.consumed);
  CHECK(!down.shell.invoked_control);
  CHECK(bridge.actions().empty());

  const auto up = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(up.shell.consumed);
  CHECK(up.shell.invoked_control.has_value());
  CHECK(up.shell.invoked_control->kind ==
      ShellControlKind::open_load_game);
  CHECK(up.dispatch.has_value());
  CHECK(up.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1);
  CHECK(std::holds_alternative<OpenLoadGameAction>(
      bridge.actions()[0].payload));

  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1);
}

void test_guard_payload_activates_exactly_once() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement guard{
      .region = ShellRegionId{1104},
      .kind = ShellControlKind::guard_combatant,
      .bounds = {20.0, 20.0, 120.0, 48.0},
      .label = "GUARD",
      .accessibility_label = "Guard active combatant",
      .focus_identifier = "focus.action.combat.guard",
      .tab_order = 1104,
      .enabled = true,
      .payload = GuardCombatantAction{2},
  };
  CHECK(!harness.recompose({guard}));
  CHECK(harness.focus(guard.focus_identifier));

  const auto down = harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken));
  CHECK(down.shell.consumed);
  CHECK(!down.shell.invoked_control);
  CHECK(bridge.actions().empty());

  const auto up = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(up.shell.consumed);
  CHECK(up.shell.invoked_control.has_value());
  CHECK(up.shell.invoked_control->kind ==
      ShellControlKind::guard_combatant);
  CHECK(up.dispatch.has_value());
  CHECK(up.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1);
  CHECK(std::get<GuardCombatantAction>(bridge.actions()[0].payload).combatant ==
      2);

  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1);
}

void test_finish_payload_orders_dispatches_and_cancels_stale_actor() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement guard{
      .region = ShellRegionId{1104},
      .kind = ShellControlKind::guard_combatant,
      .bounds = {20.0, 20.0, 120.0, 48.0},
      .label = "GUARD",
      .accessibility_label = "Guard active combatant",
      .focus_identifier = "focus.action.combat.guard",
      .tab_order = 1104,
      .enabled = true,
      .payload = GuardCombatantAction{2},
  };
  const ShellControlPlacement finish{
      .region = ShellRegionId{1105},
      .kind = ShellControlKind::finish_combatant,
      .bounds = {148.0, 20.0, 120.0, 48.0},
      .label = "FINISH",
      .accessibility_label = "Finish active combatant's turn",
      .focus_identifier = "focus.action.combat.finish",
      .tab_order = 1105,
      .enabled = true,
      .payload = FinishCombatantAction{2},
  };
  // Reversed insertion order proves that semantic tab order remains stable.
  CHECK(!harness.recompose({finish, guard}));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
  CHECK(harness.keyboard().focused_identifier() == guard.focus_identifier);
  release_tab(harness);
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
  CHECK(harness.keyboard().focused_identifier() == finish.focus_identifier);
  release_tab(harness);

  const auto down = harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken));
  CHECK(down.shell.consumed);
  CHECK(!down.shell.invoked_control);
  CHECK(bridge.actions().empty());
  const auto up = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(up.shell.consumed);
  CHECK(up.shell.invoked_control.has_value());
  CHECK(up.shell.invoked_control->kind ==
      ShellControlKind::finish_combatant);
  CHECK(up.dispatch.has_value());
  CHECK(up.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1);
  CHECK(std::get<FinishCombatantAction>(bridge.actions()[0].payload).combatant ==
      2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1);

  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.keyboard().pressed_identifier() == finish.focus_identifier);
  auto changed = harness.controls();
  const auto changed_finish = std::ranges::find_if(
      changed, [](const auto& control) {
        return control.kind == ShellControlKind::finish_combatant;
      });
  CHECK(changed_finish != changed.end());
  changed_finish->payload = FinishCombatantAction{3};
  CHECK(harness.recompose(std::move(changed)));
  CHECK(!harness.keyboard().focused_identifier());
  CHECK(!harness.keyboard().pressed_identifier());
  const auto stale_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_release.shell.consumed);
  CHECK(!stale_release.shell.invoked_control);
  CHECK(!stale_release.dispatch);
  CHECK(bridge.actions().size() == 1);
}

void test_delay_payload_orders_dispatches_and_cancels_recomposition() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement guard{
      .region = ShellRegionId{1104},
      .kind = ShellControlKind::guard_combatant,
      .bounds = {20.0, 20.0, 120.0, 48.0},
      .label = "GUARD",
      .accessibility_label = "Guard active combatant",
      .focus_identifier = "focus.action.combat.guard",
      .tab_order = 1104,
      .enabled = true,
      .payload = GuardCombatantAction{2},
  };
  const ShellControlPlacement finish{
      .region = ShellRegionId{1105},
      .kind = ShellControlKind::finish_combatant,
      .bounds = {148.0, 20.0, 120.0, 48.0},
      .label = "FINISH",
      .accessibility_label = "Finish active combatant's turn",
      .focus_identifier = "focus.action.combat.finish",
      .tab_order = 1105,
      .enabled = true,
      .payload = FinishCombatantAction{2},
  };
  const ShellControlPlacement delay{
      .region = ShellRegionId{1106},
      .kind = ShellControlKind::delay_combatant,
      .bounds = {276.0, 20.0, 120.0, 48.0},
      .label = "DELAY",
      .accessibility_label = "Delay active combatant's turn",
      .focus_identifier = "focus.action.combat.delay",
      .tab_order = 1106,
      .enabled = true,
      .payload = DelayCombatantAction{2},
  };

  // Reversed insertion proves the stable Guard, Finish, Delay tab order.
  CHECK(!harness.recompose({delay, finish, guard}));
  for (const auto& expected : {guard, finish, delay}) {
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
    CHECK(harness.keyboard().focused_identifier() ==
        expected.focus_identifier);
    release_tab(harness);
  }

  const auto down = harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken));
  CHECK(down.shell.consumed);
  CHECK(!down.shell.invoked_control);
  CHECK(bridge.actions().empty());
  const auto up = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(up.shell.consumed);
  CHECK(up.shell.invoked_control.has_value());
  CHECK(up.shell.invoked_control->kind ==
      ShellControlKind::delay_combatant);
  CHECK(up.dispatch.has_value());
  CHECK(up.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1U);
  CHECK(std::get<DelayCombatantAction>(bridge.actions()[0].payload).combatant ==
      2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1U);

  // Pointer focus uses this same production focus-control path.
  CHECK(harness.focus(guard.focus_identifier));
  CHECK(harness.focus(delay.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.keyboard().pressed_identifier() == delay.focus_identifier);
  auto changed = harness.controls();
  const auto changed_delay = std::ranges::find_if(
      changed, [](const auto& control) {
        return control.kind == ShellControlKind::delay_combatant;
      });
  CHECK(changed_delay != changed.end());
  changed_delay->payload = DelayCombatantAction{3};
  CHECK(harness.recompose(std::move(changed)));
  CHECK(!harness.keyboard().focused_identifier());
  CHECK(!harness.keyboard().pressed_identifier());
  const auto stale_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_release.shell.consumed);
  CHECK(!stale_release.shell.invoked_control);
  CHECK(!stale_release.dispatch);
  CHECK(bridge.actions().size() == 1U);

  CHECK(!harness.recompose({delay, finish, guard}));
  CHECK(harness.focus(delay.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = harness.controls();
  const auto disabled_delay = std::ranges::find_if(
      changed, [](const auto& control) {
        return control.kind == ShellControlKind::delay_combatant;
      });
  CHECK(disabled_delay != changed.end());
  disabled_delay->enabled = false;
  CHECK(harness.recompose(std::move(changed)));
  const auto disabled_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(disabled_release.shell.consumed);
  CHECK(!disabled_release.shell.invoked_control);
  CHECK(!disabled_release.dispatch);
  CHECK(bridge.actions().size() == 1U);
}

void test_center_payload_orders_dispatches_and_cancels_recomposition() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement guard{
      .region = ShellRegionId{1104},
      .kind = ShellControlKind::guard_combatant,
      .bounds = {20.0, 20.0, 120.0, 48.0},
      .label = "GUARD",
      .accessibility_label = "Guard active combatant",
      .focus_identifier = "focus.action.combat.guard",
      .tab_order = 1104,
      .enabled = true,
      .payload = GuardCombatantAction{2},
  };
  const ShellControlPlacement finish{
      .region = ShellRegionId{1105},
      .kind = ShellControlKind::finish_combatant,
      .bounds = {148.0, 20.0, 120.0, 48.0},
      .label = "FINISH",
      .accessibility_label = "Finish active combatant's turn",
      .focus_identifier = "focus.action.combat.finish",
      .tab_order = 1105,
      .enabled = true,
      .payload = FinishCombatantAction{2},
  };
  const ShellControlPlacement delay{
      .region = ShellRegionId{1106},
      .kind = ShellControlKind::delay_combatant,
      .bounds = {276.0, 20.0, 120.0, 48.0},
      .label = "DELAY",
      .accessibility_label = "Delay active combatant's turn",
      .focus_identifier = "focus.action.combat.delay",
      .tab_order = 1106,
      .enabled = true,
      .payload = DelayCombatantAction{2},
  };
  const ShellControlPlacement center{
      .region = ShellRegionId{1107},
      .kind = ShellControlKind::center_active_combatant,
      .bounds = {404.0, 20.0, 120.0, 48.0},
      .label = "CENTER",
      .accessibility_label = "Center view on active combatant",
      .focus_identifier = "focus.action.combat.center",
      .tab_order = 1107,
      .enabled = true,
      .payload = CenterActiveCombatantAction{2},
  };

  // Reversed insertion proves the stable four-action combat tab order.
  CHECK(!harness.recompose({center, delay, finish, guard}));
  for (const auto& expected : {guard, finish, delay, center}) {
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
    CHECK(harness.keyboard().focused_identifier() ==
        expected.focus_identifier);
    release_tab(harness);
  }

  const auto down = harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken));
  CHECK(down.shell.consumed);
  CHECK(!down.shell.invoked_control);
  CHECK(bridge.actions().empty());
  const auto up = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(up.shell.consumed);
  CHECK(up.shell.invoked_control.has_value());
  CHECK(up.shell.invoked_control->kind ==
      ShellControlKind::center_active_combatant);
  CHECK(up.dispatch.has_value());
  CHECK(up.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1U);
  CHECK(std::get<CenterActiveCombatantAction>(
      bridge.actions()[0].payload).combatant == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 1U);

  // Pointer focus enters through this same production focus-control path.
  CHECK(harness.focus(guard.focus_identifier));
  CHECK(harness.focus(center.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.keyboard().pressed_identifier() == center.focus_identifier);
  auto changed = harness.controls();
  const auto changed_center = std::ranges::find_if(
      changed, [](const auto& control) {
        return control.kind == ShellControlKind::center_active_combatant;
      });
  CHECK(changed_center != changed.end());
  changed_center->payload = CenterActiveCombatantAction{3};
  CHECK(harness.recompose(std::move(changed)));
  CHECK(!harness.keyboard().focused_identifier());
  CHECK(!harness.keyboard().pressed_identifier());
  const auto stale_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_release.shell.consumed);
  CHECK(!stale_release.shell.invoked_control);
  CHECK(!stale_release.dispatch);
  CHECK(bridge.actions().size() == 1U);

  CHECK(!harness.recompose({center, delay, finish, guard}));
  CHECK(harness.focus(center.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = harness.controls();
  const auto disabled_center = std::ranges::find_if(
      changed, [](const auto& control) {
        return control.kind == ShellControlKind::center_active_combatant;
      });
  CHECK(disabled_center != changed.end());
  disabled_center->enabled = false;
  CHECK(harness.recompose(std::move(changed)));
  const auto disabled_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(disabled_release.shell.consumed);
  CHECK(!disabled_release.shell.invoked_control);
  CHECK(!disabled_release.dispatch);
  CHECK(bridge.actions().size() == 1U);
}

void test_secondary_combat_actions_and_recomposition_are_fail_closed() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  const ShellControlPlacement guard{
      .region = ShellRegionId{1104},
      .kind = ShellControlKind::guard_combatant,
      .bounds = {20.0, 72.0, 110.0, 48.0},
      .label = "GUARD",
      .accessibility_label = "Guard active combatant",
      .focus_identifier = "focus.action.combat.guard",
      .tab_order = 1104,
      .enabled = true,
      .payload = GuardCombatantAction{2},
  };
  const ShellControlPlacement finish{
      .region = ShellRegionId{1105},
      .kind = ShellControlKind::finish_combatant,
      .bounds = {138.0, 72.0, 110.0, 48.0},
      .label = "FINISH",
      .accessibility_label = "Finish active combatant's turn",
      .focus_identifier = "focus.action.combat.finish",
      .tab_order = 1105,
      .enabled = true,
      .payload = FinishCombatantAction{2},
  };
  const ShellControlPlacement delay{
      .region = ShellRegionId{1106},
      .kind = ShellControlKind::delay_combatant,
      .bounds = {256.0, 72.0, 110.0, 48.0},
      .label = "DELAY",
      .accessibility_label = "Delay active combatant's turn",
      .focus_identifier = "focus.action.combat.delay",
      .tab_order = 1106,
      .enabled = true,
      .payload = DelayCombatantAction{2},
  };
  const ShellControlPlacement center{
      .region = ShellRegionId{1107},
      .kind = ShellControlKind::center_active_combatant,
      .bounds = {374.0, 72.0, 110.0, 48.0},
      .label = "CENTER",
      .accessibility_label = "Center view on active combatant",
      .focus_identifier = "focus.action.combat.center",
      .tab_order = 1107,
      .enabled = true,
      .payload = CenterActiveCombatantAction{2},
  };
  const ShellControlPlacement more{
      .region = ShellRegionId{1108},
      .kind = ShellControlKind::combat_action_page,
      .bounds = {500.0, 10.0, 44.0, 44.0},
      .label = "MORE",
      .accessibility_label = "Open more combat actions",
      .focus_identifier = "focus.action.combat.more",
      .tab_order = 1108,
      .enabled = true,
      .payload = SetCombatActionPageAction{CombatActionPage::secondary},
  };
  const ShellControlPlacement back{
      .region = ShellRegionId{1108},
      .kind = ShellControlKind::combat_action_page,
      .bounds = {500.0, 10.0, 44.0, 44.0},
      .label = "BACK",
      .accessibility_label = "Return to primary combat actions",
      .focus_identifier = "focus.action.combat.more",
      .tab_order = 1108,
      .enabled = true,
      .payload = SetCombatActionPageAction{CombatActionPage::primary},
  };
  const ShellControlPlacement weapon{
      .region = ShellRegionId{1109},
      .kind = ShellControlKind::switch_weapon_set,
      .bounds = {20.0, 72.0, 140.0, 48.0},
      .label = "WEAPON",
      .accessibility_label = "Switch active combatant's weapon set",
      .focus_identifier = "focus.action.combat.weapon",
      .tab_order = 1109,
      .enabled = true,
      .payload = SwitchWeaponSetAction{2},
  };
  const ShellControlPlacement previous{
      .region = ShellRegionId{1110},
      .kind = ShellControlKind::cycle_combat_focus,
      .bounds = {168.0, 72.0, 140.0, 48.0},
      .label = "PREV",
      .accessibility_label = "Center view on previous combatant",
      .focus_identifier = "focus.action.combat.center.previous",
      .tab_order = 1110,
      .enabled = true,
      .payload = CycleCombatFocusAction{2, CombatFocusDirection::previous},
  };
  const ShellControlPlacement next{
      .region = ShellRegionId{1111},
      .kind = ShellControlKind::cycle_combat_focus,
      .bounds = {316.0, 72.0, 140.0, 48.0},
      .label = "NEXT",
      .accessibility_label = "Center view on next combatant",
      .focus_identifier = "focus.action.combat.center.next",
      .tab_order = 1111,
      .enabled = true,
      .payload = CycleCombatFocusAction{2, CombatFocusDirection::next},
  };
  const ShellControlPlacement items{
      .region = ShellRegionId{1112},
      .kind = ShellControlKind::open_combat_items,
      .bounds = {464.0, 72.0, 140.0, 48.0},
      .label = "ITEMS",
      .accessibility_label = "Open combat items",
      .focus_identifier = "focus.action.combat.items",
      .tab_order = 1112,
      .enabled = true,
      .payload = OpenCombatItemsAction{2, 4},
  };
  const ShellControlPlacement utility_more{
      .region = ShellRegionId{1113},
      .kind = ShellControlKind::combat_action_page,
      .bounds = {448.0, 10.0, 44.0, 44.0},
      .label = "MORE",
      .accessibility_label = "Open utility combat actions",
      .focus_identifier = "focus.action.combat.utility",
      .tab_order = 1113,
      .enabled = true,
      .payload = SetCombatActionPageAction{CombatActionPage::utility},
  };
  const ShellControlPlacement utility_back{
      .region = ShellRegionId{1108},
      .kind = ShellControlKind::combat_action_page,
      .bounds = {500.0, 10.0, 44.0, 44.0},
      .label = "BACK",
      .accessibility_label = "Return to more combat actions",
      .focus_identifier = "focus.action.combat.more",
      .tab_order = 1108,
      .enabled = true,
      .payload = SetCombatActionPageAction{CombatActionPage::secondary},
  };
  const ShellControlPlacement auto_combatant{
      .region = ShellRegionId{1114},
      .kind = ShellControlKind::auto_combatant,
      .bounds = {20.0, 72.0, 160.0, 48.0},
      .label = "AUTO",
      .accessibility_label = "Auto-play active combatant's turn",
      .focus_identifier = "focus.action.combat.auto",
      .tab_order = 1114,
      .enabled = true,
      .payload = AutoCombatantAction{2},
  };
  const ShellControlPlacement combat_range{
      .region = ShellRegionId{1115},
      .kind = ShellControlKind::show_combat_range,
      .bounds = {188.0, 72.0, 160.0, 48.0},
      .label = "RANGE",
      .accessibility_label = "Show combat ranges; press any key to close",
      .focus_identifier = "focus.action.combat.range",
      .tab_order = 1115,
      .enabled = true,
      .payload = ShowCombatRangeAction{2},
  };
  const ShellControlPlacement bandage{
      .region = ShellRegionId{1116},
      .kind = ShellControlKind::bandage_combatant,
      .bounds = {356.0, 72.0, 160.0, 48.0},
      .label = "BANDAGE",
      .accessibility_label = "Choose a party member to bandage",
      .focus_identifier = "focus.action.combat.bandage",
      .tab_order = 1116,
      .enabled = true,
      .payload = BandageCombatantAction{2},
  };
  const ShellControlPlacement undo{
      .region = ShellRegionId{1117},
      .kind = ShellControlKind::undo_combatant,
      .bounds = {524.0, 72.0, 160.0, 48.0},
      .label = "UNDO",
      .accessibility_label = "Undo active combatant's movement",
      .focus_identifier = "focus.action.combat.undo",
      .tab_order = 1117,
      .enabled = true,
      .payload = UndoCombatantAction{2},
  };
  const ShellControlPlacement special_more{
      .region = ShellRegionId{1118},
      .kind = ShellControlKind::combat_action_page,
      .bounds = {448.0, 10.0, 44.0, 44.0},
      .label = "MORE",
      .accessibility_label = "Open special combat actions",
      .focus_identifier = "focus.action.combat.special",
      .tab_order = 1118,
      .enabled = true,
      .payload = SetCombatActionPageAction{CombatActionPage::special},
  };
  const ShellControlPlacement special_back{
      .region = ShellRegionId{1118},
      .kind = ShellControlKind::combat_action_page,
      .bounds = {500.0, 10.0, 44.0, 44.0},
      .label = "BACK",
      .accessibility_label = "Return to utility combat actions",
      .focus_identifier = "focus.action.combat.special",
      .tab_order = 1118,
      .enabled = true,
      .payload = SetCombatActionPageAction{CombatActionPage::utility},
  };
  const ShellControlPlacement cast{
      .region = ShellRegionId{1119},
      .kind = ShellControlKind::open_combat_spellbook,
      .bounds = {20.0, 72.0, 160.0, 48.0},
      .label = "CAST",
      .accessibility_label = "Open combat spell chooser",
      .focus_identifier = "focus.action.combat.spellbook.open",
      .tab_order = 1119,
      .enabled = true,
      .payload = OpenCombatSpellbookAction{2},
  };
  const ShellControlPlacement target{
      .region = ShellRegionId{1120},
      .kind = ShellControlKind::open_combat_targeting,
      .bounds = {188.0, 72.0, 160.0, 48.0},
      .label = "TARGET",
      .accessibility_label = "Begin combat targeting",
      .focus_identifier = "focus.action.combat.targeting.open",
      .tab_order = 1120,
      .enabled = true,
      .payload = OpenCombatTargetingAction{2},
  };
  const std::vector primary{guard, finish, delay, center, more};
  const std::vector secondary{
      back, weapon, previous, next, items, utility_more};
  const std::vector utility{
      utility_back, auto_combatant, combat_range, bandage, undo, special_more};
  const std::vector special{special_back, cast, target};

  // Insertion order cannot disturb the primary combat traversal order.
  CHECK(!harness.recompose({more, center, delay, finish, guard}));
  for (const auto& expected : primary) {
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
    CHECK(harness.keyboard().focused_identifier() ==
        expected.focus_identifier);
    release_tab(harness);
  }

  // Page commands are consumed locally by WindowManager and never dispatched
  // through the legacy bridge.
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  const auto open = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken), false);
  CHECK(open.shell.consumed);
  CHECK(open.shell.invoked_control.has_value());
  CHECK(!open.dispatch);
  CHECK(std::get<SetCombatActionPageAction>(
      open.shell.invoked_control->payload).page ==
      CombatActionPage::secondary);
  CHECK(is_valid_combat_action_page_transition(
      CombatActionPage::primary,
      std::get<SetCombatActionPageAction>(
          open.shell.invoked_control->payload).page));
  CHECK(bridge.actions().empty());
  CHECK(harness.recompose(
      {utility_more, items, next, previous, weapon, back}));
  CHECK(!harness.keyboard().focused_identifier());

  // BACK, WEAPON, PREV, NEXT, ITEMS, MORE is stable even under reversed
  // insertion.
  for (const auto& expected : secondary) {
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
    CHECK(harness.keyboard().focused_identifier() ==
        expected.focus_identifier);
    release_tab(harness);
  }

  CHECK(harness.focus(items.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto repeated = harness.handle(key_down(
        ShellKeyboardKey::space, kSpaceToken, false, true));
    CHECK(repeated.shell.consumed);
    CHECK(!repeated.shell.invoked_control);
    CHECK(!repeated.dispatch);
  }
  const auto items_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(items_release.shell.consumed);
  CHECK(items_release.shell.invoked_control.has_value());
  CHECK(items_release.shell.invoked_control->kind ==
      ShellControlKind::open_combat_items);
  CHECK(items_release.dispatch.has_value());
  CHECK(items_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 1U);
  const auto& items_action = std::get<OpenCombatItemsAction>(
      bridge.actions()[0].payload);
  CHECK(items_action.combatant == 2);
  CHECK(items_action.member == 4);
  CHECK(items_action.combatant != items_action.member);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);

  CHECK(harness.focus(next.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  const auto next_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(next_release.shell.consumed);
  CHECK(next_release.shell.invoked_control.has_value());
  CHECK(next_release.shell.invoked_control->kind ==
      ShellControlKind::cycle_combat_focus);
  CHECK(next_release.dispatch.has_value());
  CHECK(next_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 2U);
  const auto& next_action = std::get<CycleCombatFocusAction>(
      bridge.actions()[1].payload);
  CHECK(next_action.combatant == 2);
  CHECK(next_action.direction == CombatFocusDirection::next);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);

  CHECK(harness.focus(previous.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  const auto previous_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(previous_release.shell.consumed);
  CHECK(previous_release.shell.invoked_control.has_value());
  CHECK(previous_release.dispatch.has_value());
  CHECK(previous_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 3U);
  const auto& previous_action = std::get<CycleCombatFocusAction>(
      bridge.actions()[2].payload);
  CHECK(previous_action.combatant == 2);
  CHECK(previous_action.direction == CombatFocusDirection::previous);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);

  CHECK(harness.focus(weapon.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto repeated = harness.handle(key_down(
        ShellKeyboardKey::space, kSpaceToken, false, true));
    CHECK(repeated.shell.consumed);
    CHECK(!repeated.shell.invoked_control);
    CHECK(!repeated.dispatch);
  }
  const auto weapon_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(weapon_release.shell.consumed);
  CHECK(weapon_release.shell.invoked_control.has_value());
  CHECK(weapon_release.shell.invoked_control->kind ==
      ShellControlKind::switch_weapon_set);
  CHECK(weapon_release.dispatch.has_value());
  CHECK(weapon_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 4U);
  CHECK(std::get<SwitchWeaponSetAction>(
      bridge.actions()[3].payload).combatant == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 4U);

  CHECK(harness.focus(back.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  const auto close = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken), false);
  CHECK(close.shell.consumed);
  CHECK(close.shell.invoked_control.has_value());
  CHECK(!close.dispatch);
  CHECK(std::get<SetCombatActionPageAction>(
      close.shell.invoked_control->payload).page ==
      CombatActionPage::primary);
  CHECK(is_valid_combat_action_page_transition(
      CombatActionPage::secondary,
      std::get<SetCombatActionPageAction>(
          close.shell.invoked_control->payload).page));
  CHECK(bridge.actions().size() == 4U);
  CHECK(harness.recompose(primary));
  CHECK(!harness.keyboard().focused_identifier());

  // A held relative-focus activation cannot silently retarget after the
  // acting combatant changes.
  CHECK(!harness.recompose(secondary));
  CHECK(harness.focus(previous.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  auto changed = secondary;
  changed[2].payload = CycleCombatFocusAction{
      3, CombatFocusDirection::previous};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_actor_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_actor_release.shell.consumed);
  CHECK(!stale_actor_release.shell.invoked_control);
  CHECK(!stale_actor_release.dispatch);
  CHECK(bridge.actions().size() == 4U);

  // The combat-items payload owns both identities; changing either one while
  // the key is held cancels rather than opening a different inventory.
  CHECK(!harness.recompose(secondary));
  CHECK(harness.focus(items.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = secondary;
  changed[4].payload = OpenCombatItemsAction{3, 4};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_items_actor_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_items_actor_release.shell.consumed);
  CHECK(!stale_items_actor_release.shell.invoked_control);
  CHECK(!stale_items_actor_release.dispatch);
  CHECK(bridge.actions().size() == 4U);

  CHECK(!harness.recompose(secondary));
  CHECK(harness.focus(items.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = secondary;
  changed[4].payload = OpenCombatItemsAction{2, 5};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_items_member_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_items_member_release.shell.consumed);
  CHECK(!stale_items_member_release.shell.invoked_control);
  CHECK(!stale_items_member_release.dispatch);
  CHECK(bridge.actions().size() == 4U);

  // Disabling the live descriptor and changing pages both cancel a held key
  // while retaining ownership of its eventual release.
  CHECK(!harness.recompose(secondary));
  CHECK(harness.focus(items.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = secondary;
  changed[4].enabled = false;
  CHECK(harness.recompose(std::move(changed)));
  const auto disabled_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(disabled_release.shell.consumed);
  CHECK(!disabled_release.shell.invoked_control);
  CHECK(!disabled_release.dispatch);
  CHECK(bridge.actions().size() == 4U);

  CHECK(!harness.recompose(secondary));
  CHECK(harness.focus(items.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.recompose(primary));
  const auto changed_page_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(changed_page_release.shell.consumed);
  CHECK(!changed_page_release.shell.invoked_control);
  CHECK(!changed_page_release.dispatch);
  CHECK(bridge.actions().size() == 4U);

  CHECK(!harness.recompose(secondary));
  CHECK(harness.focus(items.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.set_route_enabled(false));
  CHECK(!harness.set_route_enabled(true));
  const auto changed_route_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(changed_route_release.shell.consumed);
  CHECK(!changed_route_release.shell.invoked_control);
  CHECK(!changed_route_release.dispatch);
  CHECK(bridge.actions().size() == 4U);

  // Secondary MORE advances exactly one valid page and remains local.
  CHECK(!harness.recompose(secondary));
  CHECK(harness.focus(utility_more.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  const auto open_utility = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken), false);
  CHECK(open_utility.shell.consumed);
  CHECK(open_utility.shell.invoked_control.has_value());
  CHECK(!open_utility.dispatch);
  const auto utility_page = std::get<SetCombatActionPageAction>(
      open_utility.shell.invoked_control->payload).page;
  CHECK(utility_page == CombatActionPage::utility);
  CHECK(is_valid_combat_action_page_transition(
      CombatActionPage::secondary, utility_page));
  CHECK(bridge.actions().size() == 4U);
  CHECK(harness.recompose(
      {special_more, undo, bandage, combat_range, auto_combatant,
          utility_back}));
  CHECK(!harness.keyboard().focused_identifier());

  // Utility traversal is BACK, AUTO, RANGE, BANDAGE, UNDO, MORE regardless of
  // insertion order.
  for (const auto& expected : utility) {
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
    CHECK(harness.keyboard().focused_identifier() ==
        expected.focus_identifier);
    release_tab(harness);
  }

  CHECK(harness.focus(auto_combatant.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto repeated = harness.handle(key_down(
        ShellKeyboardKey::space, kSpaceToken, false, true));
    CHECK(repeated.shell.consumed);
    CHECK(!repeated.shell.invoked_control);
    CHECK(!repeated.dispatch);
  }
  const auto auto_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(auto_release.shell.consumed);
  CHECK(auto_release.shell.invoked_control.has_value());
  CHECK(auto_release.shell.invoked_control->kind ==
      ShellControlKind::auto_combatant);
  CHECK(auto_release.dispatch.has_value());
  CHECK(auto_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 5U);
  CHECK(std::get<AutoCombatantAction>(
      bridge.actions()[4].payload).combatant == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 5U);

  // Utility BACK returns exactly to secondary and never crosses the bridge.
  CHECK(harness.focus(utility_back.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  const auto close_utility = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken), false);
  CHECK(close_utility.shell.consumed);
  CHECK(close_utility.shell.invoked_control.has_value());
  CHECK(!close_utility.dispatch);
  const auto secondary_page = std::get<SetCombatActionPageAction>(
      close_utility.shell.invoked_control->payload).page;
  CHECK(secondary_page == CombatActionPage::secondary);
  CHECK(is_valid_combat_action_page_transition(
      CombatActionPage::utility, secondary_page));
  CHECK(bridge.actions().size() == 5U);
  CHECK(harness.recompose(secondary));
  CHECK(!harness.keyboard().focused_identifier());

  // Actor, enabled state, page, and route recomposition all cancel a held
  // Auto activation while retaining ownership of the physical release.
  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(auto_combatant.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = utility;
  changed[1].payload = AutoCombatantAction{3};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_auto_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_auto_release.shell.consumed);
  CHECK(!stale_auto_release.shell.invoked_control);
  CHECK(!stale_auto_release.dispatch);
  CHECK(bridge.actions().size() == 5U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(auto_combatant.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = utility;
  changed[1].enabled = false;
  CHECK(harness.recompose(std::move(changed)));
  const auto disabled_auto_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(disabled_auto_release.shell.consumed);
  CHECK(!disabled_auto_release.shell.invoked_control);
  CHECK(!disabled_auto_release.dispatch);
  CHECK(bridge.actions().size() == 5U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(auto_combatant.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.recompose(secondary));
  const auto auto_page_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(auto_page_release.shell.consumed);
  CHECK(!auto_page_release.shell.invoked_control);
  CHECK(!auto_page_release.dispatch);
  CHECK(bridge.actions().size() == 5U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(auto_combatant.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.set_route_enabled(false));
  CHECK(!harness.set_route_enabled(true));
  const auto auto_route_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(auto_route_release.shell.consumed);
  CHECK(!auto_route_release.shell.invoked_control);
  CHECK(!auto_route_release.dispatch);
  CHECK(bridge.actions().size() == 5U);

  // Range dispatches once with its stable actor identity.
  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(combat_range.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto repeated = harness.handle(key_down(
        ShellKeyboardKey::space, kSpaceToken, false, true));
    CHECK(repeated.shell.consumed);
    CHECK(!repeated.shell.invoked_control);
    CHECK(!repeated.dispatch);
  }
  const auto range_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(range_release.shell.consumed);
  CHECK(range_release.shell.invoked_control.has_value());
  CHECK(range_release.shell.invoked_control->kind ==
      ShellControlKind::show_combat_range);
  CHECK(range_release.dispatch.has_value());
  CHECK(range_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 6U);
  CHECK(std::get<ShowCombatRangeAction>(
      bridge.actions()[5].payload).combatant == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 6U);

  // Actor, enabled state, page, and route changes cancel a held Range
  // activation while the physical release remains owned.
  CHECK(!harness.recompose(utility));
  CHECK(harness.keyboard().focused_identifier() ==
      combat_range.focus_identifier);
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = utility;
  changed[2].payload = ShowCombatRangeAction{3};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_range_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_range_release.shell.consumed);
  CHECK(!stale_range_release.shell.invoked_control);
  CHECK(!stale_range_release.dispatch);
  CHECK(bridge.actions().size() == 6U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(combat_range.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = utility;
  changed[2].enabled = false;
  CHECK(harness.recompose(std::move(changed)));
  const auto disabled_range_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(disabled_range_release.shell.consumed);
  CHECK(!disabled_range_release.shell.invoked_control);
  CHECK(!disabled_range_release.dispatch);
  CHECK(bridge.actions().size() == 6U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(combat_range.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.recompose(secondary));
  const auto range_page_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(range_page_release.shell.consumed);
  CHECK(!range_page_release.shell.invoked_control);
  CHECK(!range_page_release.dispatch);
  CHECK(bridge.actions().size() == 6U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(combat_range.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.set_route_enabled(false));
  CHECK(!harness.set_route_enabled(true));
  const auto range_route_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(range_route_release.shell.consumed);
  CHECK(!range_route_release.shell.invoked_control);
  CHECK(!range_route_release.dispatch);
  CHECK(bridge.actions().size() == 6U);

  // Bandage dispatches once with its stable acting-combatant identity. Target
  // choice remains inside the preserved Classic flow.
  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(bandage.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto repeated = harness.handle(key_down(
        ShellKeyboardKey::space, kSpaceToken, false, true));
    CHECK(repeated.shell.consumed);
    CHECK(!repeated.shell.invoked_control);
    CHECK(!repeated.dispatch);
  }
  const auto bandage_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(bandage_release.shell.consumed);
  CHECK(bandage_release.shell.invoked_control.has_value());
  CHECK(bandage_release.shell.invoked_control->kind ==
      ShellControlKind::bandage_combatant);
  CHECK(bandage_release.dispatch.has_value());
  CHECK(bandage_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 7U);
  CHECK(std::get<BandageCombatantAction>(
      bridge.actions()[6].payload).combatant == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 7U);

  // Actor, enabled state, page, and route changes cancel a held Bandage
  // activation while the physical release remains owned.
  CHECK(!harness.recompose(utility));
  CHECK(harness.keyboard().focused_identifier() == bandage.focus_identifier);
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = utility;
  changed[3].payload = BandageCombatantAction{3};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_bandage_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_bandage_release.shell.consumed);
  CHECK(!stale_bandage_release.shell.invoked_control);
  CHECK(!stale_bandage_release.dispatch);
  CHECK(bridge.actions().size() == 7U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(bandage.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = utility;
  changed[3].enabled = false;
  CHECK(harness.recompose(std::move(changed)));
  const auto disabled_bandage_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(disabled_bandage_release.shell.consumed);
  CHECK(!disabled_bandage_release.shell.invoked_control);
  CHECK(!disabled_bandage_release.dispatch);
  CHECK(bridge.actions().size() == 7U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(bandage.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.recompose(secondary));
  const auto bandage_page_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(bandage_page_release.shell.consumed);
  CHECK(!bandage_page_release.shell.invoked_control);
  CHECK(!bandage_page_release.dispatch);
  CHECK(bridge.actions().size() == 7U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(bandage.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.set_route_enabled(false));
  CHECK(!harness.set_route_enabled(true));
  const auto bandage_route_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(bandage_route_release.shell.consumed);
  CHECK(!bandage_route_release.shell.invoked_control);
  CHECK(!bandage_route_release.dispatch);
  CHECK(bridge.actions().size() == 7U);

  // Undo dispatches once with its stable actor. Classic retains all rollback
  // and turn-state ownership after this semantic handoff.
  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(undo.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto repeated = harness.handle(key_down(
        ShellKeyboardKey::space, kSpaceToken, false, true));
    CHECK(repeated.shell.consumed);
    CHECK(!repeated.shell.invoked_control);
    CHECK(!repeated.dispatch);
  }
  const auto undo_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(undo_release.shell.consumed);
  CHECK(undo_release.shell.invoked_control.has_value());
  CHECK(undo_release.shell.invoked_control->kind ==
      ShellControlKind::undo_combatant);
  CHECK(undo_release.dispatch.has_value());
  CHECK(undo_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 8U);
  CHECK(std::get<UndoCombatantAction>(
      bridge.actions()[7].payload).combatant == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 8U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.keyboard().focused_identifier() == undo.focus_identifier);
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = utility;
  changed[4].payload = UndoCombatantAction{3};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_undo_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_undo_release.shell.consumed);
  CHECK(!stale_undo_release.shell.invoked_control);
  CHECK(!stale_undo_release.dispatch);
  CHECK(bridge.actions().size() == 8U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(undo.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = utility;
  changed[4].enabled = false;
  CHECK(harness.recompose(std::move(changed)));
  const auto disabled_undo_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(disabled_undo_release.shell.consumed);
  CHECK(!disabled_undo_release.shell.invoked_control);
  CHECK(!disabled_undo_release.dispatch);
  CHECK(bridge.actions().size() == 8U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(undo.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.recompose(secondary));
  const auto undo_page_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(undo_page_release.shell.consumed);
  CHECK(!undo_page_release.shell.invoked_control);
  CHECK(!undo_page_release.dispatch);
  CHECK(bridge.actions().size() == 8U);

  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(undo.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.set_route_enabled(false));
  CHECK(!harness.set_route_enabled(true));
  const auto undo_route_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(undo_route_release.shell.consumed);
  CHECK(!undo_route_release.shell.invoked_control);
  CHECK(!undo_route_release.dispatch);
  CHECK(bridge.actions().size() == 8U);

  // Utility MORE advances exactly one page; the special BACK returns exactly
  // one page. Both are presentation-local.
  CHECK(!harness.recompose(utility));
  CHECK(harness.focus(special_more.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  const auto open_special = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken), false);
  CHECK(open_special.shell.consumed);
  CHECK(open_special.shell.invoked_control.has_value());
  CHECK(!open_special.dispatch);
  CHECK(std::get<SetCombatActionPageAction>(
      open_special.shell.invoked_control->payload).page ==
      CombatActionPage::special);
  CHECK(is_valid_combat_action_page_transition(
      CombatActionPage::utility, CombatActionPage::special));
  CHECK(bridge.actions().size() == 8U);

  CHECK(harness.recompose({target, cast, special_back}));
  for (const auto& expected : special) {
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
    CHECK(harness.keyboard().focused_identifier() ==
        expected.focus_identifier);
    release_tab(harness);
  }
  CHECK(harness.focus(special_back.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  const auto close_special = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken), false);
  CHECK(close_special.shell.consumed);
  CHECK(close_special.shell.invoked_control.has_value());
  CHECK(!close_special.dispatch);
  CHECK(std::get<SetCombatActionPageAction>(
      close_special.shell.invoked_control->payload).page ==
      CombatActionPage::utility);
  CHECK(is_valid_combat_action_page_transition(
      CombatActionPage::special, CombatActionPage::utility));

  // CAST dispatches once with the stable actor-only payload; Classic owns the
  // spell chooser, targeting, spell mutations, and turn effects.
  CHECK(!harness.recompose(special));
  CHECK(harness.focus(cast.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto repeated = harness.handle(key_down(
        ShellKeyboardKey::space, kSpaceToken, false, true));
    CHECK(repeated.shell.consumed);
    CHECK(!repeated.shell.invoked_control);
    CHECK(!repeated.dispatch);
  }
  const auto cast_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(cast_release.shell.consumed);
  CHECK(cast_release.shell.invoked_control.has_value());
  CHECK(cast_release.shell.invoked_control->kind ==
      ShellControlKind::open_combat_spellbook);
  CHECK(cast_release.dispatch.has_value());
  CHECK(cast_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 9U);
  CHECK(std::get<OpenCombatSpellbookAction>(
      bridge.actions()[8].payload).combatant == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 9U);

  CHECK(!harness.recompose(special));
  CHECK(harness.keyboard().focused_identifier() == cast.focus_identifier);
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = special;
  changed[1].payload = OpenCombatSpellbookAction{3};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_cast_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_cast_release.shell.consumed);
  CHECK(!stale_cast_release.shell.invoked_control);
  CHECK(!stale_cast_release.dispatch);
  CHECK(bridge.actions().size() == 9U);

  // TARGET is a distinct actor-only dispatch. Classic still owns quiver
  // selection, charge consumption, RNG, target choice, and combat mutation.
  CHECK(!harness.recompose(special));
  CHECK(harness.focus(target.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto repeated = harness.handle(key_down(
        ShellKeyboardKey::space, kSpaceToken, false, true));
    CHECK(repeated.shell.consumed);
    CHECK(!repeated.shell.invoked_control);
    CHECK(!repeated.dispatch);
  }
  const auto target_release = harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken));
  CHECK(target_release.shell.consumed);
  CHECK(target_release.shell.invoked_control.has_value());
  CHECK(target_release.shell.invoked_control->kind ==
      ShellControlKind::open_combat_targeting);
  CHECK(target_release.dispatch.has_value());
  CHECK(target_release.dispatch->status == DispatchStatus::handled);
  CHECK(bridge.actions().size() == 10U);
  CHECK(std::get<OpenCombatTargetingAction>(
      bridge.actions()[9].payload).combatant == 2);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
  CHECK(bridge.actions().size() == 10U);

  CHECK(!harness.recompose(special));
  CHECK(harness.keyboard().focused_identifier() == target.focus_identifier);
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = special;
  changed[2].payload = OpenCombatTargetingAction{3};
  CHECK(harness.recompose(std::move(changed)));
  const auto stale_target_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(stale_target_release.shell.consumed);
  CHECK(!stale_target_release.shell.invoked_control);
  CHECK(!stale_target_release.dispatch);
  CHECK(bridge.actions().size() == 10U);

  CHECK(!harness.recompose(special));
  CHECK(harness.focus(target.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  changed = special;
  changed[2].enabled = false;
  CHECK(harness.recompose(std::move(changed)));
  const auto disabled_target_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(disabled_target_release.shell.consumed);
  CHECK(!disabled_target_release.shell.invoked_control);
  CHECK(!disabled_target_release.dispatch);
  CHECK(bridge.actions().size() == 10U);

  CHECK(!harness.recompose(special));
  CHECK(harness.focus(target.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.recompose(utility));
  const auto target_page_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(target_page_release.shell.consumed);
  CHECK(!target_page_release.shell.invoked_control);
  CHECK(!target_page_release.dispatch);
  CHECK(bridge.actions().size() == 10U);

  CHECK(!harness.recompose(special));
  CHECK(harness.focus(target.focus_identifier));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.set_route_enabled(false));
  CHECK(!harness.set_route_enabled(true));
  const auto target_route_release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(target_route_release.shell.consumed);
  CHECK(!target_route_release.shell.invoked_control);
  CHECK(!target_route_release.dispatch);
  CHECK(bridge.actions().size() == 10U);
}

using DescriptorMutation =
    std::function<void(std::vector<ShellControlPlacement>&)>;

void verify_descriptor_change_cancels(const DescriptorMutation& mutate) {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  CHECK(harness.focus("move.north"));
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
  CHECK(harness.keyboard().pressed_identifier() == "move.north");

  const auto original = harness.controls();
  auto changed = original;
  mutate(changed);
  CHECK(harness.recompose(std::move(changed)));
  CHECK(!harness.keyboard().focused_identifier());
  CHECK(!harness.keyboard().pressed_identifier());
  CHECK(harness.keyboard().owns_token(kEnterToken));

  // Cancellation is sticky. Restoring and refocusing the original descriptor
  // before release must not resurrect the held activation.
  (void)harness.recompose(original);
  CHECK(harness.focus("move.north"));
  const auto release = harness.handle(
      key_up(ShellKeyboardKey::enter, kEnterToken));
  CHECK(release.shell.consumed);
  CHECK(!release.shell.invoked_control);
  CHECK(!release.dispatch);
  CHECK(bridge.actions().empty());
  CHECK(!harness.keyboard().owns_token(kEnterToken));
}

void test_descriptor_identity_is_strict_and_fail_closed() {
  verify_descriptor_change_cancels([](auto& controls) {
    const auto north = std::ranges::find_if(controls, [](const auto& control) {
      return control.focus_identifier == "move.north";
    });
    north->payload = MovePartyAction{MovementCommand::south};
  });
  verify_descriptor_change_cancels([](auto& controls) {
    const auto north = std::ranges::find_if(controls, [](const auto& control) {
      return control.focus_identifier == "move.north";
    });
    north->region = ShellRegionId{999};
  });
  verify_descriptor_change_cancels([](auto& controls) {
    const auto north = std::ranges::find_if(controls, [](const auto& control) {
      return control.focus_identifier == "move.north";
    });
    north->kind = static_cast<ShellControlKind>(99);
  });
  verify_descriptor_change_cancels([](auto& controls) {
    const auto north = std::ranges::find_if(controls, [](const auto& control) {
      return control.focus_identifier == "move.north";
    });
    north->focus_identifier = "move.reused-identity";
  });
  verify_descriptor_change_cancels([](auto& controls) {
    const auto north = std::ranges::find_if(controls, [](const auto& control) {
      return control.focus_identifier == "move.north";
    });
    north->enabled = false;
  });
  verify_descriptor_change_cancels([](auto& controls) {
    std::erase_if(controls, [](const auto& control) {
      return control.focus_identifier == "move.north";
    });
  });
}

void test_focus_change_clear_and_route_transition_cancel_activation() {
  {
    RecordingBridge bridge;
    ProductionKeyboardHarness harness(bridge);
    CHECK(harness.focus("move.north"));
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
    CHECK(harness.focus("move.east"));
    const auto release = harness.handle(
        key_up(ShellKeyboardKey::enter, kEnterToken));
    CHECK(release.shell.consumed);
    CHECK(!release.shell.invoked_control);
    CHECK(bridge.actions().empty());
  }
  {
    RecordingBridge bridge;
    ProductionKeyboardHarness harness(bridge);
    CHECK(harness.focus("move.north"));
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::space, kSpaceToken)).shell.consumed);
    CHECK(harness.keyboard().clear_focus());
    const auto release = harness.handle(
        key_up(ShellKeyboardKey::space, kSpaceToken));
    CHECK(release.shell.consumed);
    CHECK(!release.shell.invoked_control);
    CHECK(bridge.actions().empty());
  }
  {
    RecordingBridge bridge;
    ProductionKeyboardHarness harness(bridge);
    CHECK(harness.focus("move.north"));
    CHECK(harness.handle(
        key_down(ShellKeyboardKey::enter, kEnterToken)).shell.consumed);
    CHECK(harness.set_route_enabled(false));
    CHECK(!harness.keyboard().focused_identifier());
    CHECK(!harness.keyboard().pressed_identifier());
    CHECK(harness.keyboard().owns_token(kEnterToken));
    CHECK(!harness.set_route_enabled(true));
    CHECK(harness.focus("move.north"));
    CHECK(harness.handle(key_down(
        ShellKeyboardKey::enter, kEnterToken, false, true)).shell.consumed);
    const auto release = harness.handle(
        key_up(ShellKeyboardKey::enter, kEnterToken));
    CHECK(release.shell.consumed);
    CHECK(!release.shell.invoked_control);
    CHECK(bridge.actions().empty());
  }
}

void test_tab_route_cancellation_retains_release_ownership() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  CHECK(harness.handle(
      key_down(ShellKeyboardKey::tab, kTabToken)).shell.consumed);
  CHECK(harness.keyboard().owns_token(kTabToken));
  CHECK(harness.set_route_enabled(false));
  CHECK(!harness.keyboard().focused_identifier());
  CHECK(!harness.set_route_enabled(true));
  const auto release = harness.handle(
      key_up(ShellKeyboardKey::tab, kTabToken));
  CHECK(release.shell.consumed);
  CHECK(!release.shell.invoked_control);
  CHECK(!harness.keyboard().owns_token(kTabToken));
  CHECK(bridge.actions().empty());
}

void test_duplicate_identity_and_unowned_events_fail_closed() {
  RecordingBridge bridge;
  ProductionKeyboardHarness harness(bridge);
  auto duplicates = harness.controls();
  duplicates.emplace_back(movement_control(
      99, "move.north", 15, MovementCommand::south));
  CHECK(!harness.recompose(std::move(duplicates)));
  CHECK(!harness.focus("move.north"));

  const auto tab = harness.handle(
      key_down(ShellKeyboardKey::tab, kTabToken));
  CHECK(tab.shell.consumed);
  CHECK(harness.keyboard().focused_identifier() == "move.east");
  release_tab(harness);

  CHECK(!harness.handle(key_down(
      ShellKeyboardKey::enter, kOtherToken, false, true)).shell.consumed);
  CHECK(!harness.handle(
      key_up(ShellKeyboardKey::enter, kOtherToken)).shell.consumed);
  CHECK(bridge.actions().empty());
}

} // namespace

int main() {
  try {
    test_forward_reverse_traversal_and_disabled_skipping();
    test_tab_repeat_and_release_ownership();
    test_focus_survives_nonsemantic_recomposition_by_identifier();
    test_enter_space_exactly_once_and_physical_release_pairing();
    test_first_activation_wins_across_simultaneous_physical_keys();
    test_open_inventory_payload_activates_exactly_once();
    test_open_spellbook_payload_activates_exactly_once();
    test_open_save_payload_activates_exactly_once();
    test_open_load_payload_activates_exactly_once();
    test_guard_payload_activates_exactly_once();
    test_finish_payload_orders_dispatches_and_cancels_stale_actor();
    test_delay_payload_orders_dispatches_and_cancels_recomposition();
    test_center_payload_orders_dispatches_and_cancels_recomposition();
    test_secondary_combat_actions_and_recomposition_are_fail_closed();
    test_descriptor_identity_is_strict_and_fail_closed();
    test_focus_change_clear_and_route_transition_cancel_activation();
    test_tab_route_cancellation_retains_release_ownership();
    test_duplicate_identity_and_unowned_events_fail_closed();
    std::cout << "ShellKeyboardInteractionContractTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ShellKeyboardInteractionContractTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
