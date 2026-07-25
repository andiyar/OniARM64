// ======================================================================
// test_sweep_core.c
//
// Lifecycle tests for the sweep core. Links the real Oni_Sweep.c — that
// translation unit has only seven non-libc externals, all stubbed below — so
// these assertions are about the shipped code, not a transcription of it.
//
// The behaviours worth guarding:
//   * ONrSweep_Record is inert before ONrSweep_Begin and after ONrSweep_End.
//     The warning tap can fire during engine init or teardown, and writing to
//     a closed handle there would take the process down while it was busy
//     reporting that something else had gone wrong.
//   * every record reaches disk immediately, so a crash keeps the findings
//     that led up to it.
//   * ONrSweep_SeedRandom touches both RNG streams. UUrRandom_SetSeed covers
//     only one of the two globals; seeding one and forgetting the other is a
//     silent loss of determinism that would surface as flaky findings.
//
// Build and run: tests/test_sweep_core.sh   (from repo root)
// ======================================================================

#include "Oni_Sweep.h"
#include "Oni_GameState.h"
#include "BFW_Console.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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

// --- stubs / spies ----------------------------------------------------

static UUtWarningTap	g_registered_tap = NULL;
static UUtUns32			g_global_seed = 0;
static UUtUns32			g_local_seed = 0;
static int				g_seed_calls = 0;
static int				g_local_seed_calls = 0;
static int				g_update_calls = 0;
static UUtError			g_update_result = UUcError_None;

void UUrError_SetWarningTap(UUtWarningTap inTap)
{
	g_registered_tap = inTap;
}

#ifdef ONI_SWEEP_CONSOLE
static COtConsoleTap	g_registered_console_tap = NULL;
static COtConsoleTap	g_console_tap_saved = NULL;

void COrConsole_SetTap(COtConsoleTap inTap)
{
	g_registered_console_tap = inTap;
}
#endif

void UUrRandom_SetSeed(UUtUns32 seed)
{
	g_global_seed = seed;
	g_seed_calls++;
}

void UUrLocalRandom_SetSeed(UUtUns32 seed)
{
	g_local_seed = seed;
	g_local_seed_calls++;
}

void UUcArglist_Call UUrStartupMessage(const char *format, ...)
{
	(void) format;
}

UUtError ONrGameState_Update(UUtUns16 numActionsInBuffer, LItAction *actionBuffer, UUtUns32 *outTicksUpdated)
{
	(void) numActionsInBuffer;
	(void) actionBuffer;

	g_update_calls++;
	if (outTicksUpdated != NULL) *outTicksUpdated = 1;

	return g_update_result;
}

// --- helpers ----------------------------------------------------------

static char g_report_path[512];

static int count_lines(const char *inPath)
{
	FILE	*f = fopen(inPath, "r");
	int		lines = 0;
	int		c;

	if (f == NULL) return -1;

	while ((c = fgetc(f)) != EOF) {
		if (c == '\n') lines++;
	}
	fclose(f);

	return lines;
}

static int file_contains(const char *inPath, const char *inNeedle)
{
	char	buffer[65536];
	FILE	*f = fopen(inPath, "r");
	size_t	got;

	if (f == NULL) return 0;
	got = fread(buffer, 1, sizeof(buffer) - 1, f);
	fclose(f);
	buffer[got] = '\0';

	return strstr(buffer, inNeedle) != NULL;
}

// --- tests ------------------------------------------------------------

int main(void)
{
	const char	*tmpdir = getenv("TMPDIR");
	UUtError	error;
	int			expect = 0;		/* records written so far */

	snprintf(g_report_path, sizeof(g_report_path), "%ssweep_core_test_%d.ndjson",
		(tmpdir != NULL) ? tmpdir : "/tmp/", (int) getpid());
	remove(g_report_path);

#ifdef ONI_SWEEP_CONSOLE
	printf("(ONI_SWEEP_CONSOLE on - console tap is checked)\n");
#else
	printf("(shipping defines - console tap compiled out)\n");
#endif

	/* 1. Before Begin: inert. Nothing recorded, nothing created, no crash. */
	check_true("pre-begin: not active", ONgSweep_Active == UUcFalse);
	ONrSweep_Record("p", "s", ONcSweepSeverity_Error, "before begin");
	ONrSweep_SetContext("phase", "subject");
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Warn, "still before begin");
	check_true("pre-begin: no report file", count_lines(g_report_path) == -1);

	/* 2. Begin registers the taps and flips the flag. */
	error = ONrSweep_Begin(g_report_path, "metal", 4);
	check_true("begin: succeeded", error == UUcError_None);
	check_true("begin: active", ONgSweep_Active == UUcTrue);
	check_true("begin: warning tap registered", g_registered_tap != NULL);
#ifdef ONI_SWEEP_CONSOLE
	check_true("begin: console tap registered", g_registered_console_tap != NULL);
