/*
	sweep_diff — compare a merged sweep report against a committed baseline.

	Build:  cc -Wall -Wextra -o build/sweep_diff tools/sweep_diff.c
	Usage:  sweep_diff <merged.ndjson> <baseline.txt>
	Exit:   0 clean, 1 regressions or aborts, 2 only stale, 3 usage/IO error

	This tool deliberately does not link Oni_Sweep_Report.c / Oni_Sweep_Normalize.c.
	It reads the "key" field the sweep already wrote rather than recomputing it,
	so the gate cannot disagree with the report about what a finding is called.
*/
#include "sweep_diff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void SWrFindingSet_Init(SWtFindingSet *ioSet)
{
	ioSet->count = 0;
	ioSet->overflowed = 0;
}

static void SWiCopyField(char *outField, const char *inValue)
{
	if (inValue == NULL) {
		outField[0] = '\0';
		return;
	}
	strncpy(outField, inValue, SWcMaxFieldChars - 1);
	outField[SWcMaxFieldChars - 1] = '\0';
}

int SWrFindingSet_Add(
	SWtFindingSet	*ioSet,
	int				inLevel,
	const char		*inPhase,
	const char		*inSubject,
	const char		*inKey,
	int				inIsAbort)
{
	SWtFinding *finding;

	if (ioSet->count >= SWcMaxFindings) {
		ioSet->overflowed = 1;
		return -1;
	}

	finding = &ioSet->findings[ioSet->count];
	finding->level = inLevel;
	SWiCopyField(finding->phase, inPhase);
	SWiCopyField(finding->subject, inSubject);
	SWiCopyField(finding->key, inKey);
	finding->isAbort = inIsAbort;
	ioSet->count++;

	return 0;
}

static int SWiSameIdentity(const SWtFinding *inA, const SWtFinding *inB)
{
	return inA->level == inB->level &&
		strcmp(inA->phase,   inB->phase)   == 0 &&
		strcmp(inA->subject, inB->subject) == 0 &&
		strcmp(inA->key,     inB->key)     == 0;
}

static int SWiContains(const SWtFindingSet *inSet, const SWtFinding *inFinding)
{
	int itr;
	for (itr = 0; itr < inSet->count; itr++) {
		if (SWiSameIdentity(&inSet->findings[itr], inFinding)) return 1;
	}
	return 0;
}

/*
	True if the same identity (and the same abort-ness) appears earlier in the
	set, i.e. entry inIndex is a duplicate that has already been accounted for.

	A gate answers "which identities in this run are not accepted?", and an
	identity repeated N times is one fact stated N times. Counting it N times
	inflates the verdict and spams the log with no new information, and it
	could never be paid down: the maintainer accepts a finding by adding ONE
	baseline line, which silences all N at once. It would also be inconsistent
	with the stale direction, which is already set-flavoured — SWiContains
	stops at the first match, so one baseline entry covering five duplicate
	report records is correctly not stale.
*/
static int SWiAlreadySeen(const SWtFindingSet *inSet, int inIndex)
{
	int itr;
	for (itr = 0; itr < inIndex; itr++) {
		if (inSet->findings[itr].isAbort == inSet->findings[inIndex].isAbort &&
			SWiSameIdentity(&inSet->findings[itr], &inSet->findings[inIndex])) {
			return 1;
		}
	}
	return 0;
}

void SWrDiff(
	const SWtFindingSet	*inReport,
	const SWtFindingSet	*inBaseline,
	SWtDiffResult		*outResult)
{
	int itr;

	outResult->numRegressions = 0;
	outResult->numStale = 0;
	outResult->numAborts = 0;

	for (itr = 0; itr < inReport->count; itr++) {
		const SWtFinding *finding = &inReport->findings[itr];

		if (SWiAlreadySeen(inReport, itr)) continue;

		if (finding->isAbort) {
			outResult->numAborts++;
			printf("ABORT      level %d  %s  %s  %s\n",
				finding->level, finding->phase, finding->subject, finding->key);
			continue;
		}

		if (!SWiContains(inBaseline, finding)) {
			outResult->numRegressions++;
			printf("REGRESSION level %d  %s  %s  %s\n",
				finding->level, finding->phase, finding->subject, finding->key);
		}
	}

	for (itr = 0; itr < inBaseline->count; itr++) {
		const SWtFinding *finding = &inBaseline->findings[itr];

		if (SWiAlreadySeen(inBaseline, itr)) continue;

		if (!SWiContains(inReport, finding)) {
			outResult->numStale++;
			printf("STALE      level %d  %s  %s  %s\n",
				finding->level, finding->phase, finding->subject, finding->key);
		}
	}
}

int SWrExitCode(const SWtDiffResult *inResult)
{
	if (inResult->numAborts > 0 || inResult->numRegressions > 0) return 1;
	if (inResult->numStale > 0) return 2;
	return 0;
}

#ifndef SWEEP_DIFF_NO_MAIN

