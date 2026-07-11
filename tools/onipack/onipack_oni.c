/* onipack_oni.c — read one OniSplit V32 TXMP .oni: plain single-texture
 * export or animated group (frame TXMPs + one TXAN).
 * Format facts: plan doc 2026-07-11 + task-4b amendment (VR32 header,
 * rawTableOffset @0x30, raw offsets relative to rawTableOffset,
 * tag-prefixed name table; TXAN per BFW_Motoko.h:830-844 with the
 * maps[0]==0 base-frame convention, see onipack_format.h).
 * addresses #88 */
#include "onipack_format.h"
#include "onipack_oni.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t errsz, const char *msg) {
    snprintf(err, errsz, "%s", msg);
    return -1;
}

void opk_percent_decode(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '%' && isxdigit((unsigned char)r[1]) &&
            isxdigit((unsigned char)r[2])) {
            char hex[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

void opk_name_from_filename(const char *path, char out[OPK_NAME_MAX]) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strncmp(base, "TXMP", 4) == 0) base += 4;   /* OniSplit convention */
    snprintf(out, OPK_NAME_MAX, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot && strcmp(dot, ".oni") == 0) *dot = '\0';
    opk_percent_decode(out);
}

/* look up the tag-prefixed name of descriptor `idx`; strips the 4-char tag */
static int desc_name(const uint8_t *buf, long fsize, uint32_t nameTabOff,
                     uint32_t nameTabLen, uint32_t descIdx, uint32_t nDescs,
                     char out[OPK_NAME_MAX]) {
    if (descIdx >= nDescs) return -1;
    uint32_t nameOff = opk_rd32(buf + 64 + descIdx * OPK_IDESC_SIZE + 8);
    uint32_t flags   = opk_rd32(buf + 64 + descIdx * OPK_IDESC_SIZE + 16) & 0xFFu;
    if (flags & OPK_FLAG_UNIQUE) return -1;         /* unnamed */
    if ((uint64_t)nameTabOff + nameTabLen > (uint64_t)fsize) return -1;
    if (nameOff >= nameTabLen) return -1;
    const char *full = (const char *)buf + nameTabOff + nameOff;
    const char *nul  = memchr(full, '\0', nameTabLen - nameOff);
    if (nul == NULL || nul <= full + 4)
        return -1;      /* unterminated, NUL inside the 4CC tag, or empty name */
    snprintf(out, OPK_NAME_MAX, "%s", full + 4);    /* strip 4CC tag */
    return 0;
}

/* resolve one TXMP link slot into an OpkRef: 0 -> none; real in-file
 * instance -> local descriptor index; placeholder -> name-table name.
 * The target descriptor's tag must equal wantTag either way. */
static int resolve_ref(const uint8_t *buf, long fsize, uint32_t nameTabOff,
                       uint32_t nameTabLen, uint32_t nInst, uint32_t id,
                       uint32_t wantTag, OpkRef *ref, char *err, size_t errsz,
                       const char *wrongTagMsg, const char *noNameMsg) {
    ref->local = -1;
    ref->name[0] = '\0';
    if (id == 0) return 0;                          /* no link */
    if (!(id & 1u))
        return fail(err, errsz, "malformed link id (nonzero, even)");
    uint32_t idx = id >> 8;
    if (idx >= nInst)
        return fail(err, errsz, "link target descriptor out of range");
    const uint8_t *d = buf + 64 + idx * OPK_IDESC_SIZE;
    uint32_t dataOff = opk_rd32(d + 4);
    uint32_t flags   = opk_rd32(d + 16) & 0xFFu;
    if (opk_rd32(d) != wantTag)
        return fail(err, errsz, wrongTagMsg);
    if (dataOff != 0 && !(flags & OPK_FLAG_PLACEHOLDER)) {
        ref->local = (int32_t)idx;                  /* intra-file ref */
        return 0;
    }
    if (desc_name(buf, fsize, nameTabOff, nameTabLen, idx, nInst,
                  ref->name) != 0)
        return fail(err, errsz, noNameMsg);
    return 0;
}

