// Standalone unit tests for gl_curate_resolutions (no SDL, no engine headers).
// Build + run from the OniARM64 repo root:
//   cc -Wall -Wextra -std=c99 tests/test_gl_resolution_curate.c \
//     BungieFrameWork/BFW_Source/BFW_Motoko/Engines/DrawEngine/OpenGL/gl_resolution_curate.c \
//     -o /tmp/test_curate && /tmp/test_curate
#include <stdio.h>
#include "../BungieFrameWork/BFW_Source/BFW_Motoko/Engines/DrawEngine/OpenGL/gl_resolution_curate.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

int main(void)
{
    int ow[64], oh[64];

    // (1) drop sub-640x480, dedupe, sort ascending
    {
        int rw[] = { 1920, 800, 800, 1920, 320, 1280 };
        int rh[] = { 1080, 600, 600, 1080, 240, 720  };
        int n = gl_curate_resolutions(rw, rh, 6, ow, oh, 64);
        printf("curate basic:\n");
        CHECK(n == 3, "drops tiny + dedupes to 3 unique");
        CHECK(ow[0] == 800  && oh[0] == 600,  "sorted[0] = 800x600");
        CHECK(ow[1] == 1280 && oh[1] == 720,  "sorted[1] = 1280x720");
        CHECK(ow[2] == 1920 && oh[2] == 1080, "sorted[2] = 1920x1080");
    }

    // (2) overflow keeps the LARGEST maxOut
    {
        int rw[] = { 640, 800, 1024, 1280, 1920 };
        int rh[] = { 480, 600, 768,  720,  1080 };
        int n = gl_curate_resolutions(rw, rh, 5, ow, oh, 3);
        printf("curate overflow:\n");
        CHECK(n == 3, "capped to maxOut=3");
        CHECK(ow[0] == 1024 && oh[0] == 768,  "kept-largest sorted[0] = 1024x768");
        CHECK(ow[1] == 1280 && oh[1] == 720,  "kept-largest sorted[1] = 1280x720");
        CHECK(ow[2] == 1920 && oh[2] == 1080, "kept-largest sorted[2] = 1920x1080");
    }

    // (3) empty input -> 0 (caller uses this to trigger its fallback)
    {
        int n = gl_curate_resolutions(NULL, NULL, 0, ow, oh, 64);
        printf("curate empty:\n");
        CHECK(n == 0, "rawCount 0 returns 0");
    }

    // (4) everything below the floor -> 0
    {
        int rw[] = { 320, 512, 640 };
        int rh[] = { 240, 384, 400 }; // 640x400 fails the h >= 480 floor
        int n = gl_curate_resolutions(rw, rh, 3, ow, oh, 64);
        printf("curate all-tiny:\n");
        CHECK(n == 0, "everything below 640x480 dropped -> 0");
    }

    // (5) realistic 1440p panel set: unsorted, a dupe, and one sub-min absent
    {
        int rw[] = { 2560, 1920, 1280, 2560, 1080, 1600, 640 };
        int rh[] = { 1440, 1080, 720,  1440, 720,  900,  480 };
        int n = gl_curate_resolutions(rw, rh, 7, ow, oh, 64);
        printf("curate 1440p panel:\n");
        CHECK(n == 6, "6 unique modes >=640x480 (2560x1440 dupe collapsed)");
        CHECK(ow[0] == 640  && oh[0] == 480,  "sorted[0] = 640x480");
        CHECK(ow[5] == 2560 && oh[5] == 1440, "sorted[last] = 2560x1440 (native kept)");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
