/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
07.03.2006 ( March )
 Start
*/

/*
This file contains the code for hooking the completion routine
in arbitrary IRP's stack location

The "CMHK" and "CmHk" acronyms stand for "CoMpletion HooKer"
*/

#include "struct.h"
#include "proto.h"

//-------------------------------------------------------------

#define  OC_CMHK_CTX_SIGNATURE    0xABCDEFEE
#define  OC_CMHK_MEM_TAG          'kHmC'

//-------------------------------------------------------------

typedef enum{
    OcCmHkNativeCalledFlag = 0x1,
    OcCmHkIrpCompletionPended = 0x2,
    OcCmHkAllFlags = 0xFFFFFFFF
} OcCmHkContextFlags;

//-------------------------------------------------------------

typedef struct _OC_CMHK_CONTEXT{
#if DBG
    //
    // must be OC_CMHK_CTX_SIGNATURE
    //
    ULONG_PTR                     Signature;
    //
    // in debug version all allocated entries are 
    // connected in a linked list headed in 
    // OcCmHkGlobal.ListOfAllocatedContexts
    //
    LIST_ENTRY                    ListEntry;
#endif//DBG
    OC_HOOKER_COMPLETION_CONTEXT  HookerContext;
    PIRP                          Irp;
    PIO_COMPLETION_ROUTINE        OriginalCompletionRoutine;
    UCHAR                         OriginalControl;
    PVOID                         OriginalContext;
    OcCmHkIrpCompletionCallback   Callback;
    PVOID                         CallbackContext;
    PDEVICE_OBJECT                DeviceObject;
    PKEVENT                       CompletionEvent OPTIONAL;
    ULONG                         Flags;
    KIRQL                         Irql;

} OC_CMHK_CONTEXT, *POC_CMHK_CONTEXT;

//-------------------------------------------------------------

typedef struct _OC_CMHK_GLOBAL{

    ULONG    MemTag;

    //
    // lookaside list for the completion context
    //
    NPAGED_LOOKASIDE_LIST   ContextLookasideList;

    //
    // the allocated structures counter is used only in debug build
    //
    ULONG    ContextsCurrentlyAllocated;

    ULONG    Initialized;
#if DBG
    //
    // head for the list of allocated contexts
    //
    LIST_ENTRY    ListOfAllocatedContexts;

    //
    // the spin lock protecting the ListOfAllocatedContexts list
    //
    KSPIN_LOCK    ListSpinLock;
#endif//DBG

} OC_CMHK_GLOBAL, *POC_CMHK_GLOBAL;

//-------------------------------------------------------------

NTSTATUS
OcCmHkCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context//OC_CMHK_CONTEXT
    );

//-------------------------------------------------------------

static OC_CMHK_GLOBAL    OcCmHkGlobal;

//-------------------------------------------------------------

VOID
OcCmHkInitializeCompletionHooker()
{

#if DBG
    InitializeListHead( &OcCmHkGlobal.ListOfAllocatedContexts );
    KeInitializeSpinLock( &OcCmHkGlobal.ListSpinLock );
#endif//DBG

    OcCmHkGlobal.MemTag = OC_CMHK_MEM_TAG;

    ExInitializeNPagedLookasideList( &OcCmHkGlobal.ContextLookasideList, 
                                     NULL, 
                                     NULL, 
                                     0x0,
                                     sizeof( OC_CMHK_CONTEXT ),
                                     OcCmHkGlobal.MemTag,
                                     0x0 );

    OcCmHkGlobal.Initialized = 0x1;
}

//-------------------------------------------------------------

VOID
OcCmHkUninitializeCompletionHooker()
{
    if( 0x0 == OcCmHkGlobal.Initialized )
        return;

    OcCmHkGlobal.Initialized = 0x0;

    ASSERT( 0x0 == OcCmHkGlobal.ContextsCurrentlyAllocated );
    ASSERT( IsListEmpty( &OcCmHkGlobal.ListOfAllocatedContexts ) );

    ExDeleteNPagedLookasideList( &OcCmHkGlobal.ContextLookasideList );
}

