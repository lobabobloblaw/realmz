#ifndef REALMZ_ORIG_TIME_SCALE_H
#define REALMZ_ORIG_TIME_SCALE_H

#include <stddef.h>

static inline short RealmzTimeScaleForLocation(
    unsigned char in_dungeon,
    int last_picture,
    const short* base_scale,
    size_t base_scale_count) {
  /* Dungeon maps intentionally do not load outdoor pixmap metadata, so a
   * cold dungeon save can retain last_picture's -1 sentinel. */
  if (in_dungeon)
    return 1;
  if ((last_picture >= 0) &&
      ((size_t)last_picture < base_scale_count) &&
      (base_scale != NULL) && base_scale[last_picture])
    return 1;
  return 5;
}

#endif
