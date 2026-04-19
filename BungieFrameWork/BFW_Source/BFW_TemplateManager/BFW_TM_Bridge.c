/*
 * FILE: BFW_TM_Bridge.c
 *
 * Bridge 32-bit on-disk template-instance data to 64-bit in-memory layout.
 * See docs/superpowers/specs/2026-04-19-template-instance-32to64-bridge-design.md
 */

#include "BFW.h"
#include "BFW_TemplateManager.h"
#include "BFW_TM_Private.h"
#include "BFW_TM_Bridge.h"

#include <stdio.h>
#include <string.h>

#if UUmPlatform_PointerSize == 8

/* Maximum fields in a single template. 114 templates * average fields ~ well under this. */
#define TMcBridge_MaxFieldsPerDescriptor 512

typedef struct TMtBuildState
{
    TMtFieldDescriptor  fields[TMcBridge_MaxFieldsPerDescriptor];
    UUtUns32            num_fields;
    UUtUns32            src_cursor;
    UUtUns32            dst_cursor;
    UUtUns32            alignment;      /* running max alignment of fields added so far */
    UUtBool             overflowed;
} TMtBuildState;

static UUtUns32
iAlignUp(UUtUns32 value, UUtUns32 alignment)
{
    UUmAssert(alignment > 0);
    return (value + alignment - 1) & ~(alignment - 1);
}

static void
iBuildState_Init(TMtBuildState* ioState)
{
    memset(ioState, 0, sizeof(*ioState));
    ioState->alignment = 1;
}

static TMtFieldDescriptor*
iBuildState_AppendField(TMtBuildState* ioState)
{
    if (ioState->num_fields >= TMcBridge_MaxFieldsPerDescriptor) {
        ioState->overflowed = UUcTrue;
        return NULL;
    }
    TMtFieldDescriptor* f = &ioState->fields[ioState->num_fields++];
    memset(f, 0, sizeof(*f));
    return f;
}

/*
 * Append a scalar field to the descriptor. Updates src_cursor by src_size
 * (packed in on-disk format — no alignment padding on source) and
 * dst_cursor aligned up to inDstAlign then bumped by inDstSize.
 */
static UUtBool
iAppendScalar(
    TMtBuildState*  ioState,
    TMtFieldKind    inKind,
    UUtUns32        inSrcSize,
    UUtUns32        inDstSize,
    UUtUns32        inDstAlign)
{
    TMtFieldDescriptor* f = iBuildState_AppendField(ioState);
    if (f == NULL) return UUcFalse;

    f->kind       = (UUtUns8)inKind;
    f->src_offset = ioState->src_cursor;
    f->dst_offset = iAlignUp(ioState->dst_cursor, inDstAlign);
    f->src_size   = inSrcSize;
    f->dst_size   = inDstSize;
    f->sub        = NULL;

    ioState->src_cursor = f->src_offset + inSrcSize;
    ioState->dst_cursor = f->dst_offset + inDstSize;
    if (inDstAlign > ioState->alignment) ioState->alignment = inDstAlign;
    return UUcTrue;
}

/*
 * Walk swap codes starting at inSwapCodes and consume them into ioState.
 * Returns pointer past the last consumed code on success, or NULL on
 * unsupported code / overflow.
 *
 * This function is incrementally extended in later tasks to cover
 * additional swap-code kinds. In this task it handles scalars only.
 */
