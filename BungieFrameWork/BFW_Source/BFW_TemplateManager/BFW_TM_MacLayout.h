#ifndef BFW_TM_MACLAYOUT_H
#define BFW_TM_MACLAYOUT_H

#include "BFW.h"

/* Per-file template-checksum family discriminators (the first 8 bytes of a
   TMtInstanceFile_Header). Mac and PC retail ship different on-disk layout
   families for SNDD/OSBD/BINA/TXMP; everything else shares one layout. The
   field is UUtUns64 and host-endian by the time it is compared (the header
   loader already applied any big-endian swap). */
#define TMcMacTemplateChecksum  0x0003bcdf23c13061ULL
#define TMcPCTemplateChecksum   0x0003bcdf33dc271fULL

/*
 * Translate one Mac retail on-disk instance record into its 64-bit engine
 * struct, selected by template tag. inSrcRecord and outDstRecord both point at
 * the 8-byte container preamble; the template body follows at +TMcPreDataSize.
 * Returns UUcTrue if inTag is a Mac-divergent template this function handled
 * (the caller then skips the normal PC bridge translate), or UUcFalse to fall
 * through to the PC path unchanged.
 */
UUtBool
TMrMacLayout_Translate(
	UUtUns32		inTag,
	const void		*inSrcRecord,
	void			*outDstRecord,
	UUtBool			inNeedsSwapping);

#endif /* BFW_TM_MACLAYOUT_H */
