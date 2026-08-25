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
#include "BFW_Console.h"

#include "Oni_Sweep.h"
#include "Oni_Sweep_Report.h"
#include "Oni_GameState.h"
#include "Oni_GameStatePrivate.h"	/* serverTime / gameTime; see ONrSweep_Tick */
#include "Oni_Level.h"				/* ONgLevel, ONrLevel_Load, ONrLevel_Flag_ID_To_Flag */
#include "Oni_Character.h"			/* TRcTemplate_CharacterClass, ONrGameState_NewCharacter */
#include "Oni_AI_Setup.h"			/* AItCharacterSetup / AItCharacterSetupArray */
#include "Oni_AI2.h"				/* AI2rDeleteAllCharacters, AI2rStartAllCharacters */
#include "Oni_AI2_Error.h"			/* AI2_ERROR_REPORT, AI2rError_SetReportLevel */
#include "BFW_Akira.h"				/* AKrNodeFromPoint */
#include "BFW_Totoro.h"				/* TRrCollection_Lookup */
#include "BFW_Particle3.h"			/* P3gClassTable, P3rGetParticleClass, P3rSendEvent */
#include "BFW_EnvParticle.h"		/* EPrEnumerateAllParticles, EPrNewParticle */
#include "BFW_ScriptLang.h"			/* SLrScript_ExecuteOnce */
#include "WM_Dialog.h"				/* WMrDialog_SetModalTap — see the modal tap */
#include "ONi_BundlePath.h"			/* ONiBundlePath_ResolveStateFile — see the script phase */

/*
	The recorded-function table (task 4) lives in the script language's private
	headers, and BFW_Source/BFW_ScriptLang is not on the include path. Reached
	by relative path rather than by re-declaring SLtRecordedScriptFunction here:
	a local copy of the struct that drifted from the real one would be read at
	the wrong offsets with nothing to catch it. Private.h has to come first —
	Database.h names SLtSymbol and SLtToken, which only Private.h defines.
*/
#include "../../BungieFrameWork/BFW_Source/BFW_ScriptLang/BFW_ScriptLang_Private.h"
#include "../../BungieFrameWork/BFW_Source/BFW_ScriptLang/BFW_ScriptLang_Database.h"

// === constants ========================================================

/* ONrSweep_Report_FormatLine never emits more than 958 bytes plus NUL. */
#define ONcSweep_LineBufferSize		1024

#define ONcSweep_RendererSize		32
#define ONcSweep_PhaseSize			64
#define ONcSweep_SubjectSize		128

/* Consecutive ONrGameState_Update calls that may advance no game time before
   ONrSweep_Tick gives up. A settle that is not settling has to end as a
   finding, not as a spin. */
#define ONcSweep_TickStallLimit		16

/*
	Cap on the ONCC instances one cell enumerates.

	ONCC instances are per-level, not global: level0_Final.dat holds none at
	all, and the largest of the fourteen shipped gameplay levels holds 54
	(level8_Final.dat; 602 across the whole game). 512 is nine times the
	biggest real case and exists only so an overlay pack that adds classes
	does not silently truncate the audit.

	It can still be exceeded, which is why the count is checked against
	TMrInstance_GetTagCount rather than assumed — see
	ONiSweep_AuditCharacterClasses. A truncated enumeration under-reports
	coverage, and does it with nothing in the report saying so, which is the
	one failure mode a sweep cannot afford.

	4 KB of stack at 64-bit pointer width.
*/
#define ONcSweep_MaxCharacterClasses	512

/*
	Buffer the script phase resolves persist.dat into. Comfortably past the
	$HOME/Library/Application Support/OniARM64/persist.dat case; a path that
	somehow does not fit makes ONiBundlePath_ResolveStateFile return an error,
	which the caller reads as "not sandboxed" and refuses to run on.
*/
#define ONcSweep_StatePathSize			512

// === state ============================================================

UUtBool			ONgSweep_Active = UUcFalse;

static FILE		*ONgSweep_File = NULL;
static char		ONgSweep_Renderer[ONcSweep_RendererSize] = "";
static int		ONgSweep_Level = 0;
static char		ONgSweep_Phase[ONcSweep_PhaseSize] = "";
static char		ONgSweep_Subject[ONcSweep_SubjectSize] = "";

/* Guards against a warning raised from inside the record path re-entering it.
   Nothing in ONrSweep_Record warns today; the flag keeps that from becoming a
   recursion the moment something in the write path starts to. Shared with the
   console tap, so a print from inside the writer cannot recurse across them. */
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

/*
	Fold whitespace and control bytes in a subject to underscores, in place.

	sweep_diff's baseline file is whitespace-delimited (see the comment on
	SWtFinding in tools/sweep_diff.h): a subject containing a space can be
	written into a report but can never be written into a baseline line that
	matches it, so it would gate as a permanent regression plus a permanent
	stale entry with no way to accept it. Every other subject the harness emits
	is a printf of harness-controlled text; the ones taken from level content —
	template instance names — are the only ones nothing bounds for us. The
	shipped game data has no whitespace in any of its 602 ONCC names, but a
	content pack is exactly the input this harness exists to test.
*/
static void ONiSweep_SanitiseSubject(char *ioSubject)
{
	char *cursor;

	if (ioSubject == NULL) {
		return;
	}

	for (cursor = ioSubject; *cursor != '\0'; cursor++) {
		unsigned char byte = (unsigned char) *cursor;

		if ((byte <= 0x20) || (byte == 0x7F)) {
			*cursor = '_';
		}
	}
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

// === modal tap ========================================================

/*
	Registered with the window manager for the lifetime of the sweep. A modal
	dialog — text_console diary pages, the win and lose screens — spins its own
	frame loop until somebody dismisses it, so an unattended run reaching one
	never comes back. Returning UUcTrue records it and skips the dialog.

	The id goes into the message because the baseline keys off the recorded
	text: two different dialogs have to be two different findings, or a newly
	reached one hides behind an already-known one.
*/
static UUtBool ONiSweep_ModalTap(WMtDialogID inDialogID)
{
	char		message[ONcSweep_SubjectSize];

	if (!ONgSweep_Active) {
		return UUcFalse;
	}

	if (ONgSweep_InTap) {
		/* Consume it anyway: still better than a dialog, and it cannot recurse. */
		return UUcTrue;
	}

	ONgSweep_InTap = UUcTrue;
	snprintf(message, sizeof(message), "modal dialog suppressed (id %u)",
		(unsigned) inDialogID);
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Warn, message);
	ONgSweep_InTap = UUcFalse;

	return UUcTrue;
}