static UUtUns8*
iWalkSwapCodes(TMtBuildState* ioState, UUtUns8* inSwapCodes)
{
    UUtUns8* cur = inSwapCodes;
    while (1) {
        UUtUns8 code = *cur++;

        switch (code) {
        case TMcSwapCode_1Byte:
            if (!iAppendScalar(ioState, TMcFieldKind_1Byte, 1, 1, 1)) return NULL;
            break;

        case TMcSwapCode_2Byte:
            if (!iAppendScalar(ioState, TMcFieldKind_2Byte, 2, 2, 2)) return NULL;
            break;

        case TMcSwapCode_4Byte:
            if (!iAppendScalar(ioState, TMcFieldKind_4Byte, 4, 4, 4)) return NULL;
            break;

        case TMcSwapCode_8Byte:
            if (!iAppendScalar(ioState, TMcFieldKind_8Byte, 8, 8, 8)) return NULL;
            break;

        case TMcSwapCode_RawPtr:
            /* 4-byte on-disk offset into rawMapping widens to an 8-byte
               pointer in memory. No extra marker in the swap-code stream. */
            if (!iAppendScalar(ioState, TMcFieldKind_RawPtr, 4, 8, 8)) return NULL;
            break;

        case TMcSwapCode_TemplatePtr:
            /* 4-byte on-disk placeholder (instance-index tag) widens to an
               8-byte pointer in memory. The swap-code stream carries an
               additional 4-byte template-tag marker immediately after this
               code — see BFW_TM_Game.c:750 (TMiGame_Instance_ByteSwap's
               TemplatePtr case: curSwapCode += 4). Consume it here so the
               next iteration reads a real swap code, not a tag byte. */
            if (!iAppendScalar(ioState, TMcFieldKind_TemplatePtr, 4, 8, 8)) return NULL;
            cur += 4;
            break;

        case TMcSwapCode_SeparateIndex:
            /* 4-byte index into the separate-data file; stays 4 bytes. */
            if (!iAppendScalar(ioState, TMcFieldKind_SeparateIndex, 4, 4, 4)) return NULL;
            break;

        case TMcSwapCode_EndArray:
        case TMcSwapCode_EndVarArray:
            /* Terminator for a nested walk. Also used at the top level:
               every gSwapCodes_XXX array in templatechecksum.c ends with
               0x06 (TMcSwapCode_EndArray), and TMiGame_Instance_ByteSwap
               stops on that same code — no extra sentinel exists. */
            return cur;

        case TMcSwapCode_BeginArray:
        {
            UUtUns8 count = *cur++;

            /* Build a sub-descriptor for a single element. */
            TMtBuildState sub_state;
            iBuildState_Init(&sub_state);

            UUtUns8* after_elem = iWalkSwapCodes(&sub_state, cur);
            if (after_elem == NULL || sub_state.overflowed) return NULL;
            cur = after_elem;  /* now positioned after EndArray */

            /* Element stride = aligned element size. */
            UUtUns32 elem_src  = sub_state.src_cursor;
            UUtUns32 elem_dst  = iAlignUp(sub_state.dst_cursor, sub_state.alignment);
            UUtUns32 elem_algn = sub_state.alignment;

            /* Place the array field in the parent. */
            TMtFieldDescriptor* f = iBuildState_AppendField(ioState);
            if (f == NULL) return NULL;

            f->kind       = (UUtUns8)TMcFieldKind_FixedArray;
            f->src_offset = ioState->src_cursor;
            f->dst_offset = iAlignUp(ioState->dst_cursor, elem_algn);
            f->src_size   = (UUtUns32)count * elem_src;
            f->dst_size   = (UUtUns32)count * elem_dst;
            f->count      = count;

            /* Allocate a heap-owned sub-descriptor holding the element layout. */
            UUtUns32 block_size = sizeof(TMtLayoutDescriptor)
                                + sub_state.num_fields * sizeof(TMtFieldDescriptor);
            TMtLayoutDescriptor* sub = (TMtLayoutDescriptor*)UUrMemory_Block_New(block_size);
            if (sub == NULL) return NULL;
            sub->num_fields = sub_state.num_fields;
            sub->src_size   = elem_src;
            sub->dst_size   = elem_dst;
            sub->alignment  = elem_algn;
            sub->fields     = (TMtFieldDescriptor*)((UUtUns8*)sub + sizeof(TMtLayoutDescriptor));
            memcpy(sub->fields, sub_state.fields,
                   sub_state.num_fields * sizeof(TMtFieldDescriptor));
            f->sub = sub;

            ioState->src_cursor = f->src_offset + f->src_size;
            ioState->dst_cursor = f->dst_offset + f->dst_size;
            if (elem_algn > ioState->alignment) ioState->alignment = elem_algn;
            break;
        }

        case TMcSwapCode_BeginVarArray:
        {
            UUtUns8 count_kind = *cur++;
            UUtUns32 count_src_size;
            UUtUns32 count_dst_size;
            UUtUns32 count_align;
            TMtFieldKind count_field_kind;

            switch (count_kind) {
            case TMcSwapCode_2Byte:
                count_src_size = 2; count_dst_size = 2; count_align = 2;
                count_field_kind = TMcFieldKind_2Byte;
                break;
            case TMcSwapCode_4Byte:
                count_src_size = 4; count_dst_size = 4; count_align = 4;
                count_field_kind = TMcFieldKind_4Byte;
                break;
            case TMcSwapCode_8Byte:
                count_src_size = 8; count_dst_size = 8; count_align = 8;
                count_field_kind = TMcFieldKind_8Byte;
                break;
            default:
                fprintf(stderr,
                    "[bridge] unsupported var-array count kind 0x%02x\n",
                    count_kind);
                return NULL;
            }

            /* The count lives in-data, so it's a regular scalar field
               from the layout's perspective. */
            if (!iAppendScalar(ioState, count_field_kind,
                               count_src_size, count_dst_size, count_align)) {
                return NULL;
            }

            /* Build sub-descriptor for one element. */
            TMtBuildState sub_state;
            iBuildState_Init(&sub_state);
            UUtUns8* after_elem = iWalkSwapCodes(&sub_state, cur);
            if (after_elem == NULL || sub_state.overflowed) return NULL;
            cur = after_elem;  /* past EndVarArray */

            UUtUns32 elem_src  = sub_state.src_cursor;
            UUtUns32 elem_dst  = iAlignUp(sub_state.dst_cursor, sub_state.alignment);
            UUtUns32 elem_algn = sub_state.alignment;

            /* Record the var-array field. Its extent is zero — the
               actual array content is sized by the runtime count. */
            TMtFieldDescriptor* f = iBuildState_AppendField(ioState);
            if (f == NULL) return NULL;

            f->kind       = (UUtUns8)TMcFieldKind_VarArray;
            f->src_offset = ioState->src_cursor;
            f->dst_offset = iAlignUp(ioState->dst_cursor, elem_algn);
            f->src_size   = 0;  /* variable */
            f->dst_size   = 0;  /* variable */
            f->count      = 0;  /* resolved per instance */

            /* Allocate sub-descriptor for the element. */
            UUtUns32 block_size = sizeof(TMtLayoutDescriptor)
                                + sub_state.num_fields * sizeof(TMtFieldDescriptor);
            TMtLayoutDescriptor* sub = (TMtLayoutDescriptor*)UUrMemory_Block_New(block_size);
            if (sub == NULL) return NULL;
            sub->num_fields = sub_state.num_fields;
            sub->src_size   = elem_src;
            sub->dst_size   = elem_dst;
            sub->alignment  = elem_algn;
            sub->fields     = (TMtFieldDescriptor*)((UUtUns8*)sub + sizeof(TMtLayoutDescriptor));
            memcpy(sub->fields, sub_state.fields,
                   sub_state.num_fields * sizeof(TMtFieldDescriptor));
            f->sub = sub;

            /* Cursors stay put — the var-array starts here but has
               unknown runtime length. The descriptor's dst_size is
               the base size; the caller adds runtime varArrayElemSize * N. */
            if (elem_algn > ioState->alignment) ioState->alignment = elem_algn;
            break;
        }

        default:
            /* unsupported in this task — handled in later tasks */
            fprintf(stderr,
                "[bridge] unsupported swap code 0x%02x during descriptor build\n",
                code);
            return NULL;
        }
    }
}

