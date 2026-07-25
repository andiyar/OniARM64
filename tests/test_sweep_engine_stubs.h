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

#endif /* TEST_SWEEP_ENGINE_STUBS_H */
