/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
27.02.2007 ( February )
 Start
*/

/*
This file contains 
 the FSDs filter initialization code, 
 the FSD registration notification code
 the FSDs' hook code
*/
#include "struct.h"
#include "proto.h"
#include <fltKernel.h>
#include <ntddcdrm.h>

//------------------------------------------------------------

//
// the known FSD
//
static PWCHAR    g_KnownFsdNames[] = { L"\\FileSystem\\Ntfs",
                                       L"\\FileSystem\\FastFAT",
                                       L"\\FileSystem\\Cdfs",
                                       L"\\FileSystem\\Udfs",
                                       L"\\FileSystem\\RAW" };

//------------------------------------------------------------

VOID
OcCrProcessDirtyCallbackData(
    IN POC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore,
    IN POC_HOOKED_DRV_NODE_CTX    HookedDrvContext,
    IN PIO_STACK_LOCATION    PtrIrpStackLocation
    );

NTSTATUS
OcFsdMnfltrCompletion( 
    IN PVOID CompletionContext,// actually POC_CMHK_CONTEXT
    IN PVOID CallbackContext// CompletionContext->Context, 
                            // actually POC_FLT_CALLBACK_DATA_STORE
    );

//------------------------------------------------------------

VOID
OcCrFsNotificationChange(
    IN PDEVICE_OBJECT    DeviceObject,
    IN BOOLEAN           FsActive
    )
/*++
 Expected Interrupt Level (for execution) :
   below or equal DISPATCH_LEVEL

 Parameters

    DeviceObject - Pointer to the file system's device object.

    FsActive - Boolean indicating whether the file system has registered
        (TRUE) or unregistered (FALSE) itself as an active file system.
--*/
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( DeviceObject );
    ASSERT( DeviceObject->DriverObject != Global.DriverObject );
    ASSERT( NULL != Global.DriverHookerExports.HookDriver );

    //
    // hook the new FSD
    //
    if( FsActive ){

        NTSTATUS    RC;

        RC = Global.DriverHookerExports.HookDriver( DeviceObject->DriverObject );
        ASSERT( NT_SUCCESS( RC ) );
    }
}

//------------------------------------------------------------

NTSTATUS
OcCrRegistreFsRegistrationChange(
    IN PDRIVER_OBJECT    CoreDriverObject
    )
/*++
 Function: FwdFsRegistrationChange()

 Description:
   Register this driver for watching file systems coming and going.  This
   enumerates all existing file systems as well as new file systems as they
   come and go.

 Expected Interrupt Level (for execution) :
     IRQL_PASSIVE_LEVEL

  Return Value: STATUS_SUCCESS/Error.
--*/
{
    NTSTATUS    RC;
    ULONG       i;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( FALSE == Global.FsdRegistrationChangeInit );
    ASSERT( NULL != Global.DriverHookerExports.HookDriver );

    RC = IoRegisterFsRegistrationChange( CoreDriverObject,
                                         OcCrFsNotificationChange );
    if( NT_SUCCESS( RC ) ){

        Global.FsdRegistrationChangeInit = TRUE;
    }

    //
    // hook the known FSDs
    //
    for( i = 0; i < OC_STATIC_ARRAY_SIZE( g_KnownFsdNames ); i++ ){

        PDRIVER_OBJECT     pDriverObject;
        UNICODE_STRING     usDriverName;
        NTSTATUS           Status;

        RtlInitUnicodeString( &usDriverName, g_KnownFsdNames[ i ] );

        Status = ObReferenceObjectByName( &usDriverName,
                                          OBJ_CASE_INSENSITIVE,
                                          NULL,
                                          FILE_READ_ACCESS,
                                          *IoDriverObjectType,
                                          KernelMode,
                                          NULL,
                                          &pDriverObject );
        if( !NT_SUCCESS( Status ) )
            continue;

        //
        // hook the driver
        //
        Status = Global.DriverHookerExports.HookDriver( pDriverObject );
        ASSERT( NT_SUCCESS( Status ) );

        ObDereferenceObject( pDriverObject );
    }

    return RC;
}

