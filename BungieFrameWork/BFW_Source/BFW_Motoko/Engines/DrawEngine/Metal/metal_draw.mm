// metal_draw.mm — M1 Task 3: the eight draw entries + texture/blend selection.
// Behavioural parity target: gl_engine.c:715-857 + gl_geometry_draw_method.c.
#include "metal_internal.h"

// ---- ring -------------------------------------------------------------------
MetalScreenVertex *metal_ring_reserve(UUtUns32 inCount, UUtUns32 *outFirstVertex)
{
	if (gMetalEncoder == nil) { return NULL; } // skipped frame (nil drawable)
	if (gMetalRingCursor + inCount > MetalRing_MaxVertices) {
		if (!gMetalRingOverflowed) {
			gMetalRingOverflowed = UUcTrue;
			UUrStartupMessage("[Metal] vertex ring FULL (%u) — dropping remainder of frame",
				(unsigned)MetalRing_MaxVertices);
		}
		return NULL;
	}
	*outFirstVertex = gMetalRingCursor;
	MetalScreenVertex *out = (MetalScreenVertex *)
		((char *)gMetalRing[gMetalRingIndex].contents
			+ (size_t)gMetalRingCursor * sizeof(MetalScreenVertex));
	gMetalRingCursor += inCount;
	return out;
}

MetalEnvVertex *metal_env_ring_reserve(UUtUns32 inCount, UUtUns32 *outFirstVertex)
{
	if (gMetalEncoder == nil) { return NULL; } // skipped frame (nil drawable)
	if (gMetalEnvRingCursor + inCount > MetalEnvRing_MaxVertices) {
		if (!gMetalEnvRingOverflowed) {
			gMetalEnvRingOverflowed = UUcTrue;
			UUrStartupMessage("[Metal] env ring FULL (%u) — dropping remainder of frame",
				(unsigned)MetalEnvRing_MaxVertices);
		}
		return NULL;
	}
	*outFirstVertex = gMetalEnvRingCursor;
	MetalEnvVertex *out = (MetalEnvVertex *)
		((char *)gMetalEnvRing[gMetalRingIndex].contents
			+ (size_t)gMetalEnvRingCursor * sizeof(MetalEnvVertex));
	gMetalEnvRingCursor += inCount;
	return out;
}

