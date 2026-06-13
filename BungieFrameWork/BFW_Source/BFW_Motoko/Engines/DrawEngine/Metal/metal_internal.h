// metal_internal.h — shared state between the Metal backend's ObjC++ TUs.
// ObjC++ only: metal_engine.mm / metal_draw.mm / metal_texture.mm.
#ifndef METAL_INTERNAL_H
#define METAL_INTERNAL_H

#if !defined(__OBJC__) || !defined(__cplusplus)
#error "metal_internal.h is internal to the Metal backend's ObjC++ units"
#endif

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL2/SDL.h>

extern "C" {
#include "BFW.h"
#include "BFW_Motoko.h"
#include "Motoko_Manager.h"
}

// ---- vertex / enums --------------------------------------------------------
typedef struct MetalScreenVertex
{
	float    x, y, z;
	float    u, v, w;
	UUtUns8  r, g, b, a;
} MetalScreenVertex;
// Must match VSIn (packed_float3 x2 + uchar4) in metal_shaders.h exactly.
_Static_assert(sizeof(MetalScreenVertex) == 28, "MetalScreenVertex/VSIn layout mismatch");

// Fragment fog uniform — GL_LINEAR fog. Layout must match FogU in
// metal_shaders.h: float4 color (rgb = fog colour, a = enabled 0/1) at offset 0,
// float2 range (start, end) in screen-space z at offset 16.
// NB: the alpha slot carries the per-batch fog-enable (gMetalFogEnabled, a
// UUtBool packed to 0/1f), NOT GL's fog_color.a (which GL holds at 0 and ignores).
// Task 2's packing code crosses that bool->float boundary (the _Static_assert
// guards size, not that conversion).
typedef struct MetalFogUniform
{
	float colorR, colorG, colorB, enabled;   // -> float4 color
	float start, end;                          // -> float2 range
} MetalFogUniform;
_Static_assert(sizeof(MetalFogUniform) == 24, "MetalFogUniform/FogU layout mismatch");

typedef enum MetalBlendMode
{
	MetalBlend_Opaque = 0,     // (ONE, ZERO)                    — gl_set_textures explicit/untextured
	MetalBlend_Alpha,          // (SRC_ALPHA, 1-SRC_ALPHA)       — default textured
	MetalBlend_Additive,       // (SRC_ALPHA, ONE)               — M3cTextureFlags_Blend_Additive
	MetalBlend_MultipassBase,  // (ONE, SRC_ALPHA)               — base_map_multipass (idle until M3)
	MetalBlend_Count
} MetalBlendMode;
// depth-stencil table index: bit0 = z-compare (LEQUAL), bit1 = z-write

enum
{
	MetalRing_Depth        = 3,                        // frames in flight
	MetalRing_Bytes        = 8 * 1024 * 1024,          // per-frame vertex budget (~300K verts)
	MetalRing_MaxVertices  = MetalRing_Bytes / sizeof(MetalScreenVertex)
};

// Geometry submit modes — same meanings as gl_engine.h's _geom_draw_mode_*.
// Env-map modes intentionally collapse to the low-quality (base-only) fallback
// in M1; the real combiner is M3.
typedef enum MetalGeomMode
{
	MetalGeom_Default = 0,     // per-vertex shade + base texture
	MetalGeom_Wireframe,       // line strip, constant colour, no texture
	MetalGeom_Gouraud,         // per-vertex shade, no texture
	MetalGeom_Flat,            // base texture only, constant colour
	MetalGeom_Split,           // split vertex format: own shades + UV indices
	MetalGeom_EnvBaseFallback  // env-mapped: draw base map only (M1 fallback)
} MetalGeomMode;

// ---- device / frame objects (owned by metal_engine.mm) --------------------
extern id<MTLDevice>              gMetalDevice;
extern id<MTLCommandQueue>        gMetalQueue;
extern CAMetalLayer              *gMetalLayer;
extern id<CAMetalDrawable>        gMetalDrawable;
extern id<MTLCommandBuffer>       gMetalCmd;
extern id<MTLRenderCommandEncoder> gMetalEncoder;

