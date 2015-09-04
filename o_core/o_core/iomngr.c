/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
20.02.2007 ( February )
 Start
*/

/*
This file contains code for processing IO requests to devices
*/
#include "struct.h"
#include "proto.h"

//-------------------------------------------------

static
NTSTATUS
OcCrProcessRequestWoResult(
    __in CONST POC_NODE_CTX   NodeContext,
    __in POC_DEVICE_OBJECT    PtrOcDeviceObject
    );

static
OC_ACCESS_RIGHTS
OcCrGetDeviceRequestedAccess(
    IN POC_NODE_CTX CONST    Context,
    IN POC_DEVICE_OBJECT     OcDeviceObject
    );

//-------------------------------------------------

VOID
OcCrInitializeIoManager()
{
#if DBG
    InitializeListHead( &Global.IoManager.NodeCtxListHead );
    KeInitializeSpinLock( &Global.IoManager.ListSpinLock );
#endif//DBG

    Global.IoManager.MemoryTag = 'xtCN';

    ExInitializeNPagedLookasideList( &Global.IoManager.NodeCtxHookedDrvLookasideList, 
                                     NULL, 
                                     NULL, 
                                     0x0,
                                     sizeof( OC_HOOKED_DRV_NODE_CTX ),
                                     Global.IoManager.MemoryTag,
                                     0x0 );

    ExInitializeNPagedLookasideList( &Global.IoManager.NodeCtxPnPDrvLookasideList, 
                                     NULL, 
                                     NULL, 
                                     0x0,
                                     sizeof( OC_PNPFLTR_DRV_NODE_CTX ),
                                     Global.IoManager.MemoryTag,
                                     0x0 );

    Global.IoManager.Initialized = 0x1;
}

//-------------------------------------------------

VOID
OcCrUnInitializeIoManager()
{
    if( 0x0 == Global.IoManager.Initialized )
        return;

    ASSERT( OcObIsObjectManagerInitialized() );
    ASSERT( IsListEmpty( &Global.IoManager.NodeCtxListHead ) );
    ASSERT( 0x0 == Global.IoManager.NumberOfAllocatedCtxEntries );

    Global.IoManager.Initialized = 0x0;

    ExDeleteNPagedLookasideList( &Global.IoManager.NodeCtxHookedDrvLookasideList );
    ExDeleteNPagedLookasideList( &Global.IoManager.NodeCtxPnPDrvLookasideList );
}

//-------------------------------------------------

BOOLEAN
OcCrNodeFunctionHookedDriver(
    __in POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    __inout PVOID    Context
    )
{
    POC_HOOKED_DRV_NODE_CTX    HookedDrvContext = (POC_HOOKED_DRV_NODE_CTX)Context;
    BOOLEAN                    PostponeProcessing;
    POC_DEVICE_TYPE_OBJECT     DeviceTypeObj = NULL;
    KIRQL                      OldIrql;
    BOOLEAN                    UpperDevice;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OcIsFlagOn( HookedDrvContext->Common.Flags, OcNodeCtxHookedDriverFlag ) );
    ASSERT( FALSE == OcCrIsFileSystemDevObj( HookedDrvContext->Common.OriginalDeviceObject ) );

    //
    // get the upper device flag
    //
    UpperDevice = (0x1 == HookedDrvContext->Common.RequestCurrentParameters.UpperDevice);

    //
    // switch off the upper device flag
    //
    HookedDrvContext->Common.RequestCurrentParameters.UpperDevice = 0x0;

    //
    // top level IRP value makes sence only for FSD drivers which are not processed here
    //
    PostponeProcessing = ( KeGetCurrentIrql() > APC_LEVEL );//&& IoGetTopLevelIrp() == NULL );
    if( PostponeProcessing ){

        NTSTATUS    RC;

        //
        // postpone this function in a worker thread
        //
        RC = OcCrIoPostponeNodeFunctionInWorkerThread( OcCrNodeFunctionHookedDriver,
                                                       PtrOcNodeDeviceObject,
                                                       Context );
        ASSERT( NT_SUCCESS( RC ) );
        if( NT_SUCCESS( RC ) ){

            ASSERT( STATUS_PENDING == HookedDrvContext->Common.StatusCode );
            ASSERT( OcIrpDecisionReturnStatus == HookedDrvContext->HookedDrvIrpDecision );

            //
            // stop the PnP tree traversing
            //
            return FALSE;

        } else if( STATUS_DELETE_PENDING == RC ){

            //
            // we are currently being unload, cancel all operations
            // TO DO
            //

            ASSERT( STATUS_PENDING != HookedDrvContext->Common.StatusCode );
            ASSERT( OcIrpDecisionReturnStatus != HookedDrvContext->HookedDrvIrpDecision );

            //
            // stop the PnP tree traversing
            //
            return FALSE;

        } else {

            //
            // there was an error but I continue processing, do I feel lucky?
            //
            return TRUE;
        }
    }

    if( UpperDevice ){

        //
        // check the access, log and shadow settings for the upper device, i.e.
        // the device which has received the request
        //

        //
        // check access to the device receiving the request
        //
        // TO DO

#if CRUEL_CHECK_1
        //
        // hook the completion routine, this is a test
        //
        OcCmHkHookCompletionRoutineInCurrentStack( HookedDrvContext->Common.RequestData.Irp,
                                                   OcCmHkIrpCompletionTestCallback,
                                                   (PVOID)0x1 );
#endif//CRUEL_CHECK_1

    } else {

        //
        // convert the access rights and check against the security for the lower device
        //
        // TO DO
    }

    //
    // get the devicetype stack device
    //
    OcRwAcquireLockForRead( &PtrOcNodeDeviceObject->RwSpinLock, &OldIrql );
    {// start of the lock

        DeviceTypeObj = PtrOcNodeDeviceObject->DeviceType;
        if( NULL != DeviceTypeObj )
            OcObReferenceObject( DeviceTypeObj );

    }// end of the lock
    OcRwReleaseReadLock( &PtrOcNodeDeviceObject->RwSpinLock, OldIrql );

    if( NULL != DeviceTypeObj ){

        ULONG    i;

        //
        // process the device's types
        //
        for( i = 0x0; i<DeviceTypeObj->TypeStack->NumberOfValidEntries; ++i ){

            OC_CHECK_SECURITY_REQUEST     CheckSecurity = { 0x0 };

            //
            // TO DO - process the request's permissions by calling OcIsAccessGrantedSafe
            //

            if( UpperDevice && 0x0 == i ){

                //
                // shadow and log
                //
                HookedDrvContext->Common.SecurityParameters.LogRequest = 0x1;

                if( FALSE == OcCrIsDeviceOnPagingPath( PtrOcNodeDeviceObject ) ){

                    OcMarkForShadowAsWriteOperation( &HookedDrvContext->Common.SecurityParameters );
                    OcMarkForShadowAsReadOperation( &HookedDrvContext->Common.SecurityParameters );
                }
            }// UpperDevice

        }// for

        OcObDereferenceObject( DeviceTypeObj );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( DeviceTypeObj );
    }

    return TRUE;
}

