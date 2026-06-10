#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL2/SDL.h>

extern "C" {
#include "BFW.h"
#include "BFW_Motoko.h"
#include "Motoko_Manager.h"
#include "Oni_Platform.h"
#include "metal_engine.h"
}

// Pure-SDL display-mode enumerator shared with the GL backend (defined
// gl_sdl.c:336, declared gl_engine.h:826; verified it makes no GL calls).
// Reusing it gives Metal the identical curated mode list, so saved-resolution
// matching in ONrMotoko_SetResolution_Internal behaves the same on both.
extern "C" int gl_enumerate_valid_display_modes(M3tDisplayMode display_mode_list[M3cMaxDisplayModes]);

// ---- backend state -------------------------------------------------------
static M3tDrawEngineMethods    gMetalEngineMethods;
static M3tDrawEngineCaps       gMetalEngineCaps;
static M3tDrawContextMethods   gMetalDrawFuncs;

static id<MTLDevice>           gDevice;
static id<MTLCommandQueue>     gQueue;
static CAMetalLayer           *gLayer;
static SDL_MetalView           gView;
static id<CAMetalDrawable>     gDrawable;
static id<MTLCommandBuffer>    gCmd;
static id<MTLRenderCommandEncoder> gEncoder;

// ---- frame bracket (the only non-stub draw entry points in M0) ----------
// Signatures verified against Motoko_Manager.h:63-73 / gl_engine.c:29-31.

// M0 boundary diagnostics (issue #43 black-window investigation): counters at
// the game-loop → CAMetalLayer boundary, logged one-shot + periodically.
static UUtUns32 gDiagFrameStarts, gDiagNilDrawables, gDiagPresents;

static UUtError metal_frame_start(UUtUns32 inGameTime)
{
	(void)inGameTime;
	gDiagFrameStarts++;
	if (gDiagFrameStarts == 1) {
		UUrStartupMessage("[Metal] first frame_start: layer=%p attached=%d bounds=%.0fx%.0f drawableSize=%.0fx%.0f scale=%.1f",
			(__bridge void *)gLayer, gLayer.superlayer != nil,
			gLayer.bounds.size.width, gLayer.bounds.size.height,
			gLayer.drawableSize.width, gLayer.drawableSize.height,
			gLayer.contentsScale);
	}
	// nextDrawable returns an autoreleased object; the main-runloop pool drain
	// covers the normal frame loop. Add an explicit @autoreleasepool here if a
	// future path ever pumps frames without returning to the runloop.
	gDrawable = [gLayer nextDrawable];
	if (gDrawable == nil) { // skip frame; never crash
		gDiagNilDrawables++;
		if (gDiagNilDrawables == 1 || (gDiagNilDrawables % 300) == 0) {
			UUrStartupMessage("[Metal] nextDrawable nil (%u of %u frame_starts)",
				gDiagNilDrawables, gDiagFrameStarts);
		}
		return UUcError_None;
	}

	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture     = gDrawable.texture;
	rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
	rp.colorAttachments[0].clearColor  = MTLClearColorMake(0.0, 0.15, 0.30, 1.0); // distinctive teal
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;

	gCmd     = [gQueue commandBuffer];
	gEncoder = [gCmd renderCommandEncoderWithDescriptor:rp];
	return UUcError_None;
}

static UUtError metal_frame_end(UUtUns32 *out_texture_bytes_downloaded)
{
	if (gEncoder) { [gEncoder endEncoding]; gEncoder = nil; }
	if (gCmd) {
		if (gDrawable) {
			[gCmd presentDrawable:gDrawable];
			gDiagPresents++;
			if (gDiagPresents == 1 || (gDiagPresents % 300) == 0) {
				UUrStartupMessage("[Metal] presented frame %u", gDiagPresents);
			}
		}
		[gCmd commit];
		gCmd = nil;
	}
	gDrawable = nil;
	if (out_texture_bytes_downloaded) { *out_texture_bytes_downloaded = 0; }
	return UUcError_None;
}

static UUtError metal_frame_sync(void) { return UUcError_None; }

