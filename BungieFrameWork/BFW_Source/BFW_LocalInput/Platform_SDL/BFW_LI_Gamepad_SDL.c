// BFW_LI_Gamepad_SDL.c — SDL_GameController device layer (#73).
// Feeds the standard LItDeviceInput -> LIrActionBuffer_Add pipeline so pad
// inputs are rebindable exactly like keyboard/mouse (bind pad_a to jump).
#include "BFW_LI_Gamepad_SDL.h"
#include "BFW_LI_GamepadLogic.h"
#include "BFW_LI_Private.h"          // LIgMouse_Invert
#include <stdlib.h>
#include <string.h>

static SDL_GameController *LIgPad = NULL;
static UUtBool LIgPad_Disabled = UUcFalse;
static UUtBool LIgPad_HasRumble = UUcFalse;
static unsigned int LIgPad_StickBits = 0;     // left-stick hysteresis state
static int LIgPad_PrevR3 = 0;                 // R3 edge detect
static LItPadDashState LIgPad_Dash = { 0 };   // dash-gap state

// tunables — maintainer checkpoint adjusts these (#73)
#define LIcPadTriggerThreshold   8000     // of 32767, digitalize ZL/ZR
#define LIcPadStickOnFrac        0.35f
#define LIcPadStickOffFrac       0.30f
#define LIcPadAimDeadFrac        0.15f
#define LIcPadAimScale           8.0f     // mouse-equivalent units per poll (frame) at full deflection
#define LIcGamepadDashGapFrames  2        // polls (frames) of direction suppression for dash synth;
                                          // polls, not ticks: a 0-tick frame discards its poll (#49)

static void LIiPad_Open(int inDeviceIndex)
{
	if (LIgPad != NULL || LIgPad_Disabled) { return; }
	if (!SDL_IsGameController(inDeviceIndex)) { return; }
	LIgPad = SDL_GameControllerOpen(inDeviceIndex);
	if (LIgPad == NULL) {
		UUrStartupMessage("[pad] open failed for device %d: %s", inDeviceIndex, SDL_GetError());
		return;
	}
	LIgPad_HasRumble = SDL_GameControllerHasRumble(LIgPad);
	UUrStartupMessage("[pad] connected: %s (rumble=%s)",
		SDL_GameControllerName(LIgPad), LIgPad_HasRumble ? "yes" : "no");
}

static void LIiPad_Close(void)
{
	if (LIgPad != NULL) {
		UUrStartupMessage("[pad] disconnected: %s", SDL_GameControllerName(LIgPad));
		SDL_GameControllerClose(LIgPad);
		LIgPad = NULL;
		LIgPad_HasRumble = UUcFalse;
		LIgPad_StickBits = 0;
		LIgPad_PrevR3 = 0;
		LIgPad_Dash.gap_remaining = 0;
	}
}

UUtError LIrGamepad_Initialize(void)
{
	const char *env = getenv("ONI_GAMEPAD");
	int i, n;
	LIgPad_Disabled = UUcFalse;
	if (env != NULL && strcmp(env, "0") == 0) {
		LIgPad_Disabled = UUcTrue;
		UUrStartupMessage("[pad] disabled via ONI_GAMEPAD=0");
		return UUcError_None;
	}
	n = SDL_NumJoysticks();
	for (i = 0; i < n; i++) { LIiPad_Open(i); }
	return UUcError_None;
}

void LIrGamepad_Terminate(void) { LIiPad_Close(); }
UUtBool LIrGamepad_Present(void) { return (UUtBool)(LIgPad != NULL); }

void LIrGamepad_HandleSDLEvent(const SDL_Event *inEvent)
{
	switch (inEvent->type) {
		case SDL_CONTROLLERDEVICEADDED:
			LIiPad_Open(inEvent->cdevice.which);
		break;
		case SDL_CONTROLLERDEVICEREMOVED:
			if (LIgPad != NULL && inEvent->cdevice.which ==
				SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(LIgPad))) {
				LIiPad_Close();
			}
		break;
	}
}

