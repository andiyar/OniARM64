// ======================================================================
// Oni_CrashReport_macOS.mm
//
// Crash-recovery UX (#74). Sentinel + next-launch NSAlert + pre-filled
// GitHub issue URL + Finder reveals. Deliberately server-free: a shipped
// token in a public app is extractable, so nothing auto-files — the
// browser opens on GitHub's new-issue compose page with the body visible
// and the user submits (or doesn't) themselves.
//
// Mirrors Oni_UpdateCheck_macOS.mm: Cocoa shell, no BFW headers (the
// one engine call, UUrStartupMessage, is declared extern below), state
// in NSUserDefaults, pre-window NSAlert via the shared EnsureAppKit
// bring-up pattern.
// ======================================================================
#import <Cocoa/Cocoa.h>

#include "Oni_CrashReport_macOS.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Greppable session-log lines without dragging in the BFW master header
// (this .mm builds without the C PCH, same as the update check).
extern "C" void UUrStartupMessage(const char *format, ...);

#ifndef ONI_VERSION
#define ONI_VERSION "unknown"
#endif
#ifndef ONI_BUILD_STAMP
#define ONI_BUILD_STAMP "unknown@unknown"
#endif

static NSString * const kOniCrashReportDisabled = @"OniCrashReportDisabled";

static NSString * const kOniNewIssueURL =
    @"https://github.com/andiyar/OniARM64/issues/new";

// Encoded-URL budget. GitHub/browsers start misbehaving in the 8 KB region;
// stay comfortably under it and drop oldest log lines to fit.
static const NSUInteger kOniCrashReportURLBudget = 7000;
static const int        kOniCrashReportLogLines  = 30;
// A crash report older than this is presumed unrelated to the sentinel.
static const NSTimeInterval kOniCrashReportIPSMaxAge = 7 * 24 * 60 * 60;

static char gOniCrashSentinelPath[1024];   // cached by CheckAndPromptAtStartup

// Same AppKit bring-up as ONiUpdate_EnsureAppKit / the data picker.
// dispatch_once so whichever pre-window dialog runs first does the init.
static void ONiCrash_EnsureAppKit(void)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app finishLaunching];
        [app activateIgnoringOtherApps:YES];
    });
}

// ----------------------------------------------------------------------
// Report ingredients
// ----------------------------------------------------------------------

// startup.txt path, mirroring iOpenLogFile's cwd-first fallback
// (BFW_Error.c): an existing ./startup.txt (bare-binary workflow) wins,
// else ~/Library/Logs/OniARM64/startup.txt (.app workflow).
static NSString *ONiCrash_StartupLogPath(void)
{
    NSFileManager *fm = [NSFileManager defaultManager];
    if ([fm fileExistsAtPath:@"./startup.txt"]) {
        return @"./startup.txt";
    }
    return [NSHomeDirectory()
        stringByAppendingPathComponent:@"Library/Logs/OniARM64/startup.txt"];
}

// Last inMaxLines lines of the startup log (previous session's tail — this
// session has only written a few lines by the time the dialog runs, and the
// log is append-mode, #17). Reads at most the final 64 KB. nil if unreadable.
static NSArray<NSString *> *ONiCrash_LogTail(int inMaxLines)
{
    FILE *f = fopen([ONiCrash_StartupLogPath() fileSystemRepresentation], "rb");
    if (f == NULL) {
        return nil;
    }

    const long kWindow = 64 * 1024;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long start = (size > kWindow) ? (size - kWindow) : 0;
    fseek(f, start, SEEK_SET);

    NSMutableData *data = [NSMutableData dataWithLength:(NSUInteger)(size - start)];
    size_t got = fread([data mutableBytes], 1, (size_t)(size - start), f);
    fclose(f);
    [data setLength:got];

    NSString *text = [[NSString alloc] initWithData:data
                                           encoding:NSUTF8StringEncoding];
    if (text == nil) {   // salvage a tail that starts mid-UTF-8 sequence
        text = [[NSString alloc] initWithData:data
                                     encoding:NSISOLatin1StringEncoding];
    }
    if (text == nil) {
        return nil;
    }

    NSMutableArray<NSString *> *lines =
        [[text componentsSeparatedByString:@"\n"] mutableCopy];
    while (lines.count > 0 && [lines.lastObject length] == 0) {
        [lines removeLastObject];
    }
    if ((int)lines.count > inMaxLines) {
        [lines removeObjectsInRange:NSMakeRange(0, lines.count - (NSUInteger)inMaxLines)];
    }
    return lines;
}

