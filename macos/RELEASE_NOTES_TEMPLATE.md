<!--
RELEASE NOTES TEMPLATE — fill in <PLACEHOLDERS> and pass to `gh release create`.

Usage:
    cp macos/RELEASE_NOTES_TEMPLATE.md /tmp/oni-release-notes.md
    # edit /tmp/oni-release-notes.md: set <VERSION>, list what's new this build,
    # update the scope note. Keep it short.
    gh release create v<VERSION> \
        --repo andiyar/OniARM64 \
        --title "Oni <VERSION> — <SHORT-TAGLINE>" \
        --notes-file /tmp/oni-release-notes.md \
        --prerelease \
        build/OniARM64.dmg

Release notes are "what's new + how to get going." The README carries the
narrative, the full milestone list, and the credits.
-->

Public preview build of OniARM64, an Apple Silicon native port of Oni.

**What's new in <VERSION>**

- <ONE LINE PER CHANGE THIS BUILD>

> **Note:** This is a preview build. <SCOPE — e.g. levels 1–4 play end-to-end; later levels untested.>

### You need

- An Apple Silicon Mac
- macOS 15 (Sequoia) or newer
- Your own copy of Oni to provide game data (`GameDataFolder`), Mac or Windows data both work

### Install

1. Download `OniARM64.dmg` below.
2. Open the DMG and drag `OniARM64.app` onto the `Applications` shortcut.
3. Double-click `OniARM64.app`. On first launch it prompts you to locate your Oni `GameDataFolder` and copies it into place for you.

### Bug reports

[Open an issue](https://github.com/andiyar/OniARM64/issues). Crash reports land at `~/Library/Logs/DiagnosticReports/Oni-*.ips`; please attach the most recent.
