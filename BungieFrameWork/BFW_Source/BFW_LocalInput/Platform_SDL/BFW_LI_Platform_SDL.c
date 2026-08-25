// ======================================================================
// BFW_LI_Platform_SDL.c
// ======================================================================

// ======================================================================
// includes
// ======================================================================
#include "BFW.h"
#include "BFW_LocalInput.h"
#include "BFW_LI_Platform.h"
#include "BFW_LI_Private.h"
#include "BFW_Console.h"
#include "BFW_WindowManager.h"
#include "BFW_LI_Platform_SDL.h"
#include "BFW_Timer.h"
#include "BFW_ScriptLang.h"

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>

#include <stdlib.h>
#include <string.h>

// ======================================================================
// globals
// ======================================================================
static UUtWindow			LIgWindow;

// Issue #78 — ONI_INPUT_TRACE=1 traces raw SDL key events and per-poll
// oni-key edges, to discriminate a lost keyUp (OS/SDL level: the raw UP
// event never arrives, polled state stays down) from an engine-side
// movement latch (input goes clean, character keeps moving) when a
// strafe sticks during creep. Default off; capture-run diagnostic.
static UUtBool LIiInputTraceEnabled(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *v = getenv("ONI_INPUT_TRACE");
		cached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
	}
	return (UUtBool)cached;
}

// #83 — focus-loss auto-pause; ONI_AUTOPAUSE=0 restores the old
// keep-simulating-in-background behaviour.
static UUtBool LIiAutoPauseEnabled(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *v = getenv("ONI_AUTOPAUSE");
		cached = (v != NULL && v[0] == '0') ? 0 : 1;
	}
	return (UUtBool)cached;
}

// the other platforms specify a factor to multiply the relative mouse movement value by
// probably to scale movement to a common range
//TODO: check what factor we want here
#define SDL_MOUSEMOVE_FACTOR 0.5f
#define SDL_MOUSEWHEEL_FACTOR 1.0f
// SDL doesn't report wheel state with SDL_GetRelativeMouseState()
// so this is filled in via SDL_MouseWheelEvents
static float vertical_wheel_scroll = 0;

struct sdl_oni_key {
	UUtUns8 oni_key;
	Uint32 sdl_key;
};

// for non-ascii keys
static const struct sdl_oni_key sdl_keyboard_translation_table[] =
{
	{LIcKeyCode_F1, SDLK_F1},
	{LIcKeyCode_F2, SDLK_F2},
	{LIcKeyCode_F3, SDLK_F3},
	{LIcKeyCode_F4, SDLK_F4},
	{LIcKeyCode_F5, SDLK_F5},
	{LIcKeyCode_F6, SDLK_F6},
	{LIcKeyCode_F7, SDLK_F7},
	{LIcKeyCode_F8, SDLK_F8},
	{LIcKeyCode_F9, SDLK_F9},
	{LIcKeyCode_F10, SDLK_F10},
	{LIcKeyCode_F11, SDLK_F11},
	{LIcKeyCode_F12, SDLK_F12},
	{LIcKeyCode_F13, SDLK_F13},
	{LIcKeyCode_F14, SDLK_F14},
	{LIcKeyCode_F15, SDLK_F15},
	{LIcKeyCode_CapsLock, SDLK_CAPSLOCK},
	{LIcKeyCode_LeftShift, SDLK_LSHIFT},
	{LIcKeyCode_RightShift, SDLK_RSHIFT},
	{LIcKeyCode_LeftControl, SDLK_LCTRL},
	//{LIcKeyCode_LeftOption, },
	{LIcKeyCode_LeftAlt, SDLK_LALT},
	{LIcKeyCode_RightAlt, SDLK_RALT},
	//{LIcKeyCode_RightOption, },
	{LIcKeyCode_AppMenuKey, SDLK_MENU},
	{LIcKeyCode_RightControl, SDLK_RCTRL},
	{LIcKeyCode_PrintScreen, SDLK_PRINTSCREEN},
	{LIcKeyCode_ScrollLock, SDLK_SCROLLLOCK},
	{LIcKeyCode_Pause, SDLK_PAUSE},
	{LIcKeyCode_Insert, SDLK_INSERT},
	{LIcKeyCode_Home, SDLK_HOME},
	{LIcKeyCode_PageUp, SDLK_PAGEUP},
	{LIcKeyCode_Delete, SDLK_DELETE},
	{LIcKeyCode_End, SDLK_END},
	{LIcKeyCode_PageDown, SDLK_PAGEDOWN},

	{LIcKeyCode_UpArrow, SDLK_UP},
	{LIcKeyCode_LeftArrow, SDLK_LEFT},
	{LIcKeyCode_DownArrow, SDLK_DOWN},
	{LIcKeyCode_RightArrow, SDLK_RIGHT},

	{LIcKeyCode_NumLock, SDLK_NUMLOCKCLEAR},
	{LIcKeyCode_Divide, SDLK_KP_DIVIDE},
	{LIcKeyCode_Multiply, SDLK_KP_MULTIPLY},
	{LIcKeyCode_Subtract, SDLK_KP_MINUS},
	{LIcKeyCode_Add, SDLK_KP_PLUS},
	{LIcKeyCode_Decimal, SDLK_KP_DECIMAL},
	{LIcKeyCode_NumPadEnter, SDLK_KP_ENTER},
	{LIcKeyCode_NumPadComma, SDLK_KP_COMMA},
	{LIcKeyCode_NumPadEquals, SDLK_KP_EQUALS},

	{LIcKeyCode_NumPad0, SDLK_KP_0},
	{LIcKeyCode_NumPad1, SDLK_KP_1},
	{LIcKeyCode_NumPad2, SDLK_KP_2},
	{LIcKeyCode_NumPad3, SDLK_KP_3},
	{LIcKeyCode_NumPad4, SDLK_KP_4},
	{LIcKeyCode_NumPad5, SDLK_KP_5},
	{LIcKeyCode_NumPad6, SDLK_KP_6},
	{LIcKeyCode_NumPad7, SDLK_KP_7},
	{LIcKeyCode_NumPad8, SDLK_KP_8},
	{LIcKeyCode_NumPad9, SDLK_KP_9},

	{LIcKeyCode_LeftWindowsKey, SDLK_LGUI},
	{LIcKeyCode_RightWindowsKey, SDLK_RGUI},
};

