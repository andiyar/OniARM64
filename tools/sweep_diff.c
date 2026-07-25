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

/*
	How many records share this identity. The count does not gate — it is
	printed as an "x N" suffix so triage keeps the volume information that
	set semantics would otherwise throw away (one class warning twice reads
	very differently from one warning 400 times).
*/
static int SWiCountIdentity(const SWtFindingSet *inSet, int inIndex)
{
	int itr;
	int seen = 0;
	for (itr = 0; itr < inSet->count; itr++) {
		if (inSet->findings[itr].isAbort == inSet->findings[inIndex].isAbort &&
			SWiSameIdentity(&inSet->findings[itr], &inSet->findings[inIndex])) {
			seen++;
		}
	}
	return seen;
}

static void SWiPrintFinding(const char *inLabel, const SWtFindingSet *inSet, int inIndex)
{
	const SWtFinding	*finding = &inSet->findings[inIndex];
	int					seen = SWiCountIdentity(inSet, inIndex);

	printf("%s level %d  %s  %s  %s", inLabel,
		finding->level, finding->phase, finding->subject, finding->key);
	if (seen > 1) printf("  x%d", seen);
	printf("\n");
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
			SWiPrintFinding("ABORT     ", inReport, itr);
			continue;
		}

		if (!SWiContains(inBaseline, finding)) {
			outResult->numRegressions++;
			SWiPrintFinding("REGRESSION", inReport, itr);
		}
	}

	for (itr = 0; itr < inBaseline->count; itr++) {
		const SWtFinding *finding = &inBaseline->findings[itr];

		if (SWiAlreadySeen(inBaseline, itr)) continue;

		if (!SWiContains(inReport, finding)) {
			outResult->numStale++;
			SWiPrintFinding("STALE     ", inBaseline, itr);
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
	size_t		len = 0;

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

/*
	Extract an integer field. atoi was wrong here: it reports no error, so a
	corrupt or missing value read back as 0 — and 0 is a real level (the main
	menu), so a mangled record would silently gate against level 0's baseline
	instead of being rejected.
*/
static int SWiParseJsonInt(const char *inLine, const char *inField, int *outValue)
{
	char		needle[SWcMaxFieldChars];
	const char	*at;
	char		*end;
	long		value;

	snprintf(needle, sizeof(needle), "\"%s\":", inField);
	at = strstr(inLine, needle);
	if (at == NULL) return -1;
	at += strlen(needle);

	value = strtol(at, &end, 10);
	if (end == at) return -1;					/* no digits at all */
	if (value < 0 || value > 0x7fff) return -1;	/* not a plausible level */

	*outValue = (int)value;
	return 0;
}

/* True if the line is entirely whitespace (or empty). */
static int SWiIsBlank(const char *inLine)
{
	for (; *inLine != '\0'; inLine++) {
		if (*inLine != ' ' && *inLine != '\t' && *inLine != '\r' && *inLine != '\n') {
			return 0;
		}
	}
	return 1;
}

/*
	Read one LOGICAL line, discarding any part that does not fit the buffer.

	fgets alone would hand the overflow back as if it were the next line, and
	that tail parses as garbage — or worse, silently vanishes. Draining keeps
	one input line equal to one record, which is what the strict parsing below
	depends on.

	The buffer only has to cover each record's prefix through the closing quote
	of "key", because "msg" is written last and nothing after it is read. Worst
	case that prefix is about 440 bytes (renderer 63 + phase 63 + subject 127 +
	key 96 + severity 7 + level 11 + ~70 of punctuation), so 2048 leaves roughly
	4x headroom and a long msg is simply truncated away harmlessly. Shrink the
	buffer below ~450 and records start losing their key instead.

	Returns 1 when a line was read, 0 at end of file.
*/
static int SWiReadLine(FILE *inFile, char *outLine, int inSize)
{
	size_t len;

	if (fgets(outLine, inSize, inFile) == NULL) return 0;

	len = strlen(outLine);
	if (len > 0 && outLine[len - 1] == '\n') return 1;

	/* No newline: end of file, or a line longer than the buffer. */
	{
		int ch;
		while ((ch = fgetc(inFile)) != EOF && ch != '\n') { /* discard */ }
	}
	return 1;
}

/*
	Load the merged NDJSON report.

	Every non-blank line must be a well-formed record, and the file must hold
	at least one. A gate that reads nothing must never answer "clean": an empty
	or corrupt merged report is what a runner that died early produces, and
	waving that through is the worst thing this tool could do. fopen alone does
	not protect against it — it succeeds on a directory, which then reads as
	zero lines.

	Strict per-line parsing rather than skip-what-you-cannot-read, because this
	input is machine-written. A line the reader cannot parse means the writer,
	the redirect or the merge step produced something unexpected, and the most
	likely cause is a process cut off mid-write — precisely the run whose
	verdict cannot be trusted. Silently skipping such a line converts a crashed
	sweep into a passing one.

	Returns 0 on success, -1 if the file cannot be read (message left to the
	caller), -2 if its contents are unusable (message already printed).
*/
static int SWiLoadReport(const char *inPath, SWtFindingSet *outSet)
{
	FILE	*file;
	char	line[2048];
	int		lineNumber = 0;
	int		records = 0;

	file = fopen(inPath, "r");
	if (file == NULL) return -1;

	while (SWiReadLine(file, line, (int)sizeof(line))) {
		char	phase[SWcMaxFieldChars];
		char	subject[SWcMaxFieldChars];
		char	key[SWcMaxFieldChars];
		char	severity[SWcMaxFieldChars];
		int		level = 0;

		lineNumber++;

		phase[0] = '\0';
		subject[0] = '\0';
		key[0] = '\0';
		severity[0] = '\0';

		if (SWiIsBlank(line)) continue;

		if (SWiParseJsonInt(line, "level", &level) != 0 ||
			SWiParseJsonString(line, "phase", phase) != 0 ||
			SWiParseJsonString(line, "subject", subject) != 0 ||
			SWiParseJsonString(line, "key", key) != 0 ||
			SWiParseJsonString(line, "severity", severity) != 0) {
			fprintf(stderr, "sweep_diff: %s:%d is not a sweep record\n", inPath, lineNumber);
			fclose(file);
			return -2;
		}

		records++;

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

	if (ferror(file)) {
		fclose(file);
		return -1;
	}
	fclose(file);

	if (records == 0) {
		fprintf(stderr, "sweep_diff: %s holds no sweep records — refusing to "
			"report a run with no evidence as clean\n", inPath);
		return -2;
	}

	return 0;
}

/*
	Load the per-renderer baseline.

	Blank lines and comments are content-free by design, but a line that still
	has text after the comment is stripped and does not yield four fields is a
	typo in a hand-edited file. Skipping it silently is the worst option: the
	maintainer believes they accepted a finding, the gate disagrees, and the
	result is a regression nobody can explain. Say so and stop.

	An empty baseline is legitimate — it is the starting state, nothing
	accepted yet — so unlike the report there is no minimum record count.

	Same return codes as SWiLoadReport.
*/
static int SWiLoadBaseline(const char *inPath, SWtFindingSet *outSet)
{
	FILE	*file;
	char	line[2048];
	int		lineNumber = 0;

	file = fopen(inPath, "r");
	if (file == NULL) return -1;

	while (SWiReadLine(file, line, (int)sizeof(line))) {
		char	phase[SWcMaxFieldChars];
		char	subject[SWcMaxFieldChars];
		char	key[SWcMaxFieldChars];
		char	*comment;
		int		level = 0;

		lineNumber++;

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

		if (SWiIsBlank(line)) continue;

		if (sscanf(line, "%d %127s %127s %127s", &level, phase, subject, key) != 4) {
			fprintf(stderr, "sweep_diff: %s:%d is not a baseline entry "
				"(want: <level> <phase> <subject> <key>)\n", inPath, lineNumber);
			fclose(file);
			return -2;
		}

		SWrFindingSet_Add(outSet, level, phase, subject, key, 0);
	}

	if (ferror(file)) {
		fclose(file);
		return -1;
	}
	fclose(file);
	return 0;
}

int main(int argc, char **argv)
{
	static SWtFindingSet	report;
	static SWtFindingSet	baseline;
	SWtDiffResult			result;
	int						status;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <merged.ndjson> <baseline.txt>\n", argv[0]);
		return 3;
	}

	SWrFindingSet_Init(&report);
	SWrFindingSet_Init(&baseline);

	/* -1 means the file could not be read; -2 already explained itself. */
	status = SWiLoadReport(argv[1], &report);
	if (status == -1) fprintf(stderr, "sweep_diff: cannot read report %s\n", argv[1]);
	if (status != 0) return 3;

	status = SWiLoadBaseline(argv[2], &baseline);
	if (status == -1) fprintf(stderr, "sweep_diff: cannot read baseline %s\n", argv[2]);
	if (status != 0) return 3;

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
