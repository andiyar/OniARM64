#include "Oni_Sweep_Normalize.h"

#include <ctype.h>
#include <string.h>

#define ONcSweep_MaxKeyChars	96

/* Append a single character to the slug, collapsing separator runs. */
static void ONiSweep_SlugPut(char *outKey, size_t outSize, size_t *ioLen, char inChar)
{
	char emit;

	if (isalnum((unsigned char)inChar)) {
		emit = (char)tolower((unsigned char)inChar);
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

/* Append a placeholder token such as "<N>" as the letter itself, e.g. "-n-". */
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
			isxdigit((unsigned char)cursor[2])) {
			cursor += 2;
			while (isxdigit((unsigned char)*cursor)) cursor++;
			ONiSweep_SlugPutToken(outKey, outSize, &len, 'p');
			continue;
		}

		/* rule 2: number, optional leading minus, optional decimal part */
		if (isdigit((unsigned char)*cursor) ||
			(*cursor == '-' && isdigit((unsigned char)cursor[1]))) {
			if (*cursor == '-') cursor++;
			while (isdigit((unsigned char)*cursor)) cursor++;
			if (*cursor == '.' && isdigit((unsigned char)cursor[1])) {
				cursor++;
				while (isdigit((unsigned char)*cursor)) cursor++;
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
