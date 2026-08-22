#include <array>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "presentation/SemanticInputBoundary.h"
#include "presentation/UIAction.hpp"

extern "C" {
#include "realmz_orig/structs.h"
}

struct CGrafPort {
  unsigned char marker = 0;
};
using CGrafPtr = CGrafPort*;

namespace {

CGrafPort gameplay_port{};
CGrafPort overlay_port{};
CGrafPtr front_window = nullptr;

} // namespace

extern "C" {

CGrafPtr look = nullptr;
CGrafPtr gWindow = nullptr;
short currentscenario = 0;
short canundo = 0;
short fat = 0;
short incombat = 0;
short monsterturn = 0;
short nummon = 0;
short inspell = 0;
short lastshown = -1;
int32_t partyx = 0;
int32_t partyy = 0;
int32_t fieldx = 0;
int32_t fieldy = 0;
int32_t landlevel = 0;
int32_t dunglevel = 0;
int32_t lookx = 0;
int32_t looky = 0;
int32_t floorx = 0;
int32_t floory = 0;
int32_t moneypool[3] = {};
char charnum = -1;
char charselectnew = -1;
char charup = -1;
char monsterup = -1;
char combatround = 0;
char numenemy = 0;
char killmon = 0;
char up = 0;
char q[110] = {};
char head = 1;
char encountflag = 0;
char viewtype = 1;
char xydisplayflag = 0;
Boolean initems = 0;
Boolean inswap = 0;
Boolean inbooty = 0;
Boolean inshop = 0;
Boolean intemple = 0;
Boolean indung = 0;
Boolean incamp = 0;
Boolean shopavail = 0;
Boolean templeavail = 0;
Boolean spellcasting = 0;
short partycondition[10] = {};
struct character c[6] = {};
struct monster monster[100] = {};
char pos[6][2] = {};
char monpos[100][2] = {};
struct encount2 enc2 = {};
struct itemattr item = {};
struct itemattr allweapons[200] = {};
struct itemattr allarmor[200] = {};
struct itemattr allhelms[200] = {};
struct itemattr allmagic[200] = {};
struct itemattr allsupply[200] = {};
Rect lookrect = {};
struct tm tyme = {};

CGrafPtr FrontWindow(void) {
  return front_window;
}

} // extern "C"

using namespace realmz::presentation;

