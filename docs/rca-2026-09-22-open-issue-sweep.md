# Open-issue sweep with root-cause notes (2026-09-22)

Static pass over every open issue not already covered by
[triage-2026-09-22-tester-issues.md](triage-2026-09-22-tester-issues.md)
(#111–#115) and
[rca-2026-09-22-issue-49-sluggish.md](rca-2026-09-22-issue-49-sluggish.md)
(#49). Done from a clean clone without running the game. One open bug gets a
full root-cause section (#78); one new defect surfaced on the way (a per-level
memory leak in the 64-bit loader, filed under #30 below); the rest are
tracking or already-diagnosed items with a one-line status so the board can
be read at a glance.

## Board at a glance

| Issue | Kind | State after this sweep |
|-------|------|------------------------|
| #78 phantom Escape → in-game menu | bug, intermittent | Producers enumerated below; one testable keyboard-side candidate; the "frozen world vs still strafing" tension in the record is resolved from code; a defensive hardening is proposed that removes the harm whatever the producer is |
| #84 gamma slider inert | diagnosed by maintainer (2026-08-25) | `SDL_SetWindowGammaRamp` unsupported on macOS 15; design fork recorded on the issue. Nothing new; suggestion on sequencing below |
| #30 heap corruption, OniSplit rebake | parked, sidestepped by onipack | No new corruption lead, but the loader **leaks the whole translated block on every level unload** (new, concrete, fix is one line) |
| #91 persist downgrade guard | landed (ace830e) | Remaining items (release-note line, length-aware-read test) ride with #73 Task 7 |
| #89 Metal M5 renderer toggle | fix landed (6053051) | Awaiting one Options visit to confirm the checkbox paints above the dialog art |
| #101 / #100 Particle3 and TOOL_VERSION latents | parked | Fix shapes already recorded on the issues; nothing reachable today |
| #94 controller request | duplicate | Close as duplicate of #73 |
| #90 chapter 10–14 march | tracking | simX cleared 10–14 (comment on #90); tick the chapter boxes on that testimony, keep the staged tests (#28/#66/#74/#47/#55) owed |
| #73 / #75 / #16 / #14 | feature / roadmap | No RCA content; unchanged |

---

## #78 — phantom Escape opens the in-game menu mid-play

### What the record establishes

The 2026-07-10 lldb trap is the anchor: `ONrGameState_ProcessHeartbeat →
ONrGameState_ProcessActions → ONiGameState_ProcessMiscActions →
OWrOniWindow_Toggle → ONrOutGameUI_MainMenu_Display → WMrDialog_ModalBegin`.
That is the Escape branch at `Oni_GameState.c:3981`
(`buttonWentDown & LIc_BitMask_Escape`, gated by `CanEscapeKey`). So an
action reached the heartbeat with the Escape bit set while nobody pressed
Escape. Everything below is about where that bit can come from, and what
happens once it has.

### Every producer of the Escape action bit (current tree)

Actions are built once per frame on the main thread (`mac_get_input_in_game`,
`BFW_LocalInput.c:924`) from `LIrPlatform_PollInputForAction`
(`BFW_LI_Platform_SDL.c:725`): mouse buttons/axes, then every scancode that
`SDL_GetKeyboardState` reports down, translated to an Oni key code and looked
up in the binding table (`LIrActionBuffer_Add`, `BFW_LocalInput.c:331`).
A bit is set only if some *binding* names the "escape" action.

1. **Bindings.** Init binds `LIcKeyCode_Escape` and `LIcKeyCode_Multiply`
   (keypad `*`) to "escape" (`BFW_LocalInput.c:1091-1092`). But every
   install's `key_config.txt` starts with `unbindall` (`Oni.c:1057`), and
   `LIrBindings_RemoveAll` (`:663`) puts back **only** the physical Escape.
   So with a stock config there is exactly one binding, and the Multiply
   route is dead unless the player's own `key_config.txt` binds
   `multiply to escape` (the maintainer's file is worth a `grep escape`).
2. **Key translation.** For the Escape key code, both translation paths end
   in the same place: the positional path (`sdl_scancode_to_oni_keycode`,
   post-#93) delegates every non-printable to the layout path, and the
   layout path (`sdl_to_oni_keycode`, `:215`) returns any SDL keycode
   `< 0x80` **as-is**. So the Escape bit rises for *any scancode whose SDL
   keycode is 27*. On macOS SDL builds that keycode map from the active
   layout via `UCKeyTranslate`, and the map is not restricted to the Escape
   scancode. The one Apple key whose layout character is `0x1B` besides
   Escape is the keypad **Clear** key (`kVK_ANSI_KeypadClear`, which SDL
   files as `SDL_SCANCODE_NUMLOCKCLEAR`). This is a candidate only if an
   external keyboard with a keypad is attached; it is also the cheapest
   thing to rule in or out (below).
3. **Focus-loss auto-pause (#83, landed 2026-07-17, after the July
   captures).** `SDL_WINDOWEVENT_FOCUS_LOST` / `MINIMIZED` set a pending
   flag (`BFW_LI_Platform_SDL.c:891`) that the heartbeat fires through the
   very same `OWrOniWindow_Toggle` (`Oni_GameState.c:3991`). Any macOS
   focus change now opens the in-game menu **by design**: a Spotlight or
   notification interaction, a build from a parallel dev session raising
   its window, an app-switcher tap. This could not have caused the July
   events, but it is a second producer with an identical presentation, so
   any *future* report must first be split on the
   `[input-trace] auto-pause requested` line.
4. **Not producers:** film playback (only drives animations), networking
   (dead), `ONiRemapKeys` (adds punch/kick bits, never Escape), the
   cutscene mask (`ZeroUserInput` keeps Escape but never adds it),
   controllers (no joystick code in tree; the #73 branch stays unmerged
   for exactly this reason), the tick/action replay from #49 (a `NULL`
   action replays the last snapshot and cannot create an edge).

### Reconciling "keeps strafing" with "frozen frames"

The record holds both observations and flags them as in tension. They are
not: they are two different states on the same path.

- `OWrOniWindow_Toggle` (`Oni_Windows.c:1316`) makes the Oni window visible
  and switches input to `LIcMode_Normal`. It does **not** pause the world.
  `ONrGameState_Pause` is called only by the F1 pause screen and the
  out-of-level main menu (`Oni_Dialogs.c:1234`, `:1443`). The main loop keeps
  calling `ONrGameState_Update`; action production has stopped, so every
  tick gets a `NULL` action and `ONiGameState_ScanButtons` replays the last
  `buttonIsDown` snapshot. A character that was creep-strafing when the
  menu opened **keeps creep-strafing**, with the keyboard dead. That is the
  original report, verbatim.
- The two ~50 s stretches of identical vertex counts are the *modal*
  sub-dialogs (`WMrDialog_ModalBegin`, `WM_Dialog.c:995`) reached by
  mashing keys into the unseen menu: that loop draws only the dialog, never
  the world, so the last composed frame is what stays on the Metal drawable.
  Freeze A was reached via the menu music cue, freeze B silently, and the
  session ended in the Quit confirmation, all consistent with blind
  navigation of the menu tree.

Why the menu itself was not seen in July remains unexplained from code:
the same toggle draws a visible menu on a deliberate Escape. Nothing in the
draw path branches on how the toggle was reached, so the next traced repro
is still the only way to settle that half.

### Fix shape

The producer is still unproven, so two independent moves:

1. **Rule the keyboard-map candidate in or out in ten seconds.** A trace
   line at the poll site (`LIiPlatform_Keyboard_GetData`,
   `BFW_LI_Platform_SDL.c:476`): when the translated key is
   `LIcKeyCode_Escape`, log the scancode and `SDL_GetKeyFromScancode` value
   that produced it, once per press. If it ever says anything other than
   scancode 41, the translation is the bug and the fix is to translate
   Escape positionally (accept only `SDL_SCANCODE_ESCAPE`) instead of by
   keycode. Also worth one probe run: iterate all scancodes at startup and
   log any with keycode 27; that prints the answer without waiting for a
   repro.
2. **Remove the harm whatever the producer is.** The damaging part is the
   snapshot replay driving the character while input is not in game mode.
   On the open branch of `OWrOniWindow_Toggle`, zero the movement and
   attack bits of `ONgGameState->local.localInput` (`buttonIsDown`,
   `buttonWentDown`) and the turn deltas. The character stops when the menu
   opens instead of creeping into a striker; a deliberate Escape is
   unaffected because the player has let go of the keys anyway. This is a
   small departure from Bungie's behaviour (the world keeps running under
   the Esc menu and the held keys keep acting), but it is strictly safer
   and does not change what a deliberate menu does. Pausing the world under
   the in-game menu would be the larger change and is not needed.
3. Keep `ONI_INPUT_TRACE=1` set during normal play, as the record already
   asks: the existing trace names `LIrMode_Set`'s caller and shows the
   auto-pause line, which now splits producers 1–3 in one log.

---

## #30 — the translated block leaks on every level unload (new)

Reading the 64-bit loader for the OniSplit-rebake corruption, one concrete
defect surfaced that is real on the stock data path, not only on rebakes:

- `TMiGame_InstanceFile_New_FromFileRef` allocates
  `translatedBlock` per instance file, sized at roughly **twice** the file's
  instance-body bytes plus slack (`BFW_TM_Game.c:1815-1825`), and translates
  every instance into it. The side table `map` is freed after pass 2
  (`:2033`), but the block itself is never released:
  `TMiGame_InstanceFile_Delete` (`:2133`) frees the pointer lists, unmaps
  the file mappings, frees the descriptors and the struct, and does not
  touch `translatedBlock`. The 2026-05-27 comment on #30 noted this as
  "non-blocking"; it has stayed unfixed.
- Cost: every `TMrLevel_Unload` → `TMiGame_LoadedInstanceFiles_Remove`
  leaks a block of ~2× that level's `.dat` instance bodies. A level `.dat`
  is tens of MB, so each save-point load or level transition leaks tens of
  MB, permanently for the process. A session that reloads a save point ten
  times (simX's #115 repro style) leaks on the order of a gigabyte. The
  "few GB of swap" noted in the original #49 report is consistent with
  this, though #49's feel has its own cause.
- Fix: in `TMiGame_InstanceFile_Delete`, `UUrMemory_Block_Delete
  (inInstanceFile->translatedBlock)` before the struct is freed, guarded
  for NULL (the dynamic file at `:1599` has none). One line, plus a startup
  log of the block size per file behind `ONI_DIAG_VERBOSE` if you want to
  see the numbers once.
- Verify: Activity Monitor real memory after ten save-point reloads stays
  flat instead of climbing by a fixed step each time.

On #30 itself: the rebake path (`OniSplit -import:nosep`, PC format) is no
longer used by anything in the tree, and the shipping pack pipeline (onipack,
TXMP-only overlays) never enters the body bridge with rebuilt levels. The
untested lead (re-bake with `-type:macintel`) is still the only cheap next
step if the issue is ever unparked; otherwise it can close as
"superseded by onipack" with the leak fix credited here.

---

## #84 — gamma slider

Maintainer-verified inert on macOS 15 (`SDL_SetWindowGammaRamp` returns
-1). Nothing to add to the mechanism. One sequencing note: option 3 from the
issue (gate the control on the ramp call's return so the slider stops lying)
is a few lines and can ship in the next cut on its own; option 1 (final-pass
gamma under Metal) belongs with the next Metal milestone, not with this bug.

## #91 — persist downgrade guard

`ace830e` backs up `persist.dat` as raw bytes before any version-mismatch
reset, keep-first, so the downgrade wipe is recoverable. The issue is now a
tracker for the release-note line and the length-aware-read test that must
land with the v15→v16 bump in #73 Task 7; no engine work outstanding here.

## #89 — Metal M5

Root cause of the invisible checkbox was draw order (runtime-created child
appended after `pict_options_background`); `6053051` moves it to the head of
the child list. One Options visit confirms; then the M5 box in the README
can be ticked and the issue closed.

## #101 / #100 — parked latents

Both carry their fix shapes already. Nothing in the current data path or
tooling reaches either; no change recommended.

## #94 — controller request

Feature request duplicating #73 (which has the design and an unmerged
branch). Close as duplicate with a pointer.

## #90 — chapter 10–14 march

simX's playthrough is the campaign verification the issue asks for. Chapter
boxes 10–14 can be ticked on that testimony; the pending-verification
verdict pass and the staged tests remain as written. The M2 fog spot-check
and the #84 minute check are the two items nobody has done.
