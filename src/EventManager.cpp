#include "EventManager.h"

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <optional>
#include <phosg/Strings.hh>
#include <variant>

#include "Types.hpp"
#include "WindowManager.hpp"
#include "presentation/LegacyPartySelection.h"
#include "presentation/SemanticInputBoundary.h"
#include "replay/ReplayRuntime.hpp"
#include "replay/SemanticReplayChildSession.hpp"

static phosg::PrefixedLogger em_log("[EventManager] ", DEFAULT_LOG_LEVEL);

[[nodiscard]] static realmz::replay::ReplayRuntime*
active_replay_runtime() noexcept {
  return realmz::replay::installed_replay_runtime();
}

[[nodiscard]] static realmz::replay::ReplayPartySelectionDeliveryOutcome
replay_party_selection_outcome(
    RealmzPartySelectionApplyResult result) noexcept {
  switch (result) {
    case REALMZ_PARTY_SELECTION_CHANGED:
      return realmz::replay::ReplayPartySelectionDeliveryOutcome::changed;
    case REALMZ_PARTY_SELECTION_UNCHANGED:
      return realmz::replay::ReplayPartySelectionDeliveryOutcome::unchanged;
    case REALMZ_PARTY_SELECTION_REJECTED:
      return realmz::replay::ReplayPartySelectionDeliveryOutcome::rejected;
  }
  return realmz::replay::ReplayPartySelectionDeliveryOutcome::rejected;
}

// Prevent any C++ exception raised by replay-only dispatch, queueing, late
// translation, or logging from unwinding through the preserved C gameplay
// frames. Ordinary interactive execution retains its existing exception
// behavior.
class ReplayExceptionBoundary final {
public:
  explicit ReplayExceptionBoundary(bool active) noexcept
      : active_(active),
        uncaught_on_entry_(std::uncaught_exceptions()) {}

  ~ReplayExceptionBoundary() noexcept {
    if (active_ &&
        std::uncaught_exceptions() > uncaught_on_entry_) {
      realmz::replay::fail_semantic_replay_child(
          "unhandled exception escaped the gameplay poll");
    }
  }

  ReplayExceptionBoundary(const ReplayExceptionBoundary&) = delete;
  ReplayExceptionBoundary& operator=(const ReplayExceptionBoundary&) = delete;

private:
  bool active_;
  int uncaught_on_entry_;
};

struct PendingSemanticCenterCombatCursorCell {
  uint8_t x;
  uint8_t y;
};

static std::optional<PendingSemanticCenterCombatCursorCell>
    pending_semantic_center_combat_cursor_cell;

static void clear_pending_semantic_center_combat_cursor_cell() noexcept {
  pending_semantic_center_combat_cursor_cell.reset();
}

static void stage_semantic_center_combat_cursor_cell(
    uint8_t absolute_x,
    uint8_t absolute_y) noexcept {
  pending_semantic_center_combat_cursor_cell = {
      .x = absolute_x,
      .y = absolute_y,
  };
}

static std::optional<uint8_t> pending_semantic_open_character_sheet_member;

static void clear_pending_semantic_open_character_sheet_member() noexcept {
  pending_semantic_open_character_sheet_member.reset();
}

static void stage_semantic_open_character_sheet_member(
    uint8_t party_member) noexcept {
  pending_semantic_open_character_sheet_member = party_member;
}

static constexpr uint16_t EVMOD_RIGHT_CONTROL_KEY_DOWN = 0x8000;
static constexpr uint16_t EVMOD_RIGHT_OPTION_KEY_DOWN = 0x4000;
static constexpr uint16_t EVMOD_RIGHT_SHIFT_KEY_DOWN = 0x2000;
static constexpr uint16_t EVMOD_CONTROL_KEY_DOWN = 0x1000;
static constexpr uint16_t EVMOD_OPTION_KEY_DOWN = 0x0800;
static constexpr uint16_t EVMOD_CAPS_LOCK_ENABLED = 0x0400;
static constexpr uint16_t EVMOD_SHIFT_KEY_DOWN = 0x0200;
static constexpr uint16_t EVMOD_COMMAND_KEY_DOWN = 0x0100;
static constexpr uint16_t EVMOD_MOUSE_BUTTON_UP = 0x0080;
static constexpr uint16_t EVMOD_WINDOW_ACTIVATED = 0x0001;

static const std::unordered_map<SDL_Keycode, uint16_t> mac_vk_code_for_sdl_keycode({
    // This maps SDL key codes to Classic Mac OS virtual key codes. (Note that
    // these are not the same as hardware key codes; those are generally hidden
    // from the application.) The extracted system KCHR (below) is used to map
    // these to ASCII and Mac Roman character codes.

    // Top row of extended keyboard
    {SDLK_ESCAPE, 0x35},
    {SDLK_F1, 0x7A},
    {SDLK_F2, 0x78},
    {SDLK_F3, 0x63},
    {SDLK_F4, 0x76},
    {SDLK_F5, 0x60},
    {SDLK_F6, 0x61},
    {SDLK_F7, 0x62},
    {SDLK_F8, 0x64},
    {SDLK_F9, 0x65},
    {SDLK_F10, 0x6D},
    {SDLK_F11, 0x67},
    {SDLK_F12, 0x6F},
    {SDLK_F13, 0x69},
    {SDLK_F14, 0x6B},
    {SDLK_F15, 0x71},

    // Numerals and symbols row
    {SDLK_GRAVE, 0x32},
    {SDLK_1, 0x12},
    {SDLK_EXCLAIM, 0x12},
    {SDLK_2, 0x13},
    {SDLK_AT, 0x13},
    {SDLK_3, 0x14},
    {SDLK_HASH, 0x14},
    {SDLK_4, 0x15},
    {SDLK_DOLLAR, 0x15},
    {SDLK_5, 0x17},
    {SDLK_PERCENT, 0x17},
    {SDLK_6, 0x16},
    {SDLK_CARET, 0x16},
    {SDLK_7, 0x1A},
    {SDLK_AMPERSAND, 0x1A},
    {SDLK_8, 0x1C},
    {SDLK_ASTERISK, 0x1C},
    {SDLK_9, 0x19},
    {SDLK_LEFTPAREN, 0x19},
    {SDLK_0, 0x1D},
    {SDLK_RIGHTPAREN, 0x1D},
    {SDLK_MINUS, 0x1B},
    {SDLK_UNDERSCORE, 0x1B},
    {SDLK_EQUALS, 0x18},
    {SDLK_PLUS, 0x18},
    {SDLK_BACKSPACE, MAC_VK_BACKSPACE},
    {SDLK_INSERT, 0x72},
    {SDLK_HELP, 0x72},
    {SDLK_HOME, 0x73},
    {SDLK_PAGEUP, 0x74},
    {SDLK_NUMLOCKCLEAR, 0x47},
    {SDLK_KP_EQUALS, 0x51},
    {SDLK_KP_DIVIDE, 0x4B},
    {SDLK_KP_MULTIPLY, 0x43},

    // First alphabet row
    {SDLK_TAB, 0x30},
    {SDLK_Q, 0x0C},
    {SDLK_W, 0x0D},
    {SDLK_E, 0x0E},
    {SDLK_R, 0x0F},
    {SDLK_T, 0x11},
    {SDLK_Y, 0x10},
    {SDLK_U, 0x20},
    {SDLK_I, 0x22},
    {SDLK_O, 0x1F},
    {SDLK_P, 0x23},
    {SDLK_LEFTBRACKET, 0x21},
    {SDLK_RIGHTBRACKET, 0x1E},
    {SDLK_BACKSLASH, 0x2A},
    {SDLK_DELETE, 0x75},
    {SDLK_END, 0x77},
    {SDLK_PAGEDOWN, 0x79},
    {SDLK_KP_7, 0x59},
    {SDLK_KP_8, 0x5B},
    {SDLK_KP_9, 0x5C},
    {SDLK_KP_MINUS, 0x4E},

    // Second alphabet row
    {SDLK_A, 0x00},
    {SDLK_S, 0x01},
    {SDLK_D, 0x02},
    {SDLK_F, 0x03},
    {SDLK_G, 0x05},
    {SDLK_H, 0x04},
    {SDLK_J, 0x26},
    {SDLK_K, 0x28},
    {SDLK_L, 0x25},
    {SDLK_SEMICOLON, 0x29},
    {SDLK_COLON, 0x29},
    {SDLK_APOSTROPHE, 0x27},
    {SDLK_DBLAPOSTROPHE, 0x27},
    {SDLK_RETURN, 0x24},
    {SDLK_KP_4, 0x56},
    {SDLK_KP_5, 0x57},
    {SDLK_KP_6, 0x58},
    {SDLK_KP_PLUS, 0x45},

    // Third alphabet row
    {SDLK_Z, 0x06},
    {SDLK_X, 0x07},
    {SDLK_C, 0x08},
    {SDLK_V, 0x09},
    {SDLK_B, 0x0B},
    {SDLK_N, 0x2D},
    {SDLK_M, 0x2E},
    {SDLK_COMMA, 0x2B},
    {SDLK_LESS, 0x2B},
    {SDLK_PERIOD, 0x2F},
    {SDLK_GREATER, 0x2F},
    {SDLK_SLASH, 0x2C},
    {SDLK_QUESTION, 0x2C},
    {SDLK_UP, 0x7E},
    {SDLK_KP_1, 0x53},
    {SDLK_KP_2, 0x54},
    {SDLK_KP_3, 0x55},
    {SDLK_KP_ENTER, 0x4C},

    // Bottom row
    {SDLK_SPACE, 0x31},
    {SDLK_LEFT, 0x7B},
    {SDLK_DOWN, 0x7D},
    {SDLK_RIGHT, 0x7C},
    {SDLK_KP_0, 0x52},
    {SDLK_KP_PERIOD, 0x41},
});

