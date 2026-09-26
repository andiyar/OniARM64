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

// Dash synthesis (#73). The engine's only double-tap is the forward sprint
// (two forward went-downs under 15 ticks apart), so an R3 press while Up is
// held plays a full two-tap: OFF1 (withhold Up), ON1 (emit), OFF2 (withhold),
// then normal emission, which is the second press. Each phase lasts at least
// one poll AND phase_ms of wall time (a frame runs 0, 1 or 2 ticks, #49).
// Call once per poll. Returns nonzero: do NOT emit Up this poll. Releasing
// Up aborts; an R3 press during the sequence is ignored; phase_ms 0 disables.
typedef struct { int phase; unsigned int phase_start_ms; } LItPadDashState;
int LIrPadLogic_DashPoll(LItPadDashState *s, int dash_went_down,
	int up_held, unsigned int now_ms, unsigned int phase_ms);

#endif
