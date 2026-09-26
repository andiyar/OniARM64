// BFW_LI_GamepadLogic.c — pure gamepad mapping math (#73). See header.
#include "BFW_LI_GamepadLogic.h"
#include <math.h>

unsigned int LIrPadLogic_QuantizeStick(
	int x, int y, float on_frac, float off_frac, unsigned int prev_bits)
{
	const float fx = (float)x / 32768.0f;
	const float fy = (float)y / 32768.0f;
	unsigned int bits = 0;
	struct { unsigned int bit; float value; } axes[4] = {
		{ LIcPadDir_Up,    -fy }, { LIcPadDir_Down,  fy },
		{ LIcPadDir_Left,  -fx }, { LIcPadDir_Right, fx },
	};
	int i;
	for (i = 0; i < 4; i++) {
		const float thresh = (prev_bits & axes[i].bit) ? off_frac : on_frac;
		if (axes[i].value > thresh) { bits |= axes[i].bit; }
	}
	return bits;
}

float LIrPadLogic_AimDelta(int axis, int other, float dead_frac, float scale)
{
	const float fa = (float)axis / 32768.0f;
	const float fo = (float)other / 32768.0f;
	const float mag = sqrtf(fa * fa + fo * fo);
	float t;
	if (mag < dead_frac) { return 0.0f; }
	// rescale so the curve starts at 0 just outside the dead zone
	t = (mag - dead_frac) / (1.0f - dead_frac);
	if (t > 1.0f) { t = 1.0f; }
	// squared response on magnitude, direction from this axis' share
	return scale * t * t * (fa / (mag > 0.0f ? mag : 1.0f));
}

int LIrPadLogic_DashPoll(LItPadDashState *s, int dash_went_down,
	int up_held, unsigned int now_ms, unsigned int phase_ms)
{
	// A phase is entered on some poll and can only advance on a later poll,
	// so each lasts at least one poll.
	if (!up_held) { s->phase = 0; s->up_prev = 0; return 0; }
	if (!s->up_prev) {
		s->up_prev = 1;
		s->up_on_ms = now_ms;           // the real push is a tap
		s->up_on_valid = 1;
	}
	if (s->phase == 0) {
		if (!dash_went_down || phase_ms == 0) { return 0; }
		s->phase = 1;
		s->phase_start_ms = now_ms;
		// the re-press lands >= phase_ms from now: short path only if that
		// is still inside the window after the real push
		s->short_path = s->up_on_valid && phase_ms < LIcGamepadSprintWindowMs &&
			(now_ms - s->up_on_ms) < LIcGamepadSprintWindowMs - phase_ms;
		return 1;
	}
	if (now_ms - s->phase_start_ms >= phase_ms) {   // unsigned: wrap-safe
		if (s->phase == 3 || (s->phase == 1 && s->short_path)) {
			s->phase = 0;
			s->up_on_valid = 0;         // engine now sprinting; the push no longer counts
		} else {
			s->phase++;
		}
		s->phase_start_ms = now_ms;
	}
	return (s->phase == 1 || s->phase == 3);
}
