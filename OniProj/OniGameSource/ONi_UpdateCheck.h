// ======================================================================
// ONi_UpdateCheck.h
//
// Pure, dependency-free decision core for the GitHub release-update
// notifier (issue #40). No Cocoa, no network, no date parsing — the
// Cocoa shell (Oni_UpdateCheck_macOS.mm) fetches releases, parses JSON
// and ISO8601 timestamps, fills ONtReleaseInfo[], then calls these two
// functions to decide whether to prompt. Unit-tested standalone.
// ======================================================================
#ifndef ONI_UPDATECHECK_H
#define ONI_UPDATECHECK_H

#ifdef __cplusplus
extern "C" {
#endif

#define ONcUpdate_TagMax    64
#define ONcUpdate_NameMax   160
#define ONcUpdate_UrlMax    512

typedef enum {
    ONcUpdate_None      = 0,   // up to date, can't tell, or skipped
    ONcUpdate_Available = 1    // a newer, non-skipped release exists
} ONtUpdateVerdict;

typedef struct {
    char      tag[ONcUpdate_TagMax];    // e.g. "v1.3.0r4"
    char      name[ONcUpdate_NameMax];  // display name (caller falls back to tag)
    char      url[ONcUpdate_UrlMax];    // release html_url to open
    long long published_at;             // epoch seconds (filled by the .mm)
    int       is_draft;                 // 1 = draft (always excluded)
    int       is_prerelease;            // recorded; v1 includes prereleases
} ONtReleaseInfo;

// Pick the newest non-draft release by published_at. Returns its index,
// or -1 if the list is empty / all drafts / releases is NULL. v1 channel
// policy lives here: is_prerelease is ignored (all included).
int ONrUpdateCheck_PickLatest(const ONtReleaseInfo *releases, int count);

// Pure decision. my_build_time == 0 disables the dev-build guard.
ONtUpdateVerdict ONrUpdateCheck_Decide(
    const char           *my_version,    // CFBundleShortVersionString, e.g. "1.3.0r3"
    long long             my_build_time, // compile-time epoch, 0 = unknown
    const ONtReleaseInfo *latest,        // result of PickLatest, or NULL
    const char           *skipped_tag);  // "" or NULL if none

#ifdef __cplusplus
}
#endif

#endif // ONI_UPDATECHECK_H
