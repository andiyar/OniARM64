// ======================================================================
// test_sweep_diff.c
//
// Standalone unit tests for the baseline diff logic. Compile and run:
//
//   cc -Wall -Wextra -DSWEEP_DIFF_NO_MAIN tests/test_sweep_diff.c \
//      tools/sweep_diff.c -o /tmp/test_sweep_diff
//   /tmp/test_sweep_diff
//
// The CLI wrapper around this logic (argument handling, NDJSON and baseline
// parsing, exit codes) is covered end to end by tests/test_sweep_diff_cli.sh.
//
// g_report and g_baseline are file-scope on purpose: SWtFindingSet is 1.53 MB,
// so two of them as ordinary locals put 3.06 MB on the stack. That fits an
// 8 MB macOS main thread today, but it is a silly thing to spend and it stops
// fitting the moment a case wants a third set.
// ======================================================================
#include "../tools/sweep_diff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static SWtFindingSet g_report;
static SWtFindingSet g_baseline;

static void checkInt(const char *label, int actual, int expected)
{
	if (actual == expected) {
		g_pass++;
	} else {
		g_fail++;
		printf("FAIL %s: expected %d, got %d\n", label, expected, actual);
	}
}

static void checkStr(const char *label, const char *actual, const char *expected)
{
	if (strcmp(actual, expected) == 0) {
		g_pass++;
	} else {
		g_fail++;
		printf("FAIL %s: expected \"%s\", got \"%s\"\n", label, expected, actual);
	}
}

/* Reset both sets and hand back stable aliases for the case about to run. */
static void freshSets(SWtFindingSet **outReport, SWtFindingSet **outBaseline)
{
	SWrFindingSet_Init(&g_report);
	SWrFindingSet_Init(&g_baseline);
	*outReport = &g_report;
	*outBaseline = &g_baseline;
}

