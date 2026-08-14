#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "GameSnapshot.hpp"
#include "PresentationMode.hpp"

namespace realmz::presentation {

using ActionSequence = uint64_t;

enum class MovementCommand {
  step_forward,
  step_backward,
  turn_left,
  turn_right,
  north,
  northeast,
  east,
  southeast,
  south,
  southwest,
  west,
  northwest,
};

struct MovePartyAction {
  MovementCommand command = MovementCommand::step_forward;

  bool operator==(const MovePartyAction&) const = default;
};

struct SelectPartyMemberAction {
  PartyMemberId member = 0;

  bool operator==(const SelectPartyMemberAction&) const = default;
};

enum class InventoryVerb {
  use,
  equip,
  unequip,
  drop,
  transfer,
};

struct InventoryAction {
  InventoryVerb verb = InventoryVerb::use;
  PartyMemberId actor = 0;
  ItemInstanceId item = 0;
  std::optional<PartyMemberId> recipient;
  uint16_t quantity = 1;

  bool operator==(const InventoryAction&) const = default;
};

enum class TargetKind {
  none,
  party_member,
  combatant,
  map_cell,
};

struct ActionTarget {
  TargetKind kind = TargetKind::none;
  int32_t primary = 0;
  int32_t secondary = 0;

  bool operator==(const ActionTarget&) const = default;

  [[nodiscard]] static constexpr ActionTarget none() noexcept {
    return {};
  }

  [[nodiscard]] static constexpr ActionTarget party_member(PartyMemberId member) noexcept {
    return {TargetKind::party_member, member, 0};
  }

  [[nodiscard]] static constexpr ActionTarget combatant(CombatantId combatant_id) noexcept {
    return {TargetKind::combatant, combatant_id, 0};
  }

  [[nodiscard]] static constexpr ActionTarget map_cell(int32_t x, int32_t y) noexcept {
    return {TargetKind::map_cell, x, y};
  }
};

struct CastSpellAction {
  PartyMemberId caster = 0;
  int32_t spell_id = 0;
  ActionTarget target;

  bool operator==(const CastSpellAction&) const = default;
};

enum class TradeVerb {
  buy,
  sell,
};

struct TradeAction {
  TradeVerb verb = TradeVerb::buy;
  int32_t merchant_item_id = 0;
  uint16_t quantity = 1;
  std::optional<PartyMemberId> party_member;

  bool operator==(const TradeAction&) const = default;
};

struct SaveGameAction {
  std::string slot;

  bool operator==(const SaveGameAction&) const = default;
};

struct LoadGameAction {
  std::string slot;

  bool operator==(const LoadGameAction&) const = default;
};

struct ConfirmAction {
  bool operator==(const ConfirmAction&) const = default;
};

struct CancelAction {
  bool operator==(const CancelAction&) const = default;
};

// Presentation changes are semantic UI commands but do not mutate save data.
// Keeping them in the same stream allows deterministic input replays.
struct SetPresentationModeAction {
  PresentationMode mode = PresentationMode::classic;

  bool operator==(const SetPresentationModeAction&) const = default;
};

using UIActionPayload = std::variant<
    MovePartyAction,
    SelectPartyMemberAction,
    InventoryAction,
    CastSpellAction,
    TradeAction,
    SaveGameAction,
    LoadGameAction,
    ConfirmAction,
    CancelAction,
    SetPresentationModeAction>;

struct UIAction {
  ActionSequence sequence = 0;
  UIActionPayload payload;

  bool operator==(const UIAction&) const = default;
};

[[nodiscard]] inline std::string_view action_name(const UIActionPayload& payload) noexcept {
  return std::visit([](const auto& action) -> std::string_view {
    using Action = std::decay_t<decltype(action)>;
    if constexpr (std::is_same_v<Action, MovePartyAction>) {
      return "move_party";
    } else if constexpr (std::is_same_v<Action, SelectPartyMemberAction>) {
      return "select_party_member";
    } else if constexpr (std::is_same_v<Action, InventoryAction>) {
      return "inventory";
    } else if constexpr (std::is_same_v<Action, CastSpellAction>) {
      return "cast_spell";
    } else if constexpr (std::is_same_v<Action, TradeAction>) {
      return "trade";
    } else if constexpr (std::is_same_v<Action, SaveGameAction>) {
      return "save_game";
    } else if constexpr (std::is_same_v<Action, LoadGameAction>) {
      return "load_game";
    } else if constexpr (std::is_same_v<Action, ConfirmAction>) {
      return "confirm";
    } else if constexpr (std::is_same_v<Action, CancelAction>) {
      return "cancel";
    } else {
      return "set_presentation_mode";
    }
  }, payload);
}

} // namespace realmz::presentation
