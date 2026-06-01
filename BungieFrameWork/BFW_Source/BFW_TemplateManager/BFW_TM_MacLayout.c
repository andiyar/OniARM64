/*
	FILE:	BFW_TM_MacLayout.c

	PURPOSE: Translate Mac retail (2001) on-disk instance records into the
	         engine's 64-bit in-memory structs, for the handful of templates
	         whose Mac on-disk layout differs from the PC layout the bridge
	         compiles. Selected per source file via TMtInstanceFile.isMac at
	         the bridge translate site (BFW_TM_Game.c). See
	         docs/mac-data-support-spec.md.

	Stage 1 handles SNDD (the level-load crash). OSBD/BINA/TXMP are added in
	later stages; until then their cases return UUcFalse and fall through to
	the PC path unchanged.
*/

#include <string.h>				/* memcpy */

#include "BFW.h"
#include "BFW_TemplateManager.h"
#include "BFW_TM_Private.h"		/* TMcPreDataSize */
#include "BFW_TM_MacLayout.h"
#include "BFW_SoundSystem2.h"	/* SStSoundData, SScWaveFormat_PCM, SScSoundDataFlag_* */

/*
 * Mac SNDD on-disk body (16 bytes, little-endian), per OniSplit
 * OniMacMetadata.cs `sndd` + Sound/SoundData.cs `if (sndd.IsMacFile)`:
 *   @0  Flags     (int32)  channel count = (Flags >> 1) + 1; bit0 = compressed
 *   @4  Duration  (int32)  game-ticks
 *   @8  DataSize  (int32)  sample byte count  -> SStSoundData.num_bytes
 *   @12 DataOffset(int32)  offset into .raw   -> SStSoundData.data (resolved later)
 *
 * Samples are Apple IMA4 in the level .raw (NOT .sep), so LoadPostProcess
 * (data = rawPtr + offset) and the IMA4 decode path are unchanged; the only
 * defect was a garbage num_bytes read through the 72-byte PC SNDD layout.
 */
static void
iTranslateSNDD(
	const UUtUns8	*inSrcBody,
	SStSoundData	*outSnd,
	UUtBool			inSwap)
{
	UUtInt32	mac_flags, mac_duration, mac_datasize, mac_dataoffset;
	UUtUns32	channels;

	memcpy(&mac_flags,      inSrcBody +  0, 4);
	memcpy(&mac_duration,   inSrcBody +  4, 4);
	memcpy(&mac_datasize,   inSrcBody +  8, 4);
	memcpy(&mac_dataoffset, inSrcBody + 12, 4);

	if (inSwap)
	{
		UUrSwap_4Byte(&mac_flags);
		UUrSwap_4Byte(&mac_duration);
		UUrSwap_4Byte(&mac_datasize);
		UUrSwap_4Byte(&mac_dataoffset);
	}

	channels = ((UUtUns32)mac_flags >> 1) + 1;
	if (channels < 1) channels = 1;
	else if (channels > 2) channels = 2;

	/* Populate the shared 72-byte engine struct. wFormatTag must be != 2 so
	   SS2rPlatform_SoundChannel_SetSoundData takes the Apple-IMA4 branch, not
	   ffmpeg MS-ADPCM. nChannels drives the channel count and (via the Stereo
	   flag) SSrSound_IsStereo. The IMA path uses the SScSamplesPerSecond
	   constant for the AL buffer, so no sample rate is synthesized here. */
	outSnd->flags          = ((channels == 2) ? SScSoundDataFlag_Stereo : SScSoundDataFlag_None)
	                       | SScSoundDataFlag_MacIMA4;		/* big-endian IMA4 state words; decoder swaps */
	outSnd->f.wFormatTag   = SScWaveFormat_PCM;				/* 0x0001, != 2 */
	outSnd->f.nChannels    = (UUtUns16)channels;
	outSnd->duration_ticks = (UUtUns16)mac_duration;
	outSnd->num_bytes      = (UUtUns32)mac_datasize;
	outSnd->data           = (void *)(uintptr_t)(UUtUns32)mac_dataoffset;	/* .raw offset; LoadPostProcess adds rawPtr */
}

UUtBool
TMrMacLayout_Translate(
	UUtUns32		inTag,
	const void		*inSrcRecord,
	void			*outDstRecord,
	UUtBool			inNeedsSwapping)
{
	const UUtUns8	*src = (const UUtUns8 *)inSrcRecord;
	UUtUns8			*dst = (UUtUns8 *)outDstRecord;

	switch (inTag)
	{
		case UUm4CharToUns32('S','N','D','D'):
		{
			/* Copy the 8-byte container preamble verbatim (placeholder +
			   fileID), mirroring the PC walker's first two 4-byte fields.
			   Downstream TMrInstance_GetRawOffset reads this preamble to
			   resolve the owning instance file; if it is left zero, the
			   sample pointer resolves wrong and the fix is undone. */
			UUtUns32	pre0, pre1;
			memcpy(&pre0, src + 0, 4);
			memcpy(&pre1, src + 4, 4);
			if (inNeedsSwapping) { UUrSwap_4Byte(&pre0); UUrSwap_4Byte(&pre1); }
			memcpy(dst + 0, &pre0, 4);
			memcpy(dst + 4, &pre1, 4);

			iTranslateSNDD(src + TMcPreDataSize,
						   (SStSoundData *)(dst + TMcPreDataSize),
						   inNeedsSwapping);
			return UUcTrue;
		}

		default:
			return UUcFalse;
	}
}
