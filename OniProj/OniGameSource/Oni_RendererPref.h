// ======================================================================
// Oni_RendererPref.h
//
// Persisted renderer preference (#89, Metal M5). One-token state file
// "renderer.txt" living wherever ONiBundlePath_ResolveStateFile puts it
// (cwd for the bare-binary workflow, App Support for the .app).
//
// Launch precedence is: -metal / -renderer flag > ONI_RENDERER env >
// this file > default OpenGL. The hold-Option chooser is a transient
// per-launch override and never writes here; the Options-screen toggle
// is the only writer.
//
// Kept free of engine deps beyond BFW base types so the TU compiles
// standalone for ../../tests/test_oni_renderer_pref.c.
// ======================================================================
#ifndef ONI_RENDERER_PREF_H
#define ONI_RENDERER_PREF_H

#include "BFW.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ONtRendererPref
{
	ONcRendererPref_None,		// no file / unreadable / unknown token
	ONcRendererPref_OpenGL,
	ONcRendererPref_Metal
} ONtRendererPref;

// "metal" -> Metal, "opengl" or "gl" -> OpenGL, anything else (including
// NULL and empty) -> None. Surrounding whitespace and a trailing newline
// are tolerated; the comparison is case-insensitive.
ONtRendererPref ONrRendererPref_ParseToken(const char *inToken);

// Path-explicit forms (unit-testable without the resolver).
ONtRendererPref ONrRendererPref_ReadFromPath(const char *inPath);
UUtBool ONrRendererPref_WriteToPath(const char *inPath, UUtBool inUseMetal);

// Resolver-backed forms used by the engine. Read returns None on any
// failure; Write returns UUcFalse on any failure (callers warn, never abort).
ONtRendererPref ONrRendererPref_Read(void);
UUtBool ONrRendererPref_Write(UUtBool inUseMetal);

#ifdef __cplusplus
}
#endif

// ======================================================================
#endif /* ONI_RENDERER_PREF_H */
