// ======================================================================
// test_sweep_phases.c
//
// Tests for the load and character sweep phases (tasks 7 and 8), driven
// through the real ONrSweep_RunAllPhases in Oni_Sweep.c with the engine
// stubbed (tests/test_sweep_engine_stubs.c).
//
// What these guard, in rough order of how expensive the failure would be:
//
//   * a cell always emits at least one record. sweep_diff exits 3 on a
//     report that parses to zero records rather than calling it clean, so a
//     phase set that emits nothing on some path turns that level into a
//     permanent "no evidence" result that nobody can act on.
//   * level 0 is never handed to ONrLevel_Load. Engine init already loaded
//     it by a different route that leaves no game-state level behind, and
//     the sweep entry point runs after that.
//   * nothing runs against a level that failed to load. ONrLevel_Load bails
//     at the first failing step, so what it leaves behind is not a level.
//   * NULL level and NULL setup array are skips with a reason, not crashes.
//     Both are reachable from content: an ONLV without a character setup
//     array is a legal template.
//   * the enumeration cap is checked against the true instance count, so a
//     level with more character classes than the harness can hold says so
//     instead of silently auditing a prefix.
//   * subjects never contain whitespace. sweep_diff's baseline format is
//     whitespace-delimited, so a subject with a space in it would gate as a
//     regression and a stale entry simultaneously, forever.
//
// Build and run: tests/test_sweep_phases.sh   (from repo root)
// ======================================================================

#include "Oni_Sweep.h"
#include "Oni_GameState.h"
#include "Oni_GameStatePrivate.h"
#include "Oni_Character.h"
#include "Oni_AI_Setup.h"
#include "BFW_Console.h"

#include "test_sweep_engine_stubs.h"

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

// --- stubs for the sweep core's own externals -------------------------

void UUrError_SetWarningTap(UUtWarningTap inTap)
{
	(void) inTap;
}

#ifdef ONI_SWEEP_CONSOLE
void COrConsole_SetTap(COtConsoleTap inTap)
{
	(void) inTap;
}
#endif

void UUrRandom_SetSeed(UUtUns32 seed)
{
	(void) seed;
}

void UUrLocalRandom_SetSeed(UUtUns32 seed)
{
	(void) seed;
}

void UUcArglist_Call UUrStartupMessage(const char *format, ...)
{
	(void) format;
}

/*
	Same tick accounting as the real ONrGameState_Update: the delta is
	serverTime - gameTime clamped to six, gameTime advances by it, and
	serverTime ends equal to gameTime. A stub returning a constant would let a
	settle that never advances the clock pass.
*/
static ONtGameState	g_game_state;
ONtGameState		*ONgGameState = NULL;

static int			g_update_calls = 0;

UUtError ONrGameState_Update(UUtUns16 numActionsInBuffer, LItAction *actionBuffer, UUtUns32 *outTicksUpdated)
{
	UUtUns32 delta;

	(void) numActionsInBuffer;
	(void) actionBuffer;

	g_update_calls++;

	delta = ONgGameState->serverTime - ONgGameState->gameTime;
	if (delta > 6) delta = 6;

	ONgGameState->gameTime += delta;
	ONgGameState->serverTime = ONgGameState->gameTime;

	if (outTicksUpdated != NULL) *outTicksUpdated = delta;

	return UUcError_None;
}

// --- report reading ---------------------------------------------------

static char g_report_path[512];

#define MaxReportLines	4096

static char	g_lines[MaxReportLines][1024];
static int	g_numLines = 0;

static void read_report(void)
{
	FILE *f = fopen(g_report_path, "r");

	g_numLines = 0;
	if (f == NULL) return;

	while ((g_numLines < MaxReportLines) &&
		   (fgets(g_lines[g_numLines], sizeof(g_lines[0]), f) != NULL)) {
		g_numLines++;
	}

	fclose(f);
}

/* Number of report lines containing every one of the (up to three) needles. */
static int count_matching(const char *a, const char *b, const char *c)
{
	int itr;
	int found = 0;

	for (itr = 0; itr < g_numLines; itr++) {
		if ((a != NULL) && (strstr(g_lines[itr], a) == NULL)) continue;
		if ((b != NULL) && (strstr(g_lines[itr], b) == NULL)) continue;
		if ((c != NULL) && (strstr(g_lines[itr], c) == NULL)) continue;
		found++;
	}

	return found;
}

