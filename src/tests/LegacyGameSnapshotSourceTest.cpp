#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "presentation/LegacyGameSnapshotSource.hpp"
#include "presentation/LegacyTorchSource.h"

extern "C" {
#include "realmz_orig/structs.h"

short currentscenario = 0;
short fat = 0;
short incombat = 0;
short inspell = 0;
short lastshown = -1;
short monsterturn = 0;
short canundo = 0;
short nummon = 0;
int32_t partyx = 0;
int32_t partyy = 0;
int32_t fieldx = 0;
int32_t fieldy = 0;
int32_t landlevel = 0;
int32_t dunglevel = 0;
int32_t moneypool[3] = {};
char charnum = -1;
char charselectnew = -1;
char charup = -1;
char monsterup = -1;
char combatround = 0;
char q[110] = {};
char up = 0;
char head = 1;
char encountflag = 0;
char viewtype = 1;
Boolean initems = 0;
Boolean inswap = 0;
Boolean inbooty = 0;
Boolean inshop = 0;
Boolean intemple = 0;
Boolean indung = 0;
Boolean incamp = 0;
Boolean shopavail = 0;
Boolean templeavail = 0;
Boolean canshop = 0;
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
  inspell = 0;
  lastshown = -1;
  monsterturn = 0;
  canundo = 0;
  nummon = 0;
  partyx = partyy = fieldx = fieldy = landlevel = dunglevel = 0;
  lookrect = {};
  std::memset(moneypool, 0, sizeof(moneypool));
  charnum = -1;
  charselectnew = charup = monsterup = -1;
  combatround = 0;
  std::memset(q, 0, sizeof(q));
  up = 0;
  head = 1;
  encountflag = 0;
  viewtype = 1;
  initems = inswap = inbooty = inshop = intemple = indung = incamp = 0;
  shopavail = templeavail = canshop = 0;
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
  c[0].normattacks = 3;
  c[0].attackbonus = 2;
  c[0].movement = 4;
  c[0].movementmax = 9;
  c[0].condition[9] = 3;
  c[0].inbattle = 1;

  set_name(c[1].name, sizeof(c[1].name), "Bryn");
  c[1].level = 4;
  c[1].stamina = 0;
  c[1].staminamax = 19;
  c[1].spellpointsmax = 5;
  c[1].normattacks = -4;
  c[1].attackbonus = 1;
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
  CHECK(snapshot.party.members[0].normal_attacks == 3);
  CHECK(snapshot.party.members[0].attack_bonus == 2);
  CHECK(snapshot.party.members[1].normal_attacks == -4);
  CHECK(snapshot.party.members[1].attack_bonus == 1);
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
  c[0].normattacks = 12;
  c[0].attackbonus = -7;
  c[1].normattacks = 20;
  c[1].attackbonus = 9;
  c[1].items[0].id = 999;
  CHECK(snapshot.party.members[0].name == "Arin");
  CHECK(snapshot.party.members[0].normal_attacks == 3);
  CHECK(snapshot.party.members[0].attack_bonus == 2);
  CHECK(snapshot.party.members[1].normal_attacks == -4);
  CHECK(snapshot.party.members[1].attack_bonus == 1);
  CHECK(snapshot.inventory->items[0].item_id == 901);
}

