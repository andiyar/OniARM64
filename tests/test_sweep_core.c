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

	snprintf(g_report_path, sizeof(g_report_path), "%ssweep_core_test_%d.ndjson",
		(tmpdir != NULL) ? tmpdir : "/tmp/", (int) getpid());
	remove(g_report_path);

	/* 1. Before Begin: inert. Nothing recorded, nothing created, no crash. */
	check_true("pre-begin: not active", ONgSweep_Active == UUcFalse);
	ONrSweep_Record("p", "s", ONcSweepSeverity_Error, "before begin");
	ONrSweep_SetContext("phase", "subject");
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Warn, "still before begin");
	check_true("pre-begin: no report file", count_lines(g_report_path) == -1);

	/* 2. Begin registers the tap and flips the flag. */
	error = ONrSweep_Begin(g_report_path, "metal", 4);
	check_true("begin: succeeded", error == UUcError_None);
	check_true("begin: active", ONgSweep_Active == UUcTrue);
	check_true("begin: tap registered", g_registered_tap != NULL);

	/* 3. Beginning twice must not leak the handle or split the report. */
	check_true("begin twice: rejected",
		ONrSweep_Begin(g_report_path, "metal", 4) != UUcError_None);

	/* 4. Records land, and land immediately — the file is read while the
	   sweep is still open, so an unflushed line would not be there. */
	ONrSweep_Record("spawn", "w10_sni_p01", ONcSweepSeverity_Warn, "can't find weapon class");
	check_true("record: line on disk before End", count_lines(g_report_path) == 1);
	check_true("record: renderer present", file_contains(g_report_path, "\"renderer\":\"metal\""));
	check_true("record: level present", file_contains(g_report_path, "\"level\":4"));
	check_true("record: subject present", file_contains(g_report_path, "\"subject\":\"w10_sni_p01\""));

	/* 5. NULL phase/subject inherit the context. */
	ONrSweep_SetContext("particle", "flash01");
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Leak, "leaked something");
	check_true("context: second line on disk", count_lines(g_report_path) == 2);
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
		check_true("long context: line written", count_lines(g_report_path) == 3);
		ONrSweep_SetContext("particle", "flash01");
	}

	/* 6. The registered tap consumes warnings rather than letting them through
	   to the modal, and each one becomes a record. */
	check_true("tap: consumes warning", g_registered_tap("a tapped warning") == UUcTrue);
	check_true("tap: warning recorded", count_lines(g_report_path) == 4);
	check_true("tap: warning text present", file_contains(g_report_path, "a tapped warning"));

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
	check_true("tick: nothing recorded on success", count_lines(g_report_path) == 4);

	/* 9. A failing update records once and stops rather than spinning. */
	g_update_calls = 0;
	g_update_result = UUcError_Generic;
	ONrSweep_Tick(100);
	check_true("tick: stopped at first error", g_update_calls == 1);
	check_true("tick: error recorded once", count_lines(g_report_path) == 5);
	g_update_result = UUcError_None;

	/* 10. End unregisters the tap and clears the flag. */
	ONrSweep_End();
	check_true("end: tap unregistered", g_registered_tap == NULL);
	check_true("end: not active", ONgSweep_Active == UUcFalse);

	/* 11. After End: inert again. No write to a closed handle. */
	ONrSweep_Record("p", "s", ONcSweepSeverity_Abort, "after end");
	ONrSweep_SetContext("late", "late");
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Error, "also after end");
	check_true("post-end: nothing appended", count_lines(g_report_path) == 5);
	check_true("post-end: no late text", !file_contains(g_report_path, "after end"));

	/* 12. End is safe to call again. */
	ONrSweep_End();
	check_true("end twice: survived", 1);

	/* 13. A failed Begin leaves the module inert rather than half-open. */
	check_true("begin bad path: rejected",
		ONrSweep_Begin("/nonexistent-dir-for-sweep-test/report.ndjson", "gl", 1) != UUcError_None);
	check_true("begin bad path: not active", ONgSweep_Active == UUcFalse);
	check_true("begin bad path: tap not registered", g_registered_tap == NULL);
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Error, "after failed begin");
	check_true("begin bad path: record inert", count_lines(g_report_path) == 5);

	/* 14. NULL path is rejected without opening anything. */
	check_true("begin NULL path: rejected", ONrSweep_Begin(NULL, "gl", 1) != UUcError_None);

	remove(g_report_path);

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return (g_fail == 0) ? 0 : 1;
}
