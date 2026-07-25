// ======================================================================
// test_sweep_console_tap.c
//
// Proves the sweep console tap sees what it has to see, and changes nothing
// about what the console itself does. Links the real BFW_Console.c with
// generated stubs for its engine dependencies, so these assertions are about
// the shipped code path rather than a transcription of it.
//
// The two claims that matter:
//
//   * tapping COrConsole_Print catches BOTH printf variants. Tapping
//     COrConsole_Printf alone would miss COrConsole_Printf_Color, which is the
//     path AI errors take (Oni_AI2_Error.c:481) — those findings would just be
//     absent from the report, with nothing to indicate they were lost.
//   * the tap observes, it does not consume. The message still reaches the
//     console's own sinks afterwards, so consoleLog.txt stays an independent
//     second record of the run.
//
// Run with tests/test_sweep_console_tap.sh, which builds it twice: once with
// the sweep binary's own defines, and once with SHIPPING_VERSION=0 so that
// SUPPORT_DEBUG_FILES is on and the downstream sink is observable.
// ======================================================================

#include "BFW.h"
#include "BFW_Console.h"
#include "BFW_FileManager.h"

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

// --- the tap spy ------------------------------------------------------

static int	g_tap_calls = 0;
static char	g_tap_text[4096];

static void spy_tap(const char *inString)
{
	g_tap_calls++;
	snprintf(g_tap_text, sizeof(g_tap_text), "%s", inString);
}

// --- downstream spy ---------------------------------------------------
//
// With SUPPORT_DEBUG_FILES on, COrConsole_Print forwards to the debug file
// after the tap runs. Counting those calls is how "observe, don't consume"
// stops being an assertion about the source and becomes a measurement.

#if SUPPORT_DEBUG_FILES

static int	g_downstream_calls = 0;
static char	g_downstream_text[4096];
static int	g_fake_file = 0;

BFtDebugFile *BFrDebugFile_Open(const char *inName)
{
	(void) inName;
	return (BFtDebugFile *) &g_fake_file;
}

void UUcArglist_Call BFrDebugFile_Printf(BFtDebugFile *inFile, const char *format, ...)
{
	va_list arglist;

	(void) inFile;
	g_downstream_calls++;

	va_start(arglist, format);
	vsnprintf(g_downstream_text, sizeof(g_downstream_text), format, arglist);
	va_end(arglist);
}

#endif

// --- tests ------------------------------------------------------------

static void reset(void)
{
	g_tap_calls = 0;
	g_tap_text[0] = '\0';
#if SUPPORT_DEBUG_FILES
	g_downstream_calls = 0;
	g_downstream_text[0] = '\0';
#endif
}

int main(void)
{
#if SUPPORT_DEBUG_FILES
	printf("(SUPPORT_DEBUG_FILES on - downstream continuation is checked)\n");
#else
	printf("(SUPPORT_DEBUG_FILES off - sweep build config)\n");
#endif

	/* 1. No tap registered: the console behaves as it always did. */
	reset();
	COrConsole_Print(COcPriority_Console, IMcShade_White, IMcShade_Black, "untapped line");
	check_true("no tap: tap not called", g_tap_calls == 0);
#if SUPPORT_DEBUG_FILES
	check_true("no tap: message still reached downstream", g_downstream_calls == 1);
#endif

	/* 2. Registered tap sees COrConsole_Print verbatim. */
	reset();
	COrConsole_SetTap(spy_tap);
	COrConsole_Print(COcPriority_Console, IMcShade_White, IMcShade_Black, "a printed line");
	check_true("Print: tap called once", g_tap_calls == 1);
	check_true("Print: tap got the string", strcmp(g_tap_text, "a printed line") == 0);
#if SUPPORT_DEBUG_FILES
	check_true("Print: observed, not consumed", g_downstream_calls == 1);
	/* The console appends UUmNL on its way to the debug file, so compare the
	   prefix rather than the whole line. */
	check_true("Print: downstream got the same string",
		strncmp(g_downstream_text, "a printed line", 14) == 0);
#endif

	/* 3. COrConsole_Printf. Its very existence is the point of
	   ONI_SWEEP_CONSOLE — under the shipping defines it is an inline no-op and
	   this call would reach nothing at all. */
	reset();
	COrConsole_Printf("script error on line %d", 42);
	check_true("Printf: reached the tap", g_tap_calls == 1);
	check_true("Printf: formatted text", strcmp(g_tap_text, "script error on line 42") == 0);
#if SUPPORT_DEBUG_FILES
	check_true("Printf: observed, not consumed", g_downstream_calls == 1);
#endif

	/* 4. COrConsole_Printf_Color — the AI error path. This is the case that
	   justifies tapping the sink instead of the printf functions. */
	reset();
	COrConsole_Printf_Color(COcPriority_Console, IMcShade_Red, IMcShade_Black,
		"AI error: %s", "no path to target");
	check_true("Printf_Color: reached the tap", g_tap_calls == 1);
	check_true("Printf_Color: formatted text",
		strcmp(g_tap_text, "AI error: no path to target") == 0);
#if SUPPORT_DEBUG_FILES
	check_true("Printf_Color: observed, not consumed", g_downstream_calls == 1);
#endif

	/* 5. Volume. The console carries far more traffic than UUrPrintWarning, so
	   the tap has to survive being called in bulk. */
	reset();
	{
		int itr;
		for (itr = 0; itr < 5000; itr++) {
			COrConsole_Printf("line %d", itr);
		}
	}
	check_true("volume: every line tapped", g_tap_calls == 5000);
	check_true("volume: last line intact", strcmp(g_tap_text, "line 4999") == 0);
#if SUPPORT_DEBUG_FILES
	check_true("volume: every line still went downstream", g_downstream_calls == 5000);
#endif

	/* 6. Unregistering restores stock behaviour. */
	reset();
	COrConsole_SetTap(NULL);
	COrConsole_Print(COcPriority_Console, IMcShade_White, IMcShade_Black, "after unregister");
	check_true("unregistered: tap not called", g_tap_calls == 0);
#if SUPPORT_DEBUG_FILES
	check_true("unregistered: message still reached downstream", g_downstream_calls == 1);
#endif

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return (g_fail == 0) ? 0 : 1;
}
