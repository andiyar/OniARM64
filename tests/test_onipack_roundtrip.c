// ======================================================================
// test_onipack_roundtrip.c — synthetic V32 .oni fixture -> reader ->
// writer -> re-parse of the emitted VR31 pack bytes. Standalone:
//   cc -Wall -Wextra tests/test_onipack_roundtrip.c \
//      tools/onipack/onipack_oni.c tools/onipack/onipack_writer.c \
//      -o /tmp/t_opkrt && /tmp/t_opkrt
// Compile with -DGEN_MAIN to get a fixture-generator CLI instead of tests.
// ======================================================================
#include "../tools/onipack/onipack_format.h"
#include "../tools/onipack/onipack_oni.h"
#include "../tools/onipack/onipack_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a V32 .oni: one real TXMP (4x4, fmt 0, no mips, 32 texel bytes)
 * + optionally one TXMP placeholder "SKYENV" that the real instance's
 * envMap points at.
 * Layout: header(64) + N idesc(20 each) + 1 ndesc(8) + 0 tdesc, align32 ->
 * data (one 192-byte record) -> name table -> align32 -> raw table. */
static void write_fixture_oni(const char *path, uint32_t version,
                              uint32_t tag0, int nInstances) {
    uint8_t hdr[64] = {0};
    uint32_t tables   = 64u + (uint32_t)nInstances * 20u + 1u * 8u;
    uint32_t dataOff  = opk_align32(tables);
    uint32_t dataLen  = 192;                    /* one real record */
    uint32_t nameOff  = dataOff + dataLen;
    const char nameTab[] = "TXMPSKYENV";        /* tag-prefixed, NUL-terminated */
    uint32_t nameLen  = sizeof nameTab;         /* 11 incl NUL */
    uint32_t rawOff   = opk_align32(nameOff + nameLen);
    uint32_t rawLen   = OPK_BLOB_BASE + 32;     /* base pad + 4x4 fmt0 texels */

    opk_wr64(hdr + 0x00, OPK_CHECKSUM_MAC);
    opk_wr32(hdr + 0x08, version);
    opk_wr16(hdr + 0x0C, 64); opk_wr16(hdr + 0x0E, 20);
    opk_wr16(hdr + 0x10, 16); opk_wr16(hdr + 0x12, 8);
    opk_wr32(hdr + 0x14, (uint32_t)nInstances);
    opk_wr32(hdr + 0x18, 1);
    opk_wr32(hdr + 0x1C, 0);
    opk_wr32(hdr + 0x20, dataOff); opk_wr32(hdr + 0x24, dataLen);
    opk_wr32(hdr + 0x28, nameOff); opk_wr32(hdr + 0x2C, nameLen);
    opk_wr32(hdr + 0x30, rawOff);  opk_wr32(hdr + 0x34, rawLen);

    FILE *f = fopen(path, "wb");
    fwrite(hdr, 1, 64, f);

    uint8_t d[20] = {0};                        /* desc 0: real TXMP */
    opk_wr32(d, tag0); opk_wr32(d + 4, 8);
    opk_wr32(d + 8, 0); opk_wr32(d + 12, 192);
    opk_wr32(d + 16, OPK_FLAG_UNIQUE);          /* solo exports are unnamed */
    fwrite(d, 1, 20, f);
    if (nInstances > 1) {                       /* desc 1: envmap placeholder */
        memset(d, 0, sizeof d);
        opk_wr32(d, OPK_TAG_TXMP); opk_wr32(d + 4, 0);
        opk_wr32(d + 8, 0);                     /* nameOffset 0 -> "TXMPSKYENV" */
        opk_wr32(d + 16, OPK_FLAG_PLACEHOLDER);
        fwrite(d, 1, 20, f);
    }
    uint8_t nd[8] = {0};                        /* name desc -> instance 1 */
    opk_wr32(nd, 1);
    fwrite(nd, 1, 8, f);

    uint8_t pad[64] = {0};
    fwrite(pad, 1, dataOff - tables, f);

    uint8_t rec[192] = {0};                     /* preamble + body */
    opk_wr32(rec, (0u << 8) | 1u); opk_wr32(rec + 4, 0x12345678);
    uint8_t *body = rec + 8;
    memcpy(body + OPK_TXMP_NAME, "fixture", 8);
    opk_wr32(body + OPK_TXMP_FLAGS, OPK_TXMP_FLAG_LE);
    opk_wr16(body + OPK_TXMP_WIDTH, 4); opk_wr16(body + OPK_TXMP_HEIGHT, 4);
    opk_wr32(body + OPK_TXMP_FORMAT, 0);        /* BGRA4444 */
    if (nInstances > 1)
        opk_wr32(body + OPK_TXMP_ENVMAP, (1u << 8) | 1u); /* -> desc 1 */
    opk_wr32(body + OPK_TXMP_SEPOFF, OPK_BLOB_BASE);      /* rel rawTable */
    fwrite(rec, 1, 192, f);

    fwrite(nameTab, 1, nameLen, f);
    long pos = ftell(f);
    while (pos < (long)rawOff) { fputc(0, f); pos++; }
    for (int i = 0; i < 32; i++) fputc(0, f);   /* blob-base padding */
    for (int i = 0; i < 32; i++) fputc(0xA0 + (i & 0x0F), f); /* texels */
    fclose(f);
}

