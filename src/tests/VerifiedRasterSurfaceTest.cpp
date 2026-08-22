#include "remaster/assets/VerifiedRasterSurface.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "remaster/assets/AssetManifest.hpp"

namespace {

namespace fs = std::filesystem;

using realmz::remaster::assets::ResourceKey;
using realmz::remaster::assets::VerifiedRasterSurfaceError;
using realmz::remaster::assets::VerifiedRasterSurfacePhase;
using realmz::remaster::assets::assetContentSha256Hex;
using realmz::remaster::assets::kMaximumVerifiedRasterDimension;
using realmz::remaster::assets::kMaximumVerifiedRasterEncodedBytes;
using realmz::remaster::assets::loadVerifiedRasterSurface;
using realmz::remaster::assets::verifiedRasterSurfacePhaseName;

constexpr std::string_view kPortrait257Sha256 =
    "8eae998f6e9d745911ed518f7ad2629abd95a72d5c66c5ba0e9655663e0f2cc1";
const ResourceKey kPortrait257{"Data Files/Portraits", "cicn", 257};

struct ApprovedRasterFixture {
  ResourceKey key;
  const char* filename;
  const char* sha256;
  int physicalWidth;
  int physicalHeight;
  std::uint32_t logicalWidth;
  std::uint32_t logicalHeight;
};

const std::array<ApprovedRasterFixture, 7> kGenericApprovedRasters{{
    {kPortrait257, "05_portrait_cicn_257.png",
        "8eae998f6e9d745911ed518f7ad2629abd95a72d5c66c5ba0e9655663e0f2cc1",
        352, 352, 44U, 44U},
    {{"Data Files/Portraits", "cicn", 267},
        "06_portrait_cicn_267.png",
        "5786f9cbf6901739f34acaa72f603cdc62a53423d94b31956fbe90f1af9be281",
        352, 352, 44U, 44U},
    {{"Data Files/Portraits", "cicn", 297},
        "07_portrait_cicn_297.png",
        "c5bc0a5c315cfc115d6d1a806f9396670e59734a84102dc492d547cced78aca0",
        352, 352, 44U, 44U},
    {{"Data Files/Portraits", "cicn", 337},
        "08_portrait_cicn_337.png",
        "3bc7ac4e0d6bb17cab9562f0f2f4db178c9eddc584f658e524ab947b59a3ec5e",
        352, 352, 44U, 44U},
    {{"Data Files/The Family Jewels", "PICT", 50},
        "17_world_dungeon_PICT_50.png",
        "86e0607e8092710896fc07a1fab3c034cbd2c10340de7bec09eee0aecfd2d899",
        1024, 768, 256U, 192U},
    {{"Data Files/The Family Jewels", "cicn", -167},
        "20_world_dungeon_cicn_m167.png",
        "44ba9ea12d1afee69d5ed23ff2032cd0d553b3cfd04aad8300076bb140d07578",
        256, 256, 32U, 32U},
    {{"Scenarios/Tutorial/Scenario", "PICT", 32128},
        "21_tutorial_city_PICT_32128.png",
        "b3ad37374ae9b92a0bf745627c4db0d5022b53e16fa9d106bc4b5b4ecc032bba",
        1280, 1280, 320U, 320U},
}};

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string utf8PathText(const fs::path& path) {
  const auto encoded = path.generic_u8string();
  return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

void saveFile(const fs::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot create test file");
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("cannot write test file");
  }
}

void saveSparseOversizedFile(const fs::path& path) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot create oversized test file");
  }
  output.seekp(static_cast<std::streamoff>(
      kMaximumVerifiedRasterEncodedBytes));
  output.put('\0');
  if (!output) {
    throw std::runtime_error("cannot write oversized test file");
  }
}

void writeBigEndian32(
    std::string& bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<char>((value >> 24U) & 0xFFU);
  bytes[offset + 1U] = static_cast<char>((value >> 16U) & 0xFFU);
  bytes[offset + 2U] = static_cast<char>((value >> 8U) & 0xFFU);
  bytes[offset + 3U] = static_cast<char>(value & 0xFFU);
}

std::string pngIhdrOnly(std::uint32_t width, std::uint32_t height) {
  std::string bytes(33U, '\0');
  constexpr unsigned char signature[] = {
      0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
  for (std::size_t index = 0; index < sizeof(signature); ++index) {
    bytes[index] = static_cast<char>(signature[index]);
  }
  writeBigEndian32(bytes, 8U, 13U);
  bytes.replace(12U, 4U, "IHDR");
  writeBigEndian32(bytes, 16U, width);
  writeBigEndian32(bytes, 20U, height);
  bytes[24] = 8;
  bytes[25] = 6;
  return bytes;
}

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = fs::temp_directory_path() /
        ("realmz-verified-raster-test-" + unique);
    if (!fs::create_directories(path_)) {
      throw std::runtime_error("cannot create temporary test directory");
    }
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  [[nodiscard]] const fs::path& path() const noexcept {
    return path_;
  }

private:
  fs::path path_;
};

