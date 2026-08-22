#pragma once

#include <string_view>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "SDLHelpers.hpp"
#include "remaster/assets/AssetManifest.hpp"

namespace realmz::remaster::assets {

inline constexpr std::string_view kTutorialTitleApprovedOutputSha256 =
    "b3ad37374ae9b92a0bf745627c4db0d5022b53e16fa9d106bc4b5b4ecc032bba";
inline constexpr AssetDimensions kTutorialTitleLogicalDimensions{320U, 320U};

// Reviewed against 21_tutorial_city_PICT_32128.png at its 320x320 Classic
// logical size. The half-open rectangle stays inside the blank inset of the
// top plaque; the compositor cannot alter pixels outside it.
inline constexpr SDL_Rect kTutorialTitlePlaqueTextRect{40, 14, 240, 48};

[[nodiscard]] bool tutorialTitleEligible(
    const ResourceKey& key,
    std::string_view approvedOutputSha256,
    AssetDimensions logicalDimensions) noexcept;

// Returns an independently owned, composited copy. Any ineligible input or
// SDL/TTF failure returns null and leaves source untouched. The caller owns the
// font and must keep it alive for the duration of this call.
[[nodiscard]] sdl_surface_ptr composeTutorialTitle(
    const ResourceKey& key,
    std::string_view approvedOutputSha256,
    AssetDimensions logicalDimensions,
    SDL_Surface* source,
    TTF_Font* font) noexcept;

} // namespace realmz::remaster::assets
