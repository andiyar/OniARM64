/*
	FILE:	Oni_Persistance.c

	AUTHOR:	Michael Evans

	CREATED: June 19, 2000

	PURPOSE: Persistant data storage

	Copyright (c) Microsoft

*/

#include "BFW.h"
#include "BFW_FileManager.h"
#include "BFW_BitVector.h"
#include "BFW_Console.h"
#include "BFW_SoundSystem2.h"

#include "Oni_Persistance.h"
#include "Oni_GameState.h"
#include "Oni_Motoko.h"
#include "Oni_Sound2.h"
#include "Oni_Particle3.h"
#include "ONi_BundlePath.h"

enum
{
	ONcOptionFlag_SubtitlesOn		= 0x0001,
	ONcOptionFlag_InvertMouseOn		= 0x0002,
	ONcOptionFlag_WonGame			= 0x0004
};

#define ONcPersistance_Version	15
#define ONcPersistance_SwapCode	0xd0d00b0e
#define ONcPersistance_FileName "persist.dat"


typedef struct ONtPersistance
{
	UUtUns32 version;
	UUtUns32 swap_code;
	UUtUns32 level_bit_field[8];
	UUtUns32 we_killed_griffen;

	// persistent UI state
	UUtUns32 weapon_bit_field;
	UUtUns32 item_bit_field;
	UUtUns32 max_diary_level;
	UUtUns32 max_diary_page;

	// game options
	ONtGraphicsQuality quality;
	float overall_volume;
	UUtUns32 option_flags;
	ONtDifficultyLevel difficulty;
	M3tDisplayMode resolution;
	float gamma;
	ONtPlace place;

	ONtContinue continues[ONcPersist_NumLevels][ONcPersist_NumContinues];			// we save up to 20 levels and 10 continues per level

} ONtPersistance;

static ONtPersistance ONgPersistance;

#if UUmPlatform == UUmPlatform_Win32
ONtGraphicsQuality ONrPersistance_GraphicsQuality_GetDefault(void)
{
	ONtGraphicsQuality default_graphics_quality;
	MEMORYSTATUS memory_status;
	UUtUns32 ram_60_megabytes = 1024 * 1024 * 60;
	UUtUns32 ram_90_megabytes = 1024 * 1024 * 90;

	GlobalMemoryStatus(&memory_status);

	if (memory_status.dwTotalPhys >= ram_90_megabytes) {
		default_graphics_quality = ONcGraphicsQuality_2;
	}
	else if (memory_status.dwTotalPhys >= ram_60_megabytes) {
		default_graphics_quality = ONcGraphicsQuality_1;
	}
	else {
		default_graphics_quality = ONcGraphicsQuality_0;
	}

	return default_graphics_quality;
}
#else
ONtGraphicsQuality ONrPersistance_GraphicsQuality_GetDefault(void)
{
	ONtGraphicsQuality default_graphics_quality = ONcGraphicsQuality_Default;

	return default_graphics_quality;
}
#endif