#ifndef GEN_MAIN

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)

/* Animated V32 .oni mirroring TXMPscan.oni's shape: base TXMP (named
 * in-file, anim -> TXAN, envMap -> external placeholder) + 2 Unique frame
 * TXMPs + 1 TXAN + 1 TXMP placeholder "SKYENV". TXAN follows the OniSplit
 * convention (TextureImporter3.cs): numFrames counts the base frame and
 * maps[0] is the literal 0 that means "base texture" (onipack_format.h). */
static void write_fixture_oni_anim(const char *path) {
    enum { NINST = 5 };
    uint8_t hdr[64] = {0};
    uint32_t tables  = 64u + NINST * 20u + 1u * 8u;
    uint32_t dataOff = opk_align32(tables);
    uint32_t dataLen = 192 * 3 + 64;            /* 3 TXMP records + TXAN */
    uint32_t nameOff = dataOff + dataLen;
    const char nameTab[] = "TXMPbase\0TXMPSKYENV"; /* two names incl NULs */
    uint32_t nameLen = sizeof nameTab;             /* 20 */
    uint32_t rawOff  = opk_align32(nameOff + nameLen);
    uint32_t rawLen  = OPK_BLOB_BASE + 3 * 32;

    opk_wr64(hdr + 0x00, OPK_CHECKSUM_MAC);
    opk_wr32(hdr + 0x08, OPK_VERSION_32);
    opk_wr16(hdr + 0x0C, 64); opk_wr16(hdr + 0x0E, 20);
    opk_wr16(hdr + 0x10, 16); opk_wr16(hdr + 0x12, 8);
    opk_wr32(hdr + 0x14, NINST);
    opk_wr32(hdr + 0x18, 1);
    opk_wr32(hdr + 0x1C, 0);
    opk_wr32(hdr + 0x20, dataOff); opk_wr32(hdr + 0x24, dataLen);
    opk_wr32(hdr + 0x28, nameOff); opk_wr32(hdr + 0x2C, nameLen);
    opk_wr32(hdr + 0x30, rawOff);  opk_wr32(hdr + 0x34, rawLen);

    FILE *f = fopen(path, "wb");
    fwrite(hdr, 1, 64, f);

    /* descriptors: base, frame1, frame2, TXAN, placeholder */
    static const struct { uint32_t tag, dOff, nOff, size, flags; } dd[NINST] = {
        { OPK_TAG_TXMP,   8, 0, 192, 0 },       /* base: named in-file */
        { OPK_TAG_TXMP, 200, 0, 192, OPK_FLAG_UNIQUE },
        { OPK_TAG_TXMP, 392, 0, 192, OPK_FLAG_UNIQUE },
        { OPK_TAG_TXAN, 584, 0,  64, OPK_FLAG_UNIQUE },
        { OPK_TAG_TXMP,   0, 9,   0, OPK_FLAG_PLACEHOLDER },
    };
    for (int i = 0; i < NINST; i++) {
        uint8_t d[20];
        opk_wr32(d, dd[i].tag); opk_wr32(d + 4, dd[i].dOff);
        opk_wr32(d + 8, dd[i].nOff); opk_wr32(d + 12, dd[i].size);
        opk_wr32(d + 16, dd[i].flags);
        fwrite(d, 1, 20, f);
    }
    uint8_t nd[8] = {0};                        /* name desc -> placeholder */
    opk_wr32(nd, 4);
    fwrite(nd, 1, 8, f);

    uint8_t pad[64] = {0};
    fwrite(pad, 1, dataOff - tables, f);

    for (int t = 0; t < 3; t++) {               /* base + 2 frame records */
        uint8_t rec[192] = {0};
        opk_wr32(rec, ((uint32_t)t << 8) | 1u); opk_wr32(rec + 4, 0x12345678);
        uint8_t *body = rec + 8;
        opk_wr32(body + OPK_TXMP_FLAGS, OPK_TXMP_FLAG_LE);
        opk_wr16(body + OPK_TXMP_WIDTH, 4); opk_wr16(body + OPK_TXMP_HEIGHT, 4);
        opk_wr32(body + OPK_TXMP_FORMAT, 0);
        if (t == 0) {
            memcpy(body + OPK_TXMP_NAME, "base", 5);
            opk_wr32(body + OPK_TXMP_ANIM, (3u << 8) | 1u);   /* -> TXAN */
            opk_wr32(body + OPK_TXMP_ENVMAP, (4u << 8) | 1u); /* -> placeholder */
        }
        opk_wr32(body + OPK_TXMP_SEPOFF, OPK_BLOB_BASE + (uint32_t)t * 32u);
        fwrite(rec, 1, 192, f);
    }
    uint8_t arec[64] = {0};                     /* TXAN: base + 2 frames */
    opk_wr32(arec, (3u << 8) | 1u); opk_wr32(arec + 4, 0x12345678);
    opk_wr16(arec + 8 + OPK_TXAN_TIMEPERFRAME, 5);
    opk_wr32(arec + 8 + OPK_TXAN_NUMFRAMES, 3);
    opk_wr32(arec + 8 + OPK_TXAN_MAPS + 0, 0);            /* base frame */
    opk_wr32(arec + 8 + OPK_TXAN_MAPS + 4, (1u << 8) | 1u);
    opk_wr32(arec + 8 + OPK_TXAN_MAPS + 8, (2u << 8) | 1u);
    fwrite(arec, 1, 64, f);

    fwrite(nameTab, 1, nameLen, f);
    long pos = ftell(f);
    while (pos < (long)rawOff) { fputc(0, f); pos++; }
    for (int i = 0; i < 32; i++) fputc(0, f);   /* blob-base padding */
    for (int t = 0; t < 3; t++)                 /* 0xA0.., 0xB0.., 0xC0.. */
        for (int i = 0; i < 32; i++) fputc(0xA0 + t * 0x10 + (i & 0x0F), f);
    fclose(f);
}

