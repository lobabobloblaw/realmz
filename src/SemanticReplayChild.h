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

// Runs the currently available native child preflight. The complete action
// plan is validated against the movement-only v1 engine vocabulary before any
// save can be loaded or mutated. The live action driver and result writer are
// not connected yet, so a valid plan exits with the explicit unavailable
// status without loading, saving, or writing a result file.
int RealmzRunSemanticReplayChild(void);

#define REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT 2
#define REALMZ_SEMANTIC_REPLAY_DRIVER_UNAVAILABLE_EXIT 3

#ifdef __cplusplus
} // extern "C"
#endif