/*
	Issue #91: keep a copy of the file on disk before a mismatch resets it.

	ONrPersistance_Initialize clears the in-memory struct whenever persist.dat
	fails the version or swap-code check, or cannot be read at all. Nothing has
	been lost yet at that point — but the next ONrPersist(), which fires on
	something as small as a volume change, rewrites the file from that cleared
	struct, and every save point, unlock and diary page in it goes with it. The
	gap between the reset and that first write is the only window there is, so
	the copy happens inside it.

	Bytes are copied out of the file rather than written from ONgPersistance:
	the read is one of the things that may have failed, and a file written by a
	later build can be longer than the struct this build knows about. Copying
	the file keeps whatever is in it intact for a build that understands it.

	Keep-first, not newest. If a backup is already sitting there we leave it
	alone — a build that mismatches gets launched more than once, and rotating
	the backup on each launch would replace the copy holding the real progress
	with a copy of the freshly reset file.

	Logging goes through UUrStartupMessage rather than the console: console
	output feeds the #103 sweep report taps, and a line per cell would churn
	the baselines.
*/
static void ONiPersistance_BackupBeforeReset(
	char		*inPath,
	UUtBool		inReadSucceeded,
	UUtUns32	inFoundVersion)
{
	char		backup_path[BFcMaxPathLength + 32];
	BFtFile		*source;
	BFtFile		*destination;
	UUtUns32	remaining;
	UUtUns8		buffer[4096];
	int			path_length;

	if (inReadSucceeded) {
		path_length = snprintf(backup_path, sizeof(backup_path), "%s.v%u.bak", inPath, inFoundVersion);
	} else {
		path_length = snprintf(backup_path, sizeof(backup_path), "%s.unreadable.bak", inPath);
	}

	if ((path_length <= 0) || ((size_t) path_length >= (size_t) BFcMaxPathLength)) {
		/*
			BFrFileRef_Set refuses a path this long, so the existence check
			below would report "no backup" for one that is actually there,
			while the "w" fallback inside BFrFile_FOpen would go ahead and
			write anyway. Do nothing rather than risk clobbering a good copy.
		*/
		UUrStartupMessage("[persist] backup path for %s is too long; not backed up", inPath);
		return;
	}

	source = BFrFile_FOpen(inPath, "r");
	if (NULL == source) {
		UUrStartupMessage("[persist] could not reopen %s to back it up", inPath);
		return;
	}

	if (UUcError_None != BFrFile_GetLength(source, &remaining)) {
		UUrStartupMessage("[persist] could not measure %s; not backed up", inPath);
		BFrFile_Close(source);
		return;
	}

	if (0 == remaining) {
		/*
			Nothing in it to preserve. This is the ordinary case under the #103
			sweep, which seeds an empty persist.dat in every cell's working
			directory to keep the run's writes out of the player's real one;
			each of those fails the version check by design.
		*/
		BFrFile_Close(source);
		return;
	}

	destination = BFrFile_FOpen(backup_path, "r");
	if (NULL != destination) {
		BFrFile_Close(destination);
		BFrFile_Close(source);
		UUrStartupMessage("[persist] %s already exists; keeping the earlier backup", backup_path);
		return;
	}

	destination = BFrFile_FOpen(backup_path, "w");
	if (NULL == destination) {
		UUrStartupMessage("[persist] could not create %s; %s not backed up", backup_path, inPath);
		BFrFile_Close(source);
		return;
	}

	while (remaining > 0) {
		UUtUns32 chunk = (remaining < sizeof(buffer)) ? remaining : (UUtUns32) sizeof(buffer);

		if ((UUcError_None != BFrFile_Read(source, chunk, buffer)) ||
			(UUcError_None != BFrFile_Write(destination, chunk, buffer))) {
			BFtFileRef file_ref;

			BFrFile_Close(destination);
			BFrFile_Close(source);

			/*
				Drop the partial copy. Keep-first would otherwise hold on to a
				truncated backup in place of the complete one a later launch
				could have written.
			*/
			if (UUcError_None == BFrFileRef_Set(&file_ref, backup_path)) {
				BFrFile_Delete(&file_ref);
			}

			UUrStartupMessage("[persist] copying %s failed; partial backup removed", inPath);
			return;
		}

		remaining -= chunk;
	}

	BFrFile_Close(destination);
	BFrFile_Close(source);

	UUrStartupMessage("[persist] backed up %s to %s", inPath, backup_path);
}

