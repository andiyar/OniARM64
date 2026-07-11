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

int LIrPadLogic_DashTick(LItPadDashState *s, int dash_went_down,
	int direction_held, int gap_ticks)
{
	if (dash_went_down && direction_held && s->gap_remaining == 0) {
		s->gap_remaining = gap_ticks;
	}
	if (s->gap_remaining > 0) {
		int r = s->gap_remaining;
		s->gap_remaining--;
		return r;
	}
	return 0;
}
