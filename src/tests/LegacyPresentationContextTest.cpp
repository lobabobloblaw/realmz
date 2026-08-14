#include "presentation/LegacyPresentationContext.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <utility>

struct CGrafPort;
using CGrafPtr = CGrafPort*;
using Boolean = unsigned char;

extern "C" {
CGrafPtr look = nullptr;
CGrafPtr gWindow = nullptr;
short incombat = 0;
Boolean initems = 0;
Boolean inswap = 0;
Boolean inbooty = 0;
Boolean inshop = 0;
Boolean intemple = 0;
char encountflag = 0;
Boolean indung = 0;

CGrafPtr FrontWindow(void) {
  return gWindow;
}
}

namespace {

std::size_t checks = 0;

void expect(bool condition, const char* message) {
  ++checks;
  if (!condition) {
    std::cerr << "Legacy presentation context test failed: " << message << '\n';
    std::exit(1);
  }
}

void expect_context(
    RealmzLegacyPresentationContext actual,
    RealmzLegacyScreenContext screen,
    bool gameplay,
    bool adaptive,
    const char* message) {
  expect(actual.screen == screen, message);
  expect(actual.gameplay_window_active == gameplay, message);
  expect(actual.adaptive_eligible == adaptive, message);
  expect(actual.requires_full_frame == !adaptive, message);
}

void reset_globals() {
  look = nullptr;
  gWindow = nullptr;
  incombat = 0;
  initems = 0;
  inswap = 0;
  inbooty = 0;
  inshop = 0;
  intemple = 0;
  encountflag = 0;
  indung = 0;
}

} // namespace

int main() {
  using realmz::presentation::ScreenContext;
  using realmz::presentation::screen_context_from_legacy;
  using realmz::presentation::screen_context_from_legacy_value;

  // Stale gameplay flags cannot make a title/no-gameplay frame adaptive.
  RealmzLegacyPresentationSignals stale_without_window{
      .gameplay_window_active = 0,
      .in_combat = 1,
      .in_items = 1,
      .in_swap = 1,
      .in_booty = 1,
      .in_shop = 1,
      .in_temple = 1,
      .encounter_flag = 2,
      .in_dungeon = 1,
  };
  auto context = RealmzClassifyLegacyPresentationContext(stale_without_window);
  expect_context(context, REALMZ_LEGACY_SCREEN_TITLE, false, false,
      "no active look window must be title/full-frame");
  expect(screen_context_from_legacy(context) == ScreenContext::title,
      "C++ title mapping");

  const auto active = [](RealmzLegacyPresentationSignals signals) {
    signals.gameplay_window_active = 1;
    signals.front_is_gameplay_surface = 1;
    return RealmzClassifyLegacyPresentationContext(signals);
  };

  expect_context(active({}), REALMZ_LEGACY_SCREEN_EXPLORATION, true, true,
      "active outdoor exploration");
  expect_context(active({.in_dungeon = 1}), REALMZ_LEGACY_SCREEN_DUNGEON,
      true, true, "active dungeon");
  expect_context(active({.in_combat = 1}), REALMZ_LEGACY_SCREEN_COMBAT,
      true, true, "active combat");

  expect_context(active({.in_items = 1}), REALMZ_LEGACY_SCREEN_INVENTORY,
      true, false, "items require full frame");
  expect_context(active({.in_swap = 1}), REALMZ_LEGACY_SCREEN_INVENTORY,
      true, false, "swap requires full frame");
  expect_context(active({.in_booty = 1}), REALMZ_LEGACY_SCREEN_INVENTORY,
      true, false, "booty requires full frame");
  expect_context(active({.in_shop = 1}), REALMZ_LEGACY_SCREEN_SHOP,
      true, false, "shop requires full frame");
  expect_context(active({.in_temple = 1}), REALMZ_LEGACY_SCREEN_SHOP,
      true, false, "temple requires full frame");
  expect_context(active({.encounter_flag = 2}), REALMZ_LEGACY_SCREEN_ENCOUNTER,
      true, false, "encounter requires full frame");

  // A nested legacy flow wins over the gameplay state behind it.
  expect_context(active({.in_combat = 1, .in_items = 1}),
      REALMZ_LEGACY_SCREEN_INVENTORY, true, false,
      "inventory must override combat");
  expect_context(active({.in_combat = 1, .in_shop = 1}),
      REALMZ_LEGACY_SCREEN_SHOP, true, false,
      "shop must override combat");
  expect_context(active({.in_combat = 1, .encounter_flag = 1}),
      REALMZ_LEGACY_SCREEN_ENCOUNTER, true, false,
      "encounter must override combat");

  expect_context(RealmzClassifyLegacyPresentationContext({
      .gameplay_window_active = 1,
      .front_is_gameplay_surface = 0,
      .in_combat = 1,
  }), REALMZ_LEGACY_SCREEN_COMBAT, true, false,
      "non-gameplay front window must override adaptive combat");

  const std::array mapping_cases{
      std::pair{REALMZ_LEGACY_SCREEN_EXPLORATION, ScreenContext::exploration},
      std::pair{REALMZ_LEGACY_SCREEN_DUNGEON, ScreenContext::dungeon},
      std::pair{REALMZ_LEGACY_SCREEN_COMBAT, ScreenContext::combat},
      std::pair{REALMZ_LEGACY_SCREEN_INVENTORY, ScreenContext::inventory},
      std::pair{REALMZ_LEGACY_SCREEN_SHOP, ScreenContext::shop},
      std::pair{REALMZ_LEGACY_SCREEN_ENCOUNTER, ScreenContext::encounter},
  };
  for (const auto& [legacy, expected] : mapping_cases) {
    expect(screen_context_from_legacy(legacy) == expected,
        "C++ ScreenContext mapping");
  }
  expect(screen_context_from_legacy_value(255) ==
          ScreenContext::title,
      "unknown legacy screen maps safely to title");

  // Exercise the actual narrow-global capture boundary.
  reset_globals();
  expect_context(RealmzCaptureLegacyPresentationContext(),
      REALMZ_LEGACY_SCREEN_TITLE, false, false, "captured title");

  look = reinterpret_cast<CGrafPtr>(static_cast<uintptr_t>(1));
  gWindow = look;
  indung = 1;
  expect_context(RealmzCaptureLegacyPresentationContext(),
      REALMZ_LEGACY_SCREEN_DUNGEON, true, true, "captured dungeon");

  incombat = 1;
  initems = 1;
  expect_context(RealmzCaptureLegacyPresentationContext(),
      REALMZ_LEGACY_SCREEN_INVENTORY, true, false,
      "captured nested inventory");

  std::cout << "Legacy presentation context checks passed: " << checks << '\n';
  return 0;
}
