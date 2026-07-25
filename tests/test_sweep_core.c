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
#include "Oni_GameStatePrivate.h"
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

/*
	Re-entrancy seam. ONrSweep_Record's only outbound call is
	ONrSweep_Report_FormatLine, so the runner compiles Oni_Sweep_Report.c with
	that name macro-renamed and this wrapper takes its place. Setting
	g_reenter_once makes a console line arrive from inside the writer — the one
	shape ONgSweep_InTap exists to survive, and otherwise unreachable from a
	test. In the shipping configuration the wrapper is a plain pass-through.
*/
static UUtBool g_reenter_once = UUcFalse;

void ONrSweep_Report_FormatLine_real(
	char *outLine, size_t inLineSize, const char *inRenderer, int inLevel,
	const char *inPhase, const char *inSubject, ONtSweepSeverity inSeverity,
	const char *inMessage);

void ONrSweep_Report_FormatLine(
	char *outLine, size_t inLineSize, const char *inRenderer, int inLevel,
	const char *inPhase, const char *inSubject, ONtSweepSeverity inSeverity,
	const char *inMessage)
{
	if (g_reenter_once) {
		g_reenter_once = UUcFalse;
#ifdef ONI_SWEEP_CONSOLE
		if (g_registered_console_tap != NULL) {
			g_registered_console_tap("re-entrant console line");
		}
#endif
	}

	ONrSweep_Report_FormatLine_real(outLine, inLineSize, inRenderer, inLevel,
		inPhase, inSubject, inSeverity, inMessage);
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

/*
	Models the real ONrGameState_Update's tick accounting, which is the whole
	point of this stub. Faithfully reproduced from Oni_GameState.c:

	  iComputeDeltaTicks   delta = serverTime - gameTime, clamped to
	                       cMaxTicksPerFrame (6), then serverTime = gameTime + delta
	  heartbeat loop       gameTime += 1, delta times
	  on exit              *outTicksUpdated = delta

	The consequence that matters: serverTime ends up equal to gameTime, so a
	caller that does not advance serverTime itself gets delta == 0 on every
	subsequent call and no simulation runs at all. A stub that just returned 1
	would hide exactly that.
*/
static ONtGameState		g_game_state;
ONtGameState			*ONgGameState = NULL;

#define kMaxTicksPerFrame	6

static UUtUns32	g_forced_delta = 0;		/* non-zero models ONgFastMode / cutscene skip */
static UUtBool	g_force_zero = UUcFalse;	/* models ONgSingleStep */

UUtError ONrGameState_Update(UUtUns16 numActionsInBuffer, LItAction *actionBuffer, UUtUns32 *outTicksUpdated)
{
	UUtUns32 delta;

	(void) numActionsInBuffer;
	(void) actionBuffer;

	g_update_calls++;

	if (g_force_zero) {
		if (outTicksUpdated != NULL) *outTicksUpdated = 0;
		return g_update_result;
	}

	delta = ONgGameState->serverTime - ONgGameState->gameTime;
	if (delta > kMaxTicksPerFrame) delta = kMaxTicksPerFrame;
	if (g_forced_delta != 0) delta = g_forced_delta;

	ONgGameState->gameTime += delta;
	ONgGameState->serverTime = ONgGameState->gameTime;

	if (outTicksUpdated != NULL) *outTicksUpdated = delta;

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

/*
	Streams the whole file rather than reading a fixed prefix. The bulk-console
	case writes a few hundred KB, and a prefix search silently reported "absent"
	for everything after it — a false pass waiting to happen for any assertion
	placed late in the run.
*/
static int file_contains(const char *inPath, const char *inNeedle)
{
	char	buffer[65536];
	FILE	*f = fopen(inPath, "r");
	size_t	needle = strlen(inNeedle);
	size_t	keep = (needle > 0) ? needle - 1 : 0;
	size_t	held = 0;
	int		found = 0;

	if (f == NULL) return 0;
	if (needle == 0 || needle >= sizeof(buffer)) { fclose(f); return 0; }

	for (;;) {
		size_t got = fread(buffer + held, 1, sizeof(buffer) - 1 - held, f);

		if (got == 0) break;
		held += got;
		buffer[held] = '\0';

		if (strstr(buffer, inNeedle) != NULL) { found = 1; break; }

		/* carry the tail over so a match spanning two reads is not missed */
		if (held > keep) {
			memmove(buffer, buffer + held - keep, keep);
			held = keep;
		}
	}

	fclose(f);
	return found;
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

	memset(&g_game_state, 0, sizeof(g_game_state));
	ONgGameState = &g_game_state;

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

	check_true("begin: seeded both RNG streams", g_seed_calls == 1 && g_local_seed_calls == 1);

	/* 2b. Records before any phase sets its own context belong to "init", not
	   to an empty bucket in the merged report. */
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Warn, "something during startup");
	expect++;
	check_true("init phase: default phase recorded",
		file_contains(g_report_path, "\"phase\":\"init\""));

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

	/* 6d. Re-entrancy: a console line raised from inside the writer must be
	   dropped, not recursed on. One record comes out, not two, and the process
	   is still here to assert it. */
	{
		int before = count_lines(g_report_path);

		g_reenter_once = UUcTrue;
		g_registered_tap("outer warning that prints from inside the writer");
		g_reenter_once = UUcFalse;

		check_true("reentrancy: exactly one record, nested one dropped",
			count_lines(g_report_path) == before + 1);
		check_true("reentrancy: nested text absent",
			!file_contains(g_report_path, "re-entrant console line"));
		expect = before + 1;
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

	/* 8. GAME TIME actually elapses.
	   This is the assertion the harness lives or dies on. Counting calls to
	   ONrGameState_Update proves nothing: the bug this replaced made 600 calls
	   that advanced one tick between them, so every phase would have reported a
	   clean level having simulated essentially nothing. Assert the clock. */
	{
		UUtUns32 before = ONgGameState->gameTime;

		g_update_calls = 0;
		g_update_result = UUcError_None;
		ONrSweep_Tick(ONcSweep_SettleCharacter);

		check_true("tick: 60 ticks of game time elapsed",
			ONgGameState->gameTime - before == ONcSweep_SettleCharacter);
		/* One heartbeat per Update call, by design: ONrSweep_Tick advances
		   serverTime by exactly 1 each time round. Batching six at a time would
		   reach the same game time but run the per-call work outside the
		   heartbeat loop — P3rUpdate, CArUpdate, the sound manager — a sixth as
		   often, which is not what the real frame loop does. */
		check_true("tick: one heartbeat per update call",
			g_update_calls == ONcSweep_SettleCharacter);
		check_true("tick: nothing recorded on success", count_lines(g_report_path) == expect);
	}

	/* 8b. The long settles too — a particle settle that silently advanced no
	   time is the failure mode with the least chance of being noticed. */
	{
		UUtUns32 before = ONgGameState->gameTime;

		ONrSweep_Tick(ONcSweep_SettleParticle);
		check_true("tick: 600 ticks of game time elapsed",
			ONgGameState->gameTime - before == ONcSweep_SettleParticle);
	}

	/* 8c. Repeatable. Two identical settles advance identically — no wall clock
	   anywhere in the loop, which is what lets the fixed seeding mean anything. */
	{
		UUtUns32 first, second, mark;

		mark = ONgGameState->gameTime;
		ONrSweep_Tick(ONcSweep_SettleScript);
		first = ONgGameState->gameTime - mark;

		mark = ONgGameState->gameTime;
		ONrSweep_Tick(ONcSweep_SettleScript);
		second = ONgGameState->gameTime - mark;

		check_true("tick: two identical settles advance identically", first == second);
		check_true("tick: and by the requested amount", first == ONcSweep_SettleScript);
	}

	/* 8d. When the engine hands back more ticks per call than asked for
	   (ONgFastMode gives 24, cutscene skipping 32), the settle still measures
	   game time rather than multiplying it by the batch size. */
	{
		UUtUns32 before = ONgGameState->gameTime;
		UUtUns32 advanced;

		g_forced_delta = 24;
		ONrSweep_Tick(ONcSweep_SettleCharacter);
		g_forced_delta = 0;

		advanced = ONgGameState->gameTime - before;
		check_true("tick: fast batches do not overshoot wildly",
			advanced >= ONcSweep_SettleCharacter && advanced < ONcSweep_SettleCharacter + 24);
	}

	/* 8e. An update that never advances time ends the settle as a finding
	   rather than spinning forever. Reachable for real: iComputeDeltaTicks
	   zeroes the delta under ONgSingleStep. */
	{
		UUtUns32 before = ONgGameState->gameTime;

		g_update_calls = 0;
		g_force_zero = UUcTrue;
		ONrSweep_Tick(ONcSweep_SettleAI);
		g_force_zero = UUcFalse;

		expect++;
		check_true("tick: stall gave up rather than hanging", g_update_calls < 100);
		check_true("tick: stall advanced no time", ONgGameState->gameTime == before);
		check_true("tick: stall recorded", count_lines(g_report_path) == expect);
		check_true("tick: stall message present",
			file_contains(g_report_path, "advanced no game time"));
	}

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
