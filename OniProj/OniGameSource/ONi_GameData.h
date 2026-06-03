// ======================================================================
// ONi_GameData.h
//
// Pure, dependency-free (libc-only) helpers for locating, validating, and
// installing an Oni GameDataFolder. No BFW / engine types so the logic is
// unit-testable in isolation (see ../../tests/test_oni_gamedata.c) and can be
// shared between the C resolver (ONi_BundlePath.c) and the Cocoa first-run
// picker (Platform_SDL/Oni_DataSetup_macOS.mm).
//
// "Valid Oni data" = the directory holds the sentinel level0_Final.dat, or
// (fallback) any levelN_Final.dat. That content check is what lets the resolver
// accept a folder by *content* rather than bare existence — so an empty, wrong,
// or double-nested directory no longer resolves and then dies downstream.
//
// Booleans are plain int (nonzero = true) to stay free of <stdbool.h> include
// ordering concerns when pulled into the Obj-C++ picker.
// ======================================================================
#ifndef ONI_GAMEDATA_H
#define ONI_GAMEDATA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1 if `dir` contains Oni game data (sentinel level0_Final.dat, or any
// levelN_Final.dat as fallback); 0 otherwise (missing dir, empty, or wrong).
int ONiGameData_ValidateFolder(const char *dir);

// Resolve a valid GameDataFolder starting from `parent`:
//   - if `parent` itself validates                -> out = parent
//   - else if parent/GameDataFolder validates      -> out = that child
//   - else if parent/gamedata validates            -> out = that child
//   - else                                          -> return 0
// Handles "user picked the folder *containing* their data". `out` receives the
// resolved path (NUL-terminated, truncated safely to outSize). Returns 1 on
// success, 0 if nothing under `parent` is valid Oni data.
int ONiGameData_FindFolderIn(const char *parent, char *out, size_t outSize);

// Recursively copy the *contents* of srcDir into dstDir, so that files directly
// inside srcDir end up directly inside dstDir (no extra nesting level — the
// exact mistake this feature must not reproduce). dstDir's parent must already
// exist; dstDir is created if absent. On failure a human-readable reason is
// written to errBuf and — ONLY if this call created dstDir — the partial
// directory is removed; a pre-existing dstDir is never deleted, so a prior
// install can't be destroyed by a failed re-copy. Source trees nested deeper
// than an internal cap fail cleanly rather than overflowing the stack.
// Returns 0 on success, -1 on failure.
int ONiGameData_CopyTree(const char *srcDir, const char *dstDir,
                         char *errBuf, size_t errBufSize);

#ifdef __cplusplus
}
#endif

#endif // ONI_GAMEDATA_H
