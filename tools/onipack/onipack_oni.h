/* onipack_oni.h — read one OniSplit V32 TXMP .oni file: plain
 * single-texture export or animated group (frame TXMPs + one TXAN).
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
 * instance and its tag must be TXMP; anything else is rejected. Animated
 * multi-instance files are rejected too — use opk_oni_read_group. */
int  opk_oni_read(const char *path, OpkTexture *out, char *err, size_t errsz);
void opk_texture_free(OpkTexture *t);

/* ---- animated-group model: frame TXMPs + one TXAN (#88 task 4b) ------- */

typedef struct {
    int32_t local;              /* >= 0: file-local descriptor index (intra-file
                                   ref, writer remaps later)
                                   == -1 && name[0]: named external placeholder
                                   == -1 && !name[0]: none */
    char    name[OPK_NAME_MAX];
} OpkRef;

typedef struct {
    uint8_t  body[168];
    uint8_t *pixels;
    uint32_t pixelsSize;
    int32_t  localIdx;          /* this instance's file descriptor index */
    OpkRef   anim, envMap;
} OpkGroupTex;

#define OPK_GROUP_MAX 64

typedef struct {
    char        name[OPK_NAME_MAX]; /* base texture name (filename-derived) */
    uint32_t    nTex;
    OpkGroupTex tex[OPK_GROUP_MAX]; /* tex[0] = base: the anim-carrier TXMP when
                                       a TXAN is present (validated: exactly one
                                       must reference it), else the first real
                                       TXMP descriptor */
    int         hasTxan;
    int32_t     txanLocalIdx;
    uint8_t    *txanBody;           /* raw body incl maps[] (still file-local ids) */
    uint32_t    txanBodySize;
} OpkGroup;

/* Accepts 1..OPK_GROUP_MAX real TXMPs, at most ONE real TXAN (web-internal
 * frames+timing, not a #62 hazard) and any number of placeholders; any other
 * real tag is rejected (#62). TXAN maps[] entries stay file-local in
 * txanBody; maps[0]==0 means "base texture" (engine convention, see
 * onipack_format.h). Returns 0 on success, -1 with reason in err.
 * Zeroes *g on entry: opk_group_free any previous group BEFORE reusing it.
 * On success the caller owns pixels/txanBody; to keep a buffer past
 * opk_group_free, NULL the field first (supported ownership transfer). */
int  opk_oni_read_group(const char *path, OpkGroup *g, char *err, size_t errsz);
void opk_group_free(OpkGroup *g);

/* exposed for tests */
void opk_percent_decode(char *s);
void opk_name_from_filename(const char *path, char out[OPK_NAME_MAX]);

#endif