// The following tables were extracted with resource_dasm from the resource
// fork of the Mac OS System file (KCHR 0)

// clang-format off
std::array<uint8_t, 0x100> kchr_0_modifiers_table{
    0, 0, 1, 0, 2, 0, 1, 0, 3, 6, 4, 4, 5, 6, 4, 4,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    1, 0, 1, 0, 1, 0, 1, 0, 4, 4, 4, 4, 4, 4, 4, 4,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    3, 6, 4, 4, 5, 6, 4, 4, 3, 6, 4, 4, 5, 6, 4, 4,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};

std::array<std::array<uint8_t, 0x80>, 8> kchr_0_tables{{
  {
    0x61, 0x73, 0x64, 0x66, 0x68, 0x67, 0x7A, 0x78, 0x63, 0x76, 0xA4, 0x62, 0x71, 0x77, 0x65, 0x72,
    0x79, 0x74, 0x31, 0x32, 0x33, 0x34, 0x36, 0x35, 0x3D, 0x39, 0x37, 0x2D, 0x38, 0x30, 0x5D, 0x6F,
    0x75, 0x5B, 0x69, 0x70, 0x0D, 0x6C, 0x6A, 0x27, 0x6B, 0x3B, 0x5C, 0x2C, 0x2F, 0x6E, 0x6D, 0x2E,
    0x09, 0x20, 0x60, 0x08, 0x03, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2E, 0x1D, 0x2A, 0x00, 0x2B, 0x1C, 0x1B, 0x1F, 0x00, 0x00, 0x2F, 0x03, 0x1E, 0x2D, 0x00,
    0x00, 0x3D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x38, 0x39, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x05, 0x01, 0x0B, 0x7F, 0x10, 0x04, 0x10, 0x0C, 0x10, 0x1C, 0x1D, 0x1F, 0x1E, 0x00,
  }, {
    0x41, 0x53, 0x44, 0x46, 0x48, 0x47, 0x5A, 0x58, 0x43, 0x56, 0xB1, 0x42, 0x51, 0x57, 0x45, 0x52,
    0x59, 0x54, 0x21, 0x40, 0x23, 0x24, 0x5E, 0x25, 0x2B, 0x28, 0x26, 0x5F, 0x2A, 0x29, 0x7D, 0x4F,
    0x55, 0x7B, 0x49, 0x50, 0x0D, 0x4C, 0x4A, 0x22, 0x4B, 0x3A, 0x7C, 0x3C, 0x3F, 0x4E, 0x4D, 0x3E,
    0x09, 0x20, 0x7E, 0x08, 0x03, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2E, 0x2A, 0x2A, 0x00, 0x2B, 0x2B, 0x1B, 0x3D, 0x00, 0x00, 0x2F, 0x03, 0x2F, 0x2D, 0x00,
    0x00, 0x3D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x38, 0x39, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x05, 0x01, 0x0B, 0x7F, 0x10, 0x04, 0x10, 0x0C, 0x10, 0x1C, 0x1D, 0x1F, 0x1E, 0x00,
  }, {
    0x41, 0x53, 0x44, 0x46, 0x48, 0x47, 0x5A, 0x58, 0x43, 0x56, 0xA4, 0x42, 0x51, 0x57, 0x45, 0x52,
    0x59, 0x54, 0x31, 0x32, 0x33, 0x34, 0x36, 0x35, 0x3D, 0x39, 0x37, 0x2D, 0x38, 0x30, 0x5D, 0x4F,
    0x55, 0x5B, 0x49, 0x50, 0x0D, 0x4C, 0x4A, 0x27, 0x4B, 0x3B, 0x5C, 0x2C, 0x2F, 0x4E, 0x4D, 0x2E,
    0x09, 0x20, 0x60, 0x08, 0x03, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2E, 0x1D, 0x2A, 0x00, 0x2B, 0x1C, 0x1B, 0x1F, 0x00, 0x00, 0x2F, 0x03, 0x1E, 0x2D, 0x00,
    0x00, 0x3D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x38, 0x39, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x05, 0x01, 0x0B, 0x7F, 0x10, 0x04, 0x10, 0x0C, 0x10, 0x1C, 0x1D, 0x1F, 0x1E, 0x00,
  }, {
    0x8C, 0xA7, 0xB6, 0xC4, 0xFA, 0xA9, 0xBD, 0xC5, 0x8D, 0xC3, 0xA4, 0xBA, 0xCF, 0xB7, 0x00, 0xA8,
    0xB4, 0xA0, 0xC1, 0xAA, 0xA3, 0xA2, 0xA4, 0xB0, 0xAD, 0xBB, 0xA6, 0xD0, 0xA5, 0xBC, 0xD4, 0xBF,
    0x00, 0xD2, 0x00, 0xB9, 0x0D, 0xC2, 0xC6, 0xBE, 0xFB, 0xC9, 0xC7, 0xB2, 0xD6, 0x00, 0xB5, 0xB3,
    0x09, 0xCA, 0x00, 0x08, 0x03, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2E, 0x1D, 0x2A, 0x00, 0x2B, 0x1C, 0x1B, 0x1F, 0x00, 0x00, 0x2F, 0x03, 0x1E, 0x2D, 0x00,
    0x00, 0x3D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x38, 0x39, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x05, 0x01, 0x0B, 0x7F, 0x10, 0x04, 0x10, 0x0C, 0x10, 0x1C, 0x1D, 0x1F, 0x1E, 0x00,
  }, {
    0x81, 0xEA, 0xEB, 0xEC, 0xEE, 0xFD, 0xFC, 0xFE, 0x82, 0xD7, 0xB1, 0xF5, 0xCE, 0xE3, 0xAB, 0xE4,
    0xE7, 0xFF, 0xDA, 0xDB, 0xDC, 0xDD, 0xDF, 0xDE, 0xB1, 0xE1, 0xE0, 0xD1, 0xA1, 0xE2, 0xD5, 0xAF,
    0xAC, 0xD3, 0xF6, 0xB8, 0x0D, 0xF1, 0xEF, 0xAE, 0xF0, 0xF2, 0xC8, 0xF8, 0xC0, 0xF7, 0xE5, 0xF9,
    0x09, 0xCA, 0x60, 0x08, 0x03, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2E, 0x2A, 0x2A, 0x00, 0x2B, 0x2B, 0x1B, 0x3D, 0x00, 0x00, 0x2F, 0x03, 0x2F, 0x2D, 0x00,
    0x00, 0x3D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x38, 0x39, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x05, 0x01, 0x0B, 0x7F, 0x10, 0x04, 0x10, 0x0C, 0x10, 0x1C, 0x1D, 0x1F, 0x1E, 0x00,
  }, {
    0x81, 0xEA, 0xEB, 0xEC, 0xEE, 0xA9, 0xBD, 0xC5, 0x82, 0xC3, 0xA4, 0xF5, 0xCE, 0xB7, 0xAB, 0xA8,
    0xE7, 0xA0, 0xC1, 0xAA, 0xA3, 0xA2, 0xA4, 0xB0, 0xAD, 0xBB, 0xA6, 0xD0, 0xA5, 0xBC, 0xD4, 0xAF,
    0xAC, 0xD2, 0xF6, 0xB8, 0x0D, 0xF1, 0xEF, 0xAE, 0xFB, 0xC9, 0xC7, 0xB2, 0xD6, 0xF7, 0xE5, 0xB3,
    0x09, 0xCA, 0x60, 0x08, 0x03, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2E, 0x1D, 0x2A, 0x00, 0x2B, 0x1C, 0x1B, 0x1F, 0x00, 0x00, 0x2F, 0x03, 0x1E, 0x2D, 0x00,
    0x00, 0x3D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x38, 0x39, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x05, 0x01, 0x0B, 0x7F, 0x10, 0x04, 0x10, 0x0C, 0x10, 0x1C, 0x1D, 0x1F, 0x1E, 0x00,
  }, {
    0x8C, 0xA7, 0xB6, 0xC4, 0xFA, 0xA9, 0xBD, 0xC5, 0x8D, 0xC3, 0xA4, 0xBA, 0xCF, 0xB7, 0xAB, 0xA8,
    0xB4, 0xA0, 0xC1, 0xAA, 0xA3, 0xA2, 0xA4, 0xB0, 0xAD, 0xBB, 0xA6, 0xD0, 0xA5, 0xBC, 0xD4, 0xBF,
    0xAC, 0xD2, 0x5E, 0xB9, 0x0D, 0xC2, 0xC6, 0xBE, 0xFB, 0xC9, 0xC7, 0xB2, 0xD6, 0x7E, 0xB5, 0xB3,
    0x09, 0xCA, 0x60, 0x08, 0x03, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2E, 0x1D, 0x2A, 0x00, 0x2B, 0x1C, 0x1B, 0x1F, 0x00, 0x00, 0x2F, 0x03, 0x1E, 0x2D, 0x00,
    0x00, 0x3D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x38, 0x39, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x05, 0x01, 0x0B, 0x7F, 0x10, 0x04, 0x10, 0x0C, 0x10, 0x1C, 0x1D, 0x1F, 0x1E, 0x00,
  }, {
    0x01, 0x13, 0x04, 0x06, 0x08, 0x07, 0x1A, 0x18, 0x03, 0x16, 0x30, 0x02, 0x11, 0x17, 0x05, 0x12,
    0x19, 0x14, 0x31, 0x32, 0x33, 0x34, 0x36, 0x35, 0x3D, 0x39, 0x37, 0x1F, 0x38, 0x30, 0x1D, 0x0F,
    0x15, 0x1B, 0x09, 0x10, 0x0D, 0x0C, 0x0A, 0x27, 0x0B, 0x3B, 0x1C, 0x2C, 0x2F, 0x0E, 0x0D, 0x2E,
    0x09, 0x20, 0x60, 0x08, 0x03, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2E, 0x1D, 0x2A, 0x00, 0x2B, 0x1C, 0x1B, 0x1F, 0x00, 0x00, 0x2F, 0x03, 0x1E, 0x2D, 0x00,
    0x00, 0x3D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x38, 0x39, 0x00, 0x00, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x05, 0x01, 0x0B, 0x7F, 0x10, 0x04, 0x10, 0x0C, 0x10, 0x1C, 0x1D, 0x1F, 0x1E, 0x00,
  },
}};
// clang-format on

