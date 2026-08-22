#include "remaster/assets/ShellMaterialTextureCache.hpp"

#include <cstddef>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

#include <SDL3/SDL.h>

#include "remaster/assets/VerifiedRasterSurface.hpp"

namespace realmz::remaster::assets {

ShellMaterialTextureCache::ShellMaterialTextureCache(
    SDL_Renderer* renderer, const ShellMaterialCatalog& catalog)
    : renderer_(renderer) {
  if (renderer == nullptr) {
    throw std::invalid_argument(
        "Cannot create shell materials without an SDL renderer");
  }

  Records loaded;
  std::size_t index = 0;
  for (const auto& material : catalog.materials()) {
    if (index >= loaded.size()) {
      throw std::runtime_error(
          "Shell material catalog contains too many records");
    }
    auto surface = loadVerifiedRasterSurface(
        material.path, material.key, material.sharedMasterSha256);
    if ((surface->w <= 0) || (surface->h <= 0) ||
        (material.logicalDimensions.width == 0U) ||
        (material.logicalDimensions.height == 0U)) {
      throw std::runtime_error(std::format(
          "Shell material {} has invalid physical or logical dimensions",
          material.key.toString()));
    }

    const float horizontalScale =
        static_cast<float>(material.logicalDimensions.width) /
        static_cast<float>(surface->w);
    const float verticalScale =
        static_cast<float>(material.logicalDimensions.height) /
        static_cast<float>(surface->h);
    if (!std::isfinite(horizontalScale) || !std::isfinite(verticalScale) ||
        (horizontalScale <= 0.0f) || (verticalScale <= 0.0f) ||
        (std::abs(horizontalScale - verticalScale) > 0.0001f)) {
      throw std::runtime_error(std::format(
          "Shell material {} does not have a uniform logical tile scale",
          material.key.toString()));
    }

    auto texture = sdl_make_unique(
        SDL_CreateTextureFromSurface(renderer, surface.get()));
    if (!texture) {
      throw std::runtime_error(std::format(
          "Could not create shell material texture {}",
          material.key.toString()));
    }
    if (!SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR)) {
      throw std::runtime_error(std::format(
          "Could not set shell material sampling mode {}",
          material.key.toString()));
    }
    loaded[index++] = Record{
        .role = material.role,
        .texture = std::move(texture),
        .scale = horizontalScale,
    };
  }
  this->records_ = std::move(loaded);
}

bool ShellMaterialTextureCache::draw(
    SDL_Renderer* renderer,
    ShellMaterialRole role,
    const SDL_FRect& destination) const noexcept {
  if ((renderer == nullptr) || (renderer != this->renderer_)) {
    return false;
  }
  for (const auto& record : this->records_) {
    if (record.role == role) {
      return record.texture && SDL_RenderTextureTiled(
          renderer, record.texture.get(), nullptr, record.scale,
          &destination);
    }
  }
  return false;
}

} // namespace realmz::remaster::assets
