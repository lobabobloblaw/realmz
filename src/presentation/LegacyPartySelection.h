#ifndef REALMZ_PRESENTATION_LEGACY_PARTY_SELECTION_H
#define REALMZ_PRESENTATION_LEGACY_PARTY_SELECTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RealmzPartySelectionApplyResult {
  REALMZ_PARTY_SELECTION_REJECTED = 0,
  REALMZ_PARTY_SELECTION_UNCHANGED = 1,
  REALMZ_PARTY_SELECTION_CHANGED = 2
} RealmzPartySelectionApplyResult;

// Applies only the selection behavior of a first click on a Classic party
// portrait. The caller must already have completed semantic context validation.
// Re-selecting the active member is an idempotent no-op: it must not emulate the
// Classic second-click path, which opens the view-character modal.
RealmzPartySelectionApplyResult RealmzApplyPartyMemberSelection(
    uint8_t member);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