//-------------------------------------------------

BOOLEAN
OcCrNodeFunctionPnPFilterDevice(
    __in POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    __inout PVOID    Context
    )
{
    POC_PNPFLTR_DRV_NODE_CTX    PnPFilterDrvContext = (POC_PNPFLTR_DRV_NODE_CTX)Context;
    BOOLEAN                     PostponeProcessing;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OcIsFlagOn( PnPFilterDrvContext->Common.Flags, OcNodeCtxPnPFilterDriverFlag ) );

    //
    // top level value makes sence only for FSD drivers
    //
    PostponeProcessing = ( KeGetCurrentIrql() > APC_LEVEL );// && IoGetTopLevelIrp() == NULL );
    if( PostponeProcessing ){

        NTSTATUS    RC;

        //
        // postpone this function in a worker thread
        //
        RC = OcCrIoPostponeNodeFunctionInWorkerThread( OcCrNodeFunctionPnPFilterDevice,
                                                       PtrOcNodeDeviceObject,
                                                       Context );
        ASSERT( NT_SUCCESS( RC ) );
        if( NT_SUCCESS( RC ) ){

            ASSERT( STATUS_PENDING == PnPFilterDrvContext->Common.StatusCode );
            ASSERT( OcFilterReturnCode == PnPFilterDrvContext->PnPFltrIrpDecision );
            //
            // stop the PnP tree traversing
            //
            return FALSE;

        } else if( STATUS_DELETE_PENDING == RC ){

            //
            // we are currently being unload, cancel all operations
            // TO DO
            //

            ASSERT( STATUS_PENDING != PnPFilterDrvContext->Common.StatusCode );
            ASSERT( OcFilterReturnCode != PnPFilterDrvContext->PnPFltrIrpDecision );

            return FALSE;

        } else {

            //
            // there was an error but I continue processing, do I feel lucky?
            //

            //
            // switch off the upper device flag
            //
            PnPFilterDrvContext->Common.RequestCurrentParameters.UpperDevice = 0x0;
            return TRUE;
        }
    }

    //
    // check the access, log and shadow settings
    //
    // TO DO

    if( 0x1 == PnPFilterDrvContext->Common.RequestCurrentParameters.UpperDevice ){

        //
        // process the permissions and log settings for the device which has received the
        // request, i.e. the upper device
        //

        //
        // log and shadow
        //
        PnPFilterDrvContext->Common.SecurityParameters.LogRequest = 0x1;

        if( FALSE == OcCrIsDeviceOnPagingPath( PtrOcNodeDeviceObject ) ){

            //
            // check for shadowing
            //

            OcMarkForShadowAsWriteOperation( &PnPFilterDrvContext->Common.SecurityParameters );
            OcMarkForShadowAsWriteOperation( &PnPFilterDrvContext->Common.SecurityParameters );
        }

        //
        // switch off the upper device flag
        //
        PnPFilterDrvContext->Common.RequestCurrentParameters.UpperDevice = 0x0;

        //
        // check access to the device receiving the request
        //
        // TO DO
        //OcIsAccessGrantedSafe( OcAccessSd

        //
        // log the request
        //
        // TO DO

#if CRUEL_CHECK_1
        //
        // hook the completion routine, this is a test
        //
        OcCmHkHookCompletionRoutineInCurrentStack( PnPFilterDrvContext->Common.RequestData.Irp,
                                                   OcCmHkIrpCompletionTestCallback,
                                                   (PVOID)0x2 );
#endif//CRUEL_CHECK_1

    } else {

        //
        // convert the access rights and check against the security for the lower device
        // the upper device has been stored in PnPFilterDrvContext->Common.OcOriginalDeviceObject
        //

        //
        // TO DO

        //
        // log the request
        //
        // TO DO
    }

    //
    // the OcFilterReturnCode can't be set by the caller,
    // if needed this code might be changed in this function
    // for example if stack has been copied and( or )
    // a completion routine has been set
    //
    ASSERT( OcFilterReturnCode != PnPFilterDrvContext->PnPFltrIrpDecision );

    return TRUE;
}

//-------------------------------------------------

BOOLEAN
OcCrHookedDrvRequestCompletedOrPending(
    __in POC_NODE_CTX CONST    Context
    )
{
    POC_HOOKED_DRV_NODE_CTX CONST    HookedDrvContext = (POC_HOOKED_DRV_NODE_CTX)Context;

    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) );

    return ( OcIrpDecisionReturnStatus == HookedDrvContext->HookedDrvIrpDecision );
}

//-------------------------------------------------

