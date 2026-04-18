// ======================================================================
// BFW_SS2_Platform_OpenAL.h
// ======================================================================
#pragma once

#ifndef BFW_SS2_PLATFORM_OPENAL_H
#define BFW_SS2_PLATFORM_OPENAL_H

// ======================================================================
// includes
// ======================================================================
#include "BFW.h"
#if defined(__APPLE__)
#include <OpenAL/al.h>
#else
#include "AL/al.h"
#endif

// ======================================================================
// typedefs
// ======================================================================
//typedef struct{}		SStSemaphore;
typedef struct{}		SStGuard;

typedef struct SStSoundChannel_PlatformData
{
	ALuint buffer, source;
} SStSoundChannel_PlatformData;

// ======================================================================
#endif /* BFW_SS2_PLATFORM_OPENAL_H */
