// ======================================================================
// ONi_BundlePath.c
// ======================================================================

#include "ONi_BundlePath.h"
#include "Oni.h"   // ONcGameDataFolder1, ONcGameDataFolder2, ONcError_NoDataFolder

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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

// If `candidate` exists as a directory, set *outFolder to it (via the BFW
// file-ref API) and return UUcTrue. Else UUcFalse.
static UUtBool ONiBundlePath_TryCandidate(const char *candidate, BFtFileRef *outFolder)
{
    if (!ONiBundlePath_DirExists(candidate)) {
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
    char path[BFcMaxPathLength];
    int n = snprintf(path, sizeof(path),
                     "%s/Library/Application Support/OniARM64/gamedata",
                     home);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        UUrStartupMessage("[BundlePath] Application Support path overflow");
        return UUcFalse;
    }
    return ONiBundlePath_TryCandidate(path, outFolder);
}

// ----------------------------------------------------------------------
// Candidate 2: <executable_dir>/../Resources/gamedata
// ----------------------------------------------------------------------

#ifdef __APPLE__
static UUtBool ONiBundlePath_GetExecutableDir(char *outDir, size_t outDirSize)
{
    char raw[BFcMaxPathLength];
    uint32_t size = (uint32_t)sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) {
        return UUcFalse;
    }
    char real[BFcMaxPathLength];
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
    char path[BFcMaxPathLength];
    int n = snprintf(path, sizeof(path),
                     "%s/../Resources/gamedata",
                     execDir);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return UUcFalse;
    }
    return ONiBundlePath_TryCandidate(path, outFolder);
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
    UUtError err = BFrFileRef_Search(ONcGameDataFolder1, outFolder);
    if (err == UUcError_None) {
        UUrStartupMessage("[BundlePath] game data folder resolved via legacy search at %s", ONcGameDataFolder1);
        return UUcTrue;
    }
    UUrStartupMessage("[BundlePath] legacy search miss at %s", ONcGameDataFolder1);

    err = BFrFileRef_Search(ONcGameDataFolder2, outFolder);
    if (err == UUcError_None) {
        UUrStartupMessage("[BundlePath] game data folder resolved via legacy search at %s", ONcGameDataFolder2);
        return UUcTrue;
    }
    UUrStartupMessage("[BundlePath] legacy search miss at %s", ONcGameDataFolder2);
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
