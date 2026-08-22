#include "remaster/assets/TutorialTitleCompositor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "remaster/assets/VerifiedRasterSurface.hpp"

using realmz::remaster::assets::AssetDimensions;
using realmz::remaster::assets::ResourceKey;
using realmz::remaster::assets::composeTutorialTitle;
using realmz::remaster::assets::kTutorialTitleApprovedOutputSha256;
using realmz::remaster::assets::kTutorialTitleLogicalDimensions;
using realmz::remaster::assets::kTutorialTitlePlaqueTextRect;
using realmz::remaster::assets::tutorialTitleEligible;
using realmz::remaster::assets::loadVerifiedRasterSurface;

namespace {

namespace fs = std::filesystem;
using ttf_font_ptr = std::unique_ptr<TTF_Font, deleter_from_fn<TTF_CloseFont>>;

constexpr ResourceKey tutorialKey() {
  return {
      .pack = "Scenarios/Tutorial/Scenario",
      .type = "PICT",
      .id = 32128,
  };
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class HeadlessSdlTtfSession {
public:
  HeadlessSdlTtfSession() {
    require(SDL_SetHintWithPriority(
                SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE),
        "could not select SDL's headless video driver");
    require(SDL_Init(SDL_INIT_VIDEO),
        std::string("could not initialize headless SDL: ") + SDL_GetError());
    sdlInitialized_ = true;
    if (!TTF_Init()) {
      throw std::runtime_error(
          std::string("could not initialize SDL_ttf: ") + SDL_GetError());
    }
    ttfInitialized_ = true;
  }

  ~HeadlessSdlTtfSession() {
    if (ttfInitialized_) {
      TTF_Quit();
    }
    if (sdlInitialized_) {
      SDL_Quit();
    }
  }

  HeadlessSdlTtfSession(const HeadlessSdlTtfSession&) = delete;
  HeadlessSdlTtfSession& operator=(const HeadlessSdlTtfSession&) = delete;

private:
  bool sdlInitialized_ = false;
  bool ttfInitialized_ = false;
};

struct PixelSnapshot {
  int width = 0;
  int height = 0;
  SDL_PixelFormat format = SDL_PIXELFORMAT_UNKNOWN;
  std::vector<std::byte> pixels;

  bool operator==(const PixelSnapshot&) const = default;
};

PixelSnapshot snapshot(SDL_Surface* surface) {
  require(surface != nullptr, "cannot snapshot a null surface");
  require(surface->format == SDL_PIXELFORMAT_ARGB8888,
      "test surface must use ARGB8888");
  require(SDL_LockSurface(surface),
      std::string("could not lock test surface: ") + SDL_GetError());

  PixelSnapshot result{
      .width = surface->w,
      .height = surface->h,
      .format = surface->format,
      .pixels = std::vector<std::byte>(
          static_cast<std::size_t>(surface->w) *
          static_cast<std::size_t>(surface->h) * sizeof(std::uint32_t)),
  };
  const auto* source = static_cast<const std::byte*>(surface->pixels);
  for (int y = 0; y < surface->h; ++y) {
    std::copy_n(
        source + static_cast<std::ptrdiff_t>(y) * surface->pitch,
        static_cast<std::size_t>(surface->w) * sizeof(std::uint32_t),
        result.pixels.begin() +
            static_cast<std::ptrdiff_t>(y) * surface->w *
                static_cast<std::ptrdiff_t>(sizeof(std::uint32_t)));
  }
  SDL_UnlockSurface(surface);
  return result;
}

std::uint32_t pixelAt(const PixelSnapshot& image, int x, int y) {
  const auto offset =
      (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
          static_cast<std::size_t>(x)) *
      sizeof(std::uint32_t);
  std::uint32_t value = 0;
  SDL_memcpy(&value, image.pixels.data() + offset, sizeof(value));
  return value;
}

bool insidePlaqueTextRect(int x, int y) {
  return x >= kTutorialTitlePlaqueTextRect.x &&
      x < kTutorialTitlePlaqueTextRect.x + kTutorialTitlePlaqueTextRect.w &&
      y >= kTutorialTitlePlaqueTextRect.y &&
      y < kTutorialTitlePlaqueTextRect.y + kTutorialTitlePlaqueTextRect.h;
}

sdl_surface_ptr loadApprovedTutorialSurface(const fs::path& repositoryRoot) {
  const auto imagePath = repositoryRoot / "assets" / "remastered" /
      "style-proof" / "generation" / "outputs" /
      "21_tutorial_city_PICT_32128.png";
  auto loaded = loadVerifiedRasterSurface(
      imagePath, tutorialKey(), kTutorialTitleApprovedOutputSha256);
  require(loaded->w == 1280 && loaded->h == 1280,
      "approved Tutorial output must retain its reviewed 1280x1280 bytes");

  sdl_surface_ptr scaled{SDL_ScaleSurface(
      loaded.get(),
      static_cast<int>(kTutorialTitleLogicalDimensions.width),
      static_cast<int>(kTutorialTitleLogicalDimensions.height),
      SDL_SCALEMODE_LINEAR)};
  require(scaled != nullptr,
      std::string("could not scale approved Tutorial image: ") + SDL_GetError());

  sdl_surface_ptr converted{
      SDL_ConvertSurface(scaled.get(), SDL_PIXELFORMAT_ARGB8888)};
  require(converted != nullptr,
      std::string("could not convert approved Tutorial image: ") + SDL_GetError());
  return converted;
}

ttf_font_ptr openBlackChancery(
    const fs::path& repositoryRoot, float pointSize) {
  const auto fontPath = repositoryRoot / "resources" / "Black Chancery.ttf";
  const auto encodedPath = fontPath.u8string();
  const std::string utf8Path(
      reinterpret_cast<const char*>(encodedPath.data()), encodedPath.size());
  ttf_font_ptr font{TTF_OpenFont(utf8Path.c_str(), pointSize)};
  require(font != nullptr,
      std::string("could not load bundled Black Chancery: ") + SDL_GetError());
  return font;
}

void requireRejectedWithoutMutation(
    const ResourceKey& key,
    std::string_view digest,
    AssetDimensions dimensions,
    SDL_Surface* source,
    TTF_Font* font,
    std::string_view message) {
  const auto before = snapshot(source);
  require(!composeTutorialTitle(key, digest, dimensions, source, font), message);
  require(snapshot(source) == before,
      std::string(message) + " (source pixels changed)");
}

void testExactEligibility() {
  const auto key = tutorialKey();
  require(tutorialTitleEligible(key, kTutorialTitleApprovedOutputSha256,
              kTutorialTitleLogicalDimensions),
      "exact Tutorial identity must be eligible");

  auto wrongPack = key;
  wrongPack.pack = "Scenarios/Tutorial/Scenario ";
  require(!tutorialTitleEligible(wrongPack,
              kTutorialTitleApprovedOutputSha256,
              kTutorialTitleLogicalDimensions),
      "pack match must be exact");

  auto wrongType = key;
  wrongType.type = "pict";
  require(!tutorialTitleEligible(wrongType,
              kTutorialTitleApprovedOutputSha256,
              kTutorialTitleLogicalDimensions),
      "type match must be exact");

  auto wrongId = key;
  wrongId.id = 32127;
  require(!tutorialTitleEligible(wrongId,
              kTutorialTitleApprovedOutputSha256,
              kTutorialTitleLogicalDimensions),
      "resource id match must be exact");

  require(!tutorialTitleEligible(key,
              "B3AD37374AE9B92A0BF745627C4DB0D5022B53E16FA9D106BC4B5B4ECC032BBA",
              kTutorialTitleLogicalDimensions),
      "approved output digest match must be byte-for-byte exact");
  require(!tutorialTitleEligible(key, kTutorialTitleApprovedOutputSha256,
              AssetDimensions{319U, 320U}),
      "logical width match must be exact");
  require(!tutorialTitleEligible(key, kTutorialTitleApprovedOutputSha256,
              AssetDimensions{320U, 319U}),
      "logical height match must be exact");
}

void testCompositionAndIsolation(
    SDL_Surface* source, TTF_Font* font) {
  constexpr std::string_view title = "TUTORIAL";
  int renderedWidth = 0;
  int renderedHeight = 0;
  require(TTF_GetStringSize(font, title.data(), title.size(),
              &renderedWidth, &renderedHeight),
      std::string("could not measure Tutorial title: ") + SDL_GetError());
  const SDL_Rect centeredRenderBounds{
      kTutorialTitlePlaqueTextRect.x +
          (kTutorialTitlePlaqueTextRect.w - renderedWidth) / 2,
      kTutorialTitlePlaqueTextRect.y +
          (kTutorialTitlePlaqueTextRect.h - renderedHeight) / 2,
      renderedWidth,
      renderedHeight,
  };

  const auto sourceBefore = snapshot(source);
  auto first = composeTutorialTitle(tutorialKey(),
      kTutorialTitleApprovedOutputSha256,
      kTutorialTitleLogicalDimensions, source, font);
  require(first != nullptr, "eligible Tutorial title composition failed");
  require(first.get() != source, "composition must return a new surface");
  require(snapshot(source) == sourceBefore,
      "successful composition mutated its source surface");

  const auto composed = snapshot(first.get());
  std::size_t changedPixels = 0;
  bool foundReadableDarkInk = false;
  for (int y = 0; y < composed.height; ++y) {
    for (int x = 0; x < composed.width; ++x) {
      const auto sourcePixel = pixelAt(sourceBefore, x, y);
      const auto composedPixel = pixelAt(composed, x, y);
      const bool changed = sourcePixel != composedPixel;
      if (changed) {
        ++changedPixels;
        require(insidePlaqueTextRect(x, y),
            "composition changed a pixel outside the reviewed plaque text rect");
        require(x >= centeredRenderBounds.x &&
                x < centeredRenderBounds.x + centeredRenderBounds.w &&
                y >= centeredRenderBounds.y &&
                y < centeredRenderBounds.y + centeredRenderBounds.h,
            "title pixels escaped the deterministically centered render bounds");

        const auto red = (composedPixel >> 16U) & 0xFFU;
        const auto green = (composedPixel >> 8U) & 0xFFU;
        const auto blue = composedPixel & 0xFFU;
        foundReadableDarkInk = foundReadableDarkInk ||
            (red < 64U && green < 64U && blue < 64U);
      } else if (!insidePlaqueTextRect(x, y)) {
        require(sourcePixel == composedPixel,
            "outside pixel was not byte-identical");
      }
    }
  }
  require(changedPixels > 0, "composition did not render any title pixels");
  require(foundReadableDarkInk,
      "composition did not produce readable dark title ink");

  auto second = composeTutorialTitle(tutorialKey(),
      kTutorialTitleApprovedOutputSha256,
      kTutorialTitleLogicalDimensions, source, font);
  require(second != nullptr, "second eligible composition failed");
  require(snapshot(second.get()) == composed,
      "repeated composition was not pixel-deterministic");
}

void testRejectedInputs(
    SDL_Surface* source,
    TTF_Font* font,
    TTF_Font* oversizedFont) {
  const auto key = tutorialKey();

  auto wrongPack = key;
  wrongPack.pack = "Scenarios/Tutorial";
  requireRejectedWithoutMutation(wrongPack,
      kTutorialTitleApprovedOutputSha256,
      kTutorialTitleLogicalDimensions, source, font,
      "wrong key must fail closed");
  requireRejectedWithoutMutation(key,
      "03ad37374ae9b92a0bf745627c4db0d5022b53e16fa9d106bc4b5b4ecc032bba",
      kTutorialTitleLogicalDimensions, source, font,
      "wrong digest must fail closed");
  requireRejectedWithoutMutation(key,
      kTutorialTitleApprovedOutputSha256,
      AssetDimensions{320U, 321U}, source, font,
      "wrong logical dimensions must fail closed");
  requireRejectedWithoutMutation(key,
      kTutorialTitleApprovedOutputSha256,
      kTutorialTitleLogicalDimensions, source, nullptr,
      "null font must fail closed");
  requireRejectedWithoutMutation(key,
      kTutorialTitleApprovedOutputSha256,
      kTutorialTitleLogicalDimensions, source, oversizedFont,
      "font whose rendered title cannot fit must fail closed");

  sdl_surface_ptr wrongSurface{
      SDL_CreateSurface(319, 320, SDL_PIXELFORMAT_ARGB8888)};
  require(wrongSurface != nullptr, "could not create wrong-size test surface");
  requireRejectedWithoutMutation(key,
      kTutorialTitleApprovedOutputSha256,
      kTutorialTitleLogicalDimensions, wrongSurface.get(), font,
      "decoded surface dimensions must also match exactly");

  require(!composeTutorialTitle(key, kTutorialTitleApprovedOutputSha256,
              kTutorialTitleLogicalDimensions, nullptr, font),
      "null source must fail closed");
}

} // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 2 || argc == 3,
        "usage: TutorialTitleCompositorTest <repository-root> [preview.bmp]");
    HeadlessSdlTtfSession session;
    const fs::path repositoryRoot = fs::canonical(argv[1]);
    auto source = loadApprovedTutorialSurface(repositoryRoot);
    auto font = openBlackChancery(repositoryRoot, 32.0F);
    TTF_SetFontStyle(font.get(), TTF_STYLE_BOLD);
    auto oversizedFont = openBlackChancery(repositoryRoot, 128.0F);
    TTF_SetFontStyle(oversizedFont.get(), TTF_STYLE_BOLD);

    require(kTutorialTitlePlaqueTextRect.x >= 0 &&
            kTutorialTitlePlaqueTextRect.y >= 0 &&
            kTutorialTitlePlaqueTextRect.x + kTutorialTitlePlaqueTextRect.w <=
                static_cast<int>(kTutorialTitleLogicalDimensions.width) &&
            kTutorialTitlePlaqueTextRect.y + kTutorialTitlePlaqueTextRect.h <=
                static_cast<int>(kTutorialTitleLogicalDimensions.height),
        "reviewed plaque text rect must be inside the logical surface");

    testExactEligibility();
    testCompositionAndIsolation(source.get(), font.get());
    testRejectedInputs(source.get(), font.get(), oversizedFont.get());
    if (argc == 3) {
      auto preview = composeTutorialTitle(tutorialKey(),
          kTutorialTitleApprovedOutputSha256,
          kTutorialTitleLogicalDimensions, source.get(), font.get());
      require(preview != nullptr && SDL_SaveBMP(preview.get(), argv[2]),
          "could not write optional Tutorial title preview");
    }

    std::cout << "TutorialTitleCompositorTest passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "TutorialTitleCompositorTest failed: " << error.what()
              << '\n';
    return 1;
  }
}
