// ======================================================================
// Oni_Cinematic_macOS.mm
//
// Native macOS cinematic playback via AVFoundation, replacing the dead Bink
// path on ARM64 (see GitHub issue #31). Plays the Feral-converted intro.mov /
// outro.mov (H.264 + LPCM stereo, 512x384) bundled in the .app's Resources/.
//
// Design:
//   - Self-owned borderless black NSWindow over the main screen, so it behaves
//     identically at both call sites: the intro plays before the GL context is
//     up, and the outro plays after the SDL window has already been torn down.
//   - AVPlayer + AVPlayerLayer (hardware H.264 decode, A/V sync and audio output
//     for free). videoGravity = ResizeAspect (letterbox — preserve, no stretch).
//   - Blocks on the main thread, pumping the event queue, until the movie ends
//     or the user skips (Esc / any key / mouse click).
//   - Missing or failed asset => returns 0 immediately so the game continues.
//
// Called only from ONrMovie_Play() (Oni_Bink.c) on Apple + SDL builds.
// ======================================================================

#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include "Oni_Cinematic_macOS.h"

// Borderless windows refuse key status by default; allow it so key events route
// to us while the cinematic owns the screen.
@interface ONiCinematicWindow : NSWindow
@end

@implementation ONiCinematicWindow
- (BOOL)canBecomeKeyWindow  { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

int ONrCinematic_PlayNative(const char *inMovieBasename, void *inSDLWindow)
{
    @autoreleasepool {
        if (inMovieBasename == NULL || inMovieBasename[0] == '\0') {
            return 0;
        }

        NSString *base = [NSString stringWithUTF8String:inMovieBasename];
        if (base == nil) {
            return 0;
        }

        // Resolve <base>.mov from the app bundle Resources/. A nil result is not
        // an error: a bare-binary run (no .app) or a missing asset simply means
        // "no cinematic" — let the game proceed.
        NSURL *movieURL = [[NSBundle mainBundle] URLForResource:base withExtension:@"mov"];
        if (movieURL == nil) {
            return 0;
        }

        // The shared application exists under SDL, but the outro path runs late
        // in shutdown — fetch defensively.
        NSApplication *app = [NSApplication sharedApplication];

        AVPlayer *player = [AVPlayer playerWithURL:movieURL];
        if (player == nil) {
            return 0;
        }
        player.actionAtItemEnd = AVPlayerActionAtItemEndPause;

        AVPlayerLayer *playerLayer = [AVPlayerLayer playerLayerWithPlayer:player];
        playerLayer.videoGravity = AVLayerVideoGravityResizeAspect;

        // Decide where to play. Use the full frame of the monitor the Oni game
        // window is on, so the movie is true-fullscreen on the correct display
        // (fixes the multi-monitor case where mainScreen is a different display
        // than the one the game window is on). Fall back to the game window's own
        // frame, then to the main screen, if the screen can't be resolved (e.g.
        // the outro path, where the SDL window is already gone).
        NSRect frame;
        BOOL haveFrame = NO;
        if (inSDLWindow != NULL) {
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            if (SDL_GetWindowWMInfo((SDL_Window *)inSDLWindow, &wmInfo) &&
                wmInfo.subsystem == SDL_SYSWM_COCOA) {
                NSWindow *gameWindow = wmInfo.info.cocoa.window;
                if (gameWindow != nil) {
                    NSScreen *gameScreen = gameWindow.screen;
                    frame = (gameScreen != nil) ? gameScreen.frame : gameWindow.frame;
                    haveFrame = YES;
                }
            }
        }
        if (!haveFrame) {
            NSScreen *mainScreen = [NSScreen mainScreen];
            if (mainScreen == nil) {
                return 0;
            }
            frame = mainScreen.frame;
        }

        ONiCinematicWindow *window =
            [[ONiCinematicWindow alloc] initWithContentRect:frame
                                                  styleMask:NSWindowStyleMaskBorderless
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
        window.releasedWhenClosed = NO;             // ARC owns the window
        window.backgroundColor = [NSColor blackColor];
        window.opaque = YES;
        window.level = NSScreenSaverWindowLevel;    // cover the SDL game window

        NSView *contentView = [[NSView alloc] initWithFrame:frame];
        contentView.wantsLayer = YES;
        contentView.layer.backgroundColor = [NSColor blackColor].CGColor;
        playerLayer.frame = contentView.bounds;
        playerLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
        [contentView.layer addSublayer:playerLayer];
        window.contentView = contentView;

        [window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        [NSCursor hide];

        // End-of-playback flag, set on the main queue when the item finishes.
        __block BOOL finished = NO;
        id endObserver =
            [[NSNotificationCenter defaultCenter]
                addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                            object:player.currentItem
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification * _Nonnull note) {
                            (void)note;
                            finished = YES;
                        }];

        [player play];

        // Block on the main thread, pumping events, until done or skipped.
        BOOL skipped = NO;
        while (!finished && !skipped) {
            @autoreleasepool {
                NSEvent *event =
                    [app nextEventMatchingMask:NSEventMaskAny
                                     untilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]
                                        inMode:NSDefaultRunLoopMode
                                       dequeue:YES];
                if (event != nil) {
                    switch (event.type) {
                        case NSEventTypeKeyDown:
                        case NSEventTypeLeftMouseDown:
                        case NSEventTypeRightMouseDown:
                        case NSEventTypeOtherMouseDown:
                            skipped = YES;          // any key or click skips
                            break;
                        default:
                            [app sendEvent:event]; // keep the window/app healthy
                            break;
                    }
                }

                // Bail out if the item failed to load/decode rather than hang.
                if (player.currentItem != nil &&
                    player.currentItem.status == AVPlayerItemStatusFailed) {
                    break;
                }
            }
        }

        [player pause];
        [[NSNotificationCenter defaultCenter] removeObserver:endObserver];
        [NSCursor unhide];
        [window orderOut:nil];

        return 1;
    }
}
