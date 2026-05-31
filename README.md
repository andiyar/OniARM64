<div align="center">

<img src="docs/assets/oni-icon-256.png" alt="Oni" width="160" />

# OniARM64

*A native Apple Silicon port of Bungie's Oni (2001).*

![macOS](https://img.shields.io/badge/macOS-15%2B%20Sequoia-blue) &nbsp;
![arch](https://img.shields.io/badge/arch-ARM64-blue) &nbsp;
![status](https://img.shields.io/badge/status-playable--ish-green) &nbsp;
![type](https://img.shields.io/badge/type-fan%20port-orange)

</div>

---

## Why this exists

One of my favourite games from university, *Oni* is the action-brawler from Bungie/Take-Two in 2001 — third-person hand-to-hand and gunplay, Syndicate versus the TCTF and an intriguing story with amazing music. Spun off from Bungie as they were bought by Microsoft for Halo, the mac build (I've always been a Mac player!) was OS9 only, with MacOSX ports eventually from the amazing Omni group for PPC, and then by Feral Interactive for Intel. These were all 32-bit only, and when Apple deprecated 32 bit with Catalina, they stopped working.

Fast forward until recently and I discovered both the magic of Claude Code, and that there was, floating around on GitHub, forks of the Oni source code for windows from 2021. Two months later, lots of fiddling - I'm definitely not a programmer! - this is OniARM64, my attempt to create a vanilla Oni experience running on Apple Silicon. No Rosetta, 64 bit, no WINE required. No particular roadmap, things added as they are thought of and done.

Currently it's playable (I've run through the first 4 levels... too many times) and I figure, time to share! This is a personal project, as I love Oni, have played it too many times, and I want to keep playing it, so if you find bugs/issues please do advise, though time to fix/etc will be as life allows.

---

## Status

Levels 1–4 playable end-to-end — combat, AI, weapons, particle effects, audio, save/load all working. Downloadable and notarized .app in a DMG. List of stuff done / broken and fixed below. Issues tracking for interest are available, albeit it's more like Claude writing notes for Claude.

<details>
<summary><strong>Full milestone status</strong></summary>

### Phase 1 — Boot & init ✅
- [x] Builds as native ARM64 binary on Apple Silicon
- [x] All subsystems initialise end-to-end without SIGSEGV
- [x] Crash handler prevents zombie processes after a SIGSEGV

### Phase 2 — Render & UI ✅
- [x] Main menu renders and is interactive
- [x] HiDPI viewport scaling — game renders fullscreen, mouse aligned
- [x] Multi-frame rendering without geometry corruption
- [x] Characters render with correct bone transforms
- [x] In-game UI text renders without left-edge clipping

### Phase 3 — Level load & gameplay primitives ✅
- [x] Level 0 (main menu) loads and runs
- [x] Level 1 (tutorial / warehouse) loads from New Game
- [x] Movement (WASD / mouselook) works without crashing
- [x] Doors open in response to triggers
- [x] Trigger volumes fire scripted events
- [x] AI state machines run without crashing
- [x] Resolution / window-size persists across launches

### Phase 4 — Audio & effects ✅
- [x] Menu / cutscene / dialogue audio plays
- [x] Intro / outro cinematics play (native AVFoundation, replacing the dead Bink FMV path)
- [x] Footstep impact sounds play
- [x] Combat audio (gunfire, melee, weapon reloads) plays without lag — OpenAL buffer cache
- [x] Looping ambient sounds stop correctly (Daodan health / super ambients no longer leak)
- [x] Particle effects render — screamers, explosions, acid, environmental FX across levels 1–4
- [x] Security-laser tripwire beams render and trip.
- [ ] `w10_sni_p01` sniper particle fits its size class (non-blocking — class is dropped, game continues; see [#10](https://github.com/andiyar/OniARM64/issues/10)). WIP.

### Phase 5 — AI behaviour ✅
- [x] NPCs detect the player via sight and sound (Knowledge layer)
- [x] NPCs escalate alert → combat
- [x] AI combat behaviour fires (melee + ranged)
- [x] NPCs close distance to engage the player
- [x] Scripted NPC movement (patrol paths) executes
- [x] NPC-vs-NPC combat completes to first kill; surviving NPCs re-target

### Phase 6 — Gameplay completion
- [x] Konoko engages NPCs in combat end-to-end across a full encounter
- [x] Tutorial level completable to next-level transition
- [x] Save / load works across runs
- [x] Levels 2–4 playable with particle effects, combat, AI, level transitions
- [ ] All 14 levels playable - yet to test

### Phase 7 — Shippable artefact
- [x] `.app` bundle + Developer-ID code signing
- [x] Notarized + stapled DMG, Gatekeeper-clean, published to Releases
- [ ] Anniversary Edition stuff - HD compatibility (works but not well) and other ideas... very much TBD.

</details>

---

## Screenshots

<table>
<tr>
<td align="center"><img src="docs/assets/screenshots/gameplay-corridor.png" width="400" alt="Third-person gameplay: Konoko in a corridor facing an enemy, HUD visible" /></td>
<td align="center"><img src="docs/assets/screenshots/combat-syndicate.png" width="400" alt="Combat: two soldiers firing rifles" /></td>
</tr>
<tr>
<td align="center"><img src="docs/assets/screenshots/main-menu.png" width="400" alt="Oni main menu" /></td>
<td align="center"><img src="docs/assets/screenshots/level-select.png" width="400" alt="Load Game level-select dialog" /></td>
</tr>
</table>

---

## Get it running

### Download a build

1. Grab the latest `OniARM64.dmg` from [Releases](https://github.com/andiyar/OniARM64/releases).
2. Open the DMG and drag `OniARM64.app` onto the `Applications` shortcut.
3. Drop your Oni `GameDataFolder` at `~/Library/Application Support/OniARM64/gamedata/`).
4. Double-click `OniARM64.app` to launch (the build is signed and notarised).

### Build from source

```sh
cd build && cmake .. -DPlatform_SDL=ON && make -j8 oni_app
ln -sfn /path/to/your/Oni/GameDataFolder ~/Library/Application\ Support/OniARM64/gamedata
open build/bin/OniARM64.app
```

No Oni game data is included in the source or the app bundle. BYO game :).

---

## Contributing

Issues welcome, please upload crash reports or logs or relevant screenshots. Logs can be found in ~/Library/Logs/OniARM64/.

- [Open issues](https://github.com/andiyar/OniARM64/issues)
- [Development history (HISTORY.md)](HISTORY.md)

---

## Credits

- **Bungie** — original game (2001)
- **The Omni Group** — 2001–2007 PowerPC OS X port
- **[Feral Interactive](https://www.feralinteractive.com/)** — 2008–2015 Intel macOS port (Oni 1.1 Intel → 1.2 → 1.2.1).
- **[hogsy/OniFoxed](https://github.com/hogsy/OniFoxed)** — upstream fork.
- **[Iritscen](https://iritscen.oni2.net/)** for the icns file and and mai— Anniversary Edition project,
- **[Oni Mod Depot](https://mods.oni2.net/)** for textures, mods, ideas
- **[oni2.net community](https://oni2.net/)** — OniSplit / OUP / Daodan reverse engineering. `wiki.oni2.net` / `oni.bungie.org` forums and the anniversary edition work / Team Chrysalis :)

---

## Bundled third-party software

The downloadable `.app` ships with these libraries in `Contents/Frameworks/`, each ad-hoc re-signed alongside the binary:

| Component | License | Project |
| --- | --- | --- |
| SDL2 | Zlib | [libsdl.org](https://libsdl.org) |
| FFmpeg (`libavcodec` + `libavutil`, ADPCM_MS decoder only) | LGPL-2.1-or-later | [ffmpeg.org](https://ffmpeg.org) |

FFmpeg is built from source via `scripts/build-ffmpeg.sh` with `--disable-gpl --disable-nonfree --disable-everything --enable-decoder=adpcm_ms` and a long list of explicit `--disable-*` flags — only the minimum needed to decode Oni's Microsoft-ADPCM-encoded sound data. None of x264, x265, libvpx, dav1d, SVT-AV1, mp3lame, Opus, OpenSSL, or any other Homebrew transitive dep ends up in the bundle. Full license texts are in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) and shipped as `Contents/Resources/THIRD_PARTY_LICENSES.txt` inside the bundle.

The `Build from source` path picks up the same minimal ffmpeg from `extern/ffmpeg/` (built by the script on first run), with a fallback to Homebrew's `ffmpeg` if `extern/ffmpeg/` doesn't exist — handy for quick dev iteration where the bundle isn't being produced.

---

<sub><em>Oni © 2001 Bungie / Take-Two Interactive. Not affiliated with Bungie or Take-Two.</em></sub>
