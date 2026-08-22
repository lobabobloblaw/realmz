#include "remaster/assets/PartyPortraitTextureCache.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL.h>

#include "remaster/assets/ResourceSelectionHook.hpp"
#include "remaster/assets/VerifiedRasterSurface.hpp"

namespace realmz::remaster::assets {
namespace {

constexpr float kScaleTolerance = 0.0001F;

[[noreturn]] void fail(
    const ResourceKey& key, std::string_view phase) {
  throw std::runtime_error(
      "Party portrait texture " + key.toString() +
      " failed during " + std::string(phase));
}

[[nodiscard]] bool validDestination(
    const SDL_FRect& destination) noexcept {
  return std::isfinite(destination.x) &&
      std::isfinite(destination.y) &&
      std::isfinite(destination.w) &&
      std::isfinite(destination.h) &&
      destination.w > 0.0F && destination.h > 0.0F;
}

} // namespace

PartyPortraitTextureCache::PartyPortraitTextureCache(
    SDL_Renderer* renderer, PartyPortraitCatalog catalog)
    : renderer_(renderer), catalog_(std::move(catalog)) {
  if (renderer == nullptr) {
    throw std::invalid_argument(
        "Cannot create party portrait textures without an SDL renderer");
  }

  Records loaded;
  bool hasCommonScale = false;
  float commonScale = 0.0F;
  std::size_t index = 0;
  for (const auto& portrait : this->catalog_.portraits()) {
    if (index >= loaded.size()) {
      fail(portrait.key, "catalog-size");
    }

    auto surface = loadVerifiedRasterSurface(
        portrait.path, portrait.key, portrait.approvedContentSha256);
    if (!surface || surface->w <= 0 || surface->h <= 0 ||
        portrait.logicalDimensions.width == 0U ||
        portrait.logicalDimensions.height == 0U) {
      fail(portrait.key, "dimensions");
    }

    const float horizontalScale =
        static_cast<float>(portrait.logicalDimensions.width) /
        static_cast<float>(surface->w);
    const float verticalScale =
        static_cast<float>(portrait.logicalDimensions.height) /
        static_cast<float>(surface->h);
    if (!std::isfinite(horizontalScale) ||
        !std::isfinite(verticalScale) ||
        horizontalScale <= 0.0F || verticalScale <= 0.0F ||
        std::abs(horizontalScale - verticalScale) > kScaleTolerance) {
      fail(portrait.key, "scale");
    }
    if (hasCommonScale) {
      if (std::abs(horizontalScale - commonScale) > kScaleTolerance) {
        fail(portrait.key, "scale");
      }
    } else {
      commonScale = horizontalScale;
      hasCommonScale = true;
    }

    sdl_texture_ptr texture(
        SDL_CreateTextureFromSurface(renderer, surface.get()));
    if (!texture) {
      fail(portrait.key, "texture");
    }
    if (!SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR)) {
      fail(portrait.key, "sampling");
    }
    if (!SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND)) {
      fail(portrait.key, "blending");
    }

    loaded[index++] = Record{
        .key = portrait.key,
        .texture = std::move(texture),
    };
  }
  if (index != loaded.size() || !hasCommonScale) {
    throw std::runtime_error(
        "Party portrait texture catalog failed during catalog-size");
  }

  // Publish only after every verified surface and configured texture exists.
  // If any earlier step throws, the local array destroys all partial textures.
  this->records_ = std::move(loaded);
}

bool PartyPortraitTextureCache::draw(
    SDL_Renderer* renderer,
    const PostSelectionResult& selection,
    const SDL_FRect& destination) const noexcept {
  if (renderer == nullptr || renderer != this->renderer_ ||
      !validDestination(destination)) {
    return false;
  }

  const auto* portrait =
      this->catalog_.authorizeSelectedPortrait(selection);
  if (portrait == nullptr) {
    return false;
  }
  for (const auto& record : this->records_) {
    if (record.key == portrait->key) {
      return record.texture && SDL_RenderTexture(
          renderer, record.texture.get(), nullptr, &destination);
    }
  }
  return false;
}

} // namespace realmz::remaster::assets
