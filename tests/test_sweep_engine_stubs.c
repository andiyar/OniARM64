// ======================================================================
// test_sweep_engine_stubs.c
//
// The nine engine externals the sweep phases call, stubbed. Includes the
// real engine headers rather than re-declaring anything, so a stub whose
// signature has drifted from the engine's fails to compile instead of
// linking against the wrong shape.
//
// Behaviour is controlled through test_sweep_engine_stubs.h. Defaults are
// the happy path: the load succeeds and publishes a level, the enumeration
// returns nothing, flags exist and sit inside a BNV, spawns succeed.
// ======================================================================

#include "test_sweep_engine_stubs.h"

#include "Oni_Level.h"
#include "Oni_Character.h"
#include "Oni_AI_Setup.h"
#include "Oni_GameState.h"
#include "Oni_GameStatePrivate.h"
#include "BFW_Akira.h"
#include "BFW_Totoro.h"
#include "BFW_TemplateManager.h"
#include "BFW_Particle3.h"
#include "BFW_EnvParticle.h"
#include "BFW_ScriptLang.h"

#include "../BungieFrameWork/BFW_Source/BFW_ScriptLang/BFW_ScriptLang_Private.h"
#include "../BungieFrameWork/BFW_Source/BFW_ScriptLang/BFW_ScriptLang_Database.h"

#include <stdio.h>
#include <string.h>

// --- level ------------------------------------------------------------

ONtLevel		*ONgLevel = NULL;

static ONtLevel	gLevel;

UUtError		SweepStub_LoadResult = UUcError_None;
int				SweepStub_LoadCalls = 0;
UUtUns16		SweepStub_LoadLevel = 0;
UUtBool			SweepStub_LoadProgressBar = UUcFalse;
UUtBool			SweepStub_LoadPublishesLevel = UUcTrue;

void SweepStubs_SetLevelSetups(void *inSetupArray)
{
	gLevel.characterSetupArray = (AItCharacterSetupArray *) inSetupArray;
}

UUtError ONrLevel_Load(UUtUns16 inLevelNum, UUtBool inProgressBar)
{
	SweepStub_LoadCalls++;
	SweepStub_LoadLevel = inLevelNum;
	SweepStub_LoadProgressBar = inProgressBar;

	if (SweepStub_LoadResult != UUcError_None) {
		return SweepStub_LoadResult;
	}

	ONgLevel = SweepStub_LoadPublishesLevel ? &gLevel : NULL;

	return UUcError_None;
}

// --- flags ------------------------------------------------------------

UUtBool			SweepStub_FlagExists = UUcTrue;
float			SweepStub_FlagY = 100.0f;

UUtBool ONrLevel_Flag_ID_To_Flag(UUtInt16 inID, ONtFlag *outFlag)
{
	if (!SweepStub_FlagExists) {
		return UUcFalse;
	}

	memset(outFlag, 0, sizeof(*outFlag));
	outFlag->idNumber = inID;
	outFlag->location.x = 1.0f;
	outFlag->location.y = SweepStub_FlagY;
	outFlag->location.z = 2.0f;

	return UUcTrue;
}

// --- akira ------------------------------------------------------------

UUtBool			SweepStub_PointInBNV = UUcTrue;
float			SweepStub_LastNodeQueryY = 0.0f;

/* Never dereferenced by the harness — only compared against NULL. */
static int		gFakeNode = 0;

AKtBNVNode *AKrNodeFromPoint(const M3tPoint3D *inPoint)
{
	SweepStub_LastNodeQueryY = inPoint->y;

	return SweepStub_PointInBNV ? (AKtBNVNode *) &gFakeNode : NULL;
}

// --- template manager -------------------------------------------------

UUtError		SweepStub_ListResult = UUcError_None;
UUtUns32		SweepStub_TagCount = 0;
int				SweepStub_TagCountCalls = 0;

static void		**gClassList = NULL;
static UUtUns32	gClassCount = 0;

