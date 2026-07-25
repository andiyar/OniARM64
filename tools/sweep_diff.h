#pragma once
#ifndef SWEEP_DIFF_H
#define SWEEP_DIFF_H

#define SWcMaxFindings		4096
#define SWcMaxFieldChars	128

typedef struct SWtFinding {
	int		level;
	char	phase[SWcMaxFieldChars];
	char	subject[SWcMaxFieldChars];
	char	key[SWcMaxFieldChars];
	int		isAbort;
} SWtFinding;

/*
	~1.5 MB. Declare instances static or heap-allocated, never as an
	ordinary local — two of these on the stack is 3.2 MB, which fits an
	8 MB main thread but not a default 512 KB pthread stack.
*/
typedef struct SWtFindingSet {
	SWtFinding	findings[SWcMaxFindings];
	int			count;
	int			overflowed;
} SWtFindingSet;

typedef struct SWtDiffResult {
	int	numRegressions;
	int	numStale;
	int	numAborts;
} SWtDiffResult;

void SWrFindingSet_Init(SWtFindingSet *ioSet);

/* Returns 0 on success, -1 if the set is full (sets ioSet->overflowed). */
int SWrFindingSet_Add(
	SWtFindingSet	*ioSet,
	int				inLevel,
	const char		*inPhase,
	const char		*inSubject,
	const char		*inKey,
	int				inIsAbort);

/*
	Compare report against baseline on identity (level, phase, subject, key).
	Aborts in the report are counted regardless of baseline membership.
	Prints each regression, stale entry and abort to stdout.

	Both sides are treated as sets: a repeated identity counts once. See the
	comment on SWiAlreadySeen in sweep_diff.c for why.
*/
void SWrDiff(
	const SWtFindingSet	*inReport,
	const SWtFindingSet	*inBaseline,
	SWtDiffResult		*outResult);

/* 0 clean, 1 regressions or aborts, 2 only stale entries. */
int SWrExitCode(const SWtDiffResult *inResult);

#endif /* SWEEP_DIFF_H */