//------------------------------------------------------------

VOID
OcCrUnRegistreFsRegistrationChangeIdempotent(
    IN PDRIVER_OBJECT    CoreDriverObject
    )
    /*
    Counterpart for OcCrRegistreFsRegistrationChange,
    Idempotent function!
    */
{
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    if( FALSE == InterlockedCompareExchange( &Global.FsdRegistrationChangeInit, FALSE, TRUE ) )
        return;

    ASSERT( FALSE == Global.FsdRegistrationChangeInit );

    IoUnregisterFsRegistrationChange( CoreDriverObject, OcCrFsNotificationChange );

}

//------------------------------------------------------------

NTSTATUS
OcCrInitializeFsdSubsystem()
{
    NTSTATUS    RC = STATUS_SUCCESS;

    //
    // initialize the lookaside list used for OC_FLT_CALLBACK_DATA_STORE
    // strucutre memory allocations
    //
    ExInitializeNPagedLookasideList( &Global.FltCallbackDataStoreLaList, 
                                     NULL, 
                                     NULL, 
                                     0x0,
                                     sizeof( OC_FLT_CALLBACK_DATA_STORE ),
                                     'DFcO',
                                     0x0 );

    Global.FsdSubsystemInit = TRUE;

    return RC;
}

//------------------------------------------------------------