static uint32_t mac_message_for_sdl_key_code(SDL_Keycode key, uint16_t modifier_flags) {
  uint8_t virtual_keycode;
  try {
    virtual_keycode = mac_vk_code_for_sdl_keycode.at(key);
  } catch (const std::out_of_range&) {
    return 0;
  }

  size_t table_index = kchr_0_modifiers_table[(modifier_flags >> 8) & 0xFF];
  uint8_t char_code = kchr_0_tables.at(table_index).at(virtual_keycode);
  return (virtual_keycode << 8) | char_code;
}

uint8_t mac_vk_from_message(uint32_t message) {
  return (uint8_t)(message >> 8);
}

std::string name_for_event_type(uint16_t what) {
  switch (what) {
    case nullEvent:
      return "nullEvent";
    case mouseDown:
      return "mouseDown";
    case mouseUp:
      return "mouseUp";
    case keyDown:
      return "keyDown";
    case keyUp:
      return "keyUp";
    case autoKey:
      return "autoKey";
    case updateEvt:
      return "updateEvt";
    case diskEvt:
      return "diskEvt";
    case activateEvt:
      return "activateEvt";
    case networkEvt:
      return "networkEvt";
    case driverEvt:
      return "driverEvt";
    case app1Evt:
      return "app1Evt";
    case app2Evt:
      return "app2Evt";
    case app3Evt:
      return "app3Evt";
    case app4Evt:
      return "app4Evt/osEvt";
    case everyEvent:
      return "everyEvent";
    default:
      return std::format("0x{:04X}", what);
  }
}

class EventManager {
public:
  EventManager() = default;
  ~EventManager() = default;

  void flush_events() {
    if (active_replay_runtime()) {
      // FlushEvents is used pervasively by preserved modal code. During
      // replay it must not consume commands that the replay controller has
      // queued for a later guarded semantic poll.
      em_log.debug_f("Replay event flush ignored");
      return;
    }
    this->enqueue_pending_events(0);
    this->event_queue.clear();
    em_log.debug_f("Event queue cleared");
  }

  EventRecord get_next_event(uint32_t wait_ms) {
    if (active_replay_runtime()) {
      return this->make_replay_null_event();
    }
    this->enqueue_pending_events(wait_ms);
    const auto active_surface = RealmzCurrentSemanticInputSurface();
    const auto old_size = this->event_queue.size();
    std::erase_if(
        this->event_queue,
        [active_surface](const EventRecord& candidate) {
          if ((candidate.what != app1Evt) ||
              !RealmzIsSemanticGameplayTag(candidate.message)) {
            return false;
          }
          return (active_surface == REALMZ_SEMANTIC_INPUT_NONE) ||
              (RealmzSemanticGameplayTagSurface(candidate.message) !=
                  active_surface);
        });
    if (this->event_queue.size() != old_size) {
      em_log.debug_f(
          "Dropped {} tagged semantic gameplay event(s) outside their "
          "originating top-level gameplay input surface",
          old_size - this->event_queue.size());
    }
    if (this->event_queue.empty()) {
      auto window = WindowManager::instance().front_window();
      if (window) {
        window->idle_text_caret();
      }
      return this->make_null_event();
    } else {
      EventRecord ev = this->event_queue.front();
      this->event_queue.pop_front();
      em_log.debug_f("Dequeued event (what={}, message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), modifiers=0x{:04X})", name_for_event_type(ev.what), ev.message, ev.when, ev.where.h, ev.where.v, ev.modifiers);
      return ev;
    }
  }

  EventRecord get_next_semantic_event(uint32_t wait_ms) {
    if (active_replay_runtime()) {
      return this->get_next_replay_event();
    }
    return this->get_next_event(wait_ms);
  }

  void push_menu_event(int16_t menu_id, int16_t item_id) {
    if (active_replay_runtime()) {
      return;
    }
    Point where = {static_cast<int16_t>(-menu_id), static_cast<int16_t>(-item_id)};
    const auto& ev = this->event_queue.emplace_back(EventRecord{mouseDown, 0, 0, where, 0});
    em_log.debug_f("Enqueued menu event (what={}, message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), modifiers=0x{:04X})", name_for_event_type(ev.what), ev.message, ev.when, ev.where.h, ev.where.v, ev.modifiers);
  }

