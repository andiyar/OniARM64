#pragma once
#ifndef ONI_SWEEP_NORMALIZE_H
#define ONI_SWEEP_NORMALIZE_H

/*
	Pure, libc-only. Deliberately includes no engine headers so that both the
	game and the standalone sweep_diff tool can link it, and so it can be unit
	tested with a plain cc line.
*/

#include <stddef.h>

/*
	Normalise a raw engine message into a stable key suitable for baseline
	comparison. Strips pointers, numbers and quoted strings, then slugifies.
	Always NUL-terminates outKey. outSize must be >= 1.
*/
void ONrSweep_NormalizeKey(const char *inMessage, char *outKey, size_t outSize);

#endif /* ONI_SWEEP_NORMALIZE_H */
