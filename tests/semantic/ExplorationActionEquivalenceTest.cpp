#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "presentation/GameSnapshot.hpp"
#include "presentation/LegacyCommandBridge.hpp"
#include "presentation/UIAction.hpp"

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

// These are the legacy Mac virtual scan codes consumed by checkkeypad.c for
// outdoor movement. Keeping them in the test adapter makes this fixture an
// independent oracle; production input code is deliberately not duplicated.
struct ClassicOutdoorScanCode {
  std::uint8_t value = 0;

  bool operator==(const ClassicOutdoorScanCode&) const = default;
};

// threed.c accepts these numeric-key characters for first-person dungeon
// movement. They cover the four relative movement commands in UIAction.hpp.
struct ClassicDungeonKey {
  char value = '\0';

  bool operator==(const ClassicDungeonKey&) const = default;
};

// buttonchoice.c derives charselectnew from the clicked 50-pixel portrait row.
struct ClassicPortraitClick {
  std::uint8_t member = 0;

  bool operator==(const ClassicPortraitClick&) const = default;
};

struct ClassicOpenInventoryKey {
  char value = 'i';

  bool operator==(const ClassicOpenInventoryKey&) const = default;
};

struct ClassicOpenSpellbookKey {
  char value = 's';

  bool operator==(const ClassicOpenSpellbookKey&) const = default;
};

struct ClassicOpenSaveMenu {
  std::int16_t menu_id = 129;
  std::int16_t item_id = 3;

  bool operator==(const ClassicOpenSaveMenu&) const = default;
};

using ClassicExplorationInput = std::variant<
    ClassicOutdoorScanCode,
    ClassicDungeonKey,
    ClassicPortraitClick,
    ClassicOpenInventoryKey,
    ClassicOpenSpellbookKey,
    ClassicOpenSaveMenu>;

struct ReplayStep {
  std::string_view label;
  ClassicExplorationInput classic_input;
  UIActionPayload remastered_payload;
};

