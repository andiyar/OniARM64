/* onipack_writer.c — VR31 Mac-family pack emitter. Layout mirrors OniSplit
 * InstanceFileWriter (facts pinned 2026-07-11): tables -> align32 -> records
 * (8B preamble + body, size incl preamble, 32-aligned) -> name table; blobs
 * in .sep from offset 32, align32 steps; .raw = 32-byte zero stub; atomic
 * .tmp + rename, .dat renamed last. Task-4b deltas: unnamed (Unique) frame
 * TXMPs carry no name string or name descriptor; TXAN records are
 * align32(8+bodySize) with no sep blob, maps[] remapped to pack indices and
 * the maps[0]==0 base-frame convention preserved verbatim. addresses #88 */
#include "onipack_format.h"
#include "onipack_writer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define OPK_MAX_INSTANCES 8192

typedef struct {
    char     name[OPK_NAME_MAX];  /* instance name (no tag); "" if unnamed */
    uint32_t tag;                 /* TXMP, or TXAN for animation instances */
    int      isPlaceholder;
    int      unnamed;             /* Unique: no name string, no name desc */
    int      isTxan;
    /* real TXMPs only: */
    uint8_t  body[168];
    uint8_t *pixels;
    uint32_t pixelsSize;
    long     animTarget;          /* index into entries, -1 none */
    long     envTarget;
    /* real TXANs only: */
    uint8_t *txanBody;            /* maps[] already hold pack ids */
    uint32_t txanSize;
} OpkEntry;

struct OpkPack {
    int      level;
    char     suffix[64];
    OpkEntry e[OPK_MAX_INSTANCES];
    uint32_t n;
};

static int fail(char *err, size_t errsz, const char *msg) {
    snprintf(err, errsz, "%s", msg);
    return -1;
}

OpkPack *opk_pack_new(int level, const char *suffix) {
    if (level < 0 || level >= 128) return NULL;          /* engine parse limit */
    if (strcmp(suffix, "Final") == 0) return NULL;       /* overlay contract */
    if (!*suffix || strlen(suffix) >= sizeof ((OpkPack *)0)->suffix) return NULL;
    OpkPack *p = calloc(1, sizeof *p);
    if (p) { p->level = level; snprintf(p->suffix, sizeof p->suffix, "%s", suffix); }
    return p;
}

static long find_entry(const OpkPack *p, uint32_t tag, const char *name) {
    for (uint32_t i = 0; i < p->n; i++)
        if (!p->e[i].unnamed && p->e[i].tag == tag &&
            strcmp(p->e[i].name, name) == 0)
            return (long)i;
    return -1;
}

static long add_placeholder(OpkPack *p, uint32_t tag, const char *name) {
    long i = find_entry(p, tag, name);
    if (i >= 0) return i;
    if (p->n >= OPK_MAX_INSTANCES) return -1;
    OpkEntry *e = &p->e[p->n];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    e->tag = tag;
    e->isPlaceholder = 1;
    e->animTarget = e->envTarget = -1;
    return (long)p->n++;
}

int opk_pack_add(OpkPack *p, OpkTexture *tex, char *err, size_t errsz) {
    long existing = find_entry(p, OPK_TAG_TXMP, tex->name);
    if (existing >= 0 && !p->e[existing].isPlaceholder)
        return fail(err, errsz, "duplicate instance name");
    if (p->n >= OPK_MAX_INSTANCES)
        return fail(err, errsz, "pack full");

    OpkEntry *e;
    if (existing >= 0) {
        e = &p->e[existing];       /* upgrade placeholder to real instance */
        e->isPlaceholder = 0;
    } else {
        e = &p->e[p->n++];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", tex->name);
        e->tag = OPK_TAG_TXMP;
    }
    memcpy(e->body, tex->body, sizeof e->body);
    e->pixels = tex->pixels;       /* ownership transferred */
    tex->pixels = NULL;
    e->pixelsSize = tex->pixelsSize;
    e->animTarget = tex->animName[0]
        ? add_placeholder(p, OPK_TAG_TXAN, tex->animName) : -1;
    e->envTarget = tex->envMapName[0]
        ? add_placeholder(p, OPK_TAG_TXMP, tex->envMapName) : -1;
    if ((tex->animName[0] && e->animTarget < 0) ||
        (tex->envMapName[0] && e->envTarget < 0))
        return fail(err, errsz, "pack full (placeholders)");
    return 0;
}

