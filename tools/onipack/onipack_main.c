/* onipack_main.c — CLI. Replaces `mono OniSplit.exe -import:sep <dir> <out.dat>`.
 * Every input goes through the group reader (a plain single-texture .oni is
 * just a one-TXMP group); animated groups (frames + TXAN) pack whole.
 * Exit codes: 0 clean, 3 packed but some inputs rejected, 1 nothing packed
 * or write failed, 2 usage/argument error. addresses #88 */
#include "onipack_format.h"
#include "onipack_oni.h"
#include "onipack_writer.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --alpha-guard <tsv>: name\tformat\tpath per line (txmp-format-index output
 * against RETAIL data). Drop staged textures that would replace an
 * alpha-carrying retail format with an alpha-less one (#63). */
static const int kHasAlpha[16] = { 1,0,1,0,0,1,1,1,0,0,0,1,1,1,0,1 };
/* fmt:  0=BGRA4444 1=BGR555 2=BGRA5551 3=I8 4=I1 5=A8 6=A4I4 7=ARGB8888
 *       8=RGB888 9=DXT1 10=RGB_Bytes 11=RGBA_Bytes 12=RGBA5551
 *       13=RGBA4444 14=RGB565 15=ABGR1555   (matches build-hd-overlays.sh) */

typedef struct { char name[OPK_NAME_MAX]; int fmt; } RetailEntry;
static RetailEntry *g_retail; static size_t g_retailN;

static int retail_format(const char *name) {
    for (size_t i = 0; i < g_retailN; i++)
        if (strcmp(g_retail[i].name, name) == 0) return g_retail[i].fmt;
    return -1;
}

static int format_code(const char *s) {   /* txmp-format-index spellings */
    static const char *names[16] = { "BGRA4444","BGR555","BGRA5551","I8","I1",
        "A8","A4I4","RGBA","BGR","DXT1","RGB_Bytes","RGBA_Bytes","RGBA5551",
        "RGBA4444","RGB565","ABGR1555" };
    for (int i = 0; i < 16; i++) if (strcmp(s, names[i]) == 0) return i;
    return -1;
}

static int load_alpha_guard(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "onipack: cannot open alpha-guard %s\n", path); return -1; }
    char line[2048];
    size_t cap = 1024;
    g_retail = malloc(cap * sizeof *g_retail);
    while (fgets(line, sizeof line, f)) {
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        *t1 = '\0';
        char *t2 = strchr(t1 + 1, '\t');
        if (t2) *t2 = '\0';
        int fmt = format_code(t1 + 1);
        if (fmt < 0 || line[0] == '-' ) continue;
        if (g_retailN == cap) g_retail = realloc(g_retail, (cap *= 2) * sizeof *g_retail);
        snprintf(g_retail[g_retailN].name, OPK_NAME_MAX, "%s", line);
        g_retail[g_retailN++].fmt = fmt;
    }
    fclose(f);
    fprintf(stderr, "onipack: alpha-guard loaded, %zu retail entries\n", g_retailN);
    return 0;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(int argc, char **argv) {
    const char *guard = NULL, *dir = NULL, *outDat = NULL;
    int argi = 1;
    if (argi < argc && strcmp(argv[argi], "import-sep") == 0) argi++;
    else { fprintf(stderr, "usage: onipack import-sep [--alpha-guard idx.tsv] <staged-dir> <out.dat>\n"); return 2; }
    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--alpha-guard") == 0 && argi + 1 < argc) guard = argv[++argi];
        else if (!dir) dir = argv[argi];
        else if (!outDat) outDat = argv[argi];
        else { fprintf(stderr, "onipack: unexpected arg %s\n", argv[argi]); return 2; }
    }
    if (!dir || !outDat) { fprintf(stderr, "usage: onipack import-sep [--alpha-guard idx.tsv] <staged-dir> <out.dat>\n"); return 2; }

    /* parse level + suffix from out name: .../level<N>_<Suffix>.dat */
    const char *base = strrchr(outDat, '/');
    base = base ? base + 1 : outDat;
    int level = -1;
    char suffix[64] = "";
    if (sscanf(base, "level%d_%63[^.].dat", &level, suffix) != 2 ||
        level < 0 || level >= 128 || !suffix[0]) {
        fprintf(stderr, "onipack: output must be named level<N>_<Suffix>.dat (got %s)\n", base);
        return 2;
    }
    char outDir[1024];
    if (base == outDat) snprintf(outDir, sizeof outDir, ".");
    else { snprintf(outDir, sizeof outDir, "%.*s", (int)(base - outDat - 1), outDat); }

    if (guard && load_alpha_guard(guard) != 0) return 2;

    OpkPack *pack = opk_pack_new(level, suffix);
    if (!pack) {           /* NULL carries no message (see onipack_writer.h) */
        if (strcmp(suffix, "Final") == 0)
            fprintf(stderr, "onipack: suffix 'Final' is reserved for retail data\n");
        else
            fprintf(stderr, "onipack: suffix must be [A-Za-z0-9]+ (got '%s')\n", suffix);
        return 2;
    }

    /* collect *.oni sorted for determinism */
    DIR *dp = opendir(dir);
    if (!dp) { fprintf(stderr, "onipack: cannot open dir %s\n", dir); opk_pack_free(pack); return 2; }
    char **files = NULL; size_t nf = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(dp))) {
        size_t len = strlen(de->d_name);
        if (len < 5 || strcmp(de->d_name + len - 4, ".oni") != 0) continue;
        if (nf == cap) files = realloc(files, (cap = cap ? cap * 2 : 256) * sizeof *files);
        char *full = malloc(strlen(dir) + len + 2);
        sprintf(full, "%s/%s", dir, de->d_name);
        files[nf++] = full;
    }
    closedir(dp);
    qsort(files, nf, sizeof *files, cmp_str);

    int errors = 0, added = 0, skipped = 0;
    char err[256];
    for (size_t i = 0; i < nf; i++) {
        OpkGroup g;
        if (opk_oni_read_group(files[i], &g, err, sizeof err) != 0) {
            fprintf(stderr, "onipack: SKIP %s: %s\n", files[i], err);
            skipped++; errors++;
            continue;
        }
        if (guard) {       /* base name only: frames are unnamed and cannot
                              collide with retail names */
            int rf = retail_format(g.name);
            uint32_t sf = opk_rd32(g.tex[0].body + OPK_TXMP_FORMAT);
            if (rf >= 0 && kHasAlpha[rf] && sf < 16 && !kHasAlpha[sf]) {
                fprintf(stderr, "onipack: ALPHA-SKIP %s (retail fmt %d has alpha, staged fmt %u does not)\n",
                        g.name, rf, sf);
                opk_group_free(&g);
                skipped++;
                continue;
            }
        }
        if (opk_pack_add_group(pack, &g, err, sizeof err) != 0) {
            fprintf(stderr, "onipack: SKIP %s: %s\n", files[i], err);
            opk_group_free(&g);
            skipped++; errors++;
            continue;
        }
        opk_group_free(&g);   /* buffers were taken by the pack; frees nothing */
        added++;
    }
    for (size_t i = 0; i < nf; i++) free(files[i]);
    free(files);

    if (added == 0) { fprintf(stderr, "onipack: nothing to pack\n"); opk_pack_free(pack); return 1; }
    if (opk_pack_write(pack, outDir, err, sizeof err) != 0) {
        fprintf(stderr, "onipack: write failed: %s\n", err);
        opk_pack_free(pack);
        return 1;
    }
    fprintf(stderr, "onipack: %s: %d textures packed (%d instances incl. frames/TXAN/placeholders), %d skipped\n",
            base, opk_pack_count_textures(pack), opk_pack_count(pack), skipped);
    opk_pack_free(pack);
    return errors ? 3 : 0;   /* 3 = wrote pack but some inputs were rejected */
}