NTSTATUS
OcCrStartFsdFiltering()
{
    NTSTATUS    RC;

    ASSERT( TRUE == Global.FsdSubsystemInit );
    //
    // do not pay attention to errors during minifilter registration,
    // the minifilter must be regestered before hooker, because
    // the hooker assumes this
    //
    RC = OcCrFsmfRegisterMinifilter();
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcCrRegistreFsRegistrationChange( Global.DriverObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

__exit:
    return RC;
}

//------------------------------------------------------------

VOID
OcCrUninitializeFsdSubsystemIdempotent(
    IN BOOLEAN    Wait
    )
/*
    The function must have an idempotent behavior!
    It undoes all what has been done by 
    OcCrInitializeFsdSubsystem and OcCrStartFsdFiltering
*/
{
    OcCrUnRegistreFsRegistrationChangeIdempotent( Global.DriverObject );

    OcCrFsmfUnregisterMinifilterIdempotent();

    if( FALSE == InterlockedCompareExchange( &Global.FsdSubsystemInit, FALSE, TRUE ) )
        return;

    //
    // start uninitialization
    //
    ExDeleteNPagedLookasideList( &Global.FltCallbackDataStoreLaList );
}

//------------------------------------------------------------

VOID
OcCrProcessFsdIrpRequestToHookedDriver(
    IN POC_HOOKED_DRV_NODE_CTX    HookedDrvContext,
    IN PIO_STACK_LOCATION    PtrIrpStackLocation
    )
    /*
    the function processes requests to hooked FSDs
    */
{
    NTSTATUS                      RC = STATUS_SUCCESS;
    OC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore;
    FLT_PREOP_CALLBACK_STATUS     FilterStatus;
    PFLT_PRE_OPERATION_CALLBACK   PreOperationCallback;
    PVOID                         CompletionContext;

    //
    // Check that we are called at APC_LEVEL because the requests postponing has not yet been implemented,
    // After implementing the postponing the DISPATCH_LEVEL can be used
    //
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( OcIsFlagOn( OcCrGetNodeTypeFlag( &HookedDrvContext->Common ), OcNodeCtxHookedDriverFlag ) );

    if( !OcIsFlagOn( OcCrGetNodeTypeFlag( &HookedDrvContext->Common ), OcNodeCtxHookedDriverFlag ) ){

        //
        // A severe error! 
        // The minifilter or PnP filter must not be processed by this function!
        // Do nothing and hope that the request will be processed in a normal way.
        //
        /*
        RC = STATUS_INVALID_PARAMETER;
        HookedDrvContext->Common.RequestData.Irp->IoStatus.Status = RC;
        IoCompleteRequest( HookedDrvContext->Common.Irp, IO_DISK_INCREMENT );

        //
        // the Irp has been completed with STATUS_INVALID_PARAMETER code
        //
        HookedDrvContext->Common.StatusCode = RC;
        HookedDrvContext->HookedDrvIrpDecision = OcIrpDecisionReturnStatus;
        */

#if DBG
        KeBugCheckEx( OC_CORE_BUG_UNKNOWN_NODECTX,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)HookedDrvContext,
                      (ULONG_PTR)NULL,
                      (ULONG_PTR)NULL );
#endif//DBG

        return;
    }

    //
    // initialize a callback data that is similar to minifilter's one
    //
    OcCrFsdInitMinifilterDataForIrp( HookedDrvContext->Common.OriginalDeviceObject,
                                     HookedDrvContext->Common.RequestData.Irp,
                                     &FltCallbackDataStore );

    //
    // get the registered preoperation callback
    //
    PreOperationCallback = Global.FsdMinifilter.OcCrFsmfRegisteredCallbacks[ PtrIrpStackLocation->MajorFunction ].PreOperation;
    if( NULL == PreOperationCallback ){

        //
        // nothing to do if there is no registered preoperation callback,
        // I assume thet there is slso no registered postoperation callback
        //
        ASSERT( NULL == Global.FsdMinifilter.OcCrFsmfRegisteredCallbacks[ PtrIrpStackLocation->MajorFunction ].PostOperation );

        //
        // call the original driver to process the Irp
        //
        HookedDrvContext->HookedDrvIrpDecision = OcIrpDecisionCallOriginal;
        return;
    }

    //
    // call the minifilter's preoperation callback
    //
    FilterStatus = PreOperationCallback( &FltCallbackDataStore.FltCallbackData,
                                         NULL,
                                         &CompletionContext );

    ASSERT( FLT_PREOP_SUCCESS_NO_CALLBACK == FilterStatus ||
            FLT_PREOP_SUCCESS_WITH_CALLBACK == FilterStatus );

    //
    // if dirty flag is set then process this case
    //
    if( OcIsFlagOn( FltCallbackDataStore.FltCallbackData.Flags, FLTFL_CALLBACK_DATA_DIRTY ) ){

        OcCrProcessDirtyCallbackData( &FltCallbackDataStore, HookedDrvContext, PtrIrpStackLocation );
    }

    //
    // get the post operation callback
    //
    FltCallbackDataStore.PostOperationCallback = Global.FsdMinifilter.OcCrFsmfRegisteredCallbacks[ PtrIrpStackLocation->MajorFunction ].PostOperation;

    //
    // save the postoperation completion context
    //
    FltCallbackDataStore.CompletionContext = CompletionContext;

    //
    // process the code returned from the preoperation callback
    //
    if( FLT_PREOP_SUCCESS_NO_CALLBACK == FilterStatus || 
        ( FLT_PREOP_SUCCESS_WITH_CALLBACK == FilterStatus 
          && NULL == FltCallbackDataStore.PostOperationCallback ) ){

        //
        // nothing to do
        // call the original driver to process the Irp
        //
        HookedDrvContext->HookedDrvIrpDecision = OcIrpDecisionCallOriginal;

    } else if( FLT_PREOP_SUCCESS_WITH_CALLBACK == FilterStatus ){

        POC_FLT_CALLBACK_DATA_STORE    PtrAllocatedFltCallbackDataStore;

        PtrAllocatedFltCallbackDataStore = ExAllocateFromNPagedLookasideList( &Global.FltCallbackDataStoreLaList );
        if( NULL == PtrAllocatedFltCallbackDataStore ){

            //
            // the memory has not been allocated, complete the request
            // and call the registered callback, the callback will be called
            // with the stack allocated data, the callee must be careful with
            // posponing such data
            //
            ASSERT( !"ExAllocateFromNPagedLookasideList failed for Global.FltCallbackDataStoreLaList" );

            RC = STATUS_INSUFFICIENT_RESOURCES;

            //
            // set the error
            //
            HookedDrvContext->Common.RequestData.Irp->IoStatus.Status = RC;

            //
            // call the callers callback, complete the Irp later 
            // because the Irp must be valid when the callback 
            // is being called
            //
            OcFsdMnfltrCompletion( NULL, &FltCallbackDataStore );

            //
            // complete the Irp
            //
            IoCompleteRequest( HookedDrvContext->Common.RequestData.Irp, IO_DISK_INCREMENT );

            //
            // the Irp has been completed with the RC code
            //
            HookedDrvContext->Common.StatusCode = RC;
            HookedDrvContext->HookedDrvIrpDecision = OcIrpDecisionReturnStatus;

            return;
        }

        //
        // copy the stack allocated callback data to the pool allocated one
        //
        OcCrFsdCopyDataStoreStructures( PtrAllocatedFltCallbackDataStore, &FltCallbackDataStore );

        //
        // hook a completion routine to call a callback on completion
        //
        RC = OcCmHkHookCompletionRoutineInCurrentStack( HookedDrvContext->Common.RequestData.Irp,
                                                        OcFsdMnfltrCompletion,
                                                        ( PtrAllocatedFltCallbackDataStore? 
                                                          PtrAllocatedFltCallbackDataStore: 
                                                          &FltCallbackDataStore ) );
        if( !NT_SUCCESS( RC ) ){

            //
            // The setting of the completion routine has failed.
            //

            ASSERT( !"OcCmHkHookCompletionRoutineInCurrentStack failed for OcFsdMnfltrCompletion" );

            //
            // set the error
            //
            HookedDrvContext->Common.RequestData.Irp->IoStatus.Status = RC;

            //
            // call the callers callback with the allocated data,
            // complete the Irp later because the Irp must be valid
            // when the callback is being called
            //
            OcFsdMnfltrCompletion( NULL, PtrAllocatedFltCallbackDataStore );

            //
            // complete the Irp
            //
            IoCompleteRequest( HookedDrvContext->Common.RequestData.Irp, IO_DISK_INCREMENT );

            //
            // the Irp has been completed with the RC code
            //
            HookedDrvContext->Common.StatusCode = RC;
            HookedDrvContext->HookedDrvIrpDecision = OcIrpDecisionReturnStatus;

            return;
        }

        ASSERT( NULL != PtrAllocatedFltCallbackDataStore );
        ASSERT( NT_SUCCESS( RC ) );
        ASSERT( STATUS_DELETE_PENDING != RC );

        //
        // continue the Irp processing
        //
        HookedDrvContext->HookedDrvIrpDecision = OcIrpDecisionCallOriginal;

        //
        // clear the stack flag
        //
        OcClearFlag( PtrAllocatedFltCallbackDataStore->Flags, OC_FLT_CALLBACK_DATA_STACK_ALLOCATION );

    } else if( FLT_PREOP_COMPLETE == FilterStatus ){

        ASSERT( STATUS_PENDING != FltCallbackDataStore.FltCallbackData.IoStatus.Status );
        ASSERT( ( IRP_MJ_CLOSE == FltCallbackDataStore.FltCallbackData.Iopb->MajorFunction ||
                  IRP_MJ_CLEANUP == FltCallbackDataStore.FltCallbackData.Iopb->MajorFunction )?
                  ( STATUS_SUCCESS ==  FltCallbackDataStore.FltCallbackData.IoStatus.Status ):
                  TRUE );

        HookedDrvContext->Common.RequestData.Irp->IoStatus = FltCallbackDataStore.FltCallbackData.IoStatus;
        IoCompleteRequest( HookedDrvContext->Common.RequestData.Irp, IO_DISK_INCREMENT );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( HookedDrvContext->Common.RequestData.Irp );

    } else {

        RC = STATUS_INVALID_PARAMETER;
        HookedDrvContext->Common.RequestData.Irp->IoStatus.Status = RC;
        IoCompleteRequest( HookedDrvContext->Common.RequestData.Irp, IO_DISK_INCREMENT );

        //
        // the Irp has been completed with STATUS_INVALID_PARAMETER code
        //
        HookedDrvContext->Common.StatusCode = RC;
        HookedDrvContext->HookedDrvIrpDecision = OcIrpDecisionReturnStatus;
#if DBG
        KeBugCheckEx( OC_CORE_BUG_UNKNOWN_MFLTR_STATUS_CODE,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)HookedDrvContext,
                      (ULONG_PTR)FilterStatus,
                      (ULONG_PTR)NULL );
#endif//DBG
        return;

    }

    return;
}

