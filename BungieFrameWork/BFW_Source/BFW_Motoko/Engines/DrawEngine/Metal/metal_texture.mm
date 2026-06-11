// metal_texture.mm — Metal backend texture system (M1 Task 2, issue #43).
// Mirror of gl_texture_map_create / gl_texture_map_delete /
// gl_texture_map_proc_handler (gl_utility.c:961-1151, 1683-1768), minus GL's
// LRU texture-memory purging — deliberately not ported (unified memory;
// Oni-era texture totals are tiny).

#include "metal_internal.h"

extern "C" {
#include "BFW_TemplateManager.h"
#include "BFW_Image.h"
// Graphics-quality gates, same ones the GL upload consults (Oni_Motoko.h:63-65).
extern UUtBool ONrMotoko_GraphicsQuality_SupportTrilinear(void);
extern UUtBool ONrMotoko_GraphicsQuality_SupportHighQualityTextures(void);
}

// ---- texture table ---------------------------------------------------------
// opengl_texture_name holds (table index + 1); 0 = not loaded. opengl_dirty
// keeps its GL meaning: needs (re)upload. The shared M3tTextureMap struct is
// not modified (spec §5.5).
enum { MetalTexture_InitialCapacity = 1024 };

static NSMutableArray<id<MTLTexture>> *gTextureTable;   // index -> texture (NSNull when free)
static NSMutableArray<id<MTLSamplerState>> *gTextureSamplers; // parallel: sampler per texture
static NSMutableIndexSet               *gFreeIndices;
static id<MTLSamplerState>              gSamplerCache[2][2][2]; // [clampS][clampT][mip]
static TMtPrivateData                  *gTexturePrivateData;
static void                            *gConvertBuffer;  // 256*256*4 like gl->converted_data_buffer

static id<MTLSamplerState> metal_sampler_for(UUtBool inClampS, UUtBool inClampT, UUtBool inMip)
{
	__strong id<MTLSamplerState> *slot = &gSamplerCache[inClampS ? 1 : 0][inClampT ? 1 : 0][inMip ? 1 : 0];
	if (*slot == nil) {
		MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
		sd.minFilter    = MTLSamplerMinMagFilterLinear; // GL_LINEAR both ways
		sd.magFilter    = MTLSamplerMinMagFilterLinear;
		sd.mipFilter    = inMip
			? (ONrMotoko_GraphicsQuality_SupportTrilinear()
				? MTLSamplerMipFilterLinear      // GL_LINEAR_MIPMAP_LINEAR
				: MTLSamplerMipFilterNearest)    // GL_LINEAR_MIPMAP_NEAREST
			: MTLSamplerMipFilterNotMipmapped;
		sd.sAddressMode = inClampS ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
		sd.tAddressMode = inClampT ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
		*slot = [gMetalDevice newSamplerStateWithDescriptor:sd];
	}
	return *slot;
}

id<MTLSamplerState> metal_default_sampler(void)
{
	return metal_sampler_for(UUcFalse, UUcFalse, UUcFalse);
}

// Mirrors gl_texture_map_proc_handler (gl_utility.c:1733-1768). Without the
// Update message marking textures dirty, the text-system glyph cache (a
// runtime A8 texture redrawn via TMrInstance_Update) would never re-upload.
static UUtError metal_texture_map_proc_handler(
	TMtTemplateProc_Message message,
	void *instance_ptr,
	void *private_data)
{
	M3tTextureMap *texture_map = (M3tTextureMap *)instance_ptr;
	(void)private_data;

	switch (message)
	{
		case TMcTemplateProcMessage_NewPostProcess:
			texture_map->flags |= M3cTextureFlags_Offscreen;
			texture_map->debugName[0] = '\0';
			texture_map->opengl_texture_name = 0;
			texture_map->opengl_dirty = UUcTrue;
			break;
		case TMcTemplateProcMessage_LoadPostProcess:
			M3rTextureMap_Prepare(texture_map);
			texture_map->opengl_texture_name = 0;
			texture_map->opengl_dirty = UUcTrue;
			break;
		case TMcTemplateProcMessage_DisposePreProcess:
			metal_texture_map_delete(texture_map);
			break;
		case TMcTemplateProcMessage_Update:
			texture_map->opengl_dirty = UUcTrue;
			break;
		default:
			break;
	}
	return UUcError_None;
}