// === console tap ======================================================

#if THE_DAY_IS_MINE || defined(ONI_SWEEP_CONSOLE)
/*
	Observe-only. COrConsole_Print continues to the ring buffer and
	consoleLog.txt whatever happens here, so the console stays a second,
	independent record of the run — there is no modal to suppress, so nothing
	justifies consuming. This is the path BSL script errors travel
	(SLrScript_ReportError -> COrConsole_Printf) and the one AI errors travel
	(Oni_AI2_Error.c -> COrConsole_Printf_Color).

	The guard sequence is spelled out rather than shared with the warning tap
	above: factoring the two into a common helper would change the warning tap's
	emitted code, and the shipping binary has to stay as it was. ONgSweep_InTap
	is shared, which is what actually matters — it stops a print raised from
	inside the writer recursing across the two taps.
*/
static void ONiSweep_ConsoleTap(const char *inString)
{
	if (!ONgSweep_Active || ONgSweep_InTap) {
		return;
	}

	ONgSweep_InTap = UUcTrue;
	ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Warn, inString);
	ONgSweep_InTap = UUcFalse;
}
#endif

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

/*
	Advance inTicks of GAME time — heartbeats actually executed, not calls to
	ONrGameState_Update.

	The two are not the same, and assuming they were is a silent way to make the
	whole harness useless. ONrGameState_Update runs its heartbeat loop
	iComputeDeltaTicks(serverTime - gameTime) times and then leaves serverTime
	equal to gameTime, so a second call with nothing else in between computes a
	delta of zero and runs no heartbeat at all. Calling Update 600 times would
	advance one tick and report a level where particles never settled and AI
	never ran as clean.

	The main loop keeps serverTime ahead by calling ONrGameState_UpdateServerTime,
	which reads UUrMachineTime_Sixtieths(). That is wall clock, so it would hand
	a different number of heartbeats to every run and undo the fixed seeding this
	module exists to provide. We advance serverTime ourselves instead: one tick
	per iteration, no clock anywhere.

	Ticks are counted from what Update reports it ran, not from the iteration
	count, so a run with ONgFastMode or cutscene-skipping set — where a single
	call executes 24 or 32 heartbeats — still settles for the requested amount of
	game time rather than 24x too much.
*/
void ONrSweep_Tick(UUtUns32 inTicks)
{
	UUtUns32	elapsed = 0;
	UUtUns32	stalls = 0;

	if (ONgGameState == NULL) {
		ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Error,
			"sweep tick with no game state");
		return;
	}

	while (elapsed < inTicks) {
		UUtUns32	gameTicks = 0;
		UUtError	error;

		/* One heartbeat's worth of pending time, deterministically. */
		ONgGameState->serverTime = ONgGameState->gameTime + 1;

		error = ONrGameState_Update(0, NULL, &gameTicks);
		if (error != UUcError_None) {
			ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Error,
				"ONrGameState_Update returned an error during sweep tick");
			return;
		}

		if (gameTicks == 0) {
			/* iComputeDeltaTicks zeroes the delta under ONgSingleStep, so this
			   is reachable. Bail rather than spin, and say so — a phase that
			   silently did not settle is exactly the failure this loop exists
			   to prevent. */
			stalls++;
			if (stalls >= ONcSweep_TickStallLimit) {
				ONrSweep_Record(NULL, NULL, ONcSweepSeverity_Error,
					"sweep tick advanced no game time; settle abandoned");
				return;
			}
			continue;
		}

		stalls = 0;
		elapsed += gameTicks;
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
	/* Anything recorded before the first phase sets its own context belongs to
	   startup, and "init" says so. An empty phase would drop those records into
	   an unattributed bucket in the merged report. */
	ONiSweep_CopyField(ONgSweep_Phase, ONcSweep_PhaseSize, ONcSweep_PhaseInit);
	ONgSweep_Subject[0] = '\0';
	ONgSweep_InTap = UUcFalse;

	ONgSweep_Active = UUcTrue;

	/* Registered last: until this point a warning takes the stock path. */
	UUrError_SetWarningTap(ONiSweep_WarningTap);
	WMrDialog_SetModalTap(ONiSweep_ModalTap);
#if THE_DAY_IS_MINE || defined(ONI_SWEEP_CONSOLE)
	COrConsole_SetTap(ONiSweep_ConsoleTap);
#endif

	/* A floor, not the guarantee. The per-phase reseed is the load-bearing one,
	   because level load runs sky init and Oni_Sky.c reseeds UUrRandom from the
	   sky template's star_seed on the way through. Seeding here only makes the
	   window between Begin and the first phase reproducible. */
	ONrSweep_SeedRandom();

	UUrStartupMessage("[sweep] begin renderer=%s level=%d report=%s",
		ONgSweep_Renderer, ONgSweep_Level, inOutputPath);

	return UUcError_None;
}

void ONrSweep_End(void)
{
	/* Unregistered first, so nothing raised during teardown reaches a file
	   that is about to close. */
	UUrError_SetWarningTap(NULL);
	WMrDialog_SetModalTap(NULL);
#if THE_DAY_IS_MINE || defined(ONI_SWEEP_CONSOLE)
	COrConsole_SetTap(NULL);
#endif

	ONgSweep_Active = UUcFalse;

	if (ONgSweep_File != NULL) {
		fflush(ONgSweep_File);
		fclose(ONgSweep_File);
		ONgSweep_File = NULL;
	}

	ONgSweep_Phase[0] = '\0';
	ONgSweep_Subject[0] = '\0';

	/* Cleared here as well as in Begin: a crash inside the writer would leave
	   the flag set, and the next Begin in the same process would then drop
	   every tapped record without a word. */
	ONgSweep_InTap = UUcFalse;
}

// === phases: load =====================================================

