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

// The native driver is deliberately not part of the bootstrap slice yet.
// Until it exists, a configured child exits explicitly without writing a
// result file or claiming completion.
int RealmzRunSemanticReplayChild(void);

#define REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT 2
#define REALMZ_SEMANTIC_REPLAY_DRIVER_UNAVAILABLE_EXIT 3

#ifdef __cplusplus
} // extern "C"
#endif
