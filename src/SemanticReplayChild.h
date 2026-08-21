#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Installs the validated, process-lifetime replay policy described by the
// child config. This must run before ToolBoxInit (and therefore before SDL or
// any user-data path getter). Returns zero on success and emits a bounded
// diagnostic on failure.
int RealmzConfigureSemanticReplayChild(const char* config_path);

// True only after a replay config and all of its one-shot startup policies
// have been installed successfully.
int RealmzSemanticReplayChildIsActive(void);

// Validates and starts the movement-only plan before explicitly loading the
// configured input slot, then enters normal gameplay. Successful replay
// completion terminates from the guarded gameplay poll after synchronously
// saving, verifying, and publishing the result, so this function normally does
// not return after entry.
int RealmzRunSemanticReplayChild(void);

// Replay cannot interact with legacy warning/error modals: raw host input is
// intentionally isolated. Preserved C failure paths call this after checking
// RealmzSemanticReplayChildIsActive so they terminate nonzero instead of
// hanging or reporting a false-success exit.
void RealmzFailSemanticReplayChild(const char* detail);

#define REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT 2
#define REALMZ_SEMANTIC_REPLAY_ACTION_ERROR_EXIT 3
#define REALMZ_SEMANTIC_REPLAY_EXECUTION_ERROR_EXIT 4

#ifdef __cplusplus
} // extern "C"
#endif
