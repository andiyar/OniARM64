# Cross-check of the 2026-09-22 issue review and plan (2026-09-23)

Independent second look at the open issues, then a comparison against the
four docs on `claude/new-issues-review-4i66ex`:
`plan-2026-09-22-next-session.md`, `triage-2026-09-22-tester-issues.md`,
`rca-2026-09-22-issue-49-sluggish.md`, `rca-2026-09-22-open-issue-sweep.md`.

Method: four read-only investigators went through the open issues without
seeing those docs (texture/installer, gameplay/UI, audio/perf/input,
backlog/release). Their findings were then compared with the plan. The two
disagreements that mattered were checked by hand against `main@6053051`.
Static analysis only; nothing was run. No code changed.

**Verdict:** the plan is sound in structure (verify → implement → check,
with stop conditions) and about 80% right in substance. Its #115 and r5
crash-report analysis is stronger than the independent pass. It needs the
amendments below before execution; A1 and A2 are the important ones.

---

## Where the two reviews agree (independently reached)

| Issue | Shared conclusion |
|---|---|
| #111 / #112 | One bug: engine leaf cap is 31 chars (`BFcMaxFileNameLength` = 32 incl. NUL, `BFW_FileManager.h:24`); installer allows 32-char names → leaves up to 43. 35+ chars → silently skipped ("unable to get level info"); 32–34 → registers, then level-0 load fails at `BFW_FileManager_Linux.c:145`. Both reproduced simX's 15-pack table; the six Character Retexture packs are **not loading at all**. |
| #114 | `MSiSprite_Draw_Unoriented` uses `drawWidth*500/640` and `drawHeight*500/480` (`MS_GC_Method_Geometry.c:1792-1793`, again at `:2298-2299`). Fix: both from height. No change at 4:3. (Traced via different callers — laser dot vs `WPrDrawSprite` crosshair — same sink.) |
| #115 | OpenAL maps the engine's "looping" to `AL_LOOPING`, so the first music permutation repeats forever; Mac/Win32 platforms re-picked via `SSrGroup_Play` on buffer end. |
| #49 | Millisecond clock floored to 60ths vs a vsync-paced loop → 0/1/2 ticks per frame in drifting stretches. Diagnostic histogram first, then high-res clock. |
| #84, #89, #91, #94, #100, #101, #90 | Same reads: #84 design fork (grey the slider first), #89 needs one Options visit, #91 rides #73 Task 7, #94 duplicate of #73, #100/#101 stay parked, #90 chapter boxes can be ticked on simX's run. |

Fable's evidence was stronger on one point: it had the #112 `.ips` and tied
the r5 crash to the `(UUtUns32)` name-pointer cast fixed in `2bcbdb3`
(2026-07-24, one week after the r5 tag at `2ebaa50`, 2026-07-17 — checked).
The independent pass could not fetch the attachment (403).

---

## Amendments to the plan

### A1 — #78 analysis targets code that does not ship (chunk 7) — IMPORTANT

`Oni_Windows.c` is wrapped in `#if TOOL_VERSION` (line 3 to EOF). The
shipping build uses `Oni_Windows2.c` (`#if SHIPPING_VERSION`), where
`OWrOniWindow_Toggle` (~`:192`) calls `ONrOutGameUI_MainMenu_Display()` →
`WMrDialog_ModalBegin(...)` (`Oni_OutGameUI.c:1243-1270`): a **blocking
modal**. That matches the July lldb stack exactly.

Consequences:
- The sweep doc's "Toggle does not pause the world; the character keeps
  creep-strafing under the menu" reconciliation does not hold for the
  shipping build. While the menu is up, the world is frozen.
- Chunk 7's hardening (zero `localInput` on "the open branch of
  `OWrOniWindow_Toggle`, `Oni_Windows.c:1316`") patches dead code.
- "Stuck strafing" and "phantom menu" may be **two separate bugs** merged
  into one issue.