#endif

	/* 3. Beginning twice must not leak the handle or split the report. */
	check_true("begin twice: rejected",
		ONrSweep_Begin(g_report_path, "metal", 4) != UUcError_None);

	/* 4. Records land, and land immediately — the file is read while the
	   sweep is still open, so an unflushed line would not be there. */
	ONrSweep_Record("spawn", "w10_sni_p01", ONcSweepSeverity_Warn, "can't find weapon class");
	expect++;
	check_true("record: line on disk before End", count_lines(g_report_path) == expect);
	check_true("record: renderer present", file_contains(g_report_path, "\"renderer\":\"metal\""));
	check_true("record: level present", file_contains(g_report_path, "\"level\":4"));
	check_true("record: subject present", file_contains(g_report_path, "\"subject\":\"w10_sni_p01\""));

	/* 5. NULL phase/subject inherit the context. */
	ONrSweep_SetContext("particle", "flash01");
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Leak, "leaked something");
	expect++;
	check_true("context: second line on disk", count_lines(g_report_path) == expect);
	check_true("context: phase inherited", file_contains(g_report_path, "\"phase\":\"particle\""));
	check_true("context: subject inherited", file_contains(g_report_path, "\"subject\":\"flash01\""));

	/* 5b. An over-long phase / subject truncates instead of trapping or
	   overrunning. Subjects come from level content, so nothing bounds them
	   for us. */
	{
		char	huge[4096];

		memset(huge, 'z', sizeof(huge) - 1);
		huge[sizeof(huge) - 1] = '\0';
		ONrSweep_SetContext(huge, huge);
		ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Skipped, "over-long context");
		expect++;
		check_true("long context: line written", count_lines(g_report_path) == expect);
		ONrSweep_SetContext("particle", "flash01");
	}

	/* 6. The warning tap consumes warnings rather than letting them through to
	   the modal, and each one becomes a record. */
	check_true("warning tap: consumes", g_registered_tap("a tapped warning") == UUcTrue);
	expect++;
	check_true("warning tap: recorded", count_lines(g_report_path) == expect);
	check_true("warning tap: text present", file_contains(g_report_path, "a tapped warning"));

#ifdef ONI_SWEEP_CONSOLE
	/* 6b. The console tap records too, and returns nothing — there is no
	   channel through which it could suppress the console. */
	g_registered_console_tap("a tapped console line");
	expect++;
	check_true("console tap: recorded", count_lines(g_report_path) == expect);
	check_true("console tap: text present", file_contains(g_report_path, "a tapped console line"));

	/* 6c. Volume: the console carries far more traffic than warnings do, so the
	   tap has to hold up in bulk and every line has to land. */
	{
		int itr;

		for (itr = 0; itr < 2000; itr++) {
			g_registered_console_tap("bulk console line");
		}
		expect += 2000;
		check_true("console tap: 2000 lines all recorded",
			count_lines(g_report_path) == expect);
	}

	/* Keep a copy: after End the module hands back NULL, but the function is
	   still there and a late console print would still reach it. */
	g_console_tap_saved = g_registered_console_tap;
#endif

	/* 7. Both RNG streams, one call, same fixed seed. */
	g_seed_calls = g_local_seed_calls = 0;
	ONrSweep_SeedRandom();
	check_true("seed: global stream set once", g_seed_calls == 1);
	check_true("seed: local stream set once", g_local_seed_calls == 1);
	check_true("seed: global value", g_global_seed == ONcSweep_RandomSeed);
	check_true("seed: local value", g_local_seed == ONcSweep_RandomSeed);

	/* 8. Tick runs the simulation the requested number of times. */
	g_update_calls = 0;
	g_update_result = UUcError_None;
	ONrSweep_Tick(ONcSweep_SettleCharacter);
	check_true("tick: ran every tick", g_update_calls == ONcSweep_SettleCharacter);
	check_true("tick: nothing recorded on success", count_lines(g_report_path) == expect);

	/* 9. A failing update records once and stops rather than spinning. */
	g_update_calls = 0;
	g_update_result = UUcError_Generic;
	ONrSweep_Tick(100);
	expect++;
	check_true("tick: stopped at first error", g_update_calls == 1);
	check_true("tick: error recorded once", count_lines(g_report_path) == expect);
	g_update_result = UUcError_None;

	/* 10. End unregisters both taps and clears the flag. */
	ONrSweep_End();
	check_true("end: warning tap unregistered", g_registered_tap == NULL);
#ifdef ONI_SWEEP_CONSOLE
	check_true("end: console tap unregistered", g_registered_console_tap == NULL);
#endif
	check_true("end: not active", ONgSweep_Active == UUcFalse);

	/* 11. After End: inert again. No write to a closed handle. */
	ONrSweep_Record("p", "s", ONcSweepSeverity_Abort, "after end");
	ONrSweep_SetContext("late", "late");
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Error, "also after end");
	check_true("post-end: nothing appended", count_lines(g_report_path) == expect);
	check_true("post-end: no late text", !file_contains(g_report_path, "after end"));

#ifdef ONI_SWEEP_CONSOLE
	/* 11b. The tap function itself survives being called after End. BFW holds
	   NULL now, but engine teardown printing through a stale pointer is exactly
	   the kind of thing that takes a process down while it is shutting down. */
	g_console_tap_saved("console line after end");
	check_true("post-end: late console line dropped", count_lines(g_report_path) == expect);
	check_true("post-end: no late console text",
		!file_contains(g_report_path, "console line after end"));
#endif

	/* 12. End is safe to call again. */
	ONrSweep_End();
	check_true("end twice: survived", 1);

	/* 13. A failed Begin leaves the module inert rather than half-open. */
	check_true("begin bad path: rejected",
		ONrSweep_Begin("/nonexistent-dir-for-sweep-test/report.ndjson", "gl", 1) != UUcError_None);
	check_true("begin bad path: not active", ONgSweep_Active == UUcFalse);
	check_true("begin bad path: tap not registered", g_registered_tap == NULL);
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Error, "after failed begin");
	check_true("begin bad path: record inert", count_lines(g_report_path) == expect);

	/* 14. NULL path is rejected without opening anything. */
	check_true("begin NULL path: rejected", ONrSweep_Begin(NULL, "gl", 1) != UUcError_None);

	remove(g_report_path);

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return (g_fail == 0) ? 0 : 1;
}
