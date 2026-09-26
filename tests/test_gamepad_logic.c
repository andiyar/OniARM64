// tests/test_gamepad_logic.c — build:
//   cc -Wall -Wextra tests/test_gamepad_logic.c \
//      BungieFrameWork/BFW_Source/BFW_LocalInput/Platform_SDL/BFW_LI_GamepadLogic.c \
//      -o $SCRATCH/tgl && $SCRATCH/tgl
#include <stdio.h>
#include "../BungieFrameWork/BFW_Source/BFW_LocalInput/Platform_SDL/BFW_LI_GamepadLogic.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; \
    printf("  FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } } while (0)

static void test_quantize(void) {
    // centre: nothing
    CHECK(LIrPadLogic_QuantizeStick(0, 0, 0.35f, 0.30f, 0) == 0, "centre idle");
    // full up (SDL -y = up): Up only
    CHECK(LIrPadLogic_QuantizeStick(0, -32768, 0.35f, 0.30f, 0) == LIcPadDir_Up, "full up");
    // diagonal up-right: two bits
    CHECK(LIrPadLogic_QuantizeStick(20000, -20000, 0.35f, 0.30f, 0)
          == (LIcPadDir_Up | LIcPadDir_Right), "diagonal");
    // below on-threshold from idle: still off
    CHECK(LIrPadLogic_QuantizeStick(0, -9000, 0.35f, 0.30f, 0) == 0, "sub-threshold off");
    // hysteresis: was on, now between off(0.30) and on(0.35) → stays on
    CHECK(LIrPadLogic_QuantizeStick(0, -10600, 0.35f, 0.30f, LIcPadDir_Up)
          == LIcPadDir_Up, "hysteresis hold");   // 10600/32768 ≈ 0.323
    // below off-threshold: releases
    CHECK(LIrPadLogic_QuantizeStick(0, -9000, 0.35f, 0.30f, LIcPadDir_Up) == 0,
          "hysteresis release");
    // prev Up|Right: Right's x falls into [off,on) band → holds; Up's y released
    CHECK(LIrPadLogic_QuantizeStick(10600, -9000, 0.35f, 0.30f,
          LIcPadDir_Up | LIcPadDir_Right) == LIcPadDir_Right, "diagonal hysteresis");
}

static void test_aim(void) {
    // inside dead zone: zero
    CHECK(LIrPadLogic_AimDelta(3000, 0, 0.15f, 8.0f) == 0.0f, "dead zone");
    // full deflection: scale (squared curve peaks at 1.0)
    float full = LIrPadLogic_AimDelta(32767, 0, 0.15f, 8.0f);
    CHECK(full > 7.9f && full <= 8.01f, "full deflection = scale");
    // sign follows axis
    CHECK(LIrPadLogic_AimDelta(-32767, 0, 0.15f, 8.0f) < -7.9f, "negative axis");
    // half deflection is well under half of scale (squared curve)
    float half = LIrPadLogic_AimDelta(16384, 0, 0.15f, 8.0f);
    CHECK(half > 0.0f && half < 4.0f, "squared curve");
}

static void test_dash_poll(void) {
    // Full sequence (R3 well after Up came on): OFF1, ON1, OFF2, then normal.
    // Each phase lasts at least one poll AND phase_ms. Return != 0 = withhold Up.
    { LItPadDashState s = {0};
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 0, 40) == 0, "Up on at t=0: emit");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 900, 40) == 0, "held Up, no press: emit");
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 1000, 40) != 0, "R3 at 1000: OFF1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1010, 40) != 0, "1010: still OFF1 (time)");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1045, 40) == 0, "1045: ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1050, 40) == 0, "1050: still ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1090, 40) != 0, "1090: OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1100, 40) != 0, "1100: still OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1135, 40) == 0, "1135: normal (second press)");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1300, 40) == 0, "1300: stays normal"); }
    // one poll minimum: a poll whose time already elapsed still sees each phase
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, 0, 40);
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 1000, 40) != 0, "late: OFF1 entered");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1100, 40) == 0, "late: ON1 entered");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1200, 40) != 0, "late: OFF2 entered, not skipped");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1300, 40) == 0, "late: normal"); }
    // press with Up not held does nothing
    { LItPadDashState s = {0};
      CHECK(LIrPadLogic_DashPoll(&s, 1, 0, 1000, 40) == 0, "no Up: no-op");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1005, 40) == 0, "no Up: nothing armed"); }
    // Up release mid-sequence aborts; re-hold emits immediately
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, 0, 40);
      LIrPadLogic_DashPoll(&s, 1, 1, 1000, 40);
      CHECK(LIrPadLogic_DashPoll(&s, 0, 0, 1045, 40) == 0, "release aborts");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1050, 40) == 0, "re-hold: no stuck suppression");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1100, 40) == 0, "re-hold: still normal"); }
    // second R3 during the sequence is ignored
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, 0, 40);
      LIrPadLogic_DashPoll(&s, 1, 1, 1000, 40);
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1045, 40) == 0, "re-press case: ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 1050, 40) == 0, "R3 again ignored: still ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1090, 40) != 0, "re-press case: OFF2 on schedule");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 1135, 40) == 0, "re-press case: normal on schedule"); }
    // phase_ms 0 disables synthesis
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, 0, 0);
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 1000, 0) == 0, "phase 0: no-op"); }
    // Short path: R3 inside the engine sprint window after the real push. The
    // push was the first tap, so one release + the hold is the second tap.
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, 0, 40);                 // Up on at t=0
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 100, 40) != 0, "short: R3 at 100 OFF");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 120, 40) != 0, "short: 120 still OFF");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 140, 40) == 0, "short: 140 normal");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 180, 40) == 0, "short: no OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 190, 40) == 0, "short: stays normal");
      // engine is now sprinting; a later R3 must play the full sequence even
      // though the real push is still inside the window
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 200, 40) != 0, "after short: OFF1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 240, 40) == 0, "after short: ON1 (full path)");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 280, 40) != 0, "after short: OFF2 (full path)");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 320, 40) == 0, "after short: normal"); }
    // R3 at t=300 after Up on at t=0: outside the window, full path
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, 0, 40);
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 300, 40) != 0, "t300: OFF1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 345, 40) == 0, "t300: ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 390, 40) != 0, "t300: OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 435, 40) == 0, "t300: normal"); }
    // R3 on the same poll Up comes on: OFF1 withholds that push, so the engine
    // never saw it as a tap; full path (no window hold, no real tap to wait on)
    { LItPadDashState s = {0};
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 500, 40) != 0, "same poll: OFF1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 545, 40) == 0, "same poll: ON1 (full path)");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 590, 40) != 0, "same poll: OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 635, 40) == 0, "same poll: normal"); }
    // Up on at t=0 emitted, R3 at t=50: short path
    { LItPadDashState s = {0};
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 0, 40) == 0, "t50: Up on emitted");
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 50, 40) != 0, "t50: OFF");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 90, 40) == 0, "t50: normal");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 130, 40) == 0, "t50: no OFF2"); }
    // R3 at t=150: re-press at ~190 ms, inside window minus margin: short path
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, 0, 40);
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 150, 40) != 0, "t150: OFF");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 190, 40) == 0, "t150: normal");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 240, 40) == 0, "t150: no OFF2"); }
    // R3 at t=200: re-press would land at ~240 ms, in the jitter band: full path,
    // OFF1 held until the push is at least window + margin (284 ms) old
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, 0, 40);
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 200, 40) != 0, "t200: OFF1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 245, 40) != 0, "t200: OFF1 held past phase min");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 283, 40) != 0, "t200: OFF1 held at 283");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 284, 40) == 0, "t200: ON1 at 284");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 300, 40) == 0, "t200: still ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 324, 40) != 0, "t200: OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 364, 40) == 0, "t200: normal"); }
    // wrap-around: now_ms crosses zero (SDL_GetTicks 32-bit wrap)
    { const unsigned int W = 0xFFFFFFF0u;
      LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, W, 40);                  // Up on just before wrap
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, W + 100u, 40) != 0, "wrap short: OFF");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, W + 120u, 40) != 0, "wrap short: still OFF");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, W + 145u, 40) == 0, "wrap short: normal");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, W + 190u, 40) == 0, "wrap short: no OFF2"); }
    { const unsigned int W = 0xFFFFFFF0u;
      LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 0, 1, W - 300u, 40);           // Up on 300 ms before
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, W, 40) != 0, "wrap full: OFF1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, W + 10u, 40) != 0, "wrap full: OFF1 across zero");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, W + 45u, 40) == 0, "wrap full: ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, W + 90u, 40) != 0, "wrap full: OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, W + 135u, 40) == 0, "wrap full: normal"); }
}

int main(void) {
    test_quantize(); test_aim(); test_dash_poll();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) printf("ALL GAMEPAD LOGIC TESTS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