// ---- no-op stubs (real implementations land in M1+) ----------------------
// Signatures match the typedefs in Motoko_Manager.h:79-152.
static void     metal_triangle(void *inTriangle) { (void)inTriangle; }
static void     metal_quad(void *inQuad) { (void)inQuad; }
static void     metal_pent(void *inPent) { (void)inPent; }
static void     metal_line(UUtUns32 a, UUtUns32 b) { (void)a; (void)b; }
static void     metal_point(M3tPointScreen *p) { (void)p; }
static void     metal_tri_sprite(const M3tPointScreen *pts, const M3tTextureCoord *uv) { (void)pts; (void)uv; }
static void     metal_sprite(const M3tPointScreen *pts, const M3tTextureCoord *uv) { (void)pts; (void)uv; }
static void     metal_sprite_array(const M3tPointScreen *pts, const M3tTextureCoord *uv, const UUtUns32 *cols, const UUtUns32 n) { (void)pts; (void)uv; (void)cols; (void)n; }
static UUtError metal_screen_capture(const UUtRect *r, void *out) { (void)r; (void)out; return UUcError_None; }
static UUtBool  metal_point_visible(const M3tPointScreen *p, float tol) { (void)p; (void)tol; return UUcTrue; }
static UUtBool  metal_support_point_visible(void) { return UUcFalse; }
static UUtBool  metal_texture_format_available(IMtPixelType t) { (void)t; return UUcTrue; }
static UUtError metal_change_mode(M3tDisplayMode m) { (void)m; return UUcError_None; }
static void     metal_reset_fog(void) { }
static UUtBool  metal_load_texture(M3tTextureMap *tm) { (void)tm; return UUcTrue; }
static UUtBool  metal_unload_texture(M3tTextureMap *tm) { (void)tm; return UUcTrue; }
static UUtBool  metal_support_single_pass_multitexture(void) { return UUcFalse; }

// ---- context lifecycle ---------------------------------------------------
static UUtError metal_context_private_new(
	M3tDrawContextDescriptor *in_desc,
	M3tDrawContextMethods    **out_funcs,
	UUtBool                    in_full_screen,
	M3tDrawAPI                *out_api)
{
	(void)in_desc; (void)in_full_screen;
	UUrStartupMessage("creating new Metal context");
	*out_api = M3cDrawAPI_Metal;

	{
		Uint32 wflags = SDL_GetWindowFlags((SDL_Window *)ONgPlatformData.gameWindow);
		UUrStartupMessage("[Metal] window=%p flags: METAL=%d OPENGL=%d",
			(void *)ONgPlatformData.gameWindow,
			(wflags & SDL_WINDOW_METAL) != 0, (wflags & SDL_WINDOW_OPENGL) != 0);
	}
	gView  = SDL_Metal_CreateView((SDL_Window *)ONgPlatformData.gameWindow);
	if (gView == NULL) {
		UUrStartupMessage("[Metal] SDL_Metal_CreateView FAILED: %s", SDL_GetError());
	}
	if (gView != NULL && SDL_Metal_GetLayer(gView) == NULL) {
		UUrStartupMessage("[Metal] SDL_Metal_GetLayer returned NULL: %s", SDL_GetError());
	}
	// __bridge (no transfer): the layer is owned by gView; we hold a borrowed ref.
	gLayer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(gView);
	gLayer.device      = gDevice;
	gLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

	gMetalDrawFuncs.frameStart                    = metal_frame_start;
	gMetalDrawFuncs.frameEnd                      = metal_frame_end;
	gMetalDrawFuncs.frameSync                     = metal_frame_sync;
	gMetalDrawFuncs.triangle                      = metal_triangle;
	gMetalDrawFuncs.quad                          = metal_quad;
	gMetalDrawFuncs.pent                          = metal_pent;
	gMetalDrawFuncs.line                          = metal_line;
	gMetalDrawFuncs.point                         = metal_point;
	gMetalDrawFuncs.triSprite                     = metal_tri_sprite;
	gMetalDrawFuncs.sprite                        = metal_sprite;
	gMetalDrawFuncs.spriteArray                   = metal_sprite_array;
	gMetalDrawFuncs.screenCapture                 = metal_screen_capture;
	gMetalDrawFuncs.pointVisible                  = metal_point_visible;
	gMetalDrawFuncs.supportPointVisible           = metal_support_point_visible;
	gMetalDrawFuncs.textureFormatAvailable        = metal_texture_format_available;
	gMetalDrawFuncs.changeMode                    = metal_change_mode;
	gMetalDrawFuncs.resetFog                      = metal_reset_fog;
	gMetalDrawFuncs.loadTexture                   = metal_load_texture;
	gMetalDrawFuncs.unloadTexture                 = metal_unload_texture;
	gMetalDrawFuncs.supportSinglePassMultitexture = metal_support_single_pass_multitexture;

	*out_funcs = &gMetalDrawFuncs;
	return UUcError_None;
}

