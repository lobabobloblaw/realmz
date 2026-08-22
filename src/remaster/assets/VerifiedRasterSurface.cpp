#include "remaster/assets/VerifiedRasterSurface.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <ios>
#include <limits>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "remaster/assets/AssetManifest.hpp"

namespace realmz::remaster::assets {
namespace {

constexpr std::array<unsigned char, 8> kPngSignature{
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
constexpr std::size_t kMinimumPngHeaderBytes = 33U;
constexpr std::uint32_t kPngIhdrDataBytes = 13U;

[[nodiscard]] std::string_view stableKeyLabel(const ResourceKey& key) noexcept {
  if (!ResourceKey::isValidPack(key.pack) ||
      !ResourceKey::isValidType(key.type)) {
    return "invalid-resource-key";
  }
  return {};
}

[[nodiscard]] std::string failureMessage(
    const ResourceKey& key, VerifiedRasterSurfacePhase phase) {
  const auto fallback = stableKeyLabel(key);
  const auto label = fallback.empty() ? key.toString() : std::string(fallback);
  return "Verified raster " + label + " failed during " +
      std::string(verifiedRasterSurfacePhaseName(phase));
}

[[noreturn]] void fail(
    const ResourceKey& key, VerifiedRasterSurfacePhase phase) {
  throw VerifiedRasterSurfaceError(key, phase);
}

[[nodiscard]] bool isLowercaseSha256(std::string_view value) noexcept {
  return value.size() == 64U &&
      std::ranges::all_of(value, [](const unsigned char byte) {
        return (byte >= '0' && byte <= '9') ||
            (byte >= 'a' && byte <= 'f');
      });
}

[[nodiscard]] std::string loadBoundedBytes(
    const std::filesystem::path& path, const ResourceKey& key) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    fail(key, VerifiedRasterSurfacePhase::open);
  }

  const auto end = input.tellg();
  if (end <= 0 ||
      end > static_cast<std::streamoff>(
                kMaximumVerifiedRasterEncodedBytes)) {
    fail(key, VerifiedRasterSurfacePhase::encodedSize);
  }
  const auto byteCount = static_cast<std::size_t>(end);
  if (byteCount > static_cast<std::size_t>(
                      std::numeric_limits<std::streamsize>::max())) {
    fail(key, VerifiedRasterSurfacePhase::encodedSize);
  }

  std::string bytes(byteCount, '\0');
  input.seekg(0, std::ios::beg);
  if (!input ||
      !input.read(bytes.data(), static_cast<std::streamsize>(byteCount))) {
    fail(key, VerifiedRasterSurfacePhase::read);
  }

  // The descriptor remains open across sizing and reading. Reject growth on
  // that same descriptor so the decoded buffer is a complete file snapshot.
  if (input.peek() != std::char_traits<char>::eof() || input.bad()) {
    fail(key, VerifiedRasterSurfacePhase::read);
  }
  return bytes;
}

[[nodiscard]] std::uint32_t readBigEndian32(
    std::string_view bytes, std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(
              static_cast<unsigned char>(bytes[offset])) << 24U) |
      (static_cast<std::uint32_t>(
           static_cast<unsigned char>(bytes[offset + 1U])) << 16U) |
      (static_cast<std::uint32_t>(
           static_cast<unsigned char>(bytes[offset + 2U])) << 8U) |
      static_cast<std::uint32_t>(
          static_cast<unsigned char>(bytes[offset + 3U]));
}

struct PngDimensions {
  std::uint32_t width;
  std::uint32_t height;
};

[[nodiscard]] PngDimensions preflightPng(
    std::string_view bytes, const ResourceKey& key) {
  if (bytes.size() < kMinimumPngHeaderBytes ||
      !std::equal(kPngSignature.begin(), kPngSignature.end(),
          bytes.begin(), [](unsigned char expected, char actual) {
            return expected == static_cast<unsigned char>(actual);
          }) ||
      readBigEndian32(bytes, 8U) != kPngIhdrDataBytes ||
      bytes.substr(12U, 4U) != "IHDR") {
    fail(key, VerifiedRasterSurfacePhase::pngHeader);
  }

  const PngDimensions dimensions{
      .width = readBigEndian32(bytes, 16U),
      .height = readBigEndian32(bytes, 20U),
  };
  const auto pixels = static_cast<std::uint64_t>(dimensions.width) *
      static_cast<std::uint64_t>(dimensions.height);
  if (dimensions.width == 0U || dimensions.height == 0U ||
      dimensions.width > kMaximumVerifiedRasterDimension ||
      dimensions.height > kMaximumVerifiedRasterDimension ||
      pixels > kMaximumVerifiedRasterPixels) {
    fail(key, VerifiedRasterSurfacePhase::dimensions);
  }
  return dimensions;
}

} // namespace

std::string_view verifiedRasterSurfacePhaseName(
    VerifiedRasterSurfacePhase phase) noexcept {
  switch (phase) {
    case VerifiedRasterSurfacePhase::open:
      return "open";
    case VerifiedRasterSurfacePhase::encodedSize:
      return "encoded-size";
    case VerifiedRasterSurfacePhase::read:
      return "read";
    case VerifiedRasterSurfacePhase::digest:
      return "digest";
    case VerifiedRasterSurfacePhase::pngHeader:
      return "png-header";
    case VerifiedRasterSurfacePhase::dimensions:
      return "dimensions";
    case VerifiedRasterSurfacePhase::stream:
      return "stream";
    case VerifiedRasterSurfacePhase::decode:
      return "decode";
  }
  return "unknown";
}

VerifiedRasterSurfaceError::VerifiedRasterSurfaceError(
    const ResourceKey& key, VerifiedRasterSurfacePhase phase)
    : std::runtime_error(failureMessage(key, phase)), phase_(phase) {}

VerifiedRasterSurfacePhase VerifiedRasterSurfaceError::phase() const noexcept {
  return this->phase_;
}

sdl_surface_ptr loadVerifiedRasterSurface(
    const std::filesystem::path& path,
    const ResourceKey& key,
    std::string_view expectedSha256) {
  const auto encoded = loadBoundedBytes(path, key);
  if (!isLowercaseSha256(expectedSha256) ||
      assetContentSha256Hex(encoded) != expectedSha256) {
    fail(key, VerifiedRasterSurfacePhase::digest);
  }
  const auto dimensions = preflightPng(encoded, key);

  auto* stream = SDL_IOFromConstMem(encoded.data(), encoded.size());
  if (stream == nullptr) {
    fail(key, VerifiedRasterSurfacePhase::stream);
  }
  sdl_surface_ptr surface(IMG_LoadTyped_IO(stream, true, "PNG"));
  if (!surface) {
    fail(key, VerifiedRasterSurfacePhase::decode);
  }
  if (surface->w <= 0 || surface->h <= 0 || surface->pixels == nullptr ||
      static_cast<std::uint32_t>(surface->w) != dimensions.width ||
      static_cast<std::uint32_t>(surface->h) != dimensions.height) {
    fail(key, VerifiedRasterSurfacePhase::dimensions);
  }
  return surface;
}

} // namespace realmz::remaster::assets
