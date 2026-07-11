// ======================================================================
// Oni_CrashReport_macOS.h
//
// Crash-recovery UX (#74): a sentinel file marks a session in flight;
// if it survives to the next launch, the session died dirty (crash,
// force-quit, SIGKILL) and a native pre-window NSAlert offers a
// pre-filled GitHub issue and a Finder reveal of the logs. No servers,
// no embedded tokens — the user sees and submits the report themselves.
// Mirrors the Oni_UpdateCheck_macOS.mm pattern (Cocoa shell, one .mm,
// no-op on non-Apple builds).
// ======================================================================
#ifndef ONI_CRASHREPORT_MACOS_H
#define ONI_CRASHREPORT_MACOS_H

#ifdef __cplusplus
extern "C" {
#endif

// Call once at startup, pre-window on the main thread (same safe point as
// the update check). If inSentinelPath exists, the previous session did not
// shut down cleanly: show the report dialog (unless suppressed via
// NSUserDefaults), then remove the stale sentinel either way. Caches
// inSentinelPath for the two calls below. inRendererName is the human
// string for THIS launch ("Metal"/"OpenGL") used in the pre-filled report.
void ONrCrashReport_CheckAndPromptAtStartup(
	const char *inSentinelPath,
	const char *inRendererName);

// Write the sentinel — the session is now in flight. Call after the
// pre-window dialogs (data picker / update check) so their clean exit(0)
// paths can never leave a stale sentinel behind.
void ONrCrashReport_MarkSessionActive(void);

// Remove the sentinel — the session is ending cleanly. Call at the end of
// the normal exit path. Safe to call if the sentinel was never written.
void ONrCrashReport_MarkCleanExit(void);

#ifdef __cplusplus
}
#endif

#endif // ONI_CRASHREPORT_MACOS_H
