/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
09.03.2007 ( March )
 Start
*/

/*
This file contains code for pending IO requests processing
*/
#include "struct.h"
#include "proto.h"

//-------------------------------------------------

POC_NODE_CTX
OcCrCreateNodeCtxCopy(
    IN POC_NODE_CTX    NodeContext
    )
{
    POC_NODE_CTX    NewNodeCtx;
    BOOLEAN         IsPnPCtx;

    ASSERT( OC_NODE_CTX_SIGNATURE == NodeContext->Signature );
    //
    // The minifilter context contains pointer to a minifilter callback data
    // that might be allocated on the stack! So if this function 
    // would process the minifilter context then this fact must be
    // taken into account.
    //
    ASSERT( OcIsFlagOn( NodeContext->Flags, OcNodeCtxHookedDriverFlag ) || 
            OcIsFlagOn( NodeContext->Flags, OcNodeCtxPnPFilterDriverFlag ) );

    IsPnPCtx = OcIsFlagOn( NodeContext->Flags, OcNodeCtxPnPFilterDriverFlag );

#if _STD_ALLOCATOR_
    NewNodeCtx = (POC_NODE_CTX)ExAllocateFromPoolWithTag( NonPagedPool,
                       IsPnPCtx ? sizeof( OC_PNPFLTR_DRV_NODE_CTX ) : sizeof( OC_HOOKED_DRV_NODE_CTX ),
                       Global.IoManager.MemoryTag );
#else//_STD_ALLOCATOR_

    //
    // for debug purposes use the standard memory allocator to track memory leak with driver verifier
    //
    NewNodeCtx = (POC_NODE_CTX)ExAllocateFromNPagedLookasideList( 
        IsPnPCtx? 
        &Global.IoManager.NodeCtxPnPDrvLookasideList:
        &Global.IoManager.NodeCtxHookedDrvLookasideList );

#endif//_STD_ALLOCATOR_

    if( NULL == NewNodeCtx )
        return NULL;

    RtlCopyMemory( NewNodeCtx, NodeContext, 
                   IsPnPCtx ? sizeof( OC_PNPFLTR_DRV_NODE_CTX ) : sizeof( OC_HOOKED_DRV_NODE_CTX ) );

    OcSetFlag( NewNodeCtx->Flags, OcNodeAloocatedFromPool );

    if( NULL != NewNodeCtx->OcOriginalDeviceObject ){

        //
        // reference the pointer to OcDevice object
        //
        OcSetFlag( NewNodeCtx->Flags, OcNodeOcDeviceObjectReferenced );
        OcObReferenceObject( NewNodeCtx->OcOriginalDeviceObject );
    }

#if DBG
    InterlockedIncrement( &Global.IoManager.NumberOfAllocatedCtxEntries );

    //
    // insert this new context in the list
    //
    {
        KIRQL    OldIrql;
        KeAcquireSpinLock( &Global.IoManager.ListSpinLock, &OldIrql );
        InsertTailList( &Global.IoManager.NodeCtxListHead, &NewNodeCtx->ListEntry );
        KeReleaseSpinLock( &Global.IoManager.ListSpinLock, OldIrql );
    }
#endif//DBG

    return NewNodeCtx;
}

//-------------------------------------------------

