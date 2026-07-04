// ======================================================================
// test_oni_texturepacks.c
//
// Standalone unit tests for the pure libc-only texture-pack discovery
// helper in ONi_TexturePacks.c. No framework needed — compile and run
// directly (mirrors tests/test_oni_gamedata.c):
//
//   cc -Wall -Wextra tests/test_oni_texturepacks.c \
//      OniProj/OniGameSource/ONi_TexturePacks.c -o /tmp/test_oni_texturepacks
//   /tmp/test_oni_texturepacks
//
// Exercises: env-gating (ONI_TEXTUREPACKS=0 disables), enumeration of the
// pack subdirectories under <appSupportDir>/TexturePacks/ in ascending order
// for a deterministic load order, and a missing TexturePacks dir yielding 0.
// ======================================================================
#include "../OniProj/OniGameSource/ONi_TexturePacks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

// ONI_TEXTUREPACKS=0 disables discovery entirely (kill switch), so a fully
// populated tree still enumerates to nothing.
static void test_disabled_by_env(void)
{
    printf("ONi_TexturePacks_Enumerate (disabled by env):\n");
    system("rm -rf /tmp/oni_tp_test_root && "
           "mkdir -p /tmp/oni_tp_test_root/TexturePacks/packA");
    setenv("ONI_TEXTUREPACKS", "0", 1);
    char roots[ONI_TP_MAX_PACKS][ONI_TP_PATH_MAX];
    int n = ONi_TexturePacks_Enumerate("/tmp/oni_tp_test_root", roots);
    CHECK(n == 0, "ONI_TEXTUREPACKS=0 disables enumeration");
    unsetenv("ONI_TEXTUREPACKS");
    system("rm -rf /tmp/oni_tp_test_root");
}

// Pack subdirectories are returned sorted ascending, so load order is
// deterministic regardless of readdir() ordering. Dirs are created in
// non-alphabetical order (B, C, A) and each result is checked for exact-tail
// equality in position — so the test fails if qsort is removed even on a
// filesystem whose readdir() happens to return entries already sorted.
static void check_tail(const char *path, const char *want, const char *msg)
{
    size_t plen = strlen(path);
    size_t wlen = strlen(want);
    CHECK(plen >= wlen && strcmp(path + plen - wlen, want) == 0, msg);
}

static void test_enumerates_pack_dirs(void)
{
    printf("ONi_TexturePacks_Enumerate (enumerates pack dirs):\n");
    system("rm -rf /tmp/oni_tp_test_root && "
           "mkdir -p /tmp/oni_tp_test_root/TexturePacks/packB && "
           "mkdir -p /tmp/oni_tp_test_root/TexturePacks/packC && "
           "mkdir -p /tmp/oni_tp_test_root/TexturePacks/packA");
    char roots[ONI_TP_MAX_PACKS][ONI_TP_PATH_MAX];
    int n = ONi_TexturePacks_Enumerate("/tmp/oni_tp_test_root", roots);
    CHECK(n == 3, "enumerates all three pack directories");
    check_tail(roots[0], "/packA", "sorted ascending: packA first");
    check_tail(roots[1], "/packB", "sorted ascending: packB second");
    check_tail(roots[2], "/packC", "sorted ascending: packC third");
    system("rm -rf /tmp/oni_tp_test_root");
}

// A missing TexturePacks directory is the common case (no packs installed) —
// it must read as 0, never an error or crash.
static void test_missing_dir_is_zero(void)
{
    printf("ONi_TexturePacks_Enumerate (missing dir):\n");
    char roots[ONI_TP_MAX_PACKS][ONI_TP_PATH_MAX];
    int n = ONi_TexturePacks_Enumerate("/tmp/oni_tp_does_not_exist", roots);
    CHECK(n == 0, "missing TexturePacks dir returns 0");
}

int main(void)
{
    test_disabled_by_env();
    test_enumerates_pack_dirs();
    test_missing_dir_is_zero();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("ALL ONi_TexturePacks TESTS PASSED\n");
    }
    return g_fail == 0 ? 0 : 1;
}