void ONrPersistance_Initialize(void)
{
	BFtFile *stream;
	UUtError error;
	UUtBool invalid_file;
	UUtBool file_existed;
	UUtBool read_succeeded = UUcFalse;
	UUtUns32 found_version = 0;
	char path[BFcMaxPathLength];

	if (UUcError_None != ONiBundlePath_ResolveStateFile(ONcPersistance_FileName, path, sizeof(path))) {
		UUrString_Copy(path, ONcPersistance_FileName, sizeof(path));
	}
	stream = BFrFile_FOpen(path, "r");

	invalid_file = (NULL == stream);
	file_existed = (UUtBool) (NULL != stream);

	if (!invalid_file) {
		error = BFrFile_Read(stream, sizeof(ONtPersistance), &ONgPersistance);
		BFrFile_Close(stream);

		read_succeeded = (UUtBool) (UUcError_None == error);
		found_version = ONgPersistance.version;		// read before the reset below clears it
	}

	invalid_file = invalid_file || (UUcError_None != error);
	invalid_file = invalid_file || (ONcPersistance_Version != ONgPersistance.version);
	invalid_file = invalid_file || (ONcPersistance_SwapCode != ONgPersistance.swap_code);

	if (invalid_file) {
		if (file_existed) {
			if (!read_succeeded) {
				UUrStartupMessage("[persist] %s could not be read; backing it up before reset (#91)", path);
			} else if (ONcPersistance_Version != found_version) {
				UUrStartupMessage("[persist] %s is version %u, this build wants %u; backing it up before reset (#91)",
					path, found_version, (UUtUns32) ONcPersistance_Version);
			} else {
				UUrStartupMessage("[persist] %s has the wrong swap code; backing it up before reset (#91)", path);
			}

			ONiPersistance_BackupBeforeReset(path, read_succeeded, found_version);
		}

		UUrMemory_Clear(&ONgPersistance, sizeof(ONgPersistance));
		ONgPersistance.version = ONcPersistance_Version;
		ONgPersistance.swap_code = ONcPersistance_SwapCode;

		// persistant UI state
		ONgPersistance.weapon_bit_field = 0;
		ONgPersistance.item_bit_field = 0;
		ONgPersistance.max_diary_level = 0;
		ONgPersistance.max_diary_page = 0;

		// game options
		ONgPersistance.quality = ONrPersistance_GraphicsQuality_GetDefault();
		ONgPersistance.overall_volume = 1.f;
		ONgPersistance.option_flags = 0;

		if (!SS2rEnabled()) {
			// CB: enable subtitles by default only on machines without sound
			ONgPersistance.option_flags |= ONcOptionFlag_SubtitlesOn;
		}

		ONgPersistance.difficulty = ONcDifficultyLevel_Default;
		ONgPersistance.resolution.bitDepth = 32;
		ONgPersistance.resolution.width = 640;
		ONgPersistance.resolution.height = 480;
		ONgPersistance.option_flags |= ONcOptionFlag_InvertMouseOn;
		ONgPersistance.gamma = 0.5f;
		ONgPersistance.place.level = 0;
		ONgPersistance.place.save_point = 0;

		ONgPersistance.continues[1][1].continue_flags = ONcContinueFlag_Valid | ONcContinueFlag_Ignore_Restore;
		strcpy(ONgPersistance.continues[1][1].name, "Syndicate Warehouse");
	}

	return;
}

static void ONrPersist(void)
{
	char path[BFcMaxPathLength];
	if (UUcError_None != ONiBundlePath_ResolveStateFile(ONcPersistance_FileName, path, sizeof(path))) {
		UUrString_Copy(path, ONcPersistance_FileName, sizeof(path));
	}
	BFtFile *stream = BFrFile_FOpen(path, "w");
	UUtError error;

	if (NULL != stream) {
		error = BFrFile_Write(stream, sizeof(ONtPersistance), &ONgPersistance);
		BFrFile_Close(stream);
	}

	return;
}

void ONrUnlockLevel(UUtUns16 inLevel)
{
	if ((inLevel > 1) && (inLevel <= ONcPersist_NumLevels)) {
		UUtBool was_avaiable;

		was_avaiable = UUrBitVector_TestAndSetBit(ONgPersistance.level_bit_field, inLevel);

		if (!was_avaiable) {
			ONrPersist();
		}
	}
}

UUtBool ONrLevelIsUnlocked(UUtUns16 inLevel)
{
	UUtBool level_available;

	if (1 == inLevel) {
		level_available = UUcTrue;
	}
	else if (inLevel > ONcPersist_NumLevels) {
		level_available = UUcFalse;
	}
	else {
		level_available = UUrBitVector_TestBit(ONgPersistance.level_bit_field, inLevel);
	}

	return level_available;
}