void SweepStubs_SetClassList(void **inClasses, UUtUns32 inCount)
{
	gClassList = inClasses;
	gClassCount = inCount;
	SweepStub_TagCount = inCount;
}

/*
	Mirrors the real enumeration's two load-bearing behaviours: it clamps both
	the copy and the reported count at inMaxPtrs, and on error it returns
	without touching outNumPtrs at all. The second is why the harness has to
	initialise its own count — a stub that zeroed it on the way out would hide
	that requirement.
*/
UUtError TMrInstance_GetDataPtr_List(
	TMtTemplateTag	inTemplateTag,
	UUtUns32		inMaxPtrs,
	UUtUns32		*outNumPtrs,
	void			**outPtrList)
{
	UUtUns32	itr;
	UUtUns32	numPtrs;

	(void) inTemplateTag;

	if (SweepStub_ListResult != UUcError_None) {
		return SweepStub_ListResult;
	}

	numPtrs = (gClassCount < inMaxPtrs) ? gClassCount : inMaxPtrs;

	for (itr = 0; itr < numPtrs; itr++) {
		if (outPtrList != NULL) {
			outPtrList[itr] = gClassList[itr];
		}
	}

	if (outNumPtrs != NULL) {
		*outNumPtrs = numPtrs;
	}

	return UUcError_None;
}

UUtUns32 TMrInstance_GetTagCount(TMtTemplateTag inTemplateTag)
{
	(void) inTemplateTag;

	SweepStub_TagCountCalls++;

	return SweepStub_TagCount;
}

#define SweepStub_MaxNames	16

static const void	*gNameKeys[SweepStub_MaxNames];
static char			*gNameValues[SweepStub_MaxNames];
static int			gNumNames = 0;

void SweepStubs_SetInstanceName(const void *inInstance, char *inName)
{
	if (gNumNames >= SweepStub_MaxNames) {
		return;
	}

	gNameKeys[gNumNames] = inInstance;
	gNameValues[gNumNames] = inName;
	gNumNames++;
}

char *TMrInstance_GetInstanceName(const void *instanceDataPtr)
{
	int itr;

	for (itr = 0; itr < gNumNames; itr++) {
		if (gNameKeys[itr] == instanceDataPtr) {
			return gNameValues[itr];
		}
	}

	return NULL;
}

// --- totoro -----------------------------------------------------------

UUtBool			SweepStub_CollectionHasStand = UUcTrue;

static int		gFakeAnimation = 0;

const TRtAnimation *TRrCollection_Lookup(
	const TRtAnimationCollection	*inCollection,
	TRtAnimType						inType,
	TRtAnimState					inState,
	TRtAnimVarient					inFlags)
{
	(void) inCollection;
	(void) inType;
	(void) inState;
	(void) inFlags;

	return SweepStub_CollectionHasStand ? (const TRtAnimation *) &gFakeAnimation : NULL;
}

// --- character --------------------------------------------------------

UUtError		SweepStub_SpawnResult = UUcError_None;
int				SweepStub_SpawnCalls = 0;
int				SweepStub_SpawnCapacity = -1;

UUtError ONrGameState_NewCharacter(
	const OBJtObject		*inStartPosition,
	const AItCharacterSetup	*inSetup,
	const ONtFlag			*inFlag,
	UUtUns16				*outCharacterIndex)
{
	(void) inStartPosition;
	(void) inSetup;
	(void) inFlag;

	SweepStub_SpawnCalls++;

	if (SweepStub_SpawnResult != UUcError_None) {
		return SweepStub_SpawnResult;
	}

	/* Models the real cap: ONiGameState_GetFreeCharacter runs out of slots and
	   ONrGameState_NewCharacter returns UUcError_Generic rather than overrunning. */
	if ((SweepStub_SpawnCapacity >= 0) && (SweepStub_SpawnCalls > SweepStub_SpawnCapacity)) {
		return UUcError_Generic;
	}

	if (outCharacterIndex != NULL) {
		*outCharacterIndex = (UUtUns16) (SweepStub_SpawnCalls - 1);
	}

	return UUcError_None;
}