/*
	Load the level under test.

	Returns UUcTrue only when a game-state level is live afterwards, which is
	the precondition every later phase has. A phase run against a half-loaded
	level does not produce findings about the level, it produces a crash in
	the harness.

	inProgressBar is UUcFalse because the progress dialog draws, and a sweep
	runs with drawing bypassed.
*/
static UUtBool ONiSweep_Phase_Load(UUtUns16 inLevel)
{
	char		subject[ONcSweep_SubjectSize];
	UUtError	error;

	snprintf(subject, sizeof(subject), "level%u", (unsigned) inLevel);

	/*
		Context first, and specifically before the load call. The warning and
		console taps attribute whatever they capture to the context that is
		current when the line arrives, and ONrLevel_Load is by a wide margin
		the noisiest thing a cell does — template loading, Akira, pathfinding,
		the AI2 spawn pass and the level's `main` script all print through it.
		Set afterwards, every one of those lines would land under whichever
		phase happened to run last.
	*/
	ONrSweep_SetContext(ONcSweep_PhaseLoad, subject);

	/*
		Reseeded here rather than relying on ONrSweep_Begin: Oni_Sky.c calls
		UUrRandom_SetSeed(skyClass->star_seed) during ONrSky_LevelBegin, so the
		seed set before a load does not survive it. Every phase reseeds at its
		own top for the same reason.
	*/
	ONrSweep_SeedRandom();

	/*
		Level 0 is the menu's template set, and it is already loaded: Oni.c
		calls ONrLevel_LoadZero during engine init (Oni.c:1272), well before
		the sweep entry point (Oni.c:1349). ONrLevel_LoadZero does not go
		through ONrLevel_Load — it never calls ONrGameState_LevelBegin — so
		ONgLevel is still NULL and there is no game-state level for the later
		phases to work on. Reloading it through ONrLevel_Load here would test
		a code path the engine never takes for level 0.
	*/
	if (inLevel == 0) {
		ONrSweep_Record(ONcSweep_PhaseLoad, subject, ONcSweepSeverity_Skipped,
			"level 0 is loaded by engine init and has no game-state level");
		return UUcFalse;
	}

	error = ONrLevel_Load(inLevel, UUcFalse);
	if (error != UUcError_None) {
		ONrSweep_Record(ONcSweep_PhaseLoad, subject, ONcSweepSeverity_Abort,
			"ONrLevel_Load failed");
		return UUcFalse;
	}

	/*
		ONrLevel_Load bails out on the first failing step (UUmError_ReturnOnError
		throughout), so an error return can leave ONgLevel set from a partial
		load — and a success return cannot leave it NULL, since
		ONrGameState_LevelBegin fails outright when no ONLV carries an
		environment. Check anyway: the later phases dereference it.
	*/
	if (ONgLevel == NULL) {
		ONrSweep_Record(ONcSweep_PhaseLoad, subject, ONcSweepSeverity_Abort,
			"level loaded but no level instance is current");
		return UUcFalse;
	}

	/* Let whatever the load spawned come to rest before anything is measured. */
	ONrSweep_Tick(ONcSweep_SettleCharacter);

	return UUcTrue;
}

// === phases: characters ===============================================

/*
	Task 8a: every character class instance in the cell must be able to spawn.

	"Resolves" is not just "the template manager handed back a pointer" — the
	enumeration only ever yields descriptors whose dataPtr is non-NULL, so that
	on its own asserts nothing. The condition checked here is the one
	ONrGameState_NewCharacter refuses a spawn on (Oni_Character.c:2669-2679,
	issues #56 and #97): a class with no body, no animation collection, or an
	animation collection missing the Stand entry cannot be placed in a level at
	all. A pack that ships one is broken in a way no amount of play would
	surface until the moment something tried to spawn it.
*/
static void ONiSweep_AuditCharacterClasses(void)
{
	void		*classList[ONcSweep_MaxCharacterClasses];
	UUtUns32	numClasses = 0;
	UUtUns32	tagCount;
	UUtUns32	itr;
	UUtError	error;

	ONrSweep_SetContext(ONcSweep_PhaseCharacters, "classes");

	error = TMrInstance_GetDataPtr_List(TRcTemplate_CharacterClass,
		ONcSweep_MaxCharacterClasses, &numClasses, classList);
	if (error != UUcError_None) {
		/* The only error path leaves outNumPtrs untouched, so numClasses says
		   nothing here and classList holds whatever the stack held. */
		ONrSweep_Record(ONcSweep_PhaseCharacters, "classes", ONcSweepSeverity_Error,
			"could not enumerate character class instances");
		return;
	}

	/*
		Truncation check. TMrInstance_GetTagCount passes TMcMaxInstances as its
		own cap, so its answer is the real total and the comparison is exact.
		Called only after the enumeration above succeeded: GetTagCount asserts
		that the same call returns no error, and in a DEBUGGING build that
		assert traps.
	*/
	tagCount = TMrInstance_GetTagCount(TRcTemplate_CharacterClass);
	if (tagCount > numClasses) {
		ONrSweep_Record(ONcSweep_PhaseCharacters, "classes", ONcSweepSeverity_Error,
			"more character classes than the sweep can enumerate; audit truncated");
	}

	if (numClasses == 0) {
		ONrSweep_Record(ONcSweep_PhaseCharacters, "classes", ONcSweepSeverity_Skipped,
			"cell defines no character classes");
		return;
	}

	for (itr = 0; itr < numClasses; itr++) {
		ONtCharacterClass	*characterClass = (ONtCharacterClass *) classList[itr];
		const char			*name;
		char				subject[ONcSweep_SubjectSize];

		if (characterClass == NULL) {
			/* Filtered out by the enumeration, so this is unreachable today.
			   It costs one branch and the alternative is a NULL deref inside a
			   harness whose entire job is to survive bad content. */
			snprintf(subject, sizeof(subject), "class_%u", (unsigned) itr);
			ONrSweep_Record(ONcSweep_PhaseCharacters, subject, ONcSweepSeverity_Error,
				"character class instance is NULL");
			continue;
		}

		name = TMrInstance_GetInstanceName(characterClass);
		if ((name == NULL) || (name[0] == '\0')) {
			/* Enumeration order is fixed for fixed data, so the index is a
			   stable identity even though it is not a readable one. */
			snprintf(subject, sizeof(subject), "class_%u", (unsigned) itr);
			ONrSweep_Record(ONcSweep_PhaseCharacters, subject, ONcSweepSeverity_Error,
				"character class instance has no name");
		} else {
			ONiSweep_CopyField(subject, sizeof(subject), name);
			ONiSweep_SanitiseSubject(subject);
		}

		ONrSweep_SetContext(ONcSweep_PhaseCharacters, subject);

		if (characterClass->body == NULL) {
			ONrSweep_Record(ONcSweep_PhaseCharacters, subject, ONcSweepSeverity_Error,
				"character class has no body and cannot spawn");
		}

		if (characterClass->animations == NULL) {
			ONrSweep_Record(ONcSweep_PhaseCharacters, subject, ONcSweepSeverity_Error,
				"character class has no animation collection and cannot spawn");
		} else if (TRrCollection_Lookup(characterClass->animations,
					ONcAnimType_Stand, ONcAnimState_Standing, 0) == NULL) {
			ONrSweep_Record(ONcSweep_PhaseCharacters, subject, ONcSweepSeverity_Error,
				"character class has no standing animation and cannot spawn");
		}
	}
}

