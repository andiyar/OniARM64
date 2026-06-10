// ======================================================================
// Oni_RendererSelect_macOS.mm
//
// Hold-Option "Choose Renderer" dialog. Sibling to the first-run data-setup
// picker (Oni_DataSetup_macOS.mm); runs in the same pre-SDL slot in ONiMain,
// between the command-line/env renderer resolution and the Metal availability
// probe. Holding Option/Alt at launch pops a native NSAlert offering OpenGL
// (default) vs Metal; not held -> no dialog, the resolved default is returned
// unchanged. The pick still flows through the availability probe, so a Metal
// choice on hardware without Metal still falls back to OpenGL.
//
// This runs even EARLIER than the data-setup picker (first AppKit touch in the
// process, before UUrInitialize), so two cold-process details matter:
// - The Option check uses CGEventSourceFlagsState (live hardware/session state)
//   rather than +[NSEvent modifierFlags], whose documented contract is "as of
//   the last event received" — and this process has received no events yet.
// - The full activation dance from Oni_DataSetup_macOS.mm including
//   -finishLaunching, whose own comment notes pre-launch dialogs "may not
//   appear or receive events" without it (key-window / Return-key routing).
// ======================================================================

#import <Cocoa/Cocoa.h>

extern "C" {
#include "BFW.h"
#include "Oni_RendererSelect_macOS.h"
}

UUtBool OniMac_ChooseRendererIfOptionHeld(UUtBool inDefaultMetal)
{
	CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
	if ((flags & kCGEventFlagMaskAlternate) == 0) {
		return inDefaultMetal; // Option not held -> keep the resolved default
	}

	@autoreleasepool {
		// Activation dance from Oni_DataSetup_macOS.mm: without it the alert can
		// appear behind other apps' windows (or miss key events) when launched
		// from Finder.
		NSApplication *app = [NSApplication sharedApplication];
		[app setActivationPolicy:NSApplicationActivationPolicyRegular];
		[app finishLaunching];
		[app activateIgnoringOtherApps:YES];

		NSAlert *alert = [[NSAlert alloc] init];
		alert.messageText = @"Choose Renderer";
		alert.informativeText = @"OpenGL is the stable default. Metal is the new native backend (in testing).";
		[alert addButtonWithTitle:@"OpenGL"]; // first button = NSAlertFirstButtonReturn (default, Return key)
		[alert addButtonWithTitle:@"Metal"];
		NSModalResponse r = [alert runModal];
		return (r == NSAlertSecondButtonReturn) ? UUcTrue : UUcFalse;
	}
}
