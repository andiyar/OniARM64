# Plan of attack: verify-then-implement, next local sessions (2026-09-22)

Companion to the three analysis docs from this pass:
[triage-2026-09-22-tester-issues.md](triage-2026-09-22-tester-issues.md),
[rca-2026-09-22-issue-49-sluggish.md](rca-2026-09-22-issue-49-sluggish.md),
[rca-2026-09-22-open-issue-sweep.md](rca-2026-09-22-open-issue-sweep.md).
Everything below was derived statically; each chunk starts with the check
that turns the analysis into an observation before any code moves.

Rules for the chunks:
- **Verify → implement → check → commit**, one commit per chunk, HISTORY
  and CHANGELOG lines in the same commit per the repo contract.
- A chunk's verify step can **stop** the chunk. Where a negative result
  changes the plan, the fallback is written in.
- Sizes are honest guesses: S = under an hour, M = an evening, L = a
  session.

Suggested grouping: Session A = chunks 0–4 (all small, all shippable).
Session B = chunks 5–6 (feel and sound, both need play time).
Session C = chunks 7–9 (safety, housekeeping, the r6 cut). Controller
decision after C.

---

## Session A — verify the tester bugs and land the small fixes

### Chunk 0 — prep (S)

- `git fetch origin && git checkout claude/new-issues-review-4i66ex`
  (or cherry-pick the docs onto main; nothing else is on the branch).
- Clean configure + build both `Oni` and `OniSweep`; run the existing
  test set you normally run (`tests/test_onimod_installer.sh`,
  `tests/test_onipack_cli.sh`, the C unit tests) so the baseline is green
  before anything changes.
- Confirm `scripts/sweep.sh` on level 0 + one level, both renderers,
  matches the committed baselines. This is the regression net for
  chunks 2–4.

### Chunk 1 — #111 / #112 installer pack names (S–M)

**Verify (10 min, no code).**
1. Take any installed pack triple and copy it under a 34-character leaf,
   e.g. `level1_BetterWarehouseTraining.dat/.raw/.sep`. Launch HEAD.
   Expected: registers, then level-0 load fails and Oni exits (the
   `[tm]`/error path), no crash report.
2. Rename the same triple to a 37-character leaf. Expected:
   `[overlay] unable to get level info for … skipping` and the game runs
   vanilla.