int main(void)
{
	SWtDiffResult	result;
	SWtFindingSet	*report;
	SWtFindingSet	*baseline;
	int				itr;

	/* ---------------------------------------------------------------- */
	/* the four verdicts                                                */
	/* ---------------------------------------------------------------- */

	/* clean: report exactly matches baseline */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   2, "particles", "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("clean regressions", result.numRegressions, 0);
	checkInt("clean stale",       result.numStale,       0);
	checkInt("clean exit",        SWrExitCode(&result),  0);

	/* regression: a finding not present in the baseline */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report, 12, "scripts", "spawn_guards", "null-deref", 0);
	SWrDiff(report, baseline, &result);
	checkInt("regression count", result.numRegressions, 1);
	checkInt("regression exit",  SWrExitCode(&result),  1);

	/* stale: baseline entry no longer produced */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("stale count", result.numStale,      1);
	checkInt("stale exit",  SWrExitCode(&result), 2);

	/* abort always fails even when baselined */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   9, "load", "level9", "crash", 1);
	SWrFindingSet_Add(baseline, 9, "load", "level9", "crash", 0);
	SWrDiff(report, baseline, &result);
	checkInt("abort count", result.numAborts,     1);
	checkInt("abort exit",  SWrExitCode(&result), 1);

	/* subject is part of the identity: same key, different subject, is new */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   2, "particles", "other_class", "too-large", 0);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("subject identity regressions", result.numRegressions, 1);
	checkInt("subject identity stale",       result.numStale,       1);

	/* ---------------------------------------------------------------- */
	/* identity: every one of the four fields discriminates             */
	/* ---------------------------------------------------------------- */

	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   3, "particles", "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("level identity regressions", result.numRegressions, 1);
	checkInt("level identity stale",       result.numStale,       1);

	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   2, "textures",  "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("phase identity regressions", result.numRegressions, 1);
	checkInt("phase identity stale",       result.numStale,       1);

	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   2, "particles", "w10_sni_p01", "too-small", 0);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("key identity regressions", result.numRegressions, 1);
	checkInt("key identity stale",       result.numStale,       1);

	/* ---------------------------------------------------------------- */
	/* duplicates: both sides are sets, a repeated identity counts once */
	/* ---------------------------------------------------------------- */

	/* three copies of one un-baselined finding is one regression */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(report, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(report, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("duplicate regressions counted once", result.numRegressions, 1);

	/* ...and one baseline line silences all three, with nothing left stale */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   2, "particles", "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(report,   2, "particles", "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(report,   2, "particles", "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("one baseline line covers duplicates", result.numRegressions, 0);
	checkInt("duplicates leave nothing stale",      result.numStale,       0);
	checkInt("duplicates covered exit",             SWrExitCode(&result),  0);

	/* duplicate aborts are one abort */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report, 9, "load", "level9", "crash", 1);
	SWrFindingSet_Add(report, 9, "load", "level9", "crash", 1);
	SWrDiff(report, baseline, &result);
	checkInt("duplicate aborts counted once", result.numAborts, 1);

	/*
		An abort and a non-abort of the same identity are different facts, so
		the dedupe must not collapse them: one abort plus one regression.
	*/
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report, 9, "load", "level9", "crash", 1);
	SWrFindingSet_Add(report, 9, "load", "level9", "crash", 0);
	SWrDiff(report, baseline, &result);
	checkInt("abort and warn of same identity: aborts",      result.numAborts,      1);
	checkInt("abort and warn of same identity: regressions", result.numRegressions, 1);

	/* a baseline that lists the same entry twice is one stale entry */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrFindingSet_Add(baseline, 2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("duplicate baseline entries counted once", result.numStale, 1);

	/* ---------------------------------------------------------------- */
	/* abort handling                                                   */
	/* ---------------------------------------------------------------- */

	/* an un-baselined abort is an abort, not also a regression */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report, 9, "load", "level9", "crash", 1);
	SWrDiff(report, baseline, &result);
	checkInt("unbaselined abort: aborts",      result.numAborts,      1);
	checkInt("unbaselined abort: regressions", result.numRegressions, 0);

	/*
		A baselined abort still fails, and because the report does contain the
		identity the baseline line is not stale either — exit is 1 on the
		abort alone.
	*/
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   9, "load", "level9", "crash", 1);
	SWrFindingSet_Add(baseline, 9, "load", "level9", "crash", 0);
	SWrDiff(report, baseline, &result);
	checkInt("baselined abort leaves no stale", result.numStale, 0);

	/* ---------------------------------------------------------------- */
	/* exit code precedence                                             */
	/* ---------------------------------------------------------------- */

	freshSets(&report, &baseline);
	SWrDiff(report, baseline, &result);
	checkInt("both sides empty is clean", SWrExitCode(&result), 0);

	/* regression plus stale: failure wins over churn */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,   12, "scripts",   "spawn_guards", "null-deref", 0);
	SWrFindingSet_Add(baseline,  2, "particles", "w10_sni_p01",  "too-large",  0);
	SWrDiff(report, baseline, &result);
	checkInt("regression plus stale: regressions", result.numRegressions, 1);
	checkInt("regression plus stale: stale",       result.numStale,       1);
	checkInt("regression plus stale: exit",        SWrExitCode(&result),  1);

	/* abort plus stale is still 1 */
	freshSets(&report, &baseline);
	SWrFindingSet_Add(report,    9, "load",      "level9",      "crash",     1);
	SWrFindingSet_Add(baseline,  2, "particles", "w10_sni_p01", "too-large", 0);
	SWrDiff(report, baseline, &result);
	checkInt("abort plus stale: exit", SWrExitCode(&result), 1);

	/* SWrExitCode reads the struct, not the sets */
	result.numRegressions = 0; result.numStale = 0; result.numAborts = 0;
	checkInt("exit code zeros",       SWrExitCode(&result), 0);
	result.numAborts = 1;
	checkInt("exit code abort only",  SWrExitCode(&result), 1);
	result.numAborts = 0; result.numStale = 5;
	checkInt("exit code stale only",  SWrExitCode(&result), 2);
	result.numRegressions = 1;
	checkInt("exit code regression beats stale", SWrExitCode(&result), 1);

	/* ---------------------------------------------------------------- */
	/* SWrFindingSet_Add contract                                       */
	/* ---------------------------------------------------------------- */

	/* NULL fields become empty strings rather than crashing */
	freshSets(&report, &baseline);
	checkInt("add returns 0 under the limit",
		SWrFindingSet_Add(report, 1, NULL, NULL, NULL, 0), 0);
	checkStr("NULL phase becomes empty",   report->findings[0].phase,   "");
	checkStr("NULL subject becomes empty", report->findings[0].subject, "");
	checkStr("NULL key becomes empty",     report->findings[0].key,     "");
	checkInt("no overflow flag under the limit", report->overflowed, 0);

	/*
		Over-long fields are truncated to SWcMaxFieldChars-1 and NUL-terminated.
		Two subjects differing only past that point therefore collide — a
		documented limit of the fixed-width record, not a bug to paper over.
		Real keys are capped at 96 chars by ONcSweep_KeyBufferSize and real
		subjects at 127 by the writer's escape buffer, so neither can reach it.
	*/
	{
		char longA[SWcMaxFieldChars + 64];
		char longB[SWcMaxFieldChars + 64];
		memset(longA, 'a', sizeof(longA) - 1); longA[sizeof(longA) - 1] = '\0';
		memset(longB, 'b', sizeof(longB) - 1); longB[sizeof(longB) - 1] = '\0';

		freshSets(&report, &baseline);
		SWrFindingSet_Add(report, 1, "phase", longA, "key", 0);
		checkInt("over-long field truncated",
			(int)strlen(report->findings[0].subject), SWcMaxFieldChars - 1);

		/*
			Overwrite the same slot with a second over-long value. strncpy
			writes exactly n bytes and adds no terminator when the source is
			at least that long, so without the explicit NUL the field would
			run on into the previous occupant's last byte. Slot reuse is the
			only way to see that; a first write into a zeroed set hides it.
		*/
		freshSets(&report, &baseline);
		SWrFindingSet_Add(report, 1, "phase", longB, "key", 0);
		checkInt("over-long field terminated when reusing a slot",
			(int)strlen(report->findings[0].subject), SWcMaxFieldChars - 1);
		checkInt("reused slot holds only the new value",
			(int)strspn(report->findings[0].subject, "b"), SWcMaxFieldChars - 1);

		/*
			The header sanctions a heap-allocated set, and malloc does not zero.
			strncpy(dst, src, n) writes exactly n bytes and no terminator when
			the source is at least that long, so on non-zero memory the field's
			last byte is whatever was there — the explicit NUL in SWiCopyField
			is the only thing standing between that and a run-on read. The
			static sets above cannot show this: their last byte is already 0
			and nothing ever writes it.
		*/
		{
			SWtFindingSet *heapSet = malloc(sizeof(SWtFindingSet));
			if (heapSet == NULL) {
				g_fail++;
				printf("FAIL could not allocate a heap finding set\n");
			} else {
				memset(heapSet, 0xAA, sizeof(SWtFindingSet));
				SWrFindingSet_Init(heapSet);
				SWrFindingSet_Add(heapSet, 1, "phase", longA, "key", 0);
				checkInt("over-long field terminated on unzeroed memory",
					(int)strlen(heapSet->findings[0].subject), SWcMaxFieldChars - 1);
				checkInt("short field terminated on unzeroed memory",
					(int)strlen(heapSet->findings[0].phase), 5);
				free(heapSet);
			}
		}
	}

	/*
		Overflow: filling the set exactly is fine, one more is rejected and
		latches the flag. main() turns that flag into exit 3, so a truncated
		report can never be reported as a pass — see the CLI test.
	*/
	freshSets(&report, &baseline);
	for (itr = 0; itr < SWcMaxFindings; itr++) {
		char subject[SWcMaxFieldChars];
		snprintf(subject, sizeof(subject), "subject_%d", itr);
		SWrFindingSet_Add(report, itr % 15, "particles", subject, "too-large", 0);
	}
	checkInt("set fills to the limit",       report->count,      SWcMaxFindings);
	checkInt("no overflow flag when exact",  report->overflowed, 0);
	checkInt("add past the limit returns -1",
		SWrFindingSet_Add(report, 1, "particles", "one_too_many", "too-large", 0), -1);
	checkInt("overflow flag latches", report->overflowed, 1);
	checkInt("count does not grow past the limit", report->count, SWcMaxFindings);

	/*
		The diff is an O(n^2) linear scan in both directions. At the 4096-entry
		ceiling a fully disjoint report/baseline pair — the worst case, every
		SWiContains scanning to the end — takes ~0.37 s unoptimised on an M-series
		Mac; the realistic 30-cell load of a few hundred findings is ~15 ms. That
		is nothing against a sweep that spends minutes loading levels, so the scan
		stays as it is. This case pins correctness at full size.
	*/
	freshSets(&report, &baseline);
	for (itr = 0; itr < SWcMaxFindings; itr++) {
		char subject[SWcMaxFieldChars];
		snprintf(subject, sizeof(subject), "subject_%d", itr);
		SWrFindingSet_Add(report,   itr % 15, "particles", subject, "too-large", 0);
		SWrFindingSet_Add(baseline, itr % 15, "particles", subject, "too-large", 0);
	}
	SWrDiff(report, baseline, &result);
	checkInt("full-size matched set: regressions", result.numRegressions, 0);
	checkInt("full-size matched set: stale",       result.numStale,       0);
	checkInt("full-size matched set: exit",        SWrExitCode(&result),  0);

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
