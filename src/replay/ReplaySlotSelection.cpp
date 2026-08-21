#include "replay/ReplaySlotSelection.h"

extern "C" short RealmzReplayLegacyChoiceForSlot(char slot) {
  if ((slot < 'A') || (slot > 'J')) {
    return 0;
  }
  return static_cast<short>(4 + (slot - 'A'));
}
