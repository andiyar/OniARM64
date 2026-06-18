// ======================================================================
// Oni_UpdateCheck_macOS.mm
//
// Cocoa shell for the GitHub release-update notifier (issue #40).
// Fetches https://api.github.com/repos/andiyar/OniARM64/releases, parses
// it into ONtReleaseInfo[], runs the pure ONi_UpdateCheck core, and shows
// a native NSAlert if a newer non-skipped release exists. State + throttle
// + opt-out in NSUserDefaults. Mirrors the Oni_DataSetup_macOS.mm pattern.
// ======================================================================
#import <Cocoa/Cocoa.h>

#include "Oni_UpdateCheck_macOS.h"
#include "ONi_UpdateCheck.h"

#include <string.h>

// Configure-time epoch (CMake string(TIMESTAMP ... %s)); 0 disables the
// dev-build guard but tag-difference still works.
#ifndef ONI_BUILD_EPOCH
#define ONI_BUILD_EPOCH 0
#endif

static NSString * const kOniUpdateDisabled   = @"OniUpdateDisabled";
static NSString * const kOniUpdateSkippedTag = @"OniUpdateSkippedTag";
static NSString * const kOniUpdateLastCheck  = @"OniUpdateLastCheck";

static const NSTimeInterval kOniUpdateThrottle   = 4 * 60 * 60; // 4 hours
static const NSTimeInterval kOniUpdateNetTimeout = 2.5;         // seconds
static const int            kOniUpdateMaxReleases = 16;

static NSString * const kOniReleasesAPI =
    @"https://api.github.com/repos/andiyar/OniARM64/releases?per_page=10";
static NSString * const kOniReleasesPage =
    @"https://github.com/andiyar/OniARM64/releases";

// Bring AppKit up once if SDL hasn't yet (returning users, where the data
// picker never ran). Mirrors ONrDataSetup_RunGuidedPicker; dispatch_once so
// a first-run launch (picker already finishLaunched) doesn't double-call.
static void ONiUpdate_EnsureAppKit(void)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app finishLaunching];
        [app activateIgnoringOtherApps:YES];
    });
}

// Copy an NSString into a fixed C buffer (always NUL-terminated).
static void ONiUpdate_CopyStr(char *dst, size_t cap, NSString *src)
{
    if (cap == 0) {
        return;
    }
    if (src == nil) {
        dst[0] = '\0';
        return;
    }
    const char *utf8 = [src UTF8String];
    if (utf8 == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, utf8, cap - 1);
    dst[cap - 1] = '\0';
}

// Synchronous, time-capped GET. Returns response data or nil on
// timeout/offline/non-200. Never blocks longer than kOniUpdateNetTimeout.
static NSData *ONiUpdate_Fetch(NSString *urlString)
{
    NSURL *url = [NSURL URLWithString:urlString];
    if (url == nil) {
        return nil;
    }

    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    [req setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];
    [req setValue:@"OniARM64-UpdateCheck" forHTTPHeaderField:@"User-Agent"];
    req.timeoutInterval = kOniUpdateNetTimeout;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block NSData *result = nil;

    NSURLSessionDataTask *task =
        [[NSURLSession sharedSession] dataTaskWithRequest:req
            completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                if (error == nil && data != nil &&
                    [response isKindOfClass:[NSHTTPURLResponse class]] &&
                    ((NSHTTPURLResponse *)response).statusCode == 200) {
                    result = data;
                }
                dispatch_semaphore_signal(sem);
            }];
    [task resume];

    dispatch_time_t deadline = dispatch_time(
        DISPATCH_TIME_NOW, (int64_t)(kOniUpdateNetTimeout * NSEC_PER_SEC));
    if (dispatch_semaphore_wait(sem, deadline) != 0) {
        [task cancel];   // hard cap: don't hold up launch
        return nil;
    }
    return result;
}