/*
	Task 8b: spawn every character setup the level defines, by the same route
	the chr_create console command takes (Oni_AI2_Script.c:2496).

	The characters are deliberately left alive — the AI phase wants a populated
	level, and deleting them would undo the only thing this phase produces.

	Two things about the resulting level are worth knowing before reading a
	report. First, the level load has already spawned characters of its own:
	AI2rLevelBegin creates them from the level's OBJcType_Character objects and
	the `main` script calls chr_create for the scripted ones, both inside
	ONrLevel_Load. This phase therefore places a second character on flags that
	may already be occupied, and interpenetrating characters push each other
	apart. Second, ONcMaxCharacters is 128 and the largest shipped level defines
	19 setups, so the cap is reachable only in combination with what the load
	spawned; ONrGameState_NewCharacter handles it gracefully, returning
	UUcError_Generic once every slot is in use rather than overrunning.

	Both effects are deterministic for fixed data and a fixed seed, so whatever
	they produce enters the baseline once and only changes to it gate. That is
	the trade the harness makes everywhere: noise is affordable, silence is not.
*/
static void ONiSweep_SpawnCharacterSetups(void)
{
	AItCharacterSetupArray	*setupArray;
	UUtUns16				itr;

	setupArray = ONgLevel->characterSetupArray;
	if (setupArray == NULL) {
		ONrSweep_Record(ONcSweep_PhaseCharacters, "setups", ONcSweepSeverity_Skipped,
			"level has no character setup array");
		return;
	}

	if (setupArray->numCharacterSetups == 0) {
		ONrSweep_Record(ONcSweep_PhaseCharacters, "setups", ONcSweepSeverity_Skipped,
			"level defines no character setups");
		return;
	}

	for (itr = 0; itr < setupArray->numCharacterSetups; itr++) {
		const AItCharacterSetup	*setup = setupArray->characterSetups + itr;
		ONtFlag					flag;
		M3tPoint3D				location;
		char					subject[ONcSweep_SubjectSize];
		UUtError				error;

		/* The script ID is how a setup is named everywhere else — chr_create
		   takes it, and the engine's own "can not find character id" dump
		   prints it — so it is the handle triage will recognise. */
		snprintf(subject, sizeof(subject), "setup_%d", (int) setup->defaultScriptID);
		ONrSweep_SetContext(ONcSweep_PhaseCharacters, subject);

		if (!ONrLevel_Flag_ID_To_Flag(setup->defaultFlagID, &flag)) {
			ONrSweep_Record(ONcSweep_PhaseCharacters, subject, ONcSweepSeverity_Error,
				"default flag does not exist");
			continue;
		}

		location = flag.location;
		location.y += ONcCharacterOffsetToBNV;

		if (AKrNodeFromPoint(&location) == NULL) {
			ONrSweep_Record(ONcSweep_PhaseCharacters, subject, ONcSweepSeverity_Error,
				"spawn flag is not inside a BNV");
			continue;
		}

		error = ONrGameState_NewCharacter(NULL, setup, &flag, NULL);
		if (error != UUcError_None) {
			/*
				One message, no attributed cause. The refusal could be a full
				character table or an incomplete character class, and nothing
				reachable from here tells the two apart: ONgGameState->numCharacters
				is a high-water mark over slots ever used, not a count of slots
				currently in use, so it cannot prove the table is full. Guessing
				would put a diagnosis in the report that the evidence does not
				support.
			*/
			ONrSweep_Record(ONcSweep_PhaseCharacters, subject, ONcSweepSeverity_Error,
				"ONrGameState_NewCharacter refused the spawn");
			continue;
		}

		ONrSweep_Tick(ONcSweep_SettleCharacter);
	}
}

/*
	inLoaded is the load phase's verdict. Running against a level that did not
	load is not a way to find out more about it — ONgLevel and the game state
	are in whatever state ONrLevel_Load abandoned them in.
*/
static void ONiSweep_Phase_Characters(UUtUns16 inLevel, UUtBool inLoaded)
{
	ONrSweep_SetContext(ONcSweep_PhaseCharacters, "level");

	if (inLevel == 0) {
		ONrSweep_Record(ONcSweep_PhaseCharacters, "level", ONcSweepSeverity_Skipped,
			"menu level has no characters");
		return;
	}

	if (!inLoaded) {
		ONrSweep_Record(ONcSweep_PhaseCharacters, "level", ONcSweepSeverity_Skipped,
			"level did not load");
		return;
	}

	if ((ONgLevel == NULL) || (ONgGameState == NULL)) {
		ONrSweep_Record(ONcSweep_PhaseCharacters, "level", ONcSweepSeverity_Skipped,
			"no level or game state is current");
		return;
	}

	ONrSweep_SeedRandom();

	ONiSweep_AuditCharacterClasses();
	ONiSweep_SpawnCharacterSetups();
}

// === phases: particles ================================================

