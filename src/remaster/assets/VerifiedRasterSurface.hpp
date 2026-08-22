#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string_view>

#include "SDLHelpers.hpp"
#include "remaster/assets/ResourceKey.hpp"

namespace realmz::remaster::assets {

inline constexpr std::size_t kMaximumVerifiedRasterEncodedBytes =
    16U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumVerifiedRasterDimension = 8192U;
inline constexpr std::uint64_t kMaximumVerifiedRasterPixels =
    16U * 1024U * 1024U;

enum class VerifiedRasterSurfacePhase {
  open,
  encodedSize,
  read,
  digest,
  pngHeader,
  dimensions,
  stream,
  decode,
};

[[nodiscard]] std::string_view verifiedRasterSurfacePhaseName(
    VerifiedRasterSurfacePhase phase) noexcept;

// Runtime failures deliberately expose only a stable ResourceKey and a fixed
// phase. Host paths and decoder-specific diagnostics must not cross this
// boundary.
class VerifiedRasterSurfaceError : public std::runtime_error {
public:
  VerifiedRasterSurfaceError(
      const ResourceKey& key, VerifiedRasterSurfacePhase phase);

  [[nodiscard]] VerifiedRasterSurfacePhase phase() const noexcept;

private:
  VerifiedRasterSurfacePhase phase_;
};

// Reads a bounded PNG through its native filesystem path, rechecks the digest
// of those exact bytes, and decodes that same in-memory buffer. The returned
// surface retains the PNG's physical dimensions; scaling remains a consumer
// responsibility.
[[nodiscard]] sdl_surface_ptr loadVerifiedRasterSurface(
    const std::filesystem::path& path,
    const ResourceKey& key,
    std::string_view expectedSha256);

} // namespace realmz::remaster::assets
