#pragma once

#include <array>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>

#include "SDLHelpers.hpp"
#include "remaster/assets/PartyPortraitCatalog.hpp"

namespace realmz::remaster::assets {

// Renderer-owned, all-or-nothing realization of the four approved party
// portraits. The owner must destroy this cache before destroying its renderer.
// Draw authorization always consumes the complete live post-selection proof;
// there is deliberately no portrait-ID-only entry point.
class PartyPortraitTextureCache {
public:
  explicit PartyPortraitTextureCache(
      SDL_Renderer* renderer, PartyPortraitCatalog catalog);

  [[nodiscard]] bool draw(
      SDL_Renderer* renderer,
      const PostSelectionResult& selection,
      const SDL_FRect& destination) const noexcept;

private:
  struct Record {
    ResourceKey key;
    sdl_texture_ptr texture;
  };
  using Records = std::array<Record, 4>;

  SDL_Renderer* renderer_;
  PartyPortraitCatalog catalog_;
  Records records_;
};

} // namespace realmz::remaster::assets
