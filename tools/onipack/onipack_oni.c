/* onipack_oni.c — read one OniSplit V32 single-instance TXMP .oni.
 * Format facts: plan doc 2026-07-11 (VR32 header, rawTableOffset @0x30,
 * raw offsets relative to rawTableOffset, tag-prefixed name table).
 * addresses #88 */
#include "onipack_format.h"
#include "onipack_oni.h"

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
    uint32_t flags   = opk_rd32(buf + 64 + descIdx * OPK_IDESC_SIZE + 16);
    if (flags & OPK_FLAG_UNIQUE) return -1;         /* unnamed */
    if (nameOff + 5 > nameTabLen) return -1;
    if ((long)nameTabOff + nameOff + 5 > fsize) return -1;
    const char *full = (const char *)buf + nameTabOff + nameOff;
    snprintf(out, OPK_NAME_MAX, "%s", full + 4);    /* strip 4CC tag */
    return 0;
}

int opk_oni_read(const char *path, OpkTexture *out, char *err, size_t errsz) {
    memset(out, 0, sizeof *out);

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
        free(buf);
        return fail(err, errsz, version == OPK_VERSION_33
            ? "VR33 .oni not supported (OniSplit 1.0 output; re-export with 0.9x)"
            : "not a V32 .oni file");
    }
    uint32_t nInst      = opk_rd32(buf + OPK_HDR_NINST);
    uint32_t dataTabOff = opk_rd32(buf + OPK_HDR_DATAOFF);
    uint32_t nameTabOff = opk_rd32(buf + OPK_HDR_NAMEOFF);
    uint32_t nameTabLen = opk_rd32(buf + OPK_HDR_NAMELEN);
    uint32_t rawTabOff  = opk_rd32(buf + OPK_HDR_RAWOFF_V32);
    if (64 + (uint64_t)nInst * OPK_IDESC_SIZE > (uint64_t)fsize) {
        free(buf); return fail(err, errsz, "descriptor table out of range");
    }

    /* exactly one non-placeholder instance, tag TXMP (#62 guard) */
    long realIdx = -1;
    for (uint32_t i = 0; i < nInst; i++) {
        const uint8_t *d = buf + 64 + i * OPK_IDESC_SIZE;
        uint32_t dataOff = opk_rd32(d + 4);
        uint32_t flags   = opk_rd32(d + 16) & 0xFFu;
        if (dataOff == 0 || (flags & OPK_FLAG_PLACEHOLDER)) continue;
        if (realIdx >= 0) {
            free(buf);
            return fail(err, errsz,
                "multiple non-placeholder instances — TXMP-only rule (#62)");
        }
        if (opk_rd32(d) != OPK_TAG_TXMP) {
            free(buf);
            return fail(err, errsz,
                "instance is not TXMP — TXMP-only rule (#62)");
        }
        realIdx = (long)i;
    }
    if (realIdx < 0) { free(buf); return fail(err, errsz, "no instance data"); }

    const uint8_t *d = buf + 64 + (uint32_t)realIdx * OPK_IDESC_SIZE;
    uint32_t dataOff = opk_rd32(d + 4);
    if ((uint64_t)dataTabOff + dataOff + OPK_TXMP_BODY > (uint64_t)fsize) {
        free(buf); return fail(err, errsz, "TXMP body out of range");
    }
    memcpy(out->body, buf + dataTabOff + dataOff, OPK_TXMP_BODY);
    opk_name_from_filename(path, out->name);

    /* texel blob: whichever of the raw/sep slots is nonzero, rel rawTableOffset.
     * _os sizing: we are slicing an OniSplit-WRITTEN file (DXT1 floor tails). */
    uint32_t w    = opk_rd16(out->body + OPK_TXMP_WIDTH);
    uint32_t h    = opk_rd16(out->body + OPK_TXMP_HEIGHT);
    uint32_t fmt  = opk_rd32(out->body + OPK_TXMP_FORMAT);
    uint32_t flg  = opk_rd32(out->body + OPK_TXMP_FLAGS);
    uint32_t poff = opk_rd32(out->body + OPK_TXMP_RAWOFF);
    if (poff == 0) poff = opk_rd32(out->body + OPK_TXMP_SEPOFF);
    if (poff == 0) { free(buf); return fail(err, errsz, "no texel data"); }
    out->pixelsSize = opk_texel_bytes_os(fmt, w, h,
                                         (flg & OPK_TXMP_FLAG_HASMIPMAP) != 0);
    if (out->pixelsSize == 0) {
        free(buf); return fail(err, errsz, "unrecognized texel format");
    }
    if ((uint64_t)rawTabOff + poff + out->pixelsSize > (uint64_t)fsize) {
        free(buf); return fail(err, errsz, "texel data out of range");
    }
    out->pixels = malloc(out->pixelsSize);
    if (!out->pixels) { free(buf); return fail(err, errsz, "out of memory"); }
    memcpy(out->pixels, buf + rawTabOff + poff, out->pixelsSize);

    /* link slots -> target names via placeholder descriptors */
    uint32_t animId = opk_rd32(out->body + OPK_TXMP_ANIM);
    uint32_t envId  = opk_rd32(out->body + OPK_TXMP_ENVMAP);
    if (animId & 1u) {
        if (desc_name(buf, fsize, nameTabOff, nameTabLen, animId >> 8, nInst,
                      out->animName) != 0) {
            opk_texture_free(out); free(buf);
            return fail(err, errsz, "anim link target has no name");
        }
    }
    if (envId & 1u) {
        if (desc_name(buf, fsize, nameTabOff, nameTabLen, envId >> 8, nInst,
                      out->envMapName) != 0) {
            opk_texture_free(out); free(buf);
            return fail(err, errsz, "envmap link target has no name");
        }
    }

    free(buf);
    return 0;
}

void opk_texture_free(OpkTexture *t) {
    free(t->pixels);
    t->pixels = NULL;
}
