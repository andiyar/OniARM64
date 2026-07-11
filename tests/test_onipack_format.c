// ======================================================================
// test_onipack_format.c — unit tests for tools/onipack/onipack_format.h,
// anchored to on-disk ground truth (OniSplit-built CuratedHD pack +
// retail level0). Standalone, no framework (mirrors test_oni_gamedata.c):
//   cc -Wall -Wextra tests/test_onipack_format.c -o /tmp/t_opkfmt && /tmp/t_opkfmt
// ======================================================================
#include "../tools/onipack/onipack_format.h"
#include <stdio.h>
#include <stdlib.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)

static const char *kProvenPack =
    "/Users/andiyar/Developer/oni/OniARM64/dist/CuratedHD/level0_HD1.dat";
static const char *kRetailDat =
    "/Users/andiyar/Developer/oni/CXOni/Oni/drive_c/Program Files (x86)/Oni/"
    "GameDataFolder/level0_Final.dat";

static unsigned char *slurp(const char *path, long *sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *sz = ftell(f); rewind(f);
    unsigned char *b = malloc((size_t)*sz);
    if (fread(b, 1, (size_t)*sz, f) != (size_t)*sz) { free(b); b = NULL; }
    fclose(f);
    return b;
}

int main(void) {
    CHECK(opk_align32(0) == 0, "align32(0)");
    CHECK(opk_align32(1) == 32, "align32(1)");
    CHECK(opk_align32(32) == 32, "align32(32)");
    CHECK(opk_align32(0x93A4) == 0x93C0, "align32 matches CuratedHD table end");

    // fileID: suffix "Final" special-case -> hash 0
    CHECK(opk_file_id(0, "Final") == 1, "fileID level0_Final == 1");
    // pinned from today's disk-verified run — portable when dist/ is absent
    CHECK(opk_file_id(0, "HD1") == 0x01FFFFC7u, "fileID HD1 known answer");

    // texel sizes
    CHECK(opk_texel_bytes(0, 256, 256, 0) == 256*256*2, "BGRA4444 256^2 no mips");
    CHECK(opk_texel_bytes(7, 4, 4, 0) == 64, "ARGB8888 4x4");
    CHECK(opk_texel_bytes(9, 128, 128, 0) == 128*128/2, "DXT1 128^2");
    CHECK(opk_texel_bytes(9, 2, 2, 0) == 8, "DXT1 min one block");
    // 4x4 fmt0 with mips: 32 + 8 (2x2) + 2 (1x1) = 42
    CHECK(opk_texel_bytes(0, 4, 4, 1) == 42, "BGRA4444 4x4 mip chain");
    CHECK(opk_texel_bytes(99, 4, 4, 0) == 0, "unknown format -> 0");
    // dimension guard (engine cap 4096; also blocks w*h uint32 overflow)
    CHECK(opk_texel_bytes(0, 8192, 8192, 0) == 0, "over-cap dims -> 0");
    CHECK(opk_texel_bytes(7, 0, 4, 0) == 0, "zero width -> 0");
    // OniSplit write-side sizing: DXT1 floors (w*h/2), tail levels may be 0
    // 8x8=32, 4x4=8, 2x2=2, 1x1=0 -> 42
    CHECK(opk_texel_bytes_os(9, 8, 8, 1) == 42, "DXT1 mips OniSplit floor sizing");
    CHECK(opk_texel_bytes_os(0, 4, 4, 1) == 42, "non-DXT1 _os identical to engine");
    // engine ceil-to-block contrast: 32 + 8 + 8 + 8 = 56
    CHECK(opk_texel_bytes(9, 8, 8, 1) == 56, "DXT1 mips engine ceil sizing");

    // LE helpers round-trip
    uint8_t buf[8];
    opk_wr32(buf, 0x54584d50u);
    CHECK(opk_rd32(buf) == 0x54584d50u && buf[0] == 'P', "wr32/rd32 LE");
    opk_wr64(buf, OPK_CHECKSUM_MAC);
    CHECK(opk_rd64(buf) == OPK_CHECKSUM_MAC, "wr64/rd64");

    // Ground truth: proven pack header + instance-0 preamble
    long sz = 0;
    unsigned char *dat = slurp(kProvenPack, &sz);
    if (!dat) { printf("SKIP: proven pack not on disk\n"); }
    else {
        CHECK(opk_rd32(dat + 0x08) == OPK_VERSION_31, "CuratedHD version VR31");
        CHECK(opk_rd64(dat + 0x00) == OPK_CHECKSUM_MAC, "CuratedHD Mac checksum");
        uint32_t dataOff  = opk_rd32(dat + 0x20);
        uint32_t desc0off = opk_rd32(dat + 0x44);   // desc0 dataOffset (== 8)
        CHECK(desc0off == 8, "desc0 dataOffset == 8");
        // preamble = { (0<<8)|1, fileID("HD1") }
        CHECK(opk_rd32(dat + dataOff)     == 1, "desc0 preamble id");
        CHECK(opk_rd32(dat + dataOff + 4) == opk_file_id(0, "HD1"),
              "fileID formula matches OniSplit-written preamble");
        // template-descriptor constants vs reference bytes (breaks the
        // circularity of the writer test asserting our own constant):
        // table at 64 + nInst*20 + nName*8; scan entries for the TXMP tag.
        uint32_t nInst = opk_rd32(dat + OPK_HDR_NINST);
        uint32_t nName = opk_rd32(dat + OPK_HDR_NNAME);
        uint32_t nTmpl = opk_rd32(dat + OPK_HDR_NTEMPL);
        uint32_t tdBase = 64 + nInst * OPK_IDESC_SIZE + nName * OPK_NDESC_SIZE;
        int foundTxmp = 0;
        for (uint32_t i = 0; i < nTmpl; i++) {
            const uint8_t *td = dat + tdBase + i * OPK_TDESC_SIZE;
            if (opk_rd32(td + 8) == OPK_TAG_TXMP) {
                foundTxmp = 1;
                CHECK(opk_rd64(td) == OPK_TDESC_CHECKSUM_TXMP,
                      "OPK_TDESC_CHECKSUM_TXMP matches reference pack bytes");
            }
        }
        CHECK(foundTxmp, "reference pack has a TXMP template descriptor");
        free(dat);
    }
    unsigned char *ret = slurp(kRetailDat, &sz);
    if (!ret) { printf("SKIP: retail dat not on disk\n"); }
    else {
        uint32_t dataOff = opk_rd32(ret + 0x20);
        CHECK(opk_rd32(ret + dataOff + 4) == opk_file_id(0, "Final"),
              "retail Final fileID == 1");
        free(ret);
    }

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
