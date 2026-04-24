# OniARM64

Native ARM64 / Apple Silicon port of Oni (Bungie, 2001).

## Status (2026-04-24)

Gameplay rendering is live. Player spawns into level 1 (warehouse),
first-person camera renders, HUD draws, walking / mouselook / stairs
all work.

Interactive systems are **not** live though — the player walks through
doors instead of opening them, which means the object / trigger system
isn't firing. Strong suspicion: the `UUtUns32 inUserData` truncation
backlog (`OT_Door.c`, `OT_Trigger.c`, `OT_TriggerVolume.c`, `OT_Combat.c`,
`Oni_AI2*.c`, `Oni_Character.c`). Every heap address on Apple Silicon is
above 4 GB, so those callbacks receive garbage the first time they fire,
and the affected subsystems silently never register or never hit their
branches — no door collide, no trigger volume, no AI state transitions.

Character animation is also visibly broken ("bone horror" — joints
stretched, character levitates). Likely connected to the same class —
AI state-machine callbacks almost certainly drive animation-state
selection.

Crashes still hit the per-frame draw path on some geometries after
several seconds of play (`MSrTransform_Geom_FaceNormalToWorld` remnant
after today's floor-div fix) — also being chased.

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

- [x] Build as ARM64 binary
- [x] All subsystem init runs end-to-end
- [x] `loading level 0…` reaches the template-manager bridge
- [x] Main menu renders
- [x] `New Game` → level 1 load completes, splash clears
- [x] First gameplay frame on screen (warehouse, textures, HUD, player)
- [x] Multi-frame rendering without AKOT corruption
- [x] Movement (WASD / mouselook) doesn't instantly SIGSEGV
- [x] Crash-handler prevents UE-zombie processes after SIGSEGV (no more daily reboots)
- [ ] Remaining per-frame draw-path crashes (whatever surfaces after decal / BSP / block8 fixes)
- [ ] Doors open instead of clipped-through (→ `OT_Door` callback truncation sweep)
- [ ] Triggers / trigger volumes fire (→ `OT_Trigger`, `OT_TriggerVolume` callback truncation sweep)
- [ ] AI state machines run (→ `Oni_AI2*.c`, `OT_Combat.c` callback truncation sweep — probably unblocks bone-horror too)
- [ ] Character animation: bone transforms correct, no levitation / stretched joints
- [ ] **Audio actually plays** — `SSiSoundChannels_Initialize` runs without crashing since the session-2 fix, but we have never heard a sound. Music, footsteps, ambience, UI clicks: all silent so far. Unknown whether it's a routing / output-device / format bug, or just never being triggered because the event system doesn't fire (see callback-truncation sweep).
- [ ] HiDPI window mapping: 640×480 render in the bottom-left of 2K display is a Retina backing-scale mismatch
- [ ] `.app` bundle + code signing
- [ ] Anniversary Edition fixes (dev mode, widescreen, FPS smoothing, texture packs — scope capped there)

## Rolling timeline (newest first)

### 2026-04-24 — Session 10: gameplay-drivable
- Flagged audio as untested — OpenAL `SSiSoundChannels_Initialize` has run cleanly since the session-2 channel-count-init fix, but we have never actually heard sound play through many sessions of debugging. No commit for this; added to the milestones so it doesn't keep slipping off the list.
- Removed inherited `.github/workflows/cmake.yml` — built Windows/MSVC + MinGW-cross-to-Windows targets, both of which are out of scope for this fork. CI was red on every push because our macOS/ARM64 port work is orthogonal to those jobs, generating email spam for no signal. A macOS-ARM64 CI workflow can be written fresh later.
- `e8c85d7` SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT handler calls `SDL_Quit()` then re-raises. Stops new crashes from creating unkillable `UE`-state zombie processes (macOS kernel pins the process on driver teardown otherwise). Existing zombies still need one reboot to clear.
- `64e0e68` `MS_Geom_Transform.c`: `block8 = (numX + 7) >> 3` (ceiling) made the main loop overshoot vertex / normal arrays by up to 7 slots while the remainder loop never ran. Switched to floor.
- `de9fdcf` `BFW_Akira.c` `ARiPointInBSP`: `inPlaneEquArray[curNode->planeEquIndex]` dereferenced the raw encoded index without `AKmPlaneEqu_GetIndex()` masking. On 64-bit `base + 0x80000000 × 16` doesn't wrap, so it indexed 32 GB off into unmapped space.
- `816314b` `BFW_Decal.c` `P3iDecal_ClipToPlane`: empty input buffer caused `num_points - 1` to underflow an unsigned 32-bit to `0xFFFFFFFF`. Multiplied by `sizeof(M3tPoint3D) = 12`, this yields a ~48 GB forward offset that no longer wraps. Added a zero-check at function entry.
- User result: walked around, mouselooked, went up stairs. Walks *through* doors rather than opening them — interactive object / trigger system isn't firing. Strong candidate: the `UUtUns32 inUserData` callback-truncation class (`OT_Door`, `OT_Trigger`, `OT_TriggerVolume`, `Oni_AI2*`, `OT_Combat`). Crash moved further into `MSrTransform_Geom_FaceNormalToWorld` on a later character geometry — floor-div fix didn't cover every overshoot path.

### 2026-04-24 — Session 9: first gameplay frame
- `103496b` env-clipper over-allocates `gqVertexData.textureCoords`. The software env clipper was writing clip-vertex UVs past the exact-sized env buffer; on ARM64 the overflow landed on `AKtOctTree->interiorNodeArray` and corrupted it. Scratch copy sized `numTextureCoords + M3cExtraCoords` fixes it.
- `3e00c86` narrowing tripwires that located the stomp site.
- `ONI_AUTOSTART=1` env-var path in `Oni_Windows2.c` to skip the main menu while the HiDPI mouse bug makes clicks non-deterministic.
- First gameplay frame on screen — level loads, warehouse textures render, HUD and player visible.

### 2026-04-22 → earlier (sessions 1–8)
Template-manager 64-bit bridge, 32→64 struct layout bridging at level-load, `tm_raw` pointer resolution in `BFW_Totoro.c`, SIGBUS walks in instance-descriptor traversal, camera / AKOT corruption bisection, OpenAL channel-count init, memory allocator pointer-width fixes. See `docs/handoff-2026-04-*.md` for session-by-session narrative.

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
