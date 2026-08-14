#include "LegacyCommandBridge.hpp"

#include <exception>
#include <format>
#include <string_view>
#include <type_traits>

namespace realmz::presentation {

namespace {

template <typename Action>
DispatchResult invoke_handler(
    const LegacyActionHandler<Action>& handler,
    const Action& action,
    std::string_view name) noexcept {
  if (!handler) {
    return DispatchResult::unsupported(
        std::format("No legacy handler registered for {}", name));
  }

  try {
    return handler(action);
  } catch (const std::exception& e) {
    return DispatchResult::failed(std::format(
        "Legacy handler for {} threw an exception: {}", name, e.what()));
  } catch (...) {
    return DispatchResult::failed(std::format(
        "Legacy handler for {} threw a non-standard exception", name));
  }
}

class DispatchGuard {
public:
  explicit DispatchGuard(bool& in_progress) : in_progress_(in_progress) {
    this->in_progress_ = true;
  }

  ~DispatchGuard() {
    this->in_progress_ = false;
  }

  DispatchGuard(const DispatchGuard&) = delete;
  DispatchGuard& operator=(const DispatchGuard&) = delete;

private:
  bool& in_progress_;
};

} // namespace

InjectedLegacyCommandBridge::InjectedLegacyCommandBridge(
    LegacyActionHandlers handlers)
    : handlers_(std::move(handlers)) {}

DispatchResult InjectedLegacyCommandBridge::dispatch(const UIAction& action) {
  if (this->dispatch_in_progress_) {
    return DispatchResult::rejected(
        "Legacy command dispatch is not reentrant");
  }
  DispatchGuard guard(this->dispatch_in_progress_);

  return std::visit([this](const auto& payload) -> DispatchResult {
    using Action = std::decay_t<decltype(payload)>;
    if constexpr (std::is_same_v<Action, MovePartyAction>) {
      return invoke_handler(this->handlers_.move_party, payload, "move_party");
    } else if constexpr (std::is_same_v<Action, SelectPartyMemberAction>) {
      return invoke_handler(
          this->handlers_.select_party_member, payload, "select_party_member");
    } else if constexpr (std::is_same_v<Action, OpenInventoryAction>) {
      return invoke_handler(
          this->handlers_.open_inventory, payload, "open_inventory");
    } else if constexpr (std::is_same_v<Action, OpenSpellbookAction>) {
      return invoke_handler(
          this->handlers_.open_spellbook, payload, "open_spellbook");
    } else if constexpr (std::is_same_v<Action, InventoryAction>) {
      return invoke_handler(this->handlers_.inventory, payload, "inventory");
    } else if constexpr (std::is_same_v<Action, CastSpellAction>) {
      return invoke_handler(this->handlers_.cast_spell, payload, "cast_spell");
    } else if constexpr (std::is_same_v<Action, TradeAction>) {
      return invoke_handler(this->handlers_.trade, payload, "trade");
    } else if constexpr (std::is_same_v<Action, SaveGameAction>) {
      return invoke_handler(this->handlers_.save_game, payload, "save_game");
    } else if constexpr (std::is_same_v<Action, LoadGameAction>) {
      return invoke_handler(this->handlers_.load_game, payload, "load_game");
    } else if constexpr (std::is_same_v<Action, ConfirmAction>) {
      return invoke_handler(this->handlers_.confirm, payload, "confirm");
    } else if constexpr (std::is_same_v<Action, CancelAction>) {
      return invoke_handler(this->handlers_.cancel, payload, "cancel");
    } else if constexpr (std::is_same_v<Action, SetDrawerPanelAction>) {
      return DispatchResult::unsupported(
          "Presentation-local set_drawer_panel cannot cross the legacy bridge");
    } else {
      return invoke_handler(
          this->handlers_.set_presentation_mode,
          payload,
          "set_presentation_mode");
    }
  }, action.payload);
}

} // namespace realmz::presentation
