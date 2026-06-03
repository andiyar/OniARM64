// ======================================================================
// test_oni_gamedata.c
//
// Standalone unit tests for the pure libc-only game-data helpers in
// ONi_GameData.c. No framework needed — compile and run directly:
//
//   cc -Wall -Wextra tests/test_oni_gamedata.c \
//      OniProj/OniGameSource/ONi_GameData.c -o /tmp/test_oni_gamedata
//   /tmp/test_oni_gamedata
//
// Exercises the design's §5 cases: validate (valid/empty/wrong/fallback),
// find (direct GDF / parent-of-GDF / back-compat gamedata / neither), and
// copy (success copies contents without double-nesting + recurses; failure
// cleans up the partial destination).
// ======================================================================
#include "../OniProj/OniGameSource/ONi_GameData.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (cond) {                                                        \
            g_pass++;                                                      \
        } else {                                                           \
            g_fail++;                                                      \
            printf("  FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);    \
        }                                                                  \
    } while (0)

// --- fixture helpers ---------------------------------------------------

static char *mk(void)
{
    char *p = (char *)malloc(64);
    if (p == NULL) {
        perror("malloc");
        exit(2);
    }
    strcpy(p, "/tmp/oni_gdf.XXXXXX");
    if (mkdtemp(p) == NULL) {
        perror("mkdtemp");
        exit(2);
    }
    return p;
}

static void cleanup(char *p)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", p);
    (void)system(cmd);
    free(p);
}

static void touch(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int fd = open(path, O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) {
        (void)!write(fd, "x", 1);
        close(fd);
    }
}

static void mksub(const char *dir, const char *name, char *out, size_t n)
{
    snprintf(out, n, "%s/%s", dir, name);
    mkdir(out, 0755);
}

static int fexists(const char *dir, const char *name)
{
    char path[1024];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    return stat(path, &st) == 0;
}

// --- tests -------------------------------------------------------------

static void test_validate(void)
{
    char *d;
    printf("ONiGameData_ValidateFolder:\n");

    d = mk(); touch(d, "level0_Final.dat");
    CHECK(ONiGameData_ValidateFolder(d), "accepts dir holding sentinel level0_Final.dat");
    cleanup(d);

    d = mk();
    CHECK(!ONiGameData_ValidateFolder(d), "rejects empty dir");
    cleanup(d);

    d = mk(); touch(d, "readme.txt"); touch(d, "notes.doc");
    CHECK(!ONiGameData_ValidateFolder(d), "rejects dir with no level files");
    cleanup(d);

    d = mk(); touch(d, "level14_Final.dat");
    CHECK(ONiGameData_ValidateFolder(d), "fallback: accepts any levelN_Final.dat when level0 absent");
    cleanup(d);

    d = mk(); touch(d, "levelX_Final.dat");
    CHECK(!ONiGameData_ValidateFolder(d), "rejects level<non-digit>_Final.dat (not a real level file)");
    cleanup(d);

    CHECK(!ONiGameData_ValidateFolder("/no/such/path/xyzzy"), "rejects nonexistent path");
    CHECK(!ONiGameData_ValidateFolder(NULL), "rejects NULL");
}

static void test_find(void)
{
    char *parent;
    char sub[1024];
    char out[1024];
    printf("ONiGameData_FindFolderIn:\n");

    // parent itself is the GameDataFolder
    parent = mk(); touch(parent, "level0_Final.dat");
    CHECK(ONiGameData_FindFolderIn(parent, out, sizeof(out)), "finds GDF when parent itself is one");
    CHECK(strcmp(out, parent) == 0, "returns parent path for direct GDF");
    cleanup(parent);

    // parent contains a child named GameDataFolder
    parent = mk(); mksub(parent, "GameDataFolder", sub, sizeof(sub)); touch(sub, "level0_Final.dat");
    CHECK(ONiGameData_FindFolderIn(parent, out, sizeof(out)), "descends into child GameDataFolder");
    CHECK(strcmp(out, sub) == 0, "returns child GameDataFolder path");
    cleanup(parent);

    // parent contains a child named gamedata (back-compat name)
    parent = mk(); mksub(parent, "gamedata", sub, sizeof(sub)); touch(sub, "level0_Final.dat");
    CHECK(ONiGameData_FindFolderIn(parent, out, sizeof(out)), "descends into back-compat child gamedata");
    CHECK(strcmp(out, sub) == 0, "returns child gamedata path");
    cleanup(parent);

    // neither parent nor any known child is valid
    parent = mk(); touch(parent, "random.txt");
    CHECK(!ONiGameData_FindFolderIn(parent, out, sizeof(out)), "returns 0 when nothing under parent is valid");
    cleanup(parent);
}

static void test_copy(void)
{
    char *src, *dstparent;
    char sub[1024], dst[1024], dsub[1024], bad[1024];
    char err[256];
    int rc, fd;
    struct stat st;
    printf("ONiGameData_CopyTree:\n");

    // success: contents land directly in dst (no double-nesting) + recursion
    src = mk();
    touch(src, "level0_Final.dat");
    touch(src, "level1_Final.dat");
    mksub(src, "IGMD", sub, sizeof(sub));
    touch(sub, "sub.dat");
    dstparent = mk();
    snprintf(dst, sizeof(dst), "%s/GameDataFolder", dstparent);
    err[0] = '\0';
    rc = ONiGameData_CopyTree(src, dst, err, sizeof(err));
    CHECK(rc == 0, "returns 0 on successful copy");
    CHECK(fexists(dst, "level0_Final.dat"), "level0 lands directly in dst (no double-nesting)");
    CHECK(fexists(dst, "level1_Final.dat"), "second top-level file copied");
    snprintf(dsub, sizeof(dsub), "%s/IGMD", dst);
    CHECK(fexists(dsub, "sub.dat"), "nested subdirectory contents copied recursively");
    cleanup(src);
    cleanup(dstparent);

    // failure: unreadable source file -> nonzero + partial dst removed
    src = mk();
    touch(src, "level0_Final.dat");
    snprintf(bad, sizeof(bad), "%s/locked.dat", src);
    fd = open(bad, O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) { (void)!write(fd, "y", 1); close(fd); }
    chmod(bad, 0000); // unreadable even by owner on macOS -> open() fails mid-copy
    dstparent = mk();
    snprintf(dst, sizeof(dst), "%s/GameDataFolder", dstparent);
    err[0] = '\0';
    rc = ONiGameData_CopyTree(src, dst, err, sizeof(err));
    CHECK(rc != 0, "returns nonzero when a source file cannot be read");
    CHECK(stat(dst, &st) != 0, "partial destination removed on failure");
    CHECK(err[0] != '\0', "writes a human-readable reason to errBuf on failure");
    chmod(bad, 0644);
    cleanup(src);
    cleanup(dstparent);
}

static void test_copy_robustness(void)
{
    char *src, *dstparent;
    char dst[1024], bad[1024], deep[4096], dpath[1024];
    char err[256];
    char readback[64];
    const char *payload = "HELLO_ONI_DATA_12345";
    int rc, fd, i;
    ssize_t nread;
    printf("ONiGameData_CopyTree (robustness):\n");

    // (A) A pre-existing destination holding real data must NOT be destroyed
    // when a re-copy from a bad source fails partway.
    src = mk();
    touch(src, "level0_Final.dat");
    snprintf(bad, sizeof(bad), "%s/locked.dat", src);
    fd = open(bad, O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) { (void)!write(fd, "y", 1); close(fd); }
    chmod(bad, 0000);
    dstparent = mk();
    snprintf(dst, sizeof(dst), "%s/GameDataFolder", dstparent);
    mkdir(dst, 0755);
    touch(dst, "PREEXISTING.dat"); // simulate a prior good install
    rc = ONiGameData_CopyTree(src, dst, err, sizeof(err));
    CHECK(rc != 0, "re-copy into a pre-existing dst still fails on an unreadable source");
    CHECK(fexists(dst, "PREEXISTING.dat"), "pre-existing dst content survives a failed copy");
    chmod(bad, 0644);
    cleanup(src);
    cleanup(dstparent);

    // (B) A pathologically deep source tree aborts cleanly rather than
    // recursing until the worker stack overflows.
    src = mk();
    touch(src, "level0_Final.dat");
    strncpy(deep, src, sizeof(deep));
    deep[sizeof(deep) - 1] = '\0';
    for (i = 0; i < 80; i++) {
        size_t len = strlen(deep);
        snprintf(deep + len, sizeof(deep) - len, "/d");
        if (mkdir(deep, 0755) != 0) { break; }
    }
    dstparent = mk();
    snprintf(dst, sizeof(dst), "%s/GameDataFolder", dstparent);
    err[0] = '\0';
    rc = ONiGameData_CopyTree(src, dst, err, sizeof(err));
    CHECK(rc != 0, "deep source nesting returns an error (not a crash)");
    cleanup(src);
    cleanup(dstparent);

    // (C) Copied file contents match byte-for-byte, not just existence.
    src = mk();
    snprintf(dpath, sizeof(dpath), "%s/level0_Final.dat", src);
    fd = open(dpath, O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) { (void)!write(fd, payload, strlen(payload)); close(fd); }
    dstparent = mk();
    snprintf(dst, sizeof(dst), "%s/GameDataFolder", dstparent);
    rc = ONiGameData_CopyTree(src, dst, err, sizeof(err));
    CHECK(rc == 0, "content copy succeeds");
    snprintf(dpath, sizeof(dpath), "%s/level0_Final.dat", dst);
    nread = -1;
    fd = open(dpath, O_RDONLY);
    if (fd >= 0) { nread = read(fd, readback, sizeof(readback) - 1); close(fd); }
    if (nread < 0) { nread = 0; }
    readback[nread] = '\0';
    CHECK((size_t)nread == strlen(payload) && strcmp(readback, payload) == 0,
          "copied file bytes match the source");
    cleanup(src);
    cleanup(dstparent);
}

int main(void)
{
    if (geteuid() == 0) {
        printf("WARNING: running as root; the copy-failure permission test may not hold.\n");
    }
    test_validate();
    test_find();
    test_copy();
    test_copy_robustness();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
