// ======================================================================
// Oni_Sweep_Report.h
//
// NDJSON emission for the level sweep harness: one self-contained JSON
// record per finding, one finding per line. Like Oni_Sweep_Normalize it is
// pure and libc-only — no BFW / engine headers — because the game binary and
// the standalone sweep_diff CLI both link it, and it must stay unit-testable
// with a plain cc line (see ../../tests/test_oni_sweep_report.c).
//
// Engine messages routinely carry quotes, backslashes and the odd control
// byte. A single line that escapes those wrongly, or that gets cut mid-escape
// by a buffer limit, silently corrupts the whole merged report — so escaping
// and truncation are the load-bearing behaviour here.
// ======================================================================
#pragma once
#ifndef ONI_SWEEP_REPORT_H
#define ONI_SWEEP_REPORT_H

/* Pure, libc-only. No engine headers. */

#include <stddef.h>

typedef enum ONtSweepSeverity {
	ONcSweepSeverity_Abort   = 0,
	ONcSweepSeverity_Error   = 1,
	ONcSweepSeverity_Warn    = 2,
	ONcSweepSeverity_Skipped = 3,
	ONcSweepSeverity_Leak    = 4
} ONtSweepSeverity;

/* Stable lowercase name for a severity, e.g. "warn". Never NULL. */
const char *ONrSweep_Report_SeverityName(ONtSweepSeverity inSeverity);

/*
	Format one NDJSON record (no trailing newline) into outLine. The key field
	is derived from inMessage via ONrSweep_NormalizeKey. Always NUL-terminates.

	Oversized fields are truncated internally, never cut mid-escape and never
	mid-character, so the record stays parseable whatever comes in. Output is
	always valid UTF-8: bytes that are not part of a well-formed sequence are
	escaped as \u00XX rather than passed through.

	If the whole record will not fit in inLineSize, outLine is set to the empty
	string rather than a truncated fragment, so a mis-sized caller drops the
	finding instead of writing a corrupt line. A record cannot exceed 958 bytes
	plus the NUL, so a 1024-byte buffer always holds one.
*/
void ONrSweep_Report_FormatLine(
	char				*outLine,
	size_t				inLineSize,
	const char			*inRenderer,
	int					inLevel,
	const char			*inPhase,
	const char			*inSubject,
	ONtSweepSeverity	inSeverity,
	const char			*inMessage);

#endif /* ONI_SWEEP_REPORT_H */