// assert that ASCII special characters match
#if __STDC_VERSION__ >= 201112L
_Static_assert(
	SDLK_AMPERSAND    == LIcKeyCode_Ampersand    &&
	SDLK_ASTERISK     == LIcKeyCode_Star         &&
	SDLK_AT           == LIcKeyCode_At           &&
	SDLK_BACKQUOTE    == LIcKeyCode_Grave        &&
	SDLK_BACKSLASH    == LIcKeyCode_BackSlash    &&
	SDLK_BACKSPACE    == LIcKeyCode_BackSpace    &&
	SDLK_CARET        == LIcKeyCode_Hat          &&
	SDLK_COLON        == LIcKeyCode_Colon        &&
	SDLK_COMMA        == LIcKeyCode_Comma        &&
	SDLK_DOLLAR       == LIcKeyCode_Dollar       &&
	SDLK_EQUALS       == LIcKeyCode_Equals       &&
	SDLK_ESCAPE       == LIcKeyCode_Escape       &&
	SDLK_EXCLAIM      == LIcKeyCode_Exclamation  &&
	SDLK_GREATER      == LIcKeyCode_GreaterThan  &&
	SDLK_HASH         == LIcKeyCode_Pound        &&
	SDLK_LEFTBRACKET  == LIcKeyCode_LeftBracket  &&
	SDLK_LEFTPAREN    == LIcKeyCode_LeftParen    &&
	SDLK_LESS         == LIcKeyCode_LessThan     &&
	SDLK_MINUS        == LIcKeyCode_Minus        &&
	SDLK_PERCENT      == LIcKeyCode_Percent      &&
	SDLK_PERIOD       == LIcKeyCode_Period       &&
	SDLK_PLUS         == LIcKeyCode_Plus         &&
	SDLK_QUESTION     == LIcKeyCode_Question     &&
	SDLK_QUOTE        == LIcKeyCode_Apostrophe   &&
	SDLK_QUOTEDBL     == LIcKeyCode_Quote        &&
	SDLK_RETURN       == LIcKeyCode_Return       &&
	SDLK_RIGHTBRACKET == LIcKeyCode_RightBracket &&
	SDLK_RIGHTPAREN   == LIcKeyCode_RightParen   &&
	SDLK_SEMICOLON    == LIcKeyCode_Semicolon    &&
	SDLK_SLASH        == LIcKeyCode_Slash        &&
	SDLK_SPACE        == LIcKeyCode_Space        &&
	SDLK_TAB          == LIcKeyCode_Tab          &&
	SDLK_UNDERSCORE   == LIcKeyCode_Underscore   ,
	"Keys do not match"
);
#endif

static const unsigned num_sdl_keys = sizeof(sdl_keyboard_translation_table)/sizeof(struct sdl_oni_key);

