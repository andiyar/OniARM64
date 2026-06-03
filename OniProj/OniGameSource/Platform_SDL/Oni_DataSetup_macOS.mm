// ======================================================================
// Oni_DataSetup_macOS.mm
//
// Native macOS first-run guided game-data setup. Runs when the resolver
// (ONi_BundlePath.c) finds no recognised GameDataFolder — instead of the silent
// quit a Finder double-click used to produce, it walks the user through:
//
//   inform  -> NSAlert "couldn't find game data; Choose / Quit"
//   pick    -> NSOpenPanel (directories only)
//   validate-> ONiGameData_FindFolderIn (accepts the folder, or descends into a
//              child GameDataFolder/gamedata; rejects non-data with a re-prompt)
//   copy    -> ONiGameData_CopyTree into
//              ~/Library/Application Support/OniARM64/GameDataFolder, contents
//              landing directly inside (no double-nesting), shown behind an
//              indeterminate progress window pumped on the main thread
//   errors  -> NSAlert Retry / Choose again / Quit; partial copy is cleaned up
//
// All non-UI logic lives in the pure, unit-tested C core (ONi_GameData.c); this
// file is only the thin Cocoa shell. Called from ONiInitializeAll (Oni.c) on
// Apple + SDL builds; must run on the main thread (it does — engine init is
// single-threaded here, before the SDL window exists).
// ======================================================================

#import <Cocoa/Cocoa.h>

#include "Oni_DataSetup_macOS.h"
#include "ONi_GameData.h"

#include <stdlib.h>

// Destination: ~/Library/Application Support/OniARM64/GameDataFolder
// (the natural retail name; the resolver tries this first). nil if $HOME unset.
static NSString *ONiDataSetup_DestPath(void)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return nil;
    }
    return [NSString stringWithFormat:
            @"%s/Library/Application Support/OniARM64/GameDataFolder", home];
}

static NSAlert *ONiDataSetup_Alert(NSString *message, NSString *info)
{
    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = message;
    if (info != nil) {
        a.informativeText = info;
    }
    return a;
}

// Copy src -> dst behind an indeterminate progress window. The copy runs on a
// background queue (ONiGameData_CopyTree is pure libc, thread-safe); the main
// thread pumps the run loop so the window stays alive and the bar animates.
// Returns 0 on success, -1 on failure (message written to *outError).
static int ONiDataSetup_CopyWithProgress(NSString *src, NSString *dst, NSString **outError)
{
    NSApplication *app = [NSApplication sharedApplication];

    NSWindow *win =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 440, 116)
                                    styleMask:NSWindowStyleMaskTitled
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    win.releasedWhenClosed = NO; // ARC owns it
    win.title = @"OniARM64";
    [win center];

    NSTextField *label =
        [NSTextField labelWithString:@"Copying Oni data… this may take a minute."];
    label.frame = NSMakeRect(20, 62, 400, 20);
    [win.contentView addSubview:label];

    NSProgressIndicator *bar =
        [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 28, 400, 20)];
    bar.style = NSProgressIndicatorStyleBar;
    bar.indeterminate = YES;
    [win.contentView addSubview:bar];

    [win makeKeyAndOrderFront:nil];
    [app activateIgnoringOtherApps:YES];
    [bar startAnimation:nil];

    // Stack buffer (the function blocks in the pump loop below, so it outlives
    // the copy). Capture the pointer + size by value — capturing the array
    // itself would copy it into the block, and the writes would be lost.
    char errBuf[512];
    errBuf[0] = '\0';
    char *errPtr = errBuf;
    const size_t errCap = sizeof(errBuf);

    __block int rc = -1;
    __block BOOL done = NO;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        int r = ONiGameData_CopyTree(src.fileSystemRepresentation,
                                     dst.fileSystemRepresentation,
                                     errPtr, errCap);
        // Publish the result on the main thread so there's no data race with
        // the pump loop below (it reads `done` on the main thread).
        dispatch_async(dispatch_get_main_queue(), ^{
            rc = r;
            done = YES;
        });
    });

    while (!done) {
        @autoreleasepool {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }
    }

    [bar stopAnimation:nil];
    [win orderOut:nil];

    if (rc != 0 && outError != NULL) {
        *outError = (errBuf[0] != '\0')
                        ? [NSString stringWithUTF8String:errBuf]
                        : @"The copy failed.";
    }
    return rc;
}

