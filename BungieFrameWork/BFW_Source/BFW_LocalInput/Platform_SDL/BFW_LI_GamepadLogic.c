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
	const unsigned int short_limit =
		LIcGamepadSprintWindowMs - LIcGamepadSprintMarginMs;
	int up_went_on = 0;
	int withhold;
	if (!up_held) { s->phase = 0; s->up_prev = 0; return 0; }
	if (!s->up_prev) {
		s->up_prev = 1;
		s->up_on_valid = 0;             // a tap only once it is emitted (below)
		up_went_on = 1;
	}
	if (s->phase == 0) {
		if (!dash_went_down || phase_ms == 0) {
			withhold = 0;
		} else {
			// the re-press lands >= phase_ms from now: short path only if
			// that is inside the window, less the jitter margin, after an
			// emitted push
			s->phase = 1;
			s->phase_start_ms = now_ms;
			s->short_path = s->up_on_valid && phase_ms < short_limit &&
				(now_ms - s->up_on_ms) < short_limit - phase_ms;
			// full path after an emitted push: hold OFF1 until that push is
			// out of the window (plus margin) so ON1 cannot sprint early
			s->hold_off1 = s->up_on_valid && !s->short_path;
			withhold = 1;
		}
	} else {
		if (now_ms - s->phase_start_ms >= phase_ms &&   // unsigned: wrap-safe
			!(s->phase == 1 && s->hold_off1 && (now_ms - s->up_on_ms) <
				LIcGamepadSprintWindowMs + LIcGamepadSprintMarginMs)) {
			if (s->phase == 3 || (s->phase == 1 && s->short_path)) {
				s->phase = 0;
				s->up_on_valid = 0;     // engine now sprinting; the push no longer counts
			} else {
				s->phase++;
			}
			s->phase_start_ms = now_ms;
		}
		withhold = (s->phase == 1 || s->phase == 3);
	}
	if (up_went_on && !withhold) {      // the engine saw this push: it is a tap
		s->up_on_valid = 1;
		s->up_on_ms = now_ms;
	}
	return withhold;
}
