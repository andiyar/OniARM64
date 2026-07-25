// ======================================================================
// Oni_Sweep_Report.c
//
// Pure libc-only implementation of the NDJSON record writer declared in
// Oni_Sweep_Report.h. No BFW / engine dependencies, so this file compiles
// standalone for the unit tests (tests/test_oni_sweep_report.c) and links
// into both the game binary and the sweep_diff CLI unchanged.
//
// Every string that reaches the output goes through ONiSweep_JsonEscape,
// including the renderer and phase tags: they are harness-controlled today,
// but one unescaped byte anywhere on the line breaks the record for every
// consumer, and the cost of routing them through the same escaper is nil.
// ======================================================================

#include "Oni_Sweep_Report.h"
#include "Oni_Sweep_Normalize.h"

#include <stdio.h>
#include <string.h>

const char *ONrSweep_Report_SeverityName(ONtSweepSeverity inSeverity)
{
	switch (inSeverity) {
		case ONcSweepSeverity_Abort:	return "abort";
		case ONcSweepSeverity_Error:	return "error";
		case ONcSweepSeverity_Warn:		return "warn";
		case ONcSweepSeverity_Skipped:	return "skipped";
		case ONcSweepSeverity_Leak:		return "leak";
	}
	return "error";
}

/*
	Escape inText as a JSON string body (without surrounding quotes) into
	outBuffer. Control characters below 0x20 are emitted as \uXXXX, matching
	what any NDJSON consumer expects.

	Truncation is piece-atomic: an escape sequence that does not fit whole is
	dropped rather than clipped. Clipping would leave a dangling backslash or a
	short \uXXXX, i.e. a corrupt line — the exact failure this module exists to
	prevent.
*/
static void ONiSweep_JsonEscape(const char *inText, char *outBuffer, size_t inSize)
{
	size_t len = 0;

	if (outBuffer == NULL || inSize == 0) return;
	outBuffer[0] = '\0';
	if (inText == NULL) return;

	for (; *inText != '\0'; inText++) {
		char		scratch[8];
		const char	*piece = scratch;
		size_t		pieceLen;

		switch (*inText) {
			case '"':	piece = "\\\"";	pieceLen = 2; break;
			case '\\':	piece = "\\\\";	pieceLen = 2; break;
			case '\n':	piece = "\\n";	pieceLen = 2; break;
			case '\r':	piece = "\\r";	pieceLen = 2; break;
			case '\t':	piece = "\\t";	pieceLen = 2; break;
			default:
				if ((unsigned char)*inText < 0x20) {
					snprintf(scratch, sizeof(scratch), "\\u%04x",
						(unsigned int)(unsigned char)*inText);
					pieceLen = strlen(scratch);
				} else {
					scratch[0] = *inText;
					pieceLen = 1;
				}
				break;
		}

		if (len + pieceLen + 1 > inSize) break;
		memcpy(outBuffer + len, piece, pieceLen);
		len += pieceLen;
	}

	outBuffer[len] = '\0';
}

void ONrSweep_Report_FormatLine(
	char				*outLine,
	size_t				inLineSize,
	const char			*inRenderer,
	int					inLevel,
	const char			*inPhase,
	const char			*inSubject,
	ONtSweepSeverity	inSeverity,
	const char			*inMessage)
{
	/*
		ONcSweep_KeyBufferSize is mandatory here. The generated key depends on
		the buffer size, and sweep_diff is a separate binary — if the two call
		sites disagree on the size, every long message mismatches and the gate
		reports the entire baseline as churned with nothing explaining why.
	*/
	char key[ONcSweep_KeyBufferSize];
	char escapedMessage[512];
	char escapedSubject[128];
	char escapedRenderer[64];
	char escapedPhase[64];

	if (outLine == NULL || inLineSize == 0) return;
	outLine[0] = '\0';

	ONrSweep_NormalizeKey(inMessage, key, sizeof(key));
	ONiSweep_JsonEscape(inMessage, escapedMessage, sizeof(escapedMessage));
	ONiSweep_JsonEscape(inSubject, escapedSubject, sizeof(escapedSubject));
	ONiSweep_JsonEscape(inRenderer, escapedRenderer, sizeof(escapedRenderer));
	ONiSweep_JsonEscape(inPhase, escapedPhase, sizeof(escapedPhase));

	snprintf(outLine, inLineSize,
		"{\"renderer\":\"%s\",\"level\":%d,\"phase\":\"%s\","
		"\"subject\":\"%s\",\"severity\":\"%s\",\"key\":\"%s\",\"msg\":\"%s\"}",
		escapedRenderer,
		inLevel,
		escapedPhase,
		escapedSubject,
		ONrSweep_Report_SeverityName(inSeverity),
		key,
		escapedMessage);
}
