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

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)

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

int main(void) {
    test_reader();
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