static long map_lookup(const int32_t *mLoc, const long *mPack, uint32_t nMap,
                       int32_t local) {
    for (uint32_t i = 0; i < nMap; i++)
        if (mLoc[i] == local) return mPack[i];
    return -1;
}

int opk_pack_add_group(OpkPack *p, OpkGroup *g, char *err, size_t errsz) {
    /* ---- validate everything first: on error the pack is unchanged ---- */
    if (g->nTex == 0 || g->nTex > OPK_GROUP_MAX)
        return fail(err, errsz, "bad group");
    long existing = find_entry(p, OPK_TAG_TXMP, g->name);
    if (existing >= 0 && !p->e[existing].isPlaceholder)
        return fail(err, errsz, "duplicate instance name");
    /* conservative capacity: group + TXAN + every named ref as a fresh
     * placeholder — guarantees no mid-add failure below */
    if (p->n + 3u * g->nTex + 1u > OPK_MAX_INSTANCES)
        return fail(err, errsz, "pack full");
    uint32_t nFrames = 0;
    if (g->hasTxan) {
        if (g->txanBodySize < OPK_TXAN_MAPS + 4)
            return fail(err, errsz, "bad TXAN body");
        nFrames = opk_rd32(g->txanBody + OPK_TXAN_NUMFRAMES);
        if (nFrames < 1 || OPK_TXAN_MAPS + 4u * nFrames > g->txanBodySize)
            return fail(err, errsz, "bad TXAN body");
    }
    for (uint32_t t = 0; t < g->nTex; t++) {
        if (g->tex[t].anim.local >= 0 &&
            (!g->hasTxan || g->tex[t].anim.local != g->txanLocalIdx))
            return fail(err, errsz, "group anim ref outside group");
        if (g->tex[t].envMap.local >= 0) {
            int found = 0;
            for (uint32_t u = 0; u < g->nTex; u++)
                if (g->tex[u].localIdx == g->tex[t].envMap.local) {
                    found = 1; break;
                }
            if (!found)
                return fail(err, errsz, "group envmap ref outside group");
        }
    }
    if (g->hasTxan) {
        for (uint32_t fi = 0; fi < nFrames; fi++) {
            uint32_t id = opk_rd32(g->txanBody + OPK_TXAN_MAPS + 4u * fi);
            if (id == 0) continue;              /* base-frame convention */
            int found = 0;
            if (id & 1u)
                for (uint32_t u = 0; u < g->nTex; u++)
                    if ((uint32_t)g->tex[u].localIdx == (id >> 8)) {
                        found = 1; break;
                    }
            if (!found)
                return fail(err, errsz, "TXAN frame ref outside group");
        }
    }

    /* ---- mutate: nothing below can fail ---- */
    int32_t mLoc[OPK_GROUP_MAX + 1];
    long    mPack[OPK_GROUP_MAX + 1], texPack[OPK_GROUP_MAX];
    uint32_t nMap = 0;
    for (uint32_t t = 0; t < g->nTex; t++) {
        OpkEntry *e;
        long idx;
        if (t == 0 && existing >= 0) {
            idx = existing;                     /* upgrade placeholder */
            e = &p->e[idx];
            e->isPlaceholder = 0;
        } else {
            idx = (long)p->n++;
            e = &p->e[idx];
            memset(e, 0, sizeof *e);
            e->tag = OPK_TAG_TXMP;
            if (t == 0) snprintf(e->name, sizeof e->name, "%s", g->name);
            else e->unnamed = 1;                /* frames: Unique */
        }
        memcpy(e->body, g->tex[t].body, sizeof e->body);
        e->pixels = g->tex[t].pixels;           /* ownership transferred */
        g->tex[t].pixels = NULL;
        e->pixelsSize = g->tex[t].pixelsSize;
        e->animTarget = e->envTarget = -1;
        mLoc[nMap] = g->tex[t].localIdx; mPack[nMap] = idx; nMap++;
        texPack[t] = idx;
    }
    long txanPack = -1;
    if (g->hasTxan) {
        txanPack = (long)p->n++;
        OpkEntry *e = &p->e[txanPack];
        memset(e, 0, sizeof *e);
        e->tag = OPK_TAG_TXAN;
        e->unnamed = 1;
        e->isTxan = 1;
        e->animTarget = e->envTarget = -1;
        e->txanBody = g->txanBody;              /* ownership transferred */
        g->txanBody = NULL;
        e->txanSize = g->txanBodySize;
        mLoc[nMap] = g->txanLocalIdx; mPack[nMap] = txanPack; nMap++;

        /* remap maps[]: file-local -> pack ids; zero stays verbatim */
        for (uint32_t fi = 0; fi < nFrames; fi++) {
            uint8_t *slot = e->txanBody + OPK_TXAN_MAPS + 4u * fi;
            uint32_t id = opk_rd32(slot);
            if (id == 0) continue;
            long np = map_lookup(mLoc, mPack, nMap, (int32_t)(id >> 8));
            opk_wr32(slot, ((uint32_t)np << 8) | 1u);
        }
    }
    /* link slots -> pack targets; named refs become placeholders */
    for (uint32_t t = 0; t < g->nTex; t++) {
        OpkEntry *e = &p->e[texPack[t]];
        if (g->tex[t].anim.local >= 0)
            e->animTarget = txanPack;
        else if (g->tex[t].anim.name[0])
            e->animTarget = add_placeholder(p, OPK_TAG_TXAN,
                                            g->tex[t].anim.name);
        if (g->tex[t].envMap.local >= 0)
            e->envTarget = map_lookup(mLoc, mPack, nMap,
                                      g->tex[t].envMap.local);
        else if (g->tex[t].envMap.name[0])
            e->envTarget = add_placeholder(p, OPK_TAG_TXMP,
                                           g->tex[t].envMap.name);
    }
    return 0;
}

