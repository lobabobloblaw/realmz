#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Imports a legacy user-data root into the fork's isolated writable root.
// Returns zero on success and a nonzero process-style status on failure.
int RealmzImportLegacyUserData(const char* source_root);

#ifdef __cplusplus
}
#endif
