#include "WindowAspect.h"

#import <Cocoa/Cocoa.h>
#include <cstdint>

struct SDL_Window;
using RealmzSDLPropertiesID = std::uint32_t;
extern "C" RealmzSDLPropertiesID SDL_GetWindowProperties(
    struct SDL_Window* window);
extern "C" void* SDL_GetPointerProperty(
    RealmzSDLPropertiesID properties,
    const char* name,
    void* default_value);
static constexpr const char* kCocoaWindowProperty =
    "SDL.window.cocoa.window";

// Keep public SDL calls above SDL_sysvideo.h: SDL's internal dynapi macros
// rewrite them to private *_REAL symbols that are not exported by the shared
// library. This helper is intentionally isolated from that private header.
static NSWindow* NativeWindowForSDLWindow(struct SDL_Window* window) {
  return (__bridge NSWindow*)SDL_GetPointerProperty(
      SDL_GetWindowProperties(window),
      kCocoaWindowProperty,
      NULL);
}

// Realmz pins and builds this SDL source tree in-process. AppKit can crash
// after setContentAspectRatio(0, 0) (SDL #14229), while clearing only the
// NSWindow constraint leaves SDL's own aspect bookkeeping at 4:3. Until the
// pin includes an upstream clear-aspect fix, update both layers here. Re-audit
// these private fields whenever the SDL gitlink changes.
#include "../../vendored/SDL/src/video/SDL_sysvideo.h"

// The following is necessary to clear lock and free-resize the window.
// Switching to resize by 1x1 increments drops the lock. See SDL #14229
extern "C" void MacResetWindowAspect(struct SDL_Window* window) {
  if (!window) {
    return;
  }
  window->min_aspect = 0.0f;
  window->max_aspect = 0.0f;
  NSWindow* nswindow = NativeWindowForSDLWindow(window);
  if (nswindow) {
    [nswindow setContentResizeIncrements:NSMakeSize(1, 1)];
  }
}