// ---- state selection (the gl_set_textures analogue) -------------------------
// inBlendOverride: -1 = derive from texture flags (gl's NONE,NONE case);
// else a MetalBlendMode forced by the caller (e.g. opaque for untextured).
UUtBool metal_select_textures(M3tTextureMap *inTexture0, int inBlendOverride)
{
	id<MTLTexture>      tex  = nil;
	id<MTLSamplerState> samp = nil;
	MetalBlendMode      blend;

	if (inTexture0 != NULL) {
		tex = metal_texture_lookup(inTexture0, &samp);
		if (tex == nil) { return UUcFalse; } // upload failed: skip primitive, never crash
	}

	if (inBlendOverride >= 0) {
		blend = (MetalBlendMode)inBlendOverride;
	} else if (inTexture0 == NULL) {
		blend = MetalBlend_Opaque;                       // gl: untextured forces (ONE, ZERO)
	} else if (inTexture0->flags & M3cTextureFlags_Blend_Additive) {
		blend = MetalBlend_Additive;                     // gl_utility.c:1837-1848
	} else {
		blend = MetalBlend_Alpha;                        // default transparency :1849-1854
	}

	// Resolve fallbacks BEFORE the cache compares so the cached values are
	// always the actually-bound objects.
	if (tex == nil)  { tex  = gMetalWhiteTexture; }
	if (samp == nil) { samp = metal_default_sampler(); } // repeat/repeat, no mip

	// Encoder-local caching (reset each frameStart).
	id<MTLRenderPipelineState> pso = gMetalPipelines[blend];
	if (pso != gMetalBoundPipeline) {
		[gMetalEncoder setRenderPipelineState:pso];
		gMetalBoundPipeline = pso;
	}
	// Ensure the main vertex ring is bound at buffer(0): an env draw may have
	// swapped buffer(0) to the env ring since the last normal draw.
	if (gMetalRing[gMetalRingIndex] != gMetalBoundVertexBuffer) {
		[gMetalEncoder setVertexBuffer:gMetalRing[gMetalRingIndex] offset:0 atIndex:0];
		gMetalBoundVertexBuffer = gMetalRing[gMetalRingIndex];
	}
	if (gMetalDepthStateIndex != gMetalBoundDepthIndex) {
		[gMetalEncoder setDepthStencilState:gMetalDepthStates[gMetalDepthStateIndex]];
		gMetalBoundDepthIndex = gMetalDepthStateIndex;
	}
	if (tex != gMetalBoundTexture) {
		[gMetalEncoder setFragmentTexture:tex atIndex:0];
		gMetalBoundTexture = tex;
	}
	if (samp != gMetalBoundSampler) {
		[gMetalEncoder setFragmentSamplerState:samp atIndex:0];
		gMetalBoundSampler = samp;
	}

	// Fog uniform (M2). Set per primitive: fog colour/range are frame-global but
	// fog-enable is per-batch, so the cheapest correct path is one setFragmentBytes
	// per draw. Batching is an M5 concern (correctness-first, per M1).
	{
		MetalFogUniform fogU;
		fogU.colorR = gMetalFogColorR;
		fogU.colorG = gMetalFogColorG;
		fogU.colorB = gMetalFogColorB;
		fogU.enabled = gMetalFogEnabled ? 1.0f : 0.0f;
		fogU.start = gMetalFogStart;
		fogU.end   = gMetalFogEnd;
		[gMetalEncoder setFragmentBytes:&fogU length:sizeof(fogU) atIndex:0];
	}
	return UUcTrue;
}

// Env-map state selection: env PSO + reflection(tex0)/base(tex1) + fog.
// Mirrors gl_set_textures(texture0=env, texture1=base) for the single-pass
// _geom_draw_mode_environment_map path (gl_geometry_draw_method.c:199-211).
UUtBool metal_select_env_textures(M3tTextureMap *inBase, M3tTextureMap *inEnv)
{
	if (inBase == NULL || inEnv == NULL) { return UUcFalse; }

	id<MTLSamplerState> sBase = nil, sEnv = nil;
	id<MTLTexture> tBase = metal_texture_lookup(inBase, &sBase);
	id<MTLTexture> tEnv  = metal_texture_lookup(inEnv,  &sEnv);
	if (tBase == nil || tEnv == nil) { return UUcFalse; } // upload failed: skip, never crash
	if (sBase == nil) { sBase = metal_default_sampler(); }
	if (sEnv  == nil) { sEnv  = metal_default_sampler(); }

	if (gMetalEnvPipeline != gMetalBoundPipeline) {
		[gMetalEncoder setRenderPipelineState:gMetalEnvPipeline];
		gMetalBoundPipeline = gMetalEnvPipeline;
	}
	if (gMetalDepthStateIndex != gMetalBoundDepthIndex) {
		[gMetalEncoder setDepthStencilState:gMetalDepthStates[gMetalDepthStateIndex]];
		gMetalBoundDepthIndex = gMetalDepthStateIndex;
	}
	[gMetalEncoder setFragmentTexture:tEnv  atIndex:0];
	[gMetalEncoder setFragmentTexture:tBase atIndex:1];
	[gMetalEncoder setFragmentSamplerState:sEnv  atIndex:0];
	[gMetalEncoder setFragmentSamplerState:sBase atIndex:1];
	// The single-texture cache tracks slot 0 only; we just wrote both slots, so
	// force the next normal draw to rebind its texture0/sampler0.
	gMetalBoundTexture = nil;
	gMetalBoundSampler = nil;

	{
		MetalFogUniform fogU;
		fogU.colorR  = gMetalFogColorR;
		fogU.colorG  = gMetalFogColorG;
		fogU.colorB  = gMetalFogColorB;
		fogU.enabled = gMetalFogEnabled ? 1.0f : 0.0f;
		fogU.start   = gMetalFogStart;
		fogU.end     = gMetalFogEnd;
		[gMetalEncoder setFragmentBytes:&fogU length:sizeof(fogU) atIndex:0];
	}
	return UUcTrue;
}