//-------------------------------------------------------------

__forceinline
POC_CMHK_CONTEXT
OcCmHkAllocateCompletionHookContext()
{

    POC_CMHK_CONTEXT  CompletionContext;

#if _STD_ALLOCATOR_
    CompletionContext = (POC_CMHK_CONTEXT)ExAllocateFromPoolWithTag( NonPagedPool, sizeof( *CompletionContext ), OcCmHkGlobal.MemTag );
#else//_STD_ALLOCATOR_
    //
    // for debug purposes use standard memory allocator to track memory leak with driver verifier
    //
    CompletionContext = (POC_CMHK_CONTEXT)ExAllocateFromNPagedLookasideList( &OcCmHkGlobal.ContextLookasideList );
#endif//_STD_ALLOCATOR_

    if( NULL != CompletionContext ){

        RtlZeroMemory( CompletionContext, sizeof( *CompletionContext ) );
#if DBG
        InterlockedIncrement( &OcCmHkGlobal.ContextsCurrentlyAllocated );
        CompletionContext->Signature = OC_CMHK_CTX_SIGNATURE;

        //
        // insert this new context in the list
        //
        {
            KIRQL    OldIrql;
            KeAcquireSpinLock( &OcCmHkGlobal.ListSpinLock, &OldIrql );
            InsertTailList( &OcCmHkGlobal.ListOfAllocatedContexts, &CompletionContext->ListEntry );
            KeReleaseSpinLock( &OcCmHkGlobal.ListSpinLock, OldIrql );
        }
#endif//DBG
    }

    ASSERT( CompletionContext );

    return CompletionContext;
}

//-------------------------------------------------------------

__forceinline
VOID
OcCmHkFreeCompletionContext(
    IN POC_CMHK_CONTEXT    CompletionContext
    )
{
    ASSERT( OC_CMHK_CTX_SIGNATURE == CompletionContext->Signature );
    ASSERT( CompletionContext );

#if DBG
    CompletionContext->Signature = 0x11111111;
    //
    // remove this context from the list
    //
    {
        KIRQL    OldIrql;
        KeAcquireSpinLock( &OcCmHkGlobal.ListSpinLock, &OldIrql );
        RemoveEntryList( &CompletionContext->ListEntry );
        KeReleaseSpinLock( &OcCmHkGlobal.ListSpinLock, OldIrql );
    }
#endif//DBG

    //
    // if the event is not NULL then set it in a signal state
    //
    if( NULL != CompletionContext->CompletionEvent ){

        KeSetEvent( CompletionContext->CompletionEvent, IO_DISK_INCREMENT, FALSE );
    }

#if _STD_ALLOCATOR_
    //
    // for debug purposes use standard memory allocator to track memory leak with driver verifier
    //
    ExFreePoolWitTag( CompletionContext, OcCmHkGlobal.MemTag );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( CompletionContext );
#else
    ExFreeToNPagedLookasideList( &OcCmHkGlobal.ContextLookasideList, CompletionContext );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( CompletionContext );
#endif

#if DBG
    InterlockedDecrement( &OcCmHkGlobal.ContextsCurrentlyAllocated );
#endif//DBG
}

//-------------------------------------------------------------