static void metal_context_private_delete(void)
{
	gEncoder = nil; gCmd = nil; gDrawable = nil;
	gLayer = nil; // drop the borrowed ref BEFORE destroying the owning view
	if (gView) { SDL_Metal_DestroyView(gView); gView = NULL; }
}
static void     metal_texture_reset_all(void) { }
static UUtError metal_private_state_new(void *s) { (void)s; return UUcError_None; }
static void     metal_private_state_delete(void *s) { (void)s; }
static UUtError metal_private_state_update(void *s, UUtUns32 intFlags, const UUtInt32 *ints, UUtInt32 ptrFlags, void **ptrs)
{ (void)s; (void)intFlags; (void)ints; (void)ptrFlags; (void)ptrs; return UUcError_None; }

// ---- registration --------------------------------------------------------
UUtBool metal_is_available(void)
{
	if (gDevice == nil) { gDevice = MTLCreateSystemDefaultDevice(); }
	return (gDevice != nil) ? UUcTrue : UUcFalse;
}

UUtBool metal_draw_engine_initialize(void)
{
	if (!metal_is_available()) {
		UUrError_Report(UUcError_Generic, "no Metal device present");
		return UUcFalse;
	}
	gQueue = [gDevice newCommandQueue];

	gMetalEngineMethods.contextPrivateNew    = metal_context_private_new;
	gMetalEngineMethods.contextPrivateDelete = metal_context_private_delete;
	gMetalEngineMethods.textureResetAll      = metal_texture_reset_all;
	gMetalEngineMethods.privateStateSize     = 0;
	gMetalEngineMethods.privateStateNew      = metal_private_state_new;
	gMetalEngineMethods.privateStateDelete   = metal_private_state_delete;
	gMetalEngineMethods.privateStateUpdate   = metal_private_state_update;

	gMetalEngineCaps.engineFlags = M3cDrawEngineFlag_3DOnly;
	UUrString_Copy(gMetalEngineCaps.engineName, "Metal", M3cMaxNameLen);
	gMetalEngineCaps.engineDriver[0] = 0;
	gMetalEngineCaps.engineVersion   = 1;
	gMetalEngineCaps.numDisplayDevices = 1;

	// Same curated mode list as the GL backend — the enumerator is pure SDL,
	// so it works identically with no GL context (mirrors gl_engine.c:83-85).
	memset(gMetalEngineCaps.displayDevices[0].displayModes, 0,
	       sizeof(gMetalEngineCaps.displayDevices[0].displayModes));
	gMetalEngineCaps.displayDevices[0].numDisplayModes =
		gl_enumerate_valid_display_modes(gMetalEngineCaps.displayDevices[0].displayModes);

	if (M3rManager_Register_DrawEngine(&gMetalEngineCaps, &gMetalEngineMethods) != UUcError_None) {
		UUrError_Report(UUcError_Generic, "could not register Metal draw engine");
		return UUcFalse;
	}
	UUrStartupMessage("Metal draw engine registered (device: %s)", [[gDevice name] UTF8String]);
	return UUcTrue;
}

void metal_draw_engine_terminate(void)
{
	metal_context_private_delete();
	gQueue = nil; gDevice = nil;
	UUrStartupMessage("Metal draw engine disposed");
}