// ---- geometry (triangle/quad/pent) -------------------------------------------
// Replaces GL's macro-template gl_geometry_draw_method.c with one shared
// submitter. The unified polygons are M3tTri/Quad/Pent {UUtUns32 indices[N]};
// the Split variants are {vertexIndices; baseUVIndices; shades[N]} with
// vertexIndices FIRST (BFW_Motoko.h:1226-1245) — so a const UUtUns32* view of
// the struct aliases the vertex indices for both layouts (the same trick GL's
// function-pointer cast relies on).
static void metal_submit_polygon(const UUtUns32 *in_indices, UUtUns32 inN, const void *in_geom)
{
	const M3tPointScreen  *screen_points =
		(const M3tPointScreen *)gMetalStatePtr[M3cDrawStatePtrType_ScreenPointArray];
	const UUtUns32        *vertex_shades =
		(const UUtUns32 *)gMetalStatePtr[M3cDrawStatePtrType_ScreenShadeArray_DC];
	const M3tTextureCoord *base_uvs =
		(const M3tTextureCoord *)gMetalStatePtr[M3cDrawStatePtrType_TextureCoordArray];

	MetalGeomMode mode = gMetalGeomMode;
	M3tTextureMap *texture = gMetalTexture0;
	UUtBool textured, per_vertex_shade;
	const UUtUns32 *uv_indices = in_indices;
	const UUtUns32 *split_shades = NULL;             // indexed 0..N-1 when Split

	switch (mode)
	{
		case MetalGeom_Gouraud:
			textured = UUcFalse; per_vertex_shade = UUcTrue;
			break;
		case MetalGeom_Flat:
			textured = UUcTrue;  per_vertex_shade = UUcFalse;
			break;
		case MetalGeom_Split:
		{
			// Split layout: vertexIndices[N], baseUVIndices[N], shades[N].
			textured = UUcTrue; per_vertex_shade = UUcTrue;
			const UUtUns32 *raw = (const UUtUns32 *)in_geom;
			uv_indices   = raw + inN;        // baseUVIndices
			split_shades = raw + 2 * inN;    // shades
			break;
		}
		case MetalGeom_EnvBaseFallback: // env-mapped: base-only fallback (M3 does the combine)
		case MetalGeom_Default:
		default:
			textured = UUcTrue; per_vertex_shade = UUcTrue;
			break;
		case MetalGeom_EnvMap:
			// Handled by the dedicated branch below; these flags are unused for it.
			textured = UUcTrue; per_vertex_shade = UUcTrue;
			break;
	}

	if (mode == MetalGeom_Wireframe) {
		// gl: GL_LINE_STRIP over the outline, untextured, constant colour.
		UUtUns32 first;
		if (!metal_select_textures(NULL, MetalBlend_Opaque)) { return; }
		MetalScreenVertex *v = metal_ring_reserve(inN + 1, &first);
		if (v == NULL) { return; }
		for (UUtUns32 i = 0; i <= inN; i++) {
			const M3tPointScreen *p = screen_points + in_indices[i % inN];
			v[i].x = p->x; v[i].y = p->y; v[i].z = p->z;
			v[i].u = 0.0f; v[i].v = 0.0f; v[i].w = 1.0f;
			v[i].r = gMetalConstantR; v[i].g = gMetalConstantG;
			v[i].b = gMetalConstantB; v[i].a = gMetalConstantA;
		}
		[gMetalEncoder drawPrimitives:MTLPrimitiveTypeLineStrip
			vertexStart:first vertexCount:inN + 1];
		return;
	}

	if (mode == MetalGeom_EnvMap) {
		// Single-pass reflection combine. Two UV sets per vertex: base (this
		// array) + env (EnvTextureCoordArray), both indexed by the geometry's
		// vertex indices, exactly like SUBMIT_BASE_QRST1 / SUBMIT_ENV_QRST0
		// (gl_geometry_draw_method.c:199-209). Env-map is unified-only (the
		// decode never selects it for split geometry), so uv index == vertex index.
		const M3tTextureCoord *env_uvs =
			(const M3tTextureCoord *)gMetalStatePtr[M3cDrawStatePtrType_EnvTextureCoordArray];
		if (env_uvs == NULL) { return; } // defensive: no env UVs => nothing sane to draw

		if (!metal_select_env_textures(gMetalTexture0 /*base*/, gMetalEnvTexture /*reflection*/)) {
			return;
		}
		// Bind the env ring at buffer(0) for this draw (normal draws rebind the
		// main ring via the guard in metal_select_textures).
		if (gMetalEnvRing[gMetalRingIndex] != gMetalBoundVertexBuffer) {
			[gMetalEncoder setVertexBuffer:gMetalEnvRing[gMetalRingIndex] offset:0 atIndex:0];
			gMetalBoundVertexBuffer = gMetalEnvRing[gMetalRingIndex];
		}

		UUtUns32 tri_count = inN - 2;
		UUtUns32 first;
		MetalEnvVertex *v = metal_env_ring_reserve(tri_count * 3, &first);
		if (v == NULL) { return; }

		for (UUtUns32 t = 0; t < tri_count; t++) {
			const UUtUns32 corner[3] = { 0, t + 1, t + 2 };
			for (UUtUns32 c = 0; c < 3; c++, v++) {
				UUtUns32 i = corner[c];
				const M3tPointScreen *p = screen_points + in_indices[i];
				float oow = p->invW;
				const M3tTextureCoord *buv = base_uvs + in_indices[i];
				const M3tTextureCoord *euv = env_uvs  + in_indices[i];

				v->x = p->x; v->y = p->y; v->z = p->z;
				v->bu = buv->u * oow; v->bv = buv->v * oow; v->bw = oow;
				v->eu = euv->u * oow; v->ev = euv->v * oow; v->ew = oow;

				UUtUns32 shade = vertex_shades[in_indices[i]];
				v->r = (UUtUns8)((shade & 0x00FF0000) >> 16);
				v->g = (UUtUns8)((shade & 0x0000FF00) >> 8);
				v->b = (UUtUns8) (shade & 0x000000FF);
				v->a = gMetalConstantA;
			}
		}
		[gMetalEncoder drawPrimitives:MTLPrimitiveTypeTriangle
			vertexStart:first vertexCount:tri_count * 3];
		return;
	}

	{
		// Blend: flag-derived for the normal textured modes (gl passes NONE,NONE);
		// forced opaque for untextured AND for the env-map base fallback — GL's
		// low-quality env path is explicitly gl_set_textures(base, NULL, GL_ONE,
		// GL_ZERO) (gl_geometry_draw_method.c:240-243).
		int blend_override = (!textured || mode == MetalGeom_EnvBaseFallback)
			? (int)MetalBlend_Opaque : -1;
		if (!metal_select_textures(textured ? texture : NULL, blend_override)) {
			return;
		}
	}

	// Triangulate as a fan: (0, i, i+1) — matches GL_TRIANGLE_FAN/GL_POLYGON
	// submission order for quads and pents.
	UUtUns32 tri_count = inN - 2;
	UUtUns32 first;
	MetalScreenVertex *v = metal_ring_reserve(tri_count * 3, &first);
	if (v == NULL) { return; }

	for (UUtUns32 t = 0; t < tri_count; t++) {
		const UUtUns32 corner[3] = { 0, t + 1, t + 2 };
		for (UUtUns32 c = 0; c < 3; c++, v++) {
			UUtUns32 i = corner[c];
			const M3tPointScreen *p = screen_points + in_indices[i];
			float oow = p->invW;

			v->x = p->x; v->y = p->y; v->z = p->z;
			if (textured) {
				const M3tTextureCoord *uv = base_uvs + uv_indices[i];
				v->u = uv->u * oow; v->v = uv->v * oow; v->w = oow;
			} else {
				v->u = 0.0f; v->v = 0.0f; v->w = 1.0f;
			}
			if (per_vertex_shade) {
				UUtUns32 shade = split_shades ? split_shades[i] : vertex_shades[in_indices[i]];
				v->r = (UUtUns8)((shade & 0x00FF0000) >> 16);
				v->g = (UUtUns8)((shade & 0x0000FF00) >> 8);
				v->b = (UUtUns8) (shade & 0x000000FF);
				v->a = gMetalConstantA;                       // SUBMIT_COLOR: constant alpha
			} else {
				v->r = gMetalConstantR; v->g = gMetalConstantG;  // flat: constant colour
				v->b = gMetalConstantB; v->a = gMetalConstantA;
			}
		}
	}
	[gMetalEncoder drawPrimitives:MTLPrimitiveTypeTriangle
		vertexStart:first vertexCount:tri_count * 3];
}