NTSTATUS
OcCmHkHookCompletionRoutineInCurrentStack(
    IN PIRP    Irp,
    IN OcCmHkIrpCompletionCallback    Callback,
    IN PVOID    CallbackContext
    )
    /*
    the caller must supply the Callback and call
    OcCmHkContinueCompletion inside it or after
    it has been called, in the former case the
    value returned by OcCmHkContinueCompletion 
    must be returned in the latter the 
    STATUS_MORE_PROCESSING_REQUIRED must be 
    returned from the callback
    */
{
    NTSTATUS          RC;
    POC_CMHK_CONTEXT  CompletionContext;

    //
    // lock the driver from the unloading, 
    // the lock will be released in OcCmHkContinueCompletion
    //
    RC = OcRlAcquireRemoveLock( &Global.RemoveLock.Common );
    if( !NT_SUCCESS( RC ) ){

        //
        // driver is being unloaded, return the error
        //
        return RC;
    }

    CompletionContext = OcCmHkAllocateCompletionHookContext();
    if( NULL == CompletionContext ){

        //
        // release the acquired lock
        //
        OcRlReleaseRemoveLock( &Global.RemoveLock.Common );
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    CompletionContext->Irp = Irp;
    CompletionContext->OriginalCompletionRoutine = IoGetCurrentIrpStackLocation( Irp )->CompletionRoutine;
    CompletionContext->OriginalContext = IoGetCurrentIrpStackLocation( Irp )->Context;
    CompletionContext->OriginalControl = IoGetCurrentIrpStackLocation( Irp )->Control;

    CompletionContext->Callback = Callback;
    CompletionContext->CallbackContext = CallbackContext;

    CompletionContext->HookerContext.HookerCookie = Global.DriverHookerExports.Cookie;
    CompletionContext->HookerContext.CompletionRoutine = OcCmHkCompletionRoutine;
    CompletionContext->HookerContext.Context = CompletionContext;
    CompletionContext->HookerContext.Depth = 0x0;

    if( !Global.DriverHookerExports.AcquireCookieForCompletion( &CompletionContext->HookerContext ) ){

        //
        // hooker denied to track the completion for this cookie,
        // this means that the disconnection has been started
        //

        //
        // release the acquired lock
        //
        OcRlReleaseRemoveLock( &Global.RemoveLock.Common );

        //
        // free the context
        //
        OcCmHkFreeCompletionContext( CompletionContext );

        //
        // return the code that reports that the disconnection has been started
        //
        return STATUS_DELETE_PENDING;
    }

    {
        UCHAR Control;
        //
        // Hook Completion routine in current stack location.
        // Do not forget to clear Control on completion if you set completion routine manually. 
        //
        IoSkipCurrentIrpStackLocation(Irp);

        //
        // save the Control flags, because the IoSetCompletionRoutine rewrites them
        //
        Control = ( IoGetNextIrpStackLocation( Irp )->Control & 
                  (~( SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR | SL_INVOKE_ON_CANCEL )) );

        //
        // set the hooker's completion routine
        //
        IoSetCompletionRoutine( Irp,
                                Global.DriverHookerExports.HookerCompletionRoutine,
                                &CompletionContext->HookerContext,
                                TRUE, TRUE, TRUE );

        //
        // add the old flags to the new ones
        //
        IoGetNextIrpStackLocation( Irp )->Control |= Control;

        IoSetNextIrpStackLocation(Irp);
    }

    if( Global.DriverHookerExports.HookerCompletionRoutine == CompletionContext->OriginalCompletionRoutine ){

        CompletionContext->HookerContext.Depth = (CONTAINING_RECORD( CompletionContext->OriginalContext, OC_CMHK_CONTEXT, HookerContext ))->HookerContext.Depth + 0x1;
        ASSERT( CompletionContext->HookerContext.Depth <= 0x14 );
    }

    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcCmHkCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context//OC_CMHK_CONTEXT
    )
{
    NTSTATUS            RC;
    POC_CMHK_CONTEXT    CompletionContext = (POC_CMHK_CONTEXT)Context;

    ASSERT( NULL != CompletionContext );
    ASSERT( OC_CMHK_CTX_SIGNATURE == CompletionContext->Signature );
    ASSERT( NULL != CompletionContext->Callback );
    ASSERT( NULL != CompletionContext->Irp );

    //
    // There was found that some drivers( e.g. IrpTracker driver ) do not 
    // behave correctly and rehook us( in IoCallDriver which they hook ) 
    // after we hook their completion routines, 
    // because they think that they have not hooked completion routine,
    // as a result the following sequence of calls is created:
    // HostileDriverHook->OcCmHkCompletionRoutine->HostileDriverHook->OcCmHkCompletionRoutine( error!!! )
    // and the second call of the HostileDriverHook results in a second call of 
    // ThisDriverHook with the parameters used in the first call.
    //
    // The following solution will not protect if in the second call of the HostileDriverHook 
    // the STATUS_MORE_PROCESSING_REQUIRED is returned before calling ThisDriverHook.
    //
    ASSERT( !OcIsFlagOn( CompletionContext->Flags, OcCmHkNativeCalledFlag ) );
    if( OcIsFlagOn( CompletionContext->Flags, OcCmHkNativeCalledFlag ) ){

        //
        // Now we have to mark Irp as pending if necessary
        //
        if( Irp->PendingReturned && (Irp->CurrentLocation < (CCHAR) (Irp->StackCount + 1))) {

            IoMarkIrpPending( Irp );
        }

        return STATUS_SUCCESS;

    }

    ASSERT( FALSE == OcIsFlagOn( CompletionContext->Flags, OcCmHkNativeCalledFlag ) );
    OcSetFlag( CompletionContext->Flags, OcCmHkNativeCalledFlag );

    //
    // save the device object that is used for completion
    // it may be NULL if this is beyond a top of the IRP stacks
    //
    CompletionContext->DeviceObject = DeviceObject;

    //
    // save the Irql of the caller it is needed to
    // make a decision about the ability of 
    // postponing this completion
    //
    CompletionContext->Irql = KeGetCurrentIrql();

    //
    // call the callback
    //
    RC = CompletionContext->Callback( CompletionContext, 
                                      CompletionContext->CallbackContext );
    //
    // Completion context may be freed in OcCmHkContinueCompletion
    // which has been called in callback or will be called later
    //
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( CompletionContext );

    ASSERT( STATUS_SUCCESS == RC ||
            STATUS_MORE_PROCESSING_REQUIRED == RC );

    return RC;
}

//-------------------------------------------------------------

NTSTATUS
OcCmHkContinueCompletion(
    IN PVOID Context//actually OC_CMHK_CONTEXT
    )
    /*
    this function is called to continue IRP completion
    usually it is called in a callback or in a worker
    thread for a case of pended completion
    */
{
    NTSTATUS                  RC;
    POC_CMHK_CONTEXT          CompletionContext = (POC_CMHK_CONTEXT)Context;
    PIO_COMPLETION_ROUTINE    OriginalCompletionRoutine = CompletionContext->OriginalCompletionRoutine;
    UCHAR                     OriginalControl = CompletionContext->OriginalControl;
    PVOID                     OriginalContext = CompletionContext->OriginalContext;
    PIRP                      Irp = CompletionContext->Irp;

    ASSERT( OC_CMHK_CTX_SIGNATURE == CompletionContext->Signature );
    ASSERT( NULL != CompletionContext->Irp );

    if( OcIsFlagOn( CompletionContext->Flags, OcCmHkIrpCompletionPended ) ){

        BOOLEAN     PendingReturned = Irp->PendingReturned;

        //
        // check that there is a valid stack under the current stack pointer
        //
        ASSERT( ( Irp->CurrentLocation > 0x0 ) && 
                ( Irp->CurrentLocation <= (CCHAR) (Irp->StackCount + 1) ) );

        //
        // if the completion has been pended the life is more simple,
        // restore the next stack location, move the stack pointer to it
        // and call IoCompleteRequest.
        //
        IoSetCompletionRoutine( Irp, 
                                OriginalCompletionRoutine, 
                                OriginalContext,
                                0x0 != (OriginalControl & SL_INVOKE_ON_SUCCESS),
                                0x0 != (OriginalControl & SL_INVOKE_ON_ERROR),
                                0x0 != (OriginalControl & SL_INVOKE_ON_CANCEL) );

        IoSetNextIrpStackLocation( Irp );

        //
        // Now we have to mark the Irp as pending if necessary, because the 
        // stack location has been moved to older one where the pending
        // flag might has been overwritten
        //
        if( PendingReturned && (Irp->CurrentLocation < (CCHAR) (Irp->StackCount + 1))) {

            IoMarkIrpPending( Irp );
        }

        //
        // restart the Irp completion process, if this is a syncronous IRP completion
        // that has been pended the CompletionEvent will be set in a signal state 
        // in OcCmHkFreeCompletionContext just after inserting completion APC in the context
        // of a thread that is waiting for returning from IoCallDriver, then the APC 
        // will be executed and then IoCallDriver returns the status code.
        //
        IoCompleteRequest( Irp, IO_DISK_INCREMENT );

        //
        // actually, the returned value for this case is unimportant
        // because the function is being called inside the worker
        // thread routine but not inside IoCompleteRequest
        //
        RC = STATUS_MORE_PROCESSING_REQUIRED;

    } else {

        //
        // Call the native routine if one exists.
        //
        if ( OriginalCompletionRoutine != NULL &&
            ((NT_SUCCESS(Irp->IoStatus.Status) && (OriginalControl & SL_INVOKE_ON_SUCCESS)) ||
            (!NT_SUCCESS(Irp->IoStatus.Status) && (OriginalControl & SL_INVOKE_ON_ERROR)) ||
            (Irp->Cancel && (OriginalControl & SL_INVOKE_ON_CANCEL) ) ) )
        {

            RC = OriginalCompletionRoutine( CompletionContext->DeviceObject,
                                            Irp,
                                            OriginalContext );
            //
            // do not touch Irp if after calling native completion routine, it may be invalid
            //
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Irp );

        } else {

            //
            // Now we have to mark the Irp as pending if necessary
            //
            if( Irp->PendingReturned && (Irp->CurrentLocation < (CCHAR) (Irp->StackCount + 1))) {

                IoMarkIrpPending( Irp );
            }
            RC = STATUS_SUCCESS;
        }
    }

    //
    // do not touch the Irp after calling the native completion routine
    // the IRP might be already completed
    //
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Irp );

    ASSERT( STATUS_SUCCESS == RC ||
            STATUS_MORE_PROCESSING_REQUIRED == RC );

    //
    // Free memory, do this at the end to insure that the structure is valid 
    // in case of recursive call by the misbehaiving drivers described above.
    //
    OcCmHkFreeCompletionContext( CompletionContext );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( CompletionContext );

    //
    // release the acquired lock, allow the driver to unload
    //
    OcRlReleaseRemoveLock( &Global.RemoveLock.Common );

    return RC;
}

