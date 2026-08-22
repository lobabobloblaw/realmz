#pragma once

#include <array>

#include <SDL3/SDL_render.h>

#include "SDLHelpers.hpp"
#include "remaster/assets/ShellMaterialCatalog.hpp"

namespace realmz::remaster::assets {

// Renderer-owned, all-or-nothing texture realization of a validated shell
// material catalog. The owner must destroy or invalidate this cache before its
// SDL renderer is destroyed or reset.
class ShellMaterialTextureCache {
public:
  explicit ShellMaterialTextureCache(
      SDL_Renderer* renderer, const ShellMaterialCatalog& catalog);

  [[nodiscard]] bool draw(
      SDL_Renderer* renderer,
      ShellMaterialRole role,
      const SDL_FRect& destination) const noexcept;

private:
  struct Record {
    ShellMaterialRole role = ShellMaterialRole::neutral;
    sdl_texture_ptr texture;
    float scale = 1.0f;
  };
  using Records = std::array<Record, 4>;

  SDL_Renderer* renderer_;
  Records records_;
};

} // namespace realmz::remaster::assets
