// ======================================================================
// Oni_Sweep.c
//
// Implementation of the sweep core declared in Oni_Sweep.h.
//
// Two details here are load-bearing and easy to undo by accident:
//
//   * every record is fflushed. A sweep exists to find crashes, so the
//     findings that led up to one must already be on disk when the process
//     dies. This is affordable because ONrSweep_Record is driven by warnings
//     and phase transitions, not by anything per-tick.
//   * the warning tap is registered only between ONrSweep_Begin and
//     ONrSweep_End, and ONrSweep_Record no-ops whenever the file handle is
//     NULL. Warnings raised during engine init or teardown therefore take the
//     stock path, including the modal, exactly as in normal play.
// ======================================================================

#include <stdio.h>
#include <string.h>

#include "BFW.h"

#include "Oni_Sweep.h"
#include "Oni_Sweep_Report.h"
#include "Oni_GameState.h"

// === constants ========================================================

/* ONrSweep_Report_FormatLine never emits more than 958 bytes plus NUL. */
#define ONcSweep_LineBufferSize		1024

#define ONcSweep_RendererSize		32
#define ONcSweep_PhaseSize			64
#define ONcSweep_SubjectSize		128

// === state ============================================================

UUtBool			ONgSweep_Active = UUcFalse;

static FILE		*ONgSweep_File = NULL;
static char		ONgSweep_Renderer[ONcSweep_RendererSize] = "";
static int		ONgSweep_Level = 0;
static char		ONgSweep_Phase[ONcSweep_PhaseSize] = "";
static char		ONgSweep_Subject[ONcSweep_SubjectSize] = "";

/* Guards against a warning raised from inside the record path re-entering it.
   Nothing in ONrSweep_Record warns today; the flag keeps that from becoming a
   recursion the moment something in the write path starts to. */
static UUtBool	ONgSweep_InTap = UUcFalse;

// === helpers ==========================================================

/*
	Bounded copy, truncating. Deliberately not UUrString_Copy: that asserts
	strlen(inSrc) < inDstLength, and in a DEBUGGING build the assert traps via
	SDL_TriggerBreakpoint. An over-long subject name is not a reason to take
	down a run whose whole job is to survive bad content and write it down.
*/
static void ONiSweep_CopyField(char *outField, UUtUns32 inFieldSize, const char *inText)
{
	UUtUns32 itr;

	if (inFieldSize == 0) {
		return;
	}

	if (inText == NULL) {
		outField[0] = '\0';
		return;
	}

	for (itr = 0; (itr < inFieldSize - 1) && (inText[itr] != '\0'); itr++) {
		outField[itr] = inText[itr];
	}

	outField[itr] = '\0';
}

// === recording ========================================================

void ONrSweep_SetContext(const char *inPhase, const char *inSubject)
{
	ONiSweep_CopyField(ONgSweep_Phase, ONcSweep_PhaseSize, inPhase);
	ONiSweep_CopyField(ONgSweep_Subject, ONcSweep_SubjectSize, inSubject);
}

void ONrSweep_Record(
	const char			*inPhase,
	const char			*inSubject,
	ONtSweepSeverity	inSeverity,
	const char			*inMessage)
{
	char		line[ONcSweep_LineBufferSize];
	const char	*phase;
	const char	*subject;

	/* Not begun, or already ended. Either way there is nowhere to write. */
	if (ONgSweep_File == NULL) {
		return;
	}

	phase = (inPhase != NULL) ? inPhase : ONgSweep_Phase;
	subject = (inSubject != NULL) ? inSubject : ONgSweep_Subject;

	ONrSweep_Report_FormatLine(line, sizeof(line), ONgSweep_Renderer,
		ONgSweep_Level, phase, subject, inSeverity, inMessage);

	/* FormatLine empties the buffer rather than emit a truncated fragment. */
	if (line[0] == '\0') {
		return;
	}

	fprintf(ONgSweep_File, "%s\n", line);

	/* Every line, so a crash preserves the findings that preceded it. */
	fflush(ONgSweep_File);
}