UUtError metal_texture_system_initialize(void)
{
	if (gTexturePrivateData != NULL) { return UUcError_None; } // context re-create (GL: gl_engine.c:202 NULL guard)

	gTextureTable    = [[NSMutableArray alloc] initWithCapacity:MetalTexture_InitialCapacity];
	gTextureSamplers = [[NSMutableArray alloc] initWithCapacity:MetalTexture_InitialCapacity];
	gFreeIndices     = [[NSMutableIndexSet alloc] init];
	gConvertBuffer   = UUrMemory_Block_New(256 * 256 * sizeof(UUtUns32)); // M3cTextureMap_MaxWidth^2 RGBA8
	UUmError_ReturnOnNull(gConvertBuffer);

	// Same registration GL performs at context create (gl_engine.c:202-206).
	return TMrTemplate_PrivateData_New(M3cTemplate_TextureMap, 0,
		metal_texture_map_proc_handler, &gTexturePrivateData);
}

void metal_texture_system_terminate(void)
{
	if (gTexturePrivateData) {
		TMrTemplate_PrivateData_Delete(gTexturePrivateData);
		gTexturePrivateData = NULL;
	}
	if (gConvertBuffer) { UUrMemory_Block_Delete(gConvertBuffer); gConvertBuffer = NULL; }
	gTextureTable = nil; gTextureSamplers = nil; gFreeIndices = nil;
	for (int s = 0; s < 2; s++) {
		for (int t = 0; t < 2; t++) {
			for (int m = 0; m < 2; m++) {
				gSamplerCache[s][t][m] = nil; // ARC: plain assignment, never memset ObjC pointers
			}
		}
	}
}

// ---- upload ----------------------------------------------------------------
// IMrImage_ConvertPixelType dispatches through a SPARSE [dst][src] proc table
// (BFW_Image_PixelConversion.c:2608-2670). dst=RGBA_Bytes exists for src in
// {A8, ARGB4444, RGB555, ARGB1555, DXT1}; dst=RGB_Bytes for src in {I8,
// RGB555, DXT1, RGB888, RGB_Bytes(copy)}. There is NO conversion FROM
// RGB565 / RGBA5551 / RGBA4444 — routing them through IMrImage would hit the
// unsupplied-proc assert, so they get a hand-rolled unpack instead.
typedef enum MetalUploadKind
{
	MetalUpload_BGRA8_Native,    // ARGB8888 little-endian bytes are exactly BGRA8
	MetalUpload_RGBA8_Native,    // RGBA_Bytes
	MetalUpload_BC1_Native,      // DXT1 (Apple Silicon supports BC at the macOS 15 floor)
	MetalUpload_Convert_RGBA,    // IMrImage_ConvertPixelType -> RGBA_Bytes
	MetalUpload_Convert_RGB,     // IMrImage_ConvertPixelType -> RGB_Bytes, pad alpha=FF
	MetalUpload_Expand_A8,       // (FF,FF,FF,a)
	MetalUpload_Expand_I8,       // (l,l,l,FF)
	MetalUpload_Expand_A4I4,     // low nibble intensity, high nibble alpha
	MetalUpload_Expand_16,       // hand-rolled 16-bit unpack (no IMrImage entry exists)
	MetalUpload_Unsupported
} MetalUploadKind;

static MetalUploadKind metal_upload_kind(IMtPixelType inType)
{
	switch (inType)
	{
		case IMcPixelType_ARGB8888:   return MetalUpload_BGRA8_Native;
		case IMcPixelType_RGBA_Bytes: return MetalUpload_RGBA8_Native;
		case IMcPixelType_DXT1:
			return [gMetalDevice supportsBCTextureCompression]
				? MetalUpload_BC1_Native : MetalUpload_Convert_RGBA; // DXT1->RGBA_Bytes exists
		// A8 must expand to white+alpha: Metal's A8Unorm samples RGB=0, but GL
		// forced the RGB bias to 1.0 — without this, menu text renders black.
		case IMcPixelType_A8:         return MetalUpload_Expand_A8;
		case IMcPixelType_I8:         return MetalUpload_Expand_I8;
		case IMcPixelType_A4I4:       return MetalUpload_Expand_A4I4;
		// 16-bit types WITH a proven IMrImage path (GL's packed fallback used these):
		case IMcPixelType_ARGB4444:
		case IMcPixelType_ARGB1555:
		case IMcPixelType_RGB555:    return MetalUpload_Convert_RGBA;
		// 16-bit types WITHOUT any IMrImage entry — hand unpack:
		case IMcPixelType_RGB565:
		case IMcPixelType_RGBA5551:
		case IMcPixelType_RGBA4444:  return MetalUpload_Expand_16;
		// 24-bit RGB triples: RGB888->RGB_Bytes and RGB_Bytes->RGB_Bytes(copy) exist:
		case IMcPixelType_RGB888:
		case IMcPixelType_RGB_Bytes: return MetalUpload_Convert_RGB;
		default:                     return MetalUpload_Unsupported; // I1, ABGR1555
	}
}