void ONrWeKilledGriffen(UUtBool inMurder)
{
	if (inMurder) {
		COrConsole_Printf("we killed griffen");
	}
	else {
		COrConsole_Printf("we did not kill griffen");
	}

	ONgPersistance.we_killed_griffen = inMurder;
	ONrPersist();

	return;
}

UUtBool ONrDidWeKillGriffen(void)
{
	UUtBool murder = ONgPersistance.we_killed_griffen > 0;

	return murder;
}

// game options
ONtGraphicsQuality ONrPersist_GetGraphicsQuality(void)
{
	return ONgPersistance.quality;
}

void ONrPersist_SetGraphicsQuality(ONtGraphicsQuality inQuality)
{
	ONgPersistance.quality = inQuality;
	ONrPersist();

	// apply the changes before level reload (this is useful for authoring)
	ONrParticle3_UpdateGraphicsQuality();

	return;
}

float ONrPersist_GetOverallVolume(void)
{
	return ONgPersistance.overall_volume;
}

void ONrPersist_SetOverallVolume(float inVolume)
{
	ONgPersistance.overall_volume = inVolume;
	ONrPersist();

	SS2rVolume_Set(inVolume);

	return;
}

UUtBool ONrPersist_IsInvertMouseOn(void)
{
	UUtBool invert_mouse_on = ((ONgPersistance.option_flags & ONcOptionFlag_InvertMouseOn) != 0);

	return invert_mouse_on;
}

UUtBool ONrPersist_AreSubtitlesOn(void)
{
	UUtBool subtitles_are_on = ((ONgPersistance.option_flags & ONcOptionFlag_SubtitlesOn) != 0);

	return subtitles_are_on;
}

void ONrPersist_SetInvertMouseOn(UUtBool inOn)
{
	if (inOn) {
		ONgPersistance.option_flags |= ONcOptionFlag_InvertMouseOn;
	}
	else {
		ONgPersistance.option_flags &= ~ONcOptionFlag_InvertMouseOn;
	}

	ONrPersist();

	return;
}

void ONrPersist_SetSubtitlesOn(UUtBool inOn)
{
	if (inOn) {
		ONgPersistance.option_flags |= ONcOptionFlag_SubtitlesOn;
	}
	else {
		ONgPersistance.option_flags &= ~ONcOptionFlag_SubtitlesOn;
	}

	ONrPersist();

	return;
}


ONtDifficultyLevel ONrPersist_GetDifficulty(void)
{
	ONtDifficultyLevel difficulty_level = UUmPin(ONgPersistance.difficulty, ONcDifficultyLevel_Min, ONcDifficultyLevel_Max);

	return difficulty_level;
}

void ONrPersist_SetDifficulty(ONtDifficultyLevel inDifficulty)
{
	ONtDifficultyLevel difficulty_level = UUmPin(inDifficulty, ONcDifficultyLevel_Min, ONcDifficultyLevel_Max);

	ONgPersistance.difficulty = difficulty_level;
	ONrPersist();

	return;
}

void ONrPersist_SetResolution(M3tDisplayMode *inResolution)
{
	ONgPersistance.resolution = *inResolution;
	ONrPersist();
}

M3tDisplayMode ONrPersist_GetResolution(void)
{
	M3tDisplayMode result_resolution = ONgPersistance.resolution;

	return result_resolution;
}


const ONtContinue *ONrPersist_GetContinue(UUtInt32 inLevel, UUtInt32 inSavePoint)
{
	ONtContinue *result = NULL;

#if SHIPPING_VERSION != 0
	UUmAssert((inLevel >= 0) && (inLevel < ONcPersist_NumLevels));
#endif
	UUmAssert((inSavePoint >= 0) && (inSavePoint < ONcPersist_NumContinues));

	if ((inLevel < 0) || (inLevel >= ONcPersist_NumLevels)) {
		goto exit;
	}

	if ((inSavePoint < 0) || (inSavePoint >= ONcPersist_NumContinues)) {
		goto exit;
	}

	if (ONgPersistance.continues[inLevel][inSavePoint].continue_flags & ONcContinueFlag_Valid) {
		result = &(ONgPersistance.continues[inLevel][inSavePoint]);
	}

exit:
	return result;
}

