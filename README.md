# OniARM64

Native ARM64 / Apple Silicon port of Oni (Bungie, 2001).

## Status (2026-05-19)

Phase 3 continues. AI combat crash is root-caused and fixed — pointer
truncation in `AI2rCombat_NotifyKnowledge` was passing an 8-byte
`AI2tKnowledgeEntry *` through a `(UUtUns32)` cast into the behavior
dispatcher, which then cast the truncated 4-byte value back to a pointer
and crashed on first access. Fix verified end-to-end: NPCs enter combat
without crashing.

Cascade exposed: AI combat starts but stalls intermittently — multiple
latent truncation sites in the AI message-passing layer (knowledge
contacts, `AI2_ERROR` macro, pathfinding error handler, melee animation
through `inParam3`). Tutorial combat doesn't progress to completion.

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

- [x] Build as ARM64 binary
- [x] All subsystem init runs end-to-end
- [x] `loading level 0…` reaches the template-manager bridge
- [x] Main menu renders
- [x] `New Game` → level 1 load completes, splash clears
- [x] First gameplay frame on screen (warehouse, textures, HUD, player)
- [x] Multi-frame rendering without AKOT corruption
- [x] Movement (WASD / mouselook) doesn't instantly SIGSEGV
- [x] Crash-handler prevents UE-zombie processes after SIGSEGV (no more daily reboots)
- [x] Bug B — character geometry clip-buffer overflow fixed (texture-coord scratch buffer)
- [x] Doors open instead of clipped-through (→ `OT_Door` callback truncation sweep) — **sweep landed session 17**
- [x] Triggers / trigger volumes fire (→ `OT_Trigger`, `OT_TriggerVolume` callback truncation sweep) — **sweep landed session 17**
- [x] AI state machines run (→ `Oni_AI2*.c`, `OT_Combat.c` callback truncation sweep) — **sweep landed session 17**
- [x] NPCs render on screen — pathfinding + character visibility sorting fixed (session 20)
- [ ] Character animation: bone transforms correct, no levitation / stretched joints
- [x] **Audio actually plays** — Bug A diagnosed in session 12: shipping data has no `.sep` files, so both `BDiBinaryData_ProcHandler` (BINA) and `OSiBinaryData_ProcHandler` (OSBD, audio) silently no-op. Fix verified working in isolation (`c039fa5`, reverted in `7e51a55`) — but unblocks Bug C below. Land paired.
- [ ] **Bug C — particle loader 64-bit bridge gap** — `P3rLoad_PostProcess` SIGBUSes during 64-bit bridge of `P3tParticleDefinition`. KERN_PROTECTION_FAILURE at `0x19c8d9cb04` inside `P3rTraverseVarRef+2252` ← `P3rPackVariables+1624` ← `P3iProcessParticleClass+368`. Latent the whole port; only became reachable when Bug A unblocked the dispatch chain. Files: `BFW_Particle/BFW_Particle3.c`, `BFW_Headers/BFW_Particle3.h`. Must land paired with Bug A.
- [x] HiDPI viewport scaling: game renders fullscreen instead of 640×480 corner; mouse coords aligned
- [ ] `.app` bundle + code signing
- [ ] Anniversary Edition fixes (dev mode, widescreen, FPS smoothing, texture packs — scope capped there)

## Rolling timeline (newest first)

### 2026-05-19 — Session 23: AI Knowledge `user_data` widening landed

- **Knowledge `user_data` widened** (`Oni_AI2_Knowledge.h:87`, `Oni_AI2_Knowledge.c:74` and signatures at 124/130/1053/1126): `UUtUns32` → `uintptr_t` for `AI2tKnowledgeEntry.last_user_data`, `AI2tKnowledgePending.user_data`, and the `inAIUserData` parameter of `AI2iKnowledge_PostContact` / `AI2iKnowledge_AddContact`. Two truncating call-site casts in `AI2rKnowledge_Sound` (lines 648 and 654) switched from `(UUtUns32) inTargetN` to `(uintptr_t) inTargetN` so target-character pointers survive the trip into the knowledge layer. Cascade fix #1 from session 21's audit.
- **Behavioural run (verified non-regression, not symptom-verified):** game launched under `lldb -b`, played through the level-0 spectator combat room (3 NPCs: 2 hostile + 1 neutral) and quit normally. Clean exit, no crash. `[KNOWLEDGE-DBG] STORE` tracer fired 49 times across demo1/demo2/demo3/`char_0` with `user_data` values in `{0x0, 0x4, 0x5, 0x6, 0xa}` — all zero or damage-amount small ints. **`PostContact (sound...)` tracer and `READ-AS-PTR` tracer never fired** — the sound-event codepath the widening protects was not exercised by observed combat. Widening is structurally correct (closes a latent 32→64 truncation trap) but the test run did not bite into the protected bytes.
- **Symptom carried forward:** one pair fought to completion, then the surviving two locked into combat-ready stance facing each other and could not initiate engagement. That is target-acquisition-after-first-kill failure — AI knows there's an enemy (combat stance was entered), but cannot decide to act. Sits downstream of Knowledge, in pathfinding or behaviour-decision territory. Likely candidate: cascade fix #2 (`AI2iManeuver_PathfindingErrorHandler` declaring `inParam3` as `UUtUns32` and reading back `(PHtNode *) inParam3`).
- **Diagnostics retained** in tree per `feedback_keep_diagnostics` policy: `[KNOWLEDGE-DBG]` tracers at PostContact call-site (Knowledge.c:647/654), AddContact STORE (Knowledge.c:1209), AddContact READ-AS-PTR (Knowledge.c:1269).

