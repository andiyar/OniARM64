// tests/test_gamepad_logic.c — build:
//   cc -Wall -Wextra tests/test_gamepad_logic.c \
//      BungieFrameWork/BFW_Source/BFW_LocalInput/Platform_SDL/BFW_LI_GamepadLogic.c \
//      -o /tmp/test_gamepad_logic && /tmp/test_gamepad_logic
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

static void test_dash(void) {
    LItPadDashState s = {0};
    // no dash press: no suppression
    CHECK(LIrPadLogic_DashTick(&s, 0, 1, 2) == 0, "idle");
    // dash pressed while moving: suppress for gap_ticks ticks
    CHECK(LIrPadLogic_DashTick(&s, 1, 1, 2) == 2, "gap starts");
    CHECK(LIrPadLogic_DashTick(&s, 0, 1, 2) == 1, "gap counts down");
    CHECK(LIrPadLogic_DashTick(&s, 0, 1, 2) == 0, "gap ends");
    // dash pressed while NOT moving: no-op
    CHECK(LIrPadLogic_DashTick(&s, 1, 0, 2) == 0, "needs direction held");
    // gap_ticks == 0: dash synthesis disabled, never suppress
    { LItPadDashState s0 = {0};
      CHECK(LIrPadLogic_DashTick(&s0, 1, 1, 0) == 0, "gap_ticks 0 no-op"); }
    // re-press mid-gap must not extend the gap
    { LItPadDashState s2 = {0};
      LIrPadLogic_DashTick(&s2, 1, 1, 3);              // starts: returns 3, now 2 left
      CHECK(LIrPadLogic_DashTick(&s2, 1, 1, 3) == 2, "re-press ignored mid-gap");
      CHECK(LIrPadLogic_DashTick(&s2, 0, 1, 3) == 1, "gap continues");
      CHECK(LIrPadLogic_DashTick(&s2, 0, 1, 3) == 0, "gap ends"); }
}

int main(void) {
    test_quantize(); test_aim(); test_dash();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) printf("ALL GAMEPAD LOGIC TESTS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
