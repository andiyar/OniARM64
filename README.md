# OniARM64

Native ARM64 / Apple Silicon port of [Oni](https://en.wikipedia.org/wiki/Oni_(video_game)) (Bungie, 2001). Originally forked from [hogsy/OniFoxed](https://github.com/hogsy/OniFoxed), which derives from the 2001 Oni source release. Divergence is significant — the Window Manager message API, Template Manager bridge layer, memory allocator, OpenAL sound init, and numerous 64-bit-pointer sites have all been rewritten. This fork is not tracking upstream.

## Requirements

- Apple Silicon Mac (M-series)
- macOS 13+ (Ventura / Sonoma / Sequoia)
- Homebrew with `cmake` and `sdl2`
- A legitimate copy of Oni. This repo contains **no game assets** — you'll need the original `.dat` / `.raw` / `.sep` data files from your own install (Anniversary Edition, a Windows CrossOver / Wine prefix, or the retail CD).

## Build

```sh
cd build
cmake .. -DPlatform_SDL=ON
make -j8
```

Binary lands at `build/bin/Oni`. From there you have two ways to run it.

### Option A — bare binary

Copy the binary into a directory that contains (or symlinks to) a `GameDataFolder` and run it there:

```sh
cp build/bin/Oni /path/to/oni-data/
cd /path/to/oni-data
SDL_VIDEO_ALLOW_SCREENSAVER=1 ONI_AUTOSTART=1 ./Oni
```

State files (`persist.dat`, `key_config.txt`) land next to the binary; `startup.txt` likewise. This is the historical dev workflow and remains the fastest inner loop.

### Option B — clickable `.app` bundle

```sh
make oni_app                                                       # produces build/bin/OniARM64.app
ln -sfn /path/to/your/Oni/GameDataFolder \
    build/bin/OniARM64.app/Contents/Resources/gamedata             # one-time data hookup
xattr -d com.apple.quarantine build/bin/OniARM64.app 2>/dev/null   # one-time Gatekeeper bypass
open build/bin/OniARM64.app
```

`make oni_app` runs `macos/build-bundle.sh` which copies the binary, templates, assets, and every Homebrew dylib (direct + transitive — SDL2, ffmpeg + 12 of its deps) into the bundle, then re-signs everything ad-hoc so dyld accepts it.

Binary-swap inner loop for everyday dev:

```sh
make -j8 && cp build/bin/Oni build/bin/OniARM64.app/Contents/MacOS/Oni && \
    codesign --force --sign - build/bin/OniARM64.app/Contents/MacOS/Oni && \
    open build/bin/OniARM64.app
```

Re-run `make oni_app` only when the binary picks up new Homebrew deps (rare).

Under the `.app` workflow, files land at macOS-conventional locations:

| File | Location |
| --- | --- |
| Game data lookup | `~/Library/Application Support/OniARM64/gamedata/` → `<bundle>/Contents/Resources/gamedata/` → legacy cwd-relative search |
| `persist.dat`, `key_config.txt` | `~/Library/Application Support/OniARM64/` (cwd-relative if it already exists, else here) |
| `startup.txt`, `debugger.txt` | `~/Library/Logs/OniARM64/` (cwd-relative if writable, else here) |
| Crash reports | `~/Library/Logs/DiagnosticReports/Oni-*.ips` (macOS default) |

### Common to both

- `SDL_VIDEO_ALLOW_SCREENSAVER=1` — belt-and-braces against leaked display-sleep assertions.
- `ONI_AUTOSTART=1` — skips the main menu and jumps straight to level 1.

Crash reports land in `~/Library/Logs/DiagnosticReports/Oni-*.ips`.

## Scope

Intentionally minimal — the goal is a working port, not a remaster.

In scope for Anniversary Edition features:
- Developer mode
- Widescreen
- FPS smoothing
- Texture pack support

Out of scope:
- Multiplayer / netcode
- Modernized renderer / shaders **TBD**
- New content or gameplay changes **TBD**
- Windows / Linux build targets (handled by upstream / other forks)

## Milestones

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

### Phase 4 — Audio & effects
- [x] Menu / cutscene / dialogue audio plays
- [x] Footstep impact sounds play
- [ ] Particle classes load without size-class overflow
- [ ] Security-laser tripwire beams render in the tutorial level

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
- [ ] Save / load works across runs
- [ ] All 14 levels playable

### Phase 7 — Shippable artefact
- [ ] `.app` bundle + code signing
- [ ] Anniversary Edition fixes (dev mode, widescreen, FPS smoothing, texture packs)

## Development

- **Per-commit development log:** [HISTORY.md](HISTORY.md)
- **Active bugs and investigations:** [GitHub Issues](https://github.com/andiyar/OniARM64/issues)
