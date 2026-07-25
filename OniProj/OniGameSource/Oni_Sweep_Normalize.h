// ======================================================================
// Oni_Sweep_Normalize.h
//
// Pure, dependency-free (libc-only) message normalisation for the level
// sweep harness. Deliberately includes no BFW / engine headers so that both
// consumers link it unchanged — the game binary (Oni_Sweep_Report.c) and the
// standalone sweep_diff CLI — and so it is unit-testable with a plain cc line
// (see ../../tests/test_oni_sweep_normalize.c).
//
// Raw engine messages embed run-varying data: pointers, tick counts, class
// names, float coordinates. Normalisation replaces that content with
// placeholders and slugifies what is left, so the same finding produces the
// same key on every run and the committed baseline does not churn.
// ======================================================================
#pragma once
#ifndef ONI_SWEEP_NORMALIZE_H
#define ONI_SWEEP_NORMALIZE_H

#include <stddef.h>

/*
	Both consumers (the game binary and the standalone sweep_diff tool) MUST
	declare their key buffer at exactly this size. The generated key depends on
	the buffer size, so two call sites using different sizes would silently
	produce mismatched keys for the same message.
*/
#define ONcSweep_KeyBufferSize	97

/*
	Normalise a raw engine message into a stable key suitable for baseline
	comparison. Strips pointers, numbers and quoted strings, then slugifies.
	Always NUL-terminates outKey. outSize must be >= 1, and should be exactly
	ONcSweep_KeyBufferSize — see the note on that constant.
*/
void ONrSweep_NormalizeKey(const char *inMessage, char *outKey, size_t outSize);

#endif /* ONI_SWEEP_NORMALIZE_H */
