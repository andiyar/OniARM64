// ======================================================================
// ONi_TexturePacks.h
//
// Pure, dependency-free (libc-only) discovery of installed HD texture-pack
// directories. No BFW / engine types, so the logic is unit-testable in
// isolation (see ../../tests/test_oni_texturepacks.c) and links into the
// engine's data-registration path unchanged. A texture pack is just a
// subdirectory under <appSupportDir>/TexturePacks/; this module finds the
// enabled ones and hands back their absolute paths in a deterministic order.
//
// Booleans / counts are plain int to stay free of <stdbool.h> include
// ordering concerns when pulled into mixed C / Obj-C++ translation units.
// ======================================================================
#ifndef ONI_TEXTUREPACKS_H
#define ONI_TEXTUREPACKS_H

#ifdef __cplusplus
extern "C" {
#endif

#define ONI_TP_MAX_PACKS 32
#define ONI_TP_PATH_MAX  1024

// Enumerate enabled texture-pack directories under <appSupportDir>/TexturePacks/.
// Returns count (0..ONI_TP_MAX_PACKS); fills outRoots[i] with absolute dir paths,
// sorted ascending for deterministic load order. Returns 0 if ONI_TEXTUREPACKS=0,
// if the TexturePacks dir is absent, or on any error.
int ONi_TexturePacks_Enumerate(const char *appSupportDir,
                               char outRoots[ONI_TP_MAX_PACKS][ONI_TP_PATH_MAX]);

#ifdef __cplusplus
}
#endif

#endif // ONI_TEXTUREPACKS_H
