// ======================================================================
// Oni_Bink.c
// ======================================================================

// ======================================================================
// includes
// ======================================================================
#include "BFW.h"
#include "BFW_FileManager.h"
#include "BFW_Bink.h"
#include "BFW_ScriptLang.h"

#include "Oni.h"
#include "Oni_Bink.h"
#include "Oni_Platform.h"

#if defined(__APPLE__) && UUmSDL
#include "Oni_Cinematic_macOS.h"
#endif

// ======================================================================
// functions
// ======================================================================
// ----------------------------------------------------------------------
UUtError
ONrMovie_Play(
	char						*inMovieName,
	BKtScale					inScale)
{
#if defined(__APPLE__) && UUmSDL
	// macOS: native AVFoundation playback replaces the (dead) Bink path (#31).
	// "intro.bik" -> play "intro.mov" from the app bundle Resources/.
	// A missing asset is a graceful no-op so it never blocks startup.
	char						base[BFcMaxFileNameLength];
	char						*dot;

	UUrString_Copy(base, inMovieName, BFcMaxFileNameLength);
	dot = strrchr(base, '.');
	if (dot != NULL)
	{
		*dot = '\0';
	}

	ONrCinematic_PlayNative(base, (void *)ONgPlatformData.gameWindow);

	(void)inScale;
	return UUcError_None;
#else
	UUtError					error;
	UUtWindow					window;
	BFtFileRef					*dir_ref;
	BFtFileRef					*movie_ref;

	// get a pointer to the window
	window = ONgPlatformData.gameWindow;

	// get the DataFileDirectory
	error = BFrFileRef_Duplicate(&ONgGameDataFolder, &dir_ref);
	UUmError_ReturnOnError(error);

	// create a file ref for the movie
	error = BFrFileRef_DuplicateAndAppendName(dir_ref, inMovieName, &movie_ref);
	UUmError_ReturnOnError(error);

	// play the movie
	error = BKrMovie_Play(movie_ref, window, inScale);
	UUmError_ReturnOnError(error);

	BFrFileRef_Dispose(dir_ref);
	dir_ref = NULL;

	BFrFileRef_Dispose(movie_ref);
	movie_ref = NULL;

	return UUcError_None;
#endif
}

UUtError BKrMovie_Play_OpenGL(
	BFtFileRef *movie_ref,
	UUtWindow window,
	BKtScale scale_type);


UUtError
ONrMovie_Play_Hardware(
	char						*inMovieName,
	BKtScale					inScale)
{
	UUtError					error;
	UUtWindow					window;
	BFtFileRef					*dir_ref;
	BFtFileRef					*movie_ref;

	// get a pointer to the window
	window = ONgPlatformData.gameWindow;

	// get the DataFileDirectory
	error = BFrFileRef_Duplicate(&ONgGameDataFolder, &dir_ref);
	UUmError_ReturnOnError(error);

	// create a file ref for the movie
	error = BFrFileRef_DuplicateAndAppendName(dir_ref, inMovieName, &movie_ref);
	UUmError_ReturnOnError(error);

	// play the movie
	error = BKrMovie_Play_OpenGL(movie_ref, window, inScale);
	UUmError_ReturnOnError(error);

	BFrFileRef_Dispose(dir_ref);
	dir_ref = NULL;

	BFrFileRef_Dispose(movie_ref);
	movie_ref = NULL;

	return UUcError_None;
}

// ----------------------------------------------------------------------
static UUtError
ONiMovie_Play(
	SLtErrorContext*			inErrorContext,
	UUtUns32					inParameterListLength,
	SLtParameter_Actual*		inParameterList,
	UUtUns32					*outTicksTillCompletion,	// This returns the number of ticks the caller should be slept
	UUtBool						*outStall,					// This means the caller should be slept and this call made again
	SLtParameter_Actual			*ioReturnValue)
{
	UUtError					error;
	char						name[BFcMaxFileNameLength];

	if (strlen(inParameterList[0].val.str) >= (BFcMaxFileNameLength - strlen(".bik")))
	{
		COrConsole_Printf("The movie name is too long.");
		return UUcError_None;
	}

	UUrString_Copy(name, inParameterList[0].val.str, BFcMaxFileNameLength);
	UUrString_Cat(name, ".bik", BFcMaxFileNameLength);

	error = ONrMovie_Play(name, BKcScale_Default);
	UUmError_ReturnOnError(error);

	return UUcError_None;
}

// ----------------------------------------------------------------------
UUtError
ONrMovie_Initialize(
	void)
{
	UUtError					error;

	error =
		SLrScript_Command_Register_Void(
			"movie_play",
			"function to start a movie playing",
			"name:string",
			ONiMovie_Play);
	UUmError_ReturnOnError(error);

	return UUcError_None;
}
