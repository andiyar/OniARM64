// ======================================================================
// test_oni_sweep_report.c
//
// Standalone unit tests for NDJSON emission. Compile and run directly:
//
//   cc -Wall -Wextra tests/test_oni_sweep_report.c \
//      OniProj/OniGameSource/Oni_Sweep_Report.c \
//      OniProj/OniGameSource/Oni_Sweep_Normalize.c \
//      -o /tmp/test_oni_sweep_report
//   /tmp/test_oni_sweep_report
//
// Beyond the three record-shape cases, the suite covers the failure this
// module exists to prevent: a single malformed line silently corrupting the
// merged report. That means escape correctness, well-formedness at every
// truncation alignment of the internal message/subject buffers, no write past
// a caller's outLine, and NULL tolerance on every pointer argument.
// ======================================================================
#include "../OniProj/OniGameSource/Oni_Sweep_Report.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *label, const char *actual, const char *expected)
{
	if (strcmp(actual, expected) == 0) {
		g_pass++;
	} else {
		g_fail++;
		printf("FAIL %s\n  expected: %s\n  actual:   %s\n", label, expected, actual);
	}
}

static void check_true(const char *label, int cond)
{
	if (cond) {
		g_pass++;
	} else {
		g_fail++;
		printf("FAIL %s\n", label);
	}
}

// --- minimal JSON well-formedness check --------------------------------
//
// Not a full parser — it checks exactly the properties a truncated line can
// lose: the object opens and closes, every string closes, and no escape
// sequence is cut in half (a trailing '\' or a short '\uXXXX'). A byte-wise
// clamp in the escaper instead of a piece-atomic one fails this.

static int is_hex(char inChar)
{
	return (inChar >= '0' && inChar <= '9') ||
		(inChar >= 'a' && inChar <= 'f') ||
		(inChar >= 'A' && inChar <= 'F');
}

static int json_wellformed(const char *inLine)
{
	size_t	len;
	size_t	i;
	int		inString = 0;

	if (inLine == NULL) return 0;
	len = strlen(inLine);
	if (len < 2) return 0;
	if (inLine[0] != '{' || inLine[len - 1] != '}') return 0;

	for (i = 1; i + 1 < len; i++) {
		char c = inLine[i];

		if (!inString) {
			if (c == '"') inString = 1;
			continue;
		}

		if (c == '"') {
			inString = 0;
			continue;
		}

		if (c == '\\') {
			char esc;
			if (i + 1 >= len) return 0;			/* dangling backslash */
			esc = inLine[++i];
			if (esc == 'u') {
				size_t d;
				if (i + 4 >= len) return 0;		/* short \uXXXX */
				for (d = 1; d <= 4; d++) {
					if (!is_hex(inLine[i + d])) return 0;
				}
				i += 4;
				continue;
			}
			if (strchr("\"\\/bfnrt", esc) == NULL) return 0;
			continue;
		}

		if ((unsigned char)c < 0x20) return 0;	/* raw control char in string */
	}

	return inString == 0;
}

// Build a string of inLen bytes cycling through characters with different
// escaped widths (1, 2 and 6). inPhase rotates the starting point of the
// cycle: without it every input escapes to the same widths in the same order
// and the buffer cap always falls in the same place, so a mid-escape clamp
// can hide. Sweeping length and phase together walks the cap across every
// alignment. (A phase-less version of this test passed a clamp mutation.)
static void fill_pattern(char *outBuffer, size_t inLen, size_t inPhase)
{
	static const char kCycle[] = { 'a', '"', '\\', 'b', '\n', 'c', '\001', 'd' };
	const size_t kCycleLen = sizeof(kCycle) / sizeof(kCycle[0]);
	size_t i;

	for (i = 0; i < inLen; i++) {
		outBuffer[i] = kCycle[(i + inPhase) % kCycleLen];
	}
	outBuffer[inLen] = '\0';
}

