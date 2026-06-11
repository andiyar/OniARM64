#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL2/SDL.h>

#include "metal_internal.h"
#include "metal_shaders.h"

extern "C" {
#include "Oni_Platform.h"
#include "metal_engine.h"
// Mouse-coordinate scaling reads these (BFW_LI_Platform_SDL.c:406,550,588).
// Defined in gl_sdl.c (always compiled); GL sets them in gl_platform_initialize.
extern int GLgGameWidth, GLgGameHeight;
// Persisted gamma; M3rSetGamma (BFW_Motoko.h:2388) is the pure-SDL ramp in gl_sdl.c.
extern float ONrPersist_GetGamma(void);
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

// Device / frame objects — shared with metal_texture.mm (Task 2) and
// metal_draw.mm (Task 3) through metal_internal.h. The SDL_MetalView handle
// stays file-static: only context create/delete touch it.
id<MTLDevice>               gMetalDevice;
id<MTLCommandQueue>         gMetalQueue;
CAMetalLayer               *gMetalLayer;
id<CAMetalDrawable>         gMetalDrawable;
id<MTLCommandBuffer>        gMetalCmd;
id<MTLRenderCommandEncoder> gMetalEncoder;
static SDL_MetalView        gView;

id<MTLRenderPipelineState>  gMetalPipelines[MetalBlend_Count];
id<MTLDepthStencilState>    gMetalDepthStates[4];
id<MTLTexture>              gMetalDepthTexture;
id<MTLTexture>              gMetalWhiteTexture;

id<MTLBuffer>               gMetalRing[MetalRing_Depth];
UUtUns32                    gMetalRingIndex;
UUtUns32                    gMetalRingCursor;
UUtBool                     gMetalRingOverflowed;
dispatch_semaphore_t        gMetalInflight;

const UUtInt32             *gMetalStateInt;
void                      **gMetalStatePtr;
M3tTextureMap              *gMetalTexture0;
MetalGeomMode               gMetalGeomMode;
UUtUns8                     gMetalConstantR = 0xFF, gMetalConstantG = 0xFF,
                            gMetalConstantB = 0xFF, gMetalConstantA = 0xFF;
UUtBool                     gMetalBufferClear;
MTLClearColor               gMetalClearColor = {0.0, 0.0, 0.0, 1.0};
UUtUns32                    gMetalDepthStateIndex = 3; // compare+write, matches gl init
M3tDisplayMode              gMetalDisplayMode;

id<MTLRenderPipelineState>  gMetalBoundPipeline;
id<MTLTexture>              gMetalBoundTexture;
id<MTLSamplerState>         gMetalBoundSampler;
UUtUns32                    gMetalBoundDepthIndex = 0xFFFFFFFF;

// ---- GPU object construction ----------------------------------------------

// Build the shader library, the four blend PSOs, the four depth states, the
// white texture, the vertex ring, and the in-flight semaphore. Called from
// metal_context_private_new (device + layer exist; pixel format is fixed BGRA8).
static UUtBool metal_build_pipeline_objects(void)
{
	NSError *err = nil;

	if (gMetalPipelines[MetalBlend_Opaque] != nil) { return UUcTrue; } // already built

	id<MTLLibrary> lib = [gMetalDevice newLibraryWithSource:
		[NSString stringWithUTF8String:kMetalShaderSource] options:nil error:&err];
	if (lib == nil) {
		UUrStartupMessage("[Metal] shader compile FAILED: %s",
			err ? [[err localizedDescription] UTF8String] : "unknown");
		return UUcFalse;
	}
	id<MTLFunction> vfn = [lib newFunctionWithName:@"oni_vertex"];
	id<MTLFunction> ffn = [lib newFunctionWithName:@"oni_fragment"];
	if (vfn == nil || ffn == nil) {
		UUrStartupMessage("[Metal] shader functions missing");
		return UUcFalse;
	}

	// Blend factor table mirroring glBlendFunc usage (factors apply to RGB and A).
	static const struct { MTLBlendFactor src, dst; } kBlend[MetalBlend_Count] = {
		{ MTLBlendFactorOne,         MTLBlendFactorZero },                 // Opaque
		{ MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha },  // Alpha
		{ MTLBlendFactorSourceAlpha, MTLBlendFactorOne },                  // Additive
		{ MTLBlendFactorOne,         MTLBlendFactorSourceAlpha },          // MultipassBase
	};

	for (int i = 0; i < MetalBlend_Count; i++) {
		MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
		pd.vertexFunction   = vfn;
		pd.fragmentFunction = ffn;
		pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
		pd.colorAttachments[0].blendingEnabled = YES; // GL_BLEND is always on in gl
		pd.colorAttachments[0].sourceRGBBlendFactor        = kBlend[i].src;
		pd.colorAttachments[0].destinationRGBBlendFactor   = kBlend[i].dst;
		pd.colorAttachments[0].sourceAlphaBlendFactor      = kBlend[i].src;
		pd.colorAttachments[0].destinationAlphaBlendFactor = kBlend[i].dst;
		pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
		gMetalPipelines[i] = [gMetalDevice newRenderPipelineStateWithDescriptor:pd error:&err];
		if (gMetalPipelines[i] == nil) {
			UUrStartupMessage("[Metal] PSO %d FAILED: %s", i,
				err ? [[err localizedDescription] UTF8String] : "unknown");
			return UUcFalse;
		}
	}

	// Depth-stencil table — index bit0 = compare(LEQUAL), bit1 = write.
	// Mirrors gl_depth_mode_set: read -> LEQUAL else ALWAYS; (0,0) = disabled.
	for (int i = 0; i < 4; i++) {
		MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
		dd.depthCompareFunction = (i & 1) ? MTLCompareFunctionLessEqual
		                                  : MTLCompareFunctionAlways;
		dd.depthWriteEnabled    = (i & 2) ? YES : NO;
		gMetalDepthStates[i] = [gMetalDevice newDepthStencilStateWithDescriptor:dd];
	}

	// 1x1 white — bound when a draw has no texture so one PSO serves all modes.
	{
		MTLTextureDescriptor *td = [MTLTextureDescriptor
			texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
			width:1 height:1 mipmapped:NO];
		gMetalWhiteTexture = [gMetalDevice newTextureWithDescriptor:td];
		const UUtUns8 white[4] = {0xFF, 0xFF, 0xFF, 0xFF};
		[gMetalWhiteTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
			mipmapLevel:0 withBytes:white bytesPerRow:4];
	}

	for (int i = 0; i < MetalRing_Depth; i++) {
		gMetalRing[i] = [gMetalDevice newBufferWithLength:MetalRing_Bytes
			options:MTLResourceStorageModeShared];
	}
	gMetalInflight = dispatch_semaphore_create(MetalRing_Depth);

	UUrStartupMessage("[Metal] pipeline objects built (4 PSOs, 4 depth states, %u KB ring x%u)",
		(unsigned)(MetalRing_Bytes / 1024), (unsigned)MetalRing_Depth);
	return UUcTrue;
}

// (Re)create the depth attachment to match the drawable size.
static UUtBool metal_create_depth_texture(NSUInteger inWidth, NSUInteger inHeight)
{
	if (inWidth == 0 || inHeight == 0) { return UUcFalse; }
	if (gMetalDepthTexture != nil &&
		gMetalDepthTexture.width == inWidth && gMetalDepthTexture.height == inHeight) {
		return UUcTrue;
	}
	MTLTextureDescriptor *td = [MTLTextureDescriptor
		texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
		width:inWidth height:inHeight mipmapped:NO];
	td.usage       = MTLTextureUsageRenderTarget;
	td.storageMode = MTLStorageModePrivate;
	gMetalDepthTexture = [gMetalDevice newTextureWithDescriptor:td];
	if (gMetalDepthTexture == nil) { return UUcFalse; }
	UUrStartupMessage("[Metal] depth texture %ux%u", (unsigned)inWidth, (unsigned)inHeight);
	return UUcTrue;
}

// Apply a logical display mode: SDL window size + fullscreen policy + gamma,
// then resize the layer's drawable to native pixels and rebuild the depth
// buffer. Feeds GLgGameWidth/Height — the input layer scales mouse coordinates
// by these. Shared by context-new and changeMode (Task 4).
UUtBool metal_apply_display_settings(UUtUns16 inWidth, UUtUns16 inHeight)
{
	SDL_Window *window = (SDL_Window *)ONgPlatformData.gameWindow;
	int px_w = 0, px_h = 0;

	SDL_SetWindowSize(window, inWidth, inHeight);
	// Same policy as the GL path (gl_sdl.c:61-74): desktop-fullscreen when
	// resolution switching is on; never exclusive fullscreen on macOS.
	if (SDL_SetWindowFullscreen(window,
			M3gResolutionSwitch ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
		UUrStartupMessage("[Metal] SDL_SetWindowFullscreen failed: %s", SDL_GetError());
	}
	M3rSetGamma(ONrPersist_GetGamma()); // pure-SDL gamma ramp (gl_sdl.c:18)

	// Native-pixel drawable: parity with GL's SDL_GL_GetDrawableSize viewport.
	// The ortho mapping uses the LOGICAL size, so HiDPI costs nothing here.
	SDL_Metal_GetDrawableSize(window, &px_w, &px_h);
	if (px_w <= 0 || px_h <= 0) { px_w = inWidth; px_h = inHeight; }
	gMetalLayer.drawableSize = CGSizeMake(px_w, px_h);

	// Mouse-coordinate scaling (BFW_LI_Platform_SDL.c:406,550,588) divides by
	// the window size and multiplies by these. GL feeds them in
	// gl_platform_initialize; Metal must do the same or the menu cursor lands
	// in the wrong place.
	GLgGameWidth  = inWidth;
	GLgGameHeight = inHeight;

	UUrStartupMessage("[Metal] display settings applied: %ux%u logical, %dx%d px drawable",
		inWidth, inHeight, px_w, px_h);

	return metal_create_depth_texture((NSUInteger)px_w, (NSUInteger)px_h);
}

// ---- frame bracket --------------------------------------------------------
// Signatures verified against Motoko_Manager.h:63-73 / gl_engine.c:29-31.

// M0 boundary diagnostics (issue #43 black-window investigation): counters at
// the game-loop → CAMetalLayer boundary, logged one-shot + periodically.
static UUtUns32 gDiagFrameStarts, gDiagNilDrawables, gDiagPresents;

static UUtError metal_frame_start(UUtUns32 inGameTime)
{
	(void)inGameTime;
	gDiagFrameStarts++;

	// Cap CPU run-ahead at MetalRing_Depth frames (ring reuse safety).
	dispatch_semaphore_wait(gMetalInflight, DISPATCH_TIME_FOREVER);

	// nextDrawable returns an autoreleased object; the main-runloop pool drain
	// covers the normal frame loop. Add an explicit @autoreleasepool here if a
	// future path ever pumps frames without returning to the runloop.
	gMetalDrawable = [gMetalLayer nextDrawable];
	if (gMetalDrawable == nil) { // skip frame; never crash (M0 behaviour kept)
		gDiagNilDrawables++;
		dispatch_semaphore_signal(gMetalInflight);
		if (gDiagNilDrawables == 1 || (gDiagNilDrawables % 300) == 0) {
			UUrStartupMessage("[Metal] nextDrawable nil (%u of %u frame_starts)",
				gDiagNilDrawables, gDiagFrameStarts);
		}
		return UUcError_None;
	}

	// Depth buffer must match the drawable (resize can race a frame).
	metal_create_depth_texture(gMetalDrawable.texture.width, gMetalDrawable.texture.height);

	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture     = gMetalDrawable.texture;
	// GL clears only when the committed state asks (gl_frame_start:555-559);
	// otherwise the old buffer is preserved (Load).
	rp.colorAttachments[0].loadAction  = gMetalBufferClear ? MTLLoadActionClear : MTLLoadActionLoad;
	rp.colorAttachments[0].clearColor  = gMetalClearColor;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	rp.depthAttachment.texture     = gMetalDepthTexture;
	rp.depthAttachment.loadAction  = gMetalBufferClear ? MTLLoadActionClear : MTLLoadActionLoad;
	rp.depthAttachment.clearDepth  = 1.0;
	rp.depthAttachment.storeAction = MTLStoreActionStore;

	gMetalCmd     = [gMetalQueue commandBuffer];
	gMetalEncoder = [gMetalCmd renderCommandEncoderWithDescriptor:rp];

	// Fresh encoder == no inherited bindings: reset the per-encoder caches
	// and (re)bind the frame-constant resources.
	gMetalBoundPipeline   = nil;
	gMetalBoundTexture    = nil;
	gMetalBoundSampler    = nil;
	gMetalBoundDepthIndex = 0xFFFFFFFF;

	gMetalRingIndex      = (gMetalRingIndex + 1) % MetalRing_Depth;
	gMetalRingCursor     = 0;
	gMetalRingOverflowed = UUcFalse;
	[gMetalEncoder setVertexBuffer:gMetalRing[gMetalRingIndex] offset:0 atIndex:0];

	{
		float screen[2] = { (float)gMetalDisplayMode.width, (float)gMetalDisplayMode.height };
		[gMetalEncoder setVertexBytes:screen length:sizeof(screen) atIndex:1];
	}
	[gMetalEncoder setCullMode:MTLCullModeNone]; // GL: glDisable(GL_CULL_FACE)

	if (gDiagFrameStarts == 1) {
		UUrStartupMessage("[Metal] first frame: %ux%u logical, %ux%u px, clear=%d",
			gMetalDisplayMode.width, gMetalDisplayMode.height,
			(unsigned)gMetalDrawable.texture.width, (unsigned)gMetalDrawable.texture.height,
			(int)gMetalBufferClear);
	}
	return UUcError_None;
}

static UUtError metal_frame_end(UUtUns32 *out_texture_bytes_downloaded)
{
	if (gMetalEncoder) { [gMetalEncoder endEncoding]; gMetalEncoder = nil; }
	if (gMetalCmd) {
		if (gMetalDrawable) {
			[gMetalCmd presentDrawable:gMetalDrawable];
			gDiagPresents++;
			if (gDiagPresents == 1 || (gDiagPresents % 300) == 0) {
				UUrStartupMessage("[Metal] presented frame %u (%u verts)",
					gDiagPresents, gMetalRingCursor);
			}
		}
		dispatch_semaphore_t sem = gMetalInflight;
		[gMetalCmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
			(void)cb;
			dispatch_semaphore_signal(sem);
		}];
		[gMetalCmd commit];
		gMetalCmd = nil;
	}
	gMetalDrawable = nil;
	if (out_texture_bytes_downloaded) { *out_texture_bytes_downloaded = 0; }
	return UUcError_None;
}

static UUtError metal_frame_sync(void) { return UUcError_None; }

// ---- non-draw stubs (primitives live in metal_draw.mm since M1 Task 3) ----
// Signatures match the typedefs in Motoko_Manager.h:79-152.
static UUtError metal_screen_capture(const UUtRect *r, void *out) { (void)r; (void)out; return UUcError_None; }
static UUtBool  metal_point_visible(const M3tPointScreen *p, float tol) { (void)p; (void)tol; return UUcTrue; }
static UUtBool  metal_support_point_visible(void) { return UUcFalse; }
static UUtError metal_change_mode(M3tDisplayMode mode)
{
	// gl_change_mode parity: store the mode, re-apply display settings
	// (window size, fullscreen policy, gamma, drawable + depth + GLg feed).
	gMetalDisplayMode = mode;
	if (!metal_apply_display_settings(mode.width, mode.height)) {
		return UUcError_Generic;
	}
	UUrStartupMessage("[Metal] mode change -> %ux%u", mode.width, mode.height);
	return UUcError_None;
}
static void     metal_reset_fog(void) { }
// MUST be true under Metal: M3rGeometry_Draw (Motoko_Geom.c:213-242) otherwise
// takes a multipass path that calls gl_prepare_multipass_* directly — GL
// internals that dereference the (NULL, under Metal) gl state. True routes
// env-mapped geometry through the normal state machine, where M1 renders the
// base map only (MetalGeom_EnvBaseFallback); the real combine is M3.
static UUtBool metal_support_single_pass_multitexture(void) { return UUcTrue; }

// ---- context lifecycle ---------------------------------------------------
static void metal_context_private_delete(void); // failure unwind in _new uses it

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
	gMetalLayer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(gView);
	gMetalLayer.device      = gMetalDevice;
	gMetalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	gMetalLayer.displaySyncEnabled = YES; // vsync parity with SDL_GL_SetSwapInterval(1)

	// Active logical mode from the manager, exactly like gl_context_private_new
	// (gl_engine.c:146-175): caps were registered by metal_draw_engine_initialize.
	{
		UUtUns16 active_engine, active_device, active_mode;
		M3rManager_GetActiveDrawEngine(&active_engine, &active_device, &active_mode);
		gMetalDisplayMode = M3rDrawEngine_GetCaps(active_engine)
			->displayDevices[active_device].displayModes[active_mode];
	}

	// On any failure below, unwind via metal_context_private_delete so a failed
	// init doesn't leak the SDL Metal view / partially built GPU objects.
	if (!metal_build_pipeline_objects()) {
		metal_context_private_delete();
		return UUcError_Generic;
	}
	if (!metal_apply_display_settings(gMetalDisplayMode.width, gMetalDisplayMode.height)) {
		metal_context_private_delete();
		return UUcError_Generic;
	}
	if (metal_texture_system_initialize() != UUcError_None) {
		UUrStartupMessage("[Metal] texture system initialize FAILED");
		metal_context_private_delete();
		return UUcError_Generic;
	}

	gMetalDrawFuncs.frameStart                    = metal_frame_start;
	gMetalDrawFuncs.frameEnd                      = metal_frame_end;
	gMetalDrawFuncs.frameSync                     = metal_frame_sync;
	metal_draw_install_methods(&gMetalDrawFuncs); // the eight primitive entries (metal_draw.mm)
	gMetalDrawFuncs.screenCapture                 = metal_screen_capture;
	gMetalDrawFuncs.pointVisible                  = metal_point_visible;
	gMetalDrawFuncs.supportPointVisible           = metal_support_point_visible;
	gMetalDrawFuncs.textureFormatAvailable        = metal_texture_format_available;
	gMetalDrawFuncs.changeMode                    = metal_change_mode;
	gMetalDrawFuncs.resetFog                      = metal_reset_fog;
	gMetalDrawFuncs.loadTexture                   = metal_texture_map_create;
	gMetalDrawFuncs.unloadTexture                 = metal_texture_map_delete;
	gMetalDrawFuncs.supportSinglePassMultitexture = metal_support_single_pass_multitexture;

	*out_funcs = &gMetalDrawFuncs;
	return UUcError_None;
}