static void metal_triangle(void *inTriangle) { metal_submit_polygon((const UUtUns32 *)inTriangle, 3, inTriangle); }
static void metal_quad(void *inQuad)         { metal_submit_polygon((const UUtUns32 *)inQuad,     4, inQuad); }
static void metal_pent(void *inPent)         { metal_submit_polygon((const UUtUns32 *)inPent,     5, inPent); }

// ---- sprites, lines, points --------------------------------------------------

// gl_sprite (gl_engine.c:763-784): corners TL,(x1,y0),BR,(x0,y1), UVs [0][1][3][2],
// flat z from points[0], no perspective UVs (w=1), colour = constant colour.
static void metal_sprite(const M3tPointScreen *in_points, const M3tTextureCoord *in_uvs)
{
	UUtUns32 first;
	if (!metal_select_textures(gMetalTexture0, -1)) { return; }
	MetalScreenVertex *v = metal_ring_reserve(6, &first);
	if (v == NULL) { return; }

	const float x0 = in_points[0].x, y0 = in_points[0].y;
	const float x1 = in_points[1].x, y1 = in_points[1].y;
	const float z  = in_points[0].z;

	const float px[4] = { x0, x1, x1, x0 };
	const float py[4] = { y0, y0, y1, y1 };
	const M3tTextureCoord quv[4] = { in_uvs[0], in_uvs[1], in_uvs[3], in_uvs[2] };

	static const UUtUns32 kFan[6] = { 0, 1, 2, 0, 2, 3 };
	for (UUtUns32 i = 0; i < 6; i++, v++) {
		UUtUns32 c = kFan[i];
		v->x = px[c]; v->y = py[c]; v->z = z;
		v->u = quv[c].u; v->v = quv[c].v; v->w = 1.0f;
		v->r = gMetalConstantR; v->g = gMetalConstantG;
		v->b = gMetalConstantB; v->a = gMetalConstantA;
	}
	[gMetalEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:first vertexCount:6];
}

