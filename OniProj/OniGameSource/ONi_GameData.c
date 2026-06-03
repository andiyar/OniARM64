// ======================================================================
// ONi_GameData.c
//
// Pure libc-only implementation of the game-data validate / find / copy
// helpers declared in ONi_GameData.h. No BFW / engine dependencies, so this
// file compiles standalone for the unit tests (tests/test_oni_gamedata.c) and
// links into both the C resolver and the Cocoa picker unchanged.
// ======================================================================

#include "ONi_GameData.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Safe for macOS PATH_MAX (1024); doubled for headroom on long $HOME paths.
#define ONiGameData_PathCap 2048

// Bound copy recursion well under any plausible thread-stack limit — the picker
// runs the copy on a ~512 KB GCD worker, so a deeply nested source would
// otherwise overflow the stack and crash. This converts that into a clean
// error. Retail game data is ~2 levels deep, so 64 is far above any real need.
#define ONiGameData_MaxDepth 64

// ----------------------------------------------------------------------
// small helpers
// ----------------------------------------------------------------------

// Join a/b into out with a '/'; returns 0 on success, -1 if it would truncate.
static int ONiGameData_Join(char *out, size_t n, const char *a, const char *b)
{
    int k = snprintf(out, n, "%s/%s", a, b);
    return (k > 0 && (size_t)k < n) ? 0 : -1;
}

// Copy s into out; 0 on success, -1 if it would truncate.
static int ONiGameData_SafeCpy(char *out, size_t n, const char *s)
{
    size_t len = strlen(s);
    if (len >= n) {
        return -1;
    }
    memcpy(out, s, len + 1);
    return 0;
}

// Does `name` look like levelN_Final.dat? ("level" + at least one char + "_Final.dat")
static int ONiGameData_IsLevelFinal(const char *name)
{
    const char *suffix = "_Final.dat";
    size_t slen = 10; // strlen(suffix)
    size_t len = strlen(name);
    size_t i;
    if (len < 16) { // "level" (5) + >=1 digit + "_Final.dat" (10)
        return 0;
    }
    if (strncmp(name, "level", 5) != 0) {
        return 0;
    }
    if (strcmp(name + len - slen, suffix) != 0) {
        return 0;
    }
    // Everything between "level" and "_Final.dat" must be digits, so a stray
    // file like "levelX_Final.dat" doesn't masquerade as game data.
    for (i = 5; i < len - slen; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
    }
    return 1;
}

// Copy one regular file. 0 on success, -1 on failure (reason -> errBuf).
static int ONiGameData_CopyFile(const char *src, const char *dst,
                                char *errBuf, size_t errBufSize)
{
    int in = open(src, O_RDONLY);
    if (in < 0) {
        snprintf(errBuf, errBufSize, "cannot read %s: %s", src, strerror(errno));
        return -1;
    }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        snprintf(errBuf, errBufSize, "cannot create %s: %s", dst, strerror(errno));
        close(in);
        return -1;
    }

    char buf[65536];
    int rc = 0;
    ssize_t r;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w < 0) {
                snprintf(errBuf, errBufSize, "write failed %s: %s", dst, strerror(errno));
                rc = -1;
                break;
            }
            off += w;
        }
        if (rc != 0) {
            break;
        }
    }
    if (r < 0 && rc == 0) {
        snprintf(errBuf, errBufSize, "read failed %s: %s", src, strerror(errno));
        rc = -1;
    }

    close(in);
    // A deferred write error (e.g. ENOSPC flushed at close near the end of a
    // ~1GB copy) surfaces here, not at write() — so without this check a
    // truncated file could be reported as a successful copy.
    if (close(out) != 0 && rc == 0) {
        snprintf(errBuf, errBufSize, "close failed %s: %s", dst, strerror(errno));
        rc = -1;
    }
    return rc;
}

// Recursively remove a directory tree (best effort; ignores errors).
static void ONiGameData_RmTree(const char *path)
{
    DIR *d = opendir(path);
    if (d != NULL) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            char child[ONiGameData_PathCap];
            if (ONiGameData_Join(child, sizeof(child), path, e->d_name) != 0) {
                continue;
            }
            struct stat st;
            if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
                ONiGameData_RmTree(child);
            } else {
                unlink(child);
            }
        }
        closedir(d);
    }
    rmdir(path);
}