BOOLEAN
OcCrPnPFilterDrvRequestCompletedOrPending(
    __in POC_NODE_CTX CONST   Context
    )
{
    POC_PNPFLTR_DRV_NODE_CTX CONST    PnPFIlterDrvContext = (POC_PNPFLTR_DRV_NODE_CTX)Context;

    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) );

    return( OcFilterReturnCode == PnPFIlterDrvContext->PnPFltrIrpDecision );
}

//-------------------------------------------------

VOID
OcCrProcessIrpBasedDeviceRequest(
    __in POC_DEVICE_OBJECT    PtrOcDeviceObject,
    __inout POC_NODE_CTX      Context,
    __in PtrOcCrNodeFunction  OcCrNodeFunction,
    __in OcCrDeviceIprRequestCompletedOrPending    RequestCompletedOrPending
    )
    /*
    the function process request through the PnP tree, 

    the functor OcCrNodeFunction is called for PtrOcDeviceObject and every
    device lying below in the PnP stack

    the functor RequestCompletedOrPending is used for
    checking by Context object whether the request
    has been completed or pendig after traversing the PnP tree
    */
{
    UCHAR    MajorFunction;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_NODE_CTX_SIGNATURE == Context->Signature );
    ASSERT( IO_TYPE_IRP == Context->RequestData.Irp->Type );
    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) || 
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) );

    MajorFunction = (IoGetCurrentIrpStackLocation( Context->RequestData.Irp ))->MajorFunction;

    if( IRP_MJ_CLOSE == MajorFunction ){

        //
        // process a special case - the close request, the close
        // request must be successful in any case and must be
        // always processed to remove the file object from the
        // hash, the returned status code is not checked by the system
        //
        OcCrProcessFileObjectCloseRequest( IoGetCurrentIrpStackLocation( Context->RequestData.Irp )->FileObject );
        return;
    }

    //
    // if the device is not started yet then do nothing with it,
    // PnP requests are processed through the callbacks registered with
    // PnP filter
    //
    if( Started != PtrOcDeviceObject->PnPState )
        return;

    //
    // check whether the operation is already in the list
    //
    if( OcCrIsRequestInDeviceQueue( PtrOcDeviceObject, Context->RequestData.Irp ) )
        return;

    //
    // get the access rights for this request it they have not been got yet
    //
    if( 0x0 == Context->RequestCurrentParameters.AccessRightsReceived ){

        OC_ACCESS_RIGHTS    RequestedAccess;

        //
        // should be upper device in the device chain
        //
        ASSERT( 0x1 == Context->RequestCurrentParameters.UpperDevice );

        RequestedAccess = OcCrGetDeviceRequestedAccess( Context,
                                                        PtrOcDeviceObject );

        //
        // remember that this is a direct device open
        //
        if( DIRECT_DEVICE_OPEN & RequestedAccess )
            Context->RequestCurrentParameters.DirectDeviceOpenRequest = 0x1;

        //
        // if the access rights are 0x0 and this is not a create request,
        // then there is nothing to check, log and shadow
        //
        if( DEVICE_NO_ANY_ACCESS == RequestedAccess && 
            IRP_MJ_CREATE != MajorFunction )
            return;

        Context->SecurityParameters.RequestedAccess = RequestedAccess;
        Context->RequestCurrentParameters.AccessRightsReceived = 0x1;
    }

    //
    // traverse the PnP tree, it performs the check of 
    // access rights againts the device's permissions 
    // and logs the access if needed, if the access is
    // denied the request is completed, if the request 
    // is sent at elevated IRQL it is made pending
    //
    OcCrTraverseTopDown( PtrOcDeviceObject,
                         OcCrNodeFunction,
                         (PVOID)Context );

    //
    // check whether the request has been made pending or completed
    //
    if( RequestCompletedOrPending( Context ) )
        return;

    //
    // check whether the request should be logged or shadowed,
    // in case of logging only create request is processed to track 
    // file objects.
    // Serious amendment! I made a decision to track all file object opened
    // for devices, in case of FSD this imposes a serios overload,
    // but in case of devices the event of calling IRP_MJ_CREATE routine
    // is a very rare one and usually indicates that the user
    // will try to write or read directly from or to device and
    // this file object will be needed to determine the requested access 
    // by filtering out the user's requests and skipping the benign
    // requests send by an FSD driver or over internal subsystem
    //
    if( ( 0x0 == Context->RequestCurrentParameters.DirectDeviceOpenRequest ) && 
        !( 0x1 == Context->SecurityParameters.LogRequest && IRP_MJ_CREATE == MajorFunction ) &&
        FALSE == OcIsOperationShadowedAsWriteRequest( &Context->SecurityParameters ) &&
        FALSE == OcIsOperationShadowedAsReadRequest( &Context->SecurityParameters ) )
        return;

    //
    // the request should be made pending in OcCrNodeFunction if
    // a more complex processing is required and the IRRQL is DISPATCH_LEVEL
    //
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    //
    // process the request, create requests and shadowed requests
    // are processed here
    //
    OcCrProcessRequestWithResult( Context, PtrOcDeviceObject );

}

//-------------------------------------------------