  bool push_semantic_movement_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticMovementTag(tagged_message)) {
      return false;
    }
    // Keep this command tagged until a guarded top-level gameplay loop
    // revalidates and translates it. It must never be mistaken for physical
    // Classic key input by a nested picker or modal loop.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic movement (what={}, message=0x{:08X}, "
        "when=0x{:08X}, where=(h={}, v={}), modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_party_selection_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticPartySelectionTag(tagged_message)) {
      return false;
    }
    // The selection remains tagged until the originating guarded top-level
    // gameplay loop revalidates it. A nested picker or modal must never see a
    // synthetic portrait click or a direct legacy-global mutation.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic party selection (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_character_sheet_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenCharacterSheetTag(tagged_message)) {
      return false;
    }
    // Keep member and surface encoded until the guarded world loop can reject
    // stale selection. Never expose this tag to a nested Classic modal.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open character sheet (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_inventory_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenInventoryTag(tagged_message)) {
      return false;
    }
    // Keep the selected member attached to the command until the guarded
    // top-level loop confirms that selection has not changed.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open inventory (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_spellbook_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenSpellbookTag(tagged_message)) {
      return false;
    }
    // Keep the intended caster attached until the guarded top-level loop can
    // confirm selection, consciousness, and spell points are still valid.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open spellbook (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_scroll_case_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenScrollCaseTag(tagged_message)) {
      return false;
    }
    // Keep the selected member and originating world surface attached until
    // the guarded top-level loop can recheck live non-combat eligibility.
    // The tag must never enter the preserved scroll chooser as raw input.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open scroll case (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_save_game_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenSaveGameTag(tagged_message)) {
      return false;
    }
    // Keep this as an application-defined event until the guarded top-level
    // loop confirms the originating surface is still active. A nested modal
    // must never mistake it for a native Game-menu click.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open save game (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_load_game_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenLoadGameTag(tagged_message)) {
      return false;
    }
    // The semantic action opens only the chooser. Preserve the tagged event
    // until the top-level loop revalidates the gameplay surface; selecting a
    // slot and replacing state remain entirely inside the Classic flow.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open load game (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_rest_party_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticRestPartyTag(tagged_message)) {
      return false;
    }
    // Keep Rest distinct from combat Range even though both eventually use
    // Classic's lowercase-r record. The world-only tag retains its origin
    // until fresh camp and presentation state are checked at delivery.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic rest party (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_set_camp_state_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticSetCampStateTag(tagged_message)) {
      return false;
    }
    // Preserve the absolute desired state until delivery. Classic still
    // receives only its lowercase-c record, after the guarded world consumer
    // proves that the fresh current state differs from that desired state.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic set camp state (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_guard_combatant_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticGuardCombatantTag(tagged_message)) {
      return false;
    }
    // Keep the actor attached until the guarded combat loop confirms the same
    // live party combatant still owns the turn.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic guard combatant (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_finish_combatant_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticFinishCombatantTag(tagged_message)) {
      return false;
    }
    // Keep the actor attached until the guarded combat loop confirms the same
    // live party combatant still owns the turn.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic finish combatant (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_delay_combatant_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticDelayCombatantTag(tagged_message)) {
      return false;
    }
    // Preserve both the actor and command identity until the guarded combat
    // loop rechecks that this party member still owns an unmoved turn.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic delay combatant (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_center_active_combatant_event(
      uint32_t tagged_message) {
    if (!RealmzIsSemanticCenterActiveCombatantTag(tagged_message)) {
      return false;
    }
    // Keep the actor attached until the guarded combat loop confirms that the
    // same live party combatant is still the camera target for this turn.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic center active combatant (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_switch_weapon_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticSwitchWeaponTag(tagged_message)) {
      return false;
    }
    // Preserve the actor and relative command identity until the guarded
    // combat loop confirms that the same live party member still owns the turn.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic switch weapon (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_cycle_combat_focus_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticCycleCombatFocusTag(tagged_message)) {
      return false;
    }
    // Preserve the actor and relative direction until the guarded combat loop
    // confirms that the same live party member still owns the turn.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic cycle combat focus (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_combat_items_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenCombatItemsTag(tagged_message)) {
      return false;
    }
    // Preserve the acting combatant and selected member independently until
    // the guarded combat loop confirms that both still match the live state.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open combat items (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_auto_combatant_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticAutoCombatantTag(tagged_message)) {
      return false;
    }
    // Preserve the actor until the guarded combat loop confirms that the same
    // live party member still owns the turn. Classic retains every automated
    // decision and effect after the lowercase "a" handoff.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic auto combatant (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_show_combat_range_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticShowCombatRangeTag(tagged_message)) {
      return false;
    }
    // Preserve the actor until the guarded combat loop confirms that the same
    // live party member still owns the turn. The lowercase "r" handoff then
    // enters Classic's raw range-overlay dismissal loop outside semantic scope.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic show combat range (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_bandage_combatant_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticBandageCombatantTag(tagged_message)) {
      return false;
    }
    // Preserve the actor until the guarded combat loop rechecks both turn
    // ownership and Classic's canundo gate. The lowercase "b" handoff leaves
    // target selection and every mutation inside the preserved Classic flow.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic bandage combatant (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_undo_combatant_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticUndoCombatantTag(tagged_message)) {
      return false;
    }
    // Preserve the actor until the guarded combat loop rechecks both turn
    // ownership and the distinct Undo capability. The lowercase "u" handoff
    // leaves condition checks and every mutation inside the Classic flow.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic undo combatant (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_combat_spellbook_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenCombatSpellbookTag(tagged_message)) {
      return false;
    }
    // Preserve the actor until the guarded combat loop rechecks turn ownership
    // and the complete read-only cancast projection. The lowercase "s" handoff
    // leaves chooser state, targeting, costs/refunds, RNG, and every mutation in
    // the preserved Classic flow.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open combat spellbook (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_combat_targeting_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenCombatTargetingTag(tagged_message)) {
      return false;
    }
    // Preserve the actor until the guarded combat loop rechecks turn ownership
    // and the read-only visible Target capability. The lowercase "t" handoff
    // leaves equipment resolution, targeting, charges/drops, costs, RNG, and
    // every mutation in the preserved Classic flow.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open combat targeting (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_escape_combat_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticEscapeCombatTag(tagged_message)) {
      return false;
    }
    // Preserve only the stable actor until the guarded combat loop rechecks
    // generic turn ownership and actor liveness. The lowercase "e" handoff
    // leaves range/condition checks, warnings, confirmation, mutation, RNG,
    // and subsequent turn or combat-exit handling inside Classic.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic escape combat (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_open_combat_scroll_case_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticOpenCombatScrollCaseTag(tagged_message)) {
      return false;
    }
    // Preserve the actor until the guarded combat loop rechecks turn ownership
    // and the read-only Scroll capability. The lowercase "l" handoff leaves
    // chooser input, selection/consumption, targeting, costs, RNG, and turn
    // handling inside the preserved Classic flow.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic open combat scroll case (what={}, "
        "message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), "
        "modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
        ev.where.v, ev.modifiers);
    return true;
  }

  bool push_semantic_center_combat_cursor_event(uint32_t tagged_message) {
    if (!RealmzIsSemanticCenterCombatCursorTag(tagged_message)) {
      return false;
    }
    // Keep the absolute field cell inside the tag until the guarded combat
    // loop revalidates the actor and current viewport. It is never copied into
    // EventRecord.where or the ambient Classic mouse point.
    auto& ev = this->event_queue.emplace_back();
    ev.what = app1Evt;
    ev.message = tagged_message;
    ev.when = TickCount();
    ev.where = {};
    ev.modifiers = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
    ev.window_port = FrontWindow();
    em_log.debug_f(
        "Enqueued tagged semantic center combat cursor (what={}, "
        "message=0x{:08X}, when=0x{:08X}, modifiers=0x{:04X})",
        name_for_event_type(ev.what), ev.message, ev.when, ev.modifiers);
    return true;
  }

  void discard_semantic_gameplay_events() {
    std::erase_if(this->event_queue, [](const EventRecord& candidate) {
      return (candidate.what == app1Evt) &&
          RealmzIsSemanticGameplayTag(candidate.message);
    });
  }

  void reset_mouse_state() {
    this->modifier_flags |= EVMOD_MOUSE_BUTTON_UP;
    if (active_replay_runtime()) {
      return;
    }
    WindowManager::instance().cancel_remastered_pointer_capture();
  }

  inline const Point& get_mouse_loc() const {
    return this->mouse_loc;
  }
  inline bool is_mouse_button_down() {
    if (active_replay_runtime()) {
      return false;
    }
    // There is at least one place where Realmz busy-loops calling Button
    // (which returns true if the mouse button is down) but does not process
    // events between those calls. In our implementation, we must process
    // events to detect any state change in the mouse button, so we do so here
    this->enqueue_pending_events(0);
    return !(this->modifier_flags & EVMOD_MOUSE_BUTTON_UP);
  }
  // Semantic delivery runs immediately after get_next_semantic_event has
  // performed the ordinary SDL pump. Consult both cached views here: shell
  // hit-testing can consume a physical down before the legacy modifier is
  // updated, while pumping again can recursively dispatch input.
  inline bool is_mouse_button_down_without_event_pump() const noexcept {
    const SDL_MouseButtonFlags sdl_mouse_buttons =
        SDL_GetMouseState(nullptr, nullptr);
    return !(this->modifier_flags & EVMOD_MOUSE_BUTTON_UP) ||
        ((sdl_mouse_buttons & SDL_BUTTON_LMASK) != 0);
  }
  bool any_mouse_events_pending() const {
    for (const auto& ev : this->event_queue) {
      if (ev.what == mouseUp || ev.what == mouseDown) {
        return true;
      }
    }
    return false;
  }

  void move_mouse_to(const Point& pt) {
    if (active_replay_runtime()) {
      return;
    }
    auto sdl_window = WindowManager::instance().get_sdl_window();
    float window_x = pt.h;
    float window_y = pt.v;
    if (!WindowManager::instance().classic_to_render_point(
            &window_x, &window_y)) {
      return;
    }
    if (auto* renderer = SDL_GetRenderer(sdl_window.get())) {
      SDL_RenderCoordinatesToWindow(
          renderer, window_x, window_y, &window_x, &window_y);
    }
    SDL_WarpMouseInWindow(sdl_window.get(), window_x, window_y);
    this->mouse_loc = pt;
  }