void test_noncombat_scroll_case_eligibility_capture() {
  reset_legacy_state();
  seed_party();
  LegacyGameSnapshotSource source;

  // Classic opens the chooser for an equipped case even when all five slots
  // are empty. Contents and every subsequent choice remain Classic-owned.
  charselectnew = 0;
  c[0].stamina = 18;
  c[0].armor[13] = 1;
  std::memset(c[0].scrollcase, 0, sizeof(c[0].scrollcase));
  auto snapshot = source.capture();
  CHECK(snapshot.party.selected_member == 0);
  CHECK(snapshot.party.members[0].use_scroll_available);

  std::memset(c[0].scrollcase, 0x7F, sizeof(c[0].scrollcase));
  snapshot = source.capture();
  CHECK(snapshot.party.members[0].use_scroll_available);

  inspell = 1;
  snapshot = source.capture();
  CHECK(!snapshot.party.members[0].use_scroll_available);
  inspell = 0;

  c[0].stamina = 0;
  snapshot = source.capture();
  CHECK(!snapshot.party.members[0].use_scroll_available);
  c[0].stamina = 18;

  c[0].armor[13] = 0;
  snapshot = source.capture();
  CHECK(!snapshot.party.members[0].use_scroll_available);
  c[0].armor[13] = -1;
  snapshot = source.capture();
  CHECK(snapshot.party.members[0].use_scroll_available);

  // The DTO carries eligibility per member; selecting another member does not
  // mutate or silently transfer the first member's projection.
  charselectnew = 1;
  c[1].stamina = 1;
  c[1].armor[13] = 2;
  std::memset(c[1].scrollcase, 0, sizeof(c[1].scrollcase));
  snapshot = source.capture();
  CHECK(snapshot.party.selected_member == 1);
  CHECK(snapshot.party.members[0].use_scroll_available);
  CHECK(snapshot.party.members[1].use_scroll_available);
}

void test_camp_state_capture_is_value_only() {
  reset_legacy_state();
  LegacyGameSnapshotSource source;

  CHECK(!source.capture().world.in_camp);
  incamp = 1;
  const auto camp_snapshot = source.capture();
  CHECK(camp_snapshot.world.in_camp);

  incamp = 0;
  const auto travel_snapshot = source.capture();
  CHECK(!travel_snapshot.world.in_camp);
  CHECK(camp_snapshot.world.in_camp);
}

void test_search_state_capture_is_value_only() {
  reset_legacy_state();
  LegacyGameSnapshotSource source;

  CHECK(!source.capture().world.searching);

  partycondition[PARTY_COND_SEARCH] = -1;
  const auto persistent_search_snapshot = source.capture();
  CHECK(persistent_search_snapshot.world.searching);

  partycondition[PARTY_COND_SEARCH] = 7;
  CHECK(source.capture().world.searching);

  partycondition[PARTY_COND_SEARCH] = -9;
  CHECK(source.capture().world.searching);

  partycondition[PARTY_COND_SEARCH] = 0;
  CHECK(!source.capture().world.searching);
  CHECK(persistent_search_snapshot.world.searching);
}

void test_first_usable_torch_source_capture_is_exact_and_value_only() {
  reset_legacy_state();
  LegacyGameSnapshotSource source;

  CHECK(!source.capture().world.usable_torch_source);

  charnum = 1;
  c[0].numitems = 2;
  c[0].items[0] = {.id = -805, .charge = 9};
  c[0].items[1] = {.id = 804, .charge = 9};
  c[1].numitems = 3;
  c[1].items[0] = {.id = 1, .charge = 1};
  c[1].items[1] = {.id = 2, .charge = 1};
  c[1].items[2] = {.id = 805, .charge = 4};
  const character party_before_capture[2] = {c[0], c[1]};

  const auto usable = source.capture();
  CHECK(usable.world.usable_torch_source ==
      (TorchSource{.member = 1, .slot = 2}));
  CHECK(std::memcmp(c, party_before_capture, sizeof(party_before_capture)) == 0);
  CHECK(RealmzCurrentFirstUsableTorchSourceMatches(1, 2));
  CHECK(!RealmzCurrentFirstUsableTorchSourceMatches(0, 0));

  c[1].items[2].charge = 3;
  CHECK(usable.world.usable_torch_source ==
      (TorchSource{.member = 1, .slot = 2}));

  // Classic stops at the first exact +805 even when it cannot spend a charge.
  // A later charged match must therefore remain unreachable.
  c[0].items[1] = {.id = 805, .charge = 0};
  CHECK(!source.capture().world.usable_torch_source);
  CHECK(!RealmzCurrentFirstUsableTorchSourceMatches(1, 2));
  c[0].items[1].charge = -1;
  CHECK(!source.capture().world.usable_torch_source);
  c[0].items[1].charge = 1;
  CHECK(source.capture().world.usable_torch_source ==
      (TorchSource{.member = 0, .slot = 1}));

  // Bounds are fail-closed instead of inheriting Classic's out-of-bounds
  // behavior. Slot 29 remains a valid source.
  c[0].numitems = 30;
  std::memset(c[0].items, 0, sizeof(c[0].items));
  c[0].items[29] = {.id = 805, .charge = 1};
  c[1].numitems = 0;
  CHECK(source.capture().world.usable_torch_source ==
      (TorchSource{.member = 0, .slot = 29}));
  c[0].numitems = 31;
  CHECK(!source.capture().world.usable_torch_source);
  c[0].numitems = -1;
  CHECK(!source.capture().world.usable_torch_source);
  c[0].numitems = 0;
  charnum = 6;
  CHECK(!source.capture().world.usable_torch_source);
}

