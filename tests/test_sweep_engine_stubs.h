// ======================================================================
// test_sweep_engine_stubs.h
//
// Control surface for the engine stubs the sweep phases call. Shared by
// tests/test_sweep_core.c (which links Oni_Sweep.c and so needs every
// external satisfied, whether or not it exercises them) and
// tests/test_sweep_phases.c (which drives them).
//
// The stubs themselves live in test_sweep_engine_stubs.c and include the
// real engine headers, so a signature that drifts from the engine's is a
// compile error rather than a silent link against the wrong shape.
// ======================================================================
#pragma once
#ifndef TEST_SWEEP_ENGINE_STUBS_H
#define TEST_SWEEP_ENGINE_STUBS_H

#include "BFW.h"

/* Reset every knob and counter below to its default. */
void SweepStubs_Reset(void);

/* --- ONrLevel_Load ---------------------------------------------------- */
extern UUtError		SweepStub_LoadResult;		/* returned by ONrLevel_Load */
extern int			SweepStub_LoadCalls;
extern UUtUns16		SweepStub_LoadLevel;		/* level asked for, last call */
extern UUtBool		SweepStub_LoadProgressBar;	/* progress-bar flag, last call */
/* When UUcTrue (the default) a successful load publishes a level instance. */
extern UUtBool		SweepStub_LoadPublishesLevel;

/*
	The level instance ONgLevel points at after a successful load. Tests set
	its characterSetupArray before running a phase.
*/
void SweepStubs_SetLevelSetups(void *inSetupArray);

/* --- TMrInstance_GetDataPtr_List / GetTagCount ------------------------ */
extern UUtError		SweepStub_ListResult;		/* returned by GetDataPtr_List */
extern UUtUns32		SweepStub_TagCount;			/* returned by GetTagCount */
extern int			SweepStub_TagCountCalls;
/* Instances handed back by the enumeration, in order. */
void SweepStubs_SetClassList(void **inClasses, UUtUns32 inCount);

/* --- TMrInstance_GetInstanceName -------------------------------------- */
/* Name returned for inInstance; NULL is a legitimate answer to model. */
void SweepStubs_SetInstanceName(const void *inInstance, char *inName);

/* --- TRrCollection_Lookup --------------------------------------------- */
/* When UUcFalse the lookup returns NULL for every collection. */
extern UUtBool		SweepStub_CollectionHasStand;

/* --- ONrLevel_Flag_ID_To_Flag ----------------------------------------- */
extern UUtBool		SweepStub_FlagExists;
extern float		SweepStub_FlagY;

/* --- AKrNodeFromPoint -------------------------------------------------- */
extern UUtBool		SweepStub_PointInBNV;
extern float		SweepStub_LastNodeQueryY;	/* y of the last point queried */

/* --- ONrGameState_NewCharacter ---------------------------------------- */
extern UUtError		SweepStub_SpawnResult;
extern int			SweepStub_SpawnCalls;
/* Spawns that succeed before the stub starts returning UUcError_Generic.
   -1 (the default) means "never fail", modelling an unbounded table. */
extern int			SweepStub_SpawnCapacity;

/* --- particle classes (P3gClassTable / P3rGetParticleClass) ------------ */
/*
	Fill the first inCount entries of the real-shaped P3gClassTable with the
	given names and a non-NULL definition, and set P3gNumClasses. A NULL entry
	in inNames leaves that class unnamed.
*/
void SweepStubs_SetParticleClasses(char *const *inNames, UUtUns16 inCount);
/* Clear the definition pointer of one class, modelling a broken load. */
void SweepStubs_ClearParticleDefinition(UUtUns16 inIndex);
/*
	Name lookup behaviour. "ok" (the default) resolves a name to its own entry,
	which is what a healthy P3gClassLookupTable does. "miss" returns NULL for
	every name; "wrong" returns entry 0 for every name — the shape a mis-sorted
	lookup table takes.
*/
typedef enum SweepStubLookup {
	SweepStubLookup_Ok = 0,
	SweepStubLookup_Miss,
	SweepStubLookup_Wrong
} SweepStubLookup;
extern SweepStubLookup	SweepStub_ParticleLookup;

/* --- environmental particles ------------------------------------------ */
/* Env particles EPrEnumerateAllParticles walks, in order. */
void SweepStubs_SetEnvParticles(void *inParticles, UUtUns16 inCount);
extern int			SweepStub_EnvEnumerateCalls;
extern int			SweepStub_EnvNewCalls;
extern int			SweepStub_EnvStartEvents;	/* P3cEvent_Start sends */
extern int			SweepStub_EnvStopEvents;	/* P3cEvent_Stop sends */
/* When UUcFalse, EPrNewParticle leaves ->particle NULL for every env particle. */
extern UUtBool		SweepStub_EnvCreateSucceeds;

/* --- AI --------------------------------------------------------------- */
extern int			SweepStub_AIDeleteAllCalls;
extern int			SweepStub_AIStartAllCalls;
extern UUtError		SweepStub_AIStartAllResult;
extern UUtBool		SweepStub_AIStartAllPlayer;		/* args of the last call */
extern UUtBool		SweepStub_AIStartAllOverride;

/* --- BSL script database ---------------------------------------------- */
/*
	Recorded functions the script phase enumerates. Each entry is a name and an
	arity; pass arity 0 for a callable one. Names longer than the table's field
	are truncated by the stub exactly as the real recorder truncates them.
*/
void SweepStubs_ResetScriptFunctions(void);
void SweepStubs_AddScriptFunction(const char *inName, UUtUns16 inNumParams);
/* Names for which SLrDatabase_IsFunctionCall answers UUcFalse. */
void SweepStubs_SetScriptUnresolved(const char *inName);
extern UUtError		SweepStub_ScriptExecuteResult;
extern int			SweepStub_ScriptExecuteCalls;
/* Names passed to SLrScript_ExecuteOnce, in order. */
const char *SweepStubs_ScriptExecuted(int inIndex);

/* --- ONiBundlePath_ResolveStateFile ------------------------------------ */
/*
	Path handed back for any state file. Defaults to the App Support location,
	i.e. the unsandboxed case, so a test has to opt in to the script phase
	running at all.
*/
extern const char	*SweepStub_StateFilePath;
extern UUtError		SweepStub_StateFileResult;

#endif /* TEST_SWEEP_ENGINE_STUBS_H */