protected:
  Point mouse_loc = {0, 0};
  uint16_t modifier_flags = EVMOD_MOUSE_BUTTON_UP | EVMOD_WINDOW_ACTIVATED;
  std::deque<EventRecord> event_queue;

  void set_modifier_value(uint16_t what, bool enabled) {
    if (enabled) {
      this->modifier_flags |= what;
    } else {
      this->modifier_flags &= ~what;
    }
  }

  EventRecord make_null_event() const {
    return {
        .what = nullEvent,
        .message = 0,
        .when = TickCount(),
        .where = this->mouse_loc,
        .modifiers = this->modifier_flags,
    };
  }

  EventRecord make_replay_null_event() const {
    return {
        .what = nullEvent,
        .message = 0,
        .when = TickCount(),
        .where = {},
        .modifiers = 0,
        .window_port = nullptr,
        .text = {},
    };
  }

  EventRecord get_next_replay_event() {
    const auto active_surface = RealmzCurrentSemanticInputSurface();
    if (active_surface == REALMZ_SEMANTIC_INPUT_NONE) {
      // Raw Classic and modal polls are intentionally blind to both physical
      // input and queued replay commands. Only the guarded semantic wrapper
      // below may consume a replay command.
      return this->make_replay_null_event();
    }

    auto candidate = this->event_queue.begin();
    while (candidate != this->event_queue.end()) {
      if ((candidate->what != app1Evt) ||
          !RealmzIsSemanticGameplayTag(candidate->message)) {
        ++candidate;
        continue;
      }
      if (RealmzSemanticGameplayTagSurface(candidate->message) !=
          active_surface) {
        candidate = this->event_queue.erase(candidate);
        continue;
      }

      EventRecord ev = *candidate;
      this->event_queue.erase(candidate);
      ev.where = {};
      ev.modifiers = 0;
      ev.window_port = nullptr;
      em_log.debug_f(
          "Dequeued replay semantic event (what={}, message=0x{:08X}, "
          "when=0x{:08X}, where=(h={}, v={}))",
          name_for_event_type(ev.what), ev.message, ev.when, ev.where.h,
          ev.where.v);
      return ev;
    }
    return this->make_replay_null_event();
  }

  void enqueue_event(uint16_t what, uint32_t message, void* window_port, const char* text) {
    auto& ev = this->event_queue.emplace_back();
    ev.what = what;
    ev.message = message;
    ev.when = TickCount();
    ev.where = this->mouse_loc;
    ev.modifiers = this->modifier_flags;
    ev.window_port = window_port;
    if (text && strlen(text)) {
      strcpy(ev.text, text);
    }

#ifdef REALMZ_DEBUG
    // Debugging features: the backslash key switches all windows to partially-transparent to debug compositing issues;
    // this makes rendering much slower since it recomposites and alpha-blends all windows every time
    if ((ev.what == keyDown) && ((ev.message & 0xFF) == static_cast<uint8_t>('\\'))) {
      WindowManager::instance().on_debug_signal();
    }
#endif
    em_log.debug_f("Enqueued event (what={}, message=0x{:08X}, when=0x{:08X}, where=(h={}, v={}), modifiers=0x{:04X})", name_for_event_type(ev.what), ev.message, ev.when, ev.where.h, ev.where.v, ev.modifiers);
  }

  void enqueue_sdl_event(SDL_Event e) {
    if (auto* renderer = SDL_GetRenderer(WindowManager::instance().get_sdl_window().get())) {
      SDL_ConvertEventToRenderCoordinates(renderer, &e);
    }
    switch (e.type) {
      // TODO: Handle any cleanup of specific window that was closed
      //  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      case SDL_EVENT_QUIT:
        WindowManager::instance().save_prefs();
        exit(EXIT_SUCCESS);
        break;
      case SDL_EVENT_RENDER_DEVICE_RESET:
        // SDL invalidates every texture owned by the renderer while the
        // renderer pointer itself may remain unchanged. Drop the four native
        // shell textures and four proof-gated portrait textures before
        // drawing the recovery frame.
        WindowManager::instance().invalidate_remastered_shell_materials();
        WindowManager::instance().invalidate_remastered_party_portraits();
        WindowManager::instance().recomposite_all();
        break;
      case SDL_EVENT_RENDER_TARGETS_RESET:
        // The shell does not retain render targets, but the window contents
        // must still be repainted after SDL discards target state.
        WindowManager::instance().recomposite_all();
        break;
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        this->reset_mouse_state();
        WindowManager::instance().cancel_remastered_keyboard_route();
        WindowManager::instance().recomposite_all();
        break;
      case SDL_EVENT_WINDOW_EXPOSED:
        WindowManager::instance().recomposite_all();
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        this->reset_mouse_state();
        // Cancel visible/dispatch state while retaining release tombstones.
        // Some platforms deliver the matching key-up after focus returns; it
        // must not leak into Classic when its down belonged to the shell.
        WindowManager::instance().cancel_remastered_keyboard_route();
        WindowManager::instance().recomposite_all();
        break;
      case SDL_EVENT_WINDOW_MOVED:
        WindowManager::instance().note_window_moved();
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        em_log.info_f("{} mod={:04X} key={:08X}",
            (e.type == SDL_EVENT_KEY_UP) ? "SDL_EVENT_KEY_UP" : "SDL_EVENT_KEY_DOWN",
            e.key.mod, e.key.key);
        this->set_modifier_value(EVMOD_RIGHT_CONTROL_KEY_DOWN, e.key.mod & SDL_KMOD_RCTRL);
        this->set_modifier_value(EVMOD_RIGHT_OPTION_KEY_DOWN, e.key.mod & SDL_KMOD_RALT);
        this->set_modifier_value(EVMOD_RIGHT_SHIFT_KEY_DOWN, e.key.mod & SDL_KMOD_RSHIFT);
#ifdef _WIN32
        // On Windows the Control key stands in for the Mac Command key (see the Command mapping
        // below), so it must not also set the Control bit. Doing both would select the
        // control-character KCHR table and break Command-key shortcuts and typed characters.
        this->set_modifier_value(EVMOD_CONTROL_KEY_DOWN, false);
#else
        this->set_modifier_value(EVMOD_CONTROL_KEY_DOWN, e.key.mod & SDL_KMOD_CTRL);
#endif
        this->set_modifier_value(EVMOD_OPTION_KEY_DOWN, e.key.mod & SDL_KMOD_ALT);
        this->set_modifier_value(EVMOD_CAPS_LOCK_ENABLED, e.key.mod & SDL_KMOD_CAPS);
        this->set_modifier_value(EVMOD_SHIFT_KEY_DOWN, e.key.mod & SDL_KMOD_SHIFT);
#ifdef _WIN32
        // The Windows (Super) key is reserved by the OS for the Start menu and shell shortcuts, so
        // it cannot reliably act as the Mac Command key. Map the Control key to Command instead.
        this->set_modifier_value(EVMOD_COMMAND_KEY_DOWN, e.key.mod & SDL_KMOD_CTRL);
#else
        this->set_modifier_value(EVMOD_COMMAND_KEY_DOWN, e.key.mod & SDL_KMOD_GUI);
#endif

        // Code-native shell keys own their complete physical press/release
        // pair. A consumed event must never also reach the Classic queue.
        // This call remains active in Classic mode so a release captured just
        // before a mode switch can be swallowed safely.
        if (WindowManager::instance().handle_remastered_shell_key(e.key)) {
          break;
        }

        uint32_t message = mac_message_for_sdl_key_code(e.key.key, this->modifier_flags);
        bool enqueue = true;
        if (message != 0) {
          if (e.type == SDL_EVENT_KEY_DOWN) {
            // Key down repeats can come in faster than Realmz will process them, depending on the game speed settings.
            // When key events get backlogged in that fashion, the game appears sluggish until it clears the queue. To
            // prevent that, key repeats are deduplicated so that no key repeat enters the queue until the previous one
            // has been processed. The queue is scanned backwards since events are inserted at the back.
            auto cursor = event_queue.rbegin();
            while (cursor != event_queue.rend()) {
              EventRecord& existing = *cursor;
              if (existing.what == keyDown && existing.message == message) {
                em_log.info_f("Deduplicating key down for key=0x{:X}.", static_cast<size_t>(e.key.key));
                enqueue = false;
              }

              ++cursor;
            }
          }

          if (enqueue) {
            // TODO: Do keyboard events always go to the front window, or is
            // there some notion of keyboard focus in Classic Mac OS?
            this->enqueue_event((e.type == SDL_EVENT_KEY_DOWN) ? keyDown : keyUp, message, FrontWindow(), "");
          }
        } else {
          em_log.warning_f("Unknown key pressed: key=0x{:X} scancode=0x{:X}",
              static_cast<size_t>(e.key.key), static_cast<size_t>(e.key.scancode));
        }
        break;
      }
      case SDL_EVENT_MOUSE_MOTION:
        // Classic Mac OS doesn't have a mouse motion event, so we just track
        // the location and ignore it otherwise
        if (WindowManager::instance().get_presentation_mode() ==
            realmz::presentation::PresentationMode::remastered) {
          Point classic_point;
          if (WindowManager::instance().map_remastered_pointer_motion(
                  e.motion.x, e.motion.y, &classic_point)) {
            this->mouse_loc = classic_point;
          } else {
            this->mouse_loc = {
                std::numeric_limits<int16_t>::min(),
                std::numeric_limits<int16_t>::min(),
            };
          }
        } else {
          this->mouse_loc.h = e.motion.x;
          this->mouse_loc.v = e.motion.y;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        em_log.info_f("{} {} {} {:g} {:g}",
            (e.type == SDL_EVENT_MOUSE_BUTTON_UP) ? "SDL_EVENT_MOUSE_BUTTON_UP" : "SDL_EVENT_MOUSE_BUTTON_DOWN",
            e.button.button, e.button.clicks, e.button.x, e.button.y);
        // Ignore events for all mouse buttons except the primary (left) button
        if (e.button.button == 1) {
          Point classic_point;
          const bool remastered =
              WindowManager::instance().get_presentation_mode() ==
              realmz::presentation::PresentationMode::remastered;
          const bool routes_to_legacy = !remastered ||
              ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                      ? WindowManager::instance().begin_remastered_pointer(
                            e.button.x, e.button.y, &classic_point)
                      : WindowManager::instance().end_remastered_pointer(
                            e.button.x, e.button.y, &classic_point));
          if (!routes_to_legacy) {
            this->mouse_loc = {
                std::numeric_limits<int16_t>::min(),
                std::numeric_limits<int16_t>::min(),
            };
            break;
          }
          if (remastered) {
            this->mouse_loc = classic_point;
          } else {
            this->mouse_loc.h = e.button.x;
            this->mouse_loc.v = e.button.y;
          }
          this->set_modifier_value(EVMOD_MOUSE_BUTTON_UP, (e.type == SDL_EVENT_MOUSE_BUTTON_UP));
          auto window = WindowManager::instance().window_for_point(this->mouse_loc.h, this->mouse_loc.v);
          this->enqueue_event((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? mouseDown : mouseUp, 0, window ? &window->get_port() : nullptr, "");
        }
        break;
      case SDL_EVENT_TEXT_EDITING:
      case SDL_EVENT_TEXT_INPUT:
        em_log.info_f("{} {}",
            (e.type == SDL_EVENT_TEXT_EDITING) ? "SDL_EVENT_TEXT_EDITING" : "SDL_EVENT_TEXT_INPUT", e.text.text);

        // We can use the otherwise unused app4Evt to signal a text input event, the handling of which
        // would originally have been intercepted and processed by TextEdit.
        // TODO: Do keyboard events always go to the front window, or is
        // there some notion of keyboard focus in Classic Mac OS?
        this->enqueue_event(app4Evt, 0, FrontWindow(), e.text.text);
        break;
      default:
        em_log.debug_f("Unhandled SDL event type 0x{:X}", e.type);
    }
  }

  void enqueue_pending_events(int32_t wait_ms) {
    if (active_replay_runtime()) {
      return;
    }
    SDL_Event e;

    // If wait_ms > 0, wait for at least one event to be available before
    // enqueuing all remaining events
    if ((wait_ms > 0) && SDL_WaitEventTimeout(&e, wait_ms)) {
      this->enqueue_sdl_event(e);
    }
    while (SDL_PollEvent(&e)) {
      this->enqueue_sdl_event(e);
    }
  }
};