// gl_trisprite (gl_engine.c:742-761): 3 points, UVs 1:1, z = points[0].z everywhere.
static void metal_tri_sprite(const M3tPointScreen *in_points, const M3tTextureCoord *in_uvs)
{
	UUtUns32 first;
	if (!metal_select_textures(gMetalTexture0, -1)) { return; }
	MetalScreenVertex *v = metal_ring_reserve(3, &first);
	if (v == NULL) { return; }
	for (UUtUns32 i = 0; i < 3; i++, v++) {
		v->x = in_points[i].x; v->y = in_points[i].y; v->z = in_points[0].z;
		v->u = in_uvs[i].u; v->v = in_uvs[i].v; v->w = 1.0f;
		v->r = gMetalConstantR; v->g = gMetalConstantG;
		v->b = gMetalConstantB; v->a = gMetalConstantA;
	}
	[gMetalEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:first vertexCount:3];
}

// gl_sprite_array (gl_engine.c:786-816): per-element 2-point rect + 4 UVs.
// NOTE: GL *ignores* in_colors (the parameter is unused in its body) — vertex
// colour stays the constant colour. Mirrored deliberately for parity.
static void metal_sprite_array(const M3tPointScreen *in_points, const M3tTextureCoord *in_uvs,
	const UUtUns32 *in_colors, const UUtUns32 in_count)
{
	(void)in_colors;
	for (UUtUns32 i = 0; i < in_count; i++) {
		// Each element's z comes from in_points[0] — GL uses ZCOORD(in_points[0].z)
		// for every element of the array, not element i's own z. Mirrored.
		M3tPointScreen pts[2] = { in_points[i * 2 + 0], in_points[i * 2 + 1] };
		pts[0].z = in_points[0].z;
		pts[1].z = in_points[0].z;
		metal_sprite(pts, in_uvs + i * 4);
	}
}

