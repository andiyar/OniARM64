/* ======================================================================
 * onipack_format.h — Oni instance-file (VR31/VR32) wire format for the
 * onipack pack writer. Facts pinned 2026-07-11 against BFW_TM_Private.h,
 * BFW_TM_Game.c, BFW_TM_Common.c, BFW_Motoko.h, BFW_Image.h, OniSplit
 * (InstanceFileWriter.cs) and on-disk bytes (CuratedHD + retail level0).
 * See docs/superpowers/plans/2026-07-11-onipack-stage0-1-native-pack-writer.md
 * (parent repo) for the full fact table. addresses #88
 * ==================================================================== */
#ifndef ONIPACK_FORMAT_H
#define ONIPACK_FORMAT_H

#include <ctype.h>
#include <stdint.h>

#define OPK_VERSION_31   0x56523331u  /* 'VR31' — .dat packs      */
#define OPK_VERSION_32   0x56523332u  /* 'VR32' — OniSplit .oni   */
#define OPK_VERSION_33   0x56523333u  /* 'VR33' — rejected (unknown layout) */
#define OPK_CHECKSUM_PC  0x0003bcdf33dc271fULL
#define OPK_CHECKSUM_MAC 0x0003bcdf23c13061ULL /* our packs: sep-based Mac family */

#define OPK_TAG_TXMP 0x54584d50u
#define OPK_TAG_TXAN 0x5458414eu

/* template-descriptor checksums, observed in CuratedHD bytes (xxd @0x9384) */
#define OPK_TDESC_CHECKSUM_TXMP 0x00000008911eeb5fULL
#define OPK_TDESC_CHECKSUM_TXAN 0x0000000a8b134387ULL

/* header field offsets (64-byte header) */
enum {
    OPK_HDR_CHECKSUM = 0x00, OPK_HDR_VERSION = 0x08, OPK_HDR_SIZES = 0x0C,
    OPK_HDR_NINST = 0x14, OPK_HDR_NNAME = 0x18, OPK_HDR_NTEMPL = 0x1C,
    OPK_HDR_DATAOFF = 0x20, OPK_HDR_DATALEN = 0x24,
    OPK_HDR_NAMEOFF = 0x28, OPK_HDR_NAMELEN = 0x2C,
    OPK_HDR_RAWOFF_V32 = 0x30, OPK_HDR_RAWLEN_V32 = 0x34,
    OPK_HDR_SIZE = 0x40
};
enum { OPK_IDESC_SIZE = 20, OPK_NDESC_SIZE = 8, OPK_TDESC_SIZE = 16 };

#define OPK_FLAG_UNIQUE      0x01u /* unnamed instance */
#define OPK_FLAG_PLACEHOLDER 0x02u /* dataOffset==0; engine resolves by name */

#define OPK_PREAMBLE  8u  /* {(descIndex<<8)|1, fileID} precedes every record */
#define OPK_BLOB_BASE 32u /* first .raw/.sep allocation; offset 0 == NULL */

/* TXMP body (168 bytes) field offsets */
enum {
    OPK_TXMP_NAME = 0x00, OPK_TXMP_FLAGS = 0x80,
    OPK_TXMP_WIDTH = 0x84, OPK_TXMP_HEIGHT = 0x86, OPK_TXMP_FORMAT = 0x88,
    OPK_TXMP_ANIM = 0x8C, OPK_TXMP_ENVMAP = 0x90,
    OPK_TXMP_RAWOFF = 0x94, OPK_TXMP_SEPOFF = 0x98,
    OPK_TXMP_BODY = 168
};
#define OPK_TXMP_FLAG_HASMIPMAP (1u << 0)
#define OPK_TXMP_FLAG_LE        (1u << 12)

/* TXAN body field offsets (M3tTextureMapAnimation, BFW_Motoko.h:830-844):
 * pad[12], u16 timePerFrame @0x0C, u16 randTPF_low @0x0E, u16 randTPF_range
 * @0x10, pad[2], u32 numFrames @0x14, u32 maps[numFrames] @0x18 (TXMP
 * placeholder ids (descIdx<<8)|1, file-local). bodySize = OPK_TXAN_MAPS +
 * 4*numFrames; record = align32(8 + bodySize).
 * NB maps[0] is ZERO by convention: frame 0 means "the base texture itself",
 * stored as a NULL link "to prevent recursive touching issue" (engine,
 * Motoko_State_Draw.c ~102; OniSplit TextureImporter3.cs writes the literal
 * 0; all 13 CuratedHD TXANs carry it). */
enum {
    OPK_TXAN_TIMEPERFRAME = 0x0C, OPK_TXAN_NUMFRAMES = 0x14,
    OPK_TXAN_MAPS = 0x18
};

static inline uint32_t opk_align32(uint32_t v) { return (v + 31u) & ~31u; }