// Newest recent Oni-*.ips in ~/Library/Logs/DiagnosticReports, or nil.
// The process is named Oni, so macOS names its reports Oni-<date>.ips.
// Non-crash dirty exits (force-quit, SIGKILL) produce no report — the
// caller omits the drag-drop line in that case.
static NSURL *ONiCrash_NewestIPS(void)
{
    NSURL *dir = [NSURL fileURLWithPath:[NSHomeDirectory()
        stringByAppendingPathComponent:@"Library/Logs/DiagnosticReports"]];

    NSArray<NSURL *> *entries = [[NSFileManager defaultManager]
        contentsOfDirectoryAtURL:dir
        includingPropertiesForKeys:@[NSURLContentModificationDateKey]
        options:NSDirectoryEnumerationSkipsHiddenFiles
        error:NULL];

    NSURL *newest = nil;
    NSDate *newestDate = nil;
    for (NSURL *u in entries) {
        NSString *name = u.lastPathComponent;
        if (![name hasPrefix:@"Oni-"] || ![name hasSuffix:@".ips"]) {
            continue;
        }
        NSDate *mod = nil;
        [u getResourceValue:&mod forKey:NSURLContentModificationDateKey error:NULL];
        if (mod == nil ||
            -[mod timeIntervalSinceNow] > kOniCrashReportIPSMaxAge) {
            continue;
        }
        if (newestDate == nil || [mod compare:newestDate] == NSOrderedDescending) {
            newest = u;
            newestDate = mod;
        }
    }
    return newest;
}

// Percent-encode for a query VALUE: keep only RFC 3986 unreserved chars.
// NSURLQueryAllowedCharacterSet is wrong here — it permits '&' and '=',
// which delimit the parameters we're building.
static NSString *ONiCrash_Encode(NSString *s)
{
    static NSCharacterSet *unreserved;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSMutableCharacterSet *cs = [NSMutableCharacterSet alphanumericCharacterSet];
        [cs addCharactersInString:@"-._~"];
        unreserved = cs;
    });
    return [s stringByAddingPercentEncodingWithAllowedCharacters:unreserved];
}

// Compose the new-issue URL. Rebuilds with fewer log lines until the
// encoded whole fits the budget (the log tail is the only elastic part).
static NSURL *ONiCrash_BuildIssueURL(const char *inRendererName, NSURL *inIPS)
{
    NSString *bundleVersion = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    NSString *version = (bundleVersion.length > 0)
        ? bundleVersion : @ONI_VERSION;
    NSString *renderer = (inRendererName != NULL)
        ? [NSString stringWithUTF8String:inRendererName] : @"unknown";
    NSString *macos = [[NSProcessInfo processInfo] operatingSystemVersionString];

    NSString *title = [NSString stringWithFormat:
        @"Crash report: v%@ (%@)", version, renderer];

    NSArray<NSString *> *tail = ONiCrash_LogTail(kOniCrashReportLogLines);

    for (NSUInteger keep = (tail != nil) ? tail.count : 0; ; keep = keep / 2) {
        NSMutableString *body = [NSMutableString string];
        [body appendFormat:@"**Build:** v%@ (`%s`)\n", version, ONI_BUILD_STAMP];
        [body appendFormat:@"**Renderer (this launch):** %@\n", renderer];
        [body appendFormat:@"**macOS:** %@\n\n", macos];
        [body appendString:@"**What happened:** <!-- what were you doing when it "
                           @"crashed / didn't shut down? -->\n\n"];
        if (inIPS != nil) {
            [body appendFormat:@"**Crash report:** `%@` is selected in the Finder "
                               @"window that just opened — please drag that file "
                               @"onto this issue.\n\n", inIPS.lastPathComponent];
        } else {
            [body appendString:@"**Crash report:** no recent Oni-*.ips found in "
                               @"~/Library/Logs/DiagnosticReports (a force-quit "
                               @"leaves none).\n\n"];
        }
        if (keep > 0) {
            NSArray<NSString *> *kept =
                [tail subarrayWithRange:NSMakeRange(tail.count - keep, keep)];
            [body appendFormat:@"**Last %lu lines of startup.txt:**\n```\n%@\n```\n",
                (unsigned long)keep, [kept componentsJoinedByString:@"\n"]];
        }

        NSString *urlStr = [NSString stringWithFormat:@"%@?title=%@&body=%@",
            kOniNewIssueURL, ONiCrash_Encode(title), ONiCrash_Encode(body)];
        if (urlStr.length <= kOniCrashReportURLBudget || keep == 0) {
            return [NSURL URLWithString:urlStr];
        }
    }
}

