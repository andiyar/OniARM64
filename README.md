# OniARM64

Native ARM64 / Apple Silicon port of Oni (Bungie, 2001).

## Status (2026-05-20)

**Phase 5 done. Phase 6 has its first ticks.** Session 26: the user
played the tutorial level start-to-finish, crossed the level-1 → level-2
transition without a crash (the first time this port has ever crossed
a level boundary mid-gameplay), and verified that weapons, melee combat,
NPC-vs-NPC combat, and Konoko-vs-NPC combat all work end-to-end. Three
milestones tick at once: Phase 5's last item, plus Phase 6's first two.

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

## Rolling timeline (newest first)

### 2026-05-20 — Session 26 (continued): Text clipping FIXED — second instance of the embed-struct bridge bug

- **The session 26 text-clip bug is fixed and user-verified.** Three screenshots after the fix show the diary screen rendering full text everywhere: `BALLISTIC AMMO...........RELOAD WEAPON` (was `ISTIC AMMO`), `Coordinating the arms of mercenary...` (was `dinating...`), `Their solution was modular...` (was `r solution...`), `Hint:` (was `:`), `Reloading a weapon takes time:` (was `ading a weapon...`), `Plan accordingly.` (was ` accordingly.`), plus the encrypted-message diary entry and the weapon info panel all render their leading characters intact.
- **Root cause (`fix(64bit): bridge embedded ONtIGUI_FontInfo alignment in IGSt template`, commit `9821d11`):** exact same bug class as session 25's AKVA fix, applied to a different template. `ONtIGUI_String` (template `'IGSt'`) embeds `ONtIGUI_FontInfo` at its head; `ONtIGUI_FontInfo` contains a `TStFontFamily*` pointer so its alignment on 64-bit is 8 and its compiler-rounded sizeof is 24 (vs the 20-byte sum of field sizes). The bridge walker, oblivious to embedded-struct boundaries, walks the field stream flatly — `font_info`'s last field (`flags`, 2 bytes) ends at walker descriptor offset 28 (preamble 8 + 20), and the following `char string[384]` array is 1-byte-aligned so no alignment bump absorbs the drift. Walker writes `string[0]` at offset 28 but C reads `string[0]` from offset 32. The first 4 bytes of every on-disk string fall into the `font_info` trailing-pad slot in C's view and disappear.
- **Why the symptom looked like a clip-rect bug:** every dialog title and body line lost its leading 3-5 characters. The actual mechanism was upstream of the renderer — the strings were already truncated at template-load time. The first investigation agent hypothesised a `WMrClipRect` trivial-reject; the `[TEXT-DBG]` tracer I added at `TSiContext_DrawTextLine` showed every glyph at correct position with `glyph.left == clip.left` for i=0 — proving the renderer was getting already-clipped strings, not clipping them itself. Two screenshots from the user (showing `TH METER TRAINING` and `ISTIC AMMO`) made the 4-byte-character-offset pattern obvious enough to inspect `ONtIGUI_FontInfo` directly.
- **Fix:** extended `iFixupEmbeddedStructAlignment` in `BFW_TM_Bridge.c` with an `'IGSt'` branch that shifts every field at-or-after walker offset 28 by +4 bytes. The 384-byte string is encoded as two consecutive FixedArrays (255 + 129); both get the shift. The walker's existing 8-alignment padding on the descriptor's `dst_size` (= 416) supplies the 4 extra bytes — no overflow.
- **Embed-struct bug class scoreboard:** AKVA (PHtRoomData embed → silent AI movement failure, session 25) and IGSt (ONtIGUI_FontInfo embed → silent text clipping, session 26). Two instances of the same class, two completely different visible symptoms. The remaining ~112 templates still need an audit. The leading candidate is the laser-beam template flagged earlier this session: env-effect / particle-class templates are the next likely embed sites.
- **Diagnostic retained per `feedback_keep_diagnostics`:** `[TEXT-DBG]` tracer at `BFW_TextSystem.c:1829` logs glyph clip vs bounds for first 5 chars of every text-line draw. Confirmed innocent in this investigation; kept in tree for the next text-rendering regression.

### 2026-05-20 — Session 26: Tutorial level completed end-to-end — Phase 5 done, Phase 6 first ticks

