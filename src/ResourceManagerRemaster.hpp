#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Types.h"
#include "presentation/PresentationMode.hpp"
#include "remaster/assets/ResourceSelectionHook.hpp"

namespace realmz::remaster::assets {

// Host integration calls this whenever the persisted presentation preference
// changes. The Resource Manager defaults to Classic, so legacy startup and
// callers that know nothing about remastered assets remain untouched.
void setResourcePresentationMode(presentation::PresentationMode mode);
[[nodiscard]] presentation::PresentationMode resourcePresentationMode() noexcept;

// Exact handoff point for the PNG decoder in QuickDraw: call only after
// GetResource/Get1Resource has selected a Handle. Passthrough entries require
// no change to the Handle or format; approved entries carry a hash-validated
// override path and the Classic logical dimensions to render into.
[[nodiscard]] std::optional<PostSelectionResult>
resourceAssetSelectionForHandle(Handle selectedResource);

// Re-runs the legacy search precedence for this type/id and returns proof for
// the actual current winner. Native renderers must request this immediately
// before drawing; an integer resource ID alone never authorizes an override.
// A writable resource with pending changes deliberately has no proof. Missing
// resources return no proof without emitting the legacy search-chain log.
[[nodiscard]] std::optional<PostSelectionResult>
resourceAssetSelectionForCurrentWinner(
    std::uint32_t type, std::int16_t id);

// Returns the immutable bytes from the selected resource fork, even if a
// QuickDraw decoder has replaced the mutable Handle contents. This lets mode
// switches reconstruct the Classic representation without reopening or
// re-running the legacy resource search chain.
[[nodiscard]] std::optional<std::string>
resourceClassicPayloadForHandle(Handle selectedResource);

// The most recent deterministic coverage failure, empty when none has been
// observed since entering the current mode.
[[nodiscard]] std::string lastResourceAssetDiagnostic();

} // namespace realmz::remaster::assets
