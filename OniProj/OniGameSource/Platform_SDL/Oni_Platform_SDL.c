/*
	FILE:	Oni_Platform_SDL.c

	PURPOSE: SDL specific code

*/
// ======================================================================
// includes
// ======================================================================
#include "BFW.h"

#include "Oni_Platform.h"
#include "Oni.h" // for ONgCommandLine.useMetal

#include <signal.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_video.h>
// ======================================================================
// defines
// ======================================================================
#define ONcMainWindowTitle	("ONI ")

#define	ONcSurface_Width	MScScreenWidth
#define ONcSurface_Height	MScScreenHeight

#define ONcSurface_Left	0
#define ONcSurface_Top	0

#if defined(DEBUGGING) && DEBUGGING

	#define DEBUG_AKIRA 1

#endif

// ======================================================================
// globals
// ======================================================================

FILE*		ONgErrorFile = NULL;

// ======================================================================
// functions
// ======================================================================

// ----------------------------------------------------------------------
static void
ONiPlatform_CreateWindow(
	ONtPlatformData		*ioPlatformData)
{
	//FIXME: displayIndex
	SDL_DisplayMode desktopMode;
	Uint32 renderer_flag = SDL_WINDOW_OPENGL;
	SDL_GetDesktopDisplayMode(0, &desktopMode);
#ifdef __APPLE__
	if (ONgCommandLine.useMetal) { renderer_flag = SDL_WINDOW_METAL; }
	UUrStartupMessage("[Window] creating SDL window with %s flag (useMetal=%d)",
		(renderer_flag == SDL_WINDOW_METAL) ? "METAL" : "OPENGL", ONgCommandLine.useMetal);
#endif
	//TODO: SDL_WINDOW_FULLSCREEN?
	ioPlatformData->gameWindow = SDL_CreateWindow(ONcMainWindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, desktopMode.w, desktopMode.h, renderer_flag | SDL_WINDOW_SHOWN);

	if (!ioPlatformData->gameWindow)
	{
		// error here
	}
}


// ----------------------------------------------------------------------
// Crash handler: on SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT, call SDL_Quit
// so GPU/IOKit/audio handles are released in the normal teardown order.
// Without this, macOS pins the crashed process in kernel state `UE`
// (uninterruptible sleep, exited) indefinitely — an unkillable zombie
// that only a reboot clears, and which accumulates 1-per-crash during
// debug iteration. After cleanup we re-raise with SIG_DFL so a crash
// report still lands in ~/Library/Logs/DiagnosticReports for lldb.
//
// Signal handlers aren't strictly async-signal-safe for SDL_Quit, but
// in practice the common "game-logic bad pointer" SIGSEGV lets us run
// SDL_Quit just fine. Worst case the handler itself crashes — which is
// no worse than the current "become permanent zombie" outcome.
static void
ONiCrashSignalHandler(int sig)
{
	static volatile sig_atomic_t s_entered = 0;
	if (s_entered) {
		_exit(128 + sig);
	}
	s_entered = 1;

	SDL_Quit();

	signal(sig, SIG_DFL);
	raise(sig);
}

static void
ONiInstallCrashHandlers(void)
{
	signal(SIGSEGV, ONiCrashSignalHandler);
	signal(SIGBUS,  ONiCrashSignalHandler);
	signal(SIGFPE,  ONiCrashSignalHandler);
	signal(SIGILL,  ONiCrashSignalHandler);
	signal(SIGABRT, ONiCrashSignalHandler);
}

// ----------------------------------------------------------------------
UUtError ONrPlatform_Initialize(
	ONtPlatformData			*outPlatformData)
{
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);

	ONiInstallCrashHandlers();

	ONiPlatform_CreateWindow(outPlatformData);

	SDL_ShowCursor(SDL_FALSE);

	return UUcError_None;
}

// ----------------------------------------------------------------------
UUtBool
ONrPlatform_IsForegroundApp(
	void)
{
	//TODO: or SDL_GetMouseFocus()?
	return (ONgPlatformData.gameWindow == SDL_GetKeyboardFocus());
}

// ----------------------------------------------------------------------
void ONrPlatform_Terminate(
	void)
{
	if(ONgErrorFile != NULL)
	{
		fclose(ONgErrorFile);
	}

	SDL_Quit();
}

// ----------------------------------------------------------------------
void ONrPlatform_Update(
	void)
{
}

// ----------------------------------------------------------------------
void ONrPlatform_ErrorHandler(
	UUtError			theError,
	char				*debugDescription,
	UUtInt32			userDescriptionRef,
	char				*message)
{

	if(ONgErrorFile == NULL)
	{
		ONgErrorFile = UUrFOpen("oniErr.txt", "wb");

		if(ONgErrorFile == NULL)
		{
			/* XXX - Someday bitch really loudly */
		}
	}

	fprintf(ONgErrorFile, "InternalError: %s, %s\n\r", debugDescription, message);
}

void
ONrPlatform_CopyAkiraToScreen(
	UUtUns16	inBufferWidth,
	UUtUns16	inBufferHeight,
	UUtUns16	inRowBytes,
	UUtUns16*	inBaseAdddr);

void
ONrPlatform_CopyAkiraToScreen(
	UUtUns16	inBufferWidth,
	UUtUns16	inBufferHeight,
	UUtUns16	inRowBytes,
	UUtUns16*	inBaseAdddr)
{

}