- The "menu was invisible under Metal" inference rests on identical vertex
  counts. `gMetalRingCursor` counts the menu's draws too, so a frozen scene
  plus a menu also gives a constant count. Only a screenshot proves
  invisibility.

Revised chunk 7:
- Keep the trace: log the scancode (and `SDL_GetKeyFromScancode`) that
  produced any Escape; startup probe listing scancodes whose keycode is 27;
  keep the #83 auto-pause log line.
- Hardening instead: act on the Escape action only if a real, non-repeat
  `SDL_KEYDOWN` for `SDL_SCANCODE_ESCAPE` arrived since the last poll; log
  and drop otherwise.
- If held-input replay after the modal returns is still a concern, clear
  the snapshot in `Oni_Windows2.c` around the modal, not in `Oni_Windows.c`.
- Ask for a screenshot or screen recording at the next repro.

### A2 — #111 installer cap: plain `prefix(19)` collides (chunk 1) — IMPORTANT

"CharacterRetexture" is 18 chars, so Pt1…Pt7 and "Part2Mains" all truncate
to `CharacterRetextureP`. The installer then refuses packs 2–7 as "already
installed" (`Installer.swift:142`), or with `--replace` overwrites them.
Found independently by this review and by the texture investigator.

- Use a collision-proof short name: for example a 13-char prefix plus a
  6-char hash of the full name, or a smart abbreviation.
- Also check `opk_file_id` (`onipack_format.h:97`, a 24-bit weighted sum of
  the suffix) against already-installed packs. A collision makes the engine
  drop the second pack with a log line (`BFW_TM_Game.c` ~`:3307`).
- Fixture test: install two long, similarly named packs and assert distinct
  leaves ≤ 31 chars and distinct file IDs.

### A3 — #111/#112 tester workaround: don't advise renaming the files

The triage doc suggests renaming the `.dat/.raw/.sep` to a short suffix. The
file ID is derived from the suffix (`opk_file_id`, mirroring
`TMrUtility_LevelInfo_Get`), and onipack writes that ID into the pack. A
renamed pack may be internally inconsistent. Check this before posting.
Safer advice: shorten `NameOfMod` in `Mod_Info.cfg` (or the drop name the
installer uses) and reinstall.

### A4 — #111 knock-on: character packs will load for the first time