VOID
OcCrFreeNodeContextCopy(
    IN POC_NODE_CTX    NodeContext
    )
{
    BOOLEAN    IsPnPCtx;

    ASSERT( OC_NODE_CTX_SIGNATURE == NodeContext->Signature );
    ASSERT( OcIsFlagOn( NodeContext->Flags, OcNodeAloocatedFromPool ) );

    if( OcIsFlagOn( NodeContext->Flags, OcNodeOcDeviceObjectReferenced ) ){

        ASSERT( NULL != NodeContext->OcOriginalDeviceObject );
        OcObDereferenceObject( NodeContext->OcOriginalDeviceObject );
    }

    if( !OcIsFlagOn( NodeContext->Flags, OcNodeAloocatedFromPool ) )
        return;

    IsPnPCtx = OcIsFlagOn( NodeContext->Flags, OcNodeCtxPnPFilterDriverFlag );

#if DBG
    InterlockedDecrement( &Global.IoManager.NumberOfAllocatedCtxEntries );
    //
    // remove the context from the list
    //
    {
        KIRQL    OldIrql;
        KeAcquireSpinLock( &Global.IoManager.ListSpinLock, &OldIrql );
        RemoveEntryList( &NodeContext->ListEntry );
        KeReleaseSpinLock( &Global.IoManager.ListSpinLock, OldIrql );
    }
#endif//DBG

#if _STD_ALLOCATOR_
    //
    // for debug purposes use standard memory allocator to track memory leak with driver verifier
    //
    ExFreePoolWitTag( NodeContext, Global.IoManager.MemoryTag );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( NodeContext );
#else
    ExFreeToNPagedLookasideList( IsPnPCtx? 
                                 &Global.IoManager.NodeCtxPnPDrvLookasideList:
                                 &Global.IoManager.NodeCtxHookedDrvLookasideList ,
                                 NodeContext );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( NodeContext );
#endif
}

//-------------------------------------------------

NTSTATUS
OcCrIoNodeFunctionWR(
    IN ULONG_PTR    NodeFunction,//PtrOcCrNodeFunction
    IN ULONG_PTR    NodeDeviceObject,//POC_DEVICE_OBJECT
    IN ULONG_PTR    NodeContext//POC_NODE_CTX
    )
    /*
    used to call a pended node function in a worker thread,
    N.B. pending node function also pends OcCrProcessRequestWithResult 
    */
{

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OC_NODE_CTX_SIGNATURE == ((POC_NODE_CTX)NodeContext)->Signature );
    ASSERT( OcIsFlagOn( ((POC_NODE_CTX)NodeContext)->Flags, OcNodePendingFlag ) );
    ASSERT( IO_TYPE_IRP == ((POC_NODE_CTX)NodeContext)->RequestData.Irp->Type );
    ASSERT( 0x0 != ( (IoGetCurrentIrpStackLocation(((POC_NODE_CTX)NodeContext)->RequestData.Irp))->Control & SL_PENDING_RETURNED ) );

    if( OcIsFlagOn( ((POC_NODE_CTX)NodeContext)->Flags, OcNodeCtxPnPFilterDriverFlag ) ){

        //
        // this is a request to the PnP filter driver
        //

        POC_PNPFLTR_DRV_NODE_CTX    PnPFilterDrvContext = (POC_PNPFLTR_DRV_NODE_CTX)NodeContext;

        ASSERT( OcCrNodeFunctionPnPFilterDevice == (PtrOcCrNodeFunction)NodeFunction );

        OcCrProcessIrpBasedDeviceRequest( (POC_DEVICE_OBJECT)NodeDeviceObject,
                                          &PnPFilterDrvContext->Common,
                                          (PtrOcCrNodeFunction)NodeFunction,
                                          OcCrPnPFilterDrvRequestCompletedOrPending );

        if( OcFilterReturnCode != PnPFilterDrvContext->PnPFltrIrpDecision ){

            //
            // if the stack location must be skipped - skip it,
            // if it must be copied to next, then it must has been 
            // copied by those who made this decision because copying 
            // now will remove the completion routine
            //
            if( OcFilterSendIrpToLowerSkipStack == PnPFilterDrvContext->PnPFltrIrpDecision ){

                IoSkipCurrentIrpStackLocation( PnPFilterDrvContext->Common.RequestData.Irp );
            }

            //
            // The Irp has been pended, so I must
            // call the lower driver's device, because the PnP
            // filter returned STATUS_PENDING to the caller and 
            // didn't call the lower driver
            //
            IoCallDriver( PnPFilterDrvContext->NextLowerDeviceObject,
                          PnPFilterDrvContext->Common.RequestData.Irp );
            //
            // the Irp might has been completed!
            //
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PnPFilterDrvContext->Common.RequestData.Irp );
        }
        //
        // the Irp might has been completed!
        //
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PnPFilterDrvContext->Common.RequestData.Irp );

    } else {

        //
        // this is a request to a hooked driver
        //

        POC_HOOKED_DRV_NODE_CTX    HookedDrvContext = (POC_HOOKED_DRV_NODE_CTX)NodeContext;

        ASSERT( OcCrNodeFunctionHookedDriver == (PtrOcCrNodeFunction)NodeFunction );

        OcCrProcessIrpBasedDeviceRequest( (POC_DEVICE_OBJECT)NodeDeviceObject,
                                          &HookedDrvContext->Common,
                                          (PtrOcCrNodeFunction)NodeFunction,
                                          OcCrHookedDrvRequestCompletedOrPending );

        if( OcIrpDecisionReturnStatus != HookedDrvContext->HookedDrvIrpDecision ){

            PDRIVER_DISPATCH     OriginalDriverDispatch;

            //
            // retreive the original dispatch function
            //
            OriginalDriverDispatch = Global.DriverHookerExports.RetreiveOriginalDispatch(
                HookedDrvContext->HookedSysDeviceObject->DriverObject,
                (IoGetCurrentIrpStackLocation( HookedDrvContext->Common.RequestData.Irp ))->MajorFunction );

            //
            // call the original driver
            //
            OriginalDriverDispatch( HookedDrvContext->HookedSysDeviceObject,
                                    HookedDrvContext->Common.RequestData.Irp );

            //
            // the Irp might has been completed!
            //
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( HookedDrvContext->Common.RequestData.Irp );
        }
        //
        // the Irp might has been completed!
        //
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( HookedDrvContext->Common.RequestData.Irp );
    }

    //
    // the object was locked from deleting in OcCrIoPostponeNodeFunctionInWorkerThread
    //
    OcRlReleaseRemoveLock( &((POC_DEVICE_OBJECT)NodeDeviceObject)->RemoveLock.Common );

    //
    // the object was referenced in OcCrIoPostponeNodeFunctionInWorkerThread
    //
    OcObDereferenceObject( (POC_DEVICE_OBJECT)NodeDeviceObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( NodeDeviceObject );

    //
    // free the context allocated in OcCrIoPostponeNodeFunctionInWorkerThread
    //
    OcCrFreeNodeContextCopy( (POC_NODE_CTX)NodeContext );

    return STATUS_SUCCESS;
}