NTSTATUS
OcCrProcessIoRequestFromHookedDriver(
    IN PDEVICE_OBJECT    SysDeviceObject,
    IN PIRP    Irp,
    IN PIO_STACK_LOCATION    PtrIrpStackLocation
    )
{
    OC_HOOKED_DRV_NODE_CTX    Context = { 0x0 };
#if DBG
    BOOLEAN         CloseRequest = (IRP_MJ_CLOSE == IoGetCurrentIrpStackLocation( Irp )->MajorFunction);
    PFILE_OBJECT    FileObject = IoGetCurrentIrpStackLocation( Irp )->FileObject;
#endif//DBG

#if DBG
    Context.Common.Signature = OC_NODE_CTX_SIGNATURE;
    Context.Common.Size = sizeof( Context );
#endif//DBG
    Context.Common.Flags = OcNodeCtxHookedDriverFlag;
    Context.Common.RequestData.Irp = Irp;
    Context.Common.OriginalDeviceObject = SysDeviceObject;
    Context.Common.RequestCurrentParameters.UpperDevice = 0x1;
    Context.Common.StatusCode = STATUS_SUCCESS;
    Context.HookedDrvIrpDecision = OcIrpDecisionCallOriginal;
    Context.HookedSysDeviceObject = SysDeviceObject;
#if defined (_AMD64_)
    if( IoIs32bitProcess( Irp ) ){

        OcSetFlag( Context.Common.Flags, OcNodeCtx32bitProcessFlag );
    }
#else
    OcSetFlag( Context.Common.Flags, OcNodeCtx32bitProcessFlag );
#endif//#if defined (_AMD64_)

    //
    // The special cases are the power and PnP Irps, do not touch them,
    // the PnP requests are processed in the PnP filter through 
    // callbacks and the power Irps are not interesting for 
    // me and they can't be delayed for a long time and require 
    // a special processing
    //
    if( IRP_MJ_POWER == IoGetCurrentIrpStackLocation( Irp )->MajorFunction || 
        IRP_MJ_PNP == IoGetCurrentIrpStackLocation( Irp )->MajorFunction ){

        ASSERT( OcIrpDecisionCallOriginal == Context.HookedDrvIrpDecision );
        goto __exit_call_original_if;
    }

#if CRUEL_CHECK_1
    //
    // hook the completion routine, this is a test, remove it
    //
    OcCmHkHookCompletionRoutineInCurrentStack( Irp,
                                               OcCmHkIrpCompletionTestCallback,
                                               (PVOID)0x3 );
#endif//CRUEL_CHECK_1

    //
    // FSDs are hooked and filter objects are attached to them,
    // but attcached objects are not used for processing IRPs
    //
    if( OcCrIsFileSystemDevObj( SysDeviceObject ) ){

        //
        // This is a request to a hooked FSD.
        //

        //
        // process FSD request and return
        //
        OcCrProcessFsdIrpRequestToHookedDriver( &Context,
                                                PtrIrpStackLocation );

    } else {

        POC_DEVICE_OBJECT    PtrOcDeviceObject;

        //
        // this is not a request to a hooked FSD
        //
        PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                       (ULONG_PTR)SysDeviceObject,
                                                       OcObReferenceObject );

        if( NULL != PtrOcDeviceObject ){

            //
            // save the pointer to the device object in the context,
            // the object has been referenced, so there is no need to
            // reference it second time, if you decide to reference it
            // then do not forget to dereference it
            //
            Context.Common.OcOriginalDeviceObject = PtrOcDeviceObject;

            //
            // process requests for permissions, logging and shadowing status
            //
            OcCrProcessIrpBasedDeviceRequest( PtrOcDeviceObject,
                                              &Context.Common,
                                              OcCrNodeFunctionHookedDriver,
                                              OcCrHookedDrvRequestCompletedOrPending );

            //
            // set pointer to NULL before dereferencing
            //
            ASSERT( PtrOcDeviceObject == Context.Common.OcOriginalDeviceObject );
            Context.Common.OcOriginalDeviceObject = NULL;

            OcObDereferenceObject( PtrOcDeviceObject );

        } else if( IRP_MJ_CLOSE == IoGetCurrentIrpStackLocation( Irp )->MajorFunction ){

            //
            // process a special case - the close request, the close
            // request must be successful in any case and must be
            // always processed to remove the file object from the
            // hash, the returned status code is not checked by the system
            //

            //
            // remove the fileobject from the hash and delete it
            //
            OcCrProcessFileObjectCloseRequest( IoGetCurrentIrpStackLocation( Irp )->FileObject );

            //
            // nothing to do with this request
            //
        }

    }// else 

    //
    // now call the original function if this has not been done and Irp has not been completed
    //
__exit_call_original_if:

    if( OcIrpDecisionReturnStatus != Context.HookedDrvIrpDecision ){

        PDRIVER_DISPATCH     OriginalDriverDispatch;

        OriginalDriverDispatch = Global.DriverHookerExports.RetreiveOriginalDispatch(
                                    SysDeviceObject->DriverObject,
                                    PtrIrpStackLocation->MajorFunction );

        ASSERT( Context.Common.RequestData.Irp == Irp );

        Context.Common.StatusCode = OriginalDriverDispatch( SysDeviceObject, Irp );

        //
        // Irp might has been completed!
        //
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Irp );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Context.Common.RequestData.Irp );

    } else {

        //
        // this is a test, remove it
        //
        ASSERT( STATUS_PENDING == Context.Common.StatusCode );
        ASSERT( OcIrpDecisionReturnStatus == Context.HookedDrvIrpDecision );
    }

    //
    // Irp might has been completed!
    //
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Irp );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Context.Common.RequestData.Irp );

#if DBG
    if( CloseRequest ){
        ASSERT( NULL == OcCrRetriveReferencedFileObject( FileObject ) );
    }
#endif//DBG
    return Context.Common.StatusCode;
}

//-------------------------------------------------

