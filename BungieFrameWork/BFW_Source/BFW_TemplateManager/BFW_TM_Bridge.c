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
#include <stdlib.h>
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
            /* On-disk: no base elements — the count*elem data starts AT
               src_offset and extends for runtime-determined bytes. */
            f->src_size   = 0;
            /* In-memory: C sizeof(STRUCT) counts the 1-element stub
               declared as `tm_vararray T field[1]` — include it in the
               base size so the descriptor matches compiler sizeof. The
               runtime translator overwrites the stub with the first
               real element when count >= 1. */
            f->dst_size   = elem_dst;
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

            /* Src cursor stays put — on-disk base ends here and var-array
               content begins. Dst cursor advances by one element for the
               in-memory stub that C sizeof(STRUCT) counts. */
            ioState->dst_cursor = f->dst_offset + f->dst_size;
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

    /* Derive body alignment from body fields only (skipping the 2 fixed
       preamble 4Byte fields at the head of every template). The compiler's
       sizeof(STRUCT) rounds the body up to max-alignment of body members;
       the preamble is not declared in the C struct, so its 4-byte alignment
       must not inflate the struct's trailing pad.

       Without this, templates whose body max-alignment is strictly less than
       4 (e.g. ONSA: pad0[22] + UUtUns16 + UUtUns16 vararray, body-align=2)
       get rounded to 4 and report an extra 2 bytes vs compiler sizeof. */
    UUtUns32 body_alignment = 1;
    for (UUtUns32 i = 2; i < state.num_fields; i++) {
        TMtFieldDescriptor* f = &state.fields[i];
        UUtUns32 field_align;
        if (f->sub != NULL) {
            field_align = f->sub->alignment;
        } else {
            /* Scalars: alignment == dst_size (1,2,4,8). Pointers widen to 8. */
            field_align = f->dst_size;
        }
        if (field_align > body_alignment) body_alignment = field_align;
    }

    /* Round only the body (after the 8-byte preamble) up to body_alignment. */
    UUtUns32 body_cursor = (state.dst_cursor > TMcPreDataSize)
                         ? (state.dst_cursor - TMcPreDataSize) : 0;
    UUtUns32 dst_size = TMcPreDataSize + iAlignUp(body_cursor, body_alignment);

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

/* ------------------------------------------------------------------------
 * Diagnostic dump helpers (Phase 2B spike).
 *
 * Gated by ONI_BRIDGE_DUMP_FIELDS env var at validate time. Not committed —
 * to be removed or gated off once walker-rule fixes land.
 * ------------------------------------------------------------------------ */

static const char*
iFieldKindName(UUtUns8 kind)
{
    switch (kind) {
    case TMcFieldKind_1Byte:         return "1Byte";
    case TMcFieldKind_2Byte:         return "2Byte";
    case TMcFieldKind_4Byte:         return "4Byte";
    case TMcFieldKind_8Byte:         return "8Byte";
    case TMcFieldKind_RawPtr:        return "RawPtr";
    case TMcFieldKind_TemplatePtr:   return "TemplatePtr";
    case TMcFieldKind_SeparateIndex: return "SeparateIndex";
    case TMcFieldKind_NestedStruct:  return "NestedStruct";
    case TMcFieldKind_FixedArray:    return "FixedArray";
    case TMcFieldKind_VarArray:      return "VarArray";
    default:                         return "?";
    }
}

static void
iDumpDescriptor(
    const char*             inTag,
    TMtLayoutDescriptor*    inDesc,
    UUtUns32                inDepth)
{
    /* Indent string: up to 8 levels of two-space indent. */
    static const char* kIndent[] = {
        "", "  ", "    ", "      ", "        ",
        "          ", "            ", "              ", "                "
    };
    const char* ind = kIndent[(inDepth < 8) ? inDepth : 8];

    UUrStartupMessage(
        "[bridge-dump] %s%s descriptor num_fields=%u src_size=%u dst_size=%u alignment=%u",
        ind, inTag,
        (unsigned)inDesc->num_fields,
        (unsigned)inDesc->src_size,
        (unsigned)inDesc->dst_size,
        (unsigned)inDesc->alignment);

    for (UUtUns32 i = 0; i < inDesc->num_fields; i++) {
        TMtFieldDescriptor* f = &inDesc->fields[i];
        UUrStartupMessage(
            "[bridge-dump] %s  [%u] kind=%s src_off=%u dst_off=%u src_size=%u dst_size=%u count=%u",
            ind, (unsigned)i,
            iFieldKindName(f->kind),
            (unsigned)f->src_offset,
            (unsigned)f->dst_offset,
            (unsigned)f->src_size,
            (unsigned)f->dst_size,
            (unsigned)f->count);
        if (f->sub != NULL) {
            iDumpDescriptor(inTag, f->sub, inDepth + 1);
        }
    }
}

