// ======================================================================
// ONi_BundlePath.h
// ======================================================================
//
// Resolves the game-data folder using a layered lookup chain. At each location
// it tries the natural retail name "GameDataFolder" first, then the legacy
// "gamedata" (so existing installs and the dev symlink keep working):
//
//   1. $HOME/Library/Application Support/OniARM64/{GameDataFolder,gamedata}
//   2. <executable_dir>/../Resources/{GameDataFolder,gamedata}  (from .app)
//   3. The legacy BFrFileRef_Search chain (dev workflow — OniNative/Oni with a
//      GameDataFolder symlink, resolved relative to cwd).
//
// A candidate only wins if it *validates as real Oni data* (holds the sentinel
// level0_Final.dat, or any levelN_Final.dat) — not just that the directory
// exists. That rejects empty / wrong / double-nested folders that would
// otherwise resolve here and fail downstream. If nothing validates, returns
// ONcError_NoDataFolder; on Apple+SDL the caller then runs the guided picker.
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