int main(void)
{
	char line[1024];

	// --- record shape ---------------------------------------------------

	ONrSweep_Report_FormatLine(line, sizeof(line),
		"gl", 2, "particles", "w10_sni_p01", ONcSweepSeverity_Warn,
		"Particle class 'w10_sni_p01' is too large (268) for largest size class (256)!");
	check("basic record", line,
		"{\"renderer\":\"gl\",\"level\":2,\"phase\":\"particles\","
		"\"subject\":\"w10_sni_p01\",\"severity\":\"warn\","
		"\"key\":\"particle-class-s-is-too-large-n-for-largest-size-class-n\","
		"\"msg\":\"Particle class 'w10_sni_p01' is too large (268) for largest size class (256)!\"}");

	ONrSweep_Report_FormatLine(line, sizeof(line),
		"metal", 0, "load", "level0", ONcSweepSeverity_Error,
		"could not open \"foo\\bar\"");
	check("escaping", line,
		"{\"renderer\":\"metal\",\"level\":0,\"phase\":\"load\","
		"\"subject\":\"level0\",\"severity\":\"error\","
		"\"key\":\"could-not-open-s\","
		"\"msg\":\"could not open \\\"foo\\\\bar\\\"\"}");

	ONrSweep_Report_FormatLine(line, sizeof(line),
		"gl", 4, "scripts", "spawn_guards", ONcSweepSeverity_Skipped,
		"arity 3, unsupported formal type");
	check("skipped severity", line,
		"{\"renderer\":\"gl\",\"level\":4,\"phase\":\"scripts\","
		"\"subject\":\"spawn_guards\",\"severity\":\"skipped\","
		"\"key\":\"arity-n-unsupported-formal-type\","
		"\"msg\":\"arity 3, unsupported formal type\"}");

	// --- severity names ---------------------------------------------------

	check("severity abort",   ONrSweep_Report_SeverityName(ONcSweepSeverity_Abort),   "abort");
	check("severity error",   ONrSweep_Report_SeverityName(ONcSweepSeverity_Error),   "error");
	check("severity warn",    ONrSweep_Report_SeverityName(ONcSweepSeverity_Warn),    "warn");
	check("severity skipped", ONrSweep_Report_SeverityName(ONcSweepSeverity_Skipped), "skipped");
	check("severity leak",    ONrSweep_Report_SeverityName(ONcSweepSeverity_Leak),    "leak");
	check_true("out-of-range severity still returns a name",
		ONrSweep_Report_SeverityName((ONtSweepSeverity)99) != NULL);

	// --- key buffer size ---------------------------------------------------
	//
	// The key field must be derived with a buffer of ONcSweep_KeyBufferSize.
	// A message whose key hits the 96-character cap is the only input that
	// tells a correctly sized buffer apart from an undersized one, so pin the
	// full capped key here — the game and sweep_diff must agree on it.
	{
		static const char kLongMessage[] =
			"Particle class 'x' failed to load because the geometry stream referenced "
			"an instance that does not exist in this level at all";
		static const char kLongKeyField[] =
			"\"key\":\"particle-class-s-failed-to-load-because-the-geometry-stream-"
			"referenced-an-instance-that-does-not\"";

		ONrSweep_Report_FormatLine(line, sizeof(line),
			"gl", 6, "particles", "x", ONcSweepSeverity_Warn, kLongMessage);
		check_true("capped key emitted in full", strstr(line, kLongKeyField) != NULL);
		if (strstr(line, kLongKeyField) == NULL) printf("  line: %s\n", line);
	}

	// --- control characters ----------------------------------------------

	ONrSweep_Report_FormatLine(line, sizeof(line),
		"gl", 1, "load", "sub", ONcSweepSeverity_Error,
		"tab\there\nnewline\rcr\001soh\037us");
	check("control characters escaped", line,
		"{\"renderer\":\"gl\",\"level\":1,\"phase\":\"load\","
		"\"subject\":\"sub\",\"severity\":\"error\","
		"\"key\":\"tab-here-newline-cr-soh-us\","
		"\"msg\":\"tab\\there\\nnewline\\rcr\\u0001soh\\u001fus\"}");
	check_true("control-character line is well formed", json_wellformed(line));

	// --- truncation of the internal message / subject buffers -------------
	//
	// The escaper works into a 512-byte message buffer and a 128-byte
	// subject buffer, so oversized fields get cut short. A
	// clamp that cuts mid-escape would emit a dangling '\' or a short \uXXXX
	// — corrupt NDJSON. Sweep every length so truncation lands on every
	// alignment against those caps.
	{
		char	subject[512];
		char	message[2048];
		char	wide[4096];
		size_t	n;
		int		badSubject = -1;
		int		badMessage = -1;

		for (n = 1; n < 400; n++) {
			size_t phase;
			for (phase = 0; phase < 8; phase++) {
				fill_pattern(subject, n, phase);
				ONrSweep_Report_FormatLine(wide, sizeof(wide),
					"gl", 3, "characters", subject, ONcSweepSeverity_Warn, "short message");
				if (!json_wellformed(wide) && badSubject < 0) badSubject = (int)n;
			}
		}
		check_true("well formed at every subject length 1..399", badSubject < 0);
		if (badSubject >= 0) printf("  first bad subject length: %d\n", badSubject);

		for (n = 1; n < 1500; n++) {
			size_t phase;
			for (phase = 0; phase < 8; phase++) {
				fill_pattern(message, n, phase);
				ONrSweep_Report_FormatLine(wide, sizeof(wide),
					"gl", 3, "characters", "subj", ONcSweepSeverity_Warn, message);
				if (!json_wellformed(wide) && badMessage < 0) badMessage = (int)n;
			}
		}
		check_true("well formed at every message length 1..1499", badMessage < 0);
		if (badMessage >= 0) printf("  first bad message length: %d\n", badMessage);

		// Worst case on every field at once, into the 1024-byte buffer the
		// header tells callers to use. Must close cleanly and must not be
		// clipped — that promise is what stops a later caller from picking a
		// buffer that silently drops the closing brace.
		{
			char longRenderer[256];
			char longPhase[256];

			fill_pattern(subject, 400, 0);
			fill_pattern(message, 2000, 0);
			fill_pattern(longRenderer, 200, 3);
			fill_pattern(longPhase, 200, 5);

			ONrSweep_Report_FormatLine(line, sizeof(line),
				longRenderer, -2147483647 - 1, longPhase, subject,
				ONcSweepSeverity_Skipped, message);
			check_true("worst-case record stays well formed", json_wellformed(line));
			check_true("worst-case record fits a 1024-byte line buffer",
				strlen(line) < sizeof(line) - 1);
		}
	}

	// --- exact truncation boundary ----------------------------------------
	//
	// How much of an oversized field survives is a contract, not an accident:
	// the escaper keeps inSize-1 bytes and reserves the last for the NUL.
	// Pinning the lengths for all-'a' input (one output byte per input byte)
	// catches an off-by-one in that bound, which is otherwise invisible
	// without a sanitizer because it only shows up as a one-byte overrun of
	// an internal buffer.
	{
		char		plain[2048];
		const char	*field;
		size_t		i;

		for (i = 0; i < 2000; i++) plain[i] = 'a';
		plain[2000] = '\0';

		ONrSweep_Report_FormatLine(line, sizeof(line),
			"gl", 7, "load", plain, ONcSweepSeverity_Warn, plain);

		field = strstr(line, "\"subject\":\"");
		check_true("subject field found", field != NULL);
		if (field != NULL) {
			const char *end = strstr(field, "\",\"severity\"");
			check_true("oversized subject keeps exactly 127 bytes",
				end != NULL && (size_t)(end - (field + 11)) == 127);
		}

		field = strstr(line, "\"msg\":\"");
		check_true("msg field found", field != NULL);
		if (field != NULL) {
			check_true("oversized message keeps exactly 511 bytes",
				strlen(field + 7) == 511 + 2);	/* + closing quote and brace */
		}
	}

	// --- caller buffer safety ---------------------------------------------
	{
		char	guarded[256];
		size_t	i;
		int		clobbered = 0;

		memset(guarded, (int)0xAA, sizeof(guarded));
		ONrSweep_Report_FormatLine(guarded, 40,
			"gl", 2, "particles", "w10_sni_p01", ONcSweepSeverity_Warn,
			"Particle class 'w10_sni_p01' is too large (268) for largest size class (256)!");

		for (i = 40; i < sizeof(guarded); i++) {
			if (guarded[i] != (char)0xAA) clobbered = 1;
		}
		check_true("small outLine does not write past inLineSize", !clobbered);
		check_true("small outLine stays NUL-terminated in range", strlen(guarded) < 40);
		check_true("small outLine still starts the record", guarded[0] == '{');

		memset(guarded, (int)0xAA, sizeof(guarded));
		ONrSweep_Report_FormatLine(guarded, 0,
			"gl", 2, "particles", "subj", ONcSweepSeverity_Warn, "msg");
		check_true("zero inLineSize writes nothing", guarded[0] == (char)0xAA);
	}

	// --- NULL tolerance ----------------------------------------------------

	ONrSweep_Report_FormatLine(line, sizeof(line),
		"gl", 5, "scripts", "subj", ONcSweepSeverity_Leak, NULL);
	check("NULL message", line,
		"{\"renderer\":\"gl\",\"level\":5,\"phase\":\"scripts\","
		"\"subject\":\"subj\",\"severity\":\"leak\",\"key\":\"\",\"msg\":\"\"}");

	ONrSweep_Report_FormatLine(line, sizeof(line),
		NULL, -1, NULL, NULL, ONcSweepSeverity_Abort, "boom");
	check("NULL renderer, phase and subject", line,
		"{\"renderer\":\"\",\"level\":-1,\"phase\":\"\","
		"\"subject\":\"\",\"severity\":\"abort\",\"key\":\"boom\",\"msg\":\"boom\"}");

	/* must not crash */
	ONrSweep_Report_FormatLine(NULL, 1024,
		"gl", 0, "load", "subj", ONcSweepSeverity_Error, "msg");
	check_true("NULL outLine survives", 1);

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