static inline void opk_wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static inline void opk_wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void opk_wr64(uint8_t *p, uint64_t v) {
    opk_wr32(p, (uint32_t)v); opk_wr32(p + 4, (uint32_t)(v >> 32));
}
static inline uint16_t opk_rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t opk_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t opk_rd64(const uint8_t *p) {
    return (uint64_t)opk_rd32(p) | ((uint64_t)opk_rd32(p + 4) << 32);
}

/* fileID from output name parts — engine: TMrUtility_LevelInfo_Get
 * (BFW_TM_Common.c:623-704); OniSplit: MakeFileId. "Final" => hash 0.
 * hash/factor are uint32_t as in the engine (UUtUns32); negative digit
 * terms wrap mod 2^32, giving results bit-identical to int arithmetic. */
static inline uint32_t opk_file_id(int level, const char *suffix) {
    uint32_t hash = 0, factor = 1;
    const char *c;
    if (suffix[0]=='F' && suffix[1]=='i' && suffix[2]=='n' &&
        suffix[3]=='a' && suffix[4]=='l' && suffix[5]=='\0') {
        hash = 0;
    } else {
        for (c = suffix; *c; c++, factor++)
            hash += (toupper((unsigned char)*c) - 'A' + 1) * factor;
    }
    return ((uint32_t)level << 25) | (((uint32_t)hash & 0xFFFFFFu) << 1) | 1u;
}

/* bytes for one mip level, ENGINE ComputeSize semantics; 0 = unrecognized
 * format or out-of-range dims (caller must reject). Dims capped at the
 * engine max 4096, which also prevents uint32 w*h overflow on hostile or
 * corrupt input. NB: the engine additionally pads odd widths for 1-byte
 * formats (fmt 3/5/6: ((w+1)&~1)*h) and has no case for fmt 10 — our
 * corpus contains neither; if 1-byte formats ever matter, mirror
 * BFW_Image.c:965. */
static inline uint32_t opk_texel_level_bytes(uint32_t fmt, uint32_t w, uint32_t h) {
    if (w < 1 || h < 1 || w > 4096 || h > 4096) return 0;
    switch (fmt) {
        case 7: case 8: case 11: return w * h * 4;  /* ARGB8888 / RGB888(BGRX) / RGBA_Bytes */
        case 10:                 return w * h * 3;  /* RGB_Bytes */
        case 0: case 1: case 2: case 12: case 13: case 14: case 15:
                                 return w * h * 2;  /* 16-bit formats */
        case 3: case 5: case 6:  return w * h;      /* I8 / A8 / A4I4 */
        case 4:                  return w * h / 8;  /* I1 */
        case 9: {                                    /* DXT1: 8 B per 4x4 block */
            uint32_t bw = w / 4 ? w / 4 : 1, bh = h / 4 ? h / 4 : 1;
            return bw * bh * 8;
        }
        default: return 0;
    }
}

/* total texel bytes incl. optional mip chain (largest level first) */
static inline uint32_t opk_texel_bytes(uint32_t fmt, uint32_t w, uint32_t h,
                                       int hasMips) {
    uint32_t total;
    if (w < 1 || h < 1 || w > 4096 || h > 4096) return 0;
    total = opk_texel_level_bytes(fmt, w, h);
    if (total == 0) return 0;
    while (hasMips && (w > 1 || h > 1)) {
        if (w > 1) w >>= 1;
        if (h > 1) h >>= 1;
        total += opk_texel_level_bytes(fmt, w, h);
    }
    return total;
}

/* Blob size as OniSplit WROTE it (Surface.cs w*h/2 floor for DXT1 — a 2x2
 * DXT1 mip level is 2 bytes and a 1x1 level is 0, unlike the engine's
 * ceil-to-block ComputeSize). Use THIS when slicing OniSplit-written files;
 * use opk_texel_bytes (engine semantics) when computing engine expectations. */
static inline uint32_t opk_texel_level_bytes_os(uint32_t fmt, uint32_t w, uint32_t h) {
    if (fmt == 9) {
        if (w < 1 || h < 1 || w > 4096 || h > 4096) return 0;
        return w * h / 2;
    }
    return opk_texel_level_bytes(fmt, w, h);
}
static inline uint32_t opk_texel_bytes_os(uint32_t fmt, uint32_t w, uint32_t h, int hasMips) {
    uint32_t total = opk_texel_level_bytes_os(fmt, w, h);
    if (total == 0) return 0;
    while (hasMips && (w > 1 || h > 1)) {
        if (w > 1) w >>= 1;
        if (h > 1) h >>= 1;
        total += opk_texel_level_bytes_os(fmt, w, h);  /* tail levels may add 0 */
    }
    return total;
}

#endif /* ONIPACK_FORMAT_H */
