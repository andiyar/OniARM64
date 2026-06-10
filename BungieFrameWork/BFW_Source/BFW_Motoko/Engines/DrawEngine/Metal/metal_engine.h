#ifndef METAL_ENGINE_H
#define METAL_ENGINE_H

#include "BFW.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pre-window availability probe: cheap, caches the MTLDevice. Called from
// Oni.c BEFORE SDL window creation — the last point where falling back to
// OpenGL is still possible (the window's renderer flag is fixed at creation).
UUtBool metal_is_available(void);

// Registers the Metal DrawEngine into the Motoko manager. Returns UUcFalse if
// no Metal device is available. macOS only.
UUtBool metal_draw_engine_initialize(void);
void    metal_draw_engine_terminate(void);

#ifdef __cplusplus
}
#endif

#endif // METAL_ENGINE_H