// --- particle classes -------------------------------------------------

/* Same shapes as the engine's, so a drift in either is a compile error. */
UUtUns16			P3gNumClasses = 0;
P3tParticleClass	P3gClassTable[P3cMaxParticleClasses];

SweepStubLookup		SweepStub_ParticleLookup = SweepStubLookup_Ok;

static P3tParticleDefinition	gFakeDefinition;

void SweepStubs_SetParticleClasses(char *const *inNames, UUtUns16 inCount)
{
	UUtUns16 itr;

	if (inCount > P3cMaxParticleClasses) {
		inCount = P3cMaxParticleClasses;
	}

	for (itr = 0; itr < inCount; itr++) {
		memset(&P3gClassTable[itr], 0, sizeof(P3gClassTable[itr]));

		if ((inNames != NULL) && (inNames[itr] != NULL)) {
			strncpy(P3gClassTable[itr].classname, inNames[itr],
				P3cParticleClassNameLength);
		}

		P3gClassTable[itr].definition = &gFakeDefinition;
	}

	P3gNumClasses = inCount;
}

void SweepStubs_ClearParticleDefinition(UUtUns16 inIndex)
{
	if (inIndex < P3gNumClasses) {
		P3gClassTable[inIndex].definition = NULL;
	}
}

P3tParticleClass *P3rGetParticleClass(char *inIdentifier)
{
	UUtUns16 itr;

	if (SweepStub_ParticleLookup == SweepStubLookup_Miss) {
		return NULL;
	}

	if (SweepStub_ParticleLookup == SweepStubLookup_Wrong) {
		return (P3gNumClasses > 0) ? &P3gClassTable[0] : NULL;
	}

	for (itr = 0; itr < P3gNumClasses; itr++) {
		if (strcmp(P3gClassTable[itr].classname, inIdentifier) == 0) {
			return &P3gClassTable[itr];
		}
	}

	return NULL;
}

// --- environmental particles ------------------------------------------

int			SweepStub_EnvEnumerateCalls = 0;
int			SweepStub_EnvNewCalls = 0;
int			SweepStub_EnvStartEvents = 0;
int			SweepStub_EnvStopEvents = 0;
UUtBool		SweepStub_EnvCreateSucceeds = UUcTrue;

static EPtEnvParticle	*gEnvParticles = NULL;
static UUtUns16			gNumEnvParticles = 0;

/* Never dereferenced by the harness — only compared against NULL and handed
   straight back to P3rSendEvent. */
static P3tParticle		gFakeParticle;

void SweepStubs_SetEnvParticles(void *inParticles, UUtUns16 inCount)
{
	gEnvParticles = (EPtEnvParticle *) inParticles;
	gNumEnvParticles = inCount;
}

void EPrEnumerateAllParticles(EPtEnumCallback_EnvParticle inCallback, uintptr_t inUserData)
{
	UUtUns16 itr;

	SweepStub_EnvEnumerateCalls++;

	for (itr = 0; itr < gNumEnvParticles; itr++) {
		inCallback(&gEnvParticles[itr], inUserData);
	}
}

UUtBool EPrNewParticle(EPtEnvParticle *inParticle, UUtUns32 inTime)
{
	(void) inTime;

	SweepStub_EnvNewCalls++;

	if (!SweepStub_EnvCreateSucceeds) {
		return UUcFalse;
	}

	/* The real one refuses outright when the class did not resolve, which is
	   the case the harness reports on. */
	if (inParticle->particle_class == NULL) {
		return UUcFalse;
	}

	inParticle->particle = &gFakeParticle;

	return UUcTrue;
}

