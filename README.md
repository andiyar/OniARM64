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

Binary lands at `build/bin/Oni`. Copy it into a directory that contains (or symlinks to) the game data and run it there:

```sh
cp build/bin/Oni /path/to/oni-data/
cd /path/to/oni-data
SDL_VIDEO_ALLOW_SCREENSAVER=1 ONI_AUTOSTART=1 ./Oni
```

- `SDL_VIDEO_ALLOW_SCREENSAVER=1` — belt-and-braces against leaked display-sleep assertions.
- `ONI_AUTOSTART=1` — skips the main menu and jumps straight to level 1. Deterministic repro while the HiDPI mouse-click bug is outstanding.

Sparse observability breadcrumbs are compiled in and write to `startup.txt` in the run directory. Crash reports land in `~/Library/Logs/DiagnosticReports/Oni-*.ips`.

## Scope

Intentionally minimal — the goal is a working port, not a remaster.

In scope for Anniversary Edition features:
- Developer mode
- Widescreen
- FPS smoothing
- Texture pack support

Out of scope:
- Multiplayer / netcode
- Modernized renderer / shaders
- New content or gameplay changes
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
