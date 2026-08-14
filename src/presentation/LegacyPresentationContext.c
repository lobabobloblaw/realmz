#include "LegacyPresentationContext.h"

#include <stddef.h>

// Keep this C boundary independent of the legacy headers: QuickDraw.h contains
// historical flexible-array/prototype constructs that are intentionally not
// valid under the strict C99 core-test flags.
typedef struct CGrafPort* CGrafPtr;
typedef unsigned char Boolean;

// This is intentionally a narrow declaration list instead of variables.h.
// The adapter is the only presentation-boundary C file that reads these
// mutable globals, and it returns values only.
extern CGrafPtr look;
extern CGrafPtr gWindow;
extern CGrafPtr FrontWindow(void);
extern short incombat;
extern Boolean initems;
extern Boolean inswap;
extern Boolean inbooty;
extern Boolean inshop;
extern Boolean intemple;
extern char encountflag;
extern Boolean indung;

static RealmzLegacyPresentationContext make_context(
    RealmzLegacyScreenContext screen,
    uint8_t gameplay_window_active,
    uint8_t adaptive_eligible) {
  RealmzLegacyPresentationContext result;
  result.screen = screen;
  result.gameplay_window_active = gameplay_window_active ? 1 : 0;
  result.adaptive_eligible = adaptive_eligible ? 1 : 0;
  result.requires_full_frame = result.adaptive_eligible ? 0 : 1;
  return result;
}

RealmzLegacyPresentationContext RealmzClassifyLegacyPresentationContext(
    RealmzLegacyPresentationSignals signals) {
  if (!signals.gameplay_window_active) {
    return make_context(REALMZ_LEGACY_SCREEN_TITLE, 0, 0);
  }

  // These flags represent nested legacy screens and therefore take precedence
  // over the world/combat state beneath them. They must retain the complete
  // Classic framebuffer until those flows have semantic UI implementations.
  if (signals.in_items || signals.in_swap || signals.in_booty) {
    return make_context(REALMZ_LEGACY_SCREEN_INVENTORY, 1, 0);
  }
  if (signals.in_shop || signals.in_temple) {
    return make_context(REALMZ_LEGACY_SCREEN_SHOP, 1, 0);
  }
  if (signals.encounter_flag) {
    return make_context(REALMZ_LEGACY_SCREEN_ENCOUNTER, 1, 0);
  }
  // State flags describe the screen underneath nested legacy windows. If a
  // dialog, picker, spell list, map, or other overlay is at the front, retain
  // the complete framebuffer even though combat/world state remains active.
  if (!signals.front_is_gameplay_surface) {
    if (signals.in_combat) {
      return make_context(REALMZ_LEGACY_SCREEN_COMBAT, 1, 0);
    }
    if (signals.in_dungeon) {
      return make_context(REALMZ_LEGACY_SCREEN_DUNGEON, 1, 0);
    }
    return make_context(REALMZ_LEGACY_SCREEN_EXPLORATION, 1, 0);
  }
  if (signals.in_combat) {
    return make_context(REALMZ_LEGACY_SCREEN_COMBAT, 1, 1);
  }
  if (signals.in_dungeon) {
    return make_context(REALMZ_LEGACY_SCREEN_DUNGEON, 1, 1);
  }
  return make_context(REALMZ_LEGACY_SCREEN_EXPLORATION, 1, 1);
}

RealmzLegacyPresentationContext RealmzCaptureLegacyPresentationContext(void) {
  RealmzLegacyPresentationSignals signals = {0};
  CGrafPtr front = FrontWindow();
  signals.gameplay_window_active = (look != NULL);
  signals.front_is_gameplay_surface =
      (front != NULL) && ((front == look) || (front == gWindow));
  signals.in_combat = incombat;
  signals.in_items = initems;
  signals.in_swap = inswap;
  signals.in_booty = inbooty;
  signals.in_shop = inshop;
  signals.in_temple = intemple;
  signals.encounter_flag = encountflag;
  signals.in_dungeon = indung;
  return RealmzClassifyLegacyPresentationContext(signals);
}