/*
	A subject with whitespace in it can be written into a report but never into
	a baseline line that matches it (tools/sweep_diff.h), so scan every emitted
	subject rather than only the ones a given assertion happens to look at.
*/
static int any_subject_has_whitespace(void)
{
	int itr;

	for (itr = 0; itr < g_numLines; itr++) {
		const char	*start = strstr(g_lines[itr], "\"subject\":\"");
		const char	*cursor;

		if (start == NULL) continue;
		start += strlen("\"subject\":\"");

		for (cursor = start; (*cursor != '"') && (*cursor != '\0'); cursor++) {
			if ((*cursor == ' ') || (*cursor == '\t')) return 1;
		}
	}

	return 0;
}

static void begin_run(UUtUns16 inLevel)
{
	remove(g_report_path);
	SweepStubs_Reset();
	memset(&g_game_state, 0, sizeof(g_game_state));
	ONgGameState = &g_game_state;
	g_update_calls = 0;
	ONrSweep_Begin(g_report_path, "gl", inLevel);
}

static void end_run(void)
{
	ONrSweep_End();
	read_report();
}

// --- fixtures ---------------------------------------------------------

/*
	A character setup array with room for four setups. Declared with the array
	oversized rather than using the template's trailing [1], because that is how
	the template manager hands one over at runtime.
*/
typedef struct TestSetupArray {
	AItCharacterSetupArray	header;
	AItCharacterSetup		extra[3];
} TestSetupArray;

static TestSetupArray g_setups;

static void make_setups(int inCount, const UUtUns16 *inScriptIDs)
{
	int itr;

	memset(&g_setups, 0, sizeof(g_setups));
	g_setups.header.numCharacterSetups = (UUtUns16) inCount;

	for (itr = 0; itr < inCount; itr++) {
		g_setups.header.characterSetups[itr].defaultScriptID = inScriptIDs[itr];
		g_setups.header.characterSetups[itr].defaultFlagID = (UUtInt16) (2000 + itr);
	}

	SweepStubs_SetLevelSetups(&g_setups);
}

static ONtCharacterClass	g_classes[4];
static int					g_fakeBody;
static int					g_fakeAnimations;

static void make_classes(int inCount)
{
	int itr;

	memset(g_classes, 0, sizeof(g_classes));

	for (itr = 0; itr < inCount; itr++) {
		g_classes[itr].body = (TRtBodySet *) &g_fakeBody;
		g_classes[itr].animations = (TRtAnimationCollection *) &g_fakeAnimations;
	}
}

// --- tests ------------------------------------------------------------