extern id<MTLRenderPipelineState> gMetalPipelines[MetalBlend_Count];
extern id<MTLDepthStencilState>   gMetalDepthStates[4];   // bit0 = compare, bit1 = write
extern id<MTLTexture>             gMetalDepthTexture;
extern id<MTLTexture>             gMetalWhiteTexture;     // 1x1 white: the "no texture" texture

extern id<MTLBuffer>              gMetalRing[MetalRing_Depth];
extern UUtUns32                   gMetalRingIndex;        // which ring buffer this frame
extern UUtUns32                   gMetalRingCursor;       // vertices written this frame
extern UUtBool                    gMetalRingOverflowed;   // logged-once-per-frame marker
extern dispatch_semaphore_t       gMetalInflight;

// ---- decoded Motoko draw state (written by metal_private_state_update) ----
extern const UUtInt32            *gMetalStateInt;         // retained engine-side like gl->state_int
extern void                     **gMetalStatePtr;
extern M3tTextureMap             *gMetalTexture0;         // resolved base texture for this state
extern MetalGeomMode              gMetalGeomMode;
extern UUtUns8                    gMetalConstantR, gMetalConstantG, gMetalConstantB, gMetalConstantA;
extern UUtBool                    gMetalBufferClear;
extern MTLClearColor              gMetalClearColor;
extern UUtUns32                   gMetalDepthStateIndex;  // current depth table index
extern M3tDisplayMode             gMetalDisplayMode;      // active logical mode (ortho + GLg feed)

// ---- per-encoder caches (reset every frameStart) ---------------------------
extern id<MTLRenderPipelineState> gMetalBoundPipeline;
extern id<MTLTexture>             gMetalBoundTexture;
extern id<MTLSamplerState>        gMetalBoundSampler;
extern UUtUns32                   gMetalBoundDepthIndex;

// ---- cross-TU functions ----------------------------------------------------
// metal_engine.mm
UUtBool metal_apply_display_settings(UUtUns16 inWidth, UUtUns16 inHeight);

// metal_draw.mm (Task 3)
void metal_draw_install_methods(M3tDrawContextMethods *ioMethods);
UUtBool metal_select_textures(M3tTextureMap *inTexture0, int inBlendOverride);
MetalScreenVertex *metal_ring_reserve(UUtUns32 inCount, UUtUns32 *outFirstVertex);

// metal_texture.mm (Task 2)
UUtError metal_texture_system_initialize(void);
void     metal_texture_system_terminate(void);
UUtBool  metal_texture_map_create(M3tTextureMap *texture_map);
UUtBool  metal_texture_map_delete(M3tTextureMap *texture_map);
UUtBool  metal_texture_format_available(IMtPixelType texel_type);
id<MTLTexture>      metal_texture_lookup(M3tTextureMap *inMap, id<MTLSamplerState> *outSampler);
id<MTLSamplerState> metal_default_sampler(void);

// ---- fog state (metal_fog.mm) ----------------------------------------------
extern float   gMetalFogStart, gMetalFogEnd;                 // screen-space z range
extern float   gMetalFogColorR, gMetalFogColorG, gMetalFogColorB;
extern UUtBool gMetalFogEnabled;                             // per-batch (M3cDrawStateIntType_Fog)

void  metal_fog_system_initialize(void);   // defaults + same-named script-var registration
void  metal_reset_fog(void);               // resetFog vtable impl (replaces M1 stub)
void  metal_fog_update(int inFrames);      // per-frame ramp step (call from frame_start)
// Particle fog-factor query. No vtable slot exists for this yet — Task 3 adds a
// M3tDrawContextMethod_FogFactor typedef + fogFactor field to M3tDrawContextMethods
// and an M3rDraw_GetFogFactor wrapper, then points BFW_Particle3.c at the wrapper
// (today it calls gl_calculate_fog_factor via a direct extern).
float metal_calculate_fog_factor(M3tPoint3D *inPoint);

#endif // METAL_INTERNAL_H