void ONrPersist_SetContinue(UUtInt32 inLevel, UUtInt32 inSavePoint, const ONtContinue *inContinue)
{
	COrConsole_Printf("ONrPersist_SetContinue %d %d", inLevel, inSavePoint);

	UUmAssert((inLevel >= 0) && (inLevel < ONcPersist_NumLevels));
	UUmAssert((inSavePoint >= 0) && (inSavePoint < ONcPersist_NumContinues));

	if ((inLevel < 0) || (inLevel >= ONcPersist_NumLevels)) {
		goto exit;
	}

	if ((inSavePoint < 0) || (inSavePoint >= ONcPersist_NumContinues)) {
		goto exit;
	}

	ONgPersistance.continues[inLevel][inSavePoint] = *inContinue;

	ONrPersist();

exit:
	return;
}

void ONrPersist_SetGamma(float inGamma)
{
	if (inGamma < 0) {
		goto exit;
	}

	if (inGamma > 1.f) {
		goto exit;
	}

	ONgPersistance.gamma = inGamma;
	M3rSetGamma(inGamma);

	ONrPersist();

exit:
	return;
}

float ONrPersist_GetGamma(void)
{
	float gamma = ONgPersistance.gamma;

	gamma = UUmPin(gamma, 0.f, 1.f);

	return gamma;
}

const ONtPlace *ONrPersist_GetPlace(void)
{
	return &ONgPersistance.place;
}

void ONrPersist_SetPlace(const ONtPlace *inPlace)
{
	ONgPersistance.place = *inPlace;

	ONrPersist();

	return;
}

UUtBool ONrPersist_WeaponUnlocked(WPtWeaponClass *inWeaponClass)
{
	UUtUns32 weapon_id = inWeaponClass->ai_parameters.shootskill_index;

	return ((ONgPersistance.weapon_bit_field & (1 << weapon_id)) > 0);
}

void ONrPersist_UnlockWeapon(WPtWeaponClass *inWeaponClass)
{
	UUtUns32 weapon_id = inWeaponClass->ai_parameters.shootskill_index;

	if ((ONgPersistance.weapon_bit_field & (1 << weapon_id)) == 0) {
		ONgPersistance.weapon_bit_field |= (1 << weapon_id);
		ONrPersist();
	}
}

UUtBool ONrPersist_ItemUnlocked(WPtPowerupType inPowerupType)
{
	return ((ONgPersistance.item_bit_field & (1 << inPowerupType)) > 0);
}

void ONrPersist_UnlockItem(WPtPowerupType inPowerupType)
{
	if ((ONgPersistance.item_bit_field & (1 << inPowerupType)) == 0) {
		ONgPersistance.item_bit_field |= (1 << inPowerupType);
		ONrPersist();
	}
}

void ONrPersist_GetMaxDiaryPagesRead(UUtUns32 *outLevelNumber, UUtUns32 *outPageNumber)
{
	*outLevelNumber = ONgPersistance.max_diary_level;
	*outPageNumber = ONgPersistance.max_diary_page;
}

void ONrPersist_MarkDiaryPageRead(UUtUns32 inLevelNumber, UUtUns32 inPageNumber)
{
	if (inLevelNumber < ONgPersistance.max_diary_level)
		return;

	if ((inLevelNumber == ONgPersistance.max_diary_level) && (inPageNumber <= ONgPersistance.max_diary_page))
		return;

	ONgPersistance.max_diary_level = inLevelNumber;
	ONgPersistance.max_diary_page = inPageNumber;
	ONrPersist();
}

void ONrPersist_MarkWonGame(void)
{
	ONgPersistance.option_flags |= ONcOptionFlag_WonGame;
	ONrPersist();
}

UUtBool ONrPersist_GetWonGame(void)
{
	UUtBool has_won_game = (ONgPersistance.option_flags & ONcOptionFlag_WonGame) > 0;

	return has_won_game;
}
