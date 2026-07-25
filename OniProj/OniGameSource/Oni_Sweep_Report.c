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
	Length of the well-formed UTF-8 sequence starting at inText, or 0 if the
	bytes there are not one. Rejects what the Unicode well-formed-byte-sequence
	table rejects: continuation bytes in lead position, overlong encodings
	(0xC0/0xC1, and the 0xE0/0xF0 second-byte floors), UTF-16 surrogates
	(0xED 0xA0..0xBF) and anything above U+10FFFF (0xF4 second-byte ceiling,
	0xF5..0xFF).

	Safe against a truncated sequence at end of string: a NUL fails every
	continuation-byte range check, so the scan stops before reading past it.
*/
static size_t ONiSweep_Utf8SequenceLength(const char *inText)
{
	const unsigned char	*bytes = (const unsigned char *)inText;
	unsigned char		lead = bytes[0];
	unsigned char		secondLow;
	unsigned char		secondHigh;
	size_t				length;
	size_t				i;

	if (lead >= 0xC2 && lead <= 0xDF)		{ length = 2; secondLow = 0x80; secondHigh = 0xBF; }
	else if (lead == 0xE0)					{ length = 3; secondLow = 0xA0; secondHigh = 0xBF; }
	else if (lead >= 0xE1 && lead <= 0xEC)	{ length = 3; secondLow = 0x80; secondHigh = 0xBF; }
	else if (lead == 0xED)					{ length = 3; secondLow = 0x80; secondHigh = 0x9F; }
	else if (lead >= 0xEE && lead <= 0xEF)	{ length = 3; secondLow = 0x80; secondHigh = 0xBF; }
	else if (lead == 0xF0)					{ length = 4; secondLow = 0x90; secondHigh = 0xBF; }
	else if (lead >= 0xF1 && lead <= 0xF3)	{ length = 4; secondLow = 0x80; secondHigh = 0xBF; }
	else if (lead == 0xF4)					{ length = 4; secondLow = 0x80; secondHigh = 0x8F; }
	else return 0;

	if (bytes[1] < secondLow || bytes[1] > secondHigh) return 0;

	for (i = 2; i < length; i++) {
		if (bytes[i] < 0x80 || bytes[i] > 0xBF) return 0;
	}

	return length;
}

/*
	Escape inText as a JSON string body (without surrounding quotes) into
	outBuffer. Control characters below 0x20 are emitted as \uXXXX, matching
	what any NDJSON consumer expects.

	Truncation is piece-atomic, and a "piece" is a whole character, not a whole
	escape sequence: a multi-byte UTF-8 character is copied entire or not at
	all, exactly as \uXXXX is. Clipping either one corrupts the line — a
	dangling backslash, a short \uXXXX, or a lone lead byte that makes the
	record invalid UTF-8 and so invalid JSON.

	Bytes that are not part of a well-formed UTF-8 sequence are escaped as
	\u00XX, reading them as their Latin-1 code point. Engine messages are not
	guaranteed UTF-8 (this is a 2001 codebase), and passing a stray high byte
	through would emit invalid UTF-8 no matter how truncation behaved. Escaping
	keeps the output valid, keeps the original byte value legible to anyone
	reading the report, and is deterministic so the baseline does not churn.
*/
static void ONiSweep_JsonEscape(const char *inText, char *outBuffer, size_t inSize)
{
	const char	*cursor;
	size_t		len = 0;

	if (outBuffer == NULL || inSize == 0) return;
	outBuffer[0] = '\0';
	if (inText == NULL) return;

	cursor = inText;

	while (*cursor != '\0') {
		char		scratch[8];
		const char	*piece = scratch;
		size_t		pieceLen;
		size_t		consumed = 1;

		switch (*cursor) {
			case '"':	piece = "\\\"";	pieceLen = 2; break;
			case '\\':	piece = "\\\\";	pieceLen = 2; break;
			case '\n':	piece = "\\n";	pieceLen = 2; break;
			case '\r':	piece = "\\r";	pieceLen = 2; break;
			case '\t':	piece = "\\t";	pieceLen = 2; break;
			default:
				if ((unsigned char)*cursor < 0x20) {
					snprintf(scratch, sizeof(scratch), "\\u%04x",
						(unsigned int)(unsigned char)*cursor);
					pieceLen = strlen(scratch);
				} else if ((unsigned char)*cursor < 0x80) {
					scratch[0] = *cursor;
					pieceLen = 1;
				} else {
					size_t sequenceLen = ONiSweep_Utf8SequenceLength(cursor);

					if (sequenceLen > 0) {
						memcpy(scratch, cursor, sequenceLen);
						pieceLen = sequenceLen;
						consumed = sequenceLen;
					} else {
						snprintf(scratch, sizeof(scratch), "\\u%04x",
							(unsigned int)(unsigned char)*cursor);
						pieceLen = strlen(scratch);
					}
				}
				break;
		}

		if (len + pieceLen + 1 > inSize) break;
		memcpy(outBuffer + len, piece, pieceLen);
		len += pieceLen;
		cursor += consumed;
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
		The key buffer must be at least ONcSweep_KeyBufferSize, and using the
		constant is the way to guarantee that. An undersized buffer is the
		hazard: the normaliser caps a key at 96 characters, so it would cut the
		key short here while sweep_diff — a separate binary — produced the full
		one, and every long message would mismatch with nothing explaining why.
		(Any buffer of 97 or more is equivalent, since the cap binds first.)
	*/
	char key[ONcSweep_KeyBufferSize];
	char escapedMessage[512];
	char escapedSubject[128];
	char escapedRenderer[64];
	char escapedPhase[64];
	int  written;

	if (outLine == NULL || inLineSize == 0) return;
	outLine[0] = '\0';

	ONrSweep_NormalizeKey(inMessage, key, sizeof(key));
	ONiSweep_JsonEscape(inMessage, escapedMessage, sizeof(escapedMessage));
	ONiSweep_JsonEscape(inSubject, escapedSubject, sizeof(escapedSubject));
	ONiSweep_JsonEscape(inRenderer, escapedRenderer, sizeof(escapedRenderer));
	ONiSweep_JsonEscape(inPhase, escapedPhase, sizeof(escapedPhase));

	written = snprintf(outLine, inLineSize,
		"{\"renderer\":\"%s\",\"level\":%d,\"phase\":\"%s\","
		"\"subject\":\"%s\",\"severity\":\"%s\",\"key\":\"%s\",\"msg\":\"%s\"}",
		escapedRenderer,
		inLevel,
		escapedPhase,
		escapedSubject,
		ONrSweep_Report_SeverityName(inSeverity),
		key,
		escapedMessage);

	/*
		All or nothing. snprintf would otherwise leave a record cut mid-token,
		which every consumer of the merged report has to cope with; an empty
		outLine lets a mis-sized caller drop the one finding instead.
	*/
	if (written < 0 || (size_t)written >= inLineSize) {
		outLine[0] = '\0';
	}
}