//-------------------------------------------------

NTSTATUS
OcCrIoPostponeNodeFunctionInWorkerThread(
    IN PtrOcCrNodeFunction    NodeFunction,
    IN POC_DEVICE_OBJECT    NodeDeviceObject,
    IN PVOID    Context
    )
    /*
    If STATUS_SUCCESS is returned the function has been pended in a worker thread,
    else an error is returned
    */
{
    NTSTATUS            RC = STATUS_SUCCESS;
    POC_NODE_CTX        CommonNodeContext = (POC_NODE_CTX)Context;
    POC_NODE_CTX        AllocatedNodeContext;
    POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemList;
#if DBG
    BOOLEAN             IsDeviceLocked = FALSE;
#endif//DBG

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_NODE_CTX_SIGNATURE == CommonNodeContext->Signature );
    //
    // The minifilter context contains pointer to a minifilter callback data
    // that might be allocated on the stack! So if this function 
    // would process the minifilter context then this fact must be
    // taken into account.
    //
    ASSERT( OcIsFlagOn( CommonNodeContext->Flags, OcNodeCtxHookedDriverFlag ) || 
            OcIsFlagOn( CommonNodeContext->Flags, OcNodeCtxPnPFilterDriverFlag ) );
    ASSERT( IO_TYPE_IRP == CommonNodeContext->RequestData.Irp->Type );

    //
    // do not postpone an Irp in a worker thread if the top level IRP is
    // not NULL, or else you are on thin ice of preacquired resources!
    // this check is valid only for FSD, for devices the top level Irp can be non NULL.
    // ASSERT( NULL == IoGetTopLevelIrp() );

    //
    // pay attention that the decision( PnPFltrIrpDecision & HookedDrvIrpDecision ) 
    // code is copied in a new allocated structure and only after this it will be 
    // changed by this function in an old strucutre
    //
    AllocatedNodeContext = OcCrCreateNodeCtxCopy( CommonNodeContext );
    if( NULL == AllocatedNodeContext )
        return STATUS_INSUFFICIENT_RESOURCES;

    //
    // lock the device from removing when receiving IRP_MN_REMOVE_DEVICE
    //
    RC = OcRlAcquireRemoveLock( &NodeDeviceObject->RemoveLock.Common );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