### 2026-05-04 — Session 21: AI combat crash root-caused

- **AI combat crash fixed.** `AI2rCombat_NotifyKnowledge` (Combat.c:1120) passed `(UUtUns32) inEntry` as `inParam2` to `AI2rCombat_Behavior`, truncating an 8-byte `AI2tKnowledgeEntry *` to its lower 32 bits. The Hurt message handler at line 4055 cast `inParam2` back to `AI2tKnowledgeEntry *` and crashed on the first field access (`entry->enemy` at offset +8, address `0x4ea7e6b0` was the lower 32 bits of a real heap pointer). Changed cast to `(uintptr_t)`. Note: `inParam1` and `inParam2` are already `uintptr_t` in the typedef — only the call-site cast was wrong. Three NPCs entered combat without crashing in verification run. Initial handoff diagnosis (`weapon_parameters` at line 3963) was a stale crash report — the live crash was on a completely different path (`AI2rCombat_NotifyKnowledge` → `AI2rCombat_Behavior` → `AI2rBehavior_Default` Hurt handler, not the TooClose handler).
- **Cascade exposed:** AI combat now reachable but stalls intermittently. Audit identified 4+ remaining truncation sites in the AI message system: knowledge contact `last_user_data` field is `UUtUns32` and is fed by truncated pointers at Knowledge.c:648/652; `AI2tBehaviorFunction.inParam3` is `UUtUns32` (Melee.c:1300 truncates `TRtAnimation *`); `AI2_ERROR` macro pre-truncates all four params with `(UUtUns32)` casts; `AI2rManeuver_PathfindingErrorHandler` declares `inParam3` as `UUtUns32` and reads back `(PHtNode *) inParam3`. Tutorial combat playable but doesn't progress reliably — next investigation: coordinated sweep of these layers.
- **AUrQSort_32 audit (not landed):** 6 broken call sites identified that sort arrays of pointers through the 4-byte sort: `BFW_SoundSystem2.c:1015`, `Oni_Object.c:4168`, `BFW_Timer.c:398`, `BFW_Console.c:734`, `BFW_Particle3.c:2918,3027`. 11 other call sites are safe (they sort `UUtUns32` indices). Latent until those code paths execute.

### 2026-05-03 — Session 20: NPC activation fixes + HiDPI viewport fix

- **Pathfinding crash fixed:** `AKiPrepareGrids` had `offset = 0` on 64-bit, assuming the template bridge resolved AKVA's `tm_raw` pointers. But AKVA is a Leaf template — the bridge skips Leaf templates during `PreparePointers`. On 32-bit Bungie resolved raw pointers manually in subsystem `LoadPostProcess` callbacks; the prior session's `offset=0` broke this. Fix: restore resolution with `uintptr_t offset` (not `UUtUns32`, which truncates the rawBase). Also bypasses `UUmOffsetPtr` which casts offset to `UUtUns32`. Same fix applied to `gqDebug` name pointers in the same handler.
- **Character sort crash fixed:** `distance_from_camera_compare` took `UUtUns32` parameters (truncating 8-byte `ONtCharacter*` pointers), and `AUrQSort_32` sorted the array as 4-byte elements. Replaced with standard `qsort()` using pointer-width comparator. NPCs now render on screen for the first time.
- **Cascade status:** game now crashes in `AI2rBehavior_Default` (AI combat behavior) — bad `weapon_parameters` pointer (0x2e810310). Next investigation target.
- **HiDPI viewport scaling**: `glViewport` now uses `SDL_GL_GetDrawableSize()` to fill the actual screen instead of rendering 640×480 in the bottom-left corner. Mouse coordinates scaled from window space to game space in all three input paths (GetMouse, MouseMotion, MouseButton). Game's internal resolution stays 640×480 (ortho projection unchanged); the viewport stretches it to fullscreen. Files: `gl_sdl.c`, `gl_engine.c`, `gl_utility.c`, `OGL_DrawGeom_Common.c`, `BFW_LI_Platform_SDL.c`.
- **Resolution switcher fixed:** `ONiResolution_Switch` forced `osx = UUcTrue` for SDL builds, triggering the 2001-era "You must restart Oni" exit path on any resolution change — a workaround for 3Dfx/S3 video cards that couldn't hot-switch. Set `osx = UUcFalse` for SDL since viewport scaling handles runtime resolution changes natively.
- **Body horror fixed:** `TRrQuatArray_SetAnimationInternal` byte-swapped the per-bone offset into compressed animation data, guarded by `UUmPlatform == UUmPlatform_Mac`. This was correct for big-endian PowerPC but wrong for little-endian ARM64 — every bone read quaternion keyframes from a corrupted offset, producing twisted joints and levitation. Changed guard to `UUmEndian == UUmEndian_Big`. User-verified: Konoko and NPCs render with correct poses.
- **Resolution persistence fixed:** `BFrFile_FOpen` couldn't create new files because `BFrFileRef_Set` checks file existence and fails for non-existent paths. Added write-mode fallback that bypasses the file-ref layer and calls `fopen` directly. `persist.dat` now created on first resolution change; saved resolution restored on next launch.