/*
	Task 9a: every particle class the cell can see must be findable by its own
	name.

	This is not the tautology it looks like. P3rGetParticleClass does a binary
	search over P3gClassLookupTable, a separate array of pointers into
	P3gClassTable that P3rLoad_PostProcess builds by qsorting the class table by
	kind, walking it into the lookup table and qsorting that alphabetically
	(BFW_Particle3.c:2963-2976). Every emitter link in the game is then resolved
	through that lookup — a class that the table holds but the lookup cannot find
	is a class whose particles no other class can ever emit. The engine's own
	consistency check on this is inside `#if DEBUGGING`, so a shipping build says
	nothing about it at all.

	Subjects are class names, so a finding names the class that is wrong rather
	than saying "particles".

	Worth knowing before reading fifteen reports: particle classes are GLOBAL,
	not per-level. Every PAR3 binary-data instance in the shipped game lives in
	level0_Final.dat, which engine init loads before any sweep runs; the fourteen
	gameplay level files contain none. So this audit sees the same class table in
	every cell and any finding it produces repeats identically across all of them.
	That is the correct behaviour for a per-level gate — the alternative is a
	global class table that only one arbitrary cell checks — but it does mean one
	broken class costs fifteen baseline lines, not one.
*/
static void ONiSweep_AuditParticleClasses(void)
{
	UUtUns16	itr;

	ONrSweep_SetContext(ONcSweep_PhaseParticles, "particle_classes");

	if (P3gNumClasses == 0) {
		ONrSweep_Record(ONcSweep_PhaseParticles, "particle_classes", ONcSweepSeverity_Skipped,
			"no particle classes are loaded");
		return;
	}

	/*
		P3rLoadParticleDefinition counts every class it had to drop into
		P3gOverflowedClasses, but that counter is not declared in
		BFW_Particle3.h and this file may not add a declaration for it. A full
		table is the observable half of the same condition: the load stops
		admitting classes at exactly this point, so a cell that reaches the cap
		is a cell whose class set may be a prefix of the real one.
	*/
	if (P3gNumClasses >= P3cMaxParticleClasses) {
		ONrSweep_Record(ONcSweep_PhaseParticles, "particle_classes", ONcSweepSeverity_Error,
			"particle class table is full; some classes may not have loaded");
	}

	for (itr = 0; itr < P3gNumClasses; itr++) {
		P3tParticleClass	*particleClass = &P3gClassTable[itr];
		char				subject[ONcSweep_SubjectSize];

		if (particleClass->classname[0] == '\0') {
			/* Enumeration order is fixed for fixed data, so the index is a
			   stable identity even though it is not a readable one. */
			snprintf(subject, sizeof(subject), "p3class_%u", (unsigned) itr);
			ONrSweep_SetContext(ONcSweep_PhaseParticles, subject);
			ONrSweep_Record(ONcSweep_PhaseParticles, subject, ONcSweepSeverity_Error,
				"particle class has no name");
			continue;
		}

		ONiSweep_CopyField(subject, sizeof(subject), particleClass->classname);
		ONiSweep_SanitiseSubject(subject);
		ONrSweep_SetContext(ONcSweep_PhaseParticles, subject);

		if (particleClass->definition == NULL) {
			ONrSweep_Record(ONcSweep_PhaseParticles, subject, ONcSweepSeverity_Error,
				"particle class has no definition");
			continue;
		}

		/*
			Identity, not merely non-NULL. Names in the lookup table are unique
			and strictly ascending (the DEBUGGING assert at BFW_Particle3.c:2985
			is on strcmp < 0), so the lookup is a bijection over the table and
			the only correct answer is this very class. A lookup that returns
			some other class for this name is a sorted-table bug, which is
			exactly the shape a 32->64 pointer-stride error takes here.
		*/
		if (P3rGetParticleClass(particleClass->classname) != particleClass) {
			ONrSweep_Record(ONcSweep_PhaseParticles, subject, ONcSweepSeverity_Error,
				"particle class is not findable by its own name");
		}
	}
}

/* Shared by the start and stop passes over the level's env particles. */
typedef struct ONtSweepEnvParticleData {
	UUtUns32	tick;
	float		time;
	UUtUns16	event;
	UUtUns32	index;
	UUtBool		report;		/* only the start pass records findings */
} ONtSweepEnvParticleData;

/*
	Set the sweep context to the env particle about to be touched.

	The class name is the handle worth having: several env particles usually
	share one class, so findings collapse onto the class that is actually broken
	rather than onto one placement of it. The tag is a level-authoring label and
	is empty for most of them.
*/
static void ONiSweep_EnvParticleSubject(const EPtEnvParticle *inParticle,
										UUtUns32 inIndex, char *outSubject, UUtUns32 inSize)
{
	if (inParticle->classname[0] != '\0') {
		ONiSweep_CopyField(outSubject, inSize, inParticle->classname);
		ONiSweep_SanitiseSubject(outSubject);
	} else {
		snprintf(outSubject, inSize, "envparticle_%u", (unsigned) inIndex);
	}
}

/*
	Create (if it has not been created yet) and start one environmental particle.

	Mirrors ONiParticle3_EnumerateAllCallback in Oni_Particle3.c, which is what
	p3_startall drives, with attribution and a finding added. No settle happens
	in here — this runs inside EPrEnumerateAllParticles' walk of the global env
	particle list, and ticking the game from inside that walk would let the
	simulation delete list entries under the enumeration.
*/
static void ONiSweep_EnvParticleCallback(EPtEnvParticle *inParticle, uintptr_t inUserData)
{
	ONtSweepEnvParticleData	*userData = (ONtSweepEnvParticleData *) inUserData;
	char					subject[ONcSweep_SubjectSize];

	ONiSweep_EnvParticleSubject(inParticle, userData->index, subject, sizeof(subject));
	userData->index++;

	ONrSweep_SetContext(ONcSweep_PhaseParticles, subject);

	if (inParticle->particle == NULL) {
		EPrNewParticle(inParticle, userData->tick);
	}

	if (inParticle->particle == NULL) {
		if (userData->report) {
			/*
				One reported cause only. EPrNewParticle returns UUcFalse for a
				missing class, for a refused P3rCreateParticle, and for a decal
				whose own creation declined — and a decal leaves particle NULL
				on the perfectly ordinary path where it drew a decal instead. A
				NULL particle_class is the one case that is unambiguously broken
				content: the level places a particle whose class does not exist.
			*/
			if (inParticle->particle_class == NULL) {
				ONrSweep_Record(ONcSweep_PhaseParticles, subject, ONcSweepSeverity_Error,
					"environmental particle names a class that does not exist");
			}
		}
		return;
	}

	P3rSendEvent(inParticle->particle_class, inParticle->particle,
		(UUtUns16) userData->event, userData->time);
}