//------------------------------------------------------------

NTSTATUS
OcFsdMnfltrCompletion( 
    IN PVOID CompletionContext OPTIONAL,// actually POC_CMHK_CONTEXT
    IN PVOID CallbackContext// CompletionContext->Context, 
                            // actually POC_FLT_CALLBACK_DATA_STORE
    )
    /*
    The CallbackContext( i.e. POC_FLT_CALLBACK_DATA_STORE ) 
    can be allocated on the stack of the calling thread 
    if there was a severe error! In the latter case the 
    CompletionContext will be NULL.
    */
{
    FLT_PREOP_CALLBACK_STATUS      FilterStatus;
    POC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore = (POC_FLT_CALLBACK_DATA_STORE)CallbackContext;

    ASSERT( NULL != FltCallbackDataStore->Irp );

    //
    // save the completion context
    //
    FltCallbackDataStore->FltCallbackData.IoStatus = FltCallbackDataStore->Irp->IoStatus;

    //
    // call the posoperation callback
    //
    FilterStatus = 
     FltCallbackDataStore->PostOperationCallback( &FltCallbackDataStore->FltCallbackData,
                                                  NULL,
                                                  FltCallbackDataStore->CompletionContext,
                                                  0x0 );

#if DBG
    if( FLT_POSTOP_FINISHED_PROCESSING != FilterStatus ){

        KeBugCheckEx( OC_CORE_BUG_UNKNOWN_MFLTR_STATUS_CODE_ON_COMPLETION,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)FltCallbackDataStore,
                      (ULONG_PTR)FilterStatus,
                      (ULONG_PTR)NULL );
    }