static void
iDumpSwapCodes(const char* inTag, UUtUns8* inCodes)
{
    /* Dump 128 bytes as space-separated hex, in 16-byte chunks. */
    char buf[16 * 3 + 1];
    for (UUtUns32 chunk = 0; chunk < 8; chunk++) {
        char* p = buf;
        for (UUtUns32 j = 0; j < 16; j++) {
            UUtUns8 b = inCodes[chunk * 16 + j];
            const char* hex = "0123456789abcdef";
            *p++ = hex[(b >> 4) & 0xF];
            *p++ = hex[b & 0xF];
            *p++ = ' ';
        }
        *p = 0;
        UUrStartupMessage("[bridge-dump] %s swapCodes[%02u]: %s",
            inTag, (unsigned)(chunk * 16), buf);
    }
}

UUtError
TMrBridge_ValidateDescriptor(
    TMtTemplateDefinition*  inTemplate,
    TMtLayoutDescriptor*    inDescriptor,
    UUtUns32                inCompilerSize)
{
    /* dst_size includes the 8-byte TMcPreDataSize preamble at the head of
       every on-disk record; compiler sizeof(STRUCT) does not. Add it to
       the expected value so we validate struct-body layout correctly. */
    if (inDescriptor->dst_size != inCompilerSize + TMcPreDataSize) {
        char tag[5];
        tag[0] = (char)((inTemplate->tag >> 24) & 0xFF);
        tag[1] = (char)((inTemplate->tag >> 16) & 0xFF);
        tag[2] = (char)((inTemplate->tag >> 8) & 0xFF);
        tag[3] = (char)((inTemplate->tag >> 0) & 0xFF);
        tag[4] = 0;

        UUrStartupMessage(
            "[bridge] SIZE MISMATCH template %s: computed=%u compiler=%u (incl preamble: %u vs %u)",
            tag,
            (unsigned)(inDescriptor->dst_size - TMcPreDataSize),
            (unsigned)inCompilerSize,
            (unsigned)inDescriptor->dst_size,
            (unsigned)(inCompilerSize + TMcPreDataSize));

        if (getenv("ONI_BRIDGE_DUMP_FIELDS") != NULL) {
            /* Restrict dump to three representative targets to keep logs sane.
               Env var acts as both gate and allowlist selector. */
            UUtBool dump =
                (strcmp(tag, "IDXA") == 0) ||
                (strcmp(tag, "Mtrl") == 0) ||
                (strcmp(tag, "TRAM") == 0) ||
                (strcmp(tag, "ONSA") == 0);
            if (dump) {
                UUrStartupMessage(
                    "[bridge-dump] === %s === template->size=%u varArrayElemSize=%u compilerSize=%u",
                    tag,
                    (unsigned)inTemplate->size,
                    (unsigned)inTemplate->varArrayElemSize,
                    (unsigned)inCompilerSize);
                iDumpSwapCodes(tag, inTemplate->swapCodes);
                iDumpDescriptor(tag, inDescriptor, 0);
                UUrStartupMessage("[bridge-dump] === end %s ===", tag);
            }
        }
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
    if (inCount == 0) return NULL;

    TMtInstanceDescriptor* dst = (TMtInstanceDescriptor*)
        UUrMemory_Block_New(inCount * sizeof(TMtInstanceDescriptor));
    if (dst == NULL) return NULL;

    const UUtUns8* src = (const UUtUns8*)inSrc;

    for (UUtUns32 i = 0; i < inCount; i++, src += 20) {
        UUtUns32 templatePtr32, dataPtr32, namePtr32, size32, flags32;
        memcpy(&templatePtr32, src +  0, 4);
        memcpy(&dataPtr32,     src +  4, 4);
        memcpy(&namePtr32,     src +  8, 4);
        memcpy(&size32,        src + 12, 4);
        memcpy(&flags32,       src + 16, 4);

        if (inNeedsSwapping) {
            UUrSwap_4Byte(&templatePtr32);
            UUrSwap_4Byte(&dataPtr32);
            UUrSwap_4Byte(&namePtr32);
            UUrSwap_4Byte(&size32);
            UUrSwap_4Byte(&flags32);
        }

        /* Zero-extend the 32-bit pointer-slot values into 64-bit
           destinations. These still hold file-offsets/tags at this
           stage; they get fixed up in PrepareForMemory. */
        dst[i].templatePtr = (TMtTemplateDefinition*)(uintptr_t)templatePtr32;
        dst[i].dataPtr     = (UUtUns8*)(uintptr_t)dataPtr32;
        dst[i].namePtr     = (char*)(uintptr_t)namePtr32;
        dst[i].size        = size32;
        dst[i].flags       = (TMtDescriptorFlags)flags32;
    }

    return dst;
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