- **User completed the tutorial level start-to-finish and crossed the next-level transition** with no crash. First time the port has ever crossed a level boundary mid-gameplay. Three milestones tick in one playthrough: Phase 5's last item (NPC-vs-NPC combat completes), Phase 6's "Konoko vs NPC end-to-end combat", and Phase 6's "Tutorial completable to next-level transition".
- **What worked, observed visually:** info-panel tutorial dialogs render and step through Next/Previous, weapons fire and reload correctly, melee combat works on NPCs, NPC-vs-NPC combat resolves to a kill, level-exit trigger fires, next-level load completes without SIGBUS/SIGSEGV.
- **Two visible bugs survived the playthrough.** Both are pre-existing; neither blocked completion:
  - **Text clipping** (known, Phase 2 open item): `"TH METER TRAINING"` instead of `"HEALTH METER TRAINING"`, `"ISTIC AMMO"` instead of `"BALLISTIC AMMO"`, `"ading a weapon takes time"` instead of `"Reloading a weapon takes time"`. Consistent 3–5 char left-edge clip in dialog body and title text. Affects every tutorial popup.
  - **Security-laser beams not rendering** (new): in the tutorial security-laser room, the wall-mounted projector hardware (the three-rail emitter mounts) renders correctly, but no beam visual appears between paired emitters. Strong hypothesis: same embed-struct bridge-alignment bug class as session 25 (113 templates not audited), applied to whichever env-effect / particle-class template defines the laser beam visual. Symptom signature matches the `can't find emitted particle ''` warning floods we already see in debugger.txt — env-effect template loading a name field that came back empty because the bridge dropped a slot.
- **Logging gotcha discovered:** `startup.txt` is unlinked-while-open by Oni (lsof confirms FD held, dir entry absent). It survives a SIGKILL (kernel flushes on FD release) but vanishes on graceful exit — so a clean playthrough leaves no log on disk. To capture logs from a clean run we either need to SIGKILL the process before it shuts down OR fix the Oni code to not unlink. Filed as a workflow concern, not a port bug.
- **No code changes this session** — pure verification + docs.

### 2026-05-20 — Session 25 (continued): Scripted patrol movement also verified

- After the AKVA bridge fix landed, a separate repro confirmed scripted NPC patrol paths now execute end-to-end. User observed an NPC follow its patrol route, turn around (waypoint completion), detect the player on sight (Knowledge contact + Pursuit goal transition), and pursue up a flight of stairs (cross-BNV pathfinding + stair-flag handling). One fix, two milestones ticked.
- 55 GRID-DBG events on this run, 55 success=1, zero DetEnd-FAIL, MOVE-DBG with non-NULL next_pt on 946/1814 commits. Stable behaviour across multiple NPCs / multiple BNVs / scripted vs reactive movement paths.

### 2026-05-20 — Session 25: Pursuit movement FIXED — embedded PHtRoomData bridge alignment