#endif//DBG

    //
    // free the callback data if it has been allocated from the pool
    //
    if( !OcIsFlagOn( FltCallbackDataStore->Flags, OC_FLT_CALLBACK_DATA_STACK_ALLOCATION ) ){

        ExFreeToNPagedLookasideList( &Global.FltCallbackDataStoreLaList, FltCallbackDataStore );
    }

    if( NULL != CompletionContext )
        return OcCmHkContinueCompletion( CompletionContext );
    else
        return STATUS_SUCCESS;// only STATUS_SUCCESS or STATUS_MORE_PROCESSING_REQUIRED can be returned
}

//------------------------------------------------------------

VOID
OcCrProcessDirtyCallbackData(
    IN POC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore,
    IN POC_HOOKED_DRV_NODE_CTX    HookedDrvContext,
    IN PIO_STACK_LOCATION    PtrIrpStackLocation
    )
{
    OC_FLT_CALLBACK_DATA_STORE    TempFltCallbackDataStore;

    ASSERT( OcIsFlagOn( FltCallbackDataStore->FltCallbackData.Flags, FLTFL_CALLBACK_DATA_DIRTY ) );
    ASSERT( !"Dirty Flag processing must be implemented" );

#if DBG
    KeBugCheckEx( OC_CORE_BUG_ORPHAN_DIRTY_FLAG,
        (ULONG_PTR)__LINE__,
        (ULONG_PTR)HookedDrvContext,
        (ULONG_PTR)NULL,
        (ULONG_PTR)NULL );
#endif//DBG

    //
    // the caller must register the callback,
    // may be this is not necessary, I do not know yet
    //
    ASSERT( NULL != Global.FsdMinifilter.OcCrFsmfRegisteredCallbacks[ PtrIrpStackLocation->MajorFunction ].PostOperation );

    //
    // restore the original structure, the postoperation callback 
    // must be called with an original callback data
    //
    OcCrFsdInitMinifilterDataForIrp( HookedDrvContext->Common.OriginalDeviceObject,
                                     HookedDrvContext->Common.RequestData.Irp,
                                     &TempFltCallbackDataStore );

    //
    // change the Irp acording to changes made in the callback
    //
    // TO DO

    //
    // restore the original callback data
    //
    *FltCallbackDataStore = TempFltCallbackDataStore;

}

