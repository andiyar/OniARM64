# RCA: #49 — "feels sluggish" at a locked 60 fps, renderer-independent, transient

Static analysis of the main loop's timing, 2026-09-22, from a clean clone
without running the game. Companion to
[triage-2026-09-22-tester-issues.md](triage-2026-09-22-tester-issues.md).

## Symptom, restated from the evidence

- Maintainer (#49): a busy level "feels a touch sluggish" under **both** GL
  and Metal, at a **locked 60 fps with clean frametime**. Level 9 too.
- simX (#49 comment, 2026-09-21): seen "a few times" in chapters 10–14,
  **transient within a level**, "resolved itself once I continued playing",
  "no rhyme or reason".

So: not render throughput (frametime is clean), not a renderer (both), not
level-specific (levels 4, 9, 10–14), comes and goes on its own. That profile
fits a timing defect in the loop that sits between the display clock and the
simulation clock, not a load problem.

## How a frame becomes simulation ticks today

The port's loop (`OniProj/OniGameSource/Oni.c:582`, `ONiRunGame`) is
vsync-paced: GL enables the swap interval at init
(`gl_utility.c:260` → `SDL_GL_SetSwapInterval(1)`) and Metal sets
`displaySyncEnabled = YES` with a 3-deep in-flight ring
(`metal_engine.mm:449`, `metal_internal.h:69`). One loop iteration per
vblank, so on a 60 Hz panel the loop runs at 60.000 Hz.

Each iteration decides how many fixed 1/60 s simulation ticks to run from the
wall clock:

1. `ONrGameState_UpdateServerTime` (`Oni_GameState.c:5466`) reads
   `UUrMachineTime_Sixtieths()` and adds the delta since last frame to
   `serverTime`.
2. `iComputeDeltaTicks` (`:4527`) runs `serverTime - gameTime` ticks, clamped
   to `cMaxTicksPerFrame` = 6.
3. `ONrGameState_Update` (`:4624`) calls `ONrGameState_ProcessHeartbeat`
   once per tick; tick *i* gets input action *i* from the frame's action
   buffer, or `NULL` when there is no such action (`:4660-4665`).

`UUrMachineTime_Sixtieths` on the SDL platform
(`BungieFrameWork/BFW_Source/BFW_Utility/Platform_SDL/BFW_Platform_SDL.c:117`)
is:

```c
Uint64 sdlTicks = iGetTickCount();   // SDL_GetTicks(): whole milliseconds
tempTicks = sdlTicks * 3 / 50;       // -> 60ths, truncated
```

That is a **millisecond-resolution** clock, floored to 60ths. The original
Mac build used `TickCount()` (native 60ths, `BFW_Platform_MacOS.c:357`); the
Windows build had the same `ms * 3 / 50` conversion
(`BFW_Platform_Win32.c:453`), so this is inherited from the PC port, not a
new mistake — but it interacts badly with a vsync-locked 60 Hz loop.

Input on this build is sampled **once per frame on the main thread**, not by
a timer: `UUmPlatform` is `UUmPlatform_Mac` under `__APPLE__` (`BFW.h:102`),
so `LIrUpdate` calls `mac_get_input_in_game()` (`BFW_LocalInput.c:1245`,
`:925`) and the 60 Hz interrupt sampler is compiled out (`:1049`). Each frame
therefore produces exactly one action holding that frame's relative mouse
delta (`SDL_GetRelativeMouseState`, `BFW_LI_Platform_SDL.c:434`), and
`LIrActionBuffer_Get` (`:401`) hands the buffer over and **clears it**.

## The defect: 0/1/2 ticks per frame, and dropped mouse samples

A 60 Hz frame is 16.667 ms. Sampling a whole-millisecond clock every
16.667 ms and flooring to 60ths does not yield "1" each time. The sixtieths
counter steps at ms = 17, 34, 50, 67, 84, 100, … (spacing 17, 17, 16) while
the frames land at 16.67 n + φ ms. Depending on the phase φ between the
display and the millisecond grid, the per-frame tick count is either a clean
`1 1 1 1 …` or the pattern `0 1 2 0 1 2 …`.

Simulated (Python, frames at exactly 60 Hz, clock as above):

| Phase φ (ms) | ticks per frame over 6000 frames |
|--------------|----------------------------------|
| 0.00         | 0: 958, 1: 4084, 2: 957          |
| 0.25 / 0.50  | 0: 2000, 1: 2000, 2: 1999        |
| 0.75 / 0.90  | 1: 5999                          |

A uniform sweep of φ gives **33 % of frames advancing 0 or 2 ticks** on a
60.00 Hz panel (2.6 % at 59.94 Hz, where the beat is fast enough to average
out). The display's pixel clock and the CPU's `mach_absolute_time` are
different oscillators, so φ drifts slowly (tens of ppm → the phase walks
through one full 16.67 ms period in minutes). Adding that drift plus 0.3 ms of
sampling jitter to the simulation produces exactly the reported texture:
stretches of tens of seconds where roughly half the frames are 0- or 2-tick
frames, then minutes that are clean, with no trigger visible to the player.

Two things go wrong in a bad stretch, and both are invisible to a frametime
graph (the frame still presents on every vblank):

1. **Judder.** Oni has no render interpolation: a 0-tick frame repeats the
   previous world state, a 2-tick frame jumps two steps. Motion at a rock
   solid 60 fps looks like it stutters. This is the "sluggish" feel.
2. **Mouse input is discarded.** On a 0-tick frame the tick loop runs zero
   times, so the frame's single action — including that frame's mouse delta
   — is never seen by `ONrGameState_ProcessHeartbeat` (turning happens only
   there: `:4323`, `HandleTurnInput`). The buffer has already been cleared,
   so the delta is gone. On the following 2-tick frame the second tick gets a
   `NULL` action, i.e. zero mouse delta (`:4327`). Net: in a `0 1 2` stretch
   **one third of all mouse motion is dropped**, and the rest arrives in
   uneven lumps. Aiming and camera feel heavy and inconsistent, which is what
   "sluggish but not laggy" describes. (`CArUpdate(0, numActions, …)` at
   `:4677` still sees the actions, so the camera is fed but the character's
   turn is not.)

The busy-level correlation in the original report is probably incidental:
the phase walk happens everywhere, and a crowded fight is where a dropped
turn or a doubled step is noticed. The two later data points (level 9,
chapters 10–14) already broke the level correlation.

Note the clamp at `cMaxTicksPerFrame` and the `ONrGameState_Pause` resume
(`:5832`, `machineTimeLast = now - 6`) are unrelated: they only shape the
first frame after a pause.

## Why it is renderer-independent and profiling-invisible

Both renderers are vsync-locked at the same 60 Hz; the defect lives in the
clock conversion and the tick loop that run before either renderer is
touched. The #48/#49 A/B ("Metal 60 fps locked, still sluggish") is exactly
what this predicts. Instruments will show a flat 16.7 ms frame and nothing
unusual, because the CPU and GPU are doing the right amount of work — the
*simulation* is stepping 0 or 2 times per frame.

## Fix shape

Two parts, plus a diagnostic to confirm before and after.

1. **High-resolution clock.** Make `UUrMachineTime_Sixtieths` derive from
   `SDL_GetPerformanceCounter()` (already exposed as `UUrMachineTime_High`,
   `BFW_Platform_SDL.c:248`): `ticks = (counter - base) * 60 / freq` in
   64-bit, monotonic from a base captured at init. Keep the 32-bit wrap
   semantics the callers expect (the value is compared with subtraction,
   so wrap is fine). This alone narrows the bad phase window from ~half the
   period to ~the sampling jitter (simulated: 33 % → ~3 % bad frames, still
   in bursts).
2. **Snap-and-carry tick derivation** in `ONrGameState_UpdateServerTime` /
   `iComputeDeltaTicks`: compute elapsed ticks as a float from the high-res
   clock, run `round(elapsed)` ticks, and carry the rounding error in an
   accumulator; only add or drop one extra tick when the accumulated error
   passes ±1 tick. On a 60 Hz panel this yields exactly 1 tick per frame and
   a single drift-correction tick every few minutes (simulated with 30 ppm
   drift and 0.3 ms jitter: `{1: 35998, 2: 1}` over 10 minutes, versus
   `{0: 516, 1: 34966, 2: 517}` for floor-of-absolute-time on the same
   high-res clock). Genuine hitches (elapsed ≈ 2.0) still round to 2, and
   the existing clamp of 6 still applies. 120 Hz ProMotion panels get
   `0 1 0 1 …`, which is correct and steady. Slow-motion (`multiplier` in
   `UpdateServerTime`) needs the same treatment on the halved rate.
3. **Never discard an input sample.** Hardening for the rare 0-tick frame
   that remains (drift correction, or a genuinely early vblank): in
   `ONrGameState_Update`, when `deltaTicks < numActionsInBuffer`, fold the
   unconsumed actions' analog values (mouse deltas) into a carry that is
   added to the next frame's first action, and OR their button bits in so a
   tap is not lost. Alternatively have `LIrActionBuffer_Get` hand the buffer
   back untouched when the caller ran no ticks.
4. **Diagnostic first** (fits the repo's env-gated discipline, e.g.
   `ONI_TICK_TRACE=1`): every 5 s log a histogram of `deltaTicks` per frame
   (0 / 1 / 2 / 3+ counts) and the number of frames where
   `numActionsInBuffer > deltaTicks` (dropped samples). Prediction: during a
   "sluggish" stretch the 0/2 share is 30–50 % and dropped samples are
   non-zero; during a good stretch both are ~0. After fixes 1+2 the
   histogram is all 1s with an occasional 2, and dropped samples stay 0. One
   evening of play with the trace on settles the diagnosis before any
   behaviour change ships.

Out of scope but worth a line: render-side interpolation (Daodan-style "FPS
smoothing") would hide residual stepping at non-60 Hz refresh rates. Not
needed for this bug.

## Verify

- Trace on, unfixed build: catch one sluggish stretch and confirm the
  histogram shows 0/2-tick frames and dropped samples at that time.
- Fixed build, same trace: all-1 histogram over a full level; subjective
  A/B on level 4 exterior and level 9 (the two maintainer arenas), and the
  TCTF Science Prison save point simX used.
- Regression check: pause/unpause, cutscene skip (`engine_delta_ticks = 32`
  path), slow-motion powerup, and a deliberately GPU-bound scene (uncapped
  or 30 Hz) still step correctly.
