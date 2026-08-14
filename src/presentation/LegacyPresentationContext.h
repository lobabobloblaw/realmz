#ifndef REALMZ_PRESENTATION_LEGACY_PRESENTATION_CONTEXT_H
#define REALMZ_PRESENTATION_LEGACY_PRESENTATION_CONTEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A narrow, value-only view of the legacy globals used to decide whether the
// adaptive shell can safely replace the full 800x600 framebuffer.  Keeping the
// signal set here means C++ presentation code never needs to include the very
// large realmz_orig/variables.h header.
typedef struct RealmzLegacyPresentationSignals {
  uint8_t gameplay_window_active;
  uint8_t front_is_gameplay_surface;
  int16_t in_combat;
  uint8_t in_items;
  uint8_t in_swap;
  uint8_t in_booty;
  uint8_t in_shop;
  uint8_t in_temple;
  int8_t encounter_flag;
  uint8_t in_dungeon;
} RealmzLegacyPresentationSignals;

typedef enum RealmzLegacyScreenContext {
  REALMZ_LEGACY_SCREEN_TITLE = 0,
  REALMZ_LEGACY_SCREEN_EXPLORATION = 1,
  REALMZ_LEGACY_SCREEN_DUNGEON = 2,
  REALMZ_LEGACY_SCREEN_COMBAT = 3,
  REALMZ_LEGACY_SCREEN_INVENTORY = 4,
  REALMZ_LEGACY_SCREEN_SHOP = 5,
  REALMZ_LEGACY_SCREEN_ENCOUNTER = 6,
} RealmzLegacyScreenContext;

typedef struct RealmzLegacyPresentationContext {
  RealmzLegacyScreenContext screen;
  uint8_t gameplay_window_active;
  uint8_t adaptive_eligible;
  uint8_t requires_full_frame;
} RealmzLegacyPresentationContext;

// Pure classifier used by both the runtime adapter and dependency-free tests.
RealmzLegacyPresentationContext RealmzClassifyLegacyPresentationContext(
    RealmzLegacyPresentationSignals signals);

// Captures only the legacy globals declared by the implementation. No legacy
// pointers escape this boundary.
RealmzLegacyPresentationContext RealmzCaptureLegacyPresentationContext(void);

#ifdef __cplusplus
} // extern "C"

#include "GameSnapshot.hpp"

namespace realmz::presentation {

[[nodiscard]] inline constexpr ScreenContext screen_context_from_legacy_value(
    uint32_t screen) noexcept {
  switch (screen) {
    case REALMZ_LEGACY_SCREEN_EXPLORATION:
      return ScreenContext::exploration;
    case REALMZ_LEGACY_SCREEN_DUNGEON:
      return ScreenContext::dungeon;
    case REALMZ_LEGACY_SCREEN_COMBAT:
      return ScreenContext::combat;
    case REALMZ_LEGACY_SCREEN_INVENTORY:
      return ScreenContext::inventory;
    case REALMZ_LEGACY_SCREEN_SHOP:
      return ScreenContext::shop;
    case REALMZ_LEGACY_SCREEN_ENCOUNTER:
      return ScreenContext::encounter;
    case REALMZ_LEGACY_SCREEN_TITLE:
    default:
      return ScreenContext::title;
  }
}

[[nodiscard]] inline constexpr ScreenContext screen_context_from_legacy(
    RealmzLegacyScreenContext screen) noexcept {
  return screen_context_from_legacy_value(static_cast<uint32_t>(screen));
}

[[nodiscard]] inline constexpr ScreenContext screen_context_from_legacy(
    const RealmzLegacyPresentationContext& context) noexcept {
  return screen_context_from_legacy(context.screen);
}

} // namespace realmz::presentation
#endif

#endif
