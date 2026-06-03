// ======================================================================
// ONi_BundlePath.c
// ======================================================================

#include "ONi_BundlePath.h"
#include "ONi_GameData.h"   // ONiGameData_ValidateFolder — content check
#include "Oni.h"   // ONcGameDataFolder1, ONcGameDataFolder2, ONcError_NoDataFolder

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Scratch buffer cap that's safe for macOS realpath(3) (PATH_MAX = 1024).
// BFcMaxPathLength is 255 (Mac OS 9 era) — too small. Used both inside the
// __APPLE__ block (Task 5) and by the state-file resolver below.
#define ONiBundlePath_PathMax 1024

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

static UUtBool ONiBundlePath_DirExists(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return UUcFalse;
    }
    return S_ISDIR(st.st_mode) ? UUcTrue : UUcFalse;
}

static UUtBool ONiBundlePath_FileExists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? UUcTrue : UUcFalse;
}

// If `candidate` exists as a directory AND validates as real Oni data, set
// *outFolder to it (via the BFW file-ref API) and return UUcTrue. Else UUcFalse.
// The content check (not bare existence) is what stops an empty, wrong, or
// double-nested directory from resolving here and then failing downstream.
static UUtBool ONiBundlePath_TryCandidate(const char *candidate, BFtFileRef *outFolder)
{
    if (!ONiBundlePath_DirExists(candidate)) {
        return UUcFalse;
    }
    if (!ONiGameData_ValidateFolder(candidate)) {
        UUrStartupMessage("[BundlePath] %s exists but holds no Oni level data; skipping", candidate);
        return UUcFalse;
    }
    UUtError err = BFrFileRef_Set(outFolder, candidate);
    if (err != UUcError_None) {
        return UUcFalse;
    }
    if (!BFrFileRef_FileExists(outFolder)) {
        return UUcFalse;
    }
    UUrStartupMessage("[BundlePath] game data folder resolved to %s", candidate);
    return UUcTrue;
}

// ----------------------------------------------------------------------
// Candidate 1: $HOME/Library/Application Support/OniARM64/gamedata
// ----------------------------------------------------------------------

static UUtBool ONiBundlePath_TryApplicationSupport(BFtFileRef *outFolder)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return UUcFalse;
    }
    // Prefer the natural retail folder name "GameDataFolder"; still accept the
    // legacy "gamedata" so existing installs (and the dev symlink) keep working.
    static const char *const names[] = { "GameDataFolder", "gamedata" };
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[BFcMaxPathLength];
        int n = snprintf(path, sizeof(path),
                         "%s/Library/Application Support/OniARM64/%s",
                         home, names[i]);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            UUrStartupMessage("[BundlePath] Application Support path overflow");
            continue;
        }
        if (ONiBundlePath_TryCandidate(path, outFolder)) {
            return UUcTrue;
        }
    }
    return UUcFalse;
}

// ----------------------------------------------------------------------
// Candidate 2: <executable_dir>/../Resources/gamedata
// ----------------------------------------------------------------------

#ifdef __APPLE__
static UUtBool ONiBundlePath_GetExecutableDir(char *outDir, size_t outDirSize)
{
    char raw[ONiBundlePath_PathMax];
    uint32_t size = (uint32_t)sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) {
        return UUcFalse;
    }
    char real[ONiBundlePath_PathMax];
    if (realpath(raw, real) == NULL) {
        return UUcFalse;
    }
    char *slash = strrchr(real, '/');
    if (slash == NULL) {
        return UUcFalse;
    }
    *slash = '\0';
    if (strlcpy(outDir, real, outDirSize) >= outDirSize) {
        return UUcFalse;
    }
    return UUcTrue;
}

static UUtBool ONiBundlePath_TryBundleResources(BFtFileRef *outFolder)
{
    char execDir[BFcMaxPathLength];
    if (!ONiBundlePath_GetExecutableDir(execDir, sizeof(execDir))) {
        return UUcFalse;
    }
    static const char *const names[] = { "GameDataFolder", "gamedata" };
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[BFcMaxPathLength];
        int n = snprintf(path, sizeof(path),
                         "%s/../Resources/%s",
                         execDir, names[i]);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            continue;
        }
        if (ONiBundlePath_TryCandidate(path, outFolder)) {
            return UUcTrue;
        }
    }
    return UUcFalse;
}
#else
static UUtBool ONiBundlePath_TryBundleResources(BFtFileRef *outFolder)
{
    (void)outFolder;
    return UUcFalse;
}
#endif

// ----------------------------------------------------------------------
// Candidate 3: legacy BFrFileRef_Search chain (current OniNative workflow)
// ----------------------------------------------------------------------