// Upload one mip level into 'tex'. 'src' points at the level's source texels.
static UUtBool metal_upload_level(
	id<MTLTexture> tex, MetalUploadKind kind, IMtPixelType src_type,
	const void *src, UUtUns32 level, UUtUns32 width, UUtUns32 height)
{
	switch (kind)
	{
		case MetalUpload_BGRA8_Native:
		case MetalUpload_RGBA8_Native:
			[tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:level
				withBytes:src bytesPerRow:(NSUInteger)width * 4];
			return UUcTrue;

		case MetalUpload_BC1_Native:
		{
			// BC1: 8 bytes per 4x4 block; rows of blocks.
			NSUInteger blocks_w = (width  + 3) / 4;
			[tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:level
				withBytes:src bytesPerRow:blocks_w * 8];
			return UUcTrue;
		}

		case MetalUpload_Convert_RGBA:
		case MetalUpload_Convert_RGB:
		{
			IMtPixelType dst_type = (kind == MetalUpload_Convert_RGBA)
				? IMcPixelType_RGBA_Bytes : IMcPixelType_RGB_Bytes;
			UUtError error = IMrImage_ConvertPixelType(IMcDitherMode_Off,
				(UUtUns16)width, (UUtUns16)height, IMcNoMipMap,
				src_type, (void *)src, dst_type, gConvertBuffer);
			if (error != UUcError_None) { return UUcFalse; }

			if (kind == MetalUpload_Convert_RGB) {
				// Pad 3-byte RGB to RGBA8 in place, back to front (no overlap).
				const UUtUns8 *rgb  = (const UUtUns8 *)gConvertBuffer;
				UUtUns8       *rgba = (UUtUns8 *)gConvertBuffer;
				for (UUtInt64 i = (UUtInt64)width * height - 1; i >= 0; i--) {
					rgba[i*4+3] = 0xFF;
					rgba[i*4+2] = rgb[i*3+2];
					rgba[i*4+1] = rgb[i*3+1];
					rgba[i*4+0] = rgb[i*3+0];
				}
			}
			[tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:level
				withBytes:gConvertBuffer bytesPerRow:(NSUInteger)width * 4];
			return UUcTrue;
		}

		case MetalUpload_Expand_A8:
		case MetalUpload_Expand_I8:
		case MetalUpload_Expand_A4I4:
		{
			const UUtUns8 *s = (const UUtUns8 *)src;
			UUtUns8 *d = (UUtUns8 *)gConvertBuffer;
			UUtUns32 n = width * height;
			for (UUtUns32 i = 0; i < n; i++) {
				UUtUns8 px = s[i];
				switch (kind) {
					case MetalUpload_Expand_A8:   // white + alpha (GL bias trick)
						d[i*4+0] = 0xFF; d[i*4+1] = 0xFF; d[i*4+2] = 0xFF; d[i*4+3] = px;
						break;
					case MetalUpload_Expand_I8:   // luminance, opaque
						d[i*4+0] = px; d[i*4+1] = px; d[i*4+2] = px; d[i*4+3] = 0xFF;
						break;
					default: {                   // A4I4: high nibble alpha, low intensity
						UUtUns8 in4 = (UUtUns8)(px & 0x0F), a4 = (UUtUns8)(px >> 4);
						UUtUns8 l = (UUtUns8)((in4 << 4) | in4), a = (UUtUns8)((a4 << 4) | a4);
						d[i*4+0] = l; d[i*4+1] = l; d[i*4+2] = l; d[i*4+3] = a;
						break;
					}
				}
			}
			[tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:level
				withBytes:gConvertBuffer bytesPerRow:(NSUInteger)width * 4];
			return UUcTrue;
		}

		case MetalUpload_Expand_16:
		{
			// No IMrImage entry exists for these. Bit layouts verified against
			// IMiConvert_ARGB1555_to_RGBA_5551 / _ARGB4444_to_RGBA_4444
			// (BFW_Image_PixelConversion.c:2491,2522): a little-endian 16-bit
			// word with fields named MSB-first (RGB565: R 15:11, G 10:5, B 4:0;
			// RGBA5551 = ARGB1555 << 1 with A at bit 0; RGBA4444 = ARGB4444 << 4
			// with A at bits 3:0).
			const UUtUns16 *s = (const UUtUns16 *)src;
			UUtUns8 *d = (UUtUns8 *)gConvertBuffer;
			UUtUns32 n = width * height;
			for (UUtUns32 i = 0; i < n; i++) {
				UUtUns16 px = s[i];
				UUtUns8 r, g, b, a;
				switch (src_type) {
					case IMcPixelType_RGB565:
						r = (UUtUns8)((px >> 11) & 0x1F); r = (UUtUns8)((r << 3) | (r >> 2));
						g = (UUtUns8)((px >>  5) & 0x3F); g = (UUtUns8)((g << 2) | (g >> 4));
						b = (UUtUns8)( px        & 0x1F); b = (UUtUns8)((b << 3) | (b >> 2));
						a = 0xFF;
						break;
					case IMcPixelType_RGBA5551:   // R 15:11, G 10:6, B 5:1, A 0
						r = (UUtUns8)((px >> 11) & 0x1F); r = (UUtUns8)((r << 3) | (r >> 2));
						g = (UUtUns8)((px >>  6) & 0x1F); g = (UUtUns8)((g << 3) | (g >> 2));
						b = (UUtUns8)((px >>  1) & 0x1F); b = (UUtUns8)((b << 3) | (b >> 2));
						a = (px & 0x0001) ? 0xFF : 0x00;
						break;
					default:                      // IMcPixelType_RGBA4444: R 15:12 ... A 3:0
						r = (UUtUns8)((px >> 12) & 0x0F); r = (UUtUns8)((r << 4) | r);
						g = (UUtUns8)((px >>  8) & 0x0F); g = (UUtUns8)((g << 4) | g);
						b = (UUtUns8)((px >>  4) & 0x0F); b = (UUtUns8)((b << 4) | b);
						a = (UUtUns8)( px        & 0x0F); a = (UUtUns8)((a << 4) | a);
						break;
				}
				d[i*4+0] = r; d[i*4+1] = g; d[i*4+2] = b; d[i*4+3] = a;
			}
			[tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:level
				withBytes:gConvertBuffer bytesPerRow:(NSUInteger)width * 4];
			return UUcTrue;
		}

		default:
			return UUcFalse;
	}
}