### 2026-05-02 — Session 19: Phase 3 — Bug B fixed (character geometry crash)

- **Bug B root cause:** `MSrClip_ComputeVertex_TextureInterpolate` creates interpolated vertices at indices ≥ `numPoints` during frustum clipping. `objectVertexData.textureCoords` pointed directly into geometry's `texCoordArray` (exactly `numPoints` entries), while all other scratch arrays (`frustumPoints`, `screenPoints`, `clipCodes`) were pre-allocated with 2048 entries. Clip writes past `texCoordArray` stomped adjacent template memory — specifically `triNormalArray->numVectors` (329 → float 0.398506 bit pattern 1053559037), causing `FaceNormalToWorld` to loop into unmapped memory on the next frame.
- **Fix:** added `textureCoordsScratch` field to `MStTransformedVertexData`, allocated in `MSrTransformedVertexData_Alloc` with 2048 entries (matching other scratch arrays). At geometry draw time, `texCoordArray` contents are copied into the scratch buffer via `UUrMemory_MoveFast`, and both `objectVertexData.textureCoords` and the draw-state pointer are redirected to the scratch. Same pattern as the existing env-clipper fix for `gqVertexData` (session 9, `103496b`). Separate `textureCoordsScratch` pointer needed because sprite/contrail draw paths overwrite and NULL `textureCoords` between geometry draws.
- **Verified:** full training cutscene plays (Shinatama dialogue + camera pan + Konoko character model renders). All `[FNtW]` log entries show `numVec` in normal range (2–353). No corruption.
- **Body horror confirmed:** Konoko levitating mid-air, flexing joints. Separate from Bug B — bone-transform or animation-state issue (Step 3.2).
- **Subtitle/message fix:** `SSiSubtitleArray_FindByName` and `FindByNumber` used `(UUtUns32) TMrInstance_GetRawOffset()` which truncated the 64-bit rawPtr base. Changed to `uintptr_t`. Subtitles display correctly (T1 — user saw Shinatama's dialogue with portrait). Tutorial scripting runs end-to-end (25+ message lookups).
- **New cascade:** game crashes when an NPC becomes active and AI initializes pathfinding — `PHrGrid_Decompress` hits bad memory during `AI2rMovementState_Initialize`. The game now gets far enough into the tutorial to activate NPCs.

### 2026-04-27 — Session 17: Phase 2 — callback truncation sweep (Bug CB)

- **Full `UUtUns32 inUserData` → `uintptr_t` sweep across ~80 sites in 43 files.** Six typedef chains widened end-to-end: `OBJtEnumCallback_Object` impls, `OBJtEnumCallback_ObjectName` + `OBJtMethod_Enumerate`, `P3tClassCallback`, `AStHeap_CompareFunc`/`NotifyLocation`, `ONtEvent_EnumCallback_TypeName`, `WMtWindowEnumCallback` + `WMrDialog` userdata chain. Every callback impl that receives a pointer through `inUserData` now reads the full 64-bit value.
- **Level 1 load now completes.** All tripwires pass through `OBJrObject_LevelBegin`, `AI2rLevelBegin`, `ONrScript_LevelBegin`, `InitializeActionMarkerArray`, `ONrMechanics_LevelBegin`, character preload. The old `ONiFlagsExist` crash (truncated pointer `0x6aee1bab`) is gone.
- **Cascade found:** `P3rTraverseParticleClass` crashes during particle precache in level 1 begin — a `P3tParticleClass*` pointer (`0x2efb48`) is a 32-bit value that wasn't resolved to a 64-bit address. This is a template data pointer issue in the particle sub-emitter class linkage, not a callback truncation. Next investigation target.

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