[[nodiscard]] std::optional<MovementCommand> movement_for_outdoor_scan_code(
    std::uint8_t scan_code) noexcept {
  switch (scan_code) {
    case 0x7E:
      return MovementCommand::north;
    case 0x5C:
      return MovementCommand::northeast;
    case 0x7C:
      return MovementCommand::east;
    case 0x55:
      return MovementCommand::southeast;
    case 0x7D:
      return MovementCommand::south;
    case 0x53:
      return MovementCommand::southwest;
    case 0x7B:
      return MovementCommand::west;
    case 0x59:
      return MovementCommand::northwest;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::optional<MovementCommand> movement_for_dungeon_key(
    char key) noexcept {
  switch (key) {
    case '8':
      return MovementCommand::step_forward;
    case '2':
      return MovementCommand::step_backward;
    case '4':
      return MovementCommand::turn_left;
    case '6':
      return MovementCommand::turn_right;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::optional<UIAction> adapt_classic_input(
    const ClassicExplorationInput& input,
    ActionSequence sequence,
    std::optional<PartyMemberId> selected_member) {
  return std::visit([sequence, selected_member](
                        const auto& concrete) -> std::optional<UIAction> {
    using Input = std::decay_t<decltype(concrete)>;
    if constexpr (std::is_same_v<Input, ClassicOutdoorScanCode>) {
      const auto movement = movement_for_outdoor_scan_code(concrete.value);
      if (!movement) {
        return std::nullopt;
      }
      return UIAction{
          .sequence = sequence,
          .payload = MovePartyAction{*movement},
      };
    } else if constexpr (std::is_same_v<Input, ClassicDungeonKey>) {
      const auto movement = movement_for_dungeon_key(concrete.value);
      if (!movement) {
        return std::nullopt;
      }
      return UIAction{
          .sequence = sequence,
          .payload = MovePartyAction{*movement},
      };
    } else if constexpr (std::is_same_v<Input, ClassicPortraitClick>) {
      return UIAction{
          .sequence = sequence,
          .payload = SelectPartyMemberAction{concrete.member},
      };
    } else if constexpr (std::is_same_v<Input, ClassicOpenInventoryKey>) {
      if ((concrete.value != 'i') || !selected_member) {
        return std::nullopt;
      }
      return UIAction{
          .sequence = sequence,
          .payload = OpenInventoryAction{*selected_member},
      };
    } else if constexpr (std::is_same_v<Input, ClassicOpenSpellbookKey>) {
      if ((concrete.value != 's') || !selected_member) {
        return std::nullopt;
      }
      return UIAction{
          .sequence = sequence,
          .payload = OpenSpellbookAction{*selected_member},
      };
    } else {
      if ((concrete.menu_id != 129) || (concrete.item_id != 3)) {
        return std::nullopt;
      }
      return UIAction{
          .sequence = sequence,
          .payload = OpenSaveGameAction{},
      };
    }
  }, input);
}

[[nodiscard]] Facing turn_left(Facing facing) noexcept {
  switch (facing) {
    case Facing::north:
      return Facing::west;
    case Facing::east:
      return Facing::north;
    case Facing::south:
      return Facing::east;
    case Facing::west:
      return Facing::south;
  }
  return Facing::north;
}

[[nodiscard]] Facing turn_right(Facing facing) noexcept {
  switch (facing) {
    case Facing::north:
      return Facing::east;
    case Facing::east:
      return Facing::south;
    case Facing::south:
      return Facing::west;
    case Facing::west:
      return Facing::north;
  }
  return Facing::north;
}

[[nodiscard]] std::pair<int, int> facing_delta(Facing facing) noexcept {
  switch (facing) {
    case Facing::north:
      return {0, -1};
    case Facing::east:
      return {1, 0};
    case Facing::south:
      return {0, 1};
    case Facing::west:
      return {-1, 0};
  }
  return {0, -1};
}

// This deterministic state model is intentionally not the live Realmz engine
// and these bytes are intentionally not a Realmz save-file implementation.
// It is a save-facing mutation oracle: both input routes must produce exactly
// the same stable byte image, while actual save structures remain untouched.
class ExplorationStateFixture final : public GameSnapshotSource {
public:
  using SaveFacingBytes = std::array<std::uint8_t, 40>;

  explicit ExplorationStateFixture(int32_t party_x = 40, int32_t party_y = 40) {
    snapshot_.screen = ScreenContext::exploration;
    snapshot_.scenario_id = 1;
    snapshot_.party.fatigue = 12;
    PartyMemberView arin;
    arin.id = 0;
    arin.name = "Arin";
    arin.spell_points = {8, 8};
    arin.selected = true;
    PartyMemberView bryn;
    bryn.id = 1;
    bryn.name = "Bryn";
    bryn.spell_points = {7, 7};
    PartyMemberView cerys;
    cerys.id = 2;
    cerys.name = "Cerys";
    cerys.spell_points = {6, 6};
    snapshot_.party.members = {
        std::move(arin),
        std::move(bryn),
        std::move(cerys),
    };
    snapshot_.party.selected_member = 0;
    snapshot_.world.presentation = WorldPresentation::outdoor;
    snapshot_.world.party_x = party_x;
    snapshot_.world.party_y = party_y;
    snapshot_.world.facing = Facing::north;
    snapshot_.world.visible_columns = 3;
    snapshot_.world.visible_rows = 3;
    snapshot_.world.visible_tiles.resize(9);
    refresh_save_facing_bytes();
  }

  [[nodiscard]] GameSnapshot capture() const override {
    return snapshot_;
  }

  [[nodiscard]] const SaveFacingBytes& save_facing_bytes() const noexcept {
    return save_facing_bytes_;
  }

  [[nodiscard]] DispatchResult move(const MovePartyAction& action) {
    if (action.command == MovementCommand::turn_left ||
        action.command == MovementCommand::turn_right) {
      snapshot_.world.facing =
          (action.command == MovementCommand::turn_left)
          ? turn_left(snapshot_.world.facing)
          : turn_right(snapshot_.world.facing);
      ++snapshot_.revision;
      refresh_save_facing_bytes();
      return DispatchResult::handled({GameEvent{
          .sequence = snapshot_.revision,
          .payload = AudioCueEvent{"party_turn", AudioBus::effect, false},
      }});
    }

    int delta_x = 0;
    int delta_y = 0;
    switch (action.command) {
      case MovementCommand::step_forward: {
        const auto delta = facing_delta(snapshot_.world.facing);
        delta_x = delta.first;
        delta_y = delta.second;
        break;
      }
      case MovementCommand::step_backward: {
        const auto delta = facing_delta(snapshot_.world.facing);
        delta_x = -delta.first;
        delta_y = -delta.second;
        break;
      }
      case MovementCommand::turn_left:
      case MovementCommand::turn_right:
        throw std::logic_error("turn command reached displacement path");
      case MovementCommand::north:
        delta_y = -1;
        break;
      case MovementCommand::northeast:
        delta_x = 1;
        delta_y = -1;
        break;
      case MovementCommand::east:
        delta_x = 1;
        break;
      case MovementCommand::southeast:
        delta_x = 1;
        delta_y = 1;
        break;
      case MovementCommand::south:
        delta_y = 1;
        break;
      case MovementCommand::southwest:
        delta_x = -1;
        delta_y = 1;
        break;
      case MovementCommand::west:
        delta_x = -1;
        break;
      case MovementCommand::northwest:
        delta_x = -1;
        delta_y = -1;
        break;
    }

    const int32_t destination_x = snapshot_.world.party_x + delta_x;
    const int32_t destination_y = snapshot_.world.party_y + delta_y;
    if (destination_x < 0 || destination_x > 89 ||
        destination_y < 0 || destination_y > 89) {
      return DispatchResult::rejected("movement would leave fixture map");
    }

    snapshot_.world.party_x = destination_x;
    snapshot_.world.party_y = destination_y;
    ++snapshot_.party.fatigue;
    ++snapshot_.revision;
    refresh_save_facing_bytes();
    return DispatchResult::handled({GameEvent{
        .sequence = snapshot_.revision,
        .payload = AudioCueEvent{"party_step", AudioBus::effect, false},
    }});
  }

  [[nodiscard]] DispatchResult select(const SelectPartyMemberAction& action) {
    if (action.member >= snapshot_.party.members.size()) {
      return DispatchResult::rejected(
          "party member is outside fixture bounds");
    }
    if (snapshot_.party.selected_member == action.member) {
      return DispatchResult::handled();
    }

    for (auto& member : snapshot_.party.members) {
      member.selected = member.id == action.member;
    }
    snapshot_.party.selected_member = action.member;
    ++snapshot_.revision;
    refresh_save_facing_bytes();
    return DispatchResult::handled({GameEvent{
        .sequence = snapshot_.revision,
        .payload = AudioCueEvent{
            "party_select", AudioBus::interface_sound, false},
    }});
  }

  [[nodiscard]] DispatchResult open_inventory(
      const OpenInventoryAction& action) const {
    const auto* member = snapshot_.party.member(action.member);
    if (!member || !member->selected ||
        snapshot_.party.selected_member != action.member) {
      return DispatchResult::rejected(
          "inventory target is not the selected party member");
    }
    return DispatchResult::handled({GameEvent{
        .sequence = snapshot_.revision,
        .payload = ScreenTransitionEvent{ScreenContext::inventory},
    }});
  }

  [[nodiscard]] DispatchResult open_spellbook(
      const OpenSpellbookAction& action) const {
    const auto* member = snapshot_.party.member(action.member);
    if (!member || !member->selected || !member->conscious ||
        (member->spell_points.current <= 0) ||
        snapshot_.party.selected_member != action.member) {
      return DispatchResult::rejected(
          "spellbook target is not an eligible selected caster");
    }
    return DispatchResult::handled({GameEvent{
        .sequence = snapshot_.revision,
        .payload = ModalRequestEvent{
            .request_id = snapshot_.revision,
            .title = "Cast spell",
            .body = member->name,
        },
    }});
  }

  [[nodiscard]] DispatchResult open_save_game(
      const OpenSaveGameAction&) const {
    return DispatchResult::handled({GameEvent{
        .sequence = snapshot_.revision,
        .payload = ModalRequestEvent{
            .request_id = snapshot_.revision,
            .title = "Save game",
            .body = "Choose a Classic save slot",
        },
    }});
  }

private:
  static void write_u16(
      SaveFacingBytes& bytes,
      std::size_t offset,
      std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  }

  static void write_u32(
      SaveFacingBytes& bytes,
      std::size_t offset,
      std::uint32_t value) noexcept {
    for (std::size_t byte = 0; byte < 4; ++byte) {
      bytes[offset + byte] = static_cast<std::uint8_t>(
          (value >> (byte * 8U)) & 0xFFU);
    }
  }

  static void write_u64(
      SaveFacingBytes& bytes,
      std::size_t offset,
      std::uint64_t value) noexcept {
    for (std::size_t byte = 0; byte < 8; ++byte) {
      bytes[offset + byte] = static_cast<std::uint8_t>(
          (value >> (byte * 8U)) & 0xFFU);
    }
  }

  void refresh_save_facing_bytes() noexcept {
    save_facing_bytes_.fill(0);
    save_facing_bytes_[0] = 'R';
    save_facing_bytes_[1] = 'Z';
    save_facing_bytes_[2] = 'E';
    save_facing_bytes_[3] = 'Q';
    write_u16(save_facing_bytes_, 4, 1);
    write_u64(save_facing_bytes_, 8, snapshot_.revision);
    write_u32(
        save_facing_bytes_, 16,
        static_cast<std::uint32_t>(snapshot_.world.party_x));
    write_u32(
        save_facing_bytes_, 20,
        static_cast<std::uint32_t>(snapshot_.world.party_y));
    write_u16(
        save_facing_bytes_, 24,
        static_cast<std::uint16_t>(snapshot_.scenario_id));
    save_facing_bytes_[26] =
        static_cast<std::uint8_t>(snapshot_.world.facing);
    save_facing_bytes_[27] = snapshot_.party.selected_member.value_or(
        static_cast<PartyMemberId>(0xFF));
    write_u16(
        save_facing_bytes_, 28,
        static_cast<std::uint16_t>(snapshot_.party.fatigue));
    for (std::size_t index = 0; index < snapshot_.party.members.size(); ++index) {
      save_facing_bytes_[30 + index] =
          snapshot_.party.members[index].selected ? 1 : 0;
    }
  }

  GameSnapshot snapshot_;
  SaveFacingBytes save_facing_bytes_{};
};

[[nodiscard]] LegacyActionHandlers handlers_for(
    ExplorationStateFixture& fixture) {
  LegacyActionHandlers handlers;
  handlers.move_party = [&fixture](const MovePartyAction& action) {
    return fixture.move(action);
  };
  handlers.select_party_member =
      [&fixture](const SelectPartyMemberAction& action) {
        return fixture.select(action);
      };
  handlers.open_inventory = [&fixture](const OpenInventoryAction& action) {
    return fixture.open_inventory(action);
  };
  handlers.open_spellbook = [&fixture](const OpenSpellbookAction& action) {
    return fixture.open_spellbook(action);
  };
  handlers.open_save_game = [&fixture](const OpenSaveGameAction& action) {
    return fixture.open_save_game(action);
  };
  return handlers;
}

struct ObservedStep {
  DispatchStatus status = DispatchStatus::unsupported;
  std::string detail;
  std::vector<GameEvent> events;
  GameSnapshot snapshot;
  ExplorationStateFixture::SaveFacingBytes save_facing_bytes{};

  bool operator==(const ObservedStep&) const = default;
};

struct ReplayTrace {
  GameSnapshot initial_snapshot;
  ExplorationStateFixture::SaveFacingBytes initial_save_facing_bytes{};
  std::vector<ObservedStep> steps;

  bool operator==(const ReplayTrace&) const = default;
};

enum class ReplayRoute {
  classic_adapter,
  remastered_semantic,
};

[[nodiscard]] ReplayTrace replay(
    ReplayRoute route,
    const std::vector<ReplayStep>& steps,
    int32_t initial_x = 40,
    int32_t initial_y = 40) {
  ExplorationStateFixture fixture(initial_x, initial_y);
  InjectedLegacyCommandBridge bridge(handlers_for(fixture));
  ReplayTrace trace;
  trace.initial_snapshot = fixture.capture();
  trace.initial_save_facing_bytes = fixture.save_facing_bytes();
  trace.steps.reserve(steps.size());

  ActionSequence sequence = 1;
  for (const auto& step : steps) {
    UIAction action;
    if (route == ReplayRoute::classic_adapter) {
      const auto adapted = adapt_classic_input(
          step.classic_input,
          sequence,
          fixture.capture().party.selected_member);
      if (!adapted) {
        throw std::runtime_error(
            "Classic adapter rejected replay step: " + std::string(step.label));
      }
      action = *adapted;
    } else {
      action = UIAction{
          .sequence = sequence,
          .payload = step.remastered_payload,
      };
    }

    const DispatchResult result = bridge.dispatch(action);
    trace.steps.emplace_back(ObservedStep{
        .status = result.status,
        .detail = result.detail,
        .events = result.events,
        .snapshot = fixture.capture(),
        .save_facing_bytes = fixture.save_facing_bytes(),
    });
    ++sequence;
  }
  return trace;
}

[[nodiscard]] std::size_t movement_index(MovementCommand command) {
  switch (command) {
    case MovementCommand::step_forward:
      return 0;
    case MovementCommand::step_backward:
      return 1;
    case MovementCommand::turn_left:
      return 2;
    case MovementCommand::turn_right:
      return 3;
    case MovementCommand::north:
      return 4;
    case MovementCommand::northeast:
      return 5;
    case MovementCommand::east:
      return 6;
    case MovementCommand::southeast:
      return 7;
    case MovementCommand::south:
      return 8;
    case MovementCommand::southwest:
      return 9;
    case MovementCommand::west:
      return 10;
    case MovementCommand::northwest:
      return 11;
  }
  throw std::logic_error("unknown movement command");
}

[[nodiscard]] std::vector<ReplayStep> complete_exploration_replay() {
  return {
      {"north", ClassicOutdoorScanCode{0x7E},
          MovePartyAction{MovementCommand::north}},
      {"northeast", ClassicOutdoorScanCode{0x5C},
          MovePartyAction{MovementCommand::northeast}},
      {"east", ClassicOutdoorScanCode{0x7C},
          MovePartyAction{MovementCommand::east}},
      {"southeast", ClassicOutdoorScanCode{0x55},
          MovePartyAction{MovementCommand::southeast}},
      {"south", ClassicOutdoorScanCode{0x7D},
          MovePartyAction{MovementCommand::south}},
      {"southwest", ClassicOutdoorScanCode{0x53},
          MovePartyAction{MovementCommand::southwest}},
      {"west", ClassicOutdoorScanCode{0x7B},
          MovePartyAction{MovementCommand::west}},
      {"northwest", ClassicOutdoorScanCode{0x59},
          MovePartyAction{MovementCommand::northwest}},
      {"step forward", ClassicDungeonKey{'8'},
          MovePartyAction{MovementCommand::step_forward}},
      {"turn right", ClassicDungeonKey{'6'},
          MovePartyAction{MovementCommand::turn_right}},
      {"step backward", ClassicDungeonKey{'2'},
          MovePartyAction{MovementCommand::step_backward}},
      {"turn left", ClassicDungeonKey{'4'},
          MovePartyAction{MovementCommand::turn_left}},
      {"select upper bound", ClassicPortraitClick{2},
          SelectPartyMemberAction{2}},
      {"select lower bound", ClassicPortraitClick{0},
          SelectPartyMemberAction{0}},
      {"select upper bound again", ClassicPortraitClick{2},
          SelectPartyMemberAction{2}},
      {"idempotent selection", ClassicPortraitClick{2},
          SelectPartyMemberAction{2}},
      {"open selected inventory", ClassicOpenInventoryKey{'i'},
          OpenInventoryAction{2}},
      {"open selected spellbook", ClassicOpenSpellbookKey{'s'},
          OpenSpellbookAction{2}},
      {"open Classic save chooser", ClassicOpenSaveMenu{129, 3},
          OpenSaveGameAction{}},
  };
}

void test_classic_adapter_matches_semantic_payloads() {
  const auto steps = complete_exploration_replay();
  std::array<bool, 12> movement_coverage{};
  std::optional<PartyMemberId> selected_member = 0;
  ActionSequence sequence = 1;
  for (const auto& step : steps) {
    const auto adapted = adapt_classic_input(
        step.classic_input, sequence, selected_member);
    CHECK(adapted.has_value());
    CHECK(adapted->sequence == sequence);
    CHECK(adapted->payload == step.remastered_payload);
    if (const auto* movement =
            std::get_if<MovePartyAction>(&step.remastered_payload)) {
      movement_coverage[movement_index(movement->command)] = true;
    }
    if (const auto* selection =
            std::get_if<SelectPartyMemberAction>(&step.remastered_payload)) {
      selected_member = selection->member;
    }
    ++sequence;
  }
  CHECK(std::all_of(
      movement_coverage.begin(), movement_coverage.end(),
      [](bool covered) { return covered; }));

  CHECK(!adapt_classic_input(ClassicOutdoorScanCode{0x00}, 1, 0));
  CHECK(!adapt_classic_input(ClassicDungeonKey{'?'}, 1, 0));
  CHECK(!adapt_classic_input(ClassicOpenInventoryKey{'?'}, 1, 0));
  CHECK(!adapt_classic_input(
      ClassicOpenInventoryKey{'i'}, 1, std::nullopt));
  CHECK(!adapt_classic_input(ClassicOpenSpellbookKey{'?'}, 1, 0));
  CHECK(!adapt_classic_input(
      ClassicOpenSpellbookKey{'s'}, 1, std::nullopt));
  CHECK(!adapt_classic_input(ClassicOpenSaveMenu{128, 3}, 1, 0));
  CHECK(!adapt_classic_input(ClassicOpenSaveMenu{129, 4}, 1, 0));
}

void test_equivalent_replay_and_determinism() {
  const auto steps = complete_exploration_replay();
  const ReplayTrace classic_first = replay(ReplayRoute::classic_adapter, steps);
  const ReplayTrace remastered_first =
      replay(ReplayRoute::remastered_semantic, steps);
  CHECK(classic_first == remastered_first);

  const ReplayTrace classic_second = replay(ReplayRoute::classic_adapter, steps);
  const ReplayTrace remastered_second =
      replay(ReplayRoute::remastered_semantic, steps);
  CHECK(classic_first == classic_second);
  CHECK(remastered_first == remastered_second);

  CHECK(classic_first.steps.size() == steps.size());
  GameSnapshot previous_snapshot = classic_first.initial_snapshot;
  auto previous_save_facing_bytes = classic_first.initial_save_facing_bytes;
  for (std::size_t index = 0; index < classic_first.steps.size(); ++index) {
    const auto& observed = classic_first.steps[index];
    CHECK(observed.status == DispatchStatus::handled);
    CHECK(observed.detail.empty());
    if (index < 15) {
      CHECK(observed.snapshot.revision == previous_snapshot.revision + 1);
      CHECK(observed.snapshot != previous_snapshot);
      CHECK(observed.save_facing_bytes != previous_save_facing_bytes);
      CHECK(observed.events.size() == 1);
      CHECK(observed.events[0].sequence == observed.snapshot.revision);
      const auto* audio = std::get_if<AudioCueEvent>(&observed.events[0].payload);
      CHECK(audio != nullptr);
      if (index == 9 || index == 11) {
        CHECK(audio->cue == "party_turn");
      } else if (index < 12) {
        CHECK(audio->cue == "party_step");
      } else {
        CHECK(audio->cue == "party_select");
      }
    } else {
      CHECK(observed.snapshot == previous_snapshot);
      CHECK(observed.save_facing_bytes == previous_save_facing_bytes);
      if (std::holds_alternative<OpenInventoryAction>(
              steps[index].remastered_payload)) {
        CHECK(observed.events.size() == 1);
        const auto* transition =
            std::get_if<ScreenTransitionEvent>(&observed.events[0].payload);
        CHECK(transition != nullptr);
        CHECK(transition->destination == ScreenContext::inventory);
      } else if (std::holds_alternative<OpenSpellbookAction>(
                     steps[index].remastered_payload)) {
        CHECK(observed.events.size() == 1);
        const auto* modal =
            std::get_if<ModalRequestEvent>(&observed.events[0].payload);
        CHECK(modal != nullptr);
        CHECK(modal->title == "Cast spell");
        CHECK(modal->body == "Cerys");
      } else if (std::holds_alternative<OpenSaveGameAction>(
                     steps[index].remastered_payload)) {
        CHECK(observed.events.size() == 1);
        const auto* modal =
            std::get_if<ModalRequestEvent>(&observed.events[0].payload);
        CHECK(modal != nullptr);
        CHECK(modal->title == "Save game");
        CHECK(modal->body == "Choose a Classic save slot");
      } else {
        CHECK(observed.events.empty());
      }
    }
    previous_snapshot = observed.snapshot;
    previous_save_facing_bytes = observed.save_facing_bytes;
  }

  const auto& final = classic_first.steps.back();
  CHECK(final.snapshot.revision == 15);
  CHECK(final.snapshot.world.party_x == 39);
  CHECK(final.snapshot.world.party_y == 39);
  CHECK(final.snapshot.world.facing == Facing::north);
  CHECK(final.snapshot.party.fatigue == 22);
  CHECK(final.snapshot.party.selected_member == 2);
  CHECK(final.snapshot.party.members[0].selected == false);
  CHECK(final.snapshot.party.members[1].selected == false);
  CHECK(final.snapshot.party.members[2].selected == true);
  CHECK(final.events.size() == 1);
  const auto* save_modal =
      std::get_if<ModalRequestEvent>(&final.events[0].payload);
  CHECK(save_modal != nullptr);
  CHECK(save_modal->title == "Save game");
  const auto& before_save_chooser =
      classic_first.steps[classic_first.steps.size() - 2U];
  CHECK(final.snapshot == before_save_chooser.snapshot);
  CHECK(final.save_facing_bytes == before_save_chooser.save_facing_bytes);
}

void test_selection_bounds_reject_without_mutation() {
  for (const PartyMemberId invalid_member :
       std::array<PartyMemberId, 2>{3, 0xFF}) {
    const std::vector<ReplayStep> steps{{
        "invalid selection",
        ClassicPortraitClick{invalid_member},
        SelectPartyMemberAction{invalid_member},
    }};
    const ReplayTrace classic = replay(ReplayRoute::classic_adapter, steps);
    const ReplayTrace remastered =
        replay(ReplayRoute::remastered_semantic, steps);
    CHECK(classic == remastered);
    CHECK(classic.steps.size() == 1);
    CHECK(classic.steps[0].status == DispatchStatus::rejected);
    CHECK(classic.steps[0].detail ==
        "party member is outside fixture bounds");
    CHECK(classic.steps[0].events.empty());
    CHECK(classic.steps[0].snapshot == classic.initial_snapshot);
    CHECK(classic.steps[0].save_facing_bytes ==
        classic.initial_save_facing_bytes);
  }
}

void test_blocked_movement_rejects_without_mutation() {
  const std::vector<ReplayStep> steps{{
      "west from map edge",
      ClassicOutdoorScanCode{0x7B},
      MovePartyAction{MovementCommand::west},
  }};
  const ReplayTrace classic =
      replay(ReplayRoute::classic_adapter, steps, 0, 40);
  const ReplayTrace remastered =
      replay(ReplayRoute::remastered_semantic, steps, 0, 40);
  CHECK(classic == remastered);
  CHECK(classic.steps[0].status == DispatchStatus::rejected);
  CHECK(classic.steps[0].detail == "movement would leave fixture map");
  CHECK(classic.steps[0].events.empty());
  CHECK(classic.steps[0].snapshot == classic.initial_snapshot);
  CHECK(classic.steps[0].save_facing_bytes ==
      classic.initial_save_facing_bytes);
}

} // namespace

int main() {
  try {
    test_classic_adapter_matches_semantic_payloads();
    test_equivalent_replay_and_determinism();
    test_selection_bounds_reject_without_mutation();
    test_blocked_movement_rejects_without_mutation();
    std::cout << "ExplorationActionEquivalenceTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ExplorationActionEquivalenceTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
