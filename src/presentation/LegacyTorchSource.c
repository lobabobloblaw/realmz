#include "LegacyTorchSource.h"

#include "realmz_orig/structs.h"

extern char charnum;
extern struct character c[6];

uint8_t RealmzFindFirstUsableTorchSource(RealmzTorchSource* source) {
  const int maximum_member = (int)charnum;
  int member;

  if (source == 0) {
    return 0;
  }
  source->member = 0;
  source->slot = 0;
  if ((maximum_member < 0) || (maximum_member > 5)) {
    return 0;
  }

  for (member = 0; member <= maximum_member; ++member) {
    const int item_count = (int)c[member].numitems;
    int slot;
    if ((item_count < 0) || (item_count > 30)) {
      return 0;
    }
    for (slot = 0; slot < item_count; ++slot) {
      if (c[member].items[slot].id == 805) {
        if (c[member].items[slot].charge <= 0) {
          return 0;
        }
        source->member = (uint8_t)member;
        source->slot = (uint8_t)slot;
        return 1;
      }
    }
  }
  return 0;
}

uint8_t RealmzCurrentFirstUsableTorchSourceMatches(
    uint8_t member,
    uint8_t slot) {
  RealmzTorchSource current;
  if ((member > 5) || (slot > 29) ||
      !RealmzFindFirstUsableTorchSource(&current)) {
    return 0;
  }
  return (current.member == member) && (current.slot == slot);
}
