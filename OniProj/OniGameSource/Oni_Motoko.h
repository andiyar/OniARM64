#pragma once

/*
	FILE:	Oni_Motoko.h

	AUTHOR:	Brent H. Pease

	CREATED: Sept 28, 1998

	PURPOSE: Motoko stuff for Oni

	Copyright 1999

*/
#ifndef ONI_MOTOKO_H
#define ONI_MOTOKO_H

#include "Oni_Platform.h"

typedef enum ONtGraphicsQuality
{
	ONcGraphicsQuality_0,
	ONcGraphicsQuality_1,
	ONcGraphicsQuality_2,
	ONcGraphicsQuality_3,
	ONcGraphicsQuality_4,

	ONcGraphicsQuality_SuperLow = ONcGraphicsQuality_0,
	ONcGraphicsQuality_Low = ONcGraphicsQuality_1,
	ONcGraphicsQuality_Medium = ONcGraphicsQuality_2,
	ONcGraphicsQuality_High = ONcGraphicsQuality_3,
	ONcGraphicsQuality_SuperHigh = ONcGraphicsQuality_4,

	ONcGraphicsQuality_Min = ONcGraphicsQuality_0,
	ONcGraphicsQuality_Max = ONcGraphicsQuality_4,
	ONcGraphicsQuality_Default = ONcGraphicsQuality_Max

} ONtGraphicsQuality;


UUtError
ONrMotoko_Initialize(
	void);

void
ONrMotoko_Terminate(
	void);

UUtError
ONrMotoko_SetupDrawing(
	ONtPlatformData*	inPlatformData);

void
ONrMotoko_TearDownDrawing(
	void);

UUtBool ONrMotoko_SetResolution(M3tDisplayMode *inResolution);

UUtInt32 ONrMotoko_GraphicsQuality_CharacterPolygonCount(void);
UUtInt32 ONrMotoko_GraphicsQuality_RayCastCount(void);
UUtInt32 ONrMotoko_GraphicsQuality_NumDirectionalLights(void);
UUtBool ONrMotoko_GraphicsQuality_SupportShadows(void);
UUtBool ONrMotoko_GraphicsQuality_SupportTrilinear(void);
UUtBool ONrMotoko_GraphicsQuality_SupportReflectionMapping(void);
UUtBool ONrMotoko_GraphicsQuality_SupportHighQualityTextures(void);
UUtBool ONrMotoko_GraphicsQuality_SupportCosmeticCorpses(void);
UUtBool ONrMotoko_GraphicsQuality_SupportHighQualityCorpses(void);
UUtBool ONrMotoko_GraphicsQuality_NeverUseSuperLow(void);
UUtBool ONrMotoko_GraphicsQuality_HardwareBink(void);
UUtInt32 ONrMotoko_GraphicsQuality_RayCount(void);

extern float ONgMotoko_FieldOfView;
#define ONcMotoko_NearPlane 2.0f
//#define ONcMotoko_FarPlane 1000.0f
#define ONcMotoko_AspectRatio ((float)M3rDraw_GetWidth() / (float)M3rDraw_GetHeight())
// In-engine cutscenes were authored for a 4:3 frame with a 45 degree vertical field.
// Widescreen is "hor+" (#134): the vertical field stays at the authored value at every
// window aspect and the horizontal field opens with the width (57.8 degrees at 4:3,
// 72.7 at 16:9, about 93 at 21:9). Shots that framed side geometry out at 4:3 can
// show it; those are fixed per shot on the data side (ONrCutscene_EnvShowCompanion).
#define ONcMotoko_CutsceneAspect (4.0f / 3.0f)
// Dev override: ONI_CUTSCENE_CROP_CAP=<aspect> at launch re-enables the #36
// fill-width / vertical-crop framing (hold the 4:3 horizontal field, trim the vertical
// field) capped at that aspect, e.g. 100 = the 1.3.0 crop at any width, 1.7778 = the
// 16:9-capped crop of 1.3.1-test1. For before/after comparisons only.

extern UUtInt32 ONgMotoko_ClearColor;
extern UUtBool ONgMotoko_ShadeVertex;
extern UUtBool ONgMotoko_FillSolid;
extern UUtBool ONgMotoko_Texture;
extern UUtBool ONgMotoko_ZCompareOn;
extern UUtBool ONgMotoko_DoubleBuffer;
extern UUtBool ONgMotoko_BufferClear;


extern float	ONgMotoko_FarPlane;

#endif /* ONI_MOTOKO_H */