//-------------------------------------------------------------

NTSTATUS
OcCmHkCallCompletionCallbackWR(
    IN ULONG_PTR    Callback,//OcCmHkIrpCompletionCallback
    IN ULONG_PTR    CompletionContextV,//POC_CMHK_CONTEXT
    IN ULONG_PTR    CallbackContext //PVOID
    )
    /*
    used to call a pended callback in a worker thread
    */
{
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OC_CMHK_CTX_SIGNATURE == ((POC_CMHK_CONTEXT)CompletionContextV)->Signature );
    ASSERT( OcIsFlagOn( ((POC_CMHK_CONTEXT)CompletionContextV)->Flags, OcCmHkIrpCompletionPended ) );

    return ((OcCmHkIrpCompletionCallback)Callback)( (PVOID)CompletionContextV,
                                                    (PVOID)CallbackContext );
}

//-------------------------------------------------------------

VOID
OcChHkPrepareCompletionContextForPostponing(
    IN PVOID    CompletionContextV,
    IN POC_CMHK_WAIT_BLOCK    CompletionWaitBlock OPTIONAL
   )
{
    POC_CMHK_CONTEXT    CompletionContext = (POC_CMHK_CONTEXT)CompletionContextV;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_CMHK_CTX_SIGNATURE == CompletionContext->Signature );
    ASSERT( OcCmHkCanCompletionBePostponed( CompletionContext ) );

    //
    // remember that the completion will be postponed
    //
    OcSetFlag( CompletionContext->Flags, OcCmHkIrpCompletionPended );

    //
    // The first.
    //  Mark the Irp as pending, because if a synchronous IRP
    // is being completed and has not been marked as pending 
    // then postponing completion in another thread is actually 
    // making a pending IRP - the IRP will be completed in 
    // the context of another thread and the APC must be 
    // inserted in the original thread.
    //  So make pending IRP, do not set the flag in Control
    // field because there might be no valid stack for this
    // request, instead mark the whole Irp as IoCompleteRequest
    // does.
    //
    //  The second.
    // If the IRP has not been marked as pended
    // then the IoCallDriver will not return STATUS_PENDING and 
    // the thread( that calls IoCallDriver ) will not wait for 
    // completion, so the caller of this function must wait for 
    // the completion
    //
    ASSERT( !( FALSE == CompletionContext->Irp->PendingReturned && 
               NULL == CompletionWaitBlock ) );

    if( FALSE == CompletionContext->Irp->PendingReturned && 
        NULL != CompletionWaitBlock ){

        ASSERT( CompletionWaitBlock->CompletionEvent );
        ASSERT( CompletionContext->Irql <= APC_LEVEL );

        //
        // the caller will have to wait
        //
        CompletionWaitBlock->WaitForCompletion = TRUE;

        //
        // the event will be set on a signal state after completing
        // the IRP and inserting a completion APC in the thread
        // which calls IoCallDriver
        //
        CompletionContext->CompletionEvent = CompletionWaitBlock->CompletionEvent;

        //
        // mark the Irp as pending
        //
        CompletionContext->Irp->PendingReturned = TRUE;
    }

    //
    // must be marked as pending to properly being
    // completed in the context of the thread which
    // is not the same then that issued this request
    //
    ASSERT( TRUE == CompletionContext->Irp->PendingReturned );
}