static void test_reader(void) {
    char err[256];
    OpkTexture t;

    write_fixture_oni("/tmp/TXMPfix%2Ftex.oni", OPK_VERSION_32, OPK_TAG_TXMP, 2);
    CHECK(opk_oni_read("/tmp/TXMPfix%2Ftex.oni", &t, err, sizeof err) == 0,
          "reader accepts fixture");
    CHECK(strcmp(t.name, "fix/tex") == 0, "name: tag stripped + %2F decoded");
    CHECK(opk_rd16(t.body + OPK_TXMP_WIDTH) == 4, "width");
    CHECK(opk_rd32(t.body + OPK_TXMP_FORMAT) == 0, "format");
    CHECK(t.pixelsSize == 32, "pixel size 4x4 BGRA4444");
    CHECK(t.pixels && t.pixels[0] == 0xA0 && t.pixels[31] == 0xAF,
          "pixel bytes copied");
    CHECK(strcmp(t.envMapName, "SKYENV") == 0, "envmap link name resolved");
    CHECK(t.animName[0] == '\0', "no anim link");
    opk_texture_free(&t);

    write_fixture_oni("/tmp/TXMPbad.oni", OPK_VERSION_33, OPK_TAG_TXMP, 1);
    CHECK(opk_oni_read("/tmp/TXMPbad.oni", &t, err, sizeof err) != 0,
          "V33 rejected");

    write_fixture_oni("/tmp/ONCCbad.oni", OPK_VERSION_32, 0x4F4E4343u, 1);
    CHECK(opk_oni_read("/tmp/ONCCbad.oni", &t, err, sizeof err) != 0,
          "non-TXMP rejected (#62 guard)");
    CHECK(strstr(err, "TXMP") != NULL, "rejection names the rule");
}

static void test_group_reader(void) {
    char err[256];
    OpkGroup g;
    OpkTexture t;

    write_fixture_oni_anim("/tmp/TXMPanimfix.oni");
    CHECK(opk_oni_read_group("/tmp/TXMPanimfix.oni", &g, err, sizeof err) == 0,
          "group reader accepts animated fixture");
    CHECK(g.nTex == 3, "three real TXMPs");
    CHECK(strcmp(g.name, "animfix") == 0, "group name from filename");
    CHECK(g.tex[0].localIdx == 0 && g.tex[1].localIdx == 1 &&
          g.tex[2].localIdx == 2, "descriptor-order local indices");
    CHECK(g.hasTxan == 1 && g.txanLocalIdx == 3, "TXAN found at desc 3");
    CHECK(g.txanBodySize == 0x18 + 12, "TXAN body size: base + 2 frames");
    CHECK(g.txanBody &&
          opk_rd32(g.txanBody + OPK_TXAN_NUMFRAMES) == 3, "numFrames 3");
    CHECK(g.txanBody &&
          opk_rd32(g.txanBody + OPK_TXAN_MAPS) == 0 &&
          opk_rd32(g.txanBody + OPK_TXAN_MAPS + 4) == ((1u << 8) | 1u) &&
          opk_rd32(g.txanBody + OPK_TXAN_MAPS + 8) == ((2u << 8) | 1u),
          "maps[]: 0 base-frame convention + file-local frame ids");
    CHECK(g.tex[0].pixelsSize == 32 && g.tex[0].pixels &&
          g.tex[0].pixels[0] == 0xA0, "base pixels");
    CHECK(g.tex[1].pixelsSize == 32 && g.tex[1].pixels &&
          g.tex[1].pixels[0] == 0xB0 && g.tex[1].pixels[31] == 0xBF,
          "frame 1 pixels");
    CHECK(g.tex[2].pixelsSize == 32 && g.tex[2].pixels &&
          g.tex[2].pixels[0] == 0xC0, "frame 2 pixels");
    CHECK(g.tex[0].anim.local == 3 && g.tex[0].anim.name[0] == '\0',
          "base anim ref is file-local TXAN");
    CHECK(g.tex[0].envMap.local == -1 &&
          strcmp(g.tex[0].envMap.name, "SKYENV") == 0,
          "base envmap resolves placeholder name");
    CHECK(g.tex[1].anim.local == -1 && g.tex[1].anim.name[0] == '\0' &&
          g.tex[1].envMap.local == -1 && g.tex[1].envMap.name[0] == '\0' &&
          g.tex[2].anim.local == -1 && g.tex[2].envMap.name[0] == '\0',
          "frames carry no links");
    opk_group_free(&g);

    CHECK(opk_oni_read("/tmp/TXMPanimfix.oni", &t, err, sizeof err) != 0,
          "single-instance wrapper rejects animated file");
    CHECK(strstr(err, "TXMP") != NULL, "wrapper rejection names the scope");

    /* group reader on the single-instance fixture: same fields as wrapper */
    write_fixture_oni("/tmp/TXMPfix%2Ftex.oni", OPK_VERSION_32, OPK_TAG_TXMP, 2);
    CHECK(opk_oni_read_group("/tmp/TXMPfix%2Ftex.oni", &g, err, sizeof err) == 0,
          "group reader accepts single-instance fixture");
    CHECK(g.nTex == 1 && !g.hasTxan, "single fixture: one TXMP, no TXAN");
    CHECK(strcmp(g.name, "fix/tex") == 0, "single fixture group name");
    CHECK(g.tex[0].pixelsSize == 32 && g.tex[0].pixels &&
          g.tex[0].pixels[0] == 0xA0, "single fixture pixels via group");
    CHECK(g.tex[0].anim.local == -1 && g.tex[0].anim.name[0] == '\0' &&
          g.tex[0].envMap.local == -1 &&
          strcmp(g.tex[0].envMap.name, "SKYENV") == 0,
          "single fixture links via group");
    opk_group_free(&g);
}

