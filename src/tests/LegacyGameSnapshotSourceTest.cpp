#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "presentation/LegacyGameSnapshotSource.hpp"

extern "C" {
#include "realmz_orig/structs.h"

short currentscenario = 0;
short fat = 0;
short incombat = 0;
short monsterturn = 0;
short canundo = 0;
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
}

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  checks_run++;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

void reset_legacy_state() {
  currentscenario = 0;
  fat = 0;
  incombat = 0;
  monsterturn = 0;
  canundo = 0;
  nummon = 0;
  partyx = partyy = landlevel = dunglevel = 0;
  std::memset(moneypool, 0, sizeof(moneypool));
  charnum = -1;
  charselectnew = charup = monsterup = -1;
  combatround = 0;
  head = 1;
  encountflag = 0;
  viewtype = 1;
  initems = inswap = inbooty = inshop = intemple = indung = 0;
  std::memset(c, 0, sizeof(c));
  std::memset(monster, 0, sizeof(monster));
  std::memset(pos, 0, sizeof(pos));
  std::memset(monpos, 0, sizeof(monpos));
  std::memset(&enc2, 0, sizeof(enc2));
}

void set_name(char* destination, std::size_t capacity, const char* name) {
  std::strncpy(destination, name, capacity - 1);
  destination[capacity - 1] = '\0';
}

void seed_party() {
  charnum = 1;
  charselectnew = 1;
  currentscenario = 10;
  fat = 7;
  moneypool[0] = 123;
  moneypool[1] = 45;
  moneypool[2] = 6;
  partyx = 37;
  partyy = 28;
  landlevel = 4;
  dunglevel = 2;
  head = 2;
  indung = 1;

  set_name(c[0].name, sizeof(c[0].name), "Arin");
  c[0].level = 6;
  c[0].race = 2;
  c[0].caste = 3;
  c[0].pictid = 260;
  c[0].iconid = 505;
  c[0].stamina = 18;
  c[0].staminamax = 24;
  c[0].spellpoints = 9;
  c[0].spellpointsmax = 12;
  c[0].ac = 7;
  c[0].movement = 4;
  c[0].movementmax = 9;
  c[0].condition[9] = 3;
  c[0].inbattle = 1;

  set_name(c[1].name, sizeof(c[1].name), "Bryn");
  c[1].level = 4;
  c[1].stamina = 0;
  c[1].staminamax = 19;
  c[1].spellpointsmax = 5;
  c[1].load = 31;
  c[1].loadmax = 80;
  c[1].numitems = 2;
  c[1].items[0] = {.id = 901, .equip = 1, .ident = 1, .charge = 4};
  c[1].items[1] = {.id = 902, .equip = 0, .ident = 0, .charge = -1};
}

void test_party_world_and_inventory_capture() {
  reset_legacy_state();
  seed_party();
  initems = 1;

  LegacyGameSnapshotSource source;
  const auto snapshot = source.capture();

  CHECK(snapshot.screen == ScreenContext::inventory);
  CHECK(snapshot.scenario_id == 10);
  CHECK(snapshot.party.members.size() == 2);
  CHECK(snapshot.party.selected_member == 1);
  CHECK(snapshot.party.members[0].name == "Arin");
  CHECK(snapshot.party.members[0].conditions.size() == 1);
  CHECK(snapshot.party.members[0].conditions[0] == 9);
  CHECK(snapshot.party.members[0].conscious);
  CHECK(!snapshot.party.members[1].conscious);
  CHECK(snapshot.party.pooled_money[0] == 123);
  CHECK(snapshot.party.fatigue == 7);
  CHECK(snapshot.world.presentation == WorldPresentation::dungeon_first_person);
  CHECK(snapshot.world.party_x == 37);
  CHECK(snapshot.world.party_y == 28);
  CHECK(snapshot.world.facing == Facing::east);
  CHECK(snapshot.inventory.has_value());
  CHECK(snapshot.inventory->owner == 1);
  CHECK(snapshot.inventory->items.size() == 2);
  CHECK(snapshot.inventory->items[0].item_id == 901);
  CHECK(snapshot.inventory->items[0].equipped);
  CHECK(snapshot.inventory->items[0].identified);
  CHECK(snapshot.inventory->items[0].charges == 4);
  CHECK(snapshot.inventory->carried_weight == 31);
  CHECK(snapshot.inventory->maximum_weight == 80);

  set_name(c[0].name, sizeof(c[0].name), "Mutated");
  c[1].items[0].id = 999;
  CHECK(snapshot.party.members[0].name == "Arin");
  CHECK(snapshot.inventory->items[0].item_id == 901);
}

void test_combat_capture() {
  reset_legacy_state();
  seed_party();
  incombat = 1;
  canundo = 1;
  combatround = 5;
  charup = 0;
  monsterup = 1;
  pos[0][0] = 2;
  pos[0][1] = 3;
  pos[1][0] = 4;
  pos[1][1] = 5;
  nummon = 2;
  set_name(monster[0].monname, sizeof(monster[0].monname), "Rat");
  monster[0].stamina = 3;
  monster[0].staminamax = 3;
  monpos[0][0] = 8;
  monpos[0][1] = 9;
  set_name(monster[1].monname, sizeof(monster[1].monname), "Guard");
  monster[1].traiter = 1;
  monster[1].stamina = 7;
  monster[1].staminamax = 10;
  monpos[1][0] = 10;
  monpos[1][1] = 11;

  LegacyGameSnapshotSource source;
  auto snapshot = source.capture();
  CHECK(snapshot.screen == ScreenContext::combat);
  CHECK(snapshot.combat.has_value());
  CHECK(snapshot.combat->round == 5);
  CHECK(snapshot.combat->bandage_available);
  CHECK(snapshot.combat->undo_available);
  CHECK(snapshot.combat->acting_combatant == 0);
  CHECK(snapshot.combat->combatants.size() == 4);
  CHECK(snapshot.combat->combatants[0].active);
  CHECK(!snapshot.combat->combatants[3].active);
  CHECK(snapshot.combat->combatants[2].kind == CombatantKind::monster);
  CHECK(snapshot.combat->combatants[3].kind == CombatantKind::ally);
  CHECK(snapshot.combat->combatants[3].name == "Guard");
  CHECK(snapshot.combat->combatants[3].cell_x == 10);

  canundo = 0;
  snapshot = source.capture();
  CHECK(!snapshot.combat->bandage_available);
  CHECK(!snapshot.combat->undo_available);

  monsterturn = 1;
  snapshot = source.capture();
  CHECK(snapshot.combat->acting_combatant == 11);
  CHECK(!snapshot.combat->combatants[0].active);
  CHECK(snapshot.combat->combatants[3].active);
}

void test_encounter_and_bounds() {
  reset_legacy_state();
  charnum = 100;
  charselectnew = 100;
  encountflag = 2;
  enc2.prompt = 321;
  enc2.canbackout = 1;

  LegacyGameSnapshotSource source;
  const auto snapshot = source.capture();
  CHECK(snapshot.party.members.size() == 6);
  CHECK(!snapshot.party.selected_member.has_value());
  CHECK(snapshot.screen == ScreenContext::encounter);
  CHECK(snapshot.encounter.has_value());
  CHECK(snapshot.encounter->active);
  CHECK(snapshot.encounter->encounter_id == 321);
  CHECK(snapshot.encounter->can_cancel);
}

} // namespace

int main() {
  try {
    test_party_world_and_inventory_capture();
    test_combat_capture();
    test_encounter_and_bounds();
    std::cout << "LegacyGameSnapshotSourceTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