static SDL_Keycode oni_to_sdl_keycode(LItKeyCode key)
{
	int i;

	if (key < 0x80)
	{
		// ascii range maps directly
		// except...
		switch (key)
		{
			case LIcKeyCode_Tilde:
			case LIcKeyCode_LeftCurlyBracket:
			case LIcKeyCode_RightCurlyBracket:
			case LIcKeyCode_VerticalLine:
				// not available as key codes in SDL
				return SDLK_UNKNOWN;
		}

		return key;
	}

	for (i = 0; i<num_sdl_keys; ++i)
	{
		const struct sdl_oni_key *map = &sdl_keyboard_translation_table[i];
		if (map->oni_key == key)
		{
			return map->sdl_key;
		}
	}

	return SDLK_UNKNOWN;
}

static LItKeyCode sdl_to_oni_keycode(SDL_Keycode key)
{
	int i;

	if (key < 0x80)
	{
		// ascii range maps directly
		return key;
	}

	for (i = 0; i<num_sdl_keys; ++i)
	{
		const struct sdl_oni_key *map = &sdl_keyboard_translation_table[i];
		if (map->sdl_key == key)
		{
			return map->oni_key;
		}
	}

	return LIcKeyCode_None;
}

// #93 — translate a key by where it sits, not by what the layout calls it.
//
// SDL gives every key two identities: keysym.sym is the character the active
// layout produces, keysym.scancode is the physical position. Translating by sym
// makes the bindings follow the layout — on AZERTY the W-position key reports
// 'z', so the default WASD movement binds scatter across the board and the game
// is unplayable out of the box (a French player's report on #93). Oni's key
// codes are ASCII and every bind name in key_config.txt is a QWERTY character,
// so the fix is to read the position and translate it against a fixed QWERTY
// reference: "bind w to forward" then means the W-position key on any layout.
//
// Only the printable block moves between layouts. F-keys, arrows, modifiers,
// keypad, space/enter/escape/tab/backspace and the rest sit in the same place
// everywhere, so they delegate to sdl_to_oni_keycode() and the existing
// translation table stays the one place non-ASCII keys are mapped.
//
// Trade-off worth knowing about: the engine consumes no SDL_TEXTINPUT events at
// all (see the #77 comment in Oni_Platform_SDL.c) — every text field, including
// the dev console, reads raw key codes — so typed text goes positional too, and
// someone on AZERTY typing into the console gets QWERTY letters. That surface is
// dev-only, so it stays as is; ONI_KEY_LAYOUT=1 brings the old layout-mapped
// translation back for anyone who wants it.
static LItKeyCode sdl_scancode_to_oni_keycode(SDL_Scancode sc)
{
	switch (sc)
	{
		// letter row block — QWERTY positions
		case SDL_SCANCODE_A: return LIcKeyCode_A;
		case SDL_SCANCODE_B: return LIcKeyCode_B;
		case SDL_SCANCODE_C: return LIcKeyCode_C;
		case SDL_SCANCODE_D: return LIcKeyCode_D;
		case SDL_SCANCODE_E: return LIcKeyCode_E;
		case SDL_SCANCODE_F: return LIcKeyCode_F;
		case SDL_SCANCODE_G: return LIcKeyCode_G;
		case SDL_SCANCODE_H: return LIcKeyCode_H;
		case SDL_SCANCODE_I: return LIcKeyCode_I;
		case SDL_SCANCODE_J: return LIcKeyCode_J;
		case SDL_SCANCODE_K: return LIcKeyCode_K;
		case SDL_SCANCODE_L: return LIcKeyCode_L;
		case SDL_SCANCODE_M: return LIcKeyCode_M;
		case SDL_SCANCODE_N: return LIcKeyCode_N;
		case SDL_SCANCODE_O: return LIcKeyCode_O;
		case SDL_SCANCODE_P: return LIcKeyCode_P;
		case SDL_SCANCODE_Q: return LIcKeyCode_Q;
		case SDL_SCANCODE_R: return LIcKeyCode_R;
		case SDL_SCANCODE_S: return LIcKeyCode_S;
		case SDL_SCANCODE_T: return LIcKeyCode_T;
		case SDL_SCANCODE_U: return LIcKeyCode_U;
		case SDL_SCANCODE_V: return LIcKeyCode_V;
		case SDL_SCANCODE_W: return LIcKeyCode_W;
		case SDL_SCANCODE_X: return LIcKeyCode_X;
		case SDL_SCANCODE_Y: return LIcKeyCode_Y;
		case SDL_SCANCODE_Z: return LIcKeyCode_Z;

		// number row
		case SDL_SCANCODE_1: return LIcKeyCode_1;
		case SDL_SCANCODE_2: return LIcKeyCode_2;
		case SDL_SCANCODE_3: return LIcKeyCode_3;
		case SDL_SCANCODE_4: return LIcKeyCode_4;
		case SDL_SCANCODE_5: return LIcKeyCode_5;
		case SDL_SCANCODE_6: return LIcKeyCode_6;
		case SDL_SCANCODE_7: return LIcKeyCode_7;
		case SDL_SCANCODE_8: return LIcKeyCode_8;
		case SDL_SCANCODE_9: return LIcKeyCode_9;
		case SDL_SCANCODE_0: return LIcKeyCode_0;

		// punctuation that shares the printable block
		case SDL_SCANCODE_MINUS:        return LIcKeyCode_Minus;
		case SDL_SCANCODE_EQUALS:       return LIcKeyCode_Equals;
		case SDL_SCANCODE_LEFTBRACKET:  return LIcKeyCode_LeftBracket;
		case SDL_SCANCODE_RIGHTBRACKET: return LIcKeyCode_RightBracket;
		case SDL_SCANCODE_BACKSLASH:    return LIcKeyCode_BackSlash;
		case SDL_SCANCODE_SEMICOLON:    return LIcKeyCode_Semicolon;
		case SDL_SCANCODE_APOSTROPHE:   return LIcKeyCode_Apostrophe;
		case SDL_SCANCODE_GRAVE:        return LIcKeyCode_Grave;
		case SDL_SCANCODE_COMMA:        return LIcKeyCode_Comma;
		case SDL_SCANCODE_PERIOD:       return LIcKeyCode_Period;
		case SDL_SCANCODE_SLASH:        return LIcKeyCode_Slash;

		default:
		break;
	}

	// everything else keeps its position across layouts
	return sdl_to_oni_keycode(SDL_GetKeyFromScancode(sc));
}