/*
	Task 9b: start every environmental particle the level places, let them run,
	then stop them.

	This is the per-level half of the phase. Environmental particles are level
	content (EPtEnvParticleArray comes out of the level's own templates), unlike
	the class table above, and starting them is what p3_startall exists for:
	particles flagged NotInitiallyCreated are never created during ordinary play
	until something triggers them, so a broken one can sit in a level unnoticed.

	Started as one pass and settled once, rather than one settle per particle.
	Per-particle settling would multiply ONcSweep_SettleParticle by the number of
	placements in the level for no gain — attribution is set at creation time,
	which is where the per-particle findings are, and a shared settle is exactly
	as attributable as a per-particle one is for anything the simulation prints
	afterwards, since by then every class is emitting anyway.

	Stopped afterwards but deliberately not killed. P3rKillAll (what p3_killall
	calls) destroys every particle in the game including the ones belonging to
	characters and weapons, which is a much larger change to the level than this
	phase made; sending Stop leaves emitters quiet and lets what is already alive
	expire on its own.

	The stop pass is not an exact undo, and the difference is worth knowing when
	reading a report. It sends Stop to every environmental particle, including
	the ones the level itself had running before this phase touched anything, so
	the AI phase inherits a level slightly quieter than the load produced. That
	is the deliberate trade: three thousand AI ticks underneath every emitter in
	the level would attribute a great deal of particle-system output to the AI
	phase, and this harness buys attribution with realism everywhere else too.
*/
static void ONiSweep_StartEnvParticles(void)
{
	ONtSweepEnvParticleData	userData;

	ONrSweep_SetContext(ONcSweep_PhaseParticles, "environment");

	userData.tick = ONrGameState_GetGameTime();
	userData.time = ((float) userData.tick) / UUcFramesPerSecond;
	userData.event = P3cEvent_Start;
	userData.index = 0;
	userData.report = UUcTrue;

	EPrEnumerateAllParticles(ONiSweep_EnvParticleCallback, (uintptr_t) &userData);

	if (userData.index == 0) {
		ONrSweep_Record(ONcSweep_PhaseParticles, "environment", ONcSweepSeverity_Skipped,
			"level places no environmental particles");
		return;
	}

	ONrSweep_SetContext(ONcSweep_PhaseParticles, "environment");
	ONrSweep_Tick(ONcSweep_SettleParticle);

	userData.tick = ONrGameState_GetGameTime();
	userData.time = ((float) userData.tick) / UUcFramesPerSecond;
	userData.event = P3cEvent_Stop;
	userData.index = 0;
	userData.report = UUcFalse;

	EPrEnumerateAllParticles(ONiSweep_EnvParticleCallback, (uintptr_t) &userData);
}

static void ONiSweep_Phase_Particles(UUtUns16 inLevel, UUtBool inLoaded)
{
	ONrSweep_SetContext(ONcSweep_PhaseParticles, "level");

	if (inLevel == 0) {
		ONrSweep_Record(ONcSweep_PhaseParticles, "level", ONcSweepSeverity_Skipped,
			"menu level has no game-state level to run particles in");
		return;
	}

	if (!inLoaded) {
		ONrSweep_Record(ONcSweep_PhaseParticles, "level", ONcSweepSeverity_Skipped,
			"level did not load");
		return;
	}

	if ((ONgLevel == NULL) || (ONgGameState == NULL)) {
		ONrSweep_Record(ONcSweep_PhaseParticles, "level", ONcSweepSeverity_Skipped,
			"no level or game state is current");
		return;
	}

	ONrSweep_SeedRandom();

	ONiSweep_AuditParticleClasses();
	ONiSweep_StartEnvParticles();
}

// === phases: AI =======================================================

/*
	Task 10: populate the level with every AI it can hold and let them think.

	Driven through AI2rDeleteAllCharacters + AI2rStartAllCharacters rather than
	through COrCommand_Execute("ai2_spawnall"). Those two calls ARE ai2_spawnall
	(Oni_AI2_Script.c:539-545); going straight at them costs nothing and buys the
	error return, where the console route yields one bool that says only whether
	the console found a hook to call.

	What the spawn actually does, since it is not simply a repeat of what the
	load already did:

	  * AI2rDeleteAllCharacters(UUcFalse) removes characters whose scriptID is
	    UUcMaxUns16 — the ones AI2rLevelBegin created from the level's character
	    objects — and leaves the player alone. The character phase's spawns carry
	    setup->defaultScriptID (Oni_AI2.c:747) and so survive this.
	  * AI2rStartAllCharacters(UUcFalse, UUcTrue) recreates those with the
	    override set, which is the part that matters: it also spawns every
	    character flagged OBJcCharFlags_NotInitiallyPresent, the reinforcements
	    that ordinary play only ever sees if a trigger fires. Those are AI that a
	    level load never touches.

	So it is a fresh AI population that is strictly larger than the loaded one,
	on top of the character phase's setups. ONcMaxCharacters is 128 and
	ONrGameState_NewCharacter refuses gracefully once the table is full.
*/
static void ONiSweep_Phase_AI(UUtUns16 inLevel, UUtBool inLoaded)
{
	UUtError	error;

	ONrSweep_SetContext(ONcSweep_PhaseAI, "level");

	if (inLevel == 0) {
		ONrSweep_Record(ONcSweep_PhaseAI, "level", ONcSweepSeverity_Skipped,
			"menu level has no AI");
		return;
	}

	if (!inLoaded) {
		ONrSweep_Record(ONcSweep_PhaseAI, "level", ONcSweepSeverity_Skipped,
			"level did not load");
		return;
	}

	if ((ONgLevel == NULL) || (ONgGameState == NULL)) {
		ONrSweep_Record(ONcSweep_PhaseAI, "level", ONcSweepSeverity_Skipped,
			"no level or game state is current");
		return;
	}

	ONrSweep_SeedRandom();

	/*
		READ THIS BEFORE CONCLUDING A LEVEL'S AI IS HEALTHY.

		AI2 has two error channels and they are separately gated. The log channel
		(ai2_set_logerror) writes ai2_log.txt and never reaches the console, so
		the sweep's console tap cannot see it; the report channel
		(ai2_set_reporterror) is the one that goes through COrConsole_Printf_Color
		and is therefore the one a sweep wants. Both are irrelevant in the binary
		this actually builds as: AI2_ERROR_REPORT is #defined from TOOL_VERSION,
		TOOL_VERSION is 0 whenever SHIPPING_VERSION is 1, and the OniSweep target
		inherits SHIPPING_VERSION=1 from the Oni target. With it off, the
		AI2_ERROR macro expands to AI2rHandleError, which handles the error and
		returns without reporting or logging anything at all.

		The phase is still worth running — three thousand ticks of pathfinding,
		combat and movement is the point, and a crash or a UUrPrintWarning still
		lands in the report — but the AI's own diagnosis of itself is not in
		evidence. The record below says so in the report rather than leaving a
		reader to infer quiet means clean.
	*/
#if AI2_ERROR_REPORT
	AI2rError_SetReportLevel(AI2cSubsystem_All, AI2cStatus);
#else
	ONrSweep_Record(ONcSweep_PhaseAI, "level", ONcSweepSeverity_Skipped,
		"AI2 error reporting is compiled out of this build; AI findings come from warnings and crashes only");
#endif

	ONrSweep_SetContext(ONcSweep_PhaseAI, "spawnall");

	AI2rDeleteAllCharacters(UUcFalse);

	error = AI2rStartAllCharacters(UUcFalse, UUcTrue);
	if (error != UUcError_None) {
		ONrSweep_Record(ONcSweep_PhaseAI, "spawnall", ONcSweepSeverity_Error,
			"AI2rStartAllCharacters failed");
		/* Not a return: whatever it did manage to spawn is still worth settling,
		   and the settle is where AI findings come from. */
	}

	ONrSweep_SetContext(ONcSweep_PhaseAI, "settle");
	ONrSweep_Tick(ONcSweep_SettleAI);
}