int ONrDataSetup_RunGuidedPicker(void)
{
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        // This runs before SDL has set up the app as a GUI app. Make it a
        // regular foreground app AND complete AppKit launch ourselves —
        // otherwise the dialogs (especially NSOpenPanel, which is hosted out of
        // process and expects a finished launch) may not appear or receive
        // events. Safe here: the picker runs at most once, before SDL's own
        // Cocoa init; the Regular activation policy is intentionally left set
        // because SDL takes over the foreground GUI immediately on the success
        // path, and every non-success path exits the process.
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app finishLaunching];
        [app activateIgnoringOtherApps:YES];

        NSString *dst = ONiDataSetup_DestPath();
        if (dst == nil) {
            NSAlert *a = ONiDataSetup_Alert(
                @"Can't locate your home folder",
                @"Oni couldn't work out where to install game data ($HOME is unset).");
            [a addButtonWithTitle:@"Quit"];
            [a runModal];
            return 0;
        }

        // 1. Inform.
        {
            NSAlert *a = ONiDataSetup_Alert(
                @"Oni couldn't find its game data",
                @"Click Choose to locate your GameDataFolder. It will be copied into "
                @"Application Support so Oni can find it from now on.");
            [a addButtonWithTitle:@"Choose…"];
            [a addButtonWithTitle:@"Quit"];
            if ([a runModal] != NSAlertFirstButtonReturn) {
                return 0;
            }
        }

        NSFileManager *fm = [NSFileManager defaultManager];

        // 2–7. Pick / validate / copy.
        for (;;) {
            NSOpenPanel *panel = [NSOpenPanel openPanel];
            panel.canChooseDirectories = YES;
            panel.canChooseFiles = NO;
            panel.allowsMultipleSelection = NO;
            panel.prompt = @"Choose";
            panel.message = @"Select your Oni GameDataFolder";
            if ([panel runModal] != NSModalResponseOK) {
                return 0; // cancel = quit
            }
            NSURL *url = panel.URLs.firstObject;
            if (url == nil) {
                return 0;
            }

            // Validate, descending into a child GameDataFolder/gamedata if the
            // user picked the parent.
            char resolved[2048];
            if (!ONiGameData_FindFolderIn(url.path.fileSystemRepresentation,
                                          resolved, sizeof(resolved))) {
                NSAlert *a = ONiDataSetup_Alert(
                    @"That folder doesn't look like Oni game data",
                    @"No Oni level files were found there. Choose the folder that holds your "
                    @"Oni data (it should contain files like level0_Final.dat).");
                [a addButtonWithTitle:@"Choose again"];
                [a addButtonWithTitle:@"Quit"];
                if ([a runModal] == NSAlertFirstButtonReturn) {
                    continue;
                }
                return 0;
            }
            NSString *src = [NSString stringWithUTF8String:resolved];

            // Ensure the Application Support/OniARM64 parent exists; clear any
            // stale/partial destination so the copy starts clean.
            [fm createDirectoryAtPath:dst.stringByDeletingLastPathComponent
          withIntermediateDirectories:YES
                           attributes:nil
                                error:NULL];

            // Copy, with retry / re-pick on failure.
            BOOL rechoose = NO;
            for (;;) {
                [fm removeItemAtPath:dst error:NULL]; // ignore "not found"
                NSString *err = nil;
                if (ONiDataSetup_CopyWithProgress(src, dst, &err) == 0) {
                    return 1; // installed — caller re-resolves and continues
                }
                NSAlert *a = ONiDataSetup_Alert(@"Couldn't copy the game data",
                                                err ?: @"The copy failed.");
                [a addButtonWithTitle:@"Retry"];
                [a addButtonWithTitle:@"Choose again"];
                [a addButtonWithTitle:@"Quit"];
                NSModalResponse r = [a runModal];
                if (r == NSAlertFirstButtonReturn) {
                    continue; // retry same source
                }
                if (r == NSAlertSecondButtonReturn) {
                    rechoose = YES;
                    break;
                }
                return 0; // quit
            }
            if (rechoose) {
                continue;
            }
        }
    }
}