NTSTATUS
NTAPI
OcCrPnPFilterIoDispatcherCallback(
    IN PDEVICE_OBJECT    SysDeviceObject,
    IN PDEVICE_OBJECT    NextLowerDeviceobject,
    IN PIRP    Irp,
    OUT POC_FILTER_IRP_DECISION    PtrIrpDecision
    )
{
    POC_DEVICE_OBJECT    PtrOcDeviceObject;
    OC_PNPFLTR_DRV_NODE_CTX    Context = { 0x0 };

#if DBG
    Context.Common.Signature = OC_NODE_CTX_SIGNATURE;
    Context.Common.Size = sizeof( Context );
#endif//DBG
    Context.Common.Flags = OcNodeCtxPnPFilterDriverFlag;
    Context.Common.RequestData.Irp = Irp;
    Context.Common.StatusCode = STATUS_SUCCESS;
    Context.Common.OriginalDeviceObject = SysDeviceObject;
    Context.Common.RequestCurrentParameters.UpperDevice = 0x1;
    Context.NextLowerDeviceObject = NextLowerDeviceobject;
    Context.PnPFltrIrpDecision = OcFilterSendIrpToLowerSkipStack;
#if defined (_AMD64_)
    if( IoIs32bitProcess( Irp ) ){

        OcSetFlag( Context.Common.Flags, OcNodeCtx32bitProcessFlag );
    }
#else
    OcSetFlag( Context.Common.Flags, OcNodeCtx32bitProcessFlag );
#endif//#if defined (_AMD64_)

    //
    // PnP filter is also used for attaching to the FSDs' device objects but
    // this object is used only for the lifelong tracking of the underlying
    // device object and the close requests processing
    //
    if( FALSE == Global.DeviceInformationCollected || OcCrIsFileSystemDevObj( SysDeviceObject ) ){

        if( IRP_MJ_CLOSE == IoGetCurrentIrpStackLocation( Irp )->MajorFunction ){

            //
            // process a special case - the close request, the close
            // request must be successful in any case and must be
            // always processed to remove the file object from the
            // hash, the returned status code is not checked by the system
            // The close is always processed at this level because there
            // might be not minifilter for this FSD( old system ) and
            // the FSD driver has been unhooked( or rehooked ) by a 
            // third party kernel module or by FSD himself
            //
            OcCrProcessFileObjectCloseRequest( IoGetCurrentIrpStackLocation( Irp )->FileObject );
        }

        *PtrIrpDecision = OcFilterSendIrpToLowerSkipStack;
        return STATUS_SUCCESS;
    }

#if CRUEL_CHECK_1
    //
    // hook the completion routine, this is a test, remove it
    //
    OcCmHkHookCompletionRoutineInCurrentStack( Irp,
                                               OcCmHkIrpCompletionTestCallback,
                                               (PVOID)0x4 );
#endif//CRUEL_CHECK_1

    //
    // get the device to which the request has been sent
    //
    PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                   (ULONG_PTR)SysDeviceObject,
                                                   OcObReferenceObject );
    if( NULL != PtrOcDeviceObject ){

        //
        // save the OcDevice object pointer
        //
        Context.Common.OcOriginalDeviceObject = PtrOcDeviceObject;

        //
        // process permissions, logging and auditing state for the request
        //
        OcCrProcessIrpBasedDeviceRequest( PtrOcDeviceObject,
                                          &Context.Common,
                                          OcCrNodeFunctionPnPFilterDevice,
                                          OcCrPnPFilterDrvRequestCompletedOrPending );

        //
        // set device object pointer to NULL before dereferencing
        //
        ASSERT( PtrOcDeviceObject == Context.Common.OcOriginalDeviceObject );
        Context.Common.OcOriginalDeviceObject = NULL;

        OcObDereferenceObject( PtrOcDeviceObject );

    } else if( IRP_MJ_CLOSE == IoGetCurrentIrpStackLocation( Irp )->MajorFunction ){

        //
        // process a special case - the close request, the close
        // request must be successful in any case and must be
        // always processed to remove the file object from the
        // hash, the returned status code is not checked by the system
        //

        //
        // remove the fileobject from the hash and delete it
        //
        OcCrProcessFileObjectCloseRequest( IoGetCurrentIrpStackLocation( Irp )->FileObject );

        //
        // nothing to do with this request
        //
    }

#if DBG
    if( OcFilterReturnCode == Context.PnPFltrIrpDecision ){

        ASSERT( STATUS_PENDING == Context.Common.StatusCode );

    } else {

        ASSERT( Context.Common.RequestData.Irp == Irp );
    }
#endif//DBG

    *PtrIrpDecision = Context.PnPFltrIrpDecision;
    return Context.Common.StatusCode;
}

//-------------------------------------------------