static UUtBool ONiBundlePath_TryLegacySearch(BFtFileRef *outFolder)
{
    // The search resolves a folder relative to cwd; only accept it if it
    // validates as real Oni data (GetFullPath returns the cwd-relative path the
    // search just confirmed exists, so the content check runs in the same cwd).
    static const char *const searchNames[] = { ONcGameDataFolder1, ONcGameDataFolder2 };
    unsigned i;
    for (i = 0; i < sizeof(searchNames) / sizeof(searchNames[0]); i++) {
        UUtError err = BFrFileRef_Search(searchNames[i], outFolder);
        if (err == UUcError_None) {
            const char *resolved = BFrFileRef_GetFullPath(outFolder);
            if (resolved != NULL && ONiGameData_ValidateFolder(resolved)) {
                UUrStartupMessage("[BundlePath] game data folder resolved via legacy search at %s", resolved);
                return UUcTrue;
            }
            UUrStartupMessage("[BundlePath] legacy search hit %s but it holds no Oni level data; skipping",
                              (resolved != NULL) ? resolved : searchNames[i]);
        } else {
            UUrStartupMessage("[BundlePath] legacy search miss at %s", searchNames[i]);
        }
    }
    return UUcFalse;
}

// ----------------------------------------------------------------------
// Public entry point
// ----------------------------------------------------------------------

UUtError ONiBundlePath_ResolveGameDataFolder(BFtFileRef *outFolder)
{
    UUrStartupMessage("[BundlePath] resolving game data folder");

    if (ONiBundlePath_TryApplicationSupport(outFolder)) return UUcError_None;
    if (ONiBundlePath_TryBundleResources(outFolder))     return UUcError_None;
    if (ONiBundlePath_TryLegacySearch(outFolder))        return UUcError_None;

    UUrStartupMessage("[BundlePath] all candidates failed; game data folder not found");
    return ONcError_NoDataFolder;
}

// ----------------------------------------------------------------------
// State-file path resolution (persist.dat, key_config.txt, etc.)
// ----------------------------------------------------------------------
//
// Two-strategy resolver mirroring the gamedata chain:
//
//   1. If ./<filename> already exists, return that. Preserves the bare-binary
//      OniNative workflow — an existing persist.dat / key_config.txt next to
//      the binary stays authoritative.
//   2. Otherwise return $HOME/Library/Application Support/OniARM64/<filename>,
//      creating the directory tree if needed. This is where state files land
//      under the .app workflow (cwd = /, no writable file there).
//
// Apple's File System Programming Guide: ~/Library/Preferences/ is reserved
// for plist files managed by cfprefsd via NSUserDefaults/CFPreferences. Custom
// binary state files (Oni's persist.dat with its own version/swap-code header)
// belong under Application Support/.

UUtError ONiBundlePath_ResolveStateFile(const char *filename, char *outPath, size_t outPathSize)
{
    if (filename == NULL || outPath == NULL || outPathSize == 0) {
        return UUcError_Generic;
    }

    // Strategy 1: cwd-relative if file already exists.
    int n = snprintf(outPath, outPathSize, "./%s", filename);
    if (n > 0 && (size_t)n < outPathSize && ONiBundlePath_FileExists(outPath)) {
        UUrStartupMessage("[BundlePath] state file '%s' using cwd-relative %s", filename, outPath);
        return UUcError_None;
    }

    // Strategy 2: ~/Library/Application Support/OniARM64/<filename>.
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        UUrStartupMessage("[BundlePath] HOME unset; cannot resolve state file '%s'", filename);
        return UUcError_Generic;
    }

    char dir[ONiBundlePath_PathMax];
    int dirN = snprintf(dir, sizeof(dir), "%s/Library/Application Support/OniARM64", home);
    if (dirN < 0 || (size_t)dirN >= sizeof(dir)) {
        UUrStartupMessage("[BundlePath] App Support path overflow for '%s'", filename);
        return UUcError_Generic;
    }

    // mkdir -p semantics: tolerate EEXIST, let other failures fall through —
    // the subsequent fopen will surface them with a more useful errno.
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        // Continue anyway; user may have a usable directory we can't introspect.
    }

    n = snprintf(outPath, outPathSize, "%s/%s", dir, filename);
    if (n < 0 || (size_t)n >= outPathSize) {
        UUrStartupMessage("[BundlePath] App Support path overflow building full path for '%s'", filename);
        return UUcError_Generic;
    }

    UUrStartupMessage("[BundlePath] state file '%s' resolved to %s", filename, outPath);
    return UUcError_None;
}