int main(void)
{
	const char	*tmpdir = getenv("TMPDIR");

	snprintf(g_report_path, sizeof(g_report_path), "%ssweep_phases_test_%d.ndjson",
		(tmpdir != NULL) ? tmpdir : "/tmp/", (int) getpid());

	/* 1. Level 0 is never reloaded, and neither phase runs against it. */
	{
		begin_run(0);
		ONrSweep_RunAllPhases(0);
		end_run();

		check_true("level0: ONrLevel_Load not called", SweepStub_LoadCalls == 0);
		check_true("level0: load phase skipped with a reason",
			count_matching("\"phase\":\"load\"", "\"severity\":\"skipped\"", "level0") == 1);
		check_true("level0: character phase skipped",
			count_matching("\"phase\":\"characters\"", "\"severity\":\"skipped\"", "menu level") == 1);
		check_true("level0: nothing spawned", SweepStub_SpawnCalls == 0);
		check_true("level0: terminal record present",
			count_matching("\"phase\":\"done\"", "cell completed", NULL) == 1);
		check_true("level0: three records exactly", g_numLines == 3);
	}

	/* 2. A load failure aborts and stops everything downstream. Running the
	   character phase against what ONrLevel_Load abandoned is not a way to
	   learn more about the level. */
	{
		begin_run(4);
		SweepStub_LoadResult = UUcError_Generic;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("load fail: attempted once", SweepStub_LoadCalls == 1);
		check_true("load fail: recorded as abort",
			count_matching("\"phase\":\"load\"", "\"severity\":\"abort\"", "ONrLevel_Load failed") == 1);
		check_true("load fail: character phase skipped",
			count_matching("\"phase\":\"characters\"", "\"severity\":\"skipped\"", "did not load") == 1);
		check_true("load fail: nothing enumerated", SweepStub_TagCountCalls == 0);
		check_true("load fail: nothing spawned", SweepStub_SpawnCalls == 0);
		check_true("load fail: still terminated",
			count_matching("\"phase\":\"done\"", NULL, NULL) == 1);
	}

	/* 3. A load that succeeds but publishes no level instance is an abort too,
	   not a NULL dereference two phases later. */
	{
		begin_run(4);
		SweepStub_LoadPublishesLevel = UUcFalse;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("no level instance: recorded as abort",
			count_matching("\"phase\":\"load\"", "\"severity\":\"abort\"", "no level instance") == 1);
		check_true("no level instance: nothing spawned", SweepStub_SpawnCalls == 0);
		check_true("no level instance: still terminated",
			count_matching("\"phase\":\"done\"", NULL, NULL) == 1);
	}

	/* 4. The progress bar is off. It draws, and a sweep runs with drawing
	   bypassed, so a run with it on would hang or crash unattended. */
	{
		begin_run(9);
		ONrSweep_RunAllPhases(9);
		end_run();

		check_true("load: correct level requested", SweepStub_LoadLevel == 9);
		check_true("load: progress bar suppressed", SweepStub_LoadProgressBar == UUcFalse);
		check_true("load: game time settled after load", g_update_calls > 0);
	}

	/* 5. A level with no character setup array is a skip with a reason. An ONLV
	   is free not to have one, so this is content, not corruption. */
	{
		begin_run(4);
		SweepStubs_SetLevelSetups(NULL);
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("no setup array: skipped with a reason",
			count_matching("\"phase\":\"characters\"", "\"severity\":\"skipped\"",
				"no character setup array") == 1);
		check_true("no setup array: nothing spawned", SweepStub_SpawnCalls == 0);
		check_true("no setup array: no classes is also a skip",
			count_matching("\"subject\":\"classes\"", "\"severity\":\"skipped\"", NULL) == 1);
	}

	/* 6. An empty setup array is a skip, not silence. */
	{
		begin_run(4);
		make_setups(0, NULL);
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("empty setups: skipped with a reason",
			count_matching("\"phase\":\"characters\"", "\"severity\":\"skipped\"",
				"no character setups") == 1);
		check_true("empty setups: nothing spawned", SweepStub_SpawnCalls == 0);
	}

	/* 7. Every setup gets spawned, each attributed to its own script ID, and
	   each settled for. */
	{
		static const UUtUns16 ids[3] = { 101, 202, 303 };

		begin_run(4);
		make_setups(3, ids);
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("spawn: one per setup", SweepStub_SpawnCalls == 3);
		check_true("spawn: no findings on the happy path",
			count_matching("\"phase\":\"characters\"", "\"severity\":\"error\"", NULL) == 0);
		check_true("spawn: settle ran per spawn",
			g_update_calls >= 4 * ONcSweep_SettleCharacter);
		check_true("spawn: y offset applied to the BNV query",
			SweepStub_LastNodeQueryY == 100.0f + ONcCharacterOffsetToBNV);
	}

	/* 8. A missing flag, a flag outside a BNV and a refused spawn are all
	   errors attributed to the setup they belong to, and none of them stops
	   the remaining setups from being tried. */
	{
		static const UUtUns16 ids[2] = { 101, 202 };

		begin_run(4);
		make_setups(2, ids);
		SweepStub_FlagExists = UUcFalse;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("missing flag: one error per setup",
			count_matching("\"phase\":\"characters\"", "default flag does not exist", NULL) == 2);
		check_true("missing flag: attributed to the script id",
			count_matching("\"subject\":\"setup_101\"", "default flag", NULL) == 1);
		check_true("missing flag: no spawn attempted", SweepStub_SpawnCalls == 0);

		begin_run(4);
		make_setups(2, ids);
		SweepStub_PointInBNV = UUcFalse;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("outside BNV: one error per setup",
			count_matching("\"phase\":\"characters\"", "not inside a BNV", NULL) == 2);
		check_true("outside BNV: no spawn attempted", SweepStub_SpawnCalls == 0);

		begin_run(4);
		make_setups(2, ids);
		SweepStub_SpawnResult = UUcError_Generic;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("spawn refused: one error per setup",
			count_matching("\"phase\":\"characters\"", "refused the spawn", NULL) == 2);
		check_true("spawn refused: every setup still tried", SweepStub_SpawnCalls == 2);
	}

	/* 8b. A full character table refuses the later spawns while the earlier
	   ones succeed. Every setup is still attempted and reported — the level
	   defines at most 19 of them, so there is no flood to guard against, and
	   knowing which ones did not make it is the useful part. */
	{
		static const UUtUns16 ids[4] = { 1, 2, 3, 4 };

		begin_run(4);
		make_setups(4, ids);
		SweepStub_SpawnCapacity = 2;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("table full: every setup attempted", SweepStub_SpawnCalls == 4);
		check_true("table full: only the refused ones recorded",
			count_matching("refused the spawn", NULL, NULL) == 2);
		check_true("table full: attributed to the setups that failed",
			count_matching("\"subject\":\"setup_3\"", "refused", NULL) == 1 &&
			count_matching("\"subject\":\"setup_4\"", "refused", NULL) == 1);
	}

	/* 9. Character classes: a complete one is silent, and each of the three
	   conditions ONrGameState_NewCharacter refuses a spawn on is reported
	   against the class it belongs to. */
	{
		void *list[3];

		begin_run(4);
		make_classes(3);
		g_classes[1].body = NULL;
		g_classes[2].animations = NULL;
		list[0] = &g_classes[0];
		list[1] = &g_classes[1];
		list[2] = &g_classes[2];
		SweepStubs_SetClassList(list, 3);
		SweepStubs_SetInstanceName(&g_classes[0], "konoko_generic");
		SweepStubs_SetInstanceName(&g_classes[1], "striker_easy");
		SweepStubs_SetInstanceName(&g_classes[2], "comguy_1");
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("classes: complete class is silent",
			count_matching("\"subject\":\"konoko_generic\"", NULL, NULL) == 0);
		check_true("classes: missing body reported against its class",
			count_matching("\"subject\":\"striker_easy\"", "no body", NULL) == 1);
		check_true("classes: missing animations reported against its class",
			count_matching("\"subject\":\"comguy_1\"", "no animation collection", NULL) == 1);
	}

	/* 9b. An animation collection with no Stand entry is the issue #97 shape:
	   non-NULL, so the two checks above pass, and fatal on activation. */
	{
		void *list[1];

		begin_run(4);
		make_classes(1);
		list[0] = &g_classes[0];
		SweepStubs_SetClassList(list, 1);
		SweepStubs_SetInstanceName(&g_classes[0], "konoko_generic");
		SweepStub_CollectionHasStand = UUcFalse;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("classes: missing stand animation reported",
			count_matching("\"subject\":\"konoko_generic\"", "no standing animation", NULL) == 1);
	}

	/* 9c. An unnamed class still gets a stable identity, and its findings are
	   still reported rather than dropped for want of a subject. */
	{
		void *list[1];

		begin_run(4);
		make_classes(1);
		g_classes[0].body = NULL;
		list[0] = &g_classes[0];
		SweepStubs_SetClassList(list, 1);
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("classes: unnamed class reported as such",
			count_matching("\"subject\":\"class_0\"", "no name", NULL) == 1);
		check_true("classes: unnamed class findings still land",
			count_matching("\"subject\":\"class_0\"", "no body", NULL) == 1);
	}

	/* 10. A name with whitespace in it is folded, because sweep_diff's baseline
	   is whitespace-delimited and could otherwise never match the record. */
	{
		void	*list[1];
		char	spaced[] = "my broken class";

		begin_run(4);
		make_classes(1);
		g_classes[0].body = NULL;
		list[0] = &g_classes[0];
		SweepStubs_SetClassList(list, 1);
		SweepStubs_SetInstanceName(&g_classes[0], spaced);
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("subjects: whitespace folded",
			count_matching("\"subject\":\"my_broken_class\"", NULL, NULL) == 1);
		check_true("subjects: no whitespace anywhere in the report",
			!any_subject_has_whitespace());
	}

	/* 11. More classes than the enumeration can hold is reported, not silently
	   truncated. TMrInstance_GetTagCount reports the true total; the harness's
	   own count is capped, and the difference is the whole check. */
	{
		void *list[2];

		begin_run(4);
		make_classes(2);
		list[0] = &g_classes[0];
		list[1] = &g_classes[1];
		SweepStubs_SetClassList(list, 2);
		/* SetClassList makes the two agree; overriding the tag count afterwards
		   is what a level with more classes than the cap looks like from inside
		   the harness — the enumeration returns its cap, GetTagCount returns
		   the truth, and the harness has to notice the gap. */
		SweepStub_TagCount = 600;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("cap: truncation reported",
			count_matching("\"subject\":\"classes\"", "audit truncated", NULL) == 1);
	}

	/* 12. An enumeration that fails is an error, and the harness does not go on
	   to read the buffer it never filled. */
	{
		begin_run(4);
		SweepStub_ListResult = UUcError_Generic;
		SweepStub_TagCount = 99;
		ONrSweep_RunAllPhases(4);
		end_run();

		check_true("enumeration error: recorded",
			count_matching("\"subject\":\"classes\"", "could not enumerate", NULL) == 1);
		check_true("enumeration error: tag count not consulted",
			SweepStub_TagCountCalls == 0);
	}

	/* 13. Whatever else happens, a cell emits records. sweep_diff refuses to
	   call a report with none of them clean, so an empty one is a level nobody
	   can get a verdict on. */
	{
		begin_run(4);
		ONrSweep_RunAllPhases(4);
		end_run();
		check_true("evidence: happy path still emits records", g_numLines >= 1);

		begin_run(4);
		SweepStub_LoadResult = UUcError_Generic;
		ONrSweep_RunAllPhases(4);
		end_run();
		check_true("evidence: failed load still emits records", g_numLines >= 1);
	}

	remove(g_report_path);

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return (g_fail == 0) ? 0 : 1;
}