TMtLayoutDescriptor*
TMrBridge_BuildDescriptor(
    TMtTemplateDefinition*  inTemplate)
{
    TMtBuildState state;
    iBuildState_Init(&state);

    UUtUns8* end = iWalkSwapCodes(&state, inTemplate->swapCodes);
    if (end == NULL || state.overflowed) {
        return NULL;
    }

    /* Round dst_size up to overall struct alignment. */
    UUtUns32 dst_size = iAlignUp(state.dst_cursor, state.alignment);

    /* Allocate descriptor + fields as one block for easy dispose. */
    UUtUns32 block_size = sizeof(TMtLayoutDescriptor)
                        + state.num_fields * sizeof(TMtFieldDescriptor);
    TMtLayoutDescriptor* desc = (TMtLayoutDescriptor*)UUrMemory_Block_New(block_size);
    if (desc == NULL) return NULL;

    desc->num_fields = state.num_fields;
    desc->src_size   = state.src_cursor;
    desc->dst_size   = dst_size;
    desc->alignment  = state.alignment;
    desc->fields     = (TMtFieldDescriptor*)((UUtUns8*)desc + sizeof(TMtLayoutDescriptor));
    memcpy(desc->fields, state.fields,
           state.num_fields * sizeof(TMtFieldDescriptor));

    return desc;
}