// === phases: BSL scripts ==============================================

/*
	Refuse to run the script phase unless the engine's persist.dat resolves to
	one in the current directory.

	This is not defensiveness about a hypothetical. The shipped BSL corpus calls
	save_game 56 times across the fourteen levels; save_game reaches
	ONrGameState_MakeContinue -> ONrPersist_SetContinue -> ONrPersist(), which
	rewrites persist.dat in full. ONiBundlePath_ResolveStateFile answers that
	call with ./persist.dat when one exists in the working directory and with
	$HOME/Library/Application Support/OniARM64/persist.dat when one does not —
	and the second of those is where the player's real saved games live. A sweep
	that called every BSL function from the default working directory would
	overwrite them, on every cell, with continues manufactured from whatever
	state the harness happened to leave the level in.

	The path is resolved at write time (Oni_Persistance.c:161-165), not cached at
	startup, so a working directory holding its own persist.dat is a complete
	containment: every write during the run lands in the sandbox.

	Skipping loudly is the right failure. The driver has to opt in by giving each
	cell a working directory with a persist.dat in it, and until it does, the
	report says in so many words why the script phase produced nothing.
*/
static UUtBool ONiSweep_ScriptsAreSandboxed(void)
{
	char		path[ONcSweep_StatePathSize];
	UUtError	error;

	error = ONiBundlePath_ResolveStateFile("persist.dat", path, sizeof(path));
	if (error != UUcError_None) {
		return UUcFalse;
	}

	return (UUtBool) (strcmp(path, "./persist.dat") == 0);
}

/*
	Has this name already been called during this phase?

	Recording happens before the database's duplicate-symbol check, so the same
	name can appear twice in the table (task 4). No shipped level does it, but a
	content pack that redefines a function would, and calling a function twice
	would put a second, differently-conditioned run of the same script into the
	report under one identity — two lines that gate as one.

	Scanned against the earlier table entries rather than into a set of our own:
	the table holds up to 2048 names of 64 bytes, which is 128 KB nobody needs on
	the stack, and the worst shipped level records 175 entries, so this costs
	fifteen thousand strcmps once per cell.
*/
static UUtBool ONiSweep_ScriptNameSeenEarlier(const char *inName, UUtUns32 inIndex)
{
	UUtUns32 itr;

	for (itr = 0; itr < inIndex; itr++) {
		const SLtRecordedScriptFunction *earlier = SLrScript_Database_RecordedFunctions_Get(itr);

		if (earlier == NULL) {
			continue;
		}

		if (strcmp(earlier->name, inName) == 0) {
			return UUcTrue;
		}
	}

	return UUcFalse;
}

