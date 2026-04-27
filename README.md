# OniARM64

Native ARM64 / Apple Silicon port of Oni (Bungie, 2001).

## Status (2026-04-26)

Phase 0 of the path-to-playable spec landed (Steps 0.1 and 0.2): Options
dialog opens cleanly on macOS, and a 64-bit particle-loader bridge sits
inert in tree ready to pair with the Bug A audio fix. Step 0.3 (env-var
resolution override) is deferred to Phase 4 — the architecture map shows
five coordinate-space layers need aligning, not the one-liner the spec
assumed. Phase 1 starts next session: re-apply Bug A paired with the
particle bridge, chase whichever crash cascades out, fix audio init
ordering, ship menu music as the first user-visible audio.

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
- [x] **Audio actually plays** — Bug A diagnosed in session 12: shipping data has no `.sep` files, so both `BDiBinaryData_ProcHandler` (BINA) and `OSiBinaryData_ProcHandler` (OSBD, audio) silently no-op. Fix verified working in isolation (`c039fa5`, reverted in `7e51a55`) — but unblocks Bug C below. Land paired.
- [ ] **Bug C — particle loader 64-bit bridge gap** — `P3rLoad_PostProcess` SIGBUSes during 64-bit bridge of `P3tParticleDefinition`. KERN_PROTECTION_FAILURE at `0x19c8d9cb04` inside `P3rTraverseVarRef+2252` ← `P3rPackVariables+1624` ← `P3iProcessParticleClass+368`. Latent the whole port; only became reachable when Bug A unblocked the dispatch chain. Files: `BFW_Particle/BFW_Particle3.c`, `BFW_Headers/BFW_Particle3.h`. Must land paired with Bug A.
- [ ] HiDPI window mapping: 640×480 render in the bottom-left of 2K display is a Retina backing-scale mismatch
- [ ] `.app` bundle + code signing
- [ ] Anniversary Edition fixes (dev mode, widescreen, FPS smoothing, texture packs — scope capped there)

## Rolling timeline (newest first)

### 2026-04-27 — Session 16: Phase 1 complete — menu music plays

- **Audio works end-to-end.** User audibly confirmed clean menu music (T1 verified). Phase 1 success criterion met.
- Root cause of session 15's "music under static": the shipping data is Microsoft ADPCM (wFormatTag=2, stereo, 1024-byte blocks) but the code routed it to Bungie's custom IMA ADPCM decoder based on a flags field that didn't indicate compression. IMA decoder blew up within the first packet (step index diverging, output railing at ±32768).
- Fix: route by `SStFormat.wFormatTag` (== 2 → ADPCM_MS via libavcodec) instead of `flags & SScSoundDataFlag_Compressed`. Read channel count, block alignment, sample rate, and bits-per-coded-sample from the SStFormat metadata rather than hardcoded constants. Stereo decode + OpenAL stereo upload now works correctly.
- Secondary fix: removed `UUmSwapBig_2Byte` from IMA state-word reads in `BFW_SS2_IMA.c`. The shipping Windows `.raw` files store IMA data in little-endian (platform-native) byte order; the big-endian swap was producing wrong predictor/index values. IMA path not exercised by menu music (which is ADPCM_MS) but will matter for any future IMA-format sounds.
- Stripped all session 15/16 audio instrumentation (Play/Resume logging, hex dumps, WAV dumps).
- New Game still crashes (Bug CB — `ONiFlagsExist` via `OBJiObjectGroup_EnumerateObjects+196`, SIGSEGV at truncated pointer `0x6aee1bab`). This is the known callback-truncation class, Phase 2 work.

### 2026-04-26 — Session 15: Phase 1 audio investigation