//-------------------------------------------------------------

NTSTATUS
OcCmHkPostponeCallbackInWorkerThread(
    IN OcCmHkIrpCompletionCallback    Callback,
    IN PVOID    CompletionContextV,//POC_CMHK_CONTEXT
    IN PVOID    CallbackContext,
    IN POC_CMHK_WAIT_BLOCK    CompletionWaitBlock
    )
    /*
    The caller must understand that transforming the completion for not 
    pended IRP that is sent and completed at DISPATCH_LEVEL is impossible!
    I assume that all drivers are of a good type and return STATUS_PENDING if
    they mark Irp as pending.

    The caller must supply the initialized WaitBlock and WaitBlock->WaitForCompletion 
    set to FALSE and if WaitForCompletion is set to TRUE the caller will have to wait on 
    WaitForCompletion->CompletionEvent. For this reason the above statement about
    not pended IRP at DISPATCH_LEVEL is valid.

    If STATUS_SUCCESS is returned the callback has been pended in a worker thread,
    else an error is returned.

    */
{
    NTSTATUS            RC = STATUS_SUCCESS;
    POC_CMHK_CONTEXT    CompletionContext = (POC_CMHK_CONTEXT)CompletionContextV;
    POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemList;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_CMHK_CTX_SIGNATURE == CompletionContext->Signature );
    ASSERT( NULL != CompletionContext->Irp );
    ASSERT( CompletionContext->CallbackContext == CallbackContext );
    ASSERT( FALSE == CompletionWaitBlock->WaitForCompletion );
    ASSERT( OcCmHkCanCompletionBePostponed( CompletionContext ) );

    //
    // now I use the thread pool with the shared queue, might
    // be in the future it will be replaced
    //
    PtrWorkItemList = OcTplReferenceSharedWorkItemList( Global.IrpCompletionThreadsPoolObject );
    if( NULL != PtrWorkItemList ){

        OcChHkPrepareCompletionContextForPostponing( CompletionContext,
                                                     CompletionWaitBlock );

        //
        // post the IRP in a special dedicated thread pool
        //
        if( !NT_SUCCESS( OcWthPostWorkItemParam3( PtrWorkItemList,
                              FALSE,
                              OcCmHkCallCompletionCallbackWR,
                              (ULONG_PTR)Callback,
                              (ULONG_PTR)CompletionContext,
                              (ULONG_PTR)CallbackContext ) ) ){

                //
                // there was an error!
                //
                OcClearFlag( CompletionContext->Flags, OcCmHkIrpCompletionPended );
                CompletionContext->CompletionEvent = NULL;
                CompletionWaitBlock->WaitForCompletion = FALSE;

                ASSERT( !"Something went wrong with the worker threads!" );
                RC = STATUS_INSUFFICIENT_RESOURCES;

        } else {
            //
            // the posting in a worker thread was successful,
            // do not touch the completion it might has been deallocated
            // in a worker thread
            //
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( CompletionContext );
        }

        OcObDereferenceObject( PtrWorkItemList );

    } else {

        //
        // there was an error!
        //
        ASSERT( !"Something went wrong with the worker threads!" );
        RC = STATUS_INSUFFICIENT_RESOURCES;
    }

    return RC;
}