// ----------------------------------------------------------------------
// Entry points
// ----------------------------------------------------------------------

void ONrCrashReport_CheckAndPromptAtStartup(
    const char *inSentinelPath,
    const char *inRendererName)
{
    @autoreleasepool {
        if (inSentinelPath == NULL || inSentinelPath[0] == '\0') {
            return;
        }
        strncpy(gOniCrashSentinelPath, inSentinelPath,
                sizeof(gOniCrashSentinelPath) - 1);
        gOniCrashSentinelPath[sizeof(gOniCrashSentinelPath) - 1] = '\0';

        if (access(gOniCrashSentinelPath, F_OK) != 0) {
            return;   // previous session ended cleanly (or first run)
        }

        // Consume the sentinel first: whatever happens in the dialog, this
        // launch must not re-prompt for the same dead session.
        unlink(gOniCrashSentinelPath);
        UUrStartupMessage("[crash-report] stale sentinel found — previous session did not shut down cleanly");

        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        if ([defaults boolForKey:kOniCrashReportDisabled]) {
            UUrStartupMessage("[crash-report] prompt suppressed by user preference");
            return;
        }

        NSURL *ips = ONiCrash_NewestIPS();

        ONiCrash_EnsureAppKit();

        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Oni didn't shut down cleanly last time";
        alert.informativeText = (ips != nil)
            ? @"It probably crashed — a crash report from macOS was found. "
              @"You can open a pre-filled bug report on GitHub; you'll see "
              @"exactly what it contains before submitting."
            : @"It may have crashed or been force-quit. You can open a "
              @"pre-filled bug report on GitHub; you'll see exactly what it "
              @"contains before submitting.";
        [alert addButtonWithTitle:@"Report on GitHub…"];
        [alert addButtonWithTitle:@"Show Logs"];
        [alert addButtonWithTitle:@"Dismiss"];
        alert.showsSuppressionButton = YES;
        alert.suppressionButton.title = @"Don't ask again after unclean shutdowns";

        NSModalResponse resp = [alert runModal];

        if (alert.suppressionButton.state == NSControlStateValueOn) {
            [defaults setBool:YES forKey:kOniCrashReportDisabled];
        }

        NSWorkspace *ws = [NSWorkspace sharedWorkspace];
        if (resp == NSAlertFirstButtonReturn) {
            // Report on GitHub… — compose page in the browser, and reveal the
            // .ips so attaching it is a single drag.
            NSURL *issue = ONiCrash_BuildIssueURL(inRendererName, ips);
            if (issue != nil) {
                [ws openURL:issue];
            }
            if (ips != nil) {
                [ws activateFileViewerSelectingURLs:@[ips]];
            }
            UUrStartupMessage("[crash-report] opened pre-filled GitHub issue%s",
                (ips != nil) ? " + revealed .ips" : " (no .ips found)");
        } else if (resp == NSAlertSecondButtonReturn) {
            // Show Logs — reveal the startup log in Finder.
            NSURL *log = [NSURL fileURLWithPath:
                [ONiCrash_StartupLogPath() stringByStandardizingPath]];
            [ws activateFileViewerSelectingURLs:@[log]];
            UUrStartupMessage("[crash-report] revealed logs in Finder");
        }
        // Dismiss — nothing beyond the (possibly set) suppression pref.
    }
}

void ONrCrashReport_MarkSessionActive(void)
{
    if (gOniCrashSentinelPath[0] == '\0') {
        return;
    }
    FILE *f = fopen(gOniCrashSentinelPath, "w");
    if (f != NULL) {
        // Content is for humans poking at App Support; only existence matters.
        fprintf(f, "pid=%d version=%s stamp=%s\n",
                (int)getpid(), ONI_VERSION, ONI_BUILD_STAMP);
        fclose(f);
    }
}

void ONrCrashReport_MarkCleanExit(void)
{
    if (gOniCrashSentinelPath[0] == '\0') {
        return;
    }
    unlink(gOniCrashSentinelPath);
}
