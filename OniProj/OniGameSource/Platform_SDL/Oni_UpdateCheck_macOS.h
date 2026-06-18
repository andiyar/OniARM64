// ======================================================================
// Oni_UpdateCheck_macOS.h
//
// Cocoa shell for the GitHub release-update notifier (#40). One C entry
// point, called once at startup from Oni.c (pre-window, main thread).
// No-op on non-Apple builds (the .mm only compiles under APPLE).
// ======================================================================
#ifndef ONI_UPDATECHECK_MACOS_H
#define ONI_UPDATECHECK_MACOS_H

#ifdef __cplusplus
extern "C" {
#endif

// Fetch the public Releases list, decide via ONi_UpdateCheck core, and
// (if a newer non-skipped release exists) show a native NSAlert. Throttled
// and opt-out aware via NSUserDefaults; bounded ~2.5s network wait; silent
// on offline / rate-limit / error. Returns after the dialog is dismissed
// (or immediately if gated/throttled); the game continues launching.
void ONrUpdateCheck_RunAtStartup(void);

#ifdef __cplusplus
}
#endif

#endif // ONI_UPDATECHECK_MACOS_H
