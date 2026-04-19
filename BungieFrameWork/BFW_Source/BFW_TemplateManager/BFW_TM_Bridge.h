#ifndef BFW_TM_BRIDGE_H
#define BFW_TM_BRIDGE_H

#include "BFW.h"
#include "BFW_TemplateManager.h"
#include "BFW_TM_Private.h"

#if UUmPlatform_PointerSize == 8

typedef enum TMtFieldKind
{
    TMcFieldKind_1Byte = 1,
    TMcFieldKind_2Byte,
    TMcFieldKind_4Byte,
    TMcFieldKind_8Byte,
    TMcFieldKind_RawPtr,
    TMcFieldKind_TemplatePtr,
    TMcFieldKind_SeparateIndex,
    TMcFieldKind_NestedStruct,
    TMcFieldKind_FixedArray,
    TMcFieldKind_VarArray
} TMtFieldKind;

typedef struct TMtFieldDescriptor
{
    UUtUns8     kind;
    UUtUns8     _pad[3];
    UUtUns32    src_offset;
    UUtUns32    dst_offset;
    UUtUns32    src_size;       /* total size across all elements for arrays */
    UUtUns32    dst_size;       /* total size across all elements for arrays */
    UUtUns32    count;          /* fixed-array element count; 0 for non-array */
    UUtUns32    _pad2;
    struct TMtLayoutDescriptor* sub;  /* element layout for arrays and nested structs */
} TMtFieldDescriptor;

typedef struct TMtLayoutDescriptor
{
    UUtUns32            num_fields;
    UUtUns32            src_size;
    UUtUns32            dst_size;
    UUtUns32            alignment;
    TMtFieldDescriptor* fields;
} TMtLayoutDescriptor;

/*
 * Build a 64-bit layout descriptor for the given template by walking its
 * swap codes and applying ARM64 ABI alignment rules. Returns NULL on
 * failure (e.g. unsupported swap code). Caller owns the returned pointer
 * and must free via TMrBridge_DisposeDescriptor.
 */
TMtLayoutDescriptor*
TMrBridge_BuildDescriptor(
    TMtTemplateDefinition*  inTemplate);

/*
 * Validate that the computed 64-bit size matches the compiler's sizeof
 * (passed as inCompilerSize). Returns UUcError_None on match,
 * UUcError_Generic on mismatch with a loud log line identifying the
 * template.
 */
UUtError
TMrBridge_ValidateDescriptor(
    TMtTemplateDefinition*  inTemplate,
    TMtLayoutDescriptor*    inDescriptor,
    UUtUns32                inCompilerSize);

/*
 * Free a layout descriptor (including nested sub-descriptors).
 */
void
TMrBridge_DisposeDescriptor(
    TMtLayoutDescriptor*    inDescriptor);

/*
 * Translate a single instance from 32-bit on-disk layout at inSrc into
 * 64-bit layout at outDst (caller-allocated, sized to inDescriptor->dst_size).
 * Performs endian swap if inNeedsSwapping is UUcTrue. For var-array
 * templates, inVarCount is the count; pass 0 for non-var-array templates.
 */
void
TMrBridge_TranslateInstance(
    TMtLayoutDescriptor*    inDescriptor,
    const void*             inSrc,
    void*                   outDst,
    UUtBool                 inNeedsSwapping,
    UUtUns32                inVarCount);

/*
 * Translate the outer TMtInstanceDescriptor array from 32-bit on-disk layout
 * to 64-bit heap layout. Outputs a fresh heap buffer of
 * inCount * sizeof(TMtInstanceDescriptor) bytes; caller owns via
 * UUrMemory_Block_Delete.
 */
TMtInstanceDescriptor*
TMrBridge_TranslateInstanceDescriptorArray(
    const void*             inSrc,
    UUtUns32                inCount,
    UUtBool                 inNeedsSwapping);

/*
 * Translate the outer TMtNameDescriptor array similarly.
 */
TMtNameDescriptor*
TMrBridge_TranslateNameDescriptorArray(
    const void*             inSrc,
    UUtUns32                inCount,
    UUtBool                 inNeedsSwapping);

/*
 * Walk a translated instance's layout descriptor and resolve each
 * TemplatePtr / RawPtr field from a 32-bit placeholder (zero-extended
 * into the low 4 bytes of the 8-byte slot) into a real 8-byte pointer.
 * For TemplatePtr, looks up the target instance via the file's instance
 * descriptor array. For RawPtr, adds the offset to inInstanceFile->rawPtr.
 * Scalars and SeparateIndex fields are left alone. Fixed/var arrays
 * recurse.
 */
struct TMtInstanceFile;
UUtError
TMrBridge_PreparePointers(
    TMtLayoutDescriptor*    inDescriptor,
    void*                   ioData,
    UUtUns32                inVarCount,
    struct TMtInstanceFile* inInstanceFile);

#endif /* UUmPlatform_PointerSize == 8 */

#endif /* BFW_TM_BRIDGE_H */