static void metal_context_private_delete(void)
{
	// Mid-frame teardown guard (currently unreachable: delete only happens
	// between frames). A live encoder dropped without endEncoding trips a
	// Metal assert, and a frame_start that waited on gMetalInflight without a
	// matching commit/completed-handler would deadlock the semaphore — so
	// close the encoder and give the in-flight slot back before nil-ing out.
	if (gMetalEncoder != nil) { [gMetalEncoder endEncoding]; }
	if (gMetalCmd != nil && gMetalInflight != nil) { dispatch_semaphore_signal(gMetalInflight); }

	metal_texture_system_terminate();

	gMetalEncoder = nil; gMetalCmd = nil; gMetalDrawable = nil;

	// Per-encoder caches reference objects torn down below — drop them so a
	// deleted context can't pin GPU objects across a recreate.
	gMetalBoundPipeline   = nil;
	gMetalBoundTexture    = nil;
	gMetalBoundSampler    = nil;
	gMetalBoundDepthIndex = 0xFFFFFFFF;

	gMetalDepthTexture = nil;
	gMetalWhiteTexture = nil;
	for (int i = 0; i < MetalRing_Depth; i++) { gMetalRing[i] = nil; }
	for (int i = 0; i < MetalBlend_Count; i++) { gMetalPipelines[i] = nil; }
	for (int i = 0; i < 4; i++) { gMetalDepthStates[i] = nil; }
	// In-flight completed handlers hold their own captured semaphore refs, so
	// dropping ours here is safe even with frames still on the GPU.
	gMetalInflight = nil;

	gMetalLayer = nil; // drop the borrowed ref BEFORE destroying the owning view
	if (gView) { SDL_Metal_DestroyView(gView); gView = NULL; }
}
static void     metal_texture_reset_all(void) { }
static UUtError metal_private_state_new(void *s) { (void)s; return UUcError_None; }
static void     metal_private_state_delete(void *s) { (void)s; }