int opk_oni_read_group(const char *path, OpkGroup *g, char *err, size_t errsz) {
    memset(g, 0, sizeof *g);

    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, errsz, "cannot open");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize < 64) { fclose(f); return fail(err, errsz, "too small"); }
    uint8_t *buf = malloc((size_t)fsize);
    if (!buf || fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(buf); fclose(f); return fail(err, errsz, "read failed");
    }
    fclose(f);

    uint32_t version = opk_rd32(buf + OPK_HDR_VERSION);
    if (version != OPK_VERSION_32) {
        fail(err, errsz, version == OPK_VERSION_33
            ? "VR33 .oni not supported (OniSplit 1.0 output; re-export with 0.9x)"
            : "not a V32 .oni file");
        goto bad;
    }
    uint32_t nInst      = opk_rd32(buf + OPK_HDR_NINST);
    uint32_t dataTabOff = opk_rd32(buf + OPK_HDR_DATAOFF);
    uint32_t nameTabOff = opk_rd32(buf + OPK_HDR_NAMEOFF);
    uint32_t nameTabLen = opk_rd32(buf + OPK_HDR_NAMELEN);
    uint32_t rawTabOff  = opk_rd32(buf + OPK_HDR_RAWOFF_V32);
    if (64 + (uint64_t)nInst * OPK_IDESC_SIZE > (uint64_t)fsize) {
        fail(err, errsz, "descriptor table out of range");
        goto bad;
    }

    /* classify: 1..OPK_GROUP_MAX real TXMPs + at most one real TXAN (#62) */
    int32_t txanIdx = -1;
    for (uint32_t i = 0; i < nInst; i++) {
        const uint8_t *d = buf + 64 + i * OPK_IDESC_SIZE;
        uint32_t dataOff = opk_rd32(d + 4);
        uint32_t flags   = opk_rd32(d + 16) & 0xFFu;
        if (dataOff == 0 || (flags & OPK_FLAG_PLACEHOLDER)) continue;
        uint32_t tag = opk_rd32(d);
        if (tag == OPK_TAG_TXMP) {
            if (g->nTex >= OPK_GROUP_MAX) {
                snprintf(err, errsz, "too many TXMP instances (limit %d)",
                         OPK_GROUP_MAX);
                goto bad;
            }
            g->tex[g->nTex++].localIdx = (int32_t)i;
        } else if (tag == OPK_TAG_TXAN) {
            if (txanIdx >= 0) {
                fail(err, errsz, "multiple TXAN instances");
                goto bad;
            }
            txanIdx = (int32_t)i;
        } else {
            fail(err, errsz, "instance is not TXMP — TXMP-only rule (#62)");
            goto bad;
        }
    }
    if (g->nTex == 0) { fail(err, errsz, "no instance data"); goto bad; }

    opk_name_from_filename(path, g->name);

    /* per-TXMP: body + texel blob + link refs */
    for (uint32_t t = 0; t < g->nTex; t++) {
        OpkGroupTex *gt = &g->tex[t];
        const uint8_t *d = buf + 64 + (uint32_t)gt->localIdx * OPK_IDESC_SIZE;
        uint32_t dataOff = opk_rd32(d + 4);
        if ((uint64_t)dataTabOff + dataOff + OPK_TXMP_BODY > (uint64_t)fsize) {
            fail(err, errsz, "TXMP body out of range");
            goto bad;
        }
        memcpy(gt->body, buf + dataTabOff + dataOff, OPK_TXMP_BODY);

        /* texel blob: whichever of the raw/sep slots is nonzero, relative to
         * rawTableOffset. _os sizing: this is an OniSplit-WRITTEN file
         * (DXT1 floor tails). */
        uint32_t w    = opk_rd16(gt->body + OPK_TXMP_WIDTH);
        uint32_t h    = opk_rd16(gt->body + OPK_TXMP_HEIGHT);
        uint32_t fmt  = opk_rd32(gt->body + OPK_TXMP_FORMAT);
        uint32_t flg  = opk_rd32(gt->body + OPK_TXMP_FLAGS);
        uint32_t poff = opk_rd32(gt->body + OPK_TXMP_RAWOFF);
        if (poff == 0) poff = opk_rd32(gt->body + OPK_TXMP_SEPOFF);
        if (poff == 0) { fail(err, errsz, "no texel data"); goto bad; }
        gt->pixelsSize = opk_texel_bytes_os(fmt, w, h,
                                            (flg & OPK_TXMP_FLAG_HASMIPMAP) != 0);
        if (gt->pixelsSize == 0) {
            fail(err, errsz, "unrecognized texel format");
            goto bad;
        }
        if ((uint64_t)rawTabOff + poff + gt->pixelsSize > (uint64_t)fsize) {
            fail(err, errsz, "texel data out of range");
            goto bad;
        }
        gt->pixels = malloc(gt->pixelsSize);
        if (!gt->pixels) { fail(err, errsz, "out of memory"); goto bad; }
        memcpy(gt->pixels, buf + rawTabOff + poff, gt->pixelsSize);

        if (resolve_ref(buf, fsize, nameTabOff, nameTabLen, nInst,
                        opk_rd32(gt->body + OPK_TXMP_ANIM), OPK_TAG_TXAN,
                        &gt->anim, err, errsz,
                        "anim link target is not TXAN",
                        "anim link target has no name") != 0)
            goto bad;
        if (resolve_ref(buf, fsize, nameTabOff, nameTabLen, nInst,
                        opk_rd32(gt->body + OPK_TXMP_ENVMAP), OPK_TAG_TXMP,
                        &gt->envMap, err, errsz,
                        "envmap link target is not TXMP",
                        "envmap link target has no name") != 0)
            goto bad;
    }

    /* TXAN: copy body verbatim; maps[] stay file-local (writer remaps) */
    if (txanIdx >= 0) {
        const uint8_t *d = buf + 64 + (uint32_t)txanIdx * OPK_IDESC_SIZE;
        uint32_t dataOff = opk_rd32(d + 4);
        uint32_t recSize = opk_rd32(d + 12);
        if ((uint64_t)dataTabOff + dataOff + OPK_TXAN_MAPS > (uint64_t)fsize) {
            fail(err, errsz, "TXAN body out of range");
            goto bad;
        }
        uint32_t nFrames =
            opk_rd32(buf + dataTabOff + dataOff + OPK_TXAN_NUMFRAMES);
        if (nFrames < 1 || nFrames > 255) {
            fail(err, errsz, "TXAN frame count out of range");
            goto bad;
        }
        uint32_t bodySize = OPK_TXAN_MAPS + 4u * nFrames;
        if (8u + bodySize > recSize) {
            fail(err, errsz, "TXAN body exceeds its record");
            goto bad;
        }
        if ((uint64_t)dataTabOff + dataOff + bodySize > (uint64_t)fsize) {
            fail(err, errsz, "TXAN body out of range");
            goto bad;
        }
        g->txanBody = malloc(bodySize);
        if (!g->txanBody) { fail(err, errsz, "out of memory"); goto bad; }
        memcpy(g->txanBody, buf + dataTabOff + dataOff, bodySize);
        g->txanBodySize = bodySize;
        g->hasTxan = 1;
        g->txanLocalIdx = txanIdx;

        /* maps[0]==0 means "the base texture" (engine convention); every
         * other entry must reference a real TXMP in this file */
        for (uint32_t fi = 0; fi < nFrames; fi++) {
            uint32_t id = opk_rd32(g->txanBody + OPK_TXAN_MAPS + 4u * fi);
            int ok = 0;
            if (id == 0) {
                ok = (fi == 0);
            } else if (id & 1u) {
                uint32_t idx = id >> 8;
                for (uint32_t t = 0; t < g->nTex; t++)
                    if ((uint32_t)g->tex[t].localIdx == idx) { ok = 1; break; }
            }
            if (!ok) {
                fail(err, errsz, "TXAN frame ref is not a TXMP in this file");
                goto bad;
            }
        }

        /* base identity: the base is the ONE TXMP whose anim slot references
         * the TXAN — descriptor order is not reliable (24100-mod files put
         * the TXAN at descriptor 1). Swap the carrier into tex[0]; localIdx
         * values stay truthful. */
        long baseT = -1;
        for (uint32_t t = 0; t < g->nTex; t++) {
            if (g->tex[t].anim.local == txanIdx) {
                if (baseT >= 0) {
                    fail(err, errsz, "multiple TXMPs reference the TXAN");
                    goto bad;
                }
                baseT = (long)t;
            }
        }
        if (baseT < 0) {
            fail(err, errsz, "no TXMP references the TXAN (missing base)");
            goto bad;
        }
        if (baseT != 0) {
            OpkGroupTex tmp = g->tex[0];
            g->tex[0] = g->tex[baseT];
            g->tex[baseT] = tmp;
        }
    }

    free(buf);
    return 0;

