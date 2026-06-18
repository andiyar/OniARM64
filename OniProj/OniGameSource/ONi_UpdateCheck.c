// ======================================================================
// ONi_UpdateCheck.c
//
// Pure decision core for the GitHub release-update notifier (#40). See
// ONi_UpdateCheck.h. No Cocoa / network / date libs — string + int only.
// ======================================================================
#include "ONi_UpdateCheck.h"

#include <string.h>

// Strip a single leading 'v' so "v1.3.0r3" and "1.3.0r3" compare equal.
static const char *ONiUpdate_StripV(const char *s)
{
    if (s == NULL) {
        return "";
    }
    if (s[0] == 'v') {
        return s + 1;
    }
    return s;
}

int ONrUpdateCheck_PickLatest(const ONtReleaseInfo *releases, int count)
{
    int best = -1;
    int i;

    if (releases == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (releases[i].is_draft) {
            continue;
        }
        if (best < 0 || releases[i].published_at > releases[best].published_at) {
            best = i;
        }
    }

    return best;
}

ONtUpdateVerdict ONrUpdateCheck_Decide(
    const char           *my_version,
    long long             my_build_time,
    const ONtReleaseInfo *latest,
    const char           *skipped_tag)
{
    const char *latest_norm;
    const char *mine_norm;

    // 1. Nothing to compare against.
    if (latest == NULL) {
        return ONcUpdate_None;
    }

    // 2. User explicitly skipped exactly this tag.
    if (skipped_tag != NULL && skipped_tag[0] != '\0' &&
        strcmp(latest->tag, skipped_tag) == 0) {
        return ONcUpdate_None;
    }

    // 3. Already running this release (normalise a single leading 'v').
    latest_norm = ONiUpdate_StripV(latest->tag);
    mine_norm   = ONiUpdate_StripV(my_version);
    if (strcmp(latest_norm, mine_norm) == 0) {
        return ONcUpdate_None;
    }

    // 4. Local dev-build guard: we built after this release was published.
    if (my_build_time != 0 && latest->published_at <= my_build_time) {
        return ONcUpdate_None;
    }

    // 5. A newer, non-skipped release exists.
    return ONcUpdate_Available;
}