// #93 — the exact inverse of the table above, for the paths that start from a
// bound oni key code and ask "is that key down?" (LIrPlatform_TestKey, which
// drives the Oni_KeyBindings.c dev keys and the unit viewer). Without it those
// would resolve through SDL_GetScancodeFromKey, which is layout-mapped, and the
// dev keys would sit on different physical keys than the gameplay bindings on a
// non-QWERTY layout. Non-printable keys fall back to the layout lookup, which
// gives the same answer either way since they don't move.
static SDL_Scancode oni_to_sdl_scancode(LItKeyCode key)
{
	switch (key)
	{
		// letter row block — QWERTY positions
		case LIcKeyCode_A: return SDL_SCANCODE_A;
		case LIcKeyCode_B: return SDL_SCANCODE_B;
		case LIcKeyCode_C: return SDL_SCANCODE_C;
		case LIcKeyCode_D: return SDL_SCANCODE_D;
		case LIcKeyCode_E: return SDL_SCANCODE_E;
		case LIcKeyCode_F: return SDL_SCANCODE_F;
		case LIcKeyCode_G: return SDL_SCANCODE_G;
		case LIcKeyCode_H: return SDL_SCANCODE_H;
		case LIcKeyCode_I: return SDL_SCANCODE_I;
		case LIcKeyCode_J: return SDL_SCANCODE_J;
		case LIcKeyCode_K: return SDL_SCANCODE_K;
		case LIcKeyCode_L: return SDL_SCANCODE_L;
		case LIcKeyCode_M: return SDL_SCANCODE_M;
		case LIcKeyCode_N: return SDL_SCANCODE_N;
		case LIcKeyCode_O: return SDL_SCANCODE_O;
		case LIcKeyCode_P: return SDL_SCANCODE_P;
		case LIcKeyCode_Q: return SDL_SCANCODE_Q;
		case LIcKeyCode_R: return SDL_SCANCODE_R;
		case LIcKeyCode_S: return SDL_SCANCODE_S;
		case LIcKeyCode_T: return SDL_SCANCODE_T;
		case LIcKeyCode_U: return SDL_SCANCODE_U;
		case LIcKeyCode_V: return SDL_SCANCODE_V;
		case LIcKeyCode_W: return SDL_SCANCODE_W;
		case LIcKeyCode_X: return SDL_SCANCODE_X;
		case LIcKeyCode_Y: return SDL_SCANCODE_Y;
		case LIcKeyCode_Z: return SDL_SCANCODE_Z;

		// number row
		case LIcKeyCode_1: return SDL_SCANCODE_1;
		case LIcKeyCode_2: return SDL_SCANCODE_2;
		case LIcKeyCode_3: return SDL_SCANCODE_3;
		case LIcKeyCode_4: return SDL_SCANCODE_4;
		case LIcKeyCode_5: return SDL_SCANCODE_5;
		case LIcKeyCode_6: return SDL_SCANCODE_6;
		case LIcKeyCode_7: return SDL_SCANCODE_7;
		case LIcKeyCode_8: return SDL_SCANCODE_8;
		case LIcKeyCode_9: return SDL_SCANCODE_9;
		case LIcKeyCode_0: return SDL_SCANCODE_0;

		// punctuation that shares the printable block
		case LIcKeyCode_Minus:        return SDL_SCANCODE_MINUS;
		case LIcKeyCode_Equals:       return SDL_SCANCODE_EQUALS;
		case LIcKeyCode_LeftBracket:  return SDL_SCANCODE_LEFTBRACKET;
		case LIcKeyCode_RightBracket: return SDL_SCANCODE_RIGHTBRACKET;
		case LIcKeyCode_BackSlash:    return SDL_SCANCODE_BACKSLASH;
		case LIcKeyCode_Semicolon:    return SDL_SCANCODE_SEMICOLON;
		case LIcKeyCode_Apostrophe:   return SDL_SCANCODE_APOSTROPHE;
		case LIcKeyCode_Grave:        return SDL_SCANCODE_GRAVE;
		case LIcKeyCode_Comma:        return SDL_SCANCODE_COMMA;
		case LIcKeyCode_Period:       return SDL_SCANCODE_PERIOD;
		case LIcKeyCode_Slash:        return SDL_SCANCODE_SLASH;

		default:
		break;
	}

	return SDL_GetScancodeFromKey(oni_to_sdl_keycode(key));
}

