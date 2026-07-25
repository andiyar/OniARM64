// ======================================================================
// test_sweep_warning_tap.c
//
// Proves the one thing that decides whether an unattended sweep run finishes
// or hangs forever: UUrPrintWarning must not reach AUrMessageBox while the
// sweep's tap is registered, and must reach it exactly as before when no tap
// is registered.
//
// This links the REAL BungieFrameWork/BFW_Source/BFW_Utility/BFW_Error.c, not
// a copy of its control flow — that whole translation unit has exactly one
// non-libc external, AUrMessageBox, which this file stubs as a spy. Testing a
// transcribed copy would prove nothing about the shipped path.
//
// Build and run with tests/test_sweep_warning_tap.sh, from the repo root. It
// needs no configured CMake build — just the headers BFW_Error.c reaches, plus
// SDL2's include dir for BFW.h's SDL assert include.
//
// UUrPrintWarning also writes every warning to stderr; the runner discards it.
// ======================================================================

#include "BFW.h"

#include <stdio.h>
#include <string.h>

static int	g_pass = 0;
static int	g_fail = 0;

static void check_true(const char *label, int cond)
{
	if (cond) {
		g_pass++;
	} else {
		g_fail++;
		printf("FAIL %s\n", label);
	}
}

// --- the spy ----------------------------------------------------------
//
// Stands in for the blocking modal. Counting calls is the whole test: a
// non-zero count under an active tap means a real run would have stopped dead
// waiting for a click that nobody is there to give.

static int	g_modal_calls = 0;
static char	g_modal_text[4096];

AUtMB_ButtonChoice UUcArglist_Call AUrMessageBox(AUtMB_ButtonType inButtonType, const char *format, ...)
{
	va_list arglist;

	(void) inButtonType;

	g_modal_calls++;

	va_start(arglist, format);
	vsnprintf(g_modal_text, sizeof(g_modal_text), format, arglist);
	va_end(arglist);

	return AUcMBChoice_OK;
}

// --- taps -------------------------------------------------------------

static int	g_tap_calls = 0;
static char	g_tap_text[4096];

/* What Oni_Sweep.c's tap does: swallow the warning, report handled. */
static UUtBool consuming_tap(const char *inMessage)
{
	g_tap_calls++;
	snprintf(g_tap_text, sizeof(g_tap_text), "%s", inMessage);
	return UUcTrue;
}

/* Declines the warning, so the stock modal must still run. */
static UUtBool declining_tap(const char *inMessage)
{
	g_tap_calls++;
	snprintf(g_tap_text, sizeof(g_tap_text), "%s", inMessage);
	return UUcFalse;
}

static void reset(void)
{
	g_modal_calls = 0;
	g_tap_calls = 0;
	g_modal_text[0] = '\0';
	g_tap_text[0] = '\0';
}

// --- tests ------------------------------------------------------------

int main(void)
{
	char	longArg[4000];

	/* 1. No tap registered — stock behaviour, modal raised. */
	reset();
	UUrPrintWarning("plain warning %d", 7);
	check_true("no tap: modal raised once", g_modal_calls == 1);
	check_true("no tap: modal got formatted text", strcmp(g_modal_text, "plain warning 7") == 0);
	check_true("no tap: tap not called", g_tap_calls == 0);

	/* 2. Consuming tap — the modal must not run. This is the sweep case. */
	reset();
	UUrError_SetWarningTap(consuming_tap);
	UUrPrintWarning("can't find weapon class '%s'", "w10_sni_p01");
	check_true("consuming tap: modal suppressed", g_modal_calls == 0);
	check_true("consuming tap: tap called once", g_tap_calls == 1);
	check_true("consuming tap: tap got formatted text",
		strcmp(g_tap_text, "can't find weapon class 'w10_sni_p01'") == 0);

	/* 3. Many warnings in a row — every one suppressed, none leaks a modal. */
	reset();
	{
		int itr;
		for (itr = 0; itr < 1000; itr++) {
			UUrPrintWarning("warning number %d", itr);
		}
	}
	check_true("consuming tap: 1000 warnings, 1000 taps", g_tap_calls == 1000);
	check_true("consuming tap: 1000 warnings, 0 modals", g_modal_calls == 0);

	/* 4. Declining tap — fallthrough to the modal is preserved. */
	reset();
	UUrError_SetWarningTap(declining_tap);
	UUrPrintWarning("declined warning");
	check_true("declining tap: tap called", g_tap_calls == 1);
	check_true("declining tap: modal still raised", g_modal_calls == 1);

	/* 5. Unregistering restores stock behaviour exactly. */
	reset();
	UUrError_SetWarningTap(NULL);
	UUrPrintWarning("after unregister");
	check_true("unregistered: modal raised", g_modal_calls == 1);
	check_true("unregistered: tap not called", g_tap_calls == 0);
	check_true("unregistered: modal text intact", strcmp(g_modal_text, "after unregister") == 0);

	/* 6. Long argument. UUrPrintWarning formats into char[MAX_STRING_LENGTH]
	   (256), so an unbounded vsprintf would smash the stack here. Bounded
	   output means the tap sees at most 255 characters and returns normally. */
	reset();
	memset(longArg, 'x', sizeof(longArg) - 1);
	longArg[sizeof(longArg) - 1] = '\0';
	UUrError_SetWarningTap(consuming_tap);
	UUrPrintWarning("overlong: %s", longArg);
	check_true("long arg: survived and tapped", g_tap_calls == 1);
	check_true("long arg: truncated to buffer", strlen(g_tap_text) == 255);
	UUrError_SetWarningTap(NULL);

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return (g_fail == 0) ? 0 : 1;
}
