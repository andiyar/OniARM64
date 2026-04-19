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

#include <string.h>

#if UUmPlatform_PointerSize == 8

TMtLayoutDescriptor*
TMrBridge_BuildDescriptor(
    TMtTemplateDefinition*  inTemplate)
{
    (void)inTemplate;
    return NULL; /* stub — implemented in later tasks */
}

UUtError
TMrBridge_ValidateDescriptor(
    TMtTemplateDefinition*  inTemplate,
    TMtLayoutDescriptor*    inDescriptor,
    UUtUns32                inCompilerSize)
{
    (void)inTemplate;
    (void)inDescriptor;
    (void)inCompilerSize;
    return UUcError_None; /* stub */
}

void
TMrBridge_DisposeDescriptor(
    TMtLayoutDescriptor*    inDescriptor)
{
    (void)inDescriptor; /* stub */
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
