// ======================================================================
// Oni_DataSetup_macOS.h
//
// First-run guided game-data setup (native Cocoa). When the layered resolver
// (ONi_BundlePath.c) finds no recognised GameDataFolder, the engine calls this
// instead of quitting silently: a native dialog guides the user to locate their
// GameDataFolder, validates it, and copies it into
// ~/Library/Application Support/OniARM64/GameDataFolder so it's found from then
// on. Plain-C shim so the C engine (Oni.c) can call into the Obj-C++ impl.
// ======================================================================
#ifndef ONI_DATASETUP_MACOS_H
#define ONI_DATASETUP_MACOS_H

#ifdef __cplusplus
extern "C" {
#endif

// Show the first-run guided data-setup flow (inform → choose → validate → copy
// with progress, with re-pick / retry / quit handling). Blocks on the main
// thread until the user finishes or quits.
//
// Returns 1 if game data was successfully installed into the canonical location
// — the caller should re-resolve and continue. Returns 0 if the user cancelled
// or quit at any point — the caller should exit cleanly.
//
// Must be called on the main thread.
int ONrDataSetup_RunGuidedPicker(void);

#ifdef __cplusplus
}
#endif

#endif // ONI_DATASETUP_MACOS_H