/* overwrite 4 bytes at a fixed offset — corrupt-fixture helper */
static void patch32(const char *path, long off, uint32_t v) {
    FILE *f = fopen(path, "r+b");
    uint8_t b[4];
    opk_wr32(b, v);
    fseek(f, off, SEEK_SET);
    fwrite(b, 1, 4, f);
    fclose(f);
}

/* fixed offsets inside the animated fixture (see write_fixture_oni_anim:
 * descs @64, data @192, records base/f1/f2 @192/384/576, TXAN rec @768,
 * name table @832) */
enum {
    FXA_DESC1_TAG   = 64 + 1 * 20,
    FXA_BASE_ANIM   = 192 + 8 + OPK_TXMP_ANIM,
    FXA_BASE_ENVMAP = 192 + 8 + OPK_TXMP_ENVMAP,
    FXA_F1_ANIM     = 192 + 192 + 8 + OPK_TXMP_ANIM,
    FXA_F2_ANIM     = 192 + 384 + 8 + OPK_TXMP_ANIM,
    FXA_TXAN_BODY   = 192 + 584,
    FXA_NUMFRAMES   = FXA_TXAN_BODY + OPK_TXAN_NUMFRAMES,
    FXA_MAPS1       = FXA_TXAN_BODY + OPK_TXAN_MAPS + 4,
    FXA_NAMETAB     = 192 + 640
};

static void test_group_negative(void) {
    char err[256];
    OpkGroup g;
    const char *p = "/tmp/TXMPanimneg.oni";

    write_fixture_oni_anim(p);
    patch32(p, FXA_DESC1_TAG, OPK_TAG_TXAN);    /* frame 1 becomes a TXAN */
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "multiple TXAN"), "second TXAN rejected");

    write_fixture_oni_anim(p);
    patch32(p, FXA_MAPS1, (3u << 8) | 1u);      /* frame ref -> the TXAN */
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "frame ref"), "frame ref to TXAN rejected");

    write_fixture_oni_anim(p);
    patch32(p, FXA_MAPS1, 0);                   /* 0 only legal at index 0 */
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "frame ref"), "zero frame ref past index 0 rejected");

    write_fixture_oni_anim(p);
    patch32(p, FXA_NUMFRAMES, 0);
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "frame count"), "numFrames 0 rejected");

    write_fixture_oni_anim(p);
    patch32(p, FXA_NUMFRAMES, 300);
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "frame count"), "numFrames 300 rejected");

    write_fixture_oni_anim(p);
    patch32(p, FXA_BASE_ENVMAP, (3u << 8) | 1u); /* envmap -> the TXAN */
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "envmap link target is not TXMP"),
          "envmap slot pointing at TXAN rejected");

    write_fixture_oni_anim(p);
    {   /* NUL the 'M' of "TXMPSKYENV" @ table byte 11: the placeholder's
         * name entry (nameOff 9) now has its only reachable NUL inside the
         * 4CC tag prefix — desc_name must refuse it */
        FILE *f = fopen(p, "r+b");
        fseek(f, FXA_NAMETAB + 11, SEEK_SET);
        fputc(0, f);
        fclose(f);
    }
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "envmap link target has no name"),
          "name entry with NUL inside tag prefix rejected");

    write_fixture_oni_anim(p);
    patch32(p, FXA_BASE_ANIM, 0);               /* nobody references the TXAN */
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "no TXMP references"), "TXAN without base rejected");

    write_fixture_oni_anim(p);
    patch32(p, FXA_F1_ANIM, (3u << 8) | 1u);    /* two anim carriers */
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) != 0 &&
          strstr(err, "multiple TXMPs reference"), "two anim carriers rejected");

    /* carrier NOT first in descriptor order: swap into tex[0] */
    write_fixture_oni_anim(p);
    patch32(p, FXA_BASE_ANIM, 0);
    patch32(p, FXA_F2_ANIM, (3u << 8) | 1u);    /* carrier = desc 2 */
    CHECK(opk_oni_read_group(p, &g, err, sizeof err) == 0,
          "carrier-not-first accepted");
    CHECK(g.nTex == 3 && g.tex[0].localIdx == 2 && g.tex[0].pixels &&
          g.tex[0].pixels[0] == 0xC0 && g.tex[0].anim.local == 3,
          "anim carrier swapped into base slot");
    CHECK(g.tex[1].localIdx == 1 && g.tex[2].localIdx == 0,
          "swap keeps localIdx truthful");
    opk_group_free(&g);
}

