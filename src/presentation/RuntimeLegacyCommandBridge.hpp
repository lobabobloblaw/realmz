#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "LegacyCommandBridge.hpp"

namespace realmz::presentation {

// The production bridge captures this value immediately before queueing a
// semantic command. It deliberately contains no legacy pointers or mutable
// save structures.
struct RuntimeLegacyCommandContext {
  ScreenContext screen = ScreenContext::title;
  WorldPresentation world_presentation = WorldPresentation::none;
  bool adaptive_eligible = false;

  bool operator==(const RuntimeLegacyCommandContext&) const = default;
};

using RuntimeLegacyContextProvider =
    std::function<RuntimeLegacyCommandContext()>;
using RuntimeLegacyKeySink = std::function<bool(uint32_t)>;
using RuntimeLegacyMovementSink = std::function<bool(
    MovementCommand,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyPartySelectionSink = std::function<bool(
    PartyMemberId,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenInventorySink = std::function<bool(
    PartyMemberId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;
using RuntimeLegacyOpenSpellbookSink = std::function<bool(
    PartyMemberId,
    uint32_t,
    const RuntimeLegacyCommandContext&)>;

// Returns the exact Classic Mac key message already consumed by the preserved
// exploration/dungeon event loops. Unsupported command/context combinations
// return nullopt instead of reaching into engine globals directly.
[[nodiscard]] std::optional<uint32_t> legacy_key_message_for_movement(
    MovementCommand command,
    const RuntimeLegacyCommandContext& context) noexcept;

// Opening inventory is intentionally distinct from item-level InventoryAction
// commands. The returned message is the preserved Classic "i" key record and
// is available only on the two guarded top-level gameplay surfaces.
[[nodiscard]] std::optional<uint32_t> legacy_key_message_for_open_inventory(
    const RuntimeLegacyCommandContext& context) noexcept;

// Returns the preserved Classic "s" key record used to enter the spell
// chooser. The command is exposed only on guarded exploration/dungeon
// surfaces; spell selection and targeting stay in the Classic flow.
[[nodiscard]] std::optional<uint32_t> legacy_key_message_for_open_spellbook(
    const RuntimeLegacyCommandContext& context) noexcept;

// Live UIAction boundary for the migrated command subset. The bridge queues
// legacy input for the next normal event-loop iteration; it never calls nested
// Classic screen functions synchronously.
class RuntimeLegacyCommandBridge final : public LegacyCommandBridge {
public:
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyKeySink key_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyKeySink key_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink);
  RuntimeLegacyCommandBridge(
      RuntimeLegacyContextProvider context_provider,
      RuntimeLegacyMovementSink movement_sink,
      RuntimeLegacyPartySelectionSink party_selection_sink,
      RuntimeLegacyOpenInventorySink open_inventory_sink,
      RuntimeLegacyOpenSpellbookSink open_spellbook_sink);

  [[nodiscard]] DispatchResult dispatch(const UIAction& action) override;

private:
  InjectedLegacyCommandBridge injected_bridge_;
};

} // namespace realmz::presentation
