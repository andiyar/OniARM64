// BFW_LI_GamepadLogic.h — pure gamepad mapping math (#73).
// Libc-only: unit-tested standalone (tests/test_gamepad_logic.c).
#ifndef BFW_LI_GAMEPADLOGIC_H
#define BFW_LI_GAMEPADLOGIC_H

// Left-stick quantize output bits (module-local, NOT LIc_Bit* — the caller
// maps them to pad_ls_* device inputs, which bindings map to actions).
#define LIcPadDir_Up    0x1
#define LIcPadDir_Down  0x2
#define LIcPadDir_Left  0x4
#define LIcPadDir_Right 0x8

// Quantize a stick position (SDL range -32768..32767) into direction bits
// with hysteresis: a direction turns ON past 'on_frac' deflection and OFF
// below 'off_frac' (on_frac > off_frac). prev_bits carries state per tick.
unsigned int LIrPadLogic_QuantizeStick(
	int x, int y,               // +y = SDL down
	float on_frac, float off_frac,
	unsigned int prev_bits);

// Right-stick aim: circular dead zone then squared response curve.
// Returns per-tick delta in mouse-equivalent units (caller multiplies by
// sensitivity). 'axis' is one SDL axis (-32768..32767); 'other' the
// perpendicular axis (for the circular dead-zone radius test).
float LIrPadLogic_AimDelta(int axis, int other, float dead_frac, float scale);

// Dash gap: tiny state machine. Call once per tick with "dash button went
// down this tick" and "any direction held". Returns the number of remaining
// suppression ticks (>0 means: do NOT emit direction inputs this tick).
typedef struct { int gap_remaining; } LItPadDashState;
int LIrPadLogic_DashTick(LItPadDashState *s, int dash_went_down,
	int direction_held, int gap_ticks);

#endif
