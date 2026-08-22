#ifndef REALMZ_PRESENTATION_LEGACY_TORCH_SOURCE_H
#define REALMZ_PRESENTATION_LEGACY_TORCH_SOURCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Zero-based party member and inventory slot selected by Classic's exact
// party-scoped checkforitem(805, TRUE, -1) traversal. This is a freshness
// locator, not a stable item-instance identity.
typedef struct RealmzTorchSource {
  uint8_t member;
  uint8_t slot;
} RealmzTorchSource;

// Finds Classic's first exact +805 item without mutating inventory or the
// global checkforitem scratch state. A first matching item with no positive
// charge blocks every later match, exactly as checkforitem does. Malformed
// party or item counts fail closed.
uint8_t RealmzFindFirstUsableTorchSource(RealmzTorchSource* source);

// Repeats the nonmutating scan and succeeds only when its current source still
// equals the supplied locator.
uint8_t RealmzCurrentFirstUsableTorchSourceMatches(
    uint8_t member,
    uint8_t slot);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