- **The fix landed and is user-verified.** Konoko stood in front of an NPC in level 1; the NPC walked over and engaged. MOVE-DBG metrics flipped from 0/1041 → 368/869 commits with non-NULL `next_pt`, 519/869 with `actual_dir` non-Stopped, mostly Forward. PHrPrepRoomForPath tracer flipped from `gridX=80 gridY=0` (broken) to `gridX=37 gridY=80` (correct — room is 37 columns × 80 rows, was previously reading disk's gridY into the gridX slot and zero into the gridY slot).
- **Root cause (`fix(64bit): bridge embedded PHtRoomData alignment in AKVA template`, commit `6c030e0`):** the 32→64 template-instance bridge walker in `BFW_TM_Bridge.c` (which translates on-disk 32-bit instance data into 64-bit in-memory C structs) walks the swap-code stream as a flat sequence of scalars/pointers/arrays. There is no swap-code marker for "begin embedded `tm_struct`" — those boundaries were a no-op on 32-bit because no `tm_struct` had an alignment requirement larger than 4. On 64-bit, `PHtRoomData` (embedded inside `AKtBNVNode`) contains pointers (`compressed_grid`, `debug_info`) and therefore has alignment 8. The C compiler 8-aligns the embedded struct by inserting 4 bytes of pad before `roomData` in `AKtBNVNode`. The walker, oblivious, placed `gridX` and `gridY` at dst offsets 28/32 — the C compiler has them at 32/36. Because the walker correctly aligns `RawPtr` to 8 when it later hits `compressed_grid`, the drift cancels at offset 40 and every field after `compressed_grid` ends up correct. So the visible damage is exactly: `room->gridX` reads disk's `gridY`, `room->gridY` reads memset-zero.
- **Why the symptom looked like AI movement and not the template manager:** `room->gridY = 0` made every `(inY >= room->gridY)` bounds check in `ASiDetermineEndPoint` (Oni_AStar.c:1795) succeed for all `inY ≥ 0`, so no destination square was ever passable. `ASrPath_Generate` returned `UUcFalse` for every call. Inside `AI2iMovementState_SetupPath`, that triggered `AI2_ERROR` + `AI2rMovementState_ClearPath` — but `AI2_ERROR_REPORT = 0` in release, so the error was completely silent. The visible symptom was just "NPC sees you, enters Pursuit, never moves." Session 24's diagnostics narrowed to "grid-path generation is empty" but couldn't see why. Session 25 added three GRID-DBG tracers, the very first SetParams event printed `gridXY=(80x0)`, and the bridge bug became immediately obvious.
- **Fix scope:** a small post-pass (`iFixupEmbeddedStructAlignment`) in `TMrBridge_BuildDescriptor` recognises the `'AKVA'` template tag and shifts the two leading scalars in the element sub-descriptor by +4 bytes each. 64 lines added, all in `BFW_TM_Bridge.c`. No changes to swap codes, no checksum changes, no on-disk format changes.
- **Audit deferred:** `PHtRoomData` is the only known embedded multi-pointer `tm_struct` in this codebase (grep confirms it's embedded only in `AKtBNVNode` per `BFW_Akira.h:516`). A systematic audit of the other 113 templates for similar patterns is a follow-up — any template embedding a `tm_struct` whose alignment is `> 4` on 64-bit will have an equivalent bug, manifesting as some other "silent struct field is zero" symptom downstream.
- **Diagnostics retained per `feedback_keep_diagnostics`:** `[GRID-DBG] SetParams`, `[GRID-DBG] Generate`, `[GRID-DBG] DetEnd-FAIL`, `[GRID-DBG] SetupPath-FAIL`. The first two are also useful for verifying any future grid-path regression.

### 2026-05-20 — Session 25 (earlier): [GRID-DBG] tracers land; AI2_ERROR silent-in-release explains session 24's misread

- **Three `[GRID-DBG]` tracers added** around `AI2iMovementState_SetupPath`'s two failure-bearing calls (`ASrPath_SetParams`, `ASrPath_Generate`) and at the silent UUcFalse return in `ASiDetermineEndPoint` (`Oni_AStar.c:1787`). Retained per `feedback_keep_diagnostics`. Plus one tracer at the SetupPath ClearPath site for confirmation.
- **Diagnostic discovery #1: `AI2_ERROR` is silent in release.** `Oni_AI2_Error.h:22-25` defines `AI2_ERROR_REPORT=0` for non-TOOL_VERSION builds, so the `AI2_ERROR(...)` macro expands to `AI2rHandleError(...)` (no console / log output) instead of `AI2rReportError(...)`. Session 24's conclusion "no AI2_ERROR in startup.txt ⇒ SetupPath returned true" was wrong — the absence carried zero information. SetupPath was almost certainly returning false silently and calling `AI2rMovementState_ClearPath` (which is what produced `grid_num=0`, `next_pt=NULL` in MOVE-DBG).
- **Diagnostic discovery #2: the "successful" 7890/7891 PURSUIT-DBG event was during level-load.** The two lines immediately around it in startup.txt are `[lvl-load] before SLrScript_ExecuteOnce(main)` and `[lvl-load] after SLrScript_ExecuteOnce(main) err=0`. It was a script-driven path (different BNVs, took the `path_connections[0]->from` branch), not a real-combat pursuit. The 19 in-combat events (all same-BNV, all `ok=0 num_nodes=1`) are the actual symptom — every one of them silently failed in SetupPath.
- **No code semantics changed.** Diagnostic-only commit. Next repro categorises the failure (SetParams vs Generate vs DetEnd vs CalcWaypoints), then a targeted fix from one of four pre-analysed branches.
- **Build observation noted for follow-up:** `Oni_AStar.c:809-810` warns on `UUtInt16 *` → `UUtUns16 *` pointer-sign mismatch in `PHrWorldToGridSpace` calls. Pre-existing in Bungie's source; not 32→64, but worth keeping in mind if the GRID-DBG tracers show out-of-bounds grid coordinates being silently pinned to UUtUns8.

### 2026-05-20 — Session 24: Alert-escalation tracers land, reveal Pursuit-movement is the bug

- **Five `[ALERT-DBG]` tracers** added across `Oni_AI2_Alert.c` (four: at NotifyKnowledge entry / NotThreat-return / before UpgradeStatus call / before CombatGate) and `Oni_AI2.c` (one: in `AI2rEnterState`'s `AI2cGoal_Combat` dispatch). Bracket every decision point on the Knowledge → Combat escalation chain. Retained per `feedback_keep_diagnostics`.
- **Repro produced a clean diagnosis.** User played to a level-2 hallway with a hostile NPC. `startup.txt` captured: 42 `NotifyKnowledge` events, 42 `UpgradeStatus-call` events, 42 `CombatGate` events with `will-enter=0`, and exactly **one** `[ALERT-DBG] EnterState dispatching Combat` followed by the existing `[WEAPON-DBG] Combat_Enter` tracer. So combat *does* work — it fires once successfully when the conditions are right.
- **Type breakdown:** 3× type=1 (Sound_Interesting / Konoko's footsteps), 38× type=5 (Sight_Peripheral), 1× type=6 (Sight_Definite). The single type=6 event correctly escalated to Combat — `new=4 old=1 will-call=1` → `inStatus=4 ... will-enter=1` → EnterState dispatch → Combat_Enter. The chain is mathematically and structurally fine.
- **The real bug:** the NPC sees Konoko mostly via `Sight_Peripheral` (centre-cone math at `Oni_AI2_Knowledge.c:987-1010` requires `dist < central_dist`, and the central-cone radius is small at the angles the player sits in). Peripheral sight escalates only to `AI2cGoal_Pursuit` (verified via tracer: NPC's `goal` transitioned 3=Patrol → 6=Pursuit). **Pursuit then fails to translate into NPC locomotion.** The NPC stays put, so `aimrel_along` / `dist` stay roughly constant frame to frame, so peripheral classification stays sticky every frame, so combat is never re-entered. Only the player physically closing the distance produced the one type=6 event observed.
- **Next investigation target:** the Pursuit movement layer. Specifically `AI2rPursuit_Enter` / `AI2rPursuit_Update` / `Oni_AI2_Movement.c` / `Oni_AI2_MovementState.c`. The earlier session 21 scripted-NPC-doesn't-walk-into-room symptom and this Pursuit-doesn't-close-distance symptom are likely the same bug class: AI characters not translating position even when their state machine says they should. Investigation continues in a fresh thread; expect more diagnostic tracers, not a speculative widening sweep.
- **Eliminated as cause:** Knowledge `user_data` truncation (already widened, structurally correct), AI2_ERROR macro / inParam3-inParam4 truncation (already widened, structurally correct), the Alert→Combat escalation chain itself (proven working when fed Sight_Definite), goal-dispatch (proven working — `AI2rCombat_Enter` did fire after the dispatch tracer the one time the chain completed). All ruled out by evidence, not by assumption.

### 2026-05-20 — Session 23 continued: AI error subsystem widening — structurally correct, NOT symptom-verified

- **AI error subsystem widened across 10 files / 184 lines.** Sweep applied: `UUtUns32 inParam3 / inParam4` → `uintptr_t inParam3 / inParam4` everywhere in the AI error-handler signature family, plus the `AI2_ERROR` macro's cast from `(UUtUns32)` to `(uintptr_t)` on all four params. Touches `Oni_AI2_Error.{h,c}`, `Oni_AI2_Combat.{h,c}` (3-param `AI2tBehaviorFunction` family), `Oni_AI2_Maneuver.c`, `Oni_AI2_Melee.c`, `Oni_AI2_Patrol.{h,c}`, `Oni_AI2_Neutral.{h,c}`. Closes the cascade-fix #2 and #3 territory from session 21's audit in one pass — the macro previously truncated all four params, including pointers like `PHtNode *` (Maneuver pathfinding error) and `TRtAnimation *` (Melee combat behavior). The `AI2iManeuver_PathfindingErrorHandler` at `Oni_AI2_Maneuver.c:198` reads `inParam3` as `(PHtNode *)` — that path now receives an intact 8-byte pointer when triggered.
- **Symptom was wrong.** User-observed AI behaviour in level 2 (NPCs in combat stance refusing to advance until Konoko closes distance) was the hypothesised symptom — but post-fix verification showed the widening did not relieve it. The new diagnostic evidence reframes the bug: in this session's playthrough, `[KNOWLEDGE-DBG]` fired 10 times (owner=NPC, enemy=char_0/Konoko, types 1+6 = first sight + ongoing sight contacts) but **zero combat tracers fired**. `AI2rCombat_Enter` never ran. So combat is never being entered despite the NPC seeing Konoko clearly. The bug is upstream of combat — alert-level escalation in the Knowledge / Alert layer is the next investigation target.
- **Why land it anyway:** it's a real port-exposed 32→64 bug pattern, structurally correct, no regression risk (every `UUtUns32` value fits in `uintptr_t`; the previous truncating casts were only ever destructive). It will eventually be exercised by some code path that does need 8-byte param survival — better to close the latent trap now than re-discover it later. Marked clearly as "not symptom-verified" so future sessions know it's not the AI advance fix.
- **Diagnostics inventory in tree:** Knowledge tracers ([KNOWLEDGE-DBG] at `Oni_AI2_Knowledge.c` lines 647, 654, 1209, 1269 from session 21) and combat tracers ([WEAPON-DBG] at `Oni_AI2_Combat.c` lines 318, 507, 592, 3986 from session 21) plus animation diagnostics (`ANIM_PREP` BFW_Totoro.c:2711, `SND` Oni_Sound_Animation.c:1834 from session 22). All retained per the diagnostics-retention policy. Combat tracers proved their value this session by *not* firing — that absence is the evidence pointing to the alert-escalation layer.

### 2026-05-19 — Session 23: Footsteps audible — impact-effect on-disk struct bridge landed

- **Footsteps now play.** User-verified audibly in the level-1 opening — Konoko's run produces footstep impacts as designed.
- **Root cause:** `ONrIEBinaryData_Process` was rejecting the 47004-byte impact-effect blob against an `expected_size` of 49644, falling into its `goto cleanup` and triggering `ONrImpactEffects_CreateBlank` at `Oni_Level.c:454`, which silently produces an empty lookup table. With nothing to find, `ONrImpactEffect_Lookup` returns nothing for every footstep, and `OSrImpulse_Play` is never called. End-to-end silent footsteps.
- **Why on 64-bit:** `ONtIESound` and `ONtIEParticle` (via embedded `P3tEffectSpecification`) both carry pointer fields. On the Win32 disk file those slots are 4 bytes; on ARM64 our runtime structs put 8-byte pointers in them (plus alignment padding before the pointer in `ONtIESound`). The sanity check at `Oni_ImpactEffect.c:2075` was comparing `buffer_size` to a `sizeof()`-summed `expected_size`, so the 2640-byte delta (84 particles × +4 + 288 sounds × +8) always trips. The other five impact-effect struct types are pointer-free and disk-size == runtime-size, so no bridge needed for them.
- **Fix shape** (`Oni_ImpactEffect.c`): `ONcIESound_OnDiskSize` (48) and `ONcIEParticle_OnDiskSize` (84) constants added near the version enum; two per-record disk readers (`ONiIESound_ReadFromDisk`, `ONiIEParticle_ReadFromDisk`) copy each field at its known disk offset and leave pointer slots NULL; two per-type bridge functions (`ONiIESound_ConvertArrayFromDisk`, `ONiIEParticle_ConvertArrayFromDisk`) allocate `UUtMemory_Array`s and walk the disk buffer record-by-record. The three change sites in `ONrIEBinaryData_Process` are guarded `#if UUmPlatform_PointerSize == 4 / #else` so the 32-bit build path is unchanged. The existing `ONiImpactEffect_GetSound/GetParticle` accessors already abstract static-vs-dynamic-array; every call site just keeps working. Pointer resolution by name (`ONrImpactEffects_SetupSoundPointers` / `_SetupParticlePointers`) is unchanged and was already called by sound2 / particle3 init after binary data load — it now finds non-empty arrays to walk.
- **Verification:** clean build, clean launch under `lldb -b`, clean exit. `debugger.txt` no longer reports `expected size 49644 but buffer is 47004!` or the cleanup-path error at line 2216. Game reached level 1 and ran to a `game over` event. Pre-existing diagnostic fprintfs (`ANIM_PREP` in `BFW_Totoro.c`, `SND` in `Oni_Sound_Animation.c`) kept in tree per the diagnostics-retention policy — they helped triage the dead-end "animation sound system" path (`OSrSoundAnimation_Play` returns has=0 because shipping data has no `Binary/SoundAnims/`, expected behaviour) and confirmed the live path is the impact-effect chain.
- **Cascade carry-overs (not blockers for this fix):** the AI combat-stance-freeze-after-first-kill from earlier in the session still likely sits in cascade fix #2 (pathfinding error handler) territory. The `Particle class 'w10_sni_p01' is too large (268) for largest size class (256)!` warning is a separate latent issue unrelated to footsteps.

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