//------------------------------------------------------------

OC_ACCESS_RIGHTS
OcCrGetFsdRequestedAccess(
    IN POC_MINIFLTR_DRV_NODE_CTX    MnfltContext,
    IN POC_DEVICE_OBJECT     OcVolumeObject
    )
{
    OC_ACCESS_RIGHTS     RequestedAccess = DEVICE_NO_ANY_ACCESS;
    ULONG                uCreateDisposition;
    BOOLEAN              DirectDeviceOpen;
    PFLT_CALLBACK_DATA CONST     Data = MnfltContext->Common.RequestData.Data;
    PFLT_IO_PARAMETER_BLOCK      Iopb = Data->Iopb;
    OC_EN_SETUP_CLASS_GUID       SetupClassGuidIndex;
    POC_DEVICE_PROPERTY_HEADER   VolumePropertyObject;
    POC_FILE_OBJECT              OcFileObject = MnfltContext->FileObject;//may be NULL

    ASSERT( OcIsFlagOn( MnfltContext->Common.Flags, OcNodeCtxMinifilterDriverFlag ) );
    ASSERT( NULL != OcVolumeObject->DevicePropertyObject );

    VolumePropertyObject = OcGetDeviceProperty( OcVolumeObject );
    if( NULL != VolumePropertyObject )
        SetupClassGuidIndex = VolumePropertyObject->SetupClassGuidIndex;
    else
        SetupClassGuidIndex = en_OC_GUID_DEVCLASS_UNKNOWN;

    //
    // I think that the system should not call a FSD for FOs 
    // with FO_DIRECT_DEVICE_OPEN flag set if there is a 
    // mounted FSD, instead a volume device stack is used
    //
    ASSERT( (Iopb->TargetFileObject && ( OcVolumeObject->KernelDeviceObject->Vpb || 
                                         Iopb->TargetFileObject->Vpb ) )? 
                                          !OcIsFlagOn( Iopb->TargetFileObject->Flags, FO_DIRECT_DEVICE_OPEN ) : 
                                          TRUE );

    //
    // Determine whether the FO is a direct open FO.
    // There is a tricky part because I do not insert all FOs
    // in the database, so I try to determine whether the FO
    // might describe direct device open made by a user by 
    // adopting the following heuristic rules
    //  - The file name length is 0x0 ( this check is not valid if FSD removes names )
    //  - The FO is not a stream file object ( stream FOs usually don't have name even for ordinary data stream )
    //  - There is no related FO ( I can't check related FO, because the pointer might be invalid )
    //
    if( ( NULL != OcFileObject && 0x1 == OcFileObject->Flags.DirectDeviceOpen ) || 
        ( NULL == OcFileObject && 
          0x0 == Iopb->TargetFileObject->FileName.Length && 
          !OcIsFlagOn( Iopb->TargetFileObject->Flags, FO_STREAM_FILE ) &&
          NULL == Iopb->TargetFileObject->RelatedFileObject ) )
        DirectDeviceOpen = TRUE;
    else
        DirectDeviceOpen = FALSE;

    switch( Iopb->MajorFunction ){

    case IRP_MJ_CLOSE:
        break;

    case IRP_MJ_CREATE:

        ASSERT( NULL == OcFileObject );

        uCreateDisposition = ( Iopb->Parameters.Create.Options >> 24 ) & 0xFF;

        //
        // if the file object doesn't contain the name then check the related file object,
        // the RelatedFileObject field is valid only during create request.
        //
        if( 0x0 == Iopb->TargetFileObject->FileName.Length && NULL != Iopb->TargetFileObject->RelatedFileObject ){

            POC_FILE_OBJECT    OcRelatedFileObject;

            //
            // set direct device open to FALSE, the related file object defines
            // the type of open when the file object doesn't contain name
            //
            DirectDeviceOpen = FALSE;

            OcRelatedFileObject = OcHsFindContextByKeyValue( Global.PtrFoHashObject,
                                                             (ULONG_PTR)Iopb->TargetFileObject->RelatedFileObject,
                                                             OcObReferenceObject );
            if( NULL != OcRelatedFileObject ){

                //
                // if the related file object is not a direct device open 
                // then the name is present in create request, this is the 
                // name of the related file object
                //
                if( 0x0 == OcRelatedFileObject->Flags.DirectDeviceOpen )
                    DirectDeviceOpen = TRUE;

                OcObDereferenceObject( OcRelatedFileObject );
            }

        } else {

            //
            // either the related file object doesn't exist or the file object
            // has a name
            //
            DirectDeviceOpen = ( 0x0 == Iopb->TargetFileObject->FileName.Length );
        }

        //
        // remeber that this is a direct device open, this is needed
        // in case when there is no any other rights will be set but
        // I still will need to know that this is a direct open
        //
        if( TRUE == DirectDeviceOpen )
            RequestedAccess |= DIRECT_DEVICE_OPEN;

        if( OcIsFlagOn( Iopb->Parameters.Create.SecurityContext->DesiredAccess, ( DELETE | FILE_DELETE_CHILD ) ) )
            RequestedAccess |= DEVICE_DELETE;

        if( OcIsFlagOn( Iopb->Parameters.Create.Options, FILE_DELETE_ON_CLOSE ) )
            RequestedAccess |= DEVICE_DELETE;

        switch( uCreateDisposition ){

        case FILE_SUPERSEDE:
            RequestedAccess |= DEVICE_DELETE;
        case FILE_CREATE:
        case FILE_OVERWRITE:
        case FILE_OVERWRITE_IF://Ultra Edit can delete file if read/write denied
             RequestedAccess |= DEVICE_WRITE;
        case FILE_OPEN:
        case FILE_OPEN_IF:

            if( !OcIsFlagOn( Iopb->Parameters.Create.Options, FILE_DIRECTORY_FILE ) && 
                !OcIsFlagOn( Iopb->OperationFlags, SL_OPEN_TARGET_DIRECTORY ) ){

                //
                // execute the file
                //
                if( OcIsFlagOn( Iopb->Parameters.Create.SecurityContext->DesiredAccess, FILE_EXECUTE ) )
                    RequestedAccess |= DEVICE_EXECUTE;

                if( OcIsFlagOn( Iopb->Parameters.Create.SecurityContext->DesiredAccess, READ_ACCESS ) ){

                    //
                    // read access is required
                    //
                    if( DirectDeviceOpen )
                        RequestedAccess |= DEVICE_DIRECT_READ;
                    else 
                        RequestedAccess |= DEVICE_READ;

                }

                if( OcIsFlagOn( Iopb->Parameters.Create.SecurityContext->DesiredAccess, WRITE_ACCESS ) ){

                    //
                    // Write to the file, allow write access for files on the CD and DVD disks
                    // because autorun open files with a write access, write access for 
                    // the CD and DVD ROMs will be checked in the write dispatch routine.
                    //
                    if( en_GUID_DEVCLASS_CDROM != SetupClassGuidIndex ){

                        if( DirectDeviceOpen )
                            RequestedAccess |= DEVICE_DIRECT_WRITE;
                        else
                            RequestedAccess |= DEVICE_WRITE;

                    }

                }//if( DlIsFlagOn( pIrpStack->Parameters.Create.SecurityContext->DesiredAccess, WRITE_ACCESS ) ){

            } else {

                //
                // open or create a directory
                //
                if( FILE_CREATE == uCreateDisposition ){

                    //
                    // open or create a directory
                    //
                    RequestedAccess |= DEVICE_DIR_CREATE;

                } else if( FILE_OPEN == uCreateDisposition || 
                           FILE_OPEN_IF == uCreateDisposition ){

                    //
                    // open an existing directory
                    //
                    RequestedAccess |= DEVICE_DIR_LIST;

                }

            }

            if( OcIsFlagOn( Iopb->Parameters.Create.Options, FILE_DELETE_ON_CLOSE ) )
                RequestedAccess |= DEVICE_DELETE;

            break;

        default:

            //
            // create directory or file
            //
            RequestedAccess |= OcIsFlagOn( Iopb->Parameters.Create.Options, FILE_DIRECTORY_FILE ) ? DEVICE_DIR_CREATE : DEVICE_WRITE;
        }

        break;

    case IRP_MJ_READ:

        if( DirectDeviceOpen )
            RequestedAccess |= DEVICE_DIRECT_READ;

        break;

    case IRP_MJ_WRITE:

        if( DirectDeviceOpen )
            RequestedAccess |= DEVICE_DIRECT_WRITE;
        else if( en_GUID_DEVCLASS_CDROM != SetupClassGuidIndex )
            RequestedAccess |= DEVICE_WRITE;

        break;

    case IRP_MJ_SET_INFORMATION:

        switch( Iopb->Parameters.SetFileInformation.FileInformationClass ){

        case FileRenameInformation:
            //
            // rename the file
            //
            RequestedAccess |= DEVICE_RENAME;
            break;

        case FileDispositionInformation:
            //
            // delete the file
            //
            RequestedAccess |= DEVICE_DELETE | DEVICE_WRITE;
            break;
        }//switch( pIrpStack->Parameters.SetFile.FileInformationClass )
        break;

    case IRP_MJ_DIRECTORY_CONTROL:

        switch( Iopb->MinorFunction ){

        case IRP_MN_QUERY_DIRECTORY:
        //case IRP_MN_QUERY_OLE_DIRECTORY:

            switch( Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass )
            {
            case FileBothDirectoryInformation:

                if( Iopb->Parameters.DirectoryControl.QueryDirectory.FileName ){
                    //
                    // list directory
                    //
                    if( Iopb->Parameters.DirectoryControl.QueryDirectory.FileName->Buffer[0] == '*' )
                        RequestedAccess |= DEVICE_DIR_LIST;
                }

                break;
            }//switch( Iopb->QueryDirectory.FileInformationClass )

            break;
        }//switch( Iopb->MinorFunction )

        break;

    case IRP_MJ_FILE_SYSTEM_CONTROL:

        switch( Iopb->Parameters.DeviceIoControl.Common.IoControlCode ){

            case FSCTL_MOVE_FILE:
                //
                // move the file
                //
                RequestedAccess |= DEVICE_WRITE;
                break;

            case FSCTL_GET_VOLUME_BITMAP://obtain a map of a volume's free and in-use clusters
            case FSCTL_GET_RETRIEVAL_POINTERS://obtain a map of a file's cluster usage

                RequestedAccess |= DEVICE_VOLUME_DEFRAGMENT;
                break;

            case FSCTL_ALLOW_EXTENDED_DASD_IO:

                //
                // Quick format, 
                // IoControlCode==0x90083- device FILE_DEVICE_FILE_SYSTEM==0x9, 
                // IOCTL code FSCTL_ALLOW_EXTENDED_DASD_IO==0x32, method 3
                //
                RequestedAccess |= DEVICE_DISK_FORMAT;
                break;
        }//switch( pIrpStack->Parameters.DeviceIoControl.IoControlCode )
        break;

    case IRP_MJ_DEVICE_CONTROL:
    case IRP_MJ_INTERNAL_DEVICE_CONTROL:

        switch( Iopb->Parameters.DeviceIoControl.Common.IoControlCode ){

        case IOCTL_CDROM_PLAY_AUDIO_MSF:

            //
            // play an audio cd
            //
            RequestedAccess |= DEVICE_PLAY_AUDIO_CD;
            break;
        }//switch( pIrpStack->Parameters.DeviceIoControl.IoControlCode )

        //
        // TO DO, call only for a direct disk/volume open, i.e. w/o 
        // trailing name in a file object
        //
        RequestedAccess |= OcCrGetDiskIoctlRequestedAccess( &MnfltContext->Common );

        break;
    }

    //
    // REMOVE THIS
    //
#if DBG
    RequestedAccess |= DEVICE_READ;
#endif//DBG

    return RequestedAccess;
}

//------------------------------------------------------------

