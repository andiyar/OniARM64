// ======================================================================
// test_onipack_roundtrip.c — synthetic V32 .oni fixture -> reader ->
// (Task 5: writer -> re-parse). Standalone:
//   cc -Wall -Wextra tests/test_onipack_roundtrip.c \
//      tools/onipack/onipack_oni.c tools/onipack/onipack_writer.c \
//      -o /tmp/t_opkrt && /tmp/t_opkrt
// (until Task 5 exists, omit onipack_writer.c from the compile line)
// Compile with -DGEN_MAIN to get a fixture-generator CLI instead of tests.
// ======================================================================
#include "../tools/onipack/onipack_format.h"
#include "../tools/onipack/onipack_oni.h"
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

int main(void) {
    test_reader();
    test_group_reader();
    test_group_negative();
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