UUtBool P3rSendEvent(P3tParticleClass *inClass, P3tParticle *inParticle,
					 UUtUns16 inEventIndex, float inTime)
{
	(void) inClass;
	(void) inParticle;
	(void) inTime;

	if (inEventIndex == P3cEvent_Start) {
		SweepStub_EnvStartEvents++;
	} else if (inEventIndex == P3cEvent_Stop) {
		SweepStub_EnvStopEvents++;
	}

	return UUcTrue;
}

UUtUns32 ONrGameState_GetGameTime(void)
{
	return (ONgGameState != NULL) ? ONgGameState->gameTime : 0;
}

// --- AI ---------------------------------------------------------------

int			SweepStub_AIDeleteAllCalls = 0;
int			SweepStub_AIStartAllCalls = 0;
UUtError	SweepStub_AIStartAllResult = UUcError_None;
UUtBool		SweepStub_AIStartAllPlayer = UUcFalse;
UUtBool		SweepStub_AIStartAllOverride = UUcFalse;

void AI2rDeleteAllCharacters(UUtBool inDeletePlayer)
{
	(void) inDeletePlayer;

	SweepStub_AIDeleteAllCalls++;
}

UUtError AI2rStartAllCharacters(UUtBool inStartPlayer, UUtBool inOverride)
{
	SweepStub_AIStartAllCalls++;
	SweepStub_AIStartAllPlayer = inStartPlayer;
	SweepStub_AIStartAllOverride = inOverride;

	return SweepStub_AIStartAllResult;
}

// --- BSL script database ----------------------------------------------

#define SweepStub_MaxScriptFunctions	32

static SLtRecordedScriptFunction	gScriptFunctions[SweepStub_MaxScriptFunctions];
static UUtUns32						gNumScriptFunctions = 0;

static char		gUnresolved[SweepStub_MaxScriptFunctions][SLcMaxScriptFunctionNameChars];
static int		gNumUnresolved = 0;

static char		gExecuted[SweepStub_MaxScriptFunctions][SLcMaxScriptFunctionNameChars];

UUtError	SweepStub_ScriptExecuteResult = UUcError_None;
int			SweepStub_ScriptExecuteCalls = 0;

void SweepStubs_ResetScriptFunctions(void)
{
	memset(gScriptFunctions, 0, sizeof(gScriptFunctions));
	gNumScriptFunctions = 0;
	gNumUnresolved = 0;
	memset(gExecuted, 0, sizeof(gExecuted));
	SweepStub_ScriptExecuteCalls = 0;
}

void SweepStubs_AddScriptFunction(const char *inName, UUtUns16 inNumParams)
{
	SLtRecordedScriptFunction *slot;

	if (gNumScriptFunctions >= SweepStub_MaxScriptFunctions) {
		return;
	}

	slot = &gScriptFunctions[gNumScriptFunctions++];

	if (inName != NULL) {
		strncpy(slot->name, inName, SLcMaxScriptFunctionNameChars - 1);
	}
	slot->numParams = inNumParams;
}

void SweepStubs_SetScriptUnresolved(const char *inName)
{
	if ((inName == NULL) || (gNumUnresolved >= SweepStub_MaxScriptFunctions)) {
		return;
	}

	strncpy(gUnresolved[gNumUnresolved], inName, SLcMaxScriptFunctionNameChars - 1);
	gNumUnresolved++;
}

const char *SweepStubs_ScriptExecuted(int inIndex)
{
	if ((inIndex < 0) || (inIndex >= SweepStub_ScriptExecuteCalls) ||
		(inIndex >= SweepStub_MaxScriptFunctions)) {
		return NULL;
	}

	return gExecuted[inIndex];
}

UUtUns32 SLrScript_Database_RecordedFunctions_Count(void)
{
	return gNumScriptFunctions;
}

const SLtRecordedScriptFunction *SLrScript_Database_RecordedFunctions_Get(UUtUns32 inIndex)
{
	if (inIndex >= gNumScriptFunctions) {
		return NULL;
	}

	return &gScriptFunctions[inIndex];
}

