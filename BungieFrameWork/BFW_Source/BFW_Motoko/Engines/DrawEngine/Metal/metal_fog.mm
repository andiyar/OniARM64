// metal_fog.mm — Metal mirror of the GL backend's fog state (issue #43, M2).
// GL keeps fog colour/start/end in gl->fog_* and registers the gl_fog_* script
// variables in its init path (gl_utility.c:288-315). Under Metal that path never
// runs, so this file owns the equivalent state, registers the SAME-named script
// variables (only the selected engine registers per session — no collision),
// reproduces gl_reset_fog_parameters + the gl_fog_update ramp, and provides the
// engine-agnostic fog-factor query (Task 3 wires it into the vtable).
#include "metal_internal.h"

extern "C" {
#include "BFW_ScriptLang.h"   // SLrGlobalVariable_Register_Float, SLrScript_Command_Register_Void, SLt* types

// MSrTransform_PointListToFrustumScreen lives in the Software GeomEngine
// (MS_Geom_Transform.h); GL reaches it via a local extern (gl_utility.c:2227) to
// avoid pulling the engine header into the draw layer. Mirror that.
extern int MSrTransform_PointListToFrustumScreen(
	UUtUns32 inNumVertices, const M3tPoint3D *inPointList,
	M3tPoint4D *outFrustumPoints, M3tPointScreen *outScreenPoints,
	UUtUns8 *outClipCodeList);
}

// GL defaults (gl_engine.h:115-117): ONI_FOG_START 0.975, ONI_FOG_END 1.0,
// FOG_COLOR_GL 0.25 grey.
#define METAL_FOG_START_DEFAULT  0.975f
#define METAL_FOG_END_DEFAULT    1.0f
#define METAL_FOG_COLOR_DEFAULT  0.25f

// ---- state (mirrors gl->fog_* — gl_engine.h:726-732) -----------------------
float   gMetalFogStart   = METAL_FOG_START_DEFAULT;
float   gMetalFogEnd     = METAL_FOG_END_DEFAULT;
float   gMetalFogColorR  = METAL_FOG_COLOR_DEFAULT;
float   gMetalFogColorG  = METAL_FOG_COLOR_DEFAULT;
float   gMetalFogColorB  = METAL_FOG_COLOR_DEFAULT;
UUtBool gMetalFogEnabled = UUcTrue;   // GL inits fog_enabled = TRUE (gl_utility.c:344)

// Smooth-ramp state (gl->fog_*_desired/_delta/_changing). File-static: only the
// _changeto funcs and metal_fog_update touch these.
static float   gFogStartDesired, gFogStartDelta;
static UUtBool gFogStartChanging;
static float   gFogEndDesired,   gFogEndDelta;
static UUtBool gFogEndChanging;

// ---- script commands (mirror gl_fog_*_changeto_func, gl_utility.c:435-497) --
static UUtError metal_fog_start_changeto_func(
	SLtErrorContext *inErrorContext, UUtUns32 inParameterListLength,
	SLtParameter_Actual *inParameterList, UUtUns32 *outTicksTillCompletion,
	UUtBool *outStall, SLtParameter_Actual *ioReturnValue)
{
	int frames = (inParameterListLength >= 2) ? inParameterList[1].val.i : 0;
	float fog_val = inParameterList[0].val.f;
	if (frames <= 0) {
		gMetalFogStart = fog_val;
	} else {
		gFogStartDesired  = fog_val;
		gFogStartDelta    = (gFogStartDesired - gMetalFogStart) / frames;
		gFogStartChanging = UUcTrue;
	}
	return UUcError_None;
}

static UUtError metal_fog_end_changeto_func(
	SLtErrorContext *inErrorContext, UUtUns32 inParameterListLength,
	SLtParameter_Actual *inParameterList, UUtUns32 *outTicksTillCompletion,
	UUtBool *outStall, SLtParameter_Actual *ioReturnValue)
{
	int frames = (inParameterListLength >= 2) ? inParameterList[1].val.i : 0;
	float fog_val = inParameterList[0].val.f;
	if (frames <= 0) {
		gMetalFogEnd = fog_val;
	} else {
		gFogEndDesired  = fog_val;
		gFogEndDelta    = (gFogEndDesired - gMetalFogEnd) / frames;
		gFogEndChanging = UUcTrue;
	}
	return UUcError_None;
}

// ---- per-frame ramp (mirror gl_fog_update, gl_utility.c:499-571) ------------
void metal_fog_update(int inFrames)
{
	if (gFogStartChanging) {
		float delta = gFogStartDelta * inFrames;
		if (delta > 0) {
			if (gMetalFogStart < gFogStartDesired - delta) { gMetalFogStart += delta; }
			else { gMetalFogStart = gFogStartDesired; gFogStartChanging = UUcFalse; }
		} else if (delta < 0) {
			if (gMetalFogStart > gFogStartDesired - delta) { gMetalFogStart += delta; }
			else { gMetalFogStart = gFogStartDesired; gFogStartChanging = UUcFalse; }
		}
	}
	if (gFogEndChanging) {
		// PARITY: GL uses fog_start_delta here (gl_utility.c:538) — an upstream
		// bug. Mirrored deliberately so Metal fog-end ramps match GL exactly.
		float delta = gFogStartDelta * inFrames;
		if (delta > 0) {
			if (gMetalFogEnd < gFogEndDesired - delta) { gMetalFogEnd += delta; }
			else { gMetalFogEnd = gFogEndDesired; gFogEndChanging = UUcFalse; }
		} else if (delta < 0) {
			if (gMetalFogEnd > gFogEndDesired - delta) { gMetalFogEnd += delta; }
			else { gMetalFogEnd = gFogEndDesired; gFogEndChanging = UUcFalse; }
		}
	}
}

