# OniARM64

Native ARM64 / Apple Silicon port of Oni (Bungie, 2001).

## Status (2026-05-21)

**Phase 5 done. Phase 6 advancing.** Session 27: cheats and developer
access usable from a fresh save (commit `f9cb27c`); hi-res console fix
lifted from Daodan (`eea154a`); level 2 cinematic + combat playthrough
verified under LLDB (using the now-unlocked `winlevel` cheat and a
dev-mode level-select menu to skip there). Tutorial completes
(session 26), level 2 plays through (session 27) — gameplay reach is
now two levels deep with cheats enabling deeper testing for the rest of
Phase 6.

**Community-SVN audit completed.** Five-agent parallel audit of
`/Users/andiyar/Developer/oni/community-svn/` documented at
`docs/community-svn-audit-2026-05-21.md`. Three references are now
authoritative second-sources for our work: OniSplit's
`InstanceMetadata.cs:2613-2707` (canonical 32-bit wire format for ~99
templates), OUP's `structdefs/*.txt` (independent RE with engineer
comments, 122-template superset), Daodan's `Patches/*.c` (engine logic
patches portable to our tree, of which Cheater.c and the console fix
have now been lifted). CLAUDE.md updated to point future sessions at
the new tree.

**Bug C symptom confirmed.** The latent particle-loader 64-bit bridge
bug from the path-to-playable spec fired its documented signature in
the session-27 LLDB log:
`Particle class 'w10_sni_p01' is too large (268) for largest size class
(256)!`. Twice on level 2 load, game continued. Same particle name as
the spec — the drafted-but-unlanded bridge from session 12 is still
the right artifact to investigate when audio (Bug A, `c039fa5`) is
revisited. Audio and Bug C must land paired (see
`feedback_cascade_pattern`).

**Text clipping is also fixed.** Session 26 follow-up (`fix(64bit):
bridge embedded ONtIGUI_FontInfo alignment in IGSt template`, commit
`9821d11`) closed the Phase 2 left-edge text clip. Same bug class as
session 25's AKVA fix, applied to the `'IGSt'` template — embedded
`ONtIGUI_FontInfo` has alignment 8 on 64-bit (it contains a
`TStFontFamily*`), the bridge walker missed the 4-byte trailing pad, so
the first 4 bytes of every on-disk string landed in C's font_info pad
slot and disappeared. User-verified: full text in diary, weapon info
panel, encrypted-message screen, tutorial popups — `"BALLISTIC AMMO"`,
`"Coordinating the arms..."`, `"Reloading a weapon takes time:"`, and
everything else now intact.

**One known bug remains:**
- **Security-laser tripwire beams** don't render in the tutorial laser
  room: wall-mounted projector hardware (the three-rail emitter mounts)
  renders correctly, but the beams between paired emitters are missing.
  Strong hypothesis: same embed-struct bridge-alignment bug class
  again — third instance, this time in whichever env-effect /
  particle-class template defines the laser visual. The 112 still-
  unaudited templates are the suspect pool.

Two embed-struct bridge-alignment fixes have now landed in the same
post-pass (`iFixupEmbeddedStructAlignment` in `BFW_TM_Bridge.c`): AKVA
(session 25, commit `6c030e0`) and IGSt (session 26, commit `9821d11`).
Two instances of the same class, two completely different visible
symptoms (silent AI movement failure vs silent text clipping). The
remaining ~112 templates still need an audit.

**Open audit still pending:** PHtRoomData is the only known case of an
embedded multi-pointer tm_struct in this codebase, but a systematic
sweep of the 114 templates for similar patterns is a follow-up. The
missing-laser bug is the first concrete reason to do that audit.

Most fixes chase 32→64 bit arithmetic that was correct on Bungie's
original 32-bit target but breaks now. Common patterns:
- Unsigned-index underflow that used to wrap mod 2³² to benign heap
  padding, now goes ~48 GB into unmapped space.
- Encoded indices (top-bit flags) dereferenced without masking.
- Ceiling-divide loops without a paired remainder-loop that worked
  because the extra reads fell in 32-bit heap slack.