// #93 — ONI_KEY_LAYOUT=1 restores the pre-#93 behaviour: keys translate through
// the layout-mapped symbol rather than their physical position.
static UUtBool LIiKeyLayoutMappingEnabled(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *v = getenv("ONI_KEY_LAYOUT");
		cached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
		if (cached) {
			UUrStartupMessage("[input] ONI_KEY_LAYOUT set — keys mapped by layout symbol, not physical position (pre-#93 behaviour)");
		}
	}
	return (UUtBool)cached;
}

// #93 — the single translation point the event sites share.
static LItKeyCode sdl_keysym_to_oni_keycode(const SDL_Keysym *inKeysym)
{
	if (LIiKeyLayoutMappingEnabled())
	{
		return sdl_to_oni_keycode(inKeysym->sym);
	}

	return sdl_scancode_to_oni_keycode(inKeysym->scancode);
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
static void
LIiPlatform_Mouse_GetData(
	LItAction				*outAction)
{
	LItDeviceInput			deviceInput;

	int x = 0, y = 0, i;
	Uint32 buttonsstate = SDL_GetRelativeMouseState(&x, &y);

	for (i = 0; i < 4; ++i)
	{
		if (buttonsstate & SDL_BUTTON(i + 1))
		{
			deviceInput.input = LIcMouseCode_Button1 + i;
			deviceInput.analogValue = 1.0f;
			LIrActionBuffer_Add(outAction, &deviceInput);
		}
	}

	deviceInput.input = LIcMouseCode_XAxis;
	deviceInput.analogValue = (float)x * SDL_MOUSEMOVE_FACTOR;
	LIrActionBuffer_Add(outAction, &deviceInput);

	deviceInput.input = LIcMouseCode_YAxis;
	deviceInput.analogValue = -(float)y * SDL_MOUSEMOVE_FACTOR;
	if (LIgMouse_Invert)
	{
		deviceInput.analogValue = -deviceInput.analogValue;
	}
	LIrActionBuffer_Add(outAction, &deviceInput);

	deviceInput.input = LIcMouseCode_ZAxis;
	deviceInput.analogValue = vertical_wheel_scroll * SDL_MOUSEWHEEL_FACTOR;
	vertical_wheel_scroll = 0.0f;
	LIrActionBuffer_Add(outAction, &deviceInput);
}

#if UUmCompiler == UUmCompiler_MWerks
#pragma profile reset
#endif

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================

// ----------------------------------------------------------------------
static void
LIiPlatform_Keyboard_GetData(
	LItAction				*outAction)
{
	int i;
	int numkeys = 0;
	UUtBool trace = LIiInputTraceEnabled();
	static UUtUns8 sPrevDown[32];
	UUtUns8 nowDown[32] = {0};

	const Uint8 *keyState = SDL_GetKeyboardState(&numkeys);
	UUtBool byLayout = LIiKeyLayoutMappingEnabled();
	for (i = 0; i < numkeys; ++i)
	{
		if (keyState[i])
		{
			LItDeviceInput			deviceInput;

			// #93 — keyState is indexed by scancode, so the positional path
			// reads it straight; the lever goes back through the layout map.
			deviceInput.input = byLayout
				? sdl_to_oni_keycode(SDL_GetKeyFromScancode(i))
				: sdl_scancode_to_oni_keycode((SDL_Scancode)i);
			deviceInput.analogValue = 1.0f;

			if (deviceInput.input != LIcKeyCode_None)
			{
				if (trace && deviceInput.input < 256) {
					nowDown[deviceInput.input >> 3] |= (UUtUns8)(1u << (deviceInput.input & 7));
				}
				LIrActionBuffer_Add(outAction, &deviceInput);
			}
		}
	}

	// #78 trace: log only edges of the translated key set, so the log stays
	// quiet while keys are stable — a stuck key shows as a missing "-" line.
	if (trace) {
		int k;
		for (k = 0; k < 256; ++k) {
			int was = (sPrevDown[k >> 3] >> (k & 7)) & 1;
			int now = (nowDown[k >> 3] >> (k & 7)) & 1;
			if (was != now) {
				UUrStartupMessage("[input-trace] poll %s oni-key 0x%02x ('%c')",
					now ? "+" : "-", k, (k >= 0x20 && k < 0x7f) ? (char)k : '?');
			}
		}
		memcpy(sPrevDown, nowDown, sizeof(sPrevDown));
	}
}

#if UUmCompiler == UUmCompiler_MWerks
#pragma profile reset
#endif

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
#if UUmCompiler == UUmCompiler_MWerks
#pragma profile off
#endif

static void
LIiPlatform_Devices_GetData(
	LItAction				*outAction)
{
	// get mouse data
	if (LIcMode_Game == LIgMode_Internal)
	{
		LIiPlatform_Mouse_GetData(outAction);
	}

	// get the keyboard data
	LIiPlatform_Keyboard_GetData(outAction);
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
static UUtUns32
sdl_to_oni_mouse_modifiers(SDL_Keymod sdl_modifiers)
{
	UUtUns32 oni_modifiers = 0;
	if (sdl_modifiers & KMOD_LSHIFT)
		oni_modifiers |= LIcMouseState_LShiftDown;
	if (sdl_modifiers & KMOD_RSHIFT)
		oni_modifiers |= LIcMouseState_RShiftDown;
	if (sdl_modifiers & KMOD_LCTRL)
		oni_modifiers |= LIcMouseState_LControlDown;
	if (sdl_modifiers & KMOD_RCTRL)
		oni_modifiers |= LIcMouseState_RControlDown;
	return oni_modifiers;
}

static UUtUns32
sdl_to_oni_mouse_button_modifiers(Uint32 buttonsstate)
{
	UUtUns32 oni_modifiers = 0;
	if (buttonsstate & SDL_BUTTON_LMASK)
		oni_modifiers |= LIcMouseState_LButtonDown;
	if (buttonsstate & SDL_BUTTON_MMASK)
		oni_modifiers |= LIcMouseState_MButtonDown;
	if (buttonsstate & SDL_BUTTON_RMASK)
		oni_modifiers |= LIcMouseState_RButtonDown;
	return oni_modifiers;
}

static UUtUns32
sdl_to_oni_key_modifiers(SDL_Keymod sdl_modifiers)
{
	UUtUns32 oni_modifiers = 0;
	if (sdl_modifiers & KMOD_LSHIFT)
		oni_modifiers |= LIcKeyState_LShiftDown;
	if (sdl_modifiers & KMOD_RSHIFT)
		oni_modifiers |= LIcKeyState_RShiftDown;
	if (sdl_modifiers & KMOD_LCTRL)
		oni_modifiers |= LIcKeyState_LCommandDown;
	if (sdl_modifiers & KMOD_RCTRL)
		oni_modifiers |= LIcKeyState_RCommandDown;
	if (sdl_modifiers & KMOD_LALT)
		oni_modifiers |= LIcKeyState_LAltDown;
	if (sdl_modifiers & KMOD_RALT)
		oni_modifiers |= LIcKeyState_RAltDown;
	return oni_modifiers;
}

static SDL_Keymod
sdl_to_kmod(SDL_Keycode sym)
{
	switch (sym)
	{
		case SDLK_LCTRL:  return KMOD_LCTRL;
		case SDLK_RCTRL:  return KMOD_RCTRL;
		case SDLK_LSHIFT: return KMOD_LSHIFT;
		case SDLK_RSHIFT: return KMOD_RSHIFT;
		case SDLK_LALT:   return KMOD_LALT;
		case SDLK_RALT:   return KMOD_RALT;
		default:          return KMOD_NONE;
	}
}

UUtUns32
LIrPlatform_InputEvent_InterpretModifiers(
	LItInputEventType	inEventType,
	UUtUns32			inModifiers)
{
	(void)inEventType;
	// already translated in LIrPlatform_Update()
	return inModifiers;
}

// ----------------------------------------------------------------------
void
LIrPlatform_InputEvent_GetMouse(
	LItMode				inMode,
	LItInputEvent		*outInputEvent)
{
	// set up the outInputEvent
	outInputEvent->type			= LIcInputEvent_None;
	outInputEvent->where.x		= 0;
	outInputEvent->where.y		= 0;
	outInputEvent->key			= 0;
	outInputEvent->modifiers	= 0;

	if (inMode == LIcMode_Normal)
	{
		int x, y, winW, winH;
		extern int GLgGameWidth, GLgGameHeight;

		Uint32 buttons = SDL_GetMouseState(&x, &y);
		SDL_GetWindowSize(LIgWindow, &winW, &winH);
		if (winW > 0 && winH > 0) {
			x = x * GLgGameWidth / winW;
			y = y * GLgGameHeight / winH;
		}
		outInputEvent->where.x = (UUtInt16)x;
		outInputEvent->where.y = (UUtInt16)y;
		// Mac checks Shift, left and right mouse buttons
		if (LIrPlatform_TestKey(LIcKeyCode_LeftShift, inMode))
			outInputEvent->modifiers |= LIcMouseState_LShiftDown;
		if (buttons & SDL_BUTTON_LMASK)
			outInputEvent->modifiers |= LIcMouseState_LButtonDown;
		if (buttons & SDL_BUTTON_RMASK)
			outInputEvent->modifiers |= LIcMouseState_RButtonDown;
	}
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
void
LIrPlatform_Mode_Set(
	LItMode				inMode)
{
	int x = 0, y = 0;
	if (inMode == LIcMode_Normal)
	{
		SDL_SetWindowGrab(LIgWindow, SDL_FALSE);
		SDL_SetRelativeMouseMode(SDL_FALSE);
	}
	else
	{
		SDL_SetWindowGrab(LIgWindow, SDL_TRUE);
		SDL_SetRelativeMouseMode(SDL_TRUE);
		// reset mouse movement
		SDL_GetRelativeMouseState(&x, &y);
	}
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
UUtError
LIrPlatform_Initialize(
	UUtAppInstance		inInstance,
	UUtWindow			inWindow)
{
	//UUmAssert(inInstance);
	UUmAssert(inWindow);

	// ------------------------------
	// initialize the globals
	// ------------------------------
	LIgWindow					= inWindow;

	// ------------------------------
	// console variables
	// ------------------------------

	return UUcError_None;
}

// ----------------------------------------------------------------------
#if UUmCompiler == UUmCompiler_MWerks
#pragma profile off
#endif

void
LIrPlatform_PollInputForAction(
	LItAction				*outAction)
{
	UUtUns16				i;

	// clear the action
	outAction->buttonBits = 0;
	for (i = 0; i < LIcNumAnalogValues; i++)
		outAction->analogValues[i] = 0.0f;

	// get the action data
	LIiPlatform_Devices_GetData(outAction);
}

#if UUmCompiler == UUmCompiler_MWerks
#pragma profile reset
#endif

// ----------------------------------------------------------------------
void
LIrPlatform_Terminate(
	void)
{
}

// ----------------------------------------------------------------------
UUtBool
LIrPlatform_Update(
	LItMode					inMode)
{
	SDL_Event event;
	LItInputEventType eventType = LIcInputEvent_None;
	IMtPoint2D where = {0, 0};
	static Uint32 current_mouse_buttons = 0;
	static Uint32 current_modifiers = 0;

	//FIXME: Mac and Win32 only poll for a single event
	//       however, input is laggy if that is done.
	while (1)
	{
		if (!SDL_PollEvent(&event)) {
			return UUcFalse;
		}

		switch (event.type)
		{
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				// #78 trace: raw SDL event stream — a stuck key shows here as
				// a DOWN with no matching UP; repeat=1 marks macOS auto-repeats
				// (newly flowing since #77 stopped text-input mode).
				if (LIiInputTraceEnabled()) {
					UUrStartupMessage("[input-trace] event %s sym=0x%x scan=%d repeat=%d mod=0x%x",
						(event.key.state == SDL_PRESSED) ? "DOWN" : "UP",
						(unsigned)event.key.keysym.sym, (int)event.key.keysym.scancode,
						(int)event.key.repeat, (unsigned)event.key.keysym.mod);
				}

				eventType = (event.key.state == SDL_PRESSED) ? LIcInputEvent_KeyDown : LIcInputEvent_KeyUp;

				current_modifiers |= sdl_to_kmod(event.key.keysym.sym);
				if (current_modifiers != event.key.keysym.mod)
				{
					UUmAssert(UUcFalse);
					current_modifiers = event.key.keysym.mod;
				}

				LIrInputEvent_Add(
					eventType,
					NULL,
					sdl_keysym_to_oni_keycode(&event.key.keysym),
					sdl_to_oni_key_modifiers(current_modifiers)
				);
			break;
			case SDL_MOUSEMOTION:
			{
				int winW, winH;
				extern int GLgGameWidth, GLgGameHeight;
				SDL_GetWindowSize(LIgWindow, &winW, &winH);
				where.x = (winW > 0) ? event.motion.x * GLgGameWidth / winW : event.motion.x;
				where.y = (winH > 0) ? event.motion.y * GLgGameHeight / winH : event.motion.y;
			}

				if (current_mouse_buttons != event.motion.state)
				{
					UUmAssert(UUcFalse);
					current_mouse_buttons = event.motion.state;
				}

				// note: this reads the key member of a motion event through the
				// SDL_Event union, so the "key" here was never a real key —
				// left on the same translation path as the key sites (#93).
				LIrInputEvent_Add(
					LIcInputEvent_MouseMove,
					&where,
					sdl_keysym_to_oni_keycode(&event.key.keysym),
					sdl_to_oni_mouse_modifiers(current_modifiers) | sdl_to_oni_mouse_button_modifiers(current_mouse_buttons)
				);
			break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				switch (event.button.button)
				{
					case SDL_BUTTON_LEFT:
						eventType = (event.button.state == SDL_PRESSED) ? LIcInputEvent_LMouseDown : LIcInputEvent_LMouseUp;
					break;
					case SDL_BUTTON_RIGHT:
						eventType = (event.button.state == SDL_PRESSED) ? LIcInputEvent_RMouseDown : LIcInputEvent_RMouseUp;
					break;
					case SDL_BUTTON_MIDDLE:
						eventType = (event.button.state == SDL_PRESSED) ? LIcInputEvent_MMouseDown : LIcInputEvent_MMouseUp;
					break;
					default:
						// extra buttons: skip this event, keep draining the queue
						continue;
				}

				{
					int winW, winH;
					extern int GLgGameWidth, GLgGameHeight;
					SDL_GetWindowSize(LIgWindow, &winW, &winH);
					where.x = (winW > 0) ? event.button.x * GLgGameWidth / winW : event.button.x;
					where.y = (winH > 0) ? event.button.y * GLgGameHeight / winH : event.button.y;
				}

				if (event.button.state == SDL_PRESSED)
				{
					current_mouse_buttons |= sdl_to_oni_mouse_modifiers(SDL_BUTTON(event.button.button));
				}
				else
				{
					current_mouse_buttons &= ~sdl_to_oni_mouse_modifiers(SDL_BUTTON(event.button.button));
				}

				LIrInputEvent_Add(
					eventType,
					&where,
					0,
					sdl_to_oni_mouse_modifiers(current_modifiers) | sdl_to_oni_mouse_button_modifiers(current_mouse_buttons)
				);
			break;
			case SDL_MOUSEWHEEL:
				//TODO: inputEvent
				//TODO: regard event.wheel.direction?
				vertical_wheel_scroll += (float)event.wheel.y;
			break;
			//FIXME: not input - should be Oni_Platform_SDL.c
			case SDL_APP_DIDENTERFOREGROUND:
				LIrGameIsActive(UUcTrue);
			break;
			case SDL_APP_WILLENTERBACKGROUND:
				LIrGameIsActive(UUcFalse);
			break;
			case SDL_QUIT:
				// Dock-icon Quit / app-menu Cmd-Q — same terminate path as the
				// WM Cmd-Q key command, so normal teardown runs (#83)
				WMrMessage_Post(NULL, WMcMessage_Quit, 0, 0);
			break;
			case SDL_WINDOWEVENT:
				switch (event.window.event)
				{
					case SDL_WINDOWEVENT_CLOSE:
						WMrMessage_Post(NULL, WMcMessage_Quit, 0, 0);
					break;
					case SDL_WINDOWEVENT_FOCUS_LOST:
					case SDL_WINDOWEVENT_MINIMIZED:
						if (LIiAutoPauseEnabled()) {
							LIrAutoPause_Request();
						}
					break;
					case SDL_WINDOWEVENT_FOCUS_GAINED:
					case SDL_WINDOWEVENT_RESTORED:
						LIrAutoPause_Cancel();
					break;
				}
			break;
		}
	}

	return UUcTrue;
}

// ----------------------------------------------------------------------
UUtBool
LIrPlatform_TestKey(
	LItKeyCode			inKeyCode,
	LItMode				inMode)
{
	(void)inMode;
	UUtBool keyDown = UUcFalse;

	int numkeys = 0;
	//TODO: capture in LIrPlatform_Update()?
	const Uint8 *keyState = SDL_GetKeyboardState(&numkeys);
	// #93 — same positional scheme as the event path, so a dev key bound to 'w'
	// tests the W-position key rather than whatever types 'w' on this layout.
	SDL_Scancode scancode = LIiKeyLayoutMappingEnabled()
		? SDL_GetScancodeFromKey(oni_to_sdl_keycode(inKeyCode))
		: oni_to_sdl_scancode(inKeyCode);
	if (scancode > 0 && scancode < numkeys) {
		keyDown = keyState[scancode];
	}

	return keyDown;
}