template <typename Callback>
void requireFailure(
    Callback&& callback,
    VerifiedRasterSurfacePhase expectedPhase,
    const fs::path& forbiddenPath) {
  try {
    callback();
  } catch (const VerifiedRasterSurfaceError& error) {
    require(error.phase() == expectedPhase,
        "verified-raster failure used the wrong phase");
    const std::string message(error.what());
    require(message.find(kPortrait257.toString()) != std::string::npos,
        "verified-raster failure omitted its stable resource key");
    require(message.find(verifiedRasterSurfacePhaseName(expectedPhase)) !=
            std::string::npos,
        "verified-raster failure omitted its stable phase");
    require(message.find(utf8PathText(forbiddenPath)) == std::string::npos,
        "verified-raster failure disclosed its host path");
    require(message.find("SDL") == std::string::npos,
        "verified-raster failure disclosed an SDL diagnostic");
    return;
  }
  throw std::runtime_error("verified-raster operation unexpectedly succeeded");
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error(
          "usage: VerifiedRasterSurfaceTest <repository-root>");
    }
    const auto repositoryRoot = fs::weakly_canonical(fs::path(argv[1]));
    const auto publicOutputs = repositoryRoot /
        "assets/remastered/style-proof/generation/outputs";
    const auto portrait257 = publicOutputs / "05_portrait_cicn_257.png";
    const auto portrait267 = publicOutputs / "06_portrait_cicn_267.png";
    require(fs::is_regular_file(portrait257) &&
            fs::is_regular_file(portrait267),
        "approved public portrait fixtures are missing");

    for (const auto& fixture : kGenericApprovedRasters) {
      auto surface = loadVerifiedRasterSurface(
          publicOutputs / fixture.filename, fixture.key, fixture.sha256);
      require(surface != nullptr &&
              surface->w == fixture.physicalWidth &&
              surface->h == fixture.physicalHeight,
          "approved generic raster changed physical dimensions");
      const auto horizontalScale = surface->w /
          static_cast<double>(fixture.logicalWidth);
      const auto verticalScale = surface->h /
          static_cast<double>(fixture.logicalHeight);
      require(horizontalScale > 0.0 && horizontalScale == verticalScale,
          "approved generic raster lost its uniform logical scale");
    }

    TemporaryDirectory temporary;
    const auto unicodeRoot = temporary.path() /
        fs::path(std::u8string(u8"public-\u00e9-\u754c"));
    fs::create_directories(unicodeRoot);
    const auto candidate = unicodeRoot / "approved.png";
    fs::copy_file(portrait257, candidate);

    {
      auto surface = loadVerifiedRasterSurface(
          candidate, kPortrait257, kPortrait257Sha256);
      require(surface != nullptr, "approved public PNG did not decode");
      require(surface->w == 352 && surface->h == 352,
          "loader did not preserve approved PNG physical dimensions");
    }

    fs::copy_file(portrait267, candidate,
        fs::copy_options::overwrite_existing);
    requireFailure(
        [&] {
          (void)loadVerifiedRasterSurface(
              candidate, kPortrait257, kPortrait257Sha256);
        },
        VerifiedRasterSurfacePhase::digest, unicodeRoot);

    require(fs::remove(candidate),
        "could not remove substituted public PNG fixture");
    requireFailure(
        [&] {
          (void)loadVerifiedRasterSurface(
              candidate, kPortrait257, kPortrait257Sha256);
        },
        VerifiedRasterSurfacePhase::open, unicodeRoot);

    const std::string truncated("\x89PNG\r\n\x1a\n", 8U);
    saveFile(candidate, truncated);
    requireFailure(
        [&] {
          (void)loadVerifiedRasterSurface(candidate, kPortrait257,
              assetContentSha256Hex(truncated));
        },
        VerifiedRasterSurfacePhase::pngHeader, unicodeRoot);

    const auto malformed = pngIhdrOnly(1U, 1U);
    saveFile(candidate, malformed);
    requireFailure(
        [&] {
          (void)loadVerifiedRasterSurface(candidate, kPortrait257,
              assetContentSha256Hex(malformed));
        },
        VerifiedRasterSurfacePhase::decode, unicodeRoot);

    saveSparseOversizedFile(candidate);
    requireFailure(
        [&] {
          (void)loadVerifiedRasterSurface(
              candidate, kPortrait257, kPortrait257Sha256);
        },
        VerifiedRasterSurfacePhase::encodedSize, unicodeRoot);

    const auto excessiveDimension = pngIhdrOnly(
        kMaximumVerifiedRasterDimension + 1U, 1U);
    saveFile(candidate, excessiveDimension);
    requireFailure(
        [&] {
          (void)loadVerifiedRasterSurface(candidate, kPortrait257,
              assetContentSha256Hex(excessiveDimension));
        },
        VerifiedRasterSurfacePhase::dimensions, unicodeRoot);

    const auto excessivePixels = pngIhdrOnly(
        kMaximumVerifiedRasterDimension,
        kMaximumVerifiedRasterDimension);
    saveFile(candidate, excessivePixels);
    requireFailure(
        [&] {
          (void)loadVerifiedRasterSurface(candidate, kPortrait257,
              assetContentSha256Hex(excessivePixels));
        },
        VerifiedRasterSurfacePhase::dimensions, unicodeRoot);

    std::cout <<
        "VerifiedRasterSurfaceTest passed: exact bytes, bounded PNG decode, Unicode path\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "VerifiedRasterSurfaceTest failed: " << error.what()
              << '\n';
    return 1;
  }
}
