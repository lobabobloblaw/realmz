#include "LegacyPartySelection.h"

// Keep this mutation boundary independent of the broad legacy headers. Party
// member IDs are the zero-based indices used by charselectnew; charnum is the
// inclusive final party index and is expected to remain in the Classic 0..5
// range while a gameplay party is active.
extern char charnum;
extern char charselectnew;
extern void updatecontrols(void);

RealmzPartySelectionApplyResult RealmzApplyPartyMemberSelection(
    uint8_t member) {
  const int maximum_member = (int)charnum;
  if ((maximum_member < 0) || (maximum_member > 5) ||
      ((int)member > maximum_member)) {
    return REALMZ_PARTY_SELECTION_REJECTED;
  }
  if ((int)charselectnew == (int)member) {
    return REALMZ_PARTY_SELECTION_UNCHANGED;
  }

  charselectnew = (char)member;
  updatecontrols();
  return REALMZ_PARTY_SELECTION_CHANGED;
}
