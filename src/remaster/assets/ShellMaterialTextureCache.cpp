#include "remaster/assets/ShellMaterialTextureCache.hpp"

#include <cstddef>
#include <cmath>
#include <fstream>
#include <format>
#include <ios>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace realmz::remaster::assets {
namespace {

constexpr std::size_t kMaximumEncodedMaterialBytes = 16U * 1024U * 1024U;

[[nodiscard]] std::string loadVerifiedMaterialBytes(
    const ShellMaterialDescriptor& material) {
  std::ifstream input(material.path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error(std::format(
        "Could not open shell material {}", material.key.toString()));
  }

  const auto end = input.tellg();
  if ((end <= 0) ||
      (end > static_cast<std::streamoff>(kMaximumEncodedMaterialBytes))) {
    throw std::runtime_error(std::format(
        "Shell material {} has an invalid encoded size",
        material.key.toString()));
  }
  const auto byteCount = static_cast<std::size_t>(end);
  if (byteCount > static_cast<std::size_t>(
                      std::numeric_limits<std::streamsize>::max())) {
    throw std::runtime_error(std::format(
        "Shell material {} cannot be read on this platform",
        material.key.toString()));
  }

  std::string bytes(byteCount, '\0');
  input.seekg(0, std::ios::beg);
  if (!input ||
      !input.read(bytes.data(), static_cast<std::streamsize>(byteCount))) {
    throw std::runtime_error(std::format(
        "Could not read shell material {}", material.key.toString()));
  }
  if (assetContentSha256Hex(bytes) != material.sharedMasterSha256) {
    throw std::runtime_error(std::format(
        "Shell material {} changed after catalog validation",
        material.key.toString()));
  }
  return bytes;
}

} // namespace

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
    const auto encoded = loadVerifiedMaterialBytes(material);
    auto* stream = SDL_IOFromConstMem(encoded.data(), encoded.size());
    if (stream == nullptr) {
      throw std::runtime_error(std::format(
          "Could not create input stream for shell material {}",
          material.key.toString()));
    }
    auto surface = sdl_make_unique(IMG_Load_IO(stream, true));
    if (!surface) {
      throw std::runtime_error(std::format(
          "Could not decode shell material {}", material.key.toString()));
    }
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
