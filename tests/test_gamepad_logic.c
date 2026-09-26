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
    // Two-tap sequence (#73): OFF1, ON1, OFF2, then normal. Each phase lasts
    // at least one poll AND phase_ms of wall time. Return != 0 = withhold Up.
    { LItPadDashState s = {0};
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 0, 20) == 0, "held Up, no press: emit");
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 0, 20) != 0, "R3 at t=0: OFF1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 10, 20) != 0, "t=10: still OFF1 (time)");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 25, 20) == 0, "t=25: ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 30, 20) == 0, "t=30: still ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 50, 20) != 0, "t=50: OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 60, 20) != 0, "t=60: still OFF2");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 75, 20) == 0, "t=75: normal (second press)");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 200, 20) == 0, "t=200: stays normal"); }
    // one poll minimum: a poll whose time already elapsed still sees the phase once
    { LItPadDashState s = {0};
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 0, 20) != 0, "OFF1 entered");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 100, 20) == 0, "late poll: ON1 entered");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 200, 20) != 0, "late poll: OFF2 entered, not skipped");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 300, 20) == 0, "late poll: normal"); }
    // press with Up not held does nothing
    { LItPadDashState s = {0};
      CHECK(LIrPadLogic_DashPoll(&s, 1, 0, 0, 20) == 0, "no Up: no-op");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 5, 20) == 0, "no Up: nothing armed"); }
    // Up release at t=25 aborts; re-hold emits immediately
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 1, 1, 0, 20);
      CHECK(LIrPadLogic_DashPoll(&s, 0, 0, 25, 20) == 0, "release aborts");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 30, 20) == 0, "re-hold: no stuck suppression");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 60, 20) == 0, "re-hold: still normal"); }
    // second R3 during the sequence is ignored
    { LItPadDashState s = {0};
      LIrPadLogic_DashPoll(&s, 1, 1, 0, 20);
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 25, 20) == 0, "re-press case: ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 30, 20) == 0, "R3 at t=30 ignored: still ON1");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 50, 20) != 0, "re-press case: OFF2 on schedule");
      CHECK(LIrPadLogic_DashPoll(&s, 0, 1, 75, 20) == 0, "re-press case: normal on schedule"); }
    // phase_ms 0 disables synthesis
    { LItPadDashState s = {0};
      CHECK(LIrPadLogic_DashPoll(&s, 1, 1, 0, 0) == 0, "phase 0: no-op"); }
}

int main(void) {
    test_quantize(); test_aim(); test_dash_poll();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) printf("ALL GAMEPAD LOGIC TESTS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