int opk_pack_count(const OpkPack *p) { return (int)p->n; }

/* name-descriptor sort: byte-ordinal on tag-prefixed full name */
static char full_name_buf[2][OPK_NAME_MAX + 8];
static const char *full_name(const OpkPack *p, uint32_t i, int slot) {
    char tag[5] = { (char)(p->e[i].tag >> 24), (char)(p->e[i].tag >> 16),
                    (char)(p->e[i].tag >> 8),  (char)(p->e[i].tag), 0 };
    snprintf(full_name_buf[slot], sizeof full_name_buf[0], "%s%s",
             tag, p->e[i].name);
    return full_name_buf[slot];
}
static const OpkPack *g_sortPack;
static int cmp_by_full_name(const void *a, const void *b) {
    uint32_t ia = *(const uint32_t *)a, ib = *(const uint32_t *)b;
    return strcmp(full_name(g_sortPack, ia, 0), full_name(g_sortPack, ib, 1));
}

static int write_file(const char *path, const uint8_t *data, size_t size,
                      char *err, size_t errsz) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return fail(err, errsz, "cannot create output file");
    if (size && fwrite(data, 1, size, f) != size) {
        fclose(f); remove(tmp);
        return fail(err, errsz, "short write");
    }
    fclose(f);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return fail(err, errsz, "rename failed");
    }
    return 0;
}