// gl_line / gl_point (gl_engine.c:818-856): untextured, (ONE, ZERO), constant colour.
static void metal_line(UUtUns32 inVIndex0, UUtUns32 inVIndex1)
{
	const M3tPointScreen *screen_points =
		(const M3tPointScreen *)gMetalStatePtr[M3cDrawStatePtrType_ScreenPointArray];
	UUtUns32 first;
	if (!metal_select_textures(NULL, MetalBlend_Opaque)) { return; }
	MetalScreenVertex *v = metal_ring_reserve(2, &first);
	if (v == NULL) { return; }
	const M3tPointScreen *pts[2] = { screen_points + inVIndex0, screen_points + inVIndex1 };
	for (UUtUns32 i = 0; i < 2; i++, v++) {
		v->x = pts[i]->x; v->y = pts[i]->y; v->z = pts[i]->z;
		v->u = 0.0f; v->v = 0.0f; v->w = 1.0f;
		v->r = gMetalConstantR; v->g = gMetalConstantG;
		v->b = gMetalConstantB; v->a = gMetalConstantA;
	}
	[gMetalEncoder drawPrimitives:MTLPrimitiveTypeLine vertexStart:first vertexCount:2];
}

static void metal_point(M3tPointScreen *inCoord)
{
	UUtUns32 first;
	if (!metal_select_textures(NULL, MetalBlend_Opaque)) { return; }
	MetalScreenVertex *v = metal_ring_reserve(1, &first);
	if (v == NULL) { return; }
	v->x = inCoord->x; v->y = inCoord->y; v->z = inCoord->z;
	v->u = 0.0f; v->v = 0.0f; v->w = 1.0f;
	v->r = gMetalConstantR; v->g = gMetalConstantG;
	v->b = gMetalConstantB; v->a = gMetalConstantA;
	[gMetalEncoder drawPrimitives:MTLPrimitiveTypePoint vertexStart:first vertexCount:1];
}

// ---- vtable install ---------------------------------------------------------
void metal_draw_install_methods(M3tDrawContextMethods *ioMethods)
{
	ioMethods->triangle    = metal_triangle;
	ioMethods->quad        = metal_quad;
	ioMethods->pent        = metal_pent;
	ioMethods->line        = metal_line;
	ioMethods->point       = metal_point;
	ioMethods->triSprite   = metal_tri_sprite;
	ioMethods->sprite      = metal_sprite;
	ioMethods->spriteArray = metal_sprite_array;
}