// ---- create / delete / lookup ----------------------------------------------
// Mirror of gl_texture_map_create (gl_utility.c:961-1151): TemporarilyLoad
// materializes ->pixels for separate-data textures, the mip chain advances by
// IMrImage_ComputeSize per level halving dims with a min of 1, and we finish
// with UnloadTemporary. With skipped large lods, ->pixels still points at the
// buffer BASE (data lands at +skip_data_size, Motoko_Texture.c:200-211), so
// walking base forward over the skipped levels is correct.
UUtBool metal_texture_map_create(M3tTextureMap *texture_map)
{
	UUmAssert(texture_map);

	MetalUploadKind kind = metal_upload_kind(texture_map->texelType);
	if (kind == MetalUpload_Unsupported) {
		UUrStartupMessage("[Metal] unsupported texel type %d on '%s'",
			texture_map->texelType, texture_map->debugName);
		return UUcFalse;
	}

	UUtBool mipmap = (texture_map->flags & M3cTextureFlags_HasMipMap) ? UUcTrue : UUcFalse;
	UUtUns32 disable_large_lods = 0;

	if (mipmap && !ONrMotoko_GraphicsQuality_SupportHighQualityTextures()) {
		disable_large_lods = 1; // lod 0 skipped on low quality (gl_utility.c:1035-1038)
	}

	// Materialize ->pixels (separate-data textures load on demand).
	M3rTextureMap_TemporarilyLoad(texture_map, disable_large_lods);
	if (texture_map->pixels == NULL) {
		M3rTextureMap_UnloadTemporary(texture_map);
		return UUcFalse;
	}

	// Count the levels we'll upload + find the top-level dims after lod skip.
	UUtUns32 top_w = texture_map->width, top_h = texture_map->height;
	const void *base = texture_map->pixels;
	UUtUns32 skip = disable_large_lods;
	while (skip > 0 && top_w > 1 && top_h > 1) {
		base = (const char *)base +
			IMrImage_ComputeSize(texture_map->texelType, IMcNoMipMap, (UUtUns16)top_w, (UUtUns16)top_h);
		top_w = UUmMax(top_w >> 1, 1);
		top_h = UUmMax(top_h >> 1, 1);
		skip--;
	}
	UUtUns32 levels = 1;
	if (mipmap) {
		UUtUns32 w = top_w, h = top_h;
		while (w > 1 || h > 1) { w = UUmMax(w >> 1, 1); h = UUmMax(h >> 1, 1); levels++; }
	}

	MTLPixelFormat fmt =
		(kind == MetalUpload_BGRA8_Native) ? MTLPixelFormatBGRA8Unorm :
		(kind == MetalUpload_BC1_Native)   ? MTLPixelFormatBC1_RGBA   :
		                                     MTLPixelFormatRGBA8Unorm;

	MTLTextureDescriptor *td = [MTLTextureDescriptor
		texture2DDescriptorWithPixelFormat:fmt width:top_w height:top_h mipmapped:(levels > 1)];
	td.mipmapLevelCount = levels;
	id<MTLTexture> tex = [gMetalDevice newTextureWithDescriptor:td];
	if (tex == nil) { M3rTextureMap_UnloadTemporary(texture_map); return UUcFalse; }

	// Upload the chain (same walk as gl_utility.c:1096-1133).
	{
		UUtUns32 w = top_w, h = top_h;
		const void *src = base;
		for (UUtUns32 level = 0; level < levels; level++) {
			if (!metal_upload_level(tex, kind, texture_map->texelType, src, level, w, h)) {
				M3rTextureMap_UnloadTemporary(texture_map);
				return UUcFalse;
			}
			src = (const char *)src +
				IMrImage_ComputeSize(texture_map->texelType, IMcNoMipMap, (UUtUns16)w, (UUtUns16)h);
			w = UUmMax(w >> 1, 1);
			h = UUmMax(h >> 1, 1);
		}
	}

	// Slot it into the table (reusing a free index when available).
	{
		id<MTLSamplerState> sampler = metal_sampler_for(
			(texture_map->flags & M3cTextureFlags_ClampHoriz) != 0,
			(texture_map->flags & M3cTextureFlags_ClampVert) != 0,
			levels > 1);
		NSUInteger index;
		if (texture_map->opengl_texture_name != 0) {
			index = texture_map->opengl_texture_name - 1; // re-upload in place
			gTextureTable[index]    = tex;
			gTextureSamplers[index] = sampler;
		} else if ((index = [gFreeIndices firstIndex]) != NSNotFound) {
			[gFreeIndices removeIndex:index];
			gTextureTable[index]    = tex;
			gTextureSamplers[index] = sampler;
		} else {
			[gTextureTable addObject:tex];
			[gTextureSamplers addObject:sampler];
			index = gTextureTable.count - 1;
		}
		texture_map->opengl_texture_name = (UUtUns32)(index + 1);
	}

	texture_map->opengl_dirty = UUcFalse;
	M3rTextureMap_UnloadTemporary(texture_map);
	return UUcTrue;
}