// Parse the releases JSON array into ONtReleaseInfo[]. Returns the count
// filled (<= cap), or 0 on malformed input. published_at via ISO8601.
static int ONiUpdate_Parse(NSData *json, ONtReleaseInfo *out, int cap)
{
    if (json == nil) {
        return 0;
    }
    id parsed = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
    if (![parsed isKindOfClass:[NSArray class]]) {
        return 0;
    }

    NSISO8601DateFormatter *fmt = [[NSISO8601DateFormatter alloc] init];

    NSArray *arr = (NSArray *)parsed;
    int n = 0;
    for (id obj in arr) {
        if (n >= cap) {
            break;
        }
        if (![obj isKindOfClass:[NSDictionary class]]) {
            continue;
        }
        NSDictionary *rel = (NSDictionary *)obj;

        ONtReleaseInfo info;
        memset(&info, 0, sizeof(info));

        NSString *tag  = rel[@"tag_name"];
        NSString *name = rel[@"name"];
        NSString *htmlURL = rel[@"html_url"];
        NSString *pub  = rel[@"published_at"];

        if (![tag isKindOfClass:[NSString class]] || tag.length == 0) {
            continue;   // a release with no tag is unusable
        }
        ONiUpdate_CopyStr(info.tag, ONcUpdate_TagMax, tag);

        // name falls back to tag for the dialog title.
        if ([name isKindOfClass:[NSString class]] && name.length > 0) {
            ONiUpdate_CopyStr(info.name, ONcUpdate_NameMax, name);
        } else {
            ONiUpdate_CopyStr(info.name, ONcUpdate_NameMax, tag);
        }

        if ([htmlURL isKindOfClass:[NSString class]]) {
            ONiUpdate_CopyStr(info.url, ONcUpdate_UrlMax, htmlURL);
        }

        if ([pub isKindOfClass:[NSString class]]) {
            NSDate *d = [fmt dateFromString:pub];
            if (d != nil) {
                info.published_at = (long long)[d timeIntervalSince1970];
            }
        }

        info.is_draft      = [rel[@"draft"] boolValue] ? 1 : 0;
        info.is_prerelease = [rel[@"prerelease"] boolValue] ? 1 : 0;

        out[n++] = info;
    }
    return n;
}

void ONrUpdateCheck_RunAtStartup(void)
{
    @autoreleasepool {
        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];

        // 1. Gate: opted out?
        if ([defaults boolForKey:kOniUpdateDisabled]) {
            return;
        }

        // 1b. Gate: throttled (checked within the last ~4h)?
        double last = [defaults doubleForKey:kOniUpdateLastCheck];
        if (last > 0.0) {
            NSTimeInterval since =
                [[NSDate date] timeIntervalSince1970] - last;
            if (since >= 0 && since < kOniUpdateThrottle) {
                return;
            }
        }

        // 2. Fetch (bounded). Offline / error -> silent, lastCheck NOT
        //    advanced so the next launch retries.
        NSData *json = ONiUpdate_Fetch(kOniReleasesAPI);
        if (json == nil) {
            return;
        }

        // 3. Parse.
        ONtReleaseInfo releases[kOniUpdateMaxReleases];
        int count = ONiUpdate_Parse(json, releases, kOniUpdateMaxReleases);

        // Successful fetch+parse attempt -> record the check time even if
        // we end up not prompting (up to date / skipped).
        [defaults setDouble:[[NSDate date] timeIntervalSince1970]
                     forKey:kOniUpdateLastCheck];

        if (count <= 0) {
            return;
        }

        // 4. Decide (pure core).
        int idx = ONrUpdateCheck_PickLatest(releases, count);
        if (idx < 0) {
            return;
        }
        const ONtReleaseInfo *latest = &releases[idx];

        NSString *myVersionNS = [[NSBundle mainBundle]
            objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
        const char *myVersion =
            (myVersionNS != nil) ? [myVersionNS UTF8String] : "";

        NSString *skippedNS = [defaults stringForKey:kOniUpdateSkippedTag];
        const char *skipped = (skippedNS != nil) ? [skippedNS UTF8String] : "";

        ONtUpdateVerdict verdict = ONrUpdateCheck_Decide(
            myVersion, (long long)ONI_BUILD_EPOCH, latest, skipped);

        if (verdict != ONcUpdate_Available) {
            return;
        }

        // 5. Prompt.
        ONiUpdate_EnsureAppKit();

        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithUTF8String:latest->name];
        alert.informativeText = [NSString stringWithFormat:
            @"You're running %s. Version %s is now available on GitHub.",
            (myVersion[0] != '\0') ? myVersion : "an older build",
            latest->tag];
        [alert addButtonWithTitle:@"Download…"];   // Download…
        [alert addButtonWithTitle:@"Skip This Version"];
        [alert addButtonWithTitle:@"Later"];
        alert.showsSuppressionButton = YES;
        alert.suppressionButton.title = @"Don't check for updates";

        NSModalResponse resp = [alert runModal];

        if (alert.suppressionButton.state == NSControlStateValueOn) {
            [defaults setBool:YES forKey:kOniUpdateDisabled];
        }

        if (resp == NSAlertFirstButtonReturn) {
            // Download… -> open the release page (fall back to releases list).
            NSString *urlStr = (latest->url[0] != '\0')
                ? [NSString stringWithUTF8String:latest->url]
                : kOniReleasesPage;
            NSURL *open = [NSURL URLWithString:urlStr];
            if (open != nil) {
                [[NSWorkspace sharedWorkspace] openURL:open];
            }
        } else if (resp == NSAlertSecondButtonReturn) {
            // Skip This Version -> persist the EXACT API tag. The pure core's
            // skip-check compares this raw tag with strcmp, so persisting
            // latest->tag verbatim (not a normalised form) is required for
            // the skip to match on the next launch.
            [defaults setObject:[NSString stringWithUTF8String:latest->tag]
                         forKey:kOniUpdateSkippedTag];
        }
        // Later -> nothing persisted beyond lastCheck; re-prompts next window.
    }
}
