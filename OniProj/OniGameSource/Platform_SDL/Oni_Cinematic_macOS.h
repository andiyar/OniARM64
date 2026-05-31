// ======================================================================
// Oni_Cinematic_macOS.h
//
// Native macOS (AVFoundation) playback for the intro/outro cinematics,
// replacing the dead Bink path (BFW_Bink.c is a no-op on ARM64 — see #31).
// Plain-C shim so the C engine (Oni_Bink.c) can call into the Obj-C++ impl.
// ======================================================================
#ifndef ONI_CINEMATIC_MACOS_H
#define ONI_CINEMATIC_MACOS_H

#ifdef __cplusplus
extern "C" {
#endif

// Play <inMovieBasename>.mov from the app bundle's Resources/, blocking, until
// the movie finishes or the user skips (Esc / any key / click). inMovieBasename
// carries no extension (e.g. "intro", "outro").
//
// inSDLWindow is the Oni SDL_Window* (ONgPlatformData.gameWindow), used to anchor
// the movie to the game window's monitor and position; may be NULL (e.g. the
// outro path), in which case it falls back to the main screen.
//
// Returns 1 if a movie was played or skipped; returns 0 if the asset was
// missing or failed to open. A 0 return means "nothing played" and the caller
// must continue normally — a missing cinematic must never block startup.
int ONrCinematic_PlayNative(const char *inMovieBasename, void *inSDLWindow);

#ifdef __cplusplus
}
#endif

#endif // ONI_CINEMATIC_MACOS_H
