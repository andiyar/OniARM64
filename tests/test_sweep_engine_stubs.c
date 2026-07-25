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
#include "BFW_Akira.h"
#include "BFW_Totoro.h"
#include "BFW_TemplateManager.h"

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

// --- reset ------------------------------------------------------------

void SweepStubs_Reset(void)
{
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
