/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
22.03.2006 ( March )
 Start
*/

/*
this file contains the code for the minifilter emulation
that is used for processing FSD requests received through
the driver object's dispatch functions
*/

#ifndef _FSMINIFILTER_EMUL_
#define _FSMINIFILTER_EMUL_

#include <fltKernel.h>

//-----------------------------------------------------------

//
// Macro to extract buffering method out of the device io control code
//
#ifndef METHOD_FROM_CTL_CODE
#define METHOD_FROM_CTL_CODE(ctrlCode)          ((ULONG)(ctrlCode & 3))
#endif//METHOD_FROM_CTL_CODE

//-----------------------------------------------------------

typedef enum _OC_FLT_CALLBACK_DATA_FLAGS{

    OC_FLT_CALLBACK_DATA_STACK_ALLOCATION = 0x1,
    OC_FLT_CALLBACK_DATA_ALL_FLAGS = (ULONG)(-1)

} OC_FLT_CALLBACK_DATA_FLAGS, *POC_FLT_CALLBACK_DATA_FLAGS;

//-----------------------------------------------------------

#define  OC_FLT_CALLBACK_DATA_STORE_SIGNATURE   0xFA444456

typedef struct _OC_FLT_CALLBACK_DATA_STORE{

#if DBG
    //
    // the padding to catch an illegal type casting ( see below )
    //
    CHAR    Padding[ 0x4 ];

    //
    // the signature must be OC_FLT_CALLBACK_DATA_STORE_SIGNATURE
    //
    ULONG   Signature;
#endif

    //
    // all functions receive a pointer to the 
    // FLT_CALLBACK_DATA as an input parameter,
    // it is strongly prohibited to cast the
    // FLT_CALLBACK_DATA to the _DLD_FLT_CALLBACK_DATA_STORE
    // becasue if the system's minifilter
    // is used, the buffers will be managed
    // by the system and the actual layout of the
    // buffer's structure is unknown
    //
    FLT_CALLBACK_DATA         FltCallbackData;

    //
    // the following is the opaque part, subject to change
    //
    FLT_IO_PARAMETER_BLOCK    Iopb;

    //
    // the Irp for this operation, if this is an Irp operation
    //
    PIRP                      Irp;

    //
    // post operation callback and its context
    //
    PFLT_POST_OPERATION_CALLBACK    PostOperationCallback OPTIONAL;
    PVOID                           CompletionContext OPTIONAL;

    OC_FLT_CALLBACK_DATA_FLAGS      Flags;

} OC_FLT_CALLBACK_DATA_STORE, *POC_FLT_CALLBACK_DATA_STORE;

//-----------------------------------------------------------

VOID
OcCrFsdInitMinifilterDataForIrp(
    __in PDEVICE_OBJECT    DeviceObject,
    __in PIRP              Irp,
    __inout POC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore
    );

BOOLEAN
OcCrFltEmulIs32bitProcess(
    __in PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects
    );

//-----------------------------------------------------------

__forceinline
UCHAR
OcCrFsMiniFltIrpMjCodeToArrayIndex(
    __in UCHAR    MiniFltIrpMjCode
    )
    /*
    the function converts MiniFilter code to OcCrFsmfRegisteredCallbacks arry index,
    i.e. it converts the negative codes to positive and adds IRP_MJ_MAXIMUM_FUNCTION
    to this value and leaves positive codes intact
    */
{
    return ( MiniFltIrpMjCode & 0x80 )?
             ( (~MiniFltIrpMjCode + 0x1 ) + IRP_MJ_MAXIMUM_FUNCTION ): 
             MiniFltIrpMjCode;
}

//-----------------------------------------------------------

__forceinline
VOID
OcCrFsdCopyDataStoreStructures(
    __inout POC_FLT_CALLBACK_DATA_STORE    Destination,
    __in POC_FLT_CALLBACK_DATA_STORE    Source
    )
{
    PFLT_CALLBACK_DATA    FltCallbackData;
    PVOID*                PointerToConstPonter;

    RtlCopyMemory( (PVOID)Destination, (PVOID)Source, sizeof( *Destination ) );

    //
    // modify the const data
    //
    FltCallbackData = &Destination->FltCallbackData;
    PointerToConstPonter = (PVOID*)&FltCallbackData->Iopb;
    *PointerToConstPonter = &Destination->Iopb;

}

//-------------------------------------------------------------------

__forceinline
PIRP
OcCrGetIrpForEmulatedCallbackData(
    __in PFLT_CALLBACK_DATA   FltCallbackData
    )
    /*
    returns Irp for FltCallbackData if this callback
    data describes Irp based operation or NULL otherwise
    */
{
    POC_FLT_CALLBACK_DATA_STORE    DataStore;

    DataStore = CONTAINING_RECORD( FltCallbackData, OC_FLT_CALLBACK_DATA_STORE, FltCallbackData );

#if DBG
    ASSERT( OC_FLT_CALLBACK_DATA_STORE_SIGNATURE == DataStore->Signature );
    if( NULL != DataStore->Irp )
        ASSERT( IO_TYPE_IRP == DataStore->Irp->Type );
#endif//DBG

    return DataStore->Irp;
}

//-----------------------------------------------------------

__forceinline
BOOLEAN
OcCrFltIsEmulatedCall(
    __in PCFLT_RELATED_OBJECTS FltObjects
    )
    /*
    returns TRUE if this is an emulated callback
    or FALSE otherwise
    */
{
    return (BOOLEAN)( NULL == FltObjects );
}

//-----------------------------------------------------------

#endif//_FSMINIFILTER_EMUL_