UUtBool SLrDatabase_IsFunctionCall(const char *inName)
{
	int itr;

	for (itr = 0; itr < gNumUnresolved; itr++) {
		if (strcmp(gUnresolved[itr], inName) == 0) {
			return UUcFalse;
		}
	}

	return UUcTrue;
}

UUtError SLrScript_ExecuteOnce(
	const char				*inName,
	UUtUns16				inParameterListLength,
	SLtParameter_Actual		*inParameterList,
	SLtParameter_Actual		*ioReturnValue,
	SLtContext				**ioReferencePtr)
{
	(void) inParameterListLength;
	(void) inParameterList;
	(void) ioReturnValue;
	(void) ioReferencePtr;

	if (SweepStub_ScriptExecuteCalls < SweepStub_MaxScriptFunctions) {
		strncpy(gExecuted[SweepStub_ScriptExecuteCalls], inName,
			SLcMaxScriptFunctionNameChars - 1);
	}
	SweepStub_ScriptExecuteCalls++;

	return SweepStub_ScriptExecuteResult;
}

// --- state file resolution --------------------------------------------

const char	*SweepStub_StateFilePath =
	"/Users/nobody/Library/Application Support/OniARM64/persist.dat";
UUtError	SweepStub_StateFileResult = UUcError_None;

UUtError ONiBundlePath_ResolveStateFile(const char *filename, char *outPath, size_t outPathSize)
{
	(void) filename;

	if (SweepStub_StateFileResult != UUcError_None) {
		return SweepStub_StateFileResult;
	}

	snprintf(outPath, outPathSize, "%s", SweepStub_StateFilePath);

	return UUcError_None;
}

// --- reset ------------------------------------------------------------

void SweepStubs_Reset(void)
{
	P3gNumClasses = 0;
	SweepStub_ParticleLookup = SweepStubLookup_Ok;

	gEnvParticles = NULL;
	gNumEnvParticles = 0;
	SweepStub_EnvEnumerateCalls = 0;
	SweepStub_EnvNewCalls = 0;
	SweepStub_EnvStartEvents = 0;
	SweepStub_EnvStopEvents = 0;
	SweepStub_EnvCreateSucceeds = UUcTrue;

	SweepStub_AIDeleteAllCalls = 0;
	SweepStub_AIStartAllCalls = 0;
	SweepStub_AIStartAllResult = UUcError_None;
	SweepStub_AIStartAllPlayer = UUcFalse;
	SweepStub_AIStartAllOverride = UUcFalse;

	SweepStubs_ResetScriptFunctions();
	SweepStub_ScriptExecuteResult = UUcError_None;

	SweepStub_StateFilePath =
		"/Users/nobody/Library/Application Support/OniARM64/persist.dat";
	SweepStub_StateFileResult = UUcError_None;

	ONgLevel = NULL;
	memset(&gLevel, 0, sizeof(gLevel));

	SweepStub_LoadResult = UUcError_None;
	SweepStub_LoadCalls = 0;
	SweepStub_LoadLevel = 0;
	SweepStub_LoadProgressBar = UUcFalse;
	SweepStub_LoadPublishesLevel = UUcTrue;

	SweepStub_ListResult = UUcError_None;
	SweepStub_TagCount = 0;
	SweepStub_TagCountCalls = 0;
	gClassList = NULL;
	gClassCount = 0;
	gNumNames = 0;

	SweepStub_CollectionHasStand = UUcTrue;

	SweepStub_FlagExists = UUcTrue;
	SweepStub_FlagY = 100.0f;

	SweepStub_PointInBNV = UUcTrue;
	SweepStub_LastNodeQueryY = 0.0f;

	SweepStub_SpawnResult = UUcError_None;
	SweepStub_SpawnCalls = 0;
	SweepStub_SpawnCapacity = -1;
}