void LIrGamepad_Rumble(float inStrength01, UUtUns32 inDurationMS)
{
	Uint16 s;
	if (LIgPad == NULL || !LIgPad_HasRumble) { return; }
	if (inStrength01 < 0.0f) { inStrength01 = 0.0f; }
	if (inStrength01 > 1.0f) { inStrength01 = 1.0f; }
	s = (Uint16)(inStrength01 * 65535.0f);
	SDL_GameControllerRumble(LIgPad, s, s, inDurationMS);
}

static void LIiPad_EmitButton(LItAction *outAction, UUtUns32 inCode)
{
	LItDeviceInput deviceInput;
	deviceInput.input = inCode;
	deviceInput.analogValue = 1.0f;
	LIrActionBuffer_Add(outAction, &deviceInput);
}

void LIrGamepad_GetData(LItAction *outAction)
{
	static const struct { SDL_GameControllerButton sdl; UUtUns32 code; } buttons[] = {
		{ SDL_CONTROLLER_BUTTON_A,             LIcGamepadCode_South     },
		{ SDL_CONTROLLER_BUTTON_B,             LIcGamepadCode_East      },
		{ SDL_CONTROLLER_BUTTON_X,             LIcGamepadCode_West      },
		{ SDL_CONTROLLER_BUTTON_Y,             LIcGamepadCode_North     },
		{ SDL_CONTROLLER_BUTTON_BACK,          LIcGamepadCode_Back      },
		{ SDL_CONTROLLER_BUTTON_START,         LIcGamepadCode_Start     },
		{ SDL_CONTROLLER_BUTTON_LEFTSTICK,     LIcGamepadCode_L3        },
		{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  LIcGamepadCode_LB        },
		{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, LIcGamepadCode_RB        },
		{ SDL_CONTROLLER_BUTTON_DPAD_UP,       LIcGamepadCode_DPadUp    },
		{ SDL_CONTROLLER_BUTTON_DPAD_DOWN,     LIcGamepadCode_DPadDown  },
		{ SDL_CONTROLLER_BUTTON_DPAD_LEFT,     LIcGamepadCode_DPadLeft  },
		{ SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    LIcGamepadCode_DPadRight },
	};
	UUtUns32 i;

	if (LIgPad == NULL) { return; }

	for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (SDL_GameControllerGetButton(LIgPad, buttons[i].sdl)) {
			LIiPad_EmitButton(outAction, buttons[i].code);
		}
	}
	// triggers, digitalized
	if (SDL_GameControllerGetAxis(LIgPad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > LIcPadTriggerThreshold) {
		LIiPad_EmitButton(outAction, LIcGamepadCode_ZL);
	}
	if (SDL_GameControllerGetAxis(LIgPad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > LIcPadTriggerThreshold) {
		LIiPad_EmitButton(outAction, LIcGamepadCode_ZR);
	}
	// Left stick: quantise with hysteresis into pad_ls_* held inputs.
	// R3 (dash) is NOT emitted as a binding input: its press edge opens a
	// short gap in which the held directions are withheld, so the engine sees
	// release + re-press of the direction, i.e. its own double-tap dash.
	{
		static const struct { unsigned int bit; UUtUns32 code; } dirs[] = {
			{ LIcPadDir_Up,    LIcGamepadCode_LSUp    },
			{ LIcPadDir_Down,  LIcGamepadCode_LSDown  },
			{ LIcPadDir_Left,  LIcGamepadCode_LSLeft  },
			{ LIcPadDir_Right, LIcGamepadCode_LSRight },
		};
		const int lx = SDL_GameControllerGetAxis(LIgPad, SDL_CONTROLLER_AXIS_LEFTX);
		const int ly = SDL_GameControllerGetAxis(LIgPad, SDL_CONTROLLER_AXIS_LEFTY);
		const int r3 = SDL_GameControllerGetButton(LIgPad, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
		const int r3_went_down = r3 && !LIgPad_PrevR3;
		LIgPad_PrevR3 = r3;
		// +y is SDL down; the quantiser maps -y to Up (forward)
		LIgPad_StickBits = LIrPadLogic_QuantizeStick(lx, ly,
			LIcPadStickOnFrac, LIcPadStickOffFrac, LIgPad_StickBits);
		if (!LIrPadLogic_DashPoll(&LIgPad_Dash, r3_went_down,
			LIgPad_StickBits != 0, LIcGamepadDashGapFrames)) {
			for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
				if (LIgPad_StickBits & dirs[i].bit) {
					LIiPad_EmitButton(outAction, dirs[i].code);
				}
			}
		}
	}
	// Right stick: dead zone + curve, added as axis deltas on the same path
	// and sign convention as the mouse (+x right, +y up, then invert flip).
	{
		const int rx = SDL_GameControllerGetAxis(LIgPad, SDL_CONTROLLER_AXIS_RIGHTX);
		const int ry = SDL_GameControllerGetAxis(LIgPad, SDL_CONTROLLER_AXIS_RIGHTY);
		LItDeviceInput deviceInput;
		deviceInput.input = LIcGamepadCode_RSX;
		deviceInput.analogValue = LIrPadLogic_AimDelta(rx, ry, LIcPadAimDeadFrac, LIcPadAimScale);
		LIrActionBuffer_Add(outAction, &deviceInput);
		deviceInput.input = LIcGamepadCode_RSY;
		deviceInput.analogValue = -LIrPadLogic_AimDelta(ry, rx, LIcPadAimDeadFrac, LIcPadAimScale);
		if (LIgMouse_Invert) {
			deviceInput.analogValue = -deviceInput.analogValue;
		}
		LIrActionBuffer_Add(outAction, &deviceInput);
	}
}

void LIrGamepad_PumpMenuEvents(void)
{
	// Task 5.
}

void LIrGamepad_BindDefaults(void)
{
	// One layout, positional (SDL Xbox-style names). All rebindable.
	static const struct { UUtUns32 code; const char *action; } defaults[] = {
		{ LIcGamepadCode_LSUp,      "forward"     },
		{ LIcGamepadCode_LSDown,    "backward"    },
		{ LIcGamepadCode_LSLeft,    "stepleft"    },
		{ LIcGamepadCode_LSRight,   "stepright"   },
		{ LIcGamepadCode_RSX,       "aim_LR"      },
		{ LIcGamepadCode_RSY,       "aim_UD"      },
		{ LIcGamepadCode_South,     "jump"        },
		{ LIcGamepadCode_East,      "action"      },
		{ LIcGamepadCode_West,      "punch"       },
		{ LIcGamepadCode_North,     "kick"        },
		{ LIcGamepadCode_ZR,        "fire1"       },
		{ LIcGamepadCode_ZL,        "fire2"       },
		{ LIcGamepadCode_RB,        "reload"      },
		{ LIcGamepadCode_LB,        "swap"        },
		{ LIcGamepadCode_DPadUp,    "hypo"        },
		{ LIcGamepadCode_DPadDown,  "drop"        },
		{ LIcGamepadCode_L3,        "crouch"      },
		{ LIcGamepadCode_Start,     "escape"      },
		{ LIcGamepadCode_Back,      "pausescreen" },
	};
	UUtUns32 i;
	for (i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
		if (LIrBinding_Add(defaults[i].code, defaults[i].action) != UUcError_None) {
			UUrStartupMessage("[pad] WARNING: binding add failed for '%s' (table full?)", defaults[i].action);
		}
	}
	UUrStartupMessage("[pad] default bindings applied (%u)", (unsigned)(sizeof(defaults) / sizeof(defaults[0])));
}
