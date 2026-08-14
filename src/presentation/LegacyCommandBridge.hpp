#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "GameEvent.hpp"
#include "UIAction.hpp"

namespace realmz::presentation {

enum class DispatchStatus {
  handled,
  unsupported,
  rejected,
  failed,
};

struct DispatchResult {
  DispatchStatus status = DispatchStatus::unsupported;
  std::string detail;
  std::vector<GameEvent> events;

  [[nodiscard]] bool was_handled() const noexcept {
    return this->status == DispatchStatus::handled;
  }

  [[nodiscard]] static DispatchResult handled(
      std::vector<GameEvent> events = {}) {
    return {DispatchStatus::handled, {}, std::move(events)};
  }

  [[nodiscard]] static DispatchResult unsupported(std::string detail) {
    return {DispatchStatus::unsupported, std::move(detail), {}};
  }

  [[nodiscard]] static DispatchResult rejected(std::string detail) {
    return {DispatchStatus::rejected, std::move(detail), {}};
  }

  [[nodiscard]] static DispatchResult failed(std::string detail) {
    return {DispatchStatus::failed, std::move(detail), {}};
  }
};

class LegacyCommandBridge {
public:
  virtual ~LegacyCommandBridge() = default;

  // Implementations run on the engine/UI thread. They must finish dispatching
  // before another action is submitted; this deliberately provides no
  // reentrant or concurrent mutation surface.
  [[nodiscard]] virtual DispatchResult dispatch(const UIAction& action) = 0;
};

template <typename Action>
using LegacyActionHandler = std::function<DispatchResult(const Action&)>;

// Handlers are typed so a cast-spell callback cannot accidentally receive an
// inventory payload. Empty handlers are reported as unsupported, which lets
// the remastered UI expose migration coverage without falling through to an
// unsafe legacy command.
struct LegacyActionHandlers {
  LegacyActionHandler<MovePartyAction> move_party;
  LegacyActionHandler<SelectPartyMemberAction> select_party_member;
  LegacyActionHandler<OpenInventoryAction> open_inventory;
  LegacyActionHandler<OpenSpellbookAction> open_spellbook;
  LegacyActionHandler<OpenSaveGameAction> open_save_game;
  LegacyActionHandler<OpenLoadGameAction> open_load_game;
  LegacyActionHandler<GuardCombatantAction> guard_combatant;
  LegacyActionHandler<FinishCombatantAction> finish_combatant;
  LegacyActionHandler<DelayCombatantAction> delay_combatant;
  LegacyActionHandler<CenterActiveCombatantAction> center_active_combatant;
  LegacyActionHandler<InventoryAction> inventory;
  LegacyActionHandler<CastSpellAction> cast_spell;
  LegacyActionHandler<TradeAction> trade;
  LegacyActionHandler<SaveGameAction> save_game;
  LegacyActionHandler<LoadGameAction> load_game;
  LegacyActionHandler<ConfirmAction> confirm;
  LegacyActionHandler<CancelAction> cancel;
  LegacyActionHandler<SetPresentationModeAction> set_presentation_mode;
};

class InjectedLegacyCommandBridge final : public LegacyCommandBridge {
public:
  explicit InjectedLegacyCommandBridge(LegacyActionHandlers handlers);

  [[nodiscard]] DispatchResult dispatch(const UIAction& action) override;

private:
  LegacyActionHandlers handlers_;
  bool dispatch_in_progress_ = false;
};

} // namespace realmz::presentation
