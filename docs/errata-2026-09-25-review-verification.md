# Errata for the 2026-09-22/23 review docs (verified 2026-09-25)

The five docs beside this one (`triage-2026-09-22-tester-issues.md`,
`rca-2026-09-22-issue-49-sluggish.md`, `rca-2026-09-22-open-issue-sweep.md`,
`plan-2026-09-22-next-session.md`, `crosscheck-2026-09-23-fable-plan.md`) were
written by remote sessions from static reads of `main@6053051`. On 2026-09-25
every load-bearing claim was re-checked against the source, BungieSource, the
community RE and the actual data files. They are kept as the record of the
analysis; read them with the corrections below. The corrected plan lives in the
maintainer's local plan directory (`2026-09-25-tester-wave-plan-of-attack.md`).

## What holds

- #111/#112: the 31-character leaf cap is the cause, and the r5 crash chain
  (the `(UUtUns32)` cast in the too-long error path, fixed by 2bcbdb3 a week
  after the r5 tag) is right.
- #114: the two `500/640` and `500/480` sprite-scale sites, and the
  height-based fix.
- #115: `AL_LOOPING` pins the first permutation; the stale-flag ordering point
  is correct.
- #49: the millisecond clock and the 0/1/2-tick band are real (C simulation).
- The translated-block leak is real.
- #116 and #117 are Bungie's original behaviour.

## What is wrong

**#111/#112 (triage doc).** The file that fails at level-0 load is the `.dat`'s
own leaf, re-checked in `BFrFileRef_Duplicate`, not the derived `.raw`; the
crashing file is `level0_BetterWarehouseTraining.dat` (34 chars), since level-0
load never opens `level1_` files. On HEAD the outcome is a silent quit with exit
status 0, not a crash. The rename workaround is unsafe: onipack bakes the file
id into every record header and the engine resolves it from the leaf name, so a
renamed pack dereferences NULL. Rebuild with `onipack import-sep` under a short
suffix instead. A plain `prefix(19)` cap collides on both folder name and file
id for the seven `CharacterRetexture*` packs (cross-check A2 was right, and the
id hash is a small weighted sum, so short names collide easily). One failing
overlay aborts level-0 load entirely; that needs its own hardening.

**#113 (triage doc, and cross-check A5).** The mechanism is wrong. HD Screens
does not ship larger versions of the retail tiles; it is a 1024×768 redesign
cut into a 4×3 grid of ordinary 256×256 tiles, with its own TXMBs and 1024×768
WMDD dialog layouts. The installer keeps only the tiles, six of which share
names with the retail 3×2 grid (`oni_kanji`, `options`, `levelNN_win`,
`fail0N`), so the retail grid shows the mod's first six tiles in the wrong
cells at 1:1. Nothing is magnified. The Load Game screen draws through
`WM_Picture` → `DCrDraw_TextureRef` (`WM_DrawContext.c`), not
`M3rDraw_BigBitmap`. The per-tile UV fix would change nothing here and would
squash vanilla edge tiles (they are power-of-two padded). The fix is to have
the installer skip tiles of any screen the mod re-tiles. The maintainer's own
CuratedHD pack contains the same tiles.

**#114 (triage doc).** The call chain was wrong: `FXrDrawLaserDot` has no
callers; the reticle is `ONrGameState_DisplayAiming` → `WPrDrawSprite` → the
same sink. The "500 = 240/tan(fovy/2)" arithmetic does not hold at the 45°
default; only the aspect-ratio argument matters. The fix also un-stretches
unrotated particles, stars, glows and the lens flare.

**#115 (triage doc; cross-check A6 partly).** The proposed global channel sweep
in `SS2rUpdate` is unsafe: `SSiPlayingAmbient_Halt`, `SSiPAUpdate_Done`, the
`InterruptOnStop` stop and `SSrStopAll` all leave the Looping bit and `group`
set, so the sweep would resurrect halted ambients and can hit a stale group
after level unload. Re-pick inside `SSiPAUpdate_BodyPlaying` instead, and gate
`AL_LOOPING` in the platform `_Play`/`_SetLooping` (it is re-armed on every
play) to non-group or single-permutation channels.

**#49 (RCA doc).** The camera also freezes on a 0-tick frame (follow mode does
nothing without ticks), so judder is a symptom in its own right. A
high-resolution clock alone does not remove 0/2-tick frames under read jitter;
the snap-and-carry derivation does. The bad band is narrow (about 4% of phases
at 60.000 Hz), so "transient" needs a slow display-vs-clock drift that is still
unmeasured. Missed: input is built one frame stale because the action is polled
before the SDL event pump. A scripted `slowmo` is a competing explanation.

**#78 (sweep doc).** The analysis targets `Oni_Windows.c`, which is compiled
out (`SHIPPING_VERSION=1` in the Oni target's CMakeLists). The shipping
`OWrOniWindow_Toggle` in `Oni_Windows2.c` opens a blocking modal; the world is
frozen under the menu, and the "keeps strafing under the menu" reconciliation
and the proposed hardening are dead code. The frozen world is re-rendered by
the modal's draw callback, so identical vertex counts cannot prove the menu was
invisible. The `main_menu_win` music in the July log is the menu's own music
and identifies freeze A as this menu. The cross-check's scancode-keyed filter
would break keypad-star or pad-Start escape bindings unless keyed by binding.
The build now runs sdl2-compat on SDL3, not the SDL of the July captures (#118).

**Leak (sweep doc).** Larger than stated (about 15 to 26 MB of address space per
level reload, plus the descriptor tables, which also leak on 64-bit). The
one-line fix would free an uninitialised pointer at exit: the dynamic instance
file never sets `translatedBlock` and `TMrGame_Terminate` deletes it. It needs
the initialisations, ordering after the dispose steps, and an ASan run.

**Cross-check A10.** The "two stale CHANGELOG lines" are in the published r5
section and are not stale; the README status line and the chapters-10-to-14
checkbox are the ones to update.

## New since the reviews

#118: the app bundle built on this machine since 3 September ships
sdl2-compat's `libSDL2` and no `libSDL3`, which it loads at runtime. A fresh
release build may not launch on a Mac without Homebrew SDL3. Blocks the next cut.