// Recursively copy the contents of srcDir into dstDir (which must already
// exist). 0 on success, -1 on first failure (reason -> errBuf).
static int ONiGameData_CopyContents(const char *srcDir, const char *dstDir, int depth,
                                    char *errBuf, size_t errBufSize)
{
    if (depth > ONiGameData_MaxDepth) {
        snprintf(errBuf, errBufSize, "source directory nesting too deep (> %d) at %s",
                 ONiGameData_MaxDepth, srcDir);
        return -1;
    }

    DIR *d = opendir(srcDir);
    if (d == NULL) {
        snprintf(errBuf, errBufSize, "cannot open %s: %s", srcDir, strerror(errno));
        return -1;
    }

    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        char sp[ONiGameData_PathCap];
        char dp[ONiGameData_PathCap];
        if (ONiGameData_Join(sp, sizeof(sp), srcDir, e->d_name) != 0 ||
            ONiGameData_Join(dp, sizeof(dp), dstDir, e->d_name) != 0) {
            snprintf(errBuf, errBufSize, "path too long under %s", srcDir);
            rc = -1;
            break;
        }
        struct stat st;
        if (stat(sp, &st) != 0) {
            snprintf(errBuf, errBufSize, "cannot stat %s: %s", sp, strerror(errno));
            rc = -1;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            if (mkdir(dp, 0755) != 0 && errno != EEXIST) {
                snprintf(errBuf, errBufSize, "cannot mkdir %s: %s", dp, strerror(errno));
                rc = -1;
                break;
            }
            if (ONiGameData_CopyContents(sp, dp, depth + 1, errBuf, errBufSize) != 0) {
                rc = -1;
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (ONiGameData_CopyFile(sp, dp, errBuf, errBufSize) != 0) {
                rc = -1;
                break;
            }
        }
        // Non-regular, non-directory entries (fifos, sockets, devices) are
        // skipped — retail game data contains none.
    }

    closedir(d);
    return rc;
}

// ----------------------------------------------------------------------
// public API
// ----------------------------------------------------------------------

int ONiGameData_ValidateFolder(const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }

    // Fast path: the sentinel.
    char sentinel[ONiGameData_PathCap];
    struct stat st;
    if (ONiGameData_Join(sentinel, sizeof(sentinel), dir, "level0_Final.dat") == 0 &&
        stat(sentinel, &st) == 0 && S_ISREG(st.st_mode)) {
        return 1;
    }

    // Fallback: any levelN_Final.dat (covers data sets where level0 is named
    // or laid out differently).
    DIR *d = opendir(dir);
    if (d == NULL) {
        return 0;
    }
    int found = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (ONiGameData_IsLevelFinal(e->d_name)) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

int ONiGameData_FindFolderIn(const char *parent, char *out, size_t outSize)
{
    if (parent == NULL || out == NULL || outSize == 0) {
        return 0;
    }

    if (ONiGameData_ValidateFolder(parent)) {
        return ONiGameData_SafeCpy(out, outSize, parent) == 0 ? 1 : 0;
    }

    char child[ONiGameData_PathCap];
    if (ONiGameData_Join(child, sizeof(child), parent, "GameDataFolder") == 0 &&
        ONiGameData_ValidateFolder(child)) {
        return ONiGameData_SafeCpy(out, outSize, child) == 0 ? 1 : 0;
    }
    if (ONiGameData_Join(child, sizeof(child), parent, "gamedata") == 0 &&
        ONiGameData_ValidateFolder(child)) {
        return ONiGameData_SafeCpy(out, outSize, child) == 0 ? 1 : 0;
    }
    return 0;
}

int ONiGameData_CopyTree(const char *srcDir, const char *dstDir,
                         char *errBuf, size_t errBufSize)
{
    if (errBuf != NULL && errBufSize > 0) {
        errBuf[0] = '\0';
    }
    if (srcDir == NULL || dstDir == NULL) {
        if (errBuf != NULL && errBufSize > 0) {
            snprintf(errBuf, errBufSize, "null path");
        }
        return -1;
    }

    // Track whether *we* created dstDir. On failure we only clean up a
    // directory this call made — never a pre-existing one, so a failed re-copy
    // can't destroy a prior install. (The picker clears any stale dst first, so
    // in practice we always create it here.)
    int created = (mkdir(dstDir, 0755) == 0);
    if (!created && errno != EEXIST) {
        if (errBuf != NULL && errBufSize > 0) {
            snprintf(errBuf, errBufSize, "cannot create %s: %s", dstDir, strerror(errno));
        }
        return -1;
    }

    if (ONiGameData_CopyContents(srcDir, dstDir, 0, errBuf, errBufSize) != 0) {
        if (created) {
            ONiGameData_RmTree(dstDir); // remove only the partial dir we created
        }
        return -1;
    }
    return 0;
}