- Step 1.1 (audible-path instrumentation): confirmed **category γ** — `alSourcePlay` never called. Bug A diagnosis validated.
- Step 1.3γ (fix the gate): Re-applied Bug A `.sep→.raw` fallback. Fixed MSADPCM EAGAIN value (`-11` → `AVERROR(EAGAIN)` for macOS). Added `block_align`/`bits_per_coded_sample` to ADPCM codec context. Added safety guard for bad data pointers. Re-enabled `SStSoundData.data` raw-pointer resolution on 64-bit (was incorrectly `#if`'d out). Discovered data format is Oni's IMA ADPCM (not MSADPCM); implemented IMA decompression via `SSrIMA_DecompressSoundData`. Root-caused why all 796 SNDD instances had identical field values: the `SStFormat` struct (50 bytes) was commented out of the C struct but present in on-disk data, causing swap codes to read WAVEFORMAT fields as audio metadata. Uncommented `SStFormat f`, packed it with `__attribute__((packed))` to eliminate 2-byte trailing padding mismatch, and rewrote SNDD swap codes to include all SStFormat fields + fixed-length aCoef[7] array. Per-instance data now varies correctly. Audio reaches speakers — user reports "music underneath static." Remaining corruption likely in data pointer resolution or IMA state handling. **Not yet audibly clean — Step 1.3γ continues next session.**

### 2026-04-25 — Session 13: replan + land Phase 0 in-tree wins

Brainstormed and committed a stepwise spec replacing the cascade-grind: [docs/superpowers/specs/2026-04-25-path-to-playable-spec.md](../docs/superpowers/specs/2026-04-25-path-to-playable-spec.md). Six phases, each step pre-conditioned + verified + rollback-safe, skill mapping per step, re-entry protocol so a future session can pick up at "do step N." Phase 0 is landing the in-tree session-13 wins one commit at a time before chasing the audio cascade again.

- `4931969` Options dialog used Mac variant ID (157) but the shipping data only includes the PC variant (152) — the Mac dialog request silently no-op'd. Stripped the `#if (UUmPlatform == UUmPlatform_Mac)` branch in `Oni_OutGameUI.c` so the PC dialog (152) is requested unconditionally. Verified end-to-end this session: Options dialog opens, sliders/dropdowns visible, Cancel returns cleanly. Tiny user-visible win.
- `769798e` `P3iBridge32To64` for `P3tParticleDefinition` landed in `BFW_Particle3.c` behind `#if UUmPlatform_PointerSize == 8`. Bridges three layout gaps in the on-disk PAR3 blob: (a) `P3tAttractor::attractor_ptr` 4→8 bytes (struct grows 216→224), (b) `P3tParticleDefinition` trailing variable/action/emitter pointers 12→24 bytes, (c) `P3tEmitter::emittedclass` 4→8 bytes per emitter (struct grows 444→448). Pinned with `_Static_assert`s on every relied-on size and offset. **Inert without Bug A:** the BINA dispatch chain currently short-circuits at `OSiBinaryData_ProcHandler`, so `P3iProcessParticleClass` is never reached and the bridge function never runs. Verified: `num_ambients=0` in `startup.txt`, no PAR3 loader entries, binary reaches menu cleanly. Lands in tree now to preserve the work and prime the next session for the paired Bug A re-apply.
- Step 0.3 (`ONI_RESOLUTION` env var override) deferred. The env var path correctly drives the active mode index, GL viewport, and WM desktop size to 1920×1080 — but the main menu dialog (resource ID 150) carries hardcoded 640×480 dims in the shipping game-data fork, plus `SDL_SetWindowFullscreen(FULLSCREEN_DESKTOP)` likely expands the framebuffer past 1920×1080 on retina. Result: env var resizes the SDL window, but the menu renders ~640×480 in a corner of a giant black void. Fix requires aligning five separate coordinate-space layers (SDL window, SDL fullscreen, GL viewport, GL ortho, WM desktop, dialog resource, in-game HUD) — real Phase 4 HiDPI work, not a one-liner. Architecture map and three ranked fix candidates documented in [`docs/handoff-2026-04-25-session13-step0_3-deferred.md`](../docs/handoff-2026-04-25-session13-step0_3-deferred.md). Phase 0 closes with 0.1 + 0.2 banked.

### 2026-04-25 — Session 12: Bug A diagnosed, fix landed and reverted, cascade discovered

Net forward progress: Bug A's root cause is now known with certainty. Net regression: a 64-bit bug in the particle loader (Bug C) was unblocked by Bug A's fix and crashes init. Fix reverted at session close. Binary back to silent-but-runnable. Full handoff with re-entry instructions: [`docs/handoff-2026-04-25-session12-revert.md`](../docs/handoff-2026-04-25-session12-revert.md).

- `2f1e080` Audio investigation: re-traced the BINA dispatch chain end-to-end and added narrow breadcrumbs at every hop (`BDrRegisterClass`, `BDiBinaryData_ProcHandler` entry, `TMiGame_LoadedInstanceFiles_Add` per-tag `ContainsTemplate`, `TMiGame_LoadedInstanceFiles_PrivateData_Add`, `TMiGame_InstanceFile_Callback` with `numPrivateInfos`, `OSiAmbient_Load` entry). These stay in tree.
- Bug A root cause confirmed by playtest: every `BDiBinaryData_ProcHandler` call fires with `sep=0x0`. **Shipping Oni install has no `.sep` companion files** — verified across 2,035 files in CXOni: `.dat`/`.raw`/`.bik` only. Both `BDiBinaryData_ProcHandler` (BINA → texture materials / impact effects / particles) and `OSiBinaryData_ProcHandler` (OSBD → ambient/group/impulse audio, at `Oni_Sound2.c:4342`) gate their loads on `TMrInstance_GetSeparateFile() != NULL` and silently no-op when it's NULL. The importer concatenated the "separate" blob into `.raw`; verified by reading bytes at the on-disk `data_index` offsets — they land on valid `BDtHeader` rows (class_types `ONIE`, `OBJC`, `PAR3`, `OSAm`, …) followed by `data_size−8` bytes of payload.
- `c039fa5` (REVERTED in `7e51a55`) — fix(tm): when `BFrFile_Open(*.sep)` fails, fall back to opening `.raw` as the separate-file handle. Surgical 16-line change in `BFW_TM_Game.c` `TMiGame_InstanceFile_OpenAndLoadHeader`. Fix is correct in isolation: verified at runtime — `OSiAmbient_Load` 1,005×, `OSrAmbient_BuildHashTable num_ambients=1005`, `main_menu_win` found at idx 867. **But it unblocks Bug C**: `P3rLoad_PostProcess` SIGBUSes during 64-bit bridge of `P3tParticleDefinition` (KERN_PROTECTION_FAILURE at `0x19c8d9cb04`, classic above-4GB pointer truncation). Bug C was latent the whole port — particle binary data never reached the loader until Bug A was fixed. Fix reverted at session close pending Bug C diagnosis. **Do not re-apply solo. Land paired with Bug C fix.**
- `7e51a55` revert of `c039fa5`. Binary back to silent-but-runnable, same functional state as session 11 close.
- Discipline lessons (now in auto-memory): always use superpowers, always verify behaviourally (auto-launch + log inspect, not just diff review), never trust prior-session claims about current state, don't dispatch Opus implementer agents on uncertain hypotheses, trace one layer downstream of every unblock before declaring done. Session burned ~500K tokens for net-zero functional progress; the protocol corrections matter more than the diagnosis itself.

### 2026-04-24 — Session 11: FNtW crash characterised, audio root-caused

- Audio root cause identified and it is NOT an OpenAL / streaming / threading issue. Added breadcrumbs at every audio layer (platform → SS2 → OS layer → hash builder) and confirmed: the `SSrAmbient` template registry is completely empty (`SSrAmbient_GetNumAmbientSounds()` returns 0). `OSrAmbient_BuildHashTable` therefore adds zero entries. All subsequent lookups (`OSrAmbient_GetByName("main_menu_win")`) return NULL, so `OSrMusic_Start` and every other music/ambient/dialog call short-circuits silently. No sound can ever play by design. This is the same class of bug as character-geometry `triNormalArray` failing — a template-manager enumeration / registration path that isn't populating ambient templates on 64-bit. The OpenAL port itself is fine; the bug is at the template-registration layer. Scope revision: audio work is a TM-bridge investigation, not a platform port.
- Clarified that the earlier "3D engine startup complete → running game..." trajectory some nohup launches took was NOT user input — nobody clicked New Game. Still an unexplained auto-transition; parking for later as a likely port-exposed `ONgGameState->victory` garbage-read issue (triggers the win-splash path, which calls `ONrLevel_Load(next_level)`). Separate bug from the audio finding.

- FNtW crash root-cause narrowed. User drove gameplay; `startup.txt` captured 2,916 clean `MSrTransform_Geom_FaceNormalToWorld` calls (env geoms, `numVec` 54–353) then one bogus call on a later geom with `numVec=1063779958`. That decimal value is the IEEE-754 bit pattern for ≈0.886f — we're reading a float out of a `UUtUns32` slot. Not an overshoot; it's a struct-offset / TM-bridge miss on character geometry's `triNormalArray`. Environment geoms bridge correctly; the character-geometry path (probably `TRtBody`-nested `M3tVector3DArray*`) is missing its 32→64 widening step. Next step: widen the breadcrumb with a 16-byte hex dump around `triNormalArray` to identify which struct we're actually pointing at.
- `SIGSEGV handler verified end-to-end`. Induced SIGSEGV on a running playable process; handler fired, SDL tore down cleanly, zero UE-state zombies remained. No more daily reboots confirmed.
- `[obs commit pending]` per-call breadcrumb at `MSrTransform_Geom_FaceNormalToWorld` entry — logs `inGeom / triNormArr / vectors / numVec / block8 / outPtr` uncapped. This commit is the obs: landing; diagnostic stays in until the character-geometry bridge is fixed.
- Audio scoping: confirmed OpenAL output is silent at menu (user correctly points out menu music should be audible from memory). Tracked the path into `BFW_SS2_Platform_OpenAL.c` — the port isn't a streaming pump; it batch-decodes whole sounds via libav MSADPCM and attaches a single buffer per source. That architecture should work for looping music with `AL_LOOPING`, so the silence is a different bug. Two visible code smells already in that file: a dead-code format check at line 186 with `&&` that never fires, and an empty-body `SS2rPlatform_InitializeThread` at line 488 (non-void return with no return statement — UB). Investigation continuing with breadcrumbs inside `SetSoundData` / `Play` / `DecompressMSADPCM` to find which step actually fails.
- HiDPI mouse correction saved to auto-memory: render lives in the bottom-left of the window, pointer coords land in the top-left. Clicks themselves work. Previous framing as "menu lockup" / "non-deterministic clicks" was wrong; it's a pointer-vs-render quadrant mismatch that makes aiming hard, not broken input.

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