VOID
OcCrProcessRequestWithResult(
    IN OUT POC_NODE_CTX    NodeContext,
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject
    )
    /*

    This function processes a request basing on an infromation in
    the provided context, the major task is a requests shadowing.

    Should be any errors while processing Irp, the function
    completes an Irp and changes the Context appropriately.

    This function doesn't postpone requests, it is a synchronous routine.

    */
{
    PIO_STACK_LOCATION    CurrentStackLocation;

    ASSERT( OcIsFlagOn( NodeContext->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( NodeContext->Flags, OcNodeCtxPnPFilterDriverFlag ) );
    ASSERT( IO_TYPE_IRP == NodeContext->RequestData.Irp->Type );

    CurrentStackLocation = IoGetCurrentIrpStackLocation( NodeContext->RequestData.Irp );

    //
    // process a special case - the close request, the close
    // request must be successful in any case, the returned
    // code is not checked by the system
    //
    if( IRP_MJ_CLOSE == CurrentStackLocation->MajorFunction ){

        //
        // remove the fileobject from the hash and delete it
        //
        OcCrProcessFileObjectCloseRequest( CurrentStackLocation->FileObject );

        //
        // nothing to do with this request
        //
        goto __exit;
    }

    //
    // now check whether the request should be shadowed or logged
    //
    if( OcIsOperationLogged( &NodeContext->SecurityParameters ) && 
        OcIsOperationShadowedAsWriteRequest( &NodeContext->SecurityParameters ) && 
        OcIsOperationShadowedAsReadRequest( &NodeContext->SecurityParameters ) ){
            //
            // nothing to do here
            //
            goto __exit;
    }

__exit:

    if( OcIsFlagOn( NodeContext->Flags, OcNodeCtxPnPFilterDriverFlag ) ){

        //
        // this is a request to the PnP filter driver
        //

        POC_PNPFLTR_DRV_NODE_CTX    PnPFilterDrvContext = (POC_PNPFLTR_DRV_NODE_CTX)NodeContext;

        //
        // the completed or penede request must not be sent to this function
        //
        ASSERT( OcFilterReturnCode != PnPFilterDrvContext->PnPFltrIrpDecision );

        //
        // we have an Irp to process, nobody has canceled or completed this request
        //

        NodeContext->StatusCode = OcCrProcessRequestWoResult( NodeContext, PtrOcDeviceObject );
        ASSERT( NT_SUCCESS( NodeContext->StatusCode ) );
        if( !NT_SUCCESS( NodeContext->StatusCode ) ){

            //
            // STATUS_DELETE_PENDING code means that we are unloading, this must not hurt the system
            //
            if( STATUS_DELETE_PENDING == NodeContext->StatusCode ){

                NodeContext->StatusCode = STATUS_SUCCESS;

            } else {

                //
                // there was an error while processing the Irp,
                // complete it with an error
                //

                NodeContext->RequestData.Irp->IoStatus.Status = NodeContext->StatusCode;
                IoCompleteRequest( NodeContext->RequestData.Irp, IO_DISK_INCREMENT );
                PnPFilterDrvContext->PnPFltrIrpDecision = OcFilterReturnCode;

                //
                // Irp has been completed!
                //
                OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( NodeContext->RequestData.Irp );
            }
        }

    } else {

        //
        // this is a request to a hooked driver
        //

        POC_HOOKED_DRV_NODE_CTX    HookedDrvContext = (POC_HOOKED_DRV_NODE_CTX)NodeContext;

        ASSERT( OcIsFlagOn( NodeContext->Flags, OcNodeCtxHookedDriverFlag ) );
        ASSERT( OcIrpDecisionReturnStatus != HookedDrvContext->HookedDrvIrpDecision );

        //
        // we have an Irp to process, nobody has canceled or completed this request
        //

        NodeContext->StatusCode = OcCrProcessRequestWoResult( NodeContext, PtrOcDeviceObject );
        ASSERT( NT_SUCCESS( NodeContext->StatusCode ) );
        if( !NT_SUCCESS( NodeContext->StatusCode ) ){

            //
            // STATUS_DELETE_PENDING code means that we are unloading, this must not hurt the system
            //
            if( STATUS_DELETE_PENDING == NodeContext->StatusCode ){

                NodeContext->StatusCode = STATUS_SUCCESS;

            } else {

                //
                // there was an error while processing the Irp,
                // complete it with an error
                //

                NodeContext->RequestData.Irp->IoStatus.Status = NodeContext->StatusCode;
                IoCompleteRequest( NodeContext->RequestData.Irp, IO_DISK_INCREMENT );
                HookedDrvContext->HookedDrvIrpDecision = OcIrpDecisionReturnStatus;

                //
                // Irp has been completed!
                //
                OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( NodeContext->RequestData.Irp );
            }
        }

    }

}

//-------------------------------------------------

NTSTATUS
OcCmHkIrpCompletionRoutine( 
    IN PVOID CompletionContext,// actually POC_CMHK_CONTEXT
    IN PVOID CallbackContext// CompletionContext->Context
    )
{
    POC_OPERATION_OBJECT    OperationObject = (POC_OPERATION_OBJECT)CallbackContext;
    PIRP                    Irp = OcCmHkGetIrpFromCompletionContext( CompletionContext );
    OC_SHADOWED_REQUEST_COMPLETION_PARAMETERS    CompletionParam;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( _OC_OPERATION_OBJ_SIGNATURE == OperationObject->Signature );

    //
    // initialize the completion parameters which is propagated between functions
    //
    CompletionParam.CompleteSynchronously = TRUE;
    CompletionParam.CompletionContext = CompletionContext;

#if DBG
    KeInitializeEvent( &CompletionParam.CompletionEvent, SynchronizationEvent, FALSE );
    OcCmHkInitializeCompletionWaitBlock( &CompletionParam.CompletionWaitBlock, &CompletionParam.CompletionEvent );
    CompletionParam.CompletionWaitBlock.WaitForCompletion = FALSE;
#endif//

    //
    // continue completing if the request processing faied on our side,
    // do not use this operation object as it might not contain all
    // fields which is necessary for processing
    //
    if( 0x1 == OperationObject->Flags.RequestProcessingError ){

        //
        // free the allocated object
        //
        OcObDereferenceObject( OperationObject );

        return OcCrShadowCompleteDeviceRequest( &CompletionParam );
    }

    //
    // synchronize with the shadow code in OcCrShadowWriterWR
    //
    {
        NTSTATUS    RC;
        RC = OcCrShadowProcessOperationBuffersForDevice( OperationObject,
                                                         &CompletionParam );
        ASSERT( NT_SUCCESS( RC ) );
    }

    switch( OperationObject->MajorFunction ){
        case IRP_MJ_CREATE:
            {
                NTSTATUS    RC;

                //
                // process the special case of zero FileObject
                //
                if( 0x1 == OperationObject->OperationParameters.Create.Flags.IsFileObjectAbsent ){

                    ASSERT( NULL == OperationObject->OperationParameters.Create.RefCreationInfo );
                    ASSERT( NULL == OperationObject->OperationParameters.Create.FltCallbackDataStore );

                    //
                    // nothing to do
                    //
                    break;
                }

                //
                // remember that perationObject->OperationParameters.Create.FltCallbackDataStore
                // has been only partially initialized and can't be used in many FSD functions,
                // for example its IoStatus is wrong
                //

                RC = OcCrProcessFoCreateRequestCompletion( &Irp->IoStatus,
                                                           OperationObject->OperationParameters.Create.RefCreationInfo,
                                                           OperationObject->OperationParameters.Create.FltCallbackDataStore->Iopb.TargetFileObject,
                                                           OC_REQUEST_FROM_DEVICE );
                ASSERT( !OcIsFlagOn( OperationObject->OperationParameters.Create.FltCallbackDataStore->FltCallbackData.Flags, FLTFL_CALLBACK_DATA_DIRTY ) );
                if( !NT_SUCCESS( RC ) ){

                    //
                    // cancel the creation( IoCancelFileOpen )
                    // TO DO
                }

            }
            break;

        case IRP_MJ_READ:

            if( 0x1 == OperationObject->SecurityParameters.ShadowReadRequest ){

                NTSTATUS    RC;

                //
                // copy the data to the private buffer
                //
                ASSERT( NULL != OperationObject->PrivateBufferInfo.Buffer && 
                        0x1 == OperationObject->Flags.PrivateBufferAllocated && 
                        0x0 == OperationObject->Flags.DataInPrivateBuffer );

                RC = OcCrProcessOperationObjectPrivateBuffers( OperationObject,
                                                               TRUE );

                if( NT_SUCCESS( RC ) ){

                    RC = OcCrShadowRequest( OperationObject );
                }

                //
                // there is no error processing for the read requests shadowing,
                // TO DO the error processing for the read requests shadowing
                //

            }
            break;

    }

    if( 0x1 == OperationObject->Flags.ObjectSentInShadowingModule ){

        //
        // TO DO - shadow the completion status!
        //
    }

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
    // free the allocated object
    //
    OcObDereferenceObject( OperationObject );

    //
    // complete the request or wait for a completion
    //
    return OcCrShadowCompleteDeviceRequest( &CompletionParam );
}

//-------------------------------------------------

NTSTATUS
OcCrProcessRequestWoResult(
    __in CONST POC_NODE_CTX    NodeContext,
    __in POC_DEVICE_OBJECT     PtrOcDeviceObject
    )
    /*

    this function processes a request basing on an infromation in
    the provided context, the major task is a request shadowing

    should be any errors while processing Irp, the function
    simply stops processing and returns an error, NodeContext
    must not be changed in this function - the caller makes a
    decision about a request, the decision is based on a code
    returned from this function

    the caller must call this function on a safe IRQL and in 
    a safe context else an error is returned, this function 
    doesn't postpone requests, the postponing must have been 
    done by the caller, e.g. while traversing the PnP tree

    if this function returns STATUS_DELETE_PENDING the caller 
    should not cancell the request - this code means that this 
    driver is unloading
    */
{
    NTSTATUS                 RC;
    POC_OPERATION_OBJECT     OperationObject = NULL;
#if DBG
    OC_UNITED_DRV_NODE_CTX   InitialNodeContext = { 0x0 };
    RtlCopyMemory( &InitialNodeContext, NodeContext, NodeContext->Size );
#endif//DBG

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( OcIsFlagOn( NodeContext->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( NodeContext->Flags, OcNodeCtxPnPFilterDriverFlag ) );
    ASSERT( OC_NODE_CTX_SIGNATURE == NodeContext->Signature );
    ASSERT( 0x0 < NodeContext->Size && NodeContext->Size <= sizeof( OC_UNITED_DRV_NODE_CTX ) );
    ASSERT( IO_TYPE_IRP == NodeContext->RequestData.Irp->Type );

    //
    // create an operation object to track this request
    //
    RC = OcCrCreateOperationObjectForDevice( PtrOcDeviceObject,
                                             NodeContext,
                                             &OperationObject );
    if( NT_SUCCESS( RC ) ){

        //
        // hook the completion routine before doing any other operations,
        // because their completion require the successfull hooking and
        // it is very hard to roll them back if the hooking failed
        //
        RC = OcCmHkHookCompletionRoutineInCurrentStack( NodeContext->RequestData.Irp,
                                                        OcCmHkIrpCompletionRoutine,
                                                        OperationObject );
        if( !NT_SUCCESS( RC ) ){

            //
            // something went wrong with the worker threads pool
            // or the hooking engine
            //

            //
            // free the allocated object, it is meaningless to
            // mark it as havin an erorr during processing as
            // the completion routine won't be called
            //
            OcObDereferenceObject( OperationObject );
            OperationObject = NULL;
            goto __exit;
        }

    } else {

        //
        // there was an error in allocating the operation object
        //
        goto __exit;
    }

    //
    // process a file object creation and shadow this request
    //
    switch( OperationObject->MajorFunction ){
        case IRP_MJ_CREATE:
            {// start of the create processing
                POC_DEVICE_OBJECT    OcDeviceToOpenRefer = NULL;
                PFILE_OBJECT         FileObject;

                FileObject = IoGetCurrentIrpStackLocation( NodeContext->RequestData.Irp )->FileObject;

                //
                // some exteremely stupid drivers, such as serenum, 
                // make a create IRP with FileObject set to NULL to
                // acquire the device
                //
                if( NULL == FileObject ){

                    OperationObject->OperationParameters.Create.Flags.IsFileObjectAbsent = 0x1;
                    RC = STATUS_SUCCESS;

                    goto __clean_create;
                }

                OcDeviceToOpenRefer = OcCrGetDeviceObjectOnWhichFsdMounted( FileObject );
                ASSERT( NULL != OcDeviceToOpenRefer );
                if( NULL == OcDeviceToOpenRefer ){

                    RC = STATUS_OBJECT_NAME_NOT_FOUND;
                    goto __clean_create;
                }

                //
                // for simplicity( common functions ) use the 
                // minifilter callback data, allocate the data
                // from the pool, I can afford this because
                // a create request on a device w/o mounted 
                // FSD or a direct device open skipping an FSD 
                // are very rare operations
                //
                ASSERT( NULL == OperationObject->OperationParameters.Create.FltCallbackDataStore );
                ASSERT( NULL == OperationObject->OperationParameters.Create.RefCreationInfo );

                //
                // allocate a callback data store, it will be freed when the object is being destroyed
                //
                OperationObject->OperationParameters.Create.FltCallbackDataStore = ExAllocateFromNPagedLookasideList( &Global.FltCallbackDataStoreLaList );
                if( NULL == OperationObject->OperationParameters.Create.FltCallbackDataStore ){

                    RC = STATUS_INSUFFICIENT_RESOURCES;
                    goto __clean_create;
                }

                //
                // initialize the callback data for create request
                //
                OcCrFsdInitMinifilterDataForIrp( NodeContext->OriginalDeviceObject,
                                                 NodeContext->RequestData.Irp,
                                                 OperationObject->OperationParameters.Create.FltCallbackDataStore );

                //
                // this is a request to initialize a file object
                // for a device w/o mounted FSD or a direct volume 
                // or disk device opening
                //
                RC = OcCrProcessFoCreateRequest( NodeContext,
                                                 &OperationObject->OperationParameters.Create.FltCallbackDataStore->FltCallbackData,
                                                 OcDeviceToOpenRefer,
                                                 OC_REQUEST_FROM_DEVICE,
                                                 &OperationObject->OperationParameters.Create.RefCreationInfo );
                ASSERT( !OcIsFlagOn( OperationObject->OperationParameters.Create.FltCallbackDataStore->FltCallbackData.Flags, FLTFL_CALLBACK_DATA_DIRTY ) );
                ASSERT( NT_SUCCESS( RC ) );
                if( !NT_SUCCESS( RC ) )
                    goto __clean_create;

                __clean_create:
                {//start of the cleaning

                    //
                    // free resources used for the create request processing
                    //

                    if( NULL != OcDeviceToOpenRefer )
                        OcObDereferenceObject( OcDeviceToOpenRefer );

                }// end of the cleaning

            }// end of the create processing
            break;

        case IRP_MJ_WRITE:
        case IRP_MJ_DEVICE_CONTROL:
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:

            //
            // read requests are shadowed on completion
            //
            ASSERT( OcIsOperationLogged( &NodeContext->SecurityParameters ) || 
                    OcIsOperationShadowedAsWriteRequest( &NodeContext->SecurityParameters ) ||
                    OcIsOperationShadowedAsReadRequest( &NodeContext->SecurityParameters ) );

            RC = OcCrShadowRequest( OperationObject );

            break;
    }//switch( CurrentStackLocation->MajorFunction )

    if( !NT_SUCCESS( RC ) )
        goto __exit;

__exit:

    //
    // the operation object will be freed on an Irp completion
    //

    if( !NT_SUCCESS( RC ) ){

        if( NULL != OperationObject )
            OperationObject->Flags.RequestProcessingError = 0x1;
    }

#if DBG
    {
    //
    // the function must not change the node context, but ListEntry 
    // can be changed asynchronously by another thread( concurrent or 
    // preempting )
    //
    OC_UNITED_DRV_NODE_CTX   NodeContextAfterProcessing = { 0x0 };
    RtlCopyMemory( &NodeContextAfterProcessing, NodeContext, NodeContext->Size );

    InitialNodeContext.Common.ListEntry.Blink = 
        InitialNodeContext.Common.ListEntry.Flink = NULL;

    NodeContextAfterProcessing.Common.ListEntry.Blink = 
        NodeContextAfterProcessing.Common.ListEntry.Flink = NULL;

    ASSERT( InitialNodeContext.Common.Size == RtlCompareMemory( &InitialNodeContext,
                                                                &NodeContextAfterProcessing,
                                                                InitialNodeContext.Common.Size ) );
    }
#endif//DBG

    return RC;
}

//-------------------------------------------------

static
OC_ACCESS_RIGHTS
OcCrGetDeviceRequestedAccess(
    IN POC_NODE_CTX CONST    Context,
    IN POC_DEVICE_OBJECT     OcDeviceObject
    )
    /*
    the function returns the access required
    by a request sent to a device, the function
    must be used only for those device to which 
    the request is sent and must not be used for
    an upper or underlying devices
    */
{
    OC_ACCESS_RIGHTS             RequestedAccess = DEVICE_NO_ANY_ACCESS;
    POC_DEVICE_PROPERTY_HEADER   PropertyObject;
    OC_EN_SETUP_CLASS_GUID       SetupClassGuidIndex;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( 0x1 == Context->RequestCurrentParameters.UpperDevice );
    ASSERT( Context->OriginalDeviceObject == OcDeviceObject->KernelDeviceObject );

    PropertyObject = OcGetDeviceProperty( OcDeviceObject );
    if( NULL != PropertyObject )
        SetupClassGuidIndex = PropertyObject->SetupClassGuidIndex;
    else
        return DEVICE_NO_ANY_ACCESS;

    //
    // all create requests for any device which is not
    // a name space extender is considered as a direct 
    // device open
    //
    if( IRP_MJ_CREATE == IoGetCurrentIrpStackLocation( Context->RequestData.Irp )->MajorFunction )
        RequestedAccess |= DIRECT_DEVICE_OPEN;

    switch( SetupClassGuidIndex ){

        case en_GUID_DEVCLASS_USB:
            return OcCrGetUsbRequestedAccess( Context, OcDeviceObject );

        case en_GUID_DEVCLASS_FLOPPYDISK:
        case en_GUID_DEVCLASS_DISKDRIVE:
            return OcCrGetDiskDriveRequestedAccess( Context, OcDeviceObject );

        case en_GUID_DEVCLASS_VOLUME:
            return OcCrGetVolumeRequestedAccess( Context, OcDeviceObject );

        case en_GUID_DEVCLASS_CDROM:
            return OcCrGetOpticalDiskDriveRequestedAccess( Context, OcDeviceObject );

        case en_GUID_DEVCLASS_1394:
            return OcCrGetIEEE1394RequestedAccess( Context, OcDeviceObject );

        case en_GUID_DEVCLASS_BLUETOOTH:
            return OcCrGetBluetoothRequestedAccess( Context, OcDeviceObject );

    }

    //
    // DELETE THIS
#if DBG
    return ( DEVICE_READ | DEVICE_WRITE );
#endif//DBG

    return DEVICE_NO_ANY_ACCESS;
}
//-------------------------------------------------