/*
	Task 11: call every zero-arity BSL function the loaded level defines.

	The table is the one the task 4 hook fills at registration. Three of its
	properties are load-bearing here:

	  * numParams is not to be trusted for anything but the zero test. Recording
	    happens before the engine's own bound on the formal count, so a malformed
	    script can be recorded with an arity that no registered symbol has. It is
	    never used to size anything.
	  * a name in the table is not a promise that a symbol exists, for the same
	    reason. SLrDatabase_IsFunctionCall is asked first, and a name that does
	    not resolve is recorded as skipped — it is a fact about the script, not a
	    failure of the harness.
	  * the table is only meaningful while a level is loaded. ONrScript_LevelBegin
	    resets it and ONrLevel_Load calls that; ONrLevel_Unload does not, and
	    ONrLevel_LoadZero never called it in the first place. Reading it outside a
	    loaded level reads the previous level's entries. This phase runs inside
	    one, gated on the load phase's verdict.

	Functions that take parameters are recorded as skipped with their arity and
	not called. Passing invented arguments to a level's own script would produce
	findings about the harness's guesses rather than about the level.

	On what a cold call can do, which is the real risk in this phase: these are
	narrative scripts being run out of order, and the shipped corpus contains 347
	chr_delete calls, 168 cinematic_start, 54 restore_game and 42 win/lose. Most
	of that is survivable here. win and lose only set ONgGameState->victory, and
	the switch that acts on it is in the Oni.c main loop, which a sweep never
	reaches — it runs the phases and sets ONgTerminateGame. restore_game teleports
	the player. splash_screen sets a pending flag that the bypassed display path
	would have consumed. movie_play would block on AVFoundation, and no shipped
	script calls it. save_game is the one that reaches outside the process, and
	the sandbox check above is what answers it.

	What none of this can guard is a script that never returns. SLrScript_ExecuteOnce
	runs the body synchronously through SLrScript_Parse; a sleep or a stalling
	command returns promptly with the context parked for the scheduler, but a BSL
	loop with no sleep in it spins inside that call with nothing in this process
	able to interrupt it. A wall-clock budget here was considered and rejected: it
	would make how many functions a cell called depend on how fast the machine was,
	and every finding after the cutoff would appear and disappear between runs.
	The driver's per-cell watchdog is the backstop, and it is the only one.

	One more thing to expect in a memory profile rather than in the report:
	SLrSchedule_Function_Script has its SLrContext_Delete commented out for the
	immediate-execution path (BFW_ScriptLang_Scheduler.c:256-259, Bungie's own
	comment), so every call leaks one SLtContext. That is 5,424 bytes each on
	arm64, or about a megabyte across the worst level's table. It is not a leak
	this harness can fix from here — nothing reachable tells us whether a given
	context has finished or is parked in the scheduler, and deleting a parked one
	is a use-after-free. SLgDatabaseHeap was created non-fixed and adds subheaps
	on demand, so it grows rather than failing, and the process is one-shot.
*/
static void ONiSweep_CallScriptFunctions(void)
{
	UUtUns32	count;
	UUtUns32	itr;

	count = SLrScript_Database_RecordedFunctions_Count();

	if (count == 0) {
		ONrSweep_Record(ONcSweep_PhaseScripts, "functions", ONcSweepSeverity_Skipped,
			"level defines no script functions");
		return;
	}

	for (itr = 0; itr < count; itr++) {
		const SLtRecordedScriptFunction	*function;
		char							subject[ONcSweep_SubjectSize];
		char							message[ONcSweep_SubjectSize];
		UUtError						error;

		function = SLrScript_Database_RecordedFunctions_Get(itr);
		if (function == NULL) {
			/* Count and Get read the same counter, so this cannot happen today.
			   It costs one branch, and the alternative is dereferencing NULL. */
			snprintf(subject, sizeof(subject), "function_%u", (unsigned) itr);
			ONrSweep_Record(ONcSweep_PhaseScripts, subject, ONcSweepSeverity_Error,
				"recorded script function could not be read back");
			continue;
		}

		if (function->name[0] == '\0') {
			snprintf(subject, sizeof(subject), "function_%u", (unsigned) itr);
			ONrSweep_SetContext(ONcSweep_PhaseScripts, subject);
			ONrSweep_Record(ONcSweep_PhaseScripts, subject, ONcSweepSeverity_Error,
				"recorded script function has no name");
			continue;
		}

		ONiSweep_CopyField(subject, sizeof(subject), function->name);
		ONiSweep_SanitiseSubject(subject);
		ONrSweep_SetContext(ONcSweep_PhaseScripts, subject);

		if (ONiSweep_ScriptNameSeenEarlier(function->name, itr)) {
			ONrSweep_Record(ONcSweep_PhaseScripts, subject, ONcSweepSeverity_Skipped,
				"script function name is recorded more than once; called once only");
			continue;
		}

		if (function->numParams != 0) {
			snprintf(message, sizeof(message),
				"script function takes %u parameters; not called",
				(unsigned) function->numParams);
			ONrSweep_Record(ONcSweep_PhaseScripts, subject, ONcSweepSeverity_Skipped, message);
			continue;
		}

		if (!SLrDatabase_IsFunctionCall(function->name)) {
			ONrSweep_Record(ONcSweep_PhaseScripts, subject, ONcSweepSeverity_Skipped,
				"recorded script function does not resolve to a symbol; not called");
			continue;
		}

		error = SLrScript_ExecuteOnce(function->name, 0, NULL, NULL, NULL);
		if (error != UUcError_None) {
			ONrSweep_Record(ONcSweep_PhaseScripts, subject, ONcSweepSeverity_Error,
				"script function failed to execute");
		}

		/*
			Settled after every call, not once at the end. A BSL function that
			sleeps or stalls has left a context parked for the scheduler, and
			SLrScript_Update only resumes it as game time passes — without a
			settle here the phase would start the next function on top of a
			suspended one and nothing would ever run to completion.
		*/
		ONrSweep_Tick(ONcSweep_SettleScript);
	}
}

static void ONiSweep_Phase_Scripts(UUtUns16 inLevel, UUtBool inLoaded)
{
	ONrSweep_SetContext(ONcSweep_PhaseScripts, "level");

	if (inLevel == 0) {
		ONrSweep_Record(ONcSweep_PhaseScripts, "level", ONcSweepSeverity_Skipped,
			"menu level has no level scripts");
		return;
	}

	if (!inLoaded) {
		ONrSweep_Record(ONcSweep_PhaseScripts, "level", ONcSweepSeverity_Skipped,
			"level did not load");
		return;
	}

	if ((ONgLevel == NULL) || (ONgGameState == NULL)) {
		ONrSweep_Record(ONcSweep_PhaseScripts, "level", ONcSweepSeverity_Skipped,
			"no level or game state is current");
		return;
	}

	if (!ONiSweep_ScriptsAreSandboxed()) {
		ONrSweep_Record(ONcSweep_PhaseScripts, "level", ONcSweepSeverity_Skipped,
			"no persist.dat in the working directory; calling level scripts could overwrite the player's saved games");
		return;
	}

	ONrSweep_SeedRandom();

	ONiSweep_CallScriptFunctions();
}

// === phase driver =====================================================

/*
	Order is load, characters, particles, AI, scripts, and each of the four after
	the load is gated on it.

	Characters first because both of the phases after it want a populated level:
	the setups are placed and settled before anything starts emitting or thinking.

	Particles before AI, for attribution. The particle phase's own settle wants a
	level that is not yet in combat: six hundred ticks of emitters with the
	console otherwise quiet attributes cleanly, and the same six hundred ticks
	underneath a running firefight would land every AI line under a particle
	class's subject. The reverse order buys nothing back — the particle phase
	stops what it started before it returns, so the AI phase gets the same level
	either way.

	Scripts last, and this one is not a preference. The shipped BSL corpus deletes
	characters 347 times, starts cutscenes 168 times and restores saved state 54
	times; a function called cold can take the level apart in ways no later phase
	could work around. Running it after everything else means whatever it wrecks,
	it wrecks only its own phase.
*/
void ONrSweep_RunAllPhases(UUtUns16 inLevel)
{
	UUtBool	loaded;

	loaded = ONiSweep_Phase_Load(inLevel);

	ONiSweep_Phase_Characters(inLevel, loaded);
	ONiSweep_Phase_Particles(inLevel, loaded);
	ONiSweep_Phase_AI(inLevel, loaded);
	ONiSweep_Phase_Scripts(inLevel, loaded);

	/*
		Terminal record, always. It is what separates "the cell ran and found
		nothing" from "the process died before it got here" — sweep_diff refuses
		to report a run with no records as clean, and without this a level whose
		phases all passed quietly would be indistinguishable from a level whose
		harness crashed on the first instruction. Skipped severity because it is
		a marker, not a finding.
	*/
	ONrSweep_SetContext(ONcSweep_PhaseDone, "cell");
	ONrSweep_Record(ONcSweep_PhaseDone, "cell", ONcSweepSeverity_Skipped,
		"cell completed");
}

UUtError ONrSweep_Initialize(void)
{
	return UUcError_None;
}