Once the name fix lands, the six Character Retexture packs apply through the
installer for the first time. Expect a new wave of visual reports (the
#63/`KS_face` alpha class, #61 anonymous-instance cases). Budget for it and
say so in the r6 notes.

### A5 — #113: verify tile geometry before the UV fix (chunk 4)

The plan assumes the retail TXMB grid is unchanged and only the tiles grew,
so rescaling each tile into its cell fixes it. But the #16 thread
(2026-05-25) records that HD Screens changes `TXMBpict_mainmenu` from
**7 sub-textures to 13** (and format 1→8). If the mod re-tiled the art,
retail tile *i* no longer covers the same region as HD tile *i*, and the UV
fix yields a correctly sized scramble.

- Add a verify step before implementing: dump the retail TXMBs (name,
  num_x/num_y, tile names and sizes) from `level0_Final.dat` next to the
  mod's TXMP sizes. `scripts/txmp-format-index.c` is the natural place to
  add a width×height print.
- If the grids match → Fable's `M3rDraw_BigBitmap` UV fix as written.
- If they differ → the installer skips TXMPs that are TXMB tiles with
  mismatched geometry (menus stay vanilla; S–M). Shipping the mod's own TXMB
  would reopen the #62 splash-TXMB risk.
- Also check whether the CuratedHD v2 pack (TXMP-only, included HD Screens)
  showed the same thing.

### A6 — #115: keep Fable's sweep, but only for multi-clip groups (chunk 6)

- Fable's ordering point is **correct and necessary** (checked):
  `SSiPAUpdate_BodyPlaying` (`BFW_SoundSystem2.c:4298-4336`) re-plays
  base1 when stopped, but then tests the stale `channel1_playing` /
  `channel2_playing` and falls into `BodyStopping`. A fix that only turns off
  `AL_LOOPING` and relies on the existing re-pick would stop music after one
  clip. The pre-update sweep is needed.
- But don't turn off `AL_LOOPING` for every channel. Keep it for groups
  with one permutation, so single-clip ambient loops (machinery, fire)
  don't get a gap at the loop seam. Apply the sweep path only to
  `num_permutations > 1`. Buffer queueing can make it gapless later.
- Add a listening check of an ambient loop seam, not only the cache-hit
  statistic.
- Secondary (low confidence): OpenAL `Stop`/`Play` never clear the Paused
  flag, so `SSrPlayingChannels_Resume` (`:6589`) can restart a playing
  source once. Cheap to fix alongside.
- Optional trace: `SSrGroup_Play` logging group name, permutation count,
  pick and looping flag. It confirms the Science Prison score really has
  several short parts.

### A7 — translated-block leak (chunk 2): prove nothing outlives the block

Before freeing `translatedBlock` in `TMiGame_InstanceFile_Delete`, confirm
no pointer into it survives the level unload (persistent instances, caches,
the dynamic file). Activity Monitor won't catch a use-after-free. Do one
audit of references plus one ASan run over two save-point reloads.

### A8 — #49 (chunk 5): smaller first step, one extra hypothesis

- Land 5b.1 (high-res clock) and 5b.2 (snap-and-carry), re-run the trace,
  and only then decide on 5b.3 (carrying unconsumed actions). It may be
  unnecessary.
- Record a second hypothesis in the trace: input latency from frames in
  flight (Metal ring of 3, `metal_internal.h:69`; GL similar). Timestamp
  the poll and the present of the same frame. Trying 2 in flight is a cheap
  A/B.

### A9 — new issues not in the plan (filed after it)

- **#116** (double force shield): **original behaviour.**
  `ONiCharacter_DropInventoryItems` (`Oni_Character.c:8399-8453`) drops
  the `drop_shield` death-candy shield *and* the shield still worn
  (`shieldRemaining > 0`). This is untouched Bungie code, and simX's own
  comment links retail videos showing the same. Close with a one-line
  explanation.
- **#117** (quits after finishing): **original behaviour.** Winning level
  19 sets `ONgTerminateGame` and the outro plays during shutdown
  (`Oni.c:856-882`, `:1145-1153`; Bungie code, only a NULL-gl guard added).
  Returning to the menu would need the one-shot startup flow restructured
  (`Oni.c:1414-1440`). Label it an enhancement, low priority, M.

### A10 — sequencing and housekeeping

- **Cut r6 at the end of Session A, not Session C.** r5 (2026-07-17) is
  over two months old and ships the #112 launch crash. main is 57 commits
  ahead, and CHANGELOG "Unreleased" is already written. Chunks 5–7 can go
  out as r6.1. Keep the plan's order: the persist backup guard ships
  before the v16 bump.
- Before the cut: fix the two stale CHANGELOG lines ("chapters 10–14 await
  a playthrough"; the #78 note).
- #89: simX's build (`1.3.0r5+main@6053051`) already has the z-order fix.
  One Options screenshot from them can close it.
- #14: ekt1701 suggests removing `chamber_time = 0` at `Oni_Weapon.c:2246`
  (drop/pick-up fire-delay exploit). It's a gameplay change and conflicts
  with the vanilla rule. Needs a maintainer decision; give it its own issue.
- #73: the rebased controller branch still exists only in the local
  worktree. Push it (single-copy risk).

---

## Suggested session shape after amendments

- **A:** baseline (chunk 0) → #111/#112 with A2/A3 → #114 → #113
  geometry dump (A5) and whichever fix it indicates → leak with A7 → close
  #116/#117/#94 → **cut r6**.
- **B:** #49 trace, then clock fix (A8) → #115 with A6 → r6.1.
- **C:** #78 revised per A1 → #90 verdict spot-checks → controller decision.