/* reader accept-path pin: single TXMP whose anim slot points at a TXAN
 * placeholder -> animName populated. Rewrites the SKYENV placeholder
 * fixture into a TXAN one (desc tag + name-table prefix) and moves the
 * link from the envMap slot to the anim slot. */
static void test_reader_anim_placeholder(void) {
    char err[256];
    OpkTexture t;
    const char *p = "/tmp/TXMPanimptr.oni";

    write_fixture_oni(p, OPK_VERSION_32, OPK_TAG_TXMP, 2);
    patch32(p, 64 + 1 * 20, OPK_TAG_TXAN);          /* desc1 tag -> TXAN */
    patch32(p, 320, 0x4E415854u);                   /* name prefix -> "TXAN" */
    patch32(p, 128 + 8 + OPK_TXMP_ANIM, (1u << 8) | 1u);
    patch32(p, 128 + 8 + OPK_TXMP_ENVMAP, 0);
    CHECK(opk_oni_read(p, &t, err, sizeof err) == 0,
          "TXAN placeholder anim link accepted");
    CHECK(strcmp(t.animName, "SKYENV") == 0, "animName resolved");
    CHECK(t.envMapName[0] == '\0', "no envmap on anim-placeholder fixture");
    opk_texture_free(&t);
}

static void test_writer(void) {
    char err[256];
    OpkTexture a, b;
    write_fixture_oni("/tmp/TXMPalpha.oni", OPK_VERSION_32, OPK_TAG_TXMP, 2);
    write_fixture_oni("/tmp/TXMPzeta.oni",  OPK_VERSION_32, OPK_TAG_TXMP, 1);
    CHECK(opk_oni_read("/tmp/TXMPalpha.oni", &a, err, sizeof err) == 0, "read a");
    CHECK(opk_oni_read("/tmp/TXMPzeta.oni",  &b, err, sizeof err) == 0, "read b");

    OpkPack *p = opk_pack_new(0, "TT1");
    /* add in REVERSE alpha order to prove the writer sorts name descriptors */
    CHECK(opk_pack_add(p, &b, err, sizeof err) == 0, "add zeta");
    CHECK(opk_pack_add(p, &a, err, sizeof err) == 0, "add alpha");
    CHECK(opk_pack_write(p, "/tmp/opk_out", err, sizeof err) == 0, "write pack");
    opk_pack_free(p);

    long sz = 0;
    FILE *f = fopen("/tmp/opk_out/level0_TT1.dat", "rb");
    CHECK(f != NULL, "dat exists");
    fseek(f, 0, SEEK_END); sz = ftell(f); rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    CHECK(fread(buf, 1, (size_t)sz, f) == (size_t)sz, "dat readable");
    fclose(f);

    CHECK(opk_rd64(buf + 0x00) == OPK_CHECKSUM_MAC, "Mac checksum");
    CHECK(opk_rd32(buf + 0x08) == OPK_VERSION_31, "VR31");
    CHECK(opk_rd16(buf + 0x0C) == 64 && opk_rd16(buf + 0x0E) == 20 &&
          opk_rd16(buf + 0x10) == 16 && opk_rd16(buf + 0x12) == 8, "sizes");
    uint32_t nInst = opk_rd32(buf + OPK_HDR_NINST);
    uint32_t nName = opk_rd32(buf + OPK_HDR_NNAME);
    CHECK(nInst == 3, "2 real + 1 envmap placeholder");
    CHECK(nName == 3, "all named");
    CHECK(opk_rd32(buf + OPK_HDR_NTEMPL) == 1, "one template (TXMP only)");
    uint32_t dataOff = opk_rd32(buf + OPK_HDR_DATAOFF);
    uint32_t dataLen = opk_rd32(buf + OPK_HDR_DATALEN);
    uint32_t nameOff = opk_rd32(buf + OPK_HDR_NAMEOFF);
    uint32_t nameLen = opk_rd32(buf + OPK_HDR_NAMELEN);
    CHECK(dataOff % 32 == 0, "data block 32-aligned");
    CHECK(nameOff == dataOff + dataLen, "name block adjacent");
    CHECK((long)(nameOff + nameLen) == sz, "file ends at name block end");
    CHECK(dataLen == 2 * 192, "two 192-byte records, placeholders dataless");

    /* preambles: {(idx<<8)|1, fileID} */
    for (uint32_t i = 0; i < nInst; i++) {
        const uint8_t *d = buf + 64 + i * OPK_IDESC_SIZE;
        uint32_t off = opk_rd32(d + 4);
        if (off == 0) continue;
        CHECK(opk_rd32(buf + dataOff + off - 8) == ((i << 8) | 1u),
              "preamble instance id");
        CHECK(opk_rd32(buf + dataOff + off - 4) == opk_file_id(0, "TT1"),
              "preamble fileID");
        /* body link + storage slots rewritten */
        const uint8_t *body = buf + dataOff + off;
        CHECK(opk_rd32(body + OPK_TXMP_RAWOFF) == 0, "raw slot zeroed");
        CHECK(opk_rd32(body + OPK_TXMP_SEPOFF) >= OPK_BLOB_BASE &&
              opk_rd32(body + OPK_TXMP_SEPOFF) % 32 == 0, "sep offset aligned");
        CHECK(opk_rd32(body + OPK_TXMP_FLAGS) & OPK_TXMP_FLAG_LE, "LE flag");
    }

    /* name descriptors sorted ordinal by tag-prefixed name */
    uint32_t ndescBase = 64 + nInst * OPK_IDESC_SIZE;
    char prev[OPK_NAME_MAX] = "";
    for (uint32_t i = 0; i < nName; i++) {
        uint32_t idx = opk_rd32(buf + ndescBase + i * OPK_NDESC_SIZE);
        const uint8_t *d = buf + 64 + idx * OPK_IDESC_SIZE;
        const char *nm = (const char *)buf + nameOff + opk_rd32(d + 8);
        CHECK(strcmp(prev, nm) < 0, "name descs strictly sorted");
        snprintf(prev, sizeof prev, "%s", nm);
    }

    /* template descriptor: TXMP with reference-pack checksum, count 3 */
    uint32_t tdescBase = ndescBase + nName * OPK_NDESC_SIZE;
    CHECK(opk_rd64(buf + tdescBase) == OPK_TDESC_CHECKSUM_TXMP, "TXMP checksum");
    CHECK(opk_rd32(buf + tdescBase + 8) == OPK_TAG_TXMP, "TXMP tag");
    CHECK(opk_rd32(buf + tdescBase + 12) == 3, "numUsed = per-tag desc count");

    free(buf);

    /* companions */
    f = fopen("/tmp/opk_out/level0_TT1.raw", "rb");
    CHECK(f != NULL, "raw exists");
    fseek(f, 0, SEEK_END); CHECK(ftell(f) == 32, "raw is 32-byte stub");
    fclose(f);
    f = fopen("/tmp/opk_out/level0_TT1.sep", "rb");
    CHECK(f != NULL, "sep exists");
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) == (long)opk_align32(OPK_BLOB_BASE + 32) +
          32 /* second texture blob, 32B, fits alignment */, "sep size");
    fclose(f);

    /* duplicate-name rejection */
    OpkTexture c;
    write_fixture_oni("/tmp/TXMPalpha2.oni", OPK_VERSION_32, OPK_TAG_TXMP, 1);
    CHECK(opk_oni_read("/tmp/TXMPalpha2.oni", &c, err, sizeof err) == 0, "read c");
    p = opk_pack_new(0, "TT1");
    snprintf(c.name, sizeof c.name, "dupe");
    OpkTexture c2 = c; c2.pixels = malloc(c.pixelsSize);
    memcpy(c2.pixels, c.pixels, c.pixelsSize);
    CHECK(opk_pack_add(p, &c, err, sizeof err) == 0, "add first dupe");
    CHECK(opk_pack_add(p, &c2, err, sizeof err) != 0, "second dupe rejected");
    opk_texture_free(&c2);
    opk_pack_free(p);

    /* suffix contract */
    p = opk_pack_new(0, "Final");
    CHECK(p == NULL, "suffix 'Final' refused (overlay contract)");
}