EventManager em;

uint32_t TickCount(void) {
  if (auto* replay = active_replay_runtime()) {
    return replay->next_event_tick();
  }
  return (SDL_GetTicks() * 60) / SDL_MS_PER_SECOND;
}

uint32_t GetDblTime(void) {
  // On Classic Mac OS, the double-click time was configurable; we just set it
  // to 1/3 of a second here.
  return 20;
}

void SystemTask(void) {
  if (active_replay_runtime()) {
    return;
  }
  // Realmz uses GetNextEvent in hot loops in several places, but it also calls
  // SystemTask in those loops. There's nothing for SystemTask to do on modern
  // systems since we now have preemptive multitasking, but we can use this
  // function to make the hot loops a bit less hot by sleeping for a CPU time
  // slice or two.
  SDL_Delay(10);
}

void FlushEvents(int16_t which_mask, uint16_t stop_mask) {
  // Realmz only calls this with which_mask = everyEvent and stop_mask = 0, so
  // we don't bother to implement filtering.
  if (which_mask != everyEvent) {
    throw std::logic_error(std::format("which_mask ({:04X}) masks out some events in FlushEvents", which_mask));
  }
  if (stop_mask != 0) {
    throw std::logic_error("stop_mask specifies some events");
  }

  em_log.debug_f("FlushEvents(0x{:04X}, 0x{:04X})", which_mask, stop_mask);
  clear_pending_semantic_open_character_sheet_member();
  clear_pending_semantic_center_combat_cursor_cell();
  em.flush_events();
}

Boolean GetNextEvent(int16_t which_mask, EventRecord* ret) {
  // Realmz only calls this with which_mask = everyEvent, so we don't bother to
  // implement filtering.
  if (which_mask != everyEvent) {
    throw std::logic_error(std::format("which_mask ({:04X}) masks out some events in GetNextEvent", which_mask));
  }

  clear_pending_semantic_open_character_sheet_member();
  clear_pending_semantic_center_combat_cursor_cell();
  *ret = em.get_next_event(0);
  return (ret->what != nullEvent);
}