- `UUtUns32` callback userdata that silently truncates passed-in
  pointers — any heap address above 4 GB (i.e. all of them on Apple
  Silicon) loses its upper bits.
- **Silent error macros: `AI2_ERROR_REPORT=0` in release builds routes all
  AI2_ERROR calls to a no-log handler. Absence of an error in startup.txt
  proves nothing on its own.**

Audio works (menu music, cutscene dialogue). Phases 0–2 complete.

Most fixes chase 32→64 bit arithmetic that was correct on Bungie's
original 32-bit target but breaks now. Common patterns:
- Unsigned-index underflow that used to wrap mod 2³² to benign heap
  padding, now goes ~48 GB into unmapped space.
- Encoded indices (top-bit flags) dereferenced without masking.
- Ceiling-divide loops without a paired remainder-loop that worked
  because the extra reads fell in 32-bit heap slack.
- `UUtUns32` callback userdata that silently truncates passed-in
  pointers — any heap address above 4 GB (i.e. all of them on Apple
  Silicon) loses its upper bits.

## Milestones

### Phase 1 — Boot & init ✅
- [x] Builds as native ARM64 binary on Apple Silicon
- [x] All subsystems initialise end-to-end without SIGSEGV
- [x] Crash handler prevents zombie processes after a SIGSEGV

### Phase 2 — Render & UI
- [x] Main menu renders and is interactive
- [x] HiDPI viewport scaling — game renders fullscreen, mouse aligned
- [x] Multi-frame rendering without geometry corruption (Bug B closed, session 19)
- [x] Characters render with correct bone transforms (no levitation, no stretched joints — endian fix landed session 20)
- [x] In-game UI text renders without left-edge clipping — fixed session 26 by bridging embedded ONtIGUI_FontInfo alignment in IGSt template (BFW_TM_Bridge.c). User-verified: diary screen, weapon info panel, tutorial popups all render full text ("BALLISTIC AMMO", "Coordinating the arms...", "Reloading a weapon takes time", etc.).

### Phase 3 — Level load & gameplay primitives
- [x] Level 0 (main menu) loads and runs
- [x] Level 1 (tutorial / warehouse) loads from New Game
- [x] Movement (WASD / mouselook) works without crashing
- [x] Doors open in response to triggers
- [x] Trigger volumes fire scripted events
- [x] AI state machines run without crashing
- [x] Resolution / window-size persists across launches

