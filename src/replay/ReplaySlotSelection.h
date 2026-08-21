#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Maps a protocol save-slot label to the corresponding legacy chooser item.
 * Only uppercase ASCII A through J are valid; invalid labels return zero. */
short RealmzReplayLegacyChoiceForSlot(char slot);

/* Loads one explicit protocol save slot without opening fileprep's chooser or
 * reading or writing its lastgame, descriptions, or preference state. */
short RealmzReplayLoadSlot(char slot);

/* Saves to one explicit protocol slot with the legacy save prelude and writer,
 * but without fileprep's chooser or its preference state. A return value of
 * one only means that the legacy body reached its end; callers must still
 * structurally verify the output because some legacy writes are unchecked. */
short RealmzReplaySaveSlot(char slot);

#ifdef __cplusplus
}
#endif