bad:
    opk_group_free(g);
    free(buf);
    return -1;
}

int opk_oni_read(const char *path, OpkTexture *out, char *err, size_t errsz) {
    OpkGroup g;

    memset(out, 0, sizeof *out);
    if (opk_oni_read_group(path, &g, err, errsz) != 0) return -1;
    if (g.nTex != 1 || g.hasTxan) {
        opk_group_free(&g);
        return fail(err, errsz,
            "multi-instance file — use group ingest (#62 scope: TXMP/TXAN only)");
    }
    memcpy(out->body, g.tex[0].body, sizeof out->body);
    out->pixels     = g.tex[0].pixels;              /* steal ownership */
    out->pixelsSize = g.tex[0].pixelsSize;
    g.tex[0].pixels = NULL;
    memcpy(out->name, g.name, OPK_NAME_MAX);
    memcpy(out->animName, g.tex[0].anim.name, OPK_NAME_MAX);
    memcpy(out->envMapName, g.tex[0].envMap.name, OPK_NAME_MAX);
    opk_group_free(&g);
    return 0;
}

void opk_texture_free(OpkTexture *t) {
    free(t->pixels);
    t->pixels = NULL;
    t->pixelsSize = 0;
}

void opk_group_free(OpkGroup *g) {
    uint32_t n = g->nTex <= OPK_GROUP_MAX ? g->nTex : OPK_GROUP_MAX;
    for (uint32_t i = 0; i < n; i++) {
        free(g->tex[i].pixels);
        g->tex[i].pixels = NULL;
        g->tex[i].pixelsSize = 0;
    }
    free(g->txanBody);
    g->txanBody = NULL;
    g->txanBodySize = 0;
    g->nTex = 0;
    g->hasTxan = 0;
}