### Phase 4 — Audio & effects
- [x] Menu / cutscene / dialogue audio plays
- [x] Footstep impact sounds play (impact-effect on-disk bridge, session 23)
- [ ] Particle classes load without size-class overflow (`w10_sni_p01 is too large (268)` warning still latent)
- [ ] Security-laser tripwire beams render in the tutorial level (session 26: wall-mounted projectors render, but the actual beams between them don't — emitter hardware visible, beam visual missing. Likely the same embed-struct bridge bug class as session 25, applied to an env-effect / particle-class template).

### Phase 5 — AI behaviour ✅
- [x] NPCs detect the player via sight and sound (Knowledge layer)
- [x] NPCs escalate alert → combat correctly when player is in central vision (session 24, verified end-to-end through `Combat_Enter`)
- [x] AI combat behaviour fires (melee + ranged both work end-to-end once Combat_Enter happens)
- [x] **NPCs close distance to engage the player** — fixed in session 25 by bridging embedded-PHtRoomData alignment in the 32→64 template-instance walker (BFW_TM_Bridge.c). User-verified: NPC walked over and attacked.
- [x] Scripted NPC movement (walk-into-room patrol paths) executes — verified session 25: an NPC followed its patrol route, turned around, detected the player on sight, and pursued up a flight of stairs. Same AKVA bridge fix covers this path.
- [x] NPC-vs-NPC combat completes to first kill and surviving NPCs re-target — verified session 26 in tutorial-level playthrough.

### Phase 6 — Gameplay completion
- [x] **Konoko can engage NPCs in combat end-to-end across a full encounter** — verified session 26: user played the full tutorial level, weapons + melee + Konoko-vs-NPC combat all worked through to level exit.
- [x] **Tutorial level completable to next-level transition** — verified session 26: tutorial completed, next level loaded successfully (no crash on level boundary — first time we've ever crossed one mid-gameplay).
- [ ] Save / load works across runs
- [ ] All 14 levels playable

### Phase 7 — Shippable artefact
- [ ] `.app` bundle + code signing
- [ ] Anniversary Edition fixes (dev mode, widescreen, FPS smoothing, texture packs — scope capped there)

## Milestone log

Selected dated achievements, newest first. Full per-commit narrative in [HISTORY.md](HISTORY.md). Active bugs and investigations in [GitHub Issues](https://github.com/andiyar/OniARM64/issues).

- **2026-05-22 — Session 28** — Issue tracker established on `andiyar/OniARM64` (13 issues, 10 labels). CLAUDE.md updated with issue-tracking workflow contract. Five passes of gunfire-lag instrumentation gating reduced log volume but did not resolve user-perceived lag; architectural follow-up filed as #9.
- **2026-05-21 — Session 27** — Cheats usable from a fresh save, hi-res console fix lifted from Daodan, community-svn audit completed (Daodan / OniSplit / OUP added as authoritative second-source references). Level 2 cinematic + combat playthrough verified under LLDB.
- **2026-05-20 — Session 26** — **Tutorial level completed end-to-end. Phase 5 done, Phase 6 first ticks.** First time the port has crossed a level boundary mid-gameplay. Text clipping fixed (IGSt embed-struct bridge — second instance of session 25's bug class).
- **2026-05-20 — Session 25** — **Pursuit movement fixed.** Root cause: embedded `PHtRoomData` 8-alignment gap in the 32→64 template bridge, addressed via `iFixupEmbeddedStructAlignment` post-pass on AKVA. NPCs now close distance and engage. Bug class identified; ~112 other templates remain unaudited ([#4](https://github.com/andiyar/OniARM64/issues/4)).
- **2026-05-19 — Session 23** — Footsteps audible. Impact-effect on-disk struct bridge landed for ARM64 8-byte pointer fields.
- **2026-05-04 — Session 21** — AI combat crash fixed (`AI2rCombat_NotifyKnowledge` pointer truncation through `inParam2`).
- **2026-05-03 — Session 20** — NPC pathfinding crash fixed, character sort crash fixed (`AUrQSort_32` replaced with `qsort` for pointer arrays at character distance-sort site), HiDPI viewport scaling, body horror fixed (PowerPC endian guard mis-applied to ARM64).
- **2026-05-02 — Session 19** — **Bug B fixed (character geometry crash).** Over-allocated `textureCoords` scratch stops the env-clipper stomping template memory.
- **2026-04-27 — Session 17** — **Phase 2 callback-truncation sweep complete.** `UUtUns32 inUserData → uintptr_t` across ~80 sites in 43 files. Level 1 load now completes.
- **2026-04-27 — Session 16** — **Phase 1 complete: audio works end-to-end.** Menu music plays cleanly; root cause was decoder dispatch by wrong format flag.
- **2026-04-26 — Session 15** — Bug A (`.sep` → `.raw` fallback) re-applied; audio reaches speakers ("music underneath static").
- **2026-04-25 — Session 13** — Phase 0 banked (Options dialog + Bug C bridge inert + path-to-playable spec committed).
- **2026-04-24 — Session 10** — **Gameplay-drivable.** Walks the warehouse, mouselook, stairs.
- **2026-04-24 — Session 9** — **First gameplay frame on screen.** Env-clipper over-allocation fixed.
- **2026-04-22 → earlier (sessions 1–8)** — TM bridge, 32→64 struct layout bridging at level-load, OpenAL channel-count init, memory allocator pointer-width fixes.

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

## Origin

Originally forked from [hogsy/OniFoxed](https://github.com/hogsy/OniFoxed), which derives from the 2001 Oni source release. Divergence is significant at this point — the Window Manager message API, the Template Manager bridge layer, the memory allocator, the OpenAL sound init, and numerous 64-bit-pointer sites are all rewritten. This fork is not tracking upstream.