namespace {

constexpr uint32_t kUnchangedMessage = 0xA5A5A5A5U;
int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

using CombatConsumer = uint8_t (*)(
    RealmzSemanticInputSurface,
    uint32_t,
    uint32_t*);

struct CombatCase {
  uint32_t tag = 0;
  CombatConsumer consume = nullptr;
  uint32_t classic_message = 0;
};

uint8_t consume_center_combat_cursor_as_key(
    RealmzSemanticInputSurface surface,
    uint32_t tag,
    uint32_t* classic_message) {
  uint8_t absolute_x = 0;
  uint8_t absolute_y = 0;
  return RealmzConsumeSemanticCenterCombatCursorEvent(
      surface, tag, classic_message, &absolute_x, &absolute_y);
}

std::array<CombatCase, 17> combat_cases() {
  return {
      CombatCase{
          .tag = semantic_guard_combatant_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticGuardCombatantEvent,
          .classic_message = 0x00000567U,
      },
      CombatCase{
          .tag = semantic_finish_combatant_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticFinishCombatantEvent,
          .classic_message = 0x00000366U,
      },
      CombatCase{
          .tag = semantic_delay_combatant_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticDelayCombatantEvent,
          .classic_message = 0x00000264U,
      },
      CombatCase{
          .tag = semantic_center_active_combatant_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticCenterActiveCombatantEvent,
          .classic_message = 0x00000863U,
      },
      CombatCase{
          .tag = semantic_switch_weapon_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticSwitchWeaponEvent,
          .classic_message = 0x00000D77U,
      },
      CombatCase{
          .tag = semantic_cycle_combat_focus_tag(
              1,
              CombatFocusDirection::previous,
              REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticCycleCombatFocusEvent,
          .classic_message = 0x00002370U,
      },
      CombatCase{
          .tag = semantic_cycle_combat_focus_tag(
              1,
              CombatFocusDirection::next,
              REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticCycleCombatFocusEvent,
          .classic_message = 0x00002D6EU,
      },
      CombatCase{
          .tag = semantic_open_combat_items_tag(
              1, 1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticOpenCombatItemsEvent,
          .classic_message = 0x00002269U,
      },
      CombatCase{
          .tag = semantic_auto_combatant_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticAutoCombatantEvent,
          .classic_message = 0x00000061U,
      },
      CombatCase{
          .tag = semantic_show_combat_range_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticShowCombatRangeEvent,
          .classic_message = 0x00000F72U,
      },
      CombatCase{
          .tag = semantic_bandage_combatant_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticBandageCombatantEvent,
          .classic_message = 0x00000B62U,
      },
      CombatCase{
          .tag = semantic_undo_combatant_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticUndoCombatantEvent,
          .classic_message = 0x00002075U,
      },
      CombatCase{
          .tag = semantic_open_combat_spellbook_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticOpenCombatSpellbookEvent,
          .classic_message = 0x00000173U,
      },
      CombatCase{
          .tag = semantic_open_combat_targeting_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticOpenCombatTargetingEvent,
          .classic_message = 0x00001174U,
      },
      CombatCase{
          .tag = semantic_escape_combat_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticEscapeCombatEvent,
          .classic_message = 0x00000E65U,
      },
      CombatCase{
          .tag = semantic_open_combat_scroll_case_tag(
              1, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = RealmzConsumeSemanticOpenCombatScrollCaseEvent,
          .classic_message = 0x0000256CU,
      },
      CombatCase{
          .tag = semantic_center_combat_cursor_tag(
              1, {.x = 42, .y = 17}, REALMZ_SEMANTIC_INPUT_COMBAT),
          .consume = consume_center_combat_cursor_as_key,
          .classic_message = 0x00002E6DU,
      },
  };
}

void reset_legacy_globals() {
  RealmzInvalidateSemanticInputBoundary();
  look = nullptr;
  gWindow = nullptr;
  front_window = nullptr;
  currentscenario = 0;
  canundo = 0;
  fat = 0;
  incombat = 0;
  monsterturn = 0;
  nummon = 0;
  inspell = 0;
  lastshown = -1;
  partyx = 0;
  partyy = 0;
  fieldx = 0;
  fieldy = 0;
  lookx = 0;
  looky = 0;
  floorx = 0;
  floory = 0;
  lookrect = {};
  landlevel = 0;
  dunglevel = 0;
  std::memset(moneypool, 0, sizeof(moneypool));
  charnum = -1;
  charselectnew = -1;
  charup = -1;
  monsterup = -1;
  combatround = 0;
  numenemy = 0;
  killmon = 0;
  up = 0;
  std::memset(q, 0, sizeof(q));
  head = 1;
  encountflag = 0;
  viewtype = 1;
  xydisplayflag = 0;
  initems = 0;
  inswap = 0;
  inbooty = 0;
  inshop = 0;
  intemple = 0;
  indung = 0;
  incamp = 0;
  shopavail = 0;
  templeavail = 0;
  spellcasting = 0;
  std::memset(partycondition, 0, sizeof(partycondition));
  std::memset(c, 0, sizeof(c));
  std::memset(monster, 0, sizeof(monster));
  std::memset(pos, 0, sizeof(pos));
  std::memset(monpos, 0, sizeof(monpos));
  std::memset(&enc2, 0, sizeof(enc2));
  std::memset(&item, 0, sizeof(item));
  std::memset(allweapons, 0, sizeof(allweapons));
  std::memset(allarmor, 0, sizeof(allarmor));
  std::memset(allhelms, 0, sizeof(allhelms));
  std::memset(allmagic, 0, sizeof(allmagic));
  std::memset(allsupply, 0, sizeof(allsupply));
  std::memset(&tyme, 0, sizeof(tyme));
}

void seed_active_party_combatant() {
  reset_legacy_globals();
  look = &gameplay_port;
  gWindow = look;
  front_window = look;
  incombat = 1;
  canundo = 1;
  charnum = 1;
  charselectnew = 1;
  charup = 1;
  combatround = 3;
  fieldx = 20;
  fieldy = 30;
  lookrect = {.top = 0, .left = 0, .bottom = 416, .right = 480};
  c[1].stamina = 14;
  c[1].staminamax = 20;
  c[1].inbattle = 1;
  c[1].movement = 9;
  c[1].movementmax = 9;
  c[1].spellpoints = 8;
  c[1].maxspellsattacks = 2;
  q[0] = 1;
  lastshown = 1;
  c[1].armor[2] = 1;
  c[1].armor[13] = 1;
  c[1].numitems = 1;
  c[1].items[0] = {.id = 1, .charge = 1};
  allweapons[1].itemid = 1;
  allweapons[1].sp2 = 1101;
}

void complete_combat_scope() {
  RealmzBeginSemanticInputSurface(REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(RealmzCurrentSemanticInputSurface() ==
      REALMZ_SEMANTIC_INPUT_COMBAT);
  RealmzEndSemanticInputSurface();
  CHECK(RealmzCurrentSemanticInputSurface() ==
      REALMZ_SEMANTIC_INPUT_NONE);
}

void test_exact_combat_action_messages() {
  seed_active_party_combatant();

  for (const auto& action : combat_cases()) {
    CHECK(action.tag != 0);
    uint32_t output = kUnchangedMessage;
    complete_combat_scope();
    CHECK(action.consume(
        REALMZ_SEMANTIC_INPUT_COMBAT, action.tag, &output) != 0);
    CHECK(output == action.classic_message);
  }
}

void test_center_cursor_absolute_cell_and_viewport_projection() {
  constexpr CombatFieldCell queued_cell{.x = 42, .y = 17};
  seed_active_party_combatant();
  const uint32_t tag = semantic_center_combat_cursor_tag(
      1, queued_cell, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(tag == 0x4D012A11U);

  // Change the live camera after queueing. The absolute cell remains stable
  // and is accepted even though it no longer lies inside this valid viewport.
  fieldx = 70;
  fieldy = 60;
  complete_combat_scope();
  uint32_t output = kUnchangedMessage;
  uint8_t absolute_x = 0xA5;
  uint8_t absolute_y = 0x5A;
  CHECK(RealmzConsumeSemanticCenterCombatCursorEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      tag,
      &output,
      &absolute_x,
      &absolute_y));
  CHECK(output == 0x00002E6DU);
  CHECK(absolute_x == queued_cell.x);
  CHECK(absolute_y == queued_cell.y);

  output = kUnchangedMessage;
  absolute_x = 0xA5;
  absolute_y = 0x5A;
  CHECK(!RealmzConsumeSemanticCenterCombatCursorEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT,
      tag,
      &output,
      &absolute_x,
      &absolute_y));
  CHECK(output == kUnchangedMessage);
  CHECK(absolute_x == 0xA5);
  CHECK(absolute_y == 0x5A);

  for (int invalid_viewport = 0; invalid_viewport < 6;
       ++invalid_viewport) {
    seed_active_party_combatant();
    switch (invalid_viewport) {
      case 0:
        fieldx = -1;
        break;
      case 1:
        fieldy = -1;
        break;
      case 2:
        lookrect.right = 31;
        break;
      case 3:
        lookrect.bottom = 31;
        break;
      case 4:
        fieldx = 76;
        break;
      case 5:
        fieldy = 78;
        break;
    }
    complete_combat_scope();
    output = kUnchangedMessage;
    absolute_x = 0xA5;
    absolute_y = 0x5A;
    CHECK(!RealmzConsumeSemanticCenterCombatCursorEvent(
        REALMZ_SEMANTIC_INPUT_COMBAT,
        tag,
        &output,
        &absolute_x,
        &absolute_y));
    CHECK(output == kUnchangedMessage);
    CHECK(absolute_x == 0xA5);
    CHECK(absolute_y == 0x5A);
  }
}

void test_moved_combatant_delay_is_rejected() {
  seed_active_party_combatant();
  const uint32_t tag = semantic_delay_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(tag != 0);
  complete_combat_scope();

  c[1].movement = 8;
  uint32_t output = kUnchangedMessage;
  CHECK(RealmzConsumeSemanticDelayCombatantEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);
}

void test_bandage_without_classic_canundo_is_rejected() {
  seed_active_party_combatant();
  const uint32_t tag = semantic_bandage_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(tag != 0);
  complete_combat_scope();

  canundo = 0;
  uint32_t output = kUnchangedMessage;
  CHECK(RealmzConsumeSemanticBandageCombatantEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);

  // Re-enabling canundo cannot reuse the consumed top-level authorization.
  canundo = 1;
  CHECK(RealmzConsumeSemanticBandageCombatantEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);
}

void test_undo_without_classic_canundo_is_rejected() {
  seed_active_party_combatant();
  const uint32_t tag = semantic_undo_combatant_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(tag == 0x52550301U);
  complete_combat_scope();

  canundo = 0;
  uint32_t output = kUnchangedMessage;
  CHECK(RealmzConsumeSemanticUndoCombatantEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);

  canundo = 1;
  CHECK(RealmzConsumeSemanticUndoCombatantEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);
}

void test_unavailable_combat_spellbook_is_rejected_once() {
  seed_active_party_combatant();
  const uint32_t tag = semantic_open_combat_spellbook_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(tag == 0x53430301U);
  complete_combat_scope();

  spellcasting = 1;
  uint32_t output = kUnchangedMessage;
  CHECK(RealmzConsumeSemanticOpenCombatSpellbookEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);

  spellcasting = 0;
  CHECK(RealmzConsumeSemanticOpenCombatSpellbookEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);
}

void test_unavailable_combat_targeting_is_rejected_once() {
  seed_active_party_combatant();
  const uint32_t tag = semantic_open_combat_targeting_tag(
      1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(tag == 0x52540301U);
  complete_combat_scope();

  inspell = 1;
  uint32_t output = kUnchangedMessage;
  CHECK(RealmzConsumeSemanticOpenCombatTargetingEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);

  inspell = 0;
  CHECK(RealmzConsumeSemanticOpenCombatTargetingEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);
}

void test_unavailable_combat_scroll_case_is_rejected_once() {
  for (int rejection = 0; rejection < 4; ++rejection) {
    seed_active_party_combatant();
    const uint32_t tag = semantic_open_combat_scroll_case_tag(
        1, REALMZ_SEMANTIC_INPUT_COMBAT);
    CHECK(tag == 0x55530301U);
    complete_combat_scope();

    switch (rejection) {
      case 0:
        q[0] = 0;
        break;
      case 1:
        inspell = 1;
        break;
      case 2:
        c[1].stamina = 0;
        break;
      case 3:
        c[1].armor[13] = 0;
        break;
    }
    uint32_t output = kUnchangedMessage;
    CHECK(RealmzConsumeSemanticOpenCombatScrollCaseEvent(
        REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
    CHECK(output == kUnchangedMessage);

    // Repairing live state cannot reuse a consumed top-level authorization.
    q[0] = 1;
    inspell = 0;
    c[1].stamina = 14;
    c[1].armor[13] = 1;
    CHECK(RealmzConsumeSemanticOpenCombatScrollCaseEvent(
        REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
    CHECK(output == kUnchangedMessage);
  }
}

void test_stale_acting_combatant_is_rejected() {
  for (const auto& action : combat_cases()) {
    seed_active_party_combatant();
    complete_combat_scope();

    charup = 0;
    uint32_t output = kUnchangedMessage;
    CHECK(action.consume(
        REALMZ_SEMANTIC_INPUT_COMBAT, action.tag, &output) == 0);
    CHECK(output == kUnchangedMessage);
  }
}

void test_non_gameplay_front_window_is_rejected() {
  for (const auto& action : combat_cases()) {
    seed_active_party_combatant();
    complete_combat_scope();

    front_window = &overlay_port;
    uint32_t output = kUnchangedMessage;
    CHECK(action.consume(
        REALMZ_SEMANTIC_INPUT_COMBAT, action.tag, &output) == 0);
    CHECK(output == kUnchangedMessage);
  }
}

void test_stale_selected_member_is_rejected() {
  seed_active_party_combatant();
  const uint32_t tag = semantic_open_combat_items_tag(
      1, 1, REALMZ_SEMANTIC_INPUT_COMBAT);
  CHECK(tag != 0);
  complete_combat_scope();

  charselectnew = 0;
  uint32_t output = kUnchangedMessage;
  CHECK(RealmzConsumeSemanticOpenCombatItemsEvent(
      REALMZ_SEMANTIC_INPUT_COMBAT, tag, &output) == 0);
  CHECK(output == kUnchangedMessage);
}

} // namespace

int main() {
  try {
    test_exact_combat_action_messages();
    test_center_cursor_absolute_cell_and_viewport_projection();
    test_moved_combatant_delay_is_rejected();
    test_bandage_without_classic_canundo_is_rejected();
    test_undo_without_classic_canundo_is_rejected();
    test_unavailable_combat_spellbook_is_rejected_once();
    test_unavailable_combat_targeting_is_rejected_once();
    test_unavailable_combat_scroll_case_is_rejected_once();
    test_stale_acting_combatant_is_rejected();
    test_non_gameplay_front_window_is_rejected();
    test_stale_selected_member_is_rejected();
    reset_legacy_globals();
    std::cout << "Semantic combat legacy-adapter checks passed: "
              << checks_run << '\n';
    return 0;
  } catch (const std::exception& error) {
    reset_legacy_globals();
    std::cerr << "Semantic combat legacy-adapter test failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
