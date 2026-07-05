/* ======================================================================
 * txmp-format-index.c — list TXMP instance names + texel formats from
 * Oni instance files (.dat level files or single-instance .oni files).
 *
 * Issue #63: the curated HD pack must not replace an alpha-carrying
 * retail texture (BGRA4444 alpha = env-map shininess mask) with an
 * alpha-less mod texture (BGR) — that turns the additive env-map pass
 * into full-strength white blowout on faces/hair/glass. The build
 * script uses this tool to index retail texel formats per TXMP name
 * and the formats of the staged mod .oni files, then drops offenders.
 *
 * Wire layout (authoritative: community-svn OniSplit —
 * InstanceFileHeader.cs, InstanceDescriptor.cs, Motoko/TextureDatReader.cs):
 *   header (little-endian):
 *     0x00 i64 templateChecksum   0x08 i32 version ('13RV'/'23RV')
 *     0x0C i64 signature          0x14 i32 instanceCount
 *     0x18 i32 nameCount          0x1C i32 templateCount
 *     0x20 i32 dataTableOffset    0x24 i32 dataTableSize
 *     0x28 i32 nameTableOffset    0x2C i32 nameTableSize
 *     0x30 .. 0x40 version-dependent (raw table / padding)
 *   descriptors at 0x40, 20 bytes each:
 *     i32 tag, i32 dataOffset (rel. dataTableOffset), i32 nameOffset
 *     (rel. nameTableOffset), i32 dataSize, i32 flags
 *     flags: 1=Private(no name) 2=Placeholder
 *   TXMP instance data: +0x80 i32 flags, +0x84 i16 w, +0x86 i16 h,
 *     +0x88 i32 texel format  <- what we want
 *
 * Output, one line per non-placeholder TXMP:
 *   <instanceName-or-'-'>\t<formatName>\t<filePath>
 * instanceName has the 'TXMP' tag prefix stripped, matching OniSplit.
 * Single-instance .oni files (version '23RV') carry no name table —
 * the caller derives the name from the file name; we print '-'.
 *
 * Build: cc -O2 -o txmp-format-index txmp-format-index.c
 * Usage: txmp-format-index <file.dat|file.oni> [...]
 * ==================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Engine texel types (IMcPixelType order); OniSplit XML spellings for
 * the ones OniSplit names (Motoko/TextureFormat.cs). */
static const char *format_name(uint32_t f) {
    switch (f) {
        case 0: return "BGRA4444";
        case 1: return "BGR555";
        case 2: return "BGRA5551";
        case 3: return "I8";
        case 4: return "I1";
        case 5: return "A8";
        case 6: return "A4I4";
        case 7: return "RGBA";      /* 32-bit with alpha */
        case 8: return "BGR";       /* 32-bit BGRX, no alpha */
        case 9: return "DXT1";
        default: return NULL;
    }
}

static int process(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "%s: cannot open\n", path); return 1; }

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 1; }
    long fsize = ftell(fp);
    rewind(fp);
    if (fsize < 0x40) {
        fprintf(stderr, "%s: too small for an instance file\n", path);
        fclose(fp); return 1;
    }

    unsigned char *buf = malloc((size_t)fsize);
    if (!buf || fread(buf, 1, (size_t)fsize, fp) != (size_t)fsize) {
        fprintf(stderr, "%s: read failed\n", path);
        free(buf); fclose(fp); return 1;
    }
    fclose(fp);

    uint32_t version = rd32(buf + 0x08);
    if (version != 0x56523331u && version != 0x56523332u) {  /* '13RV'/'23RV' */
        fprintf(stderr, "%s: unknown instance-file version 0x%08x\n", path, version);
        free(buf); return 1;
    }

    uint32_t instanceCount   = rd32(buf + 0x14);
    uint32_t dataTableOffset = rd32(buf + 0x20);
    uint32_t nameTableOffset = rd32(buf + 0x28);
    uint32_t nameTableSize   = rd32(buf + 0x2C);

    int rc = 0;
    for (uint32_t i = 0; i < instanceCount; i++) {
        long doff = 0x40 + (long)i * 20;
        if (doff + 20 > fsize) { rc = 1; break; }
        const unsigned char *d = buf + doff;

        if (rd32(d) != 0x54584d50u)   /* TemplateTag.TXMP */
            continue;

        uint32_t dataOffset = rd32(d + 4);
        uint32_t nameOffset = rd32(d + 8);
        uint32_t dataSize   = rd32(d + 12);
        uint32_t flags      = rd32(d + 16) & 0xff;

        if ((flags & 0x02) || dataSize == 0 || dataOffset == 0)
            continue;   /* placeholder: no local data */

        long fmtOff = (long)dataTableOffset + (long)dataOffset + 0x88;
        if (fmtOff + 4 > fsize) {
            fprintf(stderr, "%s: TXMP #%u data out of range\n", path, i);
            rc = 1; continue;
        }
        uint32_t fmt = rd32(buf + fmtOff);
        const char *fname = format_name(fmt);
        char fmtbuf[24];
        if (!fname) {
            snprintf(fmtbuf, sizeof fmtbuf, "UNKNOWN(%u)", fmt);
            fname = fmtbuf;
        }

        const char *name = "-";
        if (!(flags & 0x01) && version == 0x56523331u &&
            nameOffset < nameTableSize &&
            (long)nameTableOffset + nameOffset < fsize) {
            name = (const char *)buf + nameTableOffset + nameOffset;
            if (strncmp(name, "TXMP", 4) == 0)
                name += 4;   /* match OniSplit's tag-stripped names */
        }

        printf("%s\t%s\t%s\n", name, fname, path);
    }

    free(buf);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.dat|file.oni> [...]\n", argv[0]);
        return 2;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++)
        rc |= process(argv[i]);
    return rc;
}