UUtError
TMrBridge_ValidateDescriptor(
    TMtTemplateDefinition*  inTemplate,
    TMtLayoutDescriptor*    inDescriptor,
    UUtUns32                inCompilerSize)
{
    /* inCompilerSize is the compiler's sizeof(struct) minus TMcPreDataSize.
       inDescriptor->dst_size is our runtime-computed size of the same. */
    if (inDescriptor->dst_size != inCompilerSize) {
        UUrStartupMessage(
            "[bridge] SIZE MISMATCH template %c%c%c%c: computed=%u compiler=%u",
            (inTemplate->tag >> 24) & 0xFF,
            (inTemplate->tag >> 16) & 0xFF,
            (inTemplate->tag >> 8) & 0xFF,
            (inTemplate->tag >> 0) & 0xFF,
            (unsigned)inDescriptor->dst_size,
            (unsigned)inCompilerSize);
        return UUcError_Generic;
    }
    return UUcError_None;
}

void
TMrBridge_DisposeDescriptor(
    TMtLayoutDescriptor*    inDescriptor)
{
    if (inDescriptor == NULL) return;

    for (UUtUns32 i = 0; i < inDescriptor->num_fields; i++) {
        if (inDescriptor->fields[i].sub != NULL) {
            TMrBridge_DisposeDescriptor(inDescriptor->fields[i].sub);
        }
    }
    UUrMemory_Block_Delete(inDescriptor);
}

void
TMrBridge_TranslateInstance(
    TMtLayoutDescriptor*    inDescriptor,
    const void*             inSrc,
    void*                   outDst,
    UUtBool                 inNeedsSwapping,
    UUtUns32                inVarCount)
{
    (void)inDescriptor;
    (void)inSrc;
    (void)outDst;
    (void)inNeedsSwapping;
    (void)inVarCount;
    /* stub */
}

TMtInstanceDescriptor*
TMrBridge_TranslateInstanceDescriptorArray(
    const void*             inSrc,
    UUtUns32                inCount,
    UUtBool                 inNeedsSwapping)
{
    (void)inSrc;
    (void)inCount;
    (void)inNeedsSwapping;
    return NULL; /* stub */
}

TMtNameDescriptor*
TMrBridge_TranslateNameDescriptorArray(
    const void*             inSrc,
    UUtUns32                inCount,
    UUtBool                 inNeedsSwapping)
{
    (void)inSrc;
    (void)inCount;
    (void)inNeedsSwapping;
    return NULL; /* stub */
}

#endif /* UUmPlatform_PointerSize == 8 */
