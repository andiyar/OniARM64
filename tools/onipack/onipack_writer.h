/* onipack_writer.h — build + emit a VR31 Mac-family TXMP overlay pack.
 * addresses #88 */
#ifndef ONIPACK_WRITER_H
#define ONIPACK_WRITER_H

#include "onipack_oni.h"

typedef struct OpkPack OpkPack;

/* level/suffix must match the output filename (fileID is derived from them).
 * suffix charset is [A-Za-z0-9]+ and "Final" is refused. Returns NULL on any
 * invalid argument (no error message is produced). */
OpkPack *opk_pack_new(int level, const char *suffix);

/* Takes ownership of tex->pixels on success (caller must NOT free).
 * Duplicate instance names are an error (staging guarantees uniqueness). */
int opk_pack_add(OpkPack *p, OpkTexture *tex, char *err, size_t errsz);

/* Adds a whole group (base + unnamed frames + TXAN) contiguously and
 * atomically: on error the pack is unchanged. Takes ownership of the
 * group's pixel/txanBody buffers only on success (NULLs them in *g).
 * TXAN maps[] ids are remapped to pack indices; the maps[0]==0
 * base-frame convention passes through verbatim. */
int opk_pack_add_group(OpkPack *p, OpkGroup *g, char *err, size_t errsz);

/* Writes <dir>/level<N>_<Suffix>.dat/.raw/.sep via .tmp + rename (dat last). */
int opk_pack_write(OpkPack *p, const char *outDir, char *err, size_t errsz);

/* opk_pack_count: total instance descriptors — real + unnamed frames/TXAN +
 * placeholders (what the emitted header's nInst will say).
 * opk_pack_count_textures: user-facing texture count — real NAMED TXMPs
 * only (animated frames, TXAN and placeholders excluded). */
int  opk_pack_count(const OpkPack *p);
int  opk_pack_count_textures(const OpkPack *p);
void opk_pack_free(OpkPack *p);

#endif