/*
	Extract a quoted string field, e.g. "phase":"particles" -> particles.

	The writer (Oni_Sweep_Report.c) emits the fields in a fixed order —
	renderer, level, phase, subject, severity, key, msg — and escapes every
	string body, so the first strstr hit for a needle is always the real field
	and never a lookalike buried in msg (a quote inside msg is written \", so
	msg can never contain the literal sequence "subject":").

	Backslash escapes are consumed but not decoded: \" and \\ come back as the
	right character, while \n / \t / \uXXXX come back as the letters n / t /
	uXXXX. Only phase, subject, key and severity are read here; keys are
	normalised slugs, severities are fixed literals, and phases are harness
	tags, so in practice none of them carry an escape at all. Subject is the
	one field an asset name could in principle put a quote in, and \" is
	handled exactly.
*/
static int SWiParseJsonString(const char *inLine, const char *inField, char *outValue)
{
	char		needle[SWcMaxFieldChars];
	const char	*at;
	int			len = 0;

	snprintf(needle, sizeof(needle), "\"%s\":\"", inField);
	at = strstr(inLine, needle);
	if (at == NULL) return -1;
	at += strlen(needle);

	while (*at != '\0' && *at != '"' && len < SWcMaxFieldChars - 1) {
		if (*at == '\\' && at[1] != '\0') at++;	/* drop the backslash, take the next byte as-is */
		outValue[len++] = *at++;
	}
	outValue[len] = '\0';
	return 0;
}

static int SWiParseJsonInt(const char *inLine, const char *inField, int *outValue)
{
	char		needle[SWcMaxFieldChars];
	const char	*at;

	snprintf(needle, sizeof(needle), "\"%s\":", inField);
	at = strstr(inLine, needle);
	if (at == NULL) return -1;
	*outValue = atoi(at + strlen(needle));
	return 0;
}

static int SWiLoadReport(const char *inPath, SWtFindingSet *outSet)
{
	FILE	*file;
	char	line[2048];

	file = fopen(inPath, "r");
	if (file == NULL) return -1;

	while (fgets(line, sizeof(line), file) != NULL) {
		char	phase[SWcMaxFieldChars];
		char	subject[SWcMaxFieldChars];
		char	key[SWcMaxFieldChars];
		char	severity[SWcMaxFieldChars];
		int		level = 0;

		phase[0] = '\0';
		subject[0] = '\0';
		key[0] = '\0';
		severity[0] = '\0';

		if (SWiParseJsonInt(line, "level", &level) != 0) continue;
		if (SWiParseJsonString(line, "phase", phase) != 0) continue;
		if (SWiParseJsonString(line, "subject", subject) != 0) continue;
		if (SWiParseJsonString(line, "key", key) != 0) continue;
		if (SWiParseJsonString(line, "severity", severity) != 0) continue;

		/* skipped and leak are reported by the sweep but never gate */
		if (strcmp(severity, "skipped") == 0) continue;
		if (strcmp(severity, "leak") == 0) continue;

		/*
			The return value is deliberately ignored: Add flags the set as
			overflowed, and main turns that into exit 3 rather than gating on
			a truncated report. Bailing out here would lose the same records
			with less to say about it.
		*/
		SWrFindingSet_Add(outSet, level, phase, subject, key,
			strcmp(severity, "abort") == 0 ? 1 : 0);
	}

	fclose(file);
	return 0;
}

static int SWiLoadBaseline(const char *inPath, SWtFindingSet *outSet)
{
	FILE	*file;
	char	line[2048];

	file = fopen(inPath, "r");
	if (file == NULL) return -1;

	while (fgets(line, sizeof(line), file) != NULL) {
		char	phase[SWcMaxFieldChars];
		char	subject[SWcMaxFieldChars];
		char	key[SWcMaxFieldChars];
		char	*comment;
		int		level = 0;

		/*
			Cleared before every line. The sscanf below only accepts a full
			four-field match, so these are never read unset today — but the
			stack slots here are the ones SWiLoadReport just finished using,
			so an unset buffer reads back as a plausible leftover value
			(observed: a short baseline line silently inheriting the last key
			parsed out of the NDJSON) rather than as obvious garbage. Clearing
			costs nothing and makes that class of mistake fail loudly.
		*/
		phase[0] = '\0';
		subject[0] = '\0';
		key[0] = '\0';

		comment = strchr(line, '#');
		if (comment != NULL) *comment = '\0';

		if (sscanf(line, "%d %127s %127s %127s", &level, phase, subject, key) != 4) continue;

		SWrFindingSet_Add(outSet, level, phase, subject, key, 0);
	}

	fclose(file);
	return 0;
}

int main(int argc, char **argv)
{
	static SWtFindingSet	report;
	static SWtFindingSet	baseline;
	SWtDiffResult			result;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <merged.ndjson> <baseline.txt>\n", argv[0]);
		return 3;
	}

	SWrFindingSet_Init(&report);
	SWrFindingSet_Init(&baseline);

	if (SWiLoadReport(argv[1], &report) != 0) {
		fprintf(stderr, "sweep_diff: cannot read report %s\n", argv[1]);
		return 3;
	}
	if (SWiLoadBaseline(argv[2], &baseline) != 0) {
		fprintf(stderr, "sweep_diff: cannot read baseline %s\n", argv[2]);
		return 3;
	}

	if (report.overflowed || baseline.overflowed) {
		fprintf(stderr, "sweep_diff: finding limit %d exceeded, results truncated\n",
			SWcMaxFindings);
		return 3;
	}

	SWrDiff(&report, &baseline, &result);

	printf("\n%d regression(s), %d stale, %d abort(s)\n",
		result.numRegressions, result.numStale, result.numAborts);

	return SWrExitCode(&result);
}

#endif /* SWEEP_DIFF_NO_MAIN */