void test_contextual_world_entry_mode_is_exact_and_value_only() {
  reset_legacy_state();
  LegacyGameSnapshotSource source;

  const auto outdoor_encounter = source.capture();
  CHECK(outdoor_encounter.screen == ScreenContext::exploration);
  CHECK(outdoor_encounter.world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::encounter);

  templeavail = 1;
  const auto outdoor_temple = source.capture();
  CHECK(outdoor_temple.world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::temple);

  shopavail = 1;
  const auto outdoor_shop = source.capture();
  CHECK(outdoor_shop.world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::shop);

  // Shop is Classic buttonchoice's authoritative first branch. Both flags are
  // normally true after loading a shop, so this priority is not an edge case.
  CHECK(shopavail && templeavail);
  CHECK(source.capture().world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::shop);

  shopavail = 0;
  templeavail = 0;
  indung = 1;
  CHECK(source.capture().screen == ScreenContext::dungeon);
  CHECK(source.capture().world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::encounter);
  templeavail = 1;
  CHECK(source.capture().world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::temple);
  shopavail = 1;
  CHECK(source.capture().world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::shop);

  // canshop affects only Classic artwork. It is serialized legacy state but is
  // not consulted by either keyboard gates or buttonchoice dispatch.
  shopavail = 0;
  templeavail = 0;
  canshop = 1;
  CHECK(source.capture().world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::encounter);

  incamp = 1;
  CHECK(source.capture().world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::unavailable);
  incamp = 0;

  const auto require_nested_unavailable = [&] {
    CHECK(source.capture().world.contextual_world_entry_mode ==
        ContextualWorldEntryMode::unavailable);
  };
  initems = 1;
  require_nested_unavailable();
  initems = 0;
  inswap = 1;
  require_nested_unavailable();
  inswap = 0;
  inbooty = 1;
  require_nested_unavailable();
  inbooty = 0;
  inshop = 1;
  require_nested_unavailable();
  inshop = 0;
  intemple = 1;
  require_nested_unavailable();
  intemple = 0;
  encountflag = 1;
  require_nested_unavailable();
  encountflag = 0;
  incombat = 1;
  require_nested_unavailable();

  // Earlier captures remain detached values after Classic globals change.
  CHECK(outdoor_encounter.world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::encounter);
  CHECK(outdoor_temple.world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::temple);
  CHECK(outdoor_shop.world.contextual_world_entry_mode ==
      ContextualWorldEntryMode::shop);
}

