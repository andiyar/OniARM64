// ======================================================================
// ONi_BundlePath.h
// ======================================================================
//
// Resolves the game-data folder using a layered lookup chain:
//
//   1. $HOME/Library/Application Support/OniARM64/gamedata
//   2. <executable_dir>/../Resources/gamedata   (when running from .app)
//   3. The legacy BFrFileRef_Search("GameDataFolder") chain (current dev
//      workflow — OniNative/Oni with a GameDataFolder symlink).
//
// First candidate that resolves to an existing directory wins. If none
// exist, returns ONcError_NoDataFolder (the same error the legacy chain
// would return).
//
// ======================================================================

#ifndef ONI_BUNDLE_PATH_H
#define ONI_BUNDLE_PATH_H

#include "BFW.h"
#include "BFW_FileManager.h"

#ifdef __cplusplus
extern "C" {
#endif

UUtError ONiBundlePath_ResolveGameDataFolder(BFtFileRef *outFolder);

// Resolve where a per-user state file (persist.dat, key_config.txt, ...)
// should live. Prefers ./<filename> if it already exists (bare-binary
// OniNative workflow), otherwise builds and ensures
// ~/Library/Application Support/OniARM64/<filename> (the .app workflow).
// Writes the chosen path to outPath.
UUtError ONiBundlePath_ResolveStateFile(const char *filename, char *outPath, size_t outPathSize);

#ifdef __cplusplus
}
#endif

#endif
