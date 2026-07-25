// ======================================================================
// Oni_Sweep_Normalize.c
//
// Pure libc-only implementation of the sweep message normaliser declared in
// Oni_Sweep_Normalize.h. No BFW / engine dependencies (and no <ctype.h> or
// <string.h>), so this file compiles standalone for the unit tests
// (tests/test_oni_sweep_normalize.c) and links into both the game binary and
// the sweep_diff CLI unchanged.
//
// Classification uses explicit ASCII range checks rather than the <ctype.h>
// predicates: those are locale-sensitive for bytes >= 0x80, which would make
// the key a function of a global this module does not own. A sweep_diff run
// from a shell with LANG set must produce byte-identical keys to the game that
// wrote the baseline, so bytes >= 0x80 always classify as separators and never
// reach the key.
// ======================================================================

#include "Oni_Sweep_Normalize.h"

/* Cap on key length, derived from the buffer size the header requires callers
   to use so the two cannot drift apart. */
#define ONcSweep_MaxKeyChars	(ONcSweep_KeyBufferSize - 1)

// --- locale-independent ASCII classification --------------------------

static int ONiSweep_IsDigit(char inChar)
{
	return (inChar >= '0' && inChar <= '9');
}

static int ONiSweep_IsHexDigit(char inChar)
{
	return ONiSweep_IsDigit(inChar) ||
		(inChar >= 'a' && inChar <= 'f') ||
		(inChar >= 'A' && inChar <= 'F');
}

static int ONiSweep_IsAlnum(char inChar)
{
	return ONiSweep_IsDigit(inChar) ||
		(inChar >= 'a' && inChar <= 'z') ||
		(inChar >= 'A' && inChar <= 'Z');
}

static char ONiSweep_ToLower(char inChar)
{
	if (inChar >= 'A' && inChar <= 'Z') {
		return (char)(inChar - 'A' + 'a');
	}
	return inChar;
}

// --- slug assembly -----------------------------------------------------

/* Append a single character to the slug, collapsing separator runs. */
static void ONiSweep_SlugPut(char *outKey, size_t outSize, size_t *ioLen, char inChar)
{
	char emit;

	if (ONiSweep_IsAlnum(inChar)) {
		emit = ONiSweep_ToLower(inChar);
	} else {
		/* never lead with a separator, never emit two in a row */
		if (*ioLen == 0) return;
		if (outKey[*ioLen - 1] == '-') return;
		emit = '-';
	}

	if (*ioLen + 1 >= outSize) return;
	if (*ioLen >= ONcSweep_MaxKeyChars) return;

	outKey[*ioLen] = emit;
	(*ioLen)++;
}

/* Append a placeholder as a standalone word: a separator, the placeholder
   letter ('p', 'n' or 's'), then a separator — e.g. "at-p-now". The bracketing
   separators go through ONiSweep_SlugPut, so a placeholder at the start or end
   of the key does not leave a stray '-'. */
static void ONiSweep_SlugPutToken(char *outKey, size_t outSize, size_t *ioLen, char inToken)
{
	ONiSweep_SlugPut(outKey, outSize, ioLen, ' ');
	ONiSweep_SlugPut(outKey, outSize, ioLen, inToken);
	ONiSweep_SlugPut(outKey, outSize, ioLen, ' ');
}

void ONrSweep_NormalizeKey(const char *inMessage, char *outKey, size_t outSize)
{
	size_t		len = 0;
	const char	*cursor;

	if (outKey == NULL || outSize == 0) return;
	outKey[0] = '\0';
	if (inMessage == NULL) return;

	cursor = inMessage;

	while (*cursor != '\0') {
		/* rule 1: hex pointer */
		if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X') &&
			ONiSweep_IsHexDigit(cursor[2])) {
			cursor += 2;
			while (ONiSweep_IsHexDigit(*cursor)) cursor++;
			ONiSweep_SlugPutToken(outKey, outSize, &len, 'p');
			continue;
		}

		/* rule 2: number, optional leading minus, optional decimal part */
		if (ONiSweep_IsDigit(*cursor) ||
			(*cursor == '-' && ONiSweep_IsDigit(cursor[1]))) {
			if (*cursor == '-') cursor++;
			while (ONiSweep_IsDigit(*cursor)) cursor++;
			if (*cursor == '.' && ONiSweep_IsDigit(cursor[1])) {
				cursor++;
				while (ONiSweep_IsDigit(*cursor)) cursor++;
			}
			ONiSweep_SlugPutToken(outKey, outSize, &len, 'n');
			continue;
		}

		/* rule 3: quoted string */
		if (*cursor == '\'' || *cursor == '"') {
			char quote = *cursor;
			cursor++;
			while (*cursor != '\0' && *cursor != quote) cursor++;
			if (*cursor == quote) cursor++;
			ONiSweep_SlugPutToken(outKey, outSize, &len, 's');
			continue;
		}

		/* rule 4: ordinary character */
		ONiSweep_SlugPut(outKey, outSize, &len, *cursor);
		cursor++;
	}

	/* rule 4 continued: strip any trailing separator */
	while (len > 0 && outKey[len - 1] == '-') len--;

	outKey[len] = '\0';
}
