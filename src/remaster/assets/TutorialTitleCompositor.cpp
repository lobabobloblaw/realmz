#include "remaster/assets/TutorialTitleCompositor.hpp"

#include <string_view>

#include <SDL3/SDL_blendmode.h>

namespace realmz::remaster::assets {
namespace {

constexpr std::string_view kTutorialTitle = "TUTORIAL";
constexpr SDL_Color kTutorialTitleInk{31, 21, 13, 255};

bool plaqueRectFits(const SDL_Surface& surface) noexcept {
  return kTutorialTitlePlaqueTextRect.x >= 0 &&
      kTutorialTitlePlaqueTextRect.y >= 0 &&
      kTutorialTitlePlaqueTextRect.w > 0 &&
      kTutorialTitlePlaqueTextRect.h > 0 &&
      kTutorialTitlePlaqueTextRect.x <= surface.w &&
      kTutorialTitlePlaqueTextRect.y <= surface.h &&
      kTutorialTitlePlaqueTextRect.w <=
          surface.w - kTutorialTitlePlaqueTextRect.x &&
      kTutorialTitlePlaqueTextRect.h <=
          surface.h - kTutorialTitlePlaqueTextRect.y;
}

} // namespace

bool tutorialTitleEligible(
    const ResourceKey& key,
    std::string_view approvedOutputSha256,
    AssetDimensions logicalDimensions) noexcept {
  return key.pack == "Scenarios/Tutorial/Scenario" &&
      key.type == "PICT" && key.id == 32128 &&
      approvedOutputSha256 == kTutorialTitleApprovedOutputSha256 &&
      logicalDimensions == kTutorialTitleLogicalDimensions;
}

sdl_surface_ptr composeTutorialTitle(
    const ResourceKey& key,
    std::string_view approvedOutputSha256,
    AssetDimensions logicalDimensions,
    SDL_Surface* source,
    TTF_Font* font) noexcept {
  if (!tutorialTitleEligible(
          key, approvedOutputSha256, logicalDimensions) ||
      source == nullptr || font == nullptr ||
      source->w != static_cast<int>(kTutorialTitleLogicalDimensions.width) ||
      source->h != static_cast<int>(kTutorialTitleLogicalDimensions.height) ||
      !plaqueRectFits(*source)) {
    return {};
  }

  sdl_surface_ptr result{SDL_DuplicateSurface(source)};
  if (!result) {
    return {};
  }

  sdl_surface_ptr title{TTF_RenderText_Blended(
      font, kTutorialTitle.data(), kTutorialTitle.size(), kTutorialTitleInk)};
  if (!title || title->w <= 0 || title->h <= 0 ||
      title->w > kTutorialTitlePlaqueTextRect.w ||
      title->h > kTutorialTitlePlaqueTextRect.h ||
      !SDL_SetSurfaceBlendMode(title.get(), SDL_BLENDMODE_BLEND)) {
    return {};
  }

  SDL_Rect destination{
      kTutorialTitlePlaqueTextRect.x +
          (kTutorialTitlePlaqueTextRect.w - title->w) / 2,
      kTutorialTitlePlaqueTextRect.y +
          (kTutorialTitlePlaqueTextRect.h - title->h) / 2,
      title->w,
      title->h,
  };
  if (!SDL_BlitSurface(title.get(), nullptr, result.get(), &destination)) {
    return {};
  }

  return result;
}

} // namespace realmz::remaster::assets
