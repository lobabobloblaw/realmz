#include "LegacyGameSnapshotSource.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

extern "C" {
#include "realmz_orig/structs.h"

extern short currentscenario;
extern short canundo;
extern short fat;
extern short incombat;
extern short inspell;
extern short lastshown;
extern short monsterturn;
extern short nummon;
extern int32_t partyx;
extern int32_t partyy;
extern int32_t landlevel;
extern int32_t dunglevel;
extern int32_t moneypool[3];
extern char charnum;
extern char charselectnew;
extern char charup;
extern char q[110];
extern char up;
extern char monsterup;
extern char combatround;
extern char head;
extern char encountflag;
extern char viewtype;
extern Boolean initems;
extern Boolean inswap;
extern Boolean inbooty;
extern Boolean inshop;
extern Boolean intemple;
extern Boolean indung;
extern Boolean spellcasting;
extern struct character c[6];
extern struct itemattr allweapons[200];
extern struct itemattr allarmor[200];
extern struct itemattr allhelms[200];
extern struct itemattr allmagic[200];
extern struct itemattr allsupply[200];
extern struct monster monster[100];
extern char pos[6][2];
extern char monpos[100][2];
extern struct encount2 enc2;
}

namespace realmz::presentation {

namespace {

std::string bounded_legacy_string(const char* data, std::size_t capacity) {
  const auto end = std::find(data, data + capacity, '\0');
  return std::string(data, end);
}

Facing facing_from_legacy_head(char value) noexcept {
  switch (value) {
    case 1:
      return Facing::north;
    case 2:
      return Facing::east;
    case 3:
      return Facing::south;
    case 4:
      return Facing::west;
    default:
      return Facing::north;
  }
}

ScreenContext screen_context() noexcept {
  if (incombat) {
    return ScreenContext::combat;
  }
  if (initems || inswap || inbooty) {
    return ScreenContext::inventory;
  }
  if (inshop || intemple) {
    return ScreenContext::shop;
  }
  if (encountflag) {
    return ScreenContext::encounter;
  }
  if (indung) {
    return ScreenContext::dungeon;
  }
  return ScreenContext::exploration;
}

WorldPresentation world_presentation() noexcept {
  if (indung) {
    return (viewtype == 1)
        ? WorldPresentation::dungeon_first_person
        : WorldPresentation::dungeon_map;
  }
  return WorldPresentation::outdoor;
}

std::vector<int16_t> active_conditions(const short (&conditions)[40]) {
  std::vector<int16_t> result;
  for (std::size_t index = 0; index < std::size(conditions); ++index) {
    if (conditions[index] != 0) {
      result.emplace_back(static_cast<int16_t>(index));
    }
  }
  return result;
}

bool active_party_actor_can_cast(int party_count) noexcept {
  if (monsterturn || (charup < 0) || (charup >= party_count) ||
      (spellcasting != 0)) {
    return false;
  }

  const auto& actor = c[static_cast<int>(charup)];
  return (actor.condition[COND_CONFUSED] == 0) &&
      (actor.condition[COND_SILENCED] == 0) &&
      (actor.condition[COND_HELPLESS] == 0) &&
      (actor.condition[COND_STUPID] == 0) &&
      (actor.condition[COND_ANIMATED] == 0) &&
      (actor.spellpoints > 0) && (actor.stamina > 0) &&
      (actor.beenattacked == 0) &&
      (actor.spellsofar < actor.maxspellsattacks);
}

const itemattr* item_attributes_for_id(short raw_id) noexcept {
  const int signed_id = static_cast<int>(raw_id);
  const int item_id = signed_id < 0 ? -signed_id : signed_id;
  if (item_id >= 1000) {
    return nullptr;
  }

  const std::size_t index = static_cast<std::size_t>(item_id % 200);
  switch (item_id / 200) {
    case 0:
      return &allweapons[index];
    case 1:
      return &allarmor[index];
    case 2:
      return &allhelms[index];
    case 3:
      return &allmagic[index];
    case 4:
      return &allsupply[index];
    default:
      return nullptr;
  }
}

bool active_party_actor_can_target(int party_count) noexcept {
  const int actor_index = static_cast<int>(charup);
  const int queue_index = static_cast<int>(up);
  if (monsterturn || (actor_index < 0) || (actor_index >= party_count) ||
      (queue_index < 0) || (queue_index >= 110) ||
      (static_cast<int>(q[queue_index]) != actor_index) ||
      (lastshown != actor_index) || (inspell != 0)) {
    return false;
  }

  const auto& actor = c[actor_index];
  const short source_id = actor.armor[actor.toggle ? 15 : 2];
  const auto* attributes = item_attributes_for_id(source_id);
  if (!attributes || (attributes->sp2 <= 1100)) {
    return false;
  }

  const int item_count = std::clamp<int>(actor.numitems, 0, 30);
  const auto first_matching_item = std::find_if(
      actor.items,
      actor.items + item_count,
      [attributes](const item& candidate) {
        return candidate.id == attributes->itemid;
      });
  return (first_matching_item != actor.items + item_count) &&
      (first_matching_item->charge != 0);
}

bool active_party_actor_can_use_scroll(int party_count) noexcept {
  const int actor_index = static_cast<int>(charup);
  const int queue_index = static_cast<int>(up);
  if (monsterturn || (actor_index < 0) || (actor_index >= party_count) ||
      (queue_index < 0) || (queue_index >= 110) ||
      (static_cast<int>(q[queue_index]) != actor_index) || (inspell != 0)) {
    return false;
  }

  const auto& actor = c[actor_index];
  return (actor.stamina > 0) && (actor.armor[13] != 0);
}

} // namespace

GameSnapshot LegacyGameSnapshotSource::capture() const {
  GameSnapshot snapshot;
  snapshot.screen = screen_context();
  snapshot.scenario_id = currentscenario;

  const int party_count = std::clamp<int>(static_cast<int>(charnum) + 1, 0, 6);
  const int selected_index =
      ((charselectnew >= 0) && (charselectnew < party_count))
      ? static_cast<int>(charselectnew)
      : -1;
  snapshot.party.members.reserve(static_cast<std::size_t>(party_count));
  for (int index = 0; index < party_count; ++index) {
    const auto& legacy = c[index];
    snapshot.party.members.emplace_back(PartyMemberView{
        .id = static_cast<PartyMemberId>(index),
        .name = bounded_legacy_string(legacy.name, std::size(legacy.name)),
        .level = legacy.level,
        .race_id = legacy.race,
        .caste_id = legacy.caste,
        .portrait_id = legacy.pictid,
        .tactical_id = legacy.iconid,
        .stamina = {legacy.stamina, legacy.staminamax},
        .spell_points = {legacy.spellpoints, legacy.spellpointsmax},
        .armor_class = legacy.ac,
        .movement = legacy.movement,
        .movement_maximum = legacy.movementmax,
        .conditions = active_conditions(legacy.condition),
        .selected = index == selected_index,
        .conscious = legacy.stamina > 0,
    });
  }
  if (selected_index >= 0) {
    snapshot.party.selected_member =
        static_cast<PartyMemberId>(selected_index);
  }
  snapshot.party.pooled_money = {moneypool[0], moneypool[1], moneypool[2]};
  snapshot.party.fatigue = fat;

  snapshot.world.presentation = world_presentation();
  snapshot.world.party_x = partyx;
  snapshot.world.party_y = partyy;
  snapshot.world.land_level = landlevel;
  snapshot.world.dungeon_level = dunglevel;
  snapshot.world.facing = facing_from_legacy_head(head);

  if (incombat) {
    CombatView combat;
    combat.active = true;
    combat.bandage_available = canundo != 0;
    combat.undo_available = canundo != 0;
    combat.cast_spell_available = active_party_actor_can_cast(party_count);
    combat.target_available = active_party_actor_can_target(party_count);
    combat.use_scroll_available =
        active_party_actor_can_use_scroll(party_count);
    combat.round = static_cast<int16_t>(combatround);
    if (!monsterturn && (charup >= 0) && (charup < party_count)) {
      combat.acting_combatant = static_cast<CombatantId>(charup);
    } else if (monsterturn && (monsterup >= 0) && (monsterup < nummon)) {
      combat.acting_combatant =
          static_cast<CombatantId>(10 + monsterup);
    }

    combat.combatants.reserve(
        static_cast<std::size_t>(party_count + std::clamp<int>(nummon, 0, 100)));
    for (int index = 0; index < party_count; ++index) {
      const auto& legacy = c[index];
      combat.combatants.emplace_back(CombatantView{
          .id = static_cast<CombatantId>(index),
          .kind = CombatantKind::party_member,
          .name = bounded_legacy_string(legacy.name, std::size(legacy.name)),
          .cell_x = static_cast<int16_t>(pos[index][0]),
          .cell_y = static_cast<int16_t>(pos[index][1]),
          .stamina = {legacy.stamina, legacy.staminamax},
          .conditions = active_conditions(legacy.condition),
          .active = !monsterturn && (charup == index),
          .targetable = legacy.inbattle && (legacy.stamina > 0),
      });
    }
    const int monster_count = std::clamp<int>(nummon, 0, 100);
    for (int index = 0; index < monster_count; ++index) {
      const auto& legacy = monster[index];
      combat.combatants.emplace_back(CombatantView{
          .id = static_cast<CombatantId>(10 + index),
          .kind = legacy.traiter
              ? CombatantKind::ally
              : CombatantKind::monster,
          .name = bounded_legacy_string(legacy.monname, std::size(legacy.monname)),
          .cell_x = static_cast<int16_t>(monpos[index][0]),
          .cell_y = static_cast<int16_t>(monpos[index][1]),
          .stamina = {legacy.stamina, legacy.staminamax},
          .active = monsterturn && (monsterup == index),
          .targetable = legacy.stamina > 0,
      });
    }
    snapshot.combat = std::move(combat);
  }

  if (initems || inswap || inbooty) {
    InventoryView inventory;
    if (selected_index >= 0) {
      inventory.owner = static_cast<PartyMemberId>(selected_index);
      const auto& owner = c[selected_index];
      inventory.carried_weight = owner.load;
      inventory.maximum_weight = owner.loadmax;
      const int item_count = std::clamp<int>(owner.numitems, 0, 30);
      inventory.items.reserve(static_cast<std::size_t>(item_count));
      for (int item_index = 0; item_index < item_count; ++item_index) {
        const auto& legacy_item = owner.items[item_index];
        inventory.items.emplace_back(InventoryItemView{
            .instance_id =
                (static_cast<ItemInstanceId>(selected_index) << 16) |
                static_cast<ItemInstanceId>(item_index),
            .item_id = legacy_item.id,
            .slot = static_cast<int16_t>(item_index),
            .quantity = 1,
            .charges = legacy_item.charge,
            .equipped = legacy_item.equip != 0,
            .identified = legacy_item.ident != 0,
        });
      }
    }
    snapshot.inventory = std::move(inventory);
  }

  if (encountflag) {
    EncounterView encounter;
    encounter.active = true;
    encounter.encounter_id = enc2.prompt;
    encounter.can_cancel = enc2.canbackout != 0;
    snapshot.encounter = std::move(encounter);
  }

  return snapshot;
}

} // namespace realmz::presentation