UUtBool metal_texture_map_delete(M3tTextureMap *texture_map)
{
	UUmAssert(texture_map);
	if (texture_map->flags & M3cTextureFlags_Offscreen) {
		texture_map->opengl_dirty = UUcTrue;  // never delete offscreen (gl parity)
	}
	else if (texture_map->opengl_texture_name != 0) {
		NSUInteger index = texture_map->opengl_texture_name - 1;
		if (gTextureTable != nil && index < gTextureTable.count) {
			gTextureTable[index]    = (id)[NSNull null];
			gTextureSamplers[index] = (id)[NSNull null];
			[gFreeIndices addIndex:index];
		}
		texture_map->opengl_texture_name = 0;
		texture_map->opengl_dirty = UUcTrue;
	}
	return UUcTrue;
}

id<MTLTexture> metal_texture_lookup(M3tTextureMap *inMap, id<MTLSamplerState> *outSampler)
{
	if (inMap == NULL) { return nil; }
	if (inMap->opengl_dirty || inMap->opengl_texture_name == 0) {
		// gl_texture_map_reload equivalent: in-place re-create.
		if (!metal_texture_map_create(inMap)) { return nil; }
	}
	NSUInteger index = inMap->opengl_texture_name - 1;
	if (index >= gTextureTable.count) { return nil; }
	id obj = gTextureTable[index];
	if (obj == (id)[NSNull null]) { return nil; }
	if (outSampler) {
		id samp = gTextureSamplers[index];
		*outSampler = (samp == (id)[NSNull null]) ? nil : (id<MTLSamplerState>)samp;
	}
	return (id<MTLTexture>)obj;
}

UUtBool metal_texture_format_available(IMtPixelType texel_type)
{
	// GL's version is self-describedly "totally meaningless" and effectively
	// always true; keep behavioural parity (upload guards the real cases).
	(void)texel_type;
	return UUcTrue;
}
