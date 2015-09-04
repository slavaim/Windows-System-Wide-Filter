/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
02.03.2007 ( March )
 Start
*/

/*
This file contains minifilter implementation for FSDs
*/
#include "struct.h"
#include "proto.h"


//-----------------------------------------------------------

FLT_PREOP_CALLBACK_STATUS
OcCrFsmfPreOperation (
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    );

FLT_PREOP_CALLBACK_STATUS
OcCrFsmfPreOperationNoPostOperation(
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    );

FLT_POSTOP_CALLBACK_STATUS
OcCrFsmfPostOperation(
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in_opt PVOID CompletionContext,
    __in FLT_POST_OPERATION_FLAGS Flags
    );

NTSTATUS
NTAPI
OcCrFsmfUnload (
    __in FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
NTAPI
OcCrFsmfInstanceSetup (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_SETUP_FLAGS Flags,
    __in DEVICE_TYPE VolumeDeviceType,
    __in FLT_FILESYSTEM_TYPE VolumeFilesystemType
    );

NTSTATUS
OcCrFsmfInstanceQueryTeardown(
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

VOID
OcCrFsmfInstanceTeardownStart (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

VOID
OcCrFsmfInstanceTeardownComplete (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

BOOLEAN
OcCrNodeFunctionMinifilterDriver(
    IN POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    IN PVOID    Context
    );

//-----------------------------------------------------------

CONST FLT_OPERATION_REGISTRATION OcCrFsmfCallbacks[] = {
    { IRP_MJ_CREATE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_CREATE_NAMED_PIPE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_CLOSE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_READ,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_WRITE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_QUERY_INFORMATION,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_SET_INFORMATION,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_QUERY_EA,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_SET_EA,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_FLUSH_BUFFERS,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_QUERY_VOLUME_INFORMATION,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_SET_VOLUME_INFORMATION,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_DIRECTORY_CONTROL,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_DEVICE_CONTROL,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_INTERNAL_DEVICE_CONTROL,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_SHUTDOWN,
      0,
      OcCrFsmfPreOperationNoPostOperation,
      NULL },//post operations not supported

    { IRP_MJ_LOCK_CONTROL,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_CLEANUP,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_CREATE_MAILSLOT,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_QUERY_SECURITY,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_SET_SECURITY,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_QUERY_QUOTA,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_SET_QUOTA,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_ACQUIRE_FOR_MOD_WRITE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_RELEASE_FOR_MOD_WRITE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_ACQUIRE_FOR_CC_FLUSH,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_RELEASE_FOR_CC_FLUSH,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_NETWORK_QUERY_OPEN,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_MDL_READ,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_MDL_READ_COMPLETE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_PREPARE_MDL_WRITE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_MDL_WRITE_COMPLETE,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_VOLUME_MOUNT,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_VOLUME_DISMOUNT,
      0,
      OcCrFsmfPreOperation,
      OcCrFsmfPostOperation },

    { IRP_MJ_OPERATION_END }
};

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = {

    sizeof( FLT_REGISTRATION ),         // Size
    FLT_REGISTRATION_VERSION,           // Version
    0,                                  // Flags

    NULL,                               // Context
    OcCrFsmfCallbacks,                  // Operation callbacks

    OcCrFsmfUnload,                     // MiniFilterUnload

    OcCrFsmfInstanceSetup,              // InstanceSetup
    OcCrFsmfInstanceQueryTeardown,      // InstanceQueryTeardown
    OcCrFsmfInstanceTeardownStart,      // InstanceTeardownStart
    OcCrFsmfInstanceTeardownComplete,   // InstanceTeardownComplete

    NULL,                               // GenerateFileName
    NULL,                               // GenerateDestinationFileName
    NULL                                // NormalizeNameComponent

};

//-----------------------------------------------------------

FLT_PREOP_CALLBACK_STATUS
OcCrFsmfPreOperation(
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is the main pre-operation dispatch routine for this
    miniFilter.
    This function could be called on the paging path!

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.
        If FltObjects is NULL the call is from the FSD hooker!

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    PFILE_OBJECT         FileObject;
    POC_DEVICE_OBJECT    PtrOcDeviceObject;
    POC_FILE_OBJECT      OcFileObject = NULL;
    OC_MINIFLTR_DRV_NODE_CTX    Context = { 0x0 };
    BOOLEAN              RequestFromMinifilter = !OcCrFltIsEmulatedCall( FltObjects );

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( ( NULL==FltObjects )? ( FALSE==RequestFromMinifilter ): ( TRUE==RequestFromMinifilter ) );

    //
    // sanity check for case of buggy driver sending FSD requests at dispatch level
    //
    if( KeGetCurrentIrql() > APC_LEVEL )
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    //
    // I am interested only in the IRP based operations,
    // processing of non Irp based operations will result
    // in an overload due to an enormous cashed requests processing( Fast IO )
    //
    if( !FLT_IS_IRP_OPERATION( Data ) )
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

#if DBG
    Context.Common.Signature = OC_NODE_CTX_SIGNATURE;
    Context.Common.Size = sizeof( Context );
#endif// DBG
    Context.Common.Flags = OcNodeCtxMinifilterDriverFlag;
    Context.Common.MiniFilterStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;
    Context.Common.RequestData.Data = Data;
    Context.Common.OriginalDeviceObject = NULL;
    Context.Common.RequestCurrentParameters.UpperDevice = 0x1;
    Context.FltObjects = FltObjects;
    if( OcCrFltEmulIs32bitProcess( Data, FltObjects ) ){

        OcSetFlag( Context.Common.Flags, OcNodeCtx32bitProcessFlag );
    }

#if DBG
    *CompletionContext = NULL;
#endif//DBG

    //
    // process a special case - the close request, the close
    // request must be successful in any case, the returned
    // code is not checked by the system
    //
    if( IRP_MJ_CLOSE == Data->Iopb->MajorFunction ){

        //
        // remove the fileobject from the hash and delete it
        //
        OcCrProcessFileObjectCloseRequest( Data->Iopb->TargetFileObject );

        //
        // nothing to do with this request in a preoperation or postoperation callbacks
        //
        ASSERT( FLT_PREOP_SUCCESS_NO_CALLBACK == Context.Common.MiniFilterStatus );
        return Context.Common.MiniFilterStatus;
    }

    //
    // get a device object on which a FSD is mounted
    //
    FileObject = Data->Iopb->TargetFileObject;
    if( NULL != FileObject )
        PtrOcDeviceObject = OcCrGetDeviceObjectOnWhichFsdMounted( FileObject );
    else
        PtrOcDeviceObject = NULL;

    if( NULL == PtrOcDeviceObject )
        return Context.Common.MiniFilterStatus;

    //
    // if the device is not started yet then do nothing with it,
    // PnP requests are processed through the callbacks registered with
    // PnP filter
    //
    if( NotStarted == PtrOcDeviceObject->PnPState )
        return Context.Common.MiniFilterStatus;

    //
    // if FOs opened on this device are under control then
    // try to retrieve a FO from the FOs database
    //
    if( 0x1 == PtrOcDeviceObject->Flags.SpyFileObjects ){

        ASSERT( NULL == Context.FileObject );

        //
        // get the file object
        //
        Context.FileObject = OcCrRetriveReferencedFileObject( Data->Iopb->TargetFileObject );

        //
        // process the case when a FSD uses stream file
        // objects for supporting FOs' shared cache map, very often
        // this stream FO also backs a segment object and therefore 
        // is used for flushing mapped portions of a data stream
        // and processing page faults
        //
        if( NULL == Context.FileObject && 
            ( IRP_MJ_WRITE == Data->Iopb->MajorFunction || IRP_MJ_READ == Data->Iopb->MajorFunction ) ){

            NTSTATUS    RC;

            //
            // the function returns a referenced file object
            //
            RC = OcCrProcessFileObjectCreating( Data->Iopb->TargetFileObject,
                                                NULL,
                                                TRUE,
                                                &Context.FileObject );
            if( !NT_SUCCESS( RC ) )
                Context.FileObject = NULL;

        }//if( NULL == Context.FileObject && 
    }


    if( NULL != Context.FileObject && 
        FALSE == RequestFromMinifilter &&
        OcCrIsRequestInFsdDeviceQueue( PtrOcDeviceObject, Data, Context.FileObject ) ){

        //
        // the operation is already in the volume list
        //
        goto __exit;
    }

    //
    // get the access requested by the caller
    //
    Context.Common.SecurityParameters.RequestedAccess = OcCrGetFsdRequestedAccess( 
                                                &Context,
                                                PtrOcDeviceObject );

    //
    // rmemeber that the direct access has been required
    //
    if( DIRECT_DEVICE_OPEN & Context.Common.SecurityParameters.RequestedAccess )
        Context.Common.RequestCurrentParameters.DirectDeviceOpenRequest = 0x1;

    //
    // nothing to do if the requested access is 0x0 and 
    // the device should not be shadowed, i.e.
    // there is no need to track all file objects
    // opend on this device( this will be checked later )
    //
    if( DEVICE_NO_ANY_ACCESS == Context.Common.SecurityParameters.RequestedAccess && 
        IRP_MJ_CREATE != Data->Iopb->MajorFunction &&
        0x0 == PtrOcDeviceObject->Flags.SpyFileObjects ){

        goto __exit;
    }

    //
    // process the request through a whole devices stack,
    // check the status returned in Context.Common.MiniFilterStatus
    // after processing, each device in the stack checks 
    // whether the request is allowed, the top device
    // also checks whether the request should be logged or shadowed
    //
    OcCrTraverseTopDown( PtrOcDeviceObject,
                         OcCrNodeFunctionMinifilterDriver,
                         (PVOID)&Context );

    //
    // only these codes are processed in the current release
    //
    ASSERT( FLT_PREOP_COMPLETE == Context.Common.MiniFilterStatus ||
            FLT_PREOP_SUCCESS_WITH_CALLBACK == Context.Common.MiniFilterStatus ||
            FLT_PREOP_SUCCESS_NO_CALLBACK == Context.Common.MiniFilterStatus );

    //
    // if one of the following codes has been returned after traversing
    // the PnP tree then the request either must be completed
    // or postponed
    //
    if( FLT_PREOP_COMPLETE == Context.Common.MiniFilterStatus ||
        FLT_PREOP_DISALLOW_FASTIO == Context.Common.MiniFilterStatus ||
        FLT_PREOP_PENDING == Context.Common.MiniFilterStatus ){

            goto __exit;
    }

    //
    // Nobody has cancelled this request, continue processing
    //

    //
    // now check whether the request should be logged or shadowed
    // in case of logging only create request is processed to track 
    // file objects
    // If the operation is direct device open - track it in any case
    //
    if( ( 0x0 == Context.Common.RequestCurrentParameters.DirectDeviceOpenRequest ) && 
        !( 0x1 == Context.Common.SecurityParameters.LogRequest && IRP_MJ_CREATE == Data->Iopb->MajorFunction ) && 
        FALSE == OcIsOperationShadowedAsWriteRequest( &Context.Common.SecurityParameters ) && 
        FALSE == OcIsOperationShadowedAsReadRequest( &Context.Common.SecurityParameters ) ){

        goto __exit;
    }

    if( NULL == Context.FileObject && IRP_MJ_CREATE != Data->Iopb->MajorFunction ){

        //
        // nothing to do without file object if this is not a create request
        //
        goto __exit;
    }

    ASSERT( OcIsOperationLogged( &Context.Common.SecurityParameters ) ||
            OcIsOperationShadowedAsReadRequest( &Context.Common.SecurityParameters ) ||
            OcIsOperationShadowedAsWriteRequest( &Context.Common.SecurityParameters ) );

    //
    // Process the request.
    // This processing is related with the shadowing
    // and logging which require the file object spying
    // and buffers processing.
    //
    switch( Data->Iopb->MajorFunction ){

        case IRP_MJ_CREATE:
            {
                NTSTATUS    RC;
                POC_FILE_OBJECT_CREATE_INFO    RefCreationInfo = NULL;

                RC = OcCrProcessFoCreateRequest( &Context.Common,
                                                 Data,
                                                 PtrOcDeviceObject,
                                                 RequestFromMinifilter? OC_REQUEST_FROM_MINIFILTER: OC_REQUEST_FROM_HOOKED_FSD,
                                                 &RefCreationInfo );
                ASSERT( NT_SUCCESS( RC ) );
                if( !NT_SUCCESS( RC ) ){

                    ASSERT( NULL == RefCreationInfo );

                    Context.Common.MiniFilterStatus = FLT_PREOP_COMPLETE;
                    Data->IoStatus.Status = RC;
                    break;
                }//if( !NT_SUCCESS( RC ) )

                ASSERT( ( OcIsOperationShadowedAsWriteRequest( &Context.Common.SecurityParameters ) ==
                          ( 0x1 == RefCreationInfo->Flags.ShadowWriteRequests ) ) || 
                        ( OcIsOperationShadowedAsWriteRequest( &Context.Common.SecurityParameters ) ==
                          ( 0x1 == RefCreationInfo->Flags.ShadowReadRequests ) ) );

                //
                // the creation info object must be dereferenced in postoperetion callback
                //
                ASSERT( NULL == *CompletionContext );
                *CompletionContext = (PVOID)RefCreationInfo;
                Context.Common.MiniFilterStatus = FLT_PREOP_SUCCESS_WITH_CALLBACK;
            }
            break;

        case IRP_MJ_WRITE:
            {

            POC_OPERATION_OBJECT    OperationObject;
            NTSTATUS                RC;

            ASSERT( OcIsOperationLogged( &Context.Common.SecurityParameters ) || 
                    OcIsOperationShadowedAsWriteRequest( &Context.Common.SecurityParameters ) );

            RC = OcCrCreateOperationObjectForFsd( PtrOcDeviceObject,
                                                  &Context,
                                                  &OperationObject );
            if( !NT_SUCCESS( RC ) )
                break;

            if( OcIsOperationShadowedAsWriteRequest( &OperationObject->SecurityParameters ) ){

                RC = OcCrShadowRequest( OperationObject );
                ASSERT( NT_SUCCESS( RC ) );
                //
                // TO DO - the error processing for the write requests shadowing
                //
            }

            if( !NT_SUCCESS( RC ) ){

                //
                // remove the allocated object
                //
                OcObDereferenceObject( OperationObject );

                break;
            }

            //
            // return the operation object as a completion context,
            // remember that it has been referenced in OcObCreateObject
            //
            ASSERT( NT_SUCCESS( RC ) );
            ASSERT( NULL == *CompletionContext );
            *CompletionContext = (PVOID)OperationObject;
            Context.Common.MiniFilterStatus = FLT_PREOP_SUCCESS_WITH_CALLBACK;

            break;
            }

        case IRP_MJ_READ:
            {

            POC_OPERATION_OBJECT    OperationObject;
            NTSTATUS                RC;

            ASSERT( 0x1 == Context.Common.SecurityParameters.LogRequest || 
                    0x1 == Context.Common.SecurityParameters.ShadowReadRequest );

            RC = OcCrCreateOperationObjectForFsd( PtrOcDeviceObject,
                                                  &Context,
                                                  &OperationObject );
            if( !NT_SUCCESS( RC ) )
                break;

            //
            // shadowing for the read requests is done on completion
            //

            //
            // return the operation object as a completion context,
            // remember that it has been referenced in OcObCreateObject
            //
            ASSERT( NT_SUCCESS( RC ) );
            ASSERT( NULL == *CompletionContext );
            *CompletionContext = (PVOID)OperationObject;
            Context.Common.MiniFilterStatus = FLT_PREOP_SUCCESS_WITH_CALLBACK;

            break;
            }

    }//switch

__exit:

    //
    // free a reference made when FO was found in
    // the database ( see OcHsFindContextByKeyValue )
    //
    if( NULL != Context.FileObject )
        OcObDereferenceObject( Context.FileObject );

    OcObDereferenceObject( PtrOcDeviceObject );

    ASSERT( ( *CompletionContext != NULL )? 
             FLT_PREOP_SUCCESS_WITH_CALLBACK == Context.Common.MiniFilterStatus :
             FLT_PREOP_SUCCESS_WITH_CALLBACK != Context.Common.MiniFilterStatus );

    return Context.Common.MiniFilterStatus;
}

//-----------------------------------------------------------

FLT_POSTOP_CALLBACK_STATUS
OcCrFsmfPostOperation(
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in_opt PVOID CompletionContext,
    __in FLT_POST_OPERATION_FLAGS Flags
    )
/*++

Routine Description:

    This routine is the post-operation completion routine for this
    miniFilter.
    This function could be called on the paging path!


Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The completion context set in the pre-operation routine.

    Flags - Denotes whether the completion is successful or is being drained.

Return Value:

    The return value is the status of the operation.

--*/
{
    BOOLEAN    RequestFromMinifilter = (BOOLEAN)(NULL != FltObjects);

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    /*
#if DBG
    if( Flags & FLTFL_POST_OPERATION_DRAINING )
        DbgBreakPoint();
#endif//DBG
        */

    switch( Data->Iopb->MajorFunction ){

        case IRP_MJ_CREATE:
            {
                NTSTATUS    RC;
                POC_FILE_OBJECT_CREATE_INFO    RefCreationInfo = (POC_FILE_OBJECT_CREATE_INFO)CompletionContext;

                RC = OcCrProcessFoCreateRequestCompletion( &Data->IoStatus,
                                                           RefCreationInfo,
                                                           Data->Iopb->TargetFileObject,
                                                           RequestFromMinifilter? OC_REQUEST_FROM_MINIFILTER: OC_REQUEST_FROM_HOOKED_FSD );

                if( !NT_SUCCESS( RC ) ){

                    //
                    // cancel the creation( IoCancelFileOpen )
                    // TO DO
                }

                //
                // dereference the creation info object referenced in 
                // the preoperation callback before returning it as a 
                // callback context
                //
                OcObDereferenceObject( RefCreationInfo );
                OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( RefCreationInfo );
            }
            break;

        case IRP_MJ_WRITE:
        case IRP_MJ_READ:
            {
                NTSTATUS    RC;
                POC_OPERATION_OBJECT    OperationObject = (POC_OPERATION_OBJECT)CompletionContext;

                ASSERT( _OC_OPERATION_OBJ_SIGNATURE == OperationObject->Signature );

                //
                // mark as processed
                //
                OperationObject->Flags.HadHisTimeOnStack = 0x1;

                //
                // cahe may has been initialized for this file inside the
                // read or write dispatch routine, check the shared cache map's 
                // file object
                //
                if( OperationObject->FileObject )
                    OcCrCheckSharedCacheMapFileObject( OperationObject->FileObject );

                //
                // shadow the read request
                //
                if( IRP_MJ_READ == Data->Iopb->MajorFunction && 
                    0x1 == OperationObject->SecurityParameters.ShadowReadRequest ){

                    RC = OcCrShadowRequest( OperationObject );
                    ASSERT( NT_SUCCESS( RC ) );
                    //
                    // TO DO - the error processing for the read requests shadowing
                    //
                }

                //
                // synchronize with the shadowing thread, if needed
                //
                RC = OcCrShadowProcessOperationBuffersForFsd( OperationObject );
                ASSERT( NT_SUCCESS( RC ) );

                //
                // the following function is called to free the MDL
                // used to lock the operation initiator's buffer, this
                // has to be done here at the completion routine as
                // the process can be completed before the MDL is
                // unlocked and the system will BSOD as the process has
                // locked pages
                //
                OcCrUnlockAndFreeMdlForOperation( OperationObject );

                //
                // dereference the operation object referenced in preoperation callback
                //
                OcObDereferenceObject( OperationObject );
            }
            break;
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}

//-----------------------------------------------------------

FLT_PREOP_CALLBACK_STATUS
OcCrFsmfPreOperationNoPostOperation(
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    )
/*++

Routine Description:

    This function could be called on the paging path!

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

//-----------------------------------------------------------

NTSTATUS
OcCrFsmfRegisterMinifilter()
/*
this function registers the minifilter and must be called at DriverEntry!
*/
{
    NTSTATUS    RC;
    ULONG       i;

    //
    // copy the minifilter registration strucuture
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( Global.FsdMinifilter.OcCrFsmfRegisteredCallbacks ); ++i ){

        UCHAR    IrpMjIndex;

        if( IRP_MJ_OPERATION_END == OcCrFsmfCallbacks[ i ].MajorFunction )
            break;

        IrpMjIndex = OcCrFsMiniFltIrpMjCodeToArrayIndex( OcCrFsmfCallbacks[ i ].MajorFunction );

        ASSERT( IrpMjIndex < OC_STATIC_ARRAY_SIZE( Global.FsdMinifilter.OcCrFsmfRegisteredCallbacks ) );

        Global.FsdMinifilter.OcCrFsmfRegisteredCallbacks[ IrpMjIndex ] = OcCrFsmfCallbacks[ i ];
    }

    if( NULL == Global.MinifilterFunctions.FltRegisterFilter ){
        //
        // there is no the minifilter manager in this system
        //
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    //
    // Register with FltMgr to tell it our callback routines
    //

    RC = Global.MinifilterFunctions.FltRegisterFilter( Global.DriverObject,
                                                       &FilterRegistration,
                                                       &Global.FsdMinifilter.FilterHandle );

    if( NT_SUCCESS( RC ) ){

        //
        // This function is called inside DriverEntry, so there is no
        // any contention, use the simple assignment
        //
        Global.FsdMinifilter.State = OC_FSD_MF_REGISTERED;

        //
        // filtering has been started at least by one filter 
        // so I can call FltGetRoutineAddress( see WDK
        // requirement "in Windows 2000 and Windows XP, before 
        // FltGetRoutineAddress is called at least one minifilter 
        // on the system must call FltRegisterFilter" ) to find
        // all needed functions
        //
        Global.MinifilterFunctions.FltIs32bitProcess = Global.MinifilterFunctions.FltGetRoutineAddress( "FltIs32bitProcess" );
        ASSERT( NULL != Global.MinifilterFunctions.FltIs32bitProcess );

        //
        // tell to the minifilter manager to call our callbacks
        //
        RC = Global.MinifilterFunctions.FltStartFiltering( Global.FsdMinifilter.FilterHandle );
        if( !NT_SUCCESS( RC ) ){

            ASSERT( !"Out of luck with the minifilter" );
            //
            // out of luck, unregister the minifilter
            //
            OcCrFsmfUnregisterMinifilterIdempotent();
        }

    }

    ASSERT( NT_SUCCESS( RC ) );

    return RC;
}

//-----------------------------------------------------------

VOID
OcCrFsmfUnregisterMinifilterIdempotent()
{

    if( OC_FSD_MF_REGISTERED != InterlockedCompareExchange( &Global.FsdMinifilter.State,
                                                            OC_FSD_MF_NOT_REGISTERED,
                                                            OC_FSD_MF_REGISTERED ) )
        return;

    ASSERT( NULL != Global.FsdMinifilter.FilterHandle );

    Global.MinifilterFunctions.FltUnregisterFilter( Global.FsdMinifilter.FilterHandle );
    Global.FsdMinifilter.FilterHandle = NULL;

    InterlockedExchange( &Global.FsdMinifilter.State, OC_FSD_MF_NOT_REGISTERED );

    return;
}

//-----------------------------------------------------------

NTSTATUS
OcCrFsmfUnload(
    __in FLT_FILTER_UNLOAD_FLAGS Flags
    )
/*++

Routine Description:

    This is the unload routine for this miniFilter driver. This is called
    when the minifilter is about to be unloaded. We can fail this unload
    request if this is not a mandatory unloaded indicated by the Flags
    parameter.

Arguments:

    Flags - Indicating if this is a mandatory unload.

Return Value:

    Returns the final status of this operation.

--*/
{
    UNREFERENCED_PARAMETER( Flags );

    OcCrFsmfUnregisterMinifilterIdempotent();

    return STATUS_SUCCESS;
}

//-----------------------------------------------------------

NTSTATUS
OcCrFsmfInstanceSetup(
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_SETUP_FLAGS Flags,
    __in DEVICE_TYPE VolumeDeviceType,
    __in FLT_FILESYSTEM_TYPE VolumeFilesystemType
    )
/*++

Routine Description:

    This routine is called whenever a new instance is created on a volume. This
    gives us a chance to decide if we need to attach to this volume or not.

    If this routine is not defined in the registration structure, automatic
    instances are alwasys created.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Flags describing the reason for this attach request.

Return Value:

    STATUS_SUCCESS - attach
    STATUS_FLT_DO_NOT_ATTACH - do not attach

--*/
{

    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
    UNREFERENCED_PARAMETER( VolumeDeviceType );
    UNREFERENCED_PARAMETER( VolumeFilesystemType );

    return STATUS_SUCCESS;
}

//-----------------------------------------------------------

NTSTATUS
OcCrFsmfInstanceQueryTeardown(
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This is called when an instance is being manually deleted by a
    call to FltDetachVolume or FilterDetach thereby giving us a
    chance to fail that detach request.

    If this routine is not defined in the registration structure, explicit
    detach requests via FltDetachVolume or FilterDetach will always be
    failed.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Indicating where this detach request came from.

Return Value:

    Returns the status of this operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    return STATUS_SUCCESS;
}

//-----------------------------------------------------------

VOID
OcCrFsmfInstanceTeardownStart (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the start of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is been deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
}

//-----------------------------------------------------------

VOID
OcCrFsmfInstanceTeardownComplete (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the end of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is been deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
}

//-----------------------------------------------------------

BOOLEAN
OcCrNodeFunctionMinifilterDriver(
    IN POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    IN PVOID    Context
    )
{
    POC_MINIFLTR_DRV_NODE_CTX    PtrContext = (POC_MINIFLTR_DRV_NODE_CTX)Context;

    ASSERT( OcIsFlagOn( PtrContext->Common.Flags, OcNodeCtxMinifilterDriverFlag ) );
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    //
    // check the access, log and shadow settings
    //
    // TO DO

    if( 0x1 == PtrContext->Common.RequestCurrentParameters.UpperDevice ){

        //
        // this is an upper device in the stack, i.e.
        // the device on which the FSD is mounted
        //

        //
        // log and shadow
        //

        //
        // suppose that I should log this request
        //
        PtrContext->Common.SecurityParameters.LogRequest = 0x1;

        //
        // make any necessary logging here!
        //

        //
        // if the device is not on a paging path - then shadow it,
        // shadowing of paging IO requests to devices on paging paths 
        // are not performed because the shadowing module uses pages 
        // backing by a page file
        //
        if( FALSE == OcCrIsDeviceOnPagingPath( PtrOcNodeDeviceObject ) ){

            //
            // check for shadowing here
            //
            // TO DO

            //
            // suppose that I should shadow this request!!!
            //
            OcMarkForShadowAsWriteOperation( &PtrContext->Common.SecurityParameters );
            OcMarkForShadowAsReadOperation( &PtrContext->Common.SecurityParameters );

            //
            // if the file object is marked as shadow then mark request as 
            // shadowed, if this is the request wghich brings nothing to
            // shadowing it will be skipped by the shadowing subystem
            //
            if( NULL != PtrContext->FileObject && 
                ( 0x1 == PtrContext->FileObject->ContextObject->Flags.ShadowWriteRequests || 
                  0x1 == PtrContext->FileObject->ContextObject->Flags.ShadowReadRequests ) ){

                    //
                    // set the operation as should be shadowed if the 
                    // file object is marked as shadowed
                    //
                    if( 0x1 == PtrContext->FileObject->ContextObject->Flags.ShadowWriteRequests )
                        OcMarkForShadowAsWriteOperation( &PtrContext->Common.SecurityParameters );

                    if( 0x1 == PtrContext->FileObject->ContextObject->Flags.ShadowReadRequests )
                        OcMarkForShadowAsReadOperation( &PtrContext->Common.SecurityParameters );
            }

        }//if( FALSE == OcCrIsDeviceOnPagingPath(...

        //
        // switch off the flag
        //
        PtrContext->Common.RequestCurrentParameters.UpperDevice = 0x0;

    } else {

        //
        // convert the access rights and check against the security for the lower device
        //
        // TO DO

        //
        // log the request
        //
        // TO DO
    }

    return TRUE;
}

//-----------------------------------------------------------

