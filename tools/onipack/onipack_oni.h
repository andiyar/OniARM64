/* onipack_oni.h — read one OniSplit V32 single-instance TXMP .oni file.
 * addresses #88 */
#ifndef ONIPACK_ONI_H
#define ONIPACK_ONI_H

#include <stdint.h>
#include <stddef.h>

#define OPK_NAME_MAX 128

typedef struct {
    char     name[OPK_NAME_MAX];    /* instance name: filename-derived, tag-stripped, %hh-decoded */
    uint8_t  body[168];             /* TXMP body verbatim (link + offset slots rewritten by writer) */
    uint8_t *pixels;                /* malloc'd texel blob (base + mips) */
    uint32_t pixelsSize;
    char     animName[OPK_NAME_MAX];   /* TXAN link target instance name, "" if none */
    char     envMapName[OPK_NAME_MAX]; /* TXMP link target instance name, "" if none */
} OpkTexture;

/* Returns 0 on success. On failure returns -1 with a one-line reason in err.
 * Guards enforced here (#62): file must contain EXACTLY ONE non-placeholder
 * instance and its tag must be TXMP; anything else is rejected. */
int  opk_oni_read(const char *path, OpkTexture *out, char *err, size_t errsz);
void opk_texture_free(OpkTexture *t);

/* exposed for tests */
void opk_percent_decode(char *s);
void opk_name_from_filename(const char *path, char out[OPK_NAME_MAX]);

#endif
