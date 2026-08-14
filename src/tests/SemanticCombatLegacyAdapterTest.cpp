#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "presentation/SemanticInputBoundary.h"

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
short fat = 0;
short incombat = 0;
short monsterturn = 0;
short nummon = 0;
int32_t partyx = 0;
int32_t partyy = 0;
int32_t landlevel = 0;
int32_t dunglevel = 0;
int32_t moneypool[3] = {};
char charnum = -1;
char charselectnew = -1;
char charup = -1;
char monsterup = -1;
char combatround = 0;
char head = 1;
char encountflag = 0;
char viewtype = 1;
Boolean initems = 0;
Boolean inswap = 0;
Boolean inbooty = 0;
Boolean inshop = 0;
Boolean intemple = 0;
Boolean indung = 0;
struct character c[6] = {};
struct monster monster[100] = {};
char pos[6][2] = {};
char monpos[100][2] = {};
struct encount2 enc2 = {};

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

std::array<CombatCase, 5> combat_cases() {
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
  };
}

void reset_legacy_globals() {
  RealmzInvalidateSemanticInputBoundary();
  look = nullptr;
  gWindow = nullptr;
  front_window = nullptr;
  currentscenario = 0;
  fat = 0;
  incombat = 0;
  monsterturn = 0;
  nummon = 0;
  partyx = 0;
  partyy = 0;
  landlevel = 0;
  dunglevel = 0;
  std::memset(moneypool, 0, sizeof(moneypool));
  charnum = -1;
  charselectnew = -1;
  charup = -1;
  monsterup = -1;
  combatround = 0;
  head = 1;
  encountflag = 0;
  viewtype = 1;
  initems = 0;
  inswap = 0;
  inbooty = 0;
  inshop = 0;
  intemple = 0;
  indung = 0;
  std::memset(c, 0, sizeof(c));
  std::memset(monster, 0, sizeof(monster));
  std::memset(pos, 0, sizeof(pos));
  std::memset(monpos, 0, sizeof(monpos));
  std::memset(&enc2, 0, sizeof(enc2));
}

void seed_active_party_combatant() {
  reset_legacy_globals();
  look = &gameplay_port;
  gWindow = look;
  front_window = look;
  incombat = 1;
  charnum = 1;
  charselectnew = 1;
  charup = 1;
  combatround = 3;
  c[1].stamina = 14;
  c[1].staminamax = 20;
  c[1].inbattle = 1;
  c[1].movement = 9;
  c[1].movementmax = 9;
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

} // namespace

int main() {
  try {
    test_exact_combat_action_messages();
    test_moved_combatant_delay_is_rejected();
    test_stale_acting_combatant_is_rejected();
    test_non_gameplay_front_window_is_rejected();
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
