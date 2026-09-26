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
// below 'off_frac' (on_frac > off_frac). prev_bits carries state per poll (frame).
unsigned int LIrPadLogic_QuantizeStick(
	int x, int y,               // +y = SDL down
	float on_frac, float off_frac,
	unsigned int prev_bits);

// Right-stick aim: circular dead zone then squared response curve.
// Returns per-poll (frame) delta in mouse-equivalent units (caller multiplies by
// sensitivity). 'axis' is one SDL axis (-32768..32767); 'other' the
// perpendicular axis (for the circular dead-zone radius test).
float LIrPadLogic_AimDelta(int axis, int other, float dead_frac, float scale);

// Dash synthesis (#73). The engine's only double-tap is the forward sprint:
// a forward went-down under 15 ticks after the previous recorded forward tap
// sprints (Oni_Character.c); a real push of the stick is itself a tap.
// An R3 press while Up is held plays one of two sequences on the Up emission:
//  - short: the re-press would land inside the sprint window (less a jitter
//    margin) after a push the engine saw (Up emitted, not withheld): OFF1
//    (withhold Up), then normal emission, so the hold is the second tap;
//  - full: otherwise, or once a sequence has already produced a sprint:
//    OFF1, ON1 (first tap), OFF2, then normal emission (second tap). After
//    an emitted push, OFF1 is held until that push is window + margin old so
//    ON1 cannot complete a double-tap early.
// Each phase lasts at least one poll AND phase_ms of wall time, since a frame
// runs 0, 1, 2 or more ticks and a 0-tick frame discards its poll (#49).
// Call once per poll with now_ms. Returns nonzero: do NOT emit Up this poll.
// Releasing Up aborts; an R3 press during a sequence is ignored; phase_ms 0
// disables. Times are unsigned ms, so the 32-bit SDL_GetTicks wrap is safe.
#define LIcGamepadSprintWindowMs 250u   // engine window: 15 ticks at 60 Hz
#define LIcGamepadSprintMarginMs  34u   // frame jitter margin: two 60 Hz frames
typedef struct {
	int phase;                  // 0 idle, 1 OFF1, 2 ON1, 3 OFF2
	int short_path;             // current sequence ends after OFF1
	int hold_off1;              // full path: hold OFF1 until the push leaves the window
	int up_prev;                // Up held on the previous poll
	int up_on_valid;            // up_on_ms is the engine's last recorded tap
	unsigned int phase_start_ms;
	unsigned int up_on_ms;      // when Up last went on AND was emitted (a real tap)
} LItPadDashState;
int LIrPadLogic_DashPoll(LItPadDashState *s, int dash_went_down,
	int up_held, unsigned int now_ms, unsigned int phase_ms);

#endif
