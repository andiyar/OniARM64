// ======================================================================
// test_oni_updatecheck.c
//
// Standalone unit tests for the pure decision core in ONi_UpdateCheck.c.
// No framework — compile and run directly from the repo root:
//
//   cc -Wall -Wextra tests/test_oni_updatecheck.c \
//      OniProj/OniGameSource/ONi_UpdateCheck.c -o /tmp/test_oni_updatecheck
//   /tmp/test_oni_updatecheck
//
// Covers the design's verification table: Decide() (newer/same/skipped/
// dev-build-guard/guard-disabled) and PickLatest() (draft exclusion,
// prerelease inclusion, empty list).
// ======================================================================
#include "../OniProj/OniGameSource/ONi_UpdateCheck.h"

#include <stdio.h>
#include <string.h>

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

static ONtReleaseInfo mk(const char *tag, long long published,
                         int is_draft, int is_prerelease)
{
    ONtReleaseInfo r;
    memset(&r, 0, sizeof(r));
    strncpy(r.tag, tag, ONcUpdate_TagMax - 1);
    r.published_at  = published;
    r.is_draft      = is_draft;
    r.is_prerelease = is_prerelease;
    return r;
}

int main(void)
{
    printf("test_oni_updatecheck\n");

    // --- ONrUpdateCheck_Decide ---

    // Newer tag, published after build time, not skipped -> Available
    {
        ONtReleaseInfo latest = mk("v1.3.0r4", 2000, 0, 1);
        CHECK(ONrUpdateCheck_Decide("1.3.0r3", 1000, &latest, "")
              == ONcUpdate_Available, "newer non-skipped -> Available");
    }

    // Same tag as mine, latest carries a leading 'v' -> None
    {
        ONtReleaseInfo latest = mk("v1.3.0r3", 2000, 0, 1);
        CHECK(ONrUpdateCheck_Decide("1.3.0r3", 1000, &latest, "")
              == ONcUpdate_None, "same tag (v-prefixed latest) -> None");
    }

    // Same tag, neither prefixed -> None
    {
        ONtReleaseInfo latest = mk("1.3.0r3", 2000, 0, 1);
        CHECK(ONrUpdateCheck_Decide("1.3.0r3", 1000, &latest, "")
              == ONcUpdate_None, "same tag (no prefix) -> None");
    }

    // Latest tag == skipped tag -> None (even though newer)
    {
        ONtReleaseInfo latest = mk("v1.3.0r4", 2000, 0, 1);
        CHECK(ONrUpdateCheck_Decide("1.3.0r3", 1000, &latest, "v1.3.0r4")
              == ONcUpdate_None, "skipped tag -> None");
    }

    // Latest published_at <= my build time (dev build ahead) -> None
    {
        ONtReleaseInfo latest = mk("v1.3.0r4", 1000, 0, 1);
        CHECK(ONrUpdateCheck_Decide("1.3.0r3", 2000, &latest, "")
              == ONcUpdate_None, "dev-build guard (published<=build) -> None");
    }

    // Build time 0 disables the guard: newer tag -> Available regardless
    {
        ONtReleaseInfo latest = mk("v1.3.0r4", 1, 0, 1);
        CHECK(ONrUpdateCheck_Decide("1.3.0r3", 0, &latest, "")
              == ONcUpdate_Available, "build_time==0 disables guard -> Available");
    }

    // latest == NULL -> None
    CHECK(ONrUpdateCheck_Decide("1.3.0r3", 1000, NULL, "")
          == ONcUpdate_None, "NULL latest -> None");

    // --- ONrUpdateCheck_PickLatest ---

    // Draft is newest but excluded; picks newest non-draft
    {
        ONtReleaseInfo rs[3] = {
            mk("v1.3.0r3", 1000, 0, 1),
            mk("v1.4.0d",  3000, 1, 1),   // draft, newest -> excluded
            mk("v1.3.0r4", 2000, 0, 1),
        };
        int idx = ONrUpdateCheck_PickLatest(rs, 3);
        CHECK(idx == 2, "draft excluded; picks newest non-draft");
    }

    // Prerelease is newest -> picked (v1 includes prereleases)
    {
        ONtReleaseInfo rs[2] = {
            mk("v1.3.0",   1000, 0, 0),
            mk("v1.3.0r4", 2000, 0, 1),   // prerelease, newest
        };
        int idx = ONrUpdateCheck_PickLatest(rs, 2);
        CHECK(idx == 1, "prerelease newest -> picked");
    }

    // Empty list -> -1
    CHECK(ONrUpdateCheck_PickLatest(NULL, 0) == -1, "NULL list -> -1");
    {
        ONtReleaseInfo rs[1] = { mk("d", 1, 1, 0) };  // single draft
        CHECK(ONrUpdateCheck_PickLatest(rs, 1) == -1, "all-draft list -> -1");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