// Decode the Motoko draw state. Mirrors gl_private_state_update
// (gl_engine.c:299-512) minus the GL calls, minus fog (M2), with env-map
// modes collapsed to a base-only fallback until the M3 combiner lands.
static UUtError metal_private_state_update(
	void *in_state_private,
	UUtUns32 in_state_int_flags,
	const UUtInt32 *in_state_int,
	UUtInt32 in_state_ptr_flags,
	void **in_state_ptr)
{
	(void)in_state_private;

	// Retain the live arrays exactly like gl->state_int/state_ptr — the draw
	// entries read ScreenPointArray/ShadeArray/TextureCoordArray through these.
	gMetalStateInt = in_state_int;
	gMetalStatePtr = in_state_ptr;

	// Depth mode (gl_engine.c:360-366 -> gl_depth_mode_set semantics).
	if ((in_state_int_flags & (1 << M3cDrawStateIntType_ZCompare)) ||
		(in_state_int_flags & (1 << M3cDrawStateIntType_ZWrite)))
	{
		UUtUns32 idx = 0;
		if ((UUtBool)in_state_int[M3cDrawStateIntType_ZCompare] == M3cDrawState_ZCompare_On) { idx |= 1; }
		if ((UUtBool)in_state_int[M3cDrawStateIntType_ZWrite]   == M3cDrawState_ZWrite_On)   { idx |= 2; }
		gMetalDepthStateIndex = idx;
	}

	// Base texture selection (gl_engine.c:368-382).
	if ((in_state_int[M3cDrawStateIntType_Appearence] != M3cDrawState_Appearence_Gouraud) &&
		(in_state_int[M3cDrawStateIntType_Fill] == M3cDrawState_Fill_Solid) &&
		(in_state_ptr_flags & (1 << M3cDrawStatePtrType_BaseTextureMap)))
	{
		gMetalTexture0 = (M3tTextureMap*)in_state_ptr[M3cDrawStatePtrType_BaseTextureMap];
	}
	else
	{
		gMetalTexture0 = NULL;
	}

	// Constant colour + alpha (gl_engine.c:384-404). GL pushes these through
	// glColor4ub; we keep them CPU-side and bake them into vertices at submit.
	if (in_state_int_flags & (1 << M3cDrawStateIntType_Alpha)) {
		gMetalConstantA = (UUtUns8)in_state_int[M3cDrawStateIntType_Alpha];
	}
	if (in_state_int_flags & (1 << M3cDrawStateIntType_ConstantColor)) {
		gMetalConstantR = (UUtUns8)((in_state_int[M3cDrawStateIntType_ConstantColor] & 0x00FF0000) >> 16);
		gMetalConstantG = (UUtUns8)((in_state_int[M3cDrawStateIntType_ConstantColor] & 0x0000FF00) >> 8);
		gMetalConstantB = (UUtUns8) (in_state_int[M3cDrawStateIntType_ConstantColor] & 0x000000FF);
	}

	// Geometry submit mode (gl_engine.c:413-480, env branches collapsed).
	switch (in_state_int[M3cDrawStateIntType_Fill])
	{
		case M3cDrawState_Fill_Point:
		case M3cDrawState_Fill_Line:
			gMetalGeomMode = MetalGeom_Wireframe;
			break;
		case M3cDrawState_Fill_Solid:
		default:
			switch (in_state_int[M3cDrawStateIntType_Appearence])
			{
				case M3cDrawState_Appearence_Gouraud:
					gMetalGeomMode = MetalGeom_Gouraud;
					break;
				default: // the three textured appearances
					switch (in_state_int[M3cDrawStateIntType_VertexFormat])
					{
						case M3cDrawStateVertex_Split:
							gMetalGeomMode = MetalGeom_Split;
							break;
						case M3cDrawStateVertex_Unified:
						default:
							if (in_state_int[M3cDrawStateIntType_Interpolation] == M3cDrawState_Interpolation_None) {
								gMetalGeomMode = MetalGeom_Flat;
							}
							else if ((in_state_ptr[M3cDrawStatePtrType_EnvTextureMap] != NULL) &&
									 (gMetalConstantA == 0xFF)) {
								// GL would env-map here; M1 draws base-only
								// (Bungie's own low-quality fallback). M3 fixes.
								gMetalGeomMode = MetalGeom_EnvBaseFallback;
							}
							else {
								gMetalGeomMode = MetalGeom_Default;
							}
							break;
					}
					break;
			}
			break;
	}

	// Frame clear (gl_engine.c:494-507). ClearColor is ARGB.
	if (in_state_int_flags & (1 << M3cDrawStateIntType_BufferClear)) {
		gMetalBufferClear = (UUtBool)in_state_int[M3cDrawStateIntType_BufferClear];
	}
	if (in_state_int_flags & (1 << M3cDrawStateIntType_ClearColor)) {
		UUtUns32 cc = (UUtUns32)in_state_int[M3cDrawStateIntType_ClearColor];
		gMetalClearColor = MTLClearColorMake(
			((cc & 0x00FF0000) >> 16) / 255.0,
			((cc & 0x0000FF00) >>  8) / 255.0,
			((cc & 0x000000FF)      ) / 255.0,
			((cc & 0xFF000000) >> 24) / 255.0);
	}
	// DoubleBuffer intentionally ignored: Metal presents via drawable rotation
	// always; GL's single-buffer GL_FRONT path is a dev-only mode.

	return UUcError_None;
}

// ---- registration --------------------------------------------------------
UUtBool metal_is_available(void)
{
	if (gMetalDevice == nil) { gMetalDevice = MTLCreateSystemDefaultDevice(); }
	return (gMetalDevice != nil) ? UUcTrue : UUcFalse;
}

UUtBool metal_draw_engine_initialize(void)
{
	if (!metal_is_available()) {
		UUrError_Report(UUcError_Generic, "no Metal device present");
		return UUcFalse;
	}
	gMetalQueue = [gMetalDevice newCommandQueue];

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
	UUrStartupMessage("Metal draw engine registered (device: %s)", [[gMetalDevice name] UTF8String]);
	return UUcTrue;
}

void metal_draw_engine_terminate(void)
{
	metal_context_private_delete();
	gMetalQueue = nil; gMetalDevice = nil;
	UUrStartupMessage("Metal draw engine disposed");
}