// === warning tap ======================================================

/*
	Registered with BFW for the lifetime of the sweep. Returning UUcTrue tells
	UUrPrintWarning the warning has been dealt with and the AUrMessageBox call
	must be skipped — that modal is the single failure that would hang an
	unattended run forever.
*/
static UUtBool ONiSweep_WarningTap(const char *inMessage)
{
	if (!ONgSweep_Active) {
		return UUcFalse;
	}

	if (ONgSweep_InTap) {
		/* Consume it anyway: still better than a dialog, and it cannot recurse. */
		return UUcTrue;
	}

	ONgSweep_InTap = UUcTrue;
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Warn, inMessage);
	ONgSweep_InTap = UUcFalse;

	return UUcTrue;
}

// === determinism ======================================================

void ONrSweep_SeedRandom(void)
{
	/* Two separate globals with two separate call sites in the engine:
	   UUrRandom drives synchronised gameplay, UUrLocalRandom drives particles
	   and other unsynchronised effects. Seeding one leaves the other free. */
	UUrRandom_SetSeed(ONcSweep_RandomSeed);
	UUrLocalRandom_SetSeed(ONcSweep_RandomSeed);
}

// === simulation =======================================================

void ONrSweep_Tick(UUtUns32 inTicks)
{
	UUtUns32 itr;

	for (itr = 0; itr < inTicks; itr++) {
		UUtUns32	gameTicks = 0;
		UUtError	error;

		error = ONrGameState_Update(0, NULL, &gameTicks);
		if (error != UUcError_None) {
			ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Error,
				"ONrGameState_Update returned an error during sweep tick");
			return;
		}
	}
}

// === lifecycle ========================================================

UUtError ONrSweep_Begin(const char *inOutputPath, const char *inRenderer, UUtUns16 inLevel)
{
	if (inOutputPath == NULL) {
		return UUcError_Generic;
	}

	/* Beginning twice would leak the first handle and silently split the
	   report across two files. */
	if (ONgSweep_File != NULL) {
		return UUcError_Generic;
	}

	ONgSweep_File = fopen(inOutputPath, "w");
	if (ONgSweep_File == NULL) {
		UUrStartupMessage("[sweep] could not open report file '%s'", inOutputPath);
		return UUcError_Generic;
	}

	ONiSweep_CopyField(ONgSweep_Renderer, ONcSweep_RendererSize, inRenderer);
	ONgSweep_Level = (int) inLevel;
	ONgSweep_Phase[0] = '\0';
	ONgSweep_Subject[0] = '\0';
	ONgSweep_InTap = UUcFalse;

	ONgSweep_Active = UUcTrue;

	/* Registered last: until this point a warning takes the stock path. */
	UUrError_SetWarningTap(ONiSweep_WarningTap);

	UUrStartupMessage("[sweep] begin renderer=%s level=%d report=%s",
		ONgSweep_Renderer, ONgSweep_Level, inOutputPath);

	return UUcError_None;
}

void ONrSweep_End(void)
{
	/* Unregistered first, so nothing raised during teardown reaches a file
	   that is about to close. */
	UUrError_SetWarningTap(NULL);

	ONgSweep_Active = UUcFalse;

	if (ONgSweep_File != NULL) {
		fflush(ONgSweep_File);
		fclose(ONgSweep_File);
		ONgSweep_File = NULL;
	}

	ONgSweep_Phase[0] = '\0';
	ONgSweep_Subject[0] = '\0';
}

// === phase driver =====================================================

/*
	Stub. The individual phases land in later tasks; until then a sweep run
	produces no records, and sweep_diff correctly treats a report with no
	records as "no evidence" (exit 3) rather than as a clean run.
*/
void ONrSweep_RunAllPhases(UUtUns16 inLevel)
{
	(void) inLevel;
}

UUtError ONrSweep_Initialize(void)
{
	return UUcError_None;
}
