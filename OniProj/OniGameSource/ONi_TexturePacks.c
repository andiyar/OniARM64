// ======================================================================
// ONi_TexturePacks.c
//
// Pure libc-only implementation of the texture-pack discovery helper
// declared in ONi_TexturePacks.h. No BFW / engine dependencies, so this file
// compiles standalone for the unit tests (tests/test_oni_texturepacks.c) and
// links into the engine's data-registration path unchanged.
// ======================================================================

#include "ONi_TexturePacks.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

// One pack name; a directory entry name. macOS NAME_MAX is 255, +1 for NUL.
#define ONiTP_NameCap 256

// strcmp adaptor for qsort over fixed-width name rows, giving ascending order.
static int ONiTP_CmpName(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int ONi_TexturePacks_Enumerate(const char *appSupportDir,
                               char outRoots[ONI_TP_MAX_PACKS][ONI_TP_PATH_MAX])
{
    if (appSupportDir == NULL || outRoots == NULL) {
        return 0;
    }

    // ONI_TEXTUREPACKS=0 is the kill switch: skip all discovery.
    const char *en = getenv("ONI_TEXTUREPACKS");
    if (en != NULL && strcmp(en, "0") == 0) {
        return 0;
    }

    char base[ONI_TP_PATH_MAX];
    int k = snprintf(base, sizeof(base), "%s/TexturePacks", appSupportDir);
    if (k <= 0 || (size_t)k >= sizeof(base)) {
        return 0; // path would truncate — treat as "no packs"
    }

    DIR *d = opendir(base);
    if (d == NULL) {
        return 0; // TexturePacks/ absent (the common no-packs case) or unreadable
    }

    // Collect names first so the result can be sorted independently of the
    // platform's readdir() ordering, which is not guaranteed alphabetical.
    char names[ONI_TP_MAX_PACKS][ONiTP_NameCap];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < ONI_TP_MAX_PACKS) {
        // Skip ".", "..", and any dotfile (e.g. .DS_Store directories).
        if (e->d_name[0] == '.') {
            continue;
        }
        char full[ONI_TP_PATH_MAX];
        int j = snprintf(full, sizeof(full), "%s/%s", base, e->d_name);
        if (j <= 0 || (size_t)j >= sizeof(full)) {
            continue; // path too long for this entry — skip it
        }
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            strncpy(names[n], e->d_name, sizeof(names[n]) - 1);
            names[n][sizeof(names[n]) - 1] = '\0';
            n++;
        }
    }
    closedir(d);

    qsort(names, (size_t)n, sizeof(names[0]), ONiTP_CmpName);

    for (int i = 0; i < n; i++) {
        snprintf(outRoots[i], ONI_TP_PATH_MAX, "%s/%s", base, names[i]);
    }
    return n;
}
