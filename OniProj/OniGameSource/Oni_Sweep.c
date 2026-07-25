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
#include "BFW_Akira.h"				/* AKrNodeFromPoint */
#include "BFW_Totoro.h"				/* TRrCollection_Lookup */

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

// === phase driver =====================================================

void ONrSweep_RunAllPhases(UUtUns16 inLevel)
{
	UUtBool	loaded;

	loaded = ONiSweep_Phase_Load(inLevel);

	ONiSweep_Phase_Characters(inLevel, loaded);

	/* Tasks 9-11 add particles, AI and scripts here, each gated on `loaded`. */

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