//-------------------------------------------------------------

NTSTATUS
OcCmHkIrpCompletionTestCallback( 
    IN PVOID CompletionContext,// actually POC_CMHK_CONTEXT
    IN PVOID CallbackContext// CompletionContext->Context
    )
    /*
    This is an example of how to write completion callback.
    Pay atention on calling of OcCmHkContinueCompletion( CompletionContext )
    at the end.
    */
{
    BOOLEAN     CompleteHere;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    if( KeGetCurrentIrql() != PASSIVE_LEVEL && 
        OcCmHkCanCompletionBePostponed( CompletionContext ) ){

        NTSTATUS    RC;
        KEVENT      CompletionEvent;
        OC_CMHK_WAIT_BLOCK    CompletionWaitBlock;

        KeInitializeEvent( &CompletionEvent, SynchronizationEvent, FALSE );
        OcCmHkInitializeCompletionWaitBlock( &CompletionWaitBlock, &CompletionEvent );

        //
        // post the request in the worker thread
        //

        RC = OcCmHkPostponeCallbackInWorkerThread( OcCmHkIrpCompletionTestCallback,
                                                   CompletionContext,
                                                   CallbackContext,
                                                   &CompletionWaitBlock );
        ASSERT( NT_SUCCESS( RC ) );
        if( !NT_SUCCESS( RC ) ){

            CompleteHere = TRUE;

        } else {

            //
            // do not touch the completion context it might be completelly invalid
            // if the completion has been posted in another thread
            //
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( CompletionContext );

            CompleteHere = FALSE;

            if( TRUE == CompletionWaitBlock.WaitForCompletion ){

                ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

                KeWaitForSingleObject( &CompletionEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL );
            }//if( TRUE == CompletionWaitBlock->WaitForCompletion )
        }

    } else {

        //
        // we are at a passive level or completion can't be postponed
        //
        CompleteHere = TRUE;
    }

    if( CompleteHere )
        return OcCmHkContinueCompletion( CompletionContext );
    else
        return STATUS_MORE_PROCESSING_REQUIRED;
}

//-------------------------------------------------------------

PIRP
OcCmHkGetIrpFromCompletionContext(
    IN PVOID    CompletionContext
    )
{
    ASSERT( OC_CMHK_CTX_SIGNATURE == ((POC_CMHK_CONTEXT)CompletionContext)->Signature );
    ASSERT( IO_TYPE_IRP == ((POC_CMHK_CONTEXT)CompletionContext)->Irp->Type );

    return ((POC_CMHK_CONTEXT)CompletionContext)->Irp;
}

//-------------------------------------------------------------

KIRQL
OcCmHkGetIrqlFromCompletionContext(
    IN PVOID    CompletionContext
    )
{
    ASSERT( OC_CMHK_CTX_SIGNATURE == ((POC_CMHK_CONTEXT)CompletionContext)->Signature );

    return ((POC_CMHK_CONTEXT)CompletionContext)->Irql;
}

//-------------------------------------------------------------

