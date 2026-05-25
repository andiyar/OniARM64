<!--
RELEASE NOTES TEMPLATE — fill in <PLACEHOLDERS> and pass to `gh release create`.

Usage:
    cp macos/RELEASE_NOTES_TEMPLATE.md /tmp/oni-release-notes.md
    # edit /tmp/oni-release-notes.md, fill in the placeholders, append a
    # new line to ## Version history (don't rewrite the existing entries),
    # update ## What works for anything new
    gh release create v<VERSION> \
        --repo andiyar/OniARM64 \
        --title "Oni v<VERSION> — <SHORT-TAGLINE>" \
        --notes-file /tmp/oni-release-notes.md \
        --prerelease \
        build/OniARM64.dmg

Keep the notes short. The README is the place for narrative; release notes are
"here's what's in this build, here's what's new, here's how to get going."
-->

<NTH> public <BUILD-STAGE> build of OniARM64, an Apple Silicon native port of Oni.

### Version history

- **1.1 PPC** (2001) — Bungie
- **1.0 v1.36** (2003) — The Omni Group
- **1.1 Intel → 1.2 → 1.2.1** (2011–2015) — Feral Interactive
- **1.3.0a1** (2026) — OniARM64 (andiyar + Claude Opus)
- **<NEW-VERSION>** (<YEAR>) — <ONE-LINE-WHAT-CHANGED>

> **Note:** This is <a/an> <BUILD-STAGE> build. <ONE-LINE-PRIMARY-GOAL-OR-SCOPE>.

### You need

- An Apple Silicon Mac
- macOS <MIN-TESTED-VERSION> or newer
- A legitimate copy of Oni to provide game data (`GameDataFolder`)

### Install

1. Download `OniARM64.dmg` below.
2. Open the DMG and drag `OniARM64.app` onto the `Applications` shortcut.
3. Drop your Oni `GameDataFolder` at `~/Library/Application Support/OniARM64/gamedata/` (or symlink it).
4. Double-click to launch.

### What works

- Native ARM64 binary, compile and boot
- HiDPI fullscreen rendering
- <CURRENT-PLAYABLE-LEVEL-RANGE> playthrough — combat, AI, weapons, doors, save/load
- Audio — music, dialogue, cutscenes, sound FX
- <ANY-NEW-CAPABILITY-THIS-RELEASE>

### What doesn't work

Anything untested cannot be guaranteed.

<KNOWN-BLOCKING-ISSUES-IF-ANY>

### Bug reports

[Open an issue](https://github.com/andiyar/OniARM64/issues). Crash reports land at `~/Library/Logs/DiagnosticReports/Oni-*.ips` — please attach the most recent.

### With thanks

Bungie · The Omni Group · Feral Interactive · [hogsy/OniFoxed](https://github.com/hogsy/OniFoxed) (forked from) · [Iritscen](https://iritscen.oni2.net/) and the AE team · the [oni2.net community](https://oni2.net/).
