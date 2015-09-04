/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
08.06.2007 ( June )
 Start
*/

/*
this file contains the memory
allocator for shadowing subsystem
*/
#include "struct.h"
#include "proto.h"

#define OC_CR_SHADOW_BUF_TAG    'BScO'

//------------------------------------------------

PVOID
OcCrAllocateShadowBuffer(
    __in ULONG     BufferSize,
    __in BOOLEAN   CanBePagedOut,
    __inout POC_PRIVATE_BUFFER_TYPE    BufferType
    )
{
    ASSERT( CanBePagedOut? 
             (KeGetCurrentIrql()<=APC_LEVEL):
             (KeGetCurrentIrql()<=DISPATCH_LEVEL) );

    *BufferType = OcCrPrivateBufferFromSystemPool;

    return ExAllocatePoolWithTag( CanBePagedOut? PagedPool: NonPagedPool,
                                  BufferSize,
                                  OC_CR_SHADOW_BUF_TAG );

}

//------------------------------------------------

VOID
OcCrFreeShadowBuffer(
    __in PVOID    ShadowBuffer,
    __in OC_PRIVATE_BUFFER_TYPE    BufferType
    )
{
    if( OcCrPrivateBufferFromSystemPool == BufferType )
        ExFreePoolWithTag( ShadowBuffer, OC_CR_SHADOW_BUF_TAG );
}

//------------------------------------------------