// ---- resetFog vtable impl (mirror gl_reset_fog_parameters, gl_utility.c:403) -
void metal_reset_fog(void)
{
	gMetalFogColorR = METAL_FOG_COLOR_DEFAULT;
	gMetalFogColorG = METAL_FOG_COLOR_DEFAULT;
	gMetalFogColorB = METAL_FOG_COLOR_DEFAULT;
	gMetalFogStart  = METAL_FOG_START_DEFAULT;
	gMetalFogEnd    = METAL_FOG_END_DEFAULT;
	gFogStartChanging = UUcFalse;
	gFogEndChanging   = UUcFalse;
	// Note: does not touch fog enable (per-batch state) — matches GL.
}

// ---- init: defaults + same-named script-var registration -------------------
void metal_fog_system_initialize(void)
{
	metal_reset_fog();
	gMetalFogEnabled = UUcTrue;

	// MUST keep the literal "gl_fog_*" names — 14+ level .bsl scripts set them.
	// Only the selected engine registers per session, so this does not collide
	// with the GL backend's identical registration.
	SLrGlobalVariable_Register_Float("gl_fog_end",   "fog end",   &gMetalFogEnd);
	SLrGlobalVariable_Register_Float("gl_fog_start", "fog start", &gMetalFogStart);
	SLrGlobalVariable_Register_Float("gl_fog_red",   "fog red",   &gMetalFogColorR);
	SLrGlobalVariable_Register_Float("gl_fog_green", "fog green", &gMetalFogColorG);
	SLrGlobalVariable_Register_Float("gl_fog_blue",  "fog blue",  &gMetalFogColorB);

	SLrScript_Command_Register_Void("gl_fog_start_changeto",
		"changes the fog start distance smoothly", "start_val:float [frames:int | ]",
		metal_fog_start_changeto_func);
	SLrScript_Command_Register_Void("gl_fog_end_changeto",
		"changes the fog end distance smoothly", "end_val:float [frames:int | ]",
		metal_fog_end_changeto_func);

	UUrStartupMessage("[Metal] fog system initialized (start=%.4f end=%.4f, gl_fog_* vars registered)",
		gMetalFogStart, gMetalFogEnd);
}

// ---- particle fog-factor query (mirror gl_calculate_fog_factor, gl_utility.c:2191) ----
// Reproduces GL's behaviour VERBATIM, including the internally-inconsistent
// clamp/interpolation. Task 3 reaches this via a new fogFactor DrawEngine vtable
// method + M3rDraw_GetFogFactor wrapper (no such slot exists yet).
// Deliberately omits GL's SHIPPING_VERSION==0 gl_fade_particles_by_fog debug
// toggle (gl_utility.c:2204-2213) — a dev-only switch, not shipping behaviour.
float metal_calculate_fog_factor(M3tPoint3D *inPoint)
{
	float fog_factor;

	if ((gMetalFogStart == gMetalFogEnd) ||
		(gMetalFogStart >= 1.f) ||
		(gMetalFogEnd <= 0.f)) {
		return 0.f;
	}

	// z must be in screen-space coordinates (same transform GL uses).
	static UUtUns8 screen_buf[sizeof(M3tPointScreen) + (2 * UUcProcessor_CacheLineSize)];
	static UUtUns8 frustum_buf[sizeof(M3tPoint4D) + (2 * UUcProcessor_CacheLineSize)];
	static UUtUns8 world_buf[sizeof(M3tPoint3D) + (2 * UUcProcessor_CacheLineSize)];
	static M3tPointScreen *screen_point = NULL;
	static M3tPoint4D     *frustum_point = NULL;
	static M3tPoint3D     *world_point = NULL;
	UUtUns8 clip_code;

	if (screen_point == NULL) {
		screen_point  = (M3tPointScreen *)UUrAlignMemory(screen_buf);
		frustum_point = (M3tPoint4D *)UUrAlignMemory(frustum_buf);
		world_point   = (M3tPoint3D *)UUrAlignMemory(world_buf);
	}
	*world_point = *inPoint;

	MSrTransform_PointListToFrustumScreen(1, world_point, frustum_point, screen_point, &clip_code);
	if (screen_point->z <= gMetalFogStart) {
		fog_factor = 0.f;
	} else if (screen_point->z >= gMetalFogEnd) {
		fog_factor = 1.f;
	} else {
		// PARITY: matches gl_utility.c:2262 verbatim (runs opposite to the clamps
		// above — upstream Bungie inconsistency, mirrored intentionally).
		fog_factor = (gMetalFogEnd - screen_point->z) / (gMetalFogEnd - gMetalFogStart);
	}
	UUmAssert((fog_factor >= 0.f) && (fog_factor <= 1.f));
	return fog_factor;
}