int opk_pack_write(OpkPack *p, const char *outDir, char *err, size_t errsz) {
    if (p->n == 0) return fail(err, errsz, "empty pack");
    mkdir(outDir, 0755);           /* EEXIST is fine */

    uint32_t fileId = opk_file_id(p->level, p->suffix);

    /* ---- layout ---- */
    uint32_t nName = 0;            /* named entries only (frames/TXAN Unique) */
    uint32_t tags[2] = { 0, 0 };   /* ascending: TXAN < TXMP by value */
    uint32_t tagCount[2] = { 0, 0 }, nTempl = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        uint32_t slot = (p->e[i].tag == OPK_TAG_TXAN) ? 0 : 1;
        if (tagCount[slot]++ == 0) nTempl++;
        tags[slot] = p->e[i].tag;
        if (!p->e[i].unnamed) nName++;
    }
    uint32_t tables  = OPK_HDR_SIZE + p->n * OPK_IDESC_SIZE +
                       nName * OPK_NDESC_SIZE + nTempl * OPK_TDESC_SIZE;
    uint32_t dataOff = opk_align32(tables);

    /* per-entry data offsets + name offsets + sep offsets */
    uint32_t dataCur = 0, sepCur = OPK_BLOB_BASE, nameCur = 0;
    uint32_t *recOff  = calloc(p->n, 4);
    uint32_t *nameOff = calloc(p->n, 4);
    uint32_t *sepOff  = calloc(p->n, 4);
    for (uint32_t i = 0; i < p->n; i++) {
        if (!p->e[i].unnamed) {
            nameOff[i] = nameCur;
            nameCur += 4 + (uint32_t)strlen(p->e[i].name) + 1;
        }
        if (p->e[i].isPlaceholder) continue;
        recOff[i] = dataCur + OPK_PREAMBLE;
        if (p->e[i].isTxan) {
            dataCur += opk_align32(OPK_PREAMBLE + p->e[i].txanSize);
        } else {
            dataCur += opk_align32(OPK_PREAMBLE + OPK_TXMP_BODY);   /* 192 */
            sepOff[i] = sepCur;
            sepCur    = opk_align32(sepCur + p->e[i].pixelsSize);
        }
    }
    uint32_t nameBlockOff = dataOff + dataCur;
    size_t datSize = nameBlockOff + nameCur;
    uint8_t *dat = calloc(1, datSize);

    /* ---- header ---- */
    opk_wr64(dat + OPK_HDR_CHECKSUM, OPK_CHECKSUM_MAC);
    opk_wr32(dat + OPK_HDR_VERSION, OPK_VERSION_31);
    opk_wr16(dat + OPK_HDR_SIZES + 0, OPK_HDR_SIZE);
    opk_wr16(dat + OPK_HDR_SIZES + 2, OPK_IDESC_SIZE);
    opk_wr16(dat + OPK_HDR_SIZES + 4, OPK_TDESC_SIZE);
    opk_wr16(dat + OPK_HDR_SIZES + 6, OPK_NDESC_SIZE);
    opk_wr32(dat + OPK_HDR_NINST, p->n);
    opk_wr32(dat + OPK_HDR_NNAME, nName);
    opk_wr32(dat + OPK_HDR_NTEMPL, nTempl);
    opk_wr32(dat + OPK_HDR_DATAOFF, dataOff);
    opk_wr32(dat + OPK_HDR_DATALEN, dataCur);
    opk_wr32(dat + OPK_HDR_NAMEOFF, nameBlockOff);
    opk_wr32(dat + OPK_HDR_NAMELEN, nameCur);

    /* ---- instance descriptors + records + name table ---- */
    for (uint32_t i = 0; i < p->n; i++) {
        uint8_t *d = dat + OPK_HDR_SIZE + i * OPK_IDESC_SIZE;
        opk_wr32(d + 0, p->e[i].tag);
        opk_wr32(d + 8, p->e[i].unnamed ? 0 : nameOff[i]);
        if (p->e[i].isPlaceholder) {
            opk_wr32(d + 16, OPK_FLAG_PLACEHOLDER);
        } else {
            uint32_t recSize = p->e[i].isTxan
                ? opk_align32(OPK_PREAMBLE + p->e[i].txanSize)
                : opk_align32(OPK_PREAMBLE + OPK_TXMP_BODY);
            opk_wr32(d + 4, recOff[i]);
            opk_wr32(d + 12, recSize);
            if (p->e[i].unnamed) opk_wr32(d + 16, OPK_FLAG_UNIQUE);
            uint8_t *rec = dat + dataOff + recOff[i] - OPK_PREAMBLE;
            opk_wr32(rec + 0, (i << 8) | 1u);
            opk_wr32(rec + 4, fileId);
            uint8_t *body = rec + OPK_PREAMBLE;
            if (p->e[i].isTxan) {
                memcpy(body, p->e[i].txanBody, p->e[i].txanSize);
            } else {
                memcpy(body, p->e[i].body, OPK_TXMP_BODY);
                uint32_t flg = opk_rd32(body + OPK_TXMP_FLAGS) | OPK_TXMP_FLAG_LE;
                opk_wr32(body + OPK_TXMP_FLAGS, flg);
                opk_wr32(body + OPK_TXMP_ANIM, p->e[i].animTarget >= 0
                    ? (((uint32_t)p->e[i].animTarget << 8) | 1u) : 0);
                opk_wr32(body + OPK_TXMP_ENVMAP, p->e[i].envTarget >= 0
                    ? (((uint32_t)p->e[i].envTarget << 8) | 1u) : 0);
                opk_wr32(body + OPK_TXMP_RAWOFF, 0);
                opk_wr32(body + OPK_TXMP_SEPOFF, sepOff[i]);
            }
        }
        if (!p->e[i].unnamed) {
            char *nm = (char *)dat + nameBlockOff + nameOff[i];
            nm[0] = (char)(p->e[i].tag >> 24); nm[1] = (char)(p->e[i].tag >> 16);
            nm[2] = (char)(p->e[i].tag >> 8);  nm[3] = (char)(p->e[i].tag);
            strcpy(nm + 4, p->e[i].name);
        }
    }

    /* ---- name descriptors (named entries only, sorted) ---- */
    uint32_t *order = calloc(p->n, 4);
    uint32_t no = 0;
    for (uint32_t i = 0; i < p->n; i++)
        if (!p->e[i].unnamed) order[no++] = i;
    g_sortPack = p;
    qsort(order, no, 4, cmp_by_full_name);
    uint8_t *nd = dat + OPK_HDR_SIZE + p->n * OPK_IDESC_SIZE;
    for (uint32_t i = 0; i < no; i++)
        opk_wr32(nd + i * OPK_NDESC_SIZE, order[i]);   /* second u32 stays 0 */

    /* ---- template descriptors (ascending tag) ---- */
    uint8_t *td = nd + nName * OPK_NDESC_SIZE;
    for (int slot = 0; slot < 2; slot++) {
        if (tagCount[slot] == 0) continue;
        opk_wr64(td, tags[slot] == OPK_TAG_TXMP ? OPK_TDESC_CHECKSUM_TXMP
                                                : OPK_TDESC_CHECKSUM_TXAN);
        opk_wr32(td + 8, tags[slot]);
        opk_wr32(td + 12, tagCount[slot]);
        td += OPK_TDESC_SIZE;
    }

    /* ---- .sep (real TXMPs only) ---- */
    uint8_t *sep = calloc(1, sepCur);
    for (uint32_t i = 0; i < p->n; i++)
        if (!p->e[i].isPlaceholder && !p->e[i].isTxan)
            memcpy(sep + sepOff[i], p->e[i].pixels, p->e[i].pixelsSize);

    /* ---- emit: raw + sep first, .dat LAST (pack becomes visible atomically) */
    char path[1024];
    uint8_t rawStub[OPK_BLOB_BASE] = {0};
    int rc = 0;
    snprintf(path, sizeof path, "%s/level%d_%s.raw", outDir, p->level, p->suffix);
    rc = write_file(path, rawStub, sizeof rawStub, err, errsz);
    if (rc == 0) {
        snprintf(path, sizeof path, "%s/level%d_%s.sep", outDir, p->level, p->suffix);
        rc = write_file(path, sep, sepCur, err, errsz);
    }
    if (rc == 0) {
        snprintf(path, sizeof path, "%s/level%d_%s.dat", outDir, p->level, p->suffix);
        rc = write_file(path, dat, datSize, err, errsz);
    }

    free(recOff); free(nameOff); free(sepOff); free(order);
    free(dat); free(sep);
    return rc;
}

void opk_pack_free(OpkPack *p) {
    if (!p) return;
    for (uint32_t i = 0; i < p->n; i++) {
        free(p->e[i].pixels);
        free(p->e[i].txanBody);
    }
    free(p);
}