Boolean GetNextSemanticGameplayEvent(
    int16_t which_mask,
    EventRecord* ret,
    RealmzSemanticInputSurface surface) {
  auto* replay = active_replay_runtime();
  const ReplayExceptionBoundary replay_exception_boundary(replay != nullptr);
  if (which_mask != everyEvent) {
    throw std::logic_error(std::format(
        "which_mask ({:04X}) masks out some events in "
        "GetNextSemanticGameplayEvent",
        which_mask));
  }
  if ((surface != REALMZ_SEMANTIC_INPUT_EXPLORATION) &&
      (surface != REALMZ_SEMANTIC_INPUT_DUNGEON) &&
      (surface != REALMZ_SEMANTIC_INPUT_COMBAT)) {
    throw std::logic_error(
        "GetNextSemanticGameplayEvent requires a gameplay surface");
  }

  clear_pending_semantic_open_character_sheet_member();
  clear_pending_semantic_center_combat_cursor_cell();

  const realmz::presentation::UIAction* replay_semantic_action = nullptr;
  std::uint32_t replay_expected_key_message = 0;
  std::optional<realmz::presentation::PartyMemberId>
      replay_expected_party_member;
  std::optional<realmz::presentation::PartyMemberId>
      replay_delivered_party_member;
  std::optional<realmz::replay::ReplayPartySelectionDeliveryOutcome>
      replay_party_selection_delivery;
  std::optional<realmz::presentation::CombatantId>
      replay_expected_switch_combatant;
  std::optional<std::uint32_t> replay_expected_switch_tag;
  if (replay && replay->action_plan_started()) {
    try {
      const auto directive = replay->next_gameplay_poll();
      if (directive.checkpoint) {
        realmz::replay::record_live_replay_checkpoint(
            *replay, *directive.checkpoint);
      }
      if (directive.finalize) {
        realmz::replay::complete_semantic_replay_child(*replay);
      }
      if (!directive.action) {
        throw realmz::replay::ReplayRuntimeError(
            "replay poll produced neither an action nor finalization");
      }
      const bool movement =
          std::holds_alternative<realmz::presentation::MovePartyAction>(
              directive.action->payload);
      const bool selection =
          std::holds_alternative<
              realmz::presentation::SelectPartyMemberAction>(
              directive.action->payload);
      const auto* switch_weapon =
          std::get_if<realmz::presentation::SwitchWeaponSetAction>(
              &directive.action->payload);
      if (movement) {
        const auto expected =
            WindowManager::instance().replay_movement_key_message(
                *directive.action, surface);
        if (!expected) {
          throw realmz::replay::ReplayRuntimeError(
              "replay movement is invalid for the current gameplay surface");
        }
        replay_expected_key_message = *expected;
      } else if (selection) {
        replay_expected_party_member =
            WindowManager::instance().replay_party_selection_member(
                *directive.action, surface);
        if (!replay_expected_party_member) {
          throw realmz::replay::ReplayRuntimeError(
              "replay party selection is invalid for the current gameplay "
              "surface or party");
        }
      } else if (switch_weapon) {
        const auto expected =
            WindowManager::instance().replay_switch_weapon_key_message(
                *directive.action, surface);
        if (!expected) {
          throw realmz::replay::ReplayRuntimeError(
              "replay weapon switch is invalid for the current combatant "
              "or gameplay surface");
        }
        const auto expected_tag =
            realmz::presentation::semantic_switch_weapon_tag(
                switch_weapon->combatant, surface);
        if (expected_tag == 0) {
          throw realmz::replay::ReplayRuntimeError(
              "replay weapon switch could not bind its semantic tag");
        }
        replay_expected_key_message = *expected;
        replay_expected_switch_combatant = switch_weapon->combatant;
        replay_expected_switch_tag = expected_tag;
      } else {
        throw realmz::replay::ReplayRuntimeError(
            "replay action is outside the configured native vocabulary");
      }

      if (replay->replay_route() == realmz::replay::ReplayRoute::classic) {
        *ret = {};
        ret->when = TickCount();
        if (movement || switch_weapon) {
          ret->what = keyDown;
          ret->message = replay_expected_key_message;
          const realmz::replay::ReplayObservedEvent observed{
              .kind = realmz::replay::ReplayObservedEventKind::key_down,
              .message = ret->message,
          };
          if (switch_weapon) {
            replay->acknowledge_switch_weapon_delivery(
                directive.action->sequence,
                *replay_expected_switch_combatant,
                replay_expected_key_message,
                observed);
          } else {
            replay->acknowledge_action_delivery(
                directive.action->sequence,
                replay_expected_key_message,
                observed);
          }
          return true;
        }

        const auto applied = RealmzApplyPartyMemberSelection(
            *replay_expected_party_member);
        replay->acknowledge_party_selection_delivery(
            directive.action->sequence,
            *replay_expected_party_member,
            replay_party_selection_outcome(applied));
        // SelectPartyMemberAction is the idempotent first-click semantic
        // behavior, not a synthetic portrait mouse event. It is already
        // applied and intentionally returns nullEvent.
        ret->what = nullEvent;
        ret->message = 0;
        return false;
      }
      if (replay->replay_route() !=
          realmz::replay::ReplayRoute::semantic) {
        throw realmz::replay::ReplayRuntimeError(
            "replay route is not supported by the gameplay poll");
      }
      replay_semantic_action = directive.action;
    } catch (const std::exception& error) {
      realmz::replay::fail_semantic_replay_child(error.what());
    } catch (...) {
      realmz::replay::fail_semantic_replay_child(
          "unknown gameplay-poll failure");
    }
  }
  const bool remastered = replay
      ? replay->presentation_mode() ==
          realmz::replay::ReplayPresentationMode::remastered
      : WindowManager::instance().get_presentation_mode() ==
          realmz::presentation::PresentationMode::remastered;
  if (replay_semantic_action && !remastered) {
    realmz::replay::fail_semantic_replay_child(
        "semantic replay route requires remastered presentation");
  }
  if (!remastered) {
    // With no active semantic scope, ordinary EventManager behavior drops any
    // stale tagged command before returning the next Classic event. The replay
    // controller injects Classic-route actions before reaching this branch;
    // replay raw-event isolation otherwise returns null here.
    *ret = em.get_next_event(0);
    return (ret->what != nullEvent);
  }

  struct SemanticInputScope {
    explicit SemanticInputScope(RealmzSemanticInputSurface value) {
      RealmzBeginSemanticInputSurface(value);
    }
    ~SemanticInputScope() {
      RealmzEndSemanticInputSurface();
    }
  };

  {
    const SemanticInputScope semantic_scope(surface);
    if (replay_semantic_action) {
      const auto dispatch =
          WindowManager::instance().dispatch_replay_semantic_action(
              *replay_semantic_action);
      if (!dispatch.was_handled()) {
        realmz::replay::fail_semantic_replay_child(
            dispatch.detail.empty()
                ? "semantic replay action dispatch failed"
                : dispatch.detail);
      }
    }
    *ret = em.get_next_semantic_event(0);
  }
  const bool still_remastered = replay
      ? replay->presentation_mode() ==
          realmz::replay::ReplayPresentationMode::remastered
      : WindowManager::instance().get_presentation_mode() ==
          realmz::presentation::PresentationMode::remastered;
  if ((ret->what == app1Evt) &&
      RealmzIsSemanticMovementTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticMovementEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A tagged presentation event is inert unless late validation succeeds.
      // Never expose a rejected command to an unmodified Classic switch.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticPartySelectionTag(ret->message)) {
    uint8_t party_member = 0;
    const bool validated = still_remastered &&
        RealmzConsumeSemanticPartySelectionEvent(
            surface, ret->message, &party_member);
    const RealmzPartySelectionApplyResult applied = validated
        ? RealmzApplyPartyMemberSelection(party_member)
        : REALMZ_PARTY_SELECTION_REJECTED;
    if (validated && (applied == REALMZ_PARTY_SELECTION_REJECTED)) {
      em_log.warning_f(
          "Late-validated semantic party member {} was rejected by the "
          "legacy selection adapter",
          party_member);
    }
    if (replay_expected_party_member) {
      replay_delivered_party_member = validated
          ? std::optional<realmz::presentation::PartyMemberId>(party_member)
          : replay_expected_party_member;
      replay_party_selection_delivery = validated
          ? replay_party_selection_outcome(applied)
          : realmz::replay::ReplayPartySelectionDeliveryOutcome::rejected;
    }
    // Selection is applied by the narrow adapter, not exposed to the preserved
    // legacy switch as a repeated portrait click or application-defined event.
    ret->what = nullEvent;
    ret->message = 0;
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenCharacterSheetTag(ret->message)) {
    uint8_t party_member = 0;
    if (still_remastered && RealmzConsumeSemanticOpenCharacterSheetEvent(
            surface, ret->message, &party_member)) {
      // Keep app1Evt as a guarded outer-loop signal while moving its authorized
      // member into a one-shot slot. The preserved loop owns charmainbut,
      // charselectold, and the existing buttonchoice path; no click is forged.
      stage_semantic_open_character_sheet_member(party_member);
      ret->message = 0;
      ret->where = {};
      ret->modifiers = 0;
      ret->window_port = nullptr;
    } else {
      // A stale member, selection, or surface is inert before Classic input.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenInventoryTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticOpenInventoryEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A stale selected-member payload is inert and must not open a different
      // member's inventory after recomposition or queued selection changes.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenSpellbookTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticOpenSpellbookEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued caster is inert if selection, consciousness, spell points,
      // or the originating gameplay surface changed before consumption.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenScrollCaseTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticOpenScrollCaseEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // Selection, capability, or either world-surface identity may have
      // changed while queued. A rejected tag is inert before Classic input.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenSaveGameTag(ret->message)) {
    int16_t menu_id = 0;
    int16_t item_id = 0;
    if (still_remastered && RealmzConsumeSemanticOpenSaveGameEvent(
            surface, ret->message, &menu_id, &item_id)) {
      // Native menu callbacks already use this exact negative-coordinate
      // mouseDown representation. The preserved loop will call MenuSelect and
      // HandleMenuChoice normally, which opens the Classic slot chooser.
      ret->what = mouseDown;
      ret->message = 0;
      ret->where.v = static_cast<int16_t>(-menu_id);
      ret->where.h = static_cast<int16_t>(-item_id);
    } else {
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenLoadGameTag(ret->message)) {
    int16_t menu_id = 0;
    int16_t item_id = 0;
    if (still_remastered && RealmzConsumeSemanticOpenLoadGameEvent(
            surface, ret->message, &menu_id, &item_id)) {
      // Reuse the native menu event representation so the preserved Game
      // menu handler opens its chooser without any semantic slot selection.
      ret->what = mouseDown;
      ret->message = 0;
      ret->where.v = static_cast<int16_t>(-menu_id);
      ret->where.h = static_cast<int16_t>(-item_id);
    } else {
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticRestPartyTag(ret->message)) {
    uint32_t classic_key_message = 0;
    const bool mouse_button_held =
        em.is_mouse_button_down_without_event_pump();
    if (still_remastered && !mouse_button_held &&
        RealmzConsumeSemanticRestPartyEvent(
            surface, ret->message, &classic_key_message)) {
      // Pointer actions dispatch after release, and keyboard actions are
      // admitted only when the cached button state is up. The resulting
      // keyDown therefore begins with one mandatory Classic rest quantum;
      // any distinct physical press after delivery remains Classic input.
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      if (mouse_button_held) {
        // A held-button rejection must also burn the completed one-shot scope.
        RealmzInvalidateSemanticInputBoundary();
      }
      // Camp state, presentation, or cached button state can change while
      // queued. Rejected Rest is inert and can never fall through as combat
      // Range or a raw app event.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticSetCampStateTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticSetCampStateEvent(
            surface, ret->message, &classic_key_message)) {
      // The tag carries an absolute desired state, but Classic receives its
      // original relative lowercase-c record only after a fresh mismatch
      // check. The preserved c/buttonchoice/campbut flow owns permission,
      // feedback, music, time, and the actual state transition.
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A stale desired state, surface, or presentation is inert before the
      // unmodified Classic switch and cannot be mistaken for combat Center.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticGuardCombatantTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticGuardCombatantEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued Guard is inert if the turn or acting combatant changed.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticFinishCombatantTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticFinishCombatantEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued Finish is inert if the turn or acting combatant changed.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticDelayCombatantTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticDelayCombatantEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // Delay is inert after movement, an actor change, or a surface change.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticCenterActiveCombatantTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticCenterActiveCombatantEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued camera command cannot follow a later turn's actor.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticSwitchWeaponTag(ret->message)) {
    uint32_t classic_key_message = 0;
    const bool replay_switch_tag_matches =
        !replay_expected_switch_tag ||
        (ret->message == *replay_expected_switch_tag);
    if (still_remastered && replay_switch_tag_matches &&
        RealmzConsumeSemanticSwitchWeaponEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued relative toggle cannot follow a later turn's actor.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticCycleCombatFocusTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticCycleCombatFocusEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued relative focus command cannot follow a later turn's actor.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenCombatItemsTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticOpenCombatItemsEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued Items request is inert after either the acting combatant or
      // selected party member changes.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticAutoCombatantTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticAutoCombatantEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued Auto request cannot follow a later turn's actor.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticShowCombatRangeTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticShowCombatRangeEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued range overlay cannot follow a later turn's actor. Rejection
      // also prevents a tagged app event from entering Classic's raw modal.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticBandageCombatantTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticBandageCombatantEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued Bandage is inert after the actor, surface, or Classic canundo
      // eligibility changes; never expose the tagged event to a nested picker.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticUndoCombatantTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticUndoCombatantEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued Undo is inert after the actor, surface, or live capability
      // changes. Classic condition checks run only after an accepted handoff.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenCombatSpellbookTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticOpenCombatSpellbookEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // Never allow a stale cast request to enter Classic's chooser or nested
      // target loop after actor, surface, or cancast eligibility changes.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenCombatTargetingTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticOpenCombatTargetingEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // Never allow a stale Target request to enter Classic's equipment or
      // nested targeting flow after actor, surface, or capability changes.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticEscapeCombatTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticEscapeCombatEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued Escape cannot follow a later turn's actor. Escape-specific
      // range, condition, warning, and confirmation behavior remains Classic.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticOpenCombatScrollCaseTag(ret->message)) {
    uint32_t classic_key_message = 0;
    if (still_remastered && RealmzConsumeSemanticOpenCombatScrollCaseEvent(
            surface, ret->message, &classic_key_message)) {
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      // A queued scroll request is inert after the actor, surface, or visible
      // Scroll capability changes; never expose its tag to Classic's modal.
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else if ((ret->what == app1Evt) &&
      RealmzIsSemanticCenterCombatCursorTag(ret->message)) {
    uint32_t classic_key_message = 0;
    uint8_t absolute_x = 0;
    uint8_t absolute_y = 0;
    if (still_remastered && RealmzConsumeSemanticCenterCombatCursorEvent(
            surface,
            ret->message,
            &classic_key_message,
            &absolute_x,
            &absolute_y)) {
      // Stage the absolute cell only after all late validation succeeds. The
      // Classic switch receives the exact lowercase "m" key record while the
      // field cell remains independent of EventRecord.where and global point.
      stage_semantic_center_combat_cursor_cell(absolute_x, absolute_y);
      ret->what = keyDown;
      ret->message = classic_key_message;
    } else {
      ret->what = nullEvent;
      ret->message = 0;
    }
  } else {
    // Authorization belongs only to the event returned by this wrapper. Do
    // not leave a completed scope available after an ordinary Classic event.
    RealmzInvalidateSemanticInputBoundary();
  }
  if (replay) {
    ret->modifiers = 0;
  }
  if (replay_semantic_action) {
    try {
      if (replay_expected_party_member) {
        replay->acknowledge_party_selection_delivery(
            replay_semantic_action->sequence,
            replay_delivered_party_member.value_or(
                *replay_expected_party_member),
            replay_party_selection_delivery.value_or(
                realmz::replay::ReplayPartySelectionDeliveryOutcome::rejected));
      } else if (replay_expected_switch_combatant) {
        replay->acknowledge_switch_weapon_delivery(
            replay_semantic_action->sequence,
            *replay_expected_switch_combatant,
            replay_expected_key_message,
            {
                .kind = (ret->what == keyDown)
                    ? realmz::replay::ReplayObservedEventKind::key_down
                    : realmz::replay::ReplayObservedEventKind::other,
                .message = ret->message,
            });
      } else {
        replay->acknowledge_action_delivery(
            replay_semantic_action->sequence,
            replay_expected_key_message,
            {
                .kind = (ret->what == keyDown)
                    ? realmz::replay::ReplayObservedEventKind::key_down
                    : realmz::replay::ReplayObservedEventKind::other,
                .message = ret->message,
            });
      }
    } catch (const std::exception& error) {
      realmz::replay::fail_semantic_replay_child(error.what());
    } catch (...) {
      realmz::replay::fail_semantic_replay_child(
          "unknown semantic action acknowledgement failure");
    }
  }
  return (ret->what != nullEvent);
}

Boolean WaitNextEvent(int16_t which_mask, EventRecord* ret, uint32_t sleep, RgnHandle mouse_rgn) {
  // Realmz doesn't use mask, sleep, or mouse_rgn (thankfully, since mouse_rgn
  // would be annoying to implement!)
  if (which_mask != everyEvent) {
    throw std::logic_error(std::format("which_mask ({:04X}) masks out some events in WaitNextEvent", which_mask));
  }
  if (mouse_rgn) {
    throw std::logic_error("mouse_rgn must be null");
  }

  clear_pending_semantic_open_character_sheet_member();
  clear_pending_semantic_center_combat_cursor_cell();
  *ret = em.get_next_event(sleep);
  return (ret->what != nullEvent);
}

void GetMouse(Point* ret) {
  if (active_replay_runtime()) {
    *ret = {};
    return;
  }
  *ret = em.get_mouse_loc();

  // GetMouse isn't actually an Event Manager function... it's a QuickDraw
  // function! So, unlike the rest of the Event Manager, it returns coordinates
  // local to the current graphics port.
  auto port = CCGrafPort::as_port(qd.thePort);
  if (port) {
    *ret = port->to_local_space(*ret);
  }
}

void GetMouseGlobal(Point* ret) {
  if (active_replay_runtime()) {
    *ret = {};
    return;
  }
  *ret = em.get_mouse_loc();
}

void SetMouseLocation(const Point* mouseLoc) {
  if (active_replay_runtime()) {
    return;
  }
  em.move_mouse_to(*mouseLoc);
}

Boolean Button(void) {
  if (active_replay_runtime()) {
    return 0;
  }
  return em.is_mouse_button_down();
}

Boolean StillDown(void) {
  if (active_replay_runtime()) {
    return 0;
  }
  return em.is_mouse_button_down() && !em.any_mouse_events_pending();
}

void PushMenuEvent(int16_t menu_id, int16_t item_id) {
  em.push_menu_event(menu_id, item_id);
}

Boolean PushSemanticMovementEvent(uint32_t tagged_message) {
  return em.push_semantic_movement_event(tagged_message);
}

Boolean PushSemanticPartySelectionEvent(uint32_t tagged_message) {
  return em.push_semantic_party_selection_event(tagged_message);
}

Boolean PushSemanticOpenCharacterSheetEvent(uint32_t tagged_message) {
  return em.push_semantic_open_character_sheet_event(tagged_message);
}

Boolean PushSemanticOpenInventoryEvent(uint32_t tagged_message) {
  return em.push_semantic_open_inventory_event(tagged_message);
}

Boolean PushSemanticOpenSpellbookEvent(uint32_t tagged_message) {
  return em.push_semantic_open_spellbook_event(tagged_message);
}

Boolean PushSemanticOpenScrollCaseEvent(uint32_t tagged_message) {
  return em.push_semantic_open_scroll_case_event(tagged_message);
}

Boolean PushSemanticOpenSaveGameEvent(uint32_t tagged_message) {
  return em.push_semantic_open_save_game_event(tagged_message);
}

Boolean PushSemanticOpenLoadGameEvent(uint32_t tagged_message) {
  return em.push_semantic_open_load_game_event(tagged_message);
}

Boolean PushSemanticRestPartyEvent(uint32_t tagged_message) {
  return em.push_semantic_rest_party_event(tagged_message);
}

Boolean PushSemanticSetCampStateEvent(uint32_t tagged_message) {
  return em.push_semantic_set_camp_state_event(tagged_message);
}

Boolean PushSemanticGuardCombatantEvent(uint32_t tagged_message) {
  return em.push_semantic_guard_combatant_event(tagged_message);
}

Boolean PushSemanticFinishCombatantEvent(uint32_t tagged_message) {
  return em.push_semantic_finish_combatant_event(tagged_message);
}

Boolean PushSemanticDelayCombatantEvent(uint32_t tagged_message) {
  return em.push_semantic_delay_combatant_event(tagged_message);
}

Boolean PushSemanticCenterActiveCombatantEvent(uint32_t tagged_message) {
  return em.push_semantic_center_active_combatant_event(tagged_message);
}

Boolean PushSemanticSwitchWeaponEvent(uint32_t tagged_message) {
  return em.push_semantic_switch_weapon_event(tagged_message);
}

Boolean PushSemanticCycleCombatFocusEvent(uint32_t tagged_message) {
  return em.push_semantic_cycle_combat_focus_event(tagged_message);
}

Boolean PushSemanticOpenCombatItemsEvent(uint32_t tagged_message) {
  return em.push_semantic_open_combat_items_event(tagged_message);
}

Boolean PushSemanticAutoCombatantEvent(uint32_t tagged_message) {
  return em.push_semantic_auto_combatant_event(tagged_message);
}

Boolean PushSemanticShowCombatRangeEvent(uint32_t tagged_message) {
  return em.push_semantic_show_combat_range_event(tagged_message);
}

Boolean PushSemanticBandageCombatantEvent(uint32_t tagged_message) {
  return em.push_semantic_bandage_combatant_event(tagged_message);
}

Boolean PushSemanticUndoCombatantEvent(uint32_t tagged_message) {
  return em.push_semantic_undo_combatant_event(tagged_message);
}

Boolean PushSemanticOpenCombatSpellbookEvent(uint32_t tagged_message) {
  return em.push_semantic_open_combat_spellbook_event(tagged_message);
}

Boolean PushSemanticOpenCombatTargetingEvent(uint32_t tagged_message) {
  return em.push_semantic_open_combat_targeting_event(tagged_message);
}

Boolean PushSemanticEscapeCombatEvent(uint32_t tagged_message) {
  return em.push_semantic_escape_combat_event(tagged_message);
}

Boolean PushSemanticOpenCombatScrollCaseEvent(uint32_t tagged_message) {
  return em.push_semantic_open_combat_scroll_case_event(tagged_message);
}

Boolean PushSemanticCenterCombatCursorEvent(uint32_t tagged_message) {
  return em.push_semantic_center_combat_cursor_event(tagged_message);
}

Boolean TakeSemanticOpenCharacterSheetMember(uint8_t* party_member) {
  const auto pending = pending_semantic_open_character_sheet_member;
  clear_pending_semantic_open_character_sheet_member();
  if (!pending || !party_member) {
    return 0;
  }
  *party_member = *pending;
  return 1;
}

Boolean TakeSemanticCenterCombatCursorCell(
    uint8_t* absolute_x,
    uint8_t* absolute_y) {
  const auto pending = pending_semantic_center_combat_cursor_cell;
  clear_pending_semantic_center_combat_cursor_cell();
  if (!pending || !absolute_x || !absolute_y) {
    return 0;
  }
  *absolute_x = pending->x;
  *absolute_y = pending->y;
  return 1;
}

void CancelSemanticGameplayInput(void) {
  clear_pending_semantic_open_character_sheet_member();
  clear_pending_semantic_center_combat_cursor_cell();
  RealmzInvalidateSemanticInputBoundary();
  em.discard_semantic_gameplay_events();
}

void CancelSemanticMovementInput(void) {
  CancelSemanticGameplayInput();
}

void reset_mouse_state() {
  em.reset_mouse_state();
}