void test_combat_capture() {
  reset_legacy_state();
  seed_party();
  incombat = 1;
  canundo = 1;
  combatround = 5;
  fieldx = 12;
  fieldy = 34;
  lookrect = {.top = 8, .left = 16, .bottom = 424, .right = 496};
  charup = 0;
  c[0].maxspellsattacks = 2;
  q[0] = 0;
  up = 0;
  lastshown = 0;
  c[0].armor[2] = 5;
  c[0].armor[13] = 1;
  c[0].numitems = 1;
  c[0].items[0] = {.id = 77, .charge = 1};
  allweapons[5].itemid = 77;
  allweapons[5].sp2 = 1101;
  item.itemid = 909;
  item.sp2 = 2222;
  item.charge = -17;
  const itemattr item_before_capture = item;
  const int32_t fieldx_before_capture = fieldx;
  const int32_t fieldy_before_capture = fieldy;
  const Rect lookrect_before_capture = lookrect;
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
  CHECK(snapshot.combat->cast_spell_available);
  CHECK(snapshot.combat->target_available);
  CHECK(snapshot.combat->use_scroll_available);
  CHECK(snapshot.combat->field_origin_x == 12);
  CHECK(snapshot.combat->field_origin_y == 34);
  CHECK(snapshot.combat->visible_columns == 15);
  CHECK(snapshot.combat->visible_rows == 13);
  CHECK(fieldx == fieldx_before_capture);
  CHECK(fieldy == fieldy_before_capture);
  CHECK(std::memcmp(
            &lookrect, &lookrect_before_capture, sizeof(lookrect)) == 0);
  CHECK(std::memcmp(&item, &item_before_capture, sizeof(item)) == 0);
  CHECK(snapshot.combat->acting_combatant == 0);
  CHECK(snapshot.combat->combatants.size() == 4);
  CHECK(snapshot.combat->combatants[0].active);
  CHECK(!snapshot.combat->combatants[3].active);
  CHECK(snapshot.combat->combatants[2].kind == CombatantKind::monster);
  CHECK(snapshot.combat->combatants[3].kind == CombatantKind::ally);
  CHECK(snapshot.combat->combatants[3].name == "Guard");
  CHECK(snapshot.combat->combatants[3].cell_x == 10);

  fieldx = 44;
  fieldy = 55;
  lookrect = {.top = 100, .left = 200, .bottom = 99, .right = 199};
  auto invalid_geometry = source.capture();
  CHECK(invalid_geometry.combat->field_origin_x == 44);
  CHECK(invalid_geometry.combat->field_origin_y == 55);
  CHECK(invalid_geometry.combat->visible_columns == 0);
  CHECK(invalid_geometry.combat->visible_rows == 0);
  CHECK(snapshot.combat->field_origin_x == 12);
  CHECK(snapshot.combat->field_origin_y == 34);
  CHECK(snapshot.combat->visible_columns == 15);
  CHECK(snapshot.combat->visible_rows == 13);

  lookrect = {.top = 10, .left = 20, .bottom = 41, .right = 51};
  invalid_geometry = source.capture();
  CHECK(invalid_geometry.combat->visible_columns == 0);
  CHECK(invalid_geometry.combat->visible_rows == 0);

  fieldx = fieldx_before_capture;
  fieldy = fieldy_before_capture;
  lookrect = lookrect_before_capture;

  canundo = 0;
  snapshot = source.capture();
  CHECK(!snapshot.combat->bandage_available);
  CHECK(!snapshot.combat->undo_available);
  CHECK(snapshot.combat->cast_spell_available);
  CHECK(snapshot.combat->target_available);
  CHECK(snapshot.combat->use_scroll_available);

  spellcasting = 1;
  snapshot = source.capture();
  CHECK(!snapshot.combat->cast_spell_available);
  spellcasting = 0;

  constexpr std::array disabling_conditions{
      COND_CONFUSED,
      COND_SILENCED,
      COND_HELPLESS,
      COND_STUPID,
      COND_ANIMATED,
  };
  for (const auto condition : disabling_conditions) {
    c[0].condition[condition] = 1;
    snapshot = source.capture();
    CHECK(!snapshot.combat->cast_spell_available);
    c[0].condition[condition] = 0;
  }

  c[0].spellpoints = 0;
  snapshot = source.capture();
  CHECK(!snapshot.combat->cast_spell_available);
  c[0].spellpoints = 9;

  c[0].stamina = 0;
  snapshot = source.capture();
  CHECK(!snapshot.combat->cast_spell_available);
  c[0].stamina = 18;

  c[0].beenattacked = 1;
  snapshot = source.capture();
  CHECK(!snapshot.combat->cast_spell_available);
  c[0].beenattacked = 0;

  c[0].spellsofar = c[0].maxspellsattacks;
  snapshot = source.capture();
  CHECK(!snapshot.combat->cast_spell_available);
  c[0].spellsofar = c[0].maxspellsattacks - 1;
  snapshot = source.capture();
  CHECK(snapshot.combat->cast_spell_available);

  charup = -1;
  snapshot = source.capture();
  CHECK(!snapshot.combat->cast_spell_available);
  CHECK(!snapshot.combat->target_available);
  charup = static_cast<char>(charnum + 1);
  snapshot = source.capture();
  CHECK(!snapshot.combat->cast_spell_available);
  CHECK(!snapshot.combat->target_available);
  charup = 0;

  const auto clear_target_attributes = [] {
    std::memset(allweapons, 0, sizeof(allweapons));
    std::memset(allarmor, 0, sizeof(allarmor));
    std::memset(allhelms, 0, sizeof(allhelms));
    std::memset(allmagic, 0, sizeof(allmagic));
    std::memset(allsupply, 0, sizeof(allsupply));
  };
  const auto configure_valid_target = [&](short source_id = 5) {
    clear_target_attributes();
    up = 0;
    q[0] = 0;
    lastshown = 0;
    inspell = 0;
    c[0].toggle = 0;
    c[0].armor[2] = source_id;
    c[0].armor[15] = 0;
    c[0].numitems = 1;
    std::memset(c[0].items, 0, sizeof(c[0].items));
    c[0].items[0] = {.id = 77, .charge = 1};
    allweapons[5].itemid = 77;
    allweapons[5].sp2 = 1101;
  };
  const auto capture_target_available = [&] {
    const auto captured = source.capture();
    CHECK(captured.combat.has_value());
    CHECK(std::memcmp(&item, &item_before_capture, sizeof(item)) == 0);
    return captured.combat->target_available;
  };

  configure_valid_target();
  q[0] = 1;
  CHECK(!capture_target_available());
  q[0] = 0;
  lastshown = 1;
  CHECK(!capture_target_available());
  lastshown = 0;
  inspell = 1;
  CHECK(!capture_target_available());
  inspell = 0;
  up = -1;
  CHECK(!capture_target_available());
  up = 110;
  CHECK(!capture_target_available());
  up = 0;

  struct AttributeCollectionCase {
    short source_id;
    itemattr* attributes;
  };
  const std::array attribute_collections{
      AttributeCollectionCase{0, allweapons},
      AttributeCollectionCase{200, allarmor},
      AttributeCollectionCase{400, allhelms},
      AttributeCollectionCase{600, allmagic},
      AttributeCollectionCase{800, allsupply},
  };
  for (const auto& collection : attribute_collections) {
    configure_valid_target(collection.source_id);
    clear_target_attributes();
    collection.attributes[0].itemid = 77;
    collection.attributes[0].sp2 = 1101;
    CHECK(capture_target_available());
  }

  configure_valid_target(200);
  clear_target_attributes();
  allarmor[0].itemid = 77;
  allarmor[0].sp2 = 1101;
  c[0].armor[2] = -200;
  CHECK(capture_target_available());

  configure_valid_target();
  c[0].armor[2] = 1000;
  c[0].armor[15] = 205;
  allarmor[5].itemid = 88;
  allarmor[5].sp2 = 1101;
  c[0].items[0] = {.id = 88, .charge = 1};
  c[0].toggle = 1;
  CHECK(capture_target_available());
  c[0].toggle = 0;
  CHECK(!capture_target_available());

  configure_valid_target();
  allweapons[5].sp2 = 1100;
  CHECK(!capture_target_available());
  allweapons[5].sp2 = 1101;
  c[0].numitems = 2;
  c[0].items[0] = {.id = 77, .charge = 0};
  c[0].items[1] = {.id = 77, .charge = 1};
  CHECK(!capture_target_available());
  c[0].items[0].charge = -1;
  CHECK(capture_target_available());
  c[0].items[0].id = 66;
  CHECK(capture_target_available());
  c[0].numitems = 1;
  CHECK(!capture_target_available());
  c[0].numitems = 30;
  std::memset(c[0].items, 0, sizeof(c[0].items));
  c[0].items[29] = {.id = 77, .charge = -3};
  CHECK(capture_target_available());
  c[0].numitems = 31;
  CHECK(capture_target_available());
  c[0].numitems = -1;
  CHECK(!capture_target_available());

  configure_valid_target();
  c[0].armor[2] = 1000;
  CHECK(!capture_target_available());
  c[0].armor[2] = -1000;
  CHECK(!capture_target_available());
  c[0].armor[2] = 5;
  allweapons[5].itemid = 0;
  CHECK(!capture_target_available());
  CHECK(std::memcmp(&item, &item_before_capture, sizeof(item)) == 0);

  configure_valid_target();

  const auto capture_use_scroll_available = [&] {
    const character actor_before_capture = c[0];
    const itemattr scratch_before_capture = item;
    const std::array<char, 110> queue_before_capture = [] {
      std::array<char, 110> copy{};
      std::memcpy(copy.data(), q, sizeof(q));
      return copy;
    }();
    const char selected_before_capture = charselectnew;
    const short lastshown_before_capture = lastshown;
    const short inspell_before_capture = inspell;
    const char up_before_capture = up;
    const auto captured = source.capture();
    CHECK(captured.combat.has_value());
    CHECK(std::memcmp(
              &c[0], &actor_before_capture, sizeof(actor_before_capture)) ==
        0);
    CHECK(std::memcmp(
              &item, &scratch_before_capture, sizeof(scratch_before_capture)) ==
        0);
    CHECK(std::memcmp(
              q, queue_before_capture.data(), sizeof(q)) == 0);
    CHECK(charselectnew == selected_before_capture);
    CHECK(lastshown == lastshown_before_capture);
    CHECK(inspell == inspell_before_capture);
    CHECK(up == up_before_capture);
    return captured.combat->use_scroll_available;
  };

  // The shell projects only the live case-opening gate. Scroll contents,
  // Target visibility, animation, and spellcasting gates remain Classic-owned.
  up = 0;
  q[0] = 0;
  charup = 0;
  monsterturn = 0;
  inspell = 0;
  c[0].stamina = 18;
  c[0].armor[13] = 1;
  std::memset(c[0].scrollcase, 0, sizeof(c[0].scrollcase));
  lastshown = -1;
  CHECK(capture_use_scroll_available());
  lastshown = 1;
  CHECK(capture_use_scroll_available());
  c[0].condition[COND_ANIMATED] = 1;
  CHECK(capture_use_scroll_available());
  c[0].condition[COND_CONFUSED] = 1;
  c[0].condition[COND_SILENCED] = 1;
  c[0].condition[COND_HELPLESS] = 1;
  c[0].condition[COND_STUPID] = 1;
  c[0].beenattacked = 1;
  c[0].spellpoints = 0;
  c[0].spellsofar = c[0].maxspellsattacks;
  spellcasting = 1;
  CHECK(capture_use_scroll_available());

  spellcasting = 0;
  c[0].condition[COND_ANIMATED] = 0;
  c[0].condition[COND_CONFUSED] = 0;
  c[0].condition[COND_SILENCED] = 0;
  c[0].condition[COND_HELPLESS] = 0;
  c[0].condition[COND_STUPID] = 0;
  c[0].beenattacked = 0;
  c[0].spellpoints = 9;
  c[0].spellsofar = 0;
  q[0] = 1;
  CHECK(!capture_use_scroll_available());
  q[0] = 0;
  up = -1;
  CHECK(!capture_use_scroll_available());
  up = 110;
  CHECK(!capture_use_scroll_available());
  up = 0;
  inspell = 1;
  CHECK(!capture_use_scroll_available());
  inspell = 0;
  c[0].stamina = 0;
  CHECK(!capture_use_scroll_available());
  c[0].stamina = 18;
  c[0].armor[13] = 0;
  CHECK(!capture_use_scroll_available());
  c[0].armor[13] = -1;
  CHECK(capture_use_scroll_available());
  c[0].armor[13] = 1;

  monsterturn = 1;
  snapshot = source.capture();
  CHECK(!snapshot.combat->cast_spell_available);
  CHECK(!snapshot.combat->target_available);
  CHECK(!snapshot.combat->use_scroll_available);
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
    test_noncombat_scroll_case_eligibility_capture();
    test_camp_state_capture_is_value_only();
    test_search_state_capture_is_value_only();
    test_first_usable_torch_source_capture_is_exact_and_value_only();
    test_contextual_world_entry_mode_is_exact_and_value_only();
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