#if DBG
    IsDeviceLocked = TRUE;
#endif//DBG

    //
    // now I use the thread pool with the shared queue, might
    // be in the future it will be replaced
    //
    PtrWorkItemList = OcTplReferenceSharedWorkItemList( Global.IoRequestsThreadsPoolObject );
    if( NULL != PtrWorkItemList ){

        //
        // remember that the completion will be pended
        //
        OcSetFlag( AllocatedNodeContext->Flags, OcNodePendingFlag );

        //
        // reference the device object, it will be derefrenced in OcCrIoNodeFunctionWR
        //
        OcObReferenceObject( NodeDeviceObject );

        //
        // mark the IRP as pending
        //
        IoMarkIrpPending( CommonNodeContext->RequestData.Irp );

        //
        // post IRP in a special dedicated thread pool
        //
        RC = OcWthPostWorkItemParam3( PtrWorkItemList,
                                      FALSE,
                                      OcCrIoNodeFunctionWR,
                                      (ULONG_PTR)NodeFunction,
                                      (ULONG_PTR)NodeDeviceObject,
                                      (ULONG_PTR)AllocatedNodeContext );
        ASSERT( NT_SUCCESS( RC ) );
        if( !NT_SUCCESS( RC ) ){

                //
                // there was an error!
                //

                //
                // forget about attempt to postpone the request
                //
                OcClearFlag( AllocatedNodeContext->Flags, OcNodePendingFlag );

                //
                // dereference the refernced object
                //
                OcObDereferenceObject( NodeDeviceObject );

                ASSERT( !"Something went wrong with the worker threads!" );

        } else {

            //
            // All was successful
            //

            ASSERT( !OcIsFlagOn( CommonNodeContext->Flags, OcNodeCtxMinifilterDriverFlag ) );

            //
            // the context will be( or has been ) freed in OcCrIoNodeFunctionWR
            //
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( AllocatedNodeContext );

            //
            // say the PnP filter or to the caller to 
            // return the STATUS_PENDING code
            //
            CommonNodeContext->StatusCode = STATUS_PENDING;
            if( OcIsFlagOn( CommonNodeContext->Flags, OcNodeCtxPnPFilterDriverFlag ) ){

                //
                // this is a call from the PnP filter for the filter's device
                //
                ((POC_PNPFLTR_DRV_NODE_CTX)CommonNodeContext)->PnPFltrIrpDecision = OcFilterReturnCode;

            } else {

                ASSERT( OcIsFlagOn( CommonNodeContext->Flags, OcNodeCtxHookedDriverFlag ) );

                //
                // this is a call from the hooker for a hooked driver
                //
                ((POC_HOOKED_DRV_NODE_CTX)CommonNodeContext)->HookedDrvIrpDecision = OcIrpDecisionReturnStatus;

            }

        }

        OcObDereferenceObject( PtrWorkItemList );

    } else {

        //
        // there was an error!
        //
        ASSERT( !"Something went wrong with the worker threads!" );
        RC = STATUS_INSUFFICIENT_RESOURCES;
    }

__exit:
    if( !NT_SUCCESS( RC ) ){

        //
        // release the device lock
        //
        ASSERT( IsDeviceLocked );
        OcRlReleaseRemoveLock( &NodeDeviceObject->RemoveLock.Common );

        //
        // free the allocated context
        //
        OcCrFreeNodeContextCopy( (POC_NODE_CTX)AllocatedNodeContext );
    }

    return RC;
}

//-------------------------------------------------