static void test_writer_group(void) {
    char err[256];
    OpkGroup g;
    OpkTexture pre;

    /* pre-add one single texture so the group's pack indices differ from
     * its file-local indices — proves maps[]/link remapping is real */
    write_fixture_oni("/tmp/TXMPaaa.oni", OPK_VERSION_32, OPK_TAG_TXMP, 1);
    write_fixture_oni_anim("/tmp/TXMPanimfix.oni");
    CHECK(opk_oni_read("/tmp/TXMPaaa.oni", &pre, err, sizeof err) == 0,
          "read pre-texture");
    CHECK(opk_oni_read_group("/tmp/TXMPanimfix.oni", &g, err, sizeof err) == 0,
          "read group for pack");

    OpkPack *p = opk_pack_new(3, "GRP");
    CHECK(opk_pack_add(p, &pre, err, sizeof err) == 0, "add pre-texture");
    CHECK(opk_pack_add_group(p, &g, err, sizeof err) == 0, "add group");
    CHECK(g.tex[0].pixels == NULL && g.tex[1].pixels == NULL &&
          g.tex[2].pixels == NULL && g.txanBody == NULL,
          "group buffers owned by pack");
    CHECK(opk_pack_count(p) == 6, "pre + base + 2 frames + TXAN + placeholder");
    CHECK(opk_pack_write(p, "/tmp/opk_outg", err, sizeof err) == 0,
          "write group pack");
    opk_pack_free(p);
    opk_group_free(&g);

    FILE *f = fopen("/tmp/opk_outg/level3_GRP.dat", "rb");
    CHECK(f != NULL, "group dat exists");
    long sz = 0;
    fseek(f, 0, SEEK_END); sz = ftell(f); rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    CHECK(fread(buf, 1, (size_t)sz, f) == (size_t)sz, "group dat readable");
    fclose(f);

    uint32_t nInst = opk_rd32(buf + OPK_HDR_NINST);
    uint32_t nName = opk_rd32(buf + OPK_HDR_NNAME);
    uint32_t dataOff = opk_rd32(buf + OPK_HDR_DATAOFF);
    CHECK(nInst == 6 && nName == 3, "nInst counts all, nName named only");
    CHECK(opk_rd32(buf + OPK_HDR_NTEMPL) == 2, "TXAN + TXMP templates");
    CHECK(opk_rd32(buf + OPK_HDR_DATALEN) == 4 * 192 + 64,
          "four TXMP records + one 64-byte TXAN record");

    /* pack order: 0=aaa 1=base 2=frame1 3=frame2 4=TXAN 5=SKYENV placeholder */
    const uint8_t *d4 = buf + 64 + 4 * OPK_IDESC_SIZE;
    CHECK(opk_rd32(d4) == OPK_TAG_TXAN && opk_rd32(d4 + 12) == 64 &&
          (opk_rd32(d4 + 16) & 0xFFu) == OPK_FLAG_UNIQUE &&
          opk_rd32(d4 + 8) == 0,
          "TXAN desc: tag, 64B record, Unique, no name offset");
    uint32_t txanOff = opk_rd32(d4 + 4);
    const uint8_t *tb = buf + dataOff + txanOff;
    CHECK(opk_rd32(buf + dataOff + txanOff - 8) == ((4u << 8) | 1u) &&
          opk_rd32(buf + dataOff + txanOff - 4) == opk_file_id(3, "GRP"),
          "TXAN preamble id + fileID");
    CHECK(opk_rd32(tb + OPK_TXAN_NUMFRAMES) == 3, "TXAN numFrames intact");
    CHECK(opk_rd32(tb + OPK_TXAN_MAPS) == 0, "maps[0] zero passes verbatim");
    CHECK(opk_rd32(tb + OPK_TXAN_MAPS + 4) == ((2u << 8) | 1u) &&
          opk_rd32(tb + OPK_TXAN_MAPS + 8) == ((3u << 8) | 1u),
          "maps[] remapped to frame pack indices");

    /* base (idx 1): anim -> TXAN pack id, envMap -> placeholder pack id */
    const uint8_t *d1 = buf + 64 + 1 * OPK_IDESC_SIZE;
    const uint8_t *b1 = buf + dataOff + opk_rd32(d1 + 4);
    CHECK(opk_rd32(b1 + OPK_TXMP_ANIM) == ((4u << 8) | 1u), "base anim slot");
    CHECK(opk_rd32(b1 + OPK_TXMP_ENVMAP) == ((5u << 8) | 1u), "base envmap slot");

    /* frames (idx 2,3): Unique, link-free, aligned sep offsets */
    for (uint32_t i = 2; i <= 3; i++) {
        const uint8_t *d = buf + 64 + i * OPK_IDESC_SIZE;
        const uint8_t *b = buf + dataOff + opk_rd32(d + 4);
        CHECK((opk_rd32(d + 16) & 0xFFu) == OPK_FLAG_UNIQUE &&
              opk_rd32(d + 8) == 0, "frame desc Unique + no name offset");
        CHECK(opk_rd32(b + OPK_TXMP_ANIM) == 0 &&
              opk_rd32(b + OPK_TXMP_ENVMAP) == 0, "frame carries no links");
        CHECK(opk_rd32(b + OPK_TXMP_SEPOFF) >= OPK_BLOB_BASE &&
              opk_rd32(b + OPK_TXMP_SEPOFF) % 32 == 0,
              "frame sep offset aligned");
    }

    /* name descriptors reference only named entries (0, 1, 5) */
    uint32_t ndescBase = 64 + nInst * OPK_IDESC_SIZE;
    for (uint32_t i = 0; i < nName; i++) {
        uint32_t idx = opk_rd32(buf + ndescBase + i * OPK_NDESC_SIZE);
        CHECK(idx == 0 || idx == 1 || idx == 5,
              "name descs skip unnamed entries");
    }

    /* template table: TXAN then TXMP (ascending tag), observed checksums */
    uint32_t tdescBase = ndescBase + nName * OPK_NDESC_SIZE;
    CHECK(opk_rd64(buf + tdescBase) == OPK_TDESC_CHECKSUM_TXAN &&
          opk_rd32(buf + tdescBase + 8) == OPK_TAG_TXAN &&
          opk_rd32(buf + tdescBase + 12) == 1, "TXAN template desc, count 1");
    CHECK(opk_rd64(buf + tdescBase + 16) == OPK_TDESC_CHECKSUM_TXMP &&
          opk_rd32(buf + tdescBase + 16 + 8) == OPK_TAG_TXMP &&
          opk_rd32(buf + tdescBase + 16 + 12) == 5, "TXMP template desc, count 5");

    /* frame pixels land in .sep at their slots, distinct patterns */
    {
        const uint8_t *d2 = buf + 64 + 2 * OPK_IDESC_SIZE;
        const uint8_t *d3 = buf + 64 + 3 * OPK_IDESC_SIZE;
        uint32_t s2 = opk_rd32(buf + dataOff + opk_rd32(d2 + 4) + OPK_TXMP_SEPOFF);
        uint32_t s3 = opk_rd32(buf + dataOff + opk_rd32(d3 + 4) + OPK_TXMP_SEPOFF);
        FILE *fs = fopen("/tmp/opk_outg/level3_GRP.sep", "rb");
        CHECK(fs != NULL, "group sep exists");
        long ssz = 0;
        fseek(fs, 0, SEEK_END); ssz = ftell(fs); rewind(fs);
        uint8_t *sep = malloc((size_t)ssz);
        CHECK(fread(sep, 1, (size_t)ssz, fs) == (size_t)ssz,
              "group sep readable");
        fclose(fs);
        CHECK((long)(s2 + 32) <= ssz && sep[s2] == 0xB0 && sep[s2 + 31] == 0xBF,
              "frame 1 sep bytes");
        CHECK((long)(s3 + 32) <= ssz && sep[s3] == 0xC0, "frame 2 sep bytes");
        free(sep);
    }
    free(buf);

    /* atomicity: same-named base rejected, pack unchanged, later add works */
    OpkTexture poison;
    OpkGroup g2;
    write_fixture_oni("/tmp/TXMPpoison.oni", OPK_VERSION_32, OPK_TAG_TXMP, 1);
    CHECK(opk_oni_read("/tmp/TXMPpoison.oni", &poison, err, sizeof err) == 0,
          "read poison");
    snprintf(poison.name, sizeof poison.name, "animfix");
    OpkPack *p2 = opk_pack_new(3, "AT");
    CHECK(opk_pack_add(p2, &poison, err, sizeof err) == 0, "poison added");
    CHECK(opk_oni_read_group("/tmp/TXMPanimfix.oni", &g2, err, sizeof err) == 0,
          "re-read group");
    CHECK(opk_pack_add_group(p2, &g2, err, sizeof err) != 0 &&
          strstr(err, "duplicate"), "duplicate base name rejected");
    CHECK(opk_pack_count(p2) == 1, "pack unchanged after failed group add");
    CHECK(g2.tex[0].pixels != NULL && g2.txanBody != NULL,
          "group keeps ownership on failure");
    snprintf(g2.name, sizeof g2.name, "other");
    CHECK(opk_pack_add_group(p2, &g2, err, sizeof err) == 0,
          "subsequent good group add works");
    CHECK(opk_pack_count(p2) == 6, "poison + group + placeholder");
    opk_group_free(&g2);
    opk_pack_free(p2);
    opk_texture_free(&poison);
}

int main(void) {
    test_reader();
    test_group_reader();
    test_group_negative();
    test_reader_anim_placeholder();
    test_writer();
    test_writer_group();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

#else /* GEN_MAIN: fixture generator for the CLI test (later task) */

int main(int argc, char **argv) {
    char p[1024];
    if (argc < 2) return 2;
    snprintf(p, sizeof p, "%s/TXMPcliA.oni", argv[1]);
    write_fixture_oni(p, OPK_VERSION_32, OPK_TAG_TXMP, 2);
    snprintf(p, sizeof p, "%s/TXMPcliB.oni", argv[1]);
    write_fixture_oni(p, OPK_VERSION_32, OPK_TAG_TXMP, 1);
    snprintf(p, sizeof p, "%s/ONCCbad.oni", argv[1]);
    write_fixture_oni(p, OPK_VERSION_32, 0x4F4E4343u, 1);
    return 0;
}

#endif