3. Ask simX (on #112) to grep their startup.txt for
   `unable to get level info`. Prediction: one line per long-named
   character pack. This is the second data set; not blocking.

**Stop condition.** If step 1 runs fine on HEAD, the length theory is
wrong for the crash half; keep the silent-skip fix and re-read the r5
`.ips` chain in the triage doc against a HEAD symbolication.

**Implement.**
- `tools/OniModInstaller/Installer.swift:208` — `prefix(32)` → `prefix(19)`;
  update the comment at `:203`. Add a long-`NameOfMod` case to the
  fixture test in `main.swift` (expects a 19-char pack folder and a
  ≤ 31-char leaf).
- `tools/onipack/onipack_main.c:85-91` — refuse an output leaf ≥ 32
  chars with a message naming the limit; add a case to
  `tests/test_onipack_cli.sh`.
- `BFW_TM_Game.c` `TMiGame_OverlayDir_Scan` (~`:3270`, before
  `TMrUtility_LevelInfo_Get`): `strlen(leaf) >= BFcMaxFileNameLength` →
  `[overlay] %s: file name too long (max 31 characters incl. .dat);
  rename the pack; skipping.` and `continue`.

**Check.** Repeat verify 1 and 2: both now log the new line and the game
starts. Reinstall BetterWarehouseTraining through the fixed installer:
`registered level1_<19-char>.dat`, level 1 loads with the textures.

**Close out.** Reply on #111 and #112 with the mechanism in two sentences
and the rename workaround for r5 users (all three files, ≤ 31 chars).
CHANGELOG under Mods.

### Chunk 2 — translated-block leak (S)

**Verify.** Activity Monitor → Oni real memory. Load a save point five
times in a row. Expected: a fixed step up per load (tens of MB), never
coming back. Optional: one `UUrStartupMessage` of
`translatedBlockSize` per file behind `ONI_DIAG_VERBOSE` to see the step
size in the log.

**Implement.** `BFW_TM_Game.c` `TMiGame_InstanceFile_Delete` (`:2133`):
`if (inInstanceFile->translatedBlock) UUrMemory_Block_Delete(...)` before
the struct is freed. The dynamic file (`:1599`) has none, hence the guard.

**Check.** Same five reloads: memory returns to within noise of the
pre-load value each time. Sweep still matches baseline. Note it on #30.

### Chunk 3 — #114 reticle stretch (S)

**Verify.** Any 16:9 or wider resolution, equip a pistol: the laser dot
is an ellipse; a muzzle flash is visibly wider than tall.

**Implement.** `MS_GC_Method_Geometry.c:1792-1793` and `:2298-2299`:
`xScale = drawHeight * (500.f / 480.f)` (same as `yScale`). Two sites.

**Check.** Dot is round at 16:9 and 21:9; unchanged at 4:3 (set a 4:3
mode once). Glows and stars look right under both renderers.

### Chunk 4 — #113 HD Screens tiles (S–M)

**Verify.** Install HD Screens through the (now fixed) installer, open
Load Game: the quartered background. Remove the pack: normal.

**Implement.** `Motoko_Utility.c` `M3rDraw_BigBitmap` (`:629`): compute
`nominal_w = min(256, big->width - x*256)`,
`nominal_h = min(256, big->height - y*256)`; draw each tile through
`M3rDraw_BitmapUV` with `uv = (width / nominal_w, height / nominal_h)`.
Optionally switch interpolation to linear when the tile's pixel size
differs from nominal.

**Check.** Load Game shows the full HD art; with the pack removed the
menus are pixel-identical to before (UVs reduce to the old `cell/256`).
One pass through the other screens the pack touches (pause, diary).

---

## Session B — feel and sound

### Chunk 5 — #49 tick judder (M, split in two)

**5a. Diagnostic first (S).** New `ONI_TICK_TRACE=1` gate: every 5 s log
a histogram of `deltaTicks` per frame (0 / 1 / 2 / 3+) and the count of
frames where `numActionsInBuffer > deltaTicks` (dropped input samples).
Play an evening with it on, both renderers if convenient.

**Read the result.**
- Prediction: sluggish stretches coincide with a 30–50 % share of 0/2
  frames and non-zero dropped samples; good stretches are ~all 1s.
- **Stop condition.** If a sluggish stretch shows all 1s and zero
  dropped samples, the clock theory is wrong for the *feel* (the leak and
  the drop remain real). Fall back to measuring input latency: timestamp
  the poll and the present for the same frame and look at the gap.

**5b. Fix (M).**
1. `BFW_Platform_SDL.c:117` `UUrMachineTime_Sixtieths`: derive from
   `SDL_GetPerformanceCounter()` in 64-bit, base captured at init, same
   32-bit wrap semantics.
2. `Oni_GameState.c` `ONrGameState_UpdateServerTime` (`:5466`) /
   `iComputeDeltaTicks` (`:4527`): elapsed ticks as a float from the
   high-res clock; run `round(elapsed)`; carry the error in an
   accumulator; add or drop one tick only when |error| ≥ 1. Keep the
   clamp of 6; apply the slow-motion multiplier to the same math.
3. `ONrGameState_Update` (`:4624`): when `deltaTicks < numActions`, fold
   the unconsumed actions' analog values into a carry added to the next
   frame's first action and OR their button bits.

**Check.** Trace on: all-1 histogram over a full level, dropped samples 0,
an occasional lone 2. Regression list: pause/unpause, cutscene skip,
slow-motion powerup, a GPU-bound scene (uncapped or 30 Hz mode), the
level-4 exterior and level-9 arenas by feel. Sweep matches baseline.

### Chunk 6 — #115 music loop (M)

**Verify.** TCTF Science Prison save point 4, several loads: catch the
opening bars repeating. Optional: `ONI_SOUND_TRACE=1` shows the base
track never re-selects a permutation.

**Implement.** `BFW_SS2_Platform_OpenAL.c`: `_Play` (`:291`) and
`_SetLooping` (`:307`) set `AL_LOOPING` to `AL_FALSE` always; the shadow
Looping bit stays. `BFW_SoundSystem2.c` `SS2rUpdate` (`:6774`): before
`SSiPlayingAmbient_UpdateList()`, sweep mono and stereo channels; for any
channel with the Looping bit set, a non-NULL `group` and a stopped
source, call `SSrGroup_Play(channel->group, channel, "sound channel",
NULL)`. This is the Mac callback's semantic. Order matters: the sweep
must precede the ambient update (body-playing's stale-flag check at
`:4334`).

**Check.** Same save point: the opening part gives way every load. A
scripted mid-level music change. A cutscene fade-out (graceful stop
path: current permutation finishes, out-sound plays). Gunfire and
ambients unaffected (cache hit ratio in the terminate stats stays
where it was).

---

## Session C — safety, housekeeping, the cut

### Chunk 7 — #78 hardening and trace (S)

- Trace: in `LIiPlatform_Keyboard_GetData` (`BFW_LI_Platform_SDL.c:476`),
  when the translated key is `LIcKeyCode_Escape`, log the scancode and
  `SDL_GetKeyFromScancode` value once per press. Plus a startup probe
  under `ONI_INPUT_TRACE`: list every scancode whose keycode is 27.
  Ten-second read: anything other than scancode 41 is the bug.
- Hardening: on the open branch of `OWrOniWindow_Toggle`
  (`Oni_Windows.c:1316`), zero movement/attack bits in
  `localInput.buttonIsDown` / `buttonWentDown` and the turn deltas so
  the snapshot cannot drive the character while the menu is up.
- `grep escape ~/Library/Application\ Support/OniARM64/key_config.txt`
  once, to rule the keypad-star binding in or out on your machine.

**Check.** Deliberate Escape still opens and closes the menu; a character
mid-creep stops when it opens. Leave `ONI_INPUT_TRACE=1` on for play.

### Chunk 8 — issue housekeeping (S)

- #84: gate the gamma slider on the ramp call's return (grey or annotate)
  so it stops lying; final-pass gamma stays with the next Metal work.
- #94: close as duplicate of #73.
- #90: tick chapters 10–14 on simX's testimony; leave the staged tests
  and the M2 fog / #84 spot-checks as written.
- #30: note the leak fix; decide parked vs superseded-by-onipack.
- #49 / #115 / #114 / #113: close on verified checks with one line each.
- #91: nothing until the persist bump.

### Chunk 9 — r6 cut, dry run first (M)

- Release notes = CHANGELOG Unreleased plus lines for chunks 1–7. Call
  out that r5's launch crash with long pack names is fixed and that the
  installer now caps names.
- The pipeline's installer path has never run for real: do one full
  `make oni_app_release` as a dry run (two notary round-trips plus the
  DMG), install from the DMG on a clean user, drop a depot zip on the
  installer, launch. Only then publish.
- r6 carries the persist backup guard; the v16 bump waits for the release
  after it (see the #73 section of the sweep doc).

---

## After C — the controller decision

Two rational choices, both fine; pick one and write it on #73:

- **1.0 without the pad.** r6 soaks as the 1.0 candidate; #73 becomes the
  1.1 headline. #78 stays a known issue with the chunk-7 hardening.
- **1.0 with the pad.** Push and re-rebase the branch, take the chunk-7
  hardening plus trace extension as the #78 mitigation, run
  CHECKPOINT A, then Tasks 4–5 with #49 already landed so the feel can
  be tuned honestly. Three to five sessions of pad-in-hand checkpoints.

Either way, nothing in Sessions A–C depends on the choice.
