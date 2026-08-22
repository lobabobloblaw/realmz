#pragma once

#include "QuickDraw.h"
#include "SDL3/SDL.h"
#include "Types.h"
#include "presentation/SemanticInputBoundary.h"

// See Event Manager chapter in Inside Macintosh volume 1, starting on page 241

enum {
  nullEvent = 0,
  mouseDown = 1,
  mouseUp = 2,
  keyDown = 3,
  keyUp = 4,
  autoKey = 5,
  updateEvt = 6,
  diskEvt = 7,
  activateEvt = 8,
  networkEvt = 10,
  driverEvt = 11,
  app1Evt = 12,
  app2Evt = 13,
  app3Evt = 14,
  app4Evt = 15,
  osEvt = 15,
  everyEvent = -1,
};

#define inMenuBar 1
#define inSysWindow 2
#define inContent 3

#define MAC_VK_BACKSPACE 0x33

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  // Event type (one of the event codes in the above enum)
  uint16_t what;

  // The contents of the message field depend on the event type:
  // keyDown/keyUp: char code in low byte, key code in next byte, upper word unused
  // activateEvt/updateEvt: pointer to window structure
  // diskEvt: drive number in low word, file manager result code in high word (we never generate these)
  // mouseDown/mouseUp/nullEvent: unused
  // networkEvt: handle to parameter block (we never generate these)
  // driverEvt: see driver chapter (we never generate these)
  // appEvt types: anything (application-defined)
  uint32_t message;

  // When the event occurred, in ticks (just use TickCount for this)
  uint32_t when;

  // Mouse location at time of event
  Point where;

  // Bits in the modifiers field: rosROLSCM------A
  // r = right control key down
  // o = right option key down
  // s = right shift key down
  // R = control key down
  // O = option key down
  // L = caps lock enabled
  // S = shift key down
  // C = command key down
  // M = mouse button is up (NOT DOWN, unlike the other bits!)
  // A = window activated (for activateEvt)
  uint16_t modifiers;

  // The following fields are not part of the original Classic Mac OS API
  void* window_port; // CCGrafPort pointer used to identify the window in C++
  char text[32];
} EventRecord;

uint8_t mac_vk_from_message(uint32_t message);

uint32_t TickCount(void);
uint32_t GetDblTime(void);
void SystemTask(void);
// GetCaretTime (IM1-260) not used by Realmz

// While a replay runtime is installed, raw Classic polls return a deterministic
// null record and never ingest SDL or consume queued semantic commands.
Boolean GetNextEvent(int16_t mask, EventRecord* ev); // IM1-257
// Top-level exploration/dungeon/combat receive boundary for Remastered semantic
// input. In ordinary Classic mode this is exactly the GetNextEvent route. A
// semantic replay remains able to consume controller-queued tagged commands
// through this guarded boundary while all physical input stays isolated.
Boolean GetNextSemanticGameplayEvent(
    int16_t mask,
    EventRecord* ev,
    RealmzSemanticInputSurface surface);
// EventAvail (IM1-258) not used by Realmz
void PushMenuEvent(int16_t menu_id, int16_t item_id);

// Queues tagged presentation commands. They remain distinguishable from
// physical Classic input until a guarded top-level gameplay loop revalidates
// them at consumption time.
Boolean PushSemanticMovementEvent(uint32_t tagged_message);
Boolean PushSemanticPartySelectionEvent(uint32_t tagged_message);
Boolean PushSemanticOpenCharacterSheetEvent(uint32_t tagged_message);
Boolean PushSemanticOpenInventoryEvent(uint32_t tagged_message);
Boolean PushSemanticOpenSpellbookEvent(uint32_t tagged_message);
Boolean PushSemanticOpenScrollCaseEvent(uint32_t tagged_message);
Boolean PushSemanticOpenSaveGameEvent(uint32_t tagged_message);
Boolean PushSemanticOpenLoadGameEvent(uint32_t tagged_message);
Boolean PushSemanticRestPartyEvent(uint32_t tagged_message);
Boolean PushSemanticSetCampStateEvent(uint32_t tagged_message);
Boolean PushSemanticSetSearchStateEvent(uint32_t tagged_message);
Boolean PushSemanticUseTorchEvent(uint32_t tagged_message);
Boolean PushSemanticGuardCombatantEvent(uint32_t tagged_message);
Boolean PushSemanticFinishCombatantEvent(uint32_t tagged_message);
Boolean PushSemanticDelayCombatantEvent(uint32_t tagged_message);
Boolean PushSemanticCenterActiveCombatantEvent(uint32_t tagged_message);
Boolean PushSemanticSwitchWeaponEvent(uint32_t tagged_message);
Boolean PushSemanticCycleCombatFocusEvent(uint32_t tagged_message);
Boolean PushSemanticOpenCombatItemsEvent(uint32_t tagged_message);
Boolean PushSemanticAutoCombatantEvent(uint32_t tagged_message);
Boolean PushSemanticShowCombatRangeEvent(uint32_t tagged_message);
Boolean PushSemanticBandageCombatantEvent(uint32_t tagged_message);
Boolean PushSemanticUndoCombatantEvent(uint32_t tagged_message);
Boolean PushSemanticOpenCombatSpellbookEvent(uint32_t tagged_message);
Boolean PushSemanticOpenCombatTargetingEvent(uint32_t tagged_message);
Boolean PushSemanticEscapeCombatEvent(uint32_t tagged_message);
Boolean PushSemanticOpenCombatScrollCaseEvent(uint32_t tagged_message);
Boolean PushSemanticCenterCombatCursorEvent(uint32_t tagged_message);

// Removes and returns the selected member staged for the current semantic
// Character Sheet handoff. The value is one-shot and is cleared before every
// subsequent event poll, FlushEvents, or semantic-input cancellation.
Boolean TakeSemanticOpenCharacterSheetMember(uint8_t* party_member);

// Removes and returns the absolute Search state staged for the current
// semantic app1Evt handoff. The strict 0/1 value is one-shot and clears before
// pointer validation, every subsequent poll, flush, or semantic cancellation.
Boolean TakeSemanticSetSearchStateDesired(uint8_t* desired_searching);

// Removes and returns the first-usable Torch locator staged for the current
// neutral semantic app1Evt handoff. It is one-shot and clears before pointer
// validation, every later poll, flush, or semantic cancellation.
Boolean TakeSemanticUseTorchSource(uint8_t* member, uint8_t* slot);

// Removes and returns the absolute cell staged for the current semantic
// lowercase-"m" handoff. The value is one-shot and is cleared before every
// subsequent event poll or semantic-input cancellation.
Boolean TakeSemanticCenterCombatCursorCell(
    uint8_t* absolute_x,
    uint8_t* absolute_y);

// Cancels active authorization and removes every queued presentation command.
// Used by presentation-mode transitions, including native-menu callbacks.
void CancelSemanticGameplayInput(void);
// Compatibility name retained for existing movement-only callers. It now
// cancels the complete tagged gameplay stream.
void CancelSemanticMovementInput(void);

void GetMouse(Point* mouseLoc); // IM1-259
void GetMouseGlobal(Point* mouseLoc); // extension (not part of original API)
void SetMouseLocation(const Point* mouseLoc); // extension (not part of original API)
Boolean Button(void); // IM1-259
Boolean StillDown(void); // IM1-259

void FlushEvents(int16_t mask, uint16_t stop_mask); // IM2-69

Boolean WaitNextEvent(int16_t mask, EventRecord* ev, uint32_t sleep, RgnHandle mouse_rgn);

void reset_mouse_state();

#ifdef __cplusplus
}
#endif
