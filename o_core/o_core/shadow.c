/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
06.04.2007 ( April )
 Start
*/

/*
this file contains the code for the 
file operation objects management
*/
#include "struct.h"
#include "proto.h"

//-------------------------------------------

typedef struct _OC_SHADOWING_GLOBAL_DATA{

    struct {
        ULONG    Initialized:0x1;
    } Flags;

    POC_WORKER_THREAD_OBJECT     WriteWorkerThread;

} OC_SHADOWING_GLOBAL_DATA, *POC_SHADOWING_GLOBAL_DATA;

//-------------------------------------------

NTSTATUS
OcCrShadowWriterWR(
    IN ULONG_PTR    OperationObjectV//POC_OPERATION_OBJECT
    );

//-------------------------------------------

static OC_SHADOWING_GLOBAL_DATA     g_ShadowGlobal;

//-------------------------------------------

NTSTATUS
OcCrInitializeShadowingSubsystem()
{
    NTSTATUS    RC;

    ASSERT( OcWthIsWorkerThreadManagerInitialized() );

    if( !OcWthIsWorkerThreadManagerInitialized() )
        return STATUS_INVALID_PARAMETER;

    RC = OcWthCreateWorkerThread( 0x1,
                                  NULL,
                                  &g_ShadowGlobal.WriteWorkerThread );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    g_ShadowGlobal.Flags.Initialized = 0x1;

__exit:

    if( !NT_SUCCESS( RC ) ){

        //
        // sometning went wrong
        //
        OcCrUnInitializeShadowingSubsystem();
    }

    return RC;
}

//-------------------------------------------

VOID
OcCrUnInitializeShadowingSubsystem()
{
    ASSERT( 0x1 == g_ShadowGlobal.Flags.Initialized );
    ASSERT( OcObIsObjectManagerInitialized() );

    if( 0x0 == g_ShadowGlobal.Flags.Initialized )
        return;

    g_ShadowGlobal.Flags.Initialized = 0x0;

    if( NULL != g_ShadowGlobal.WriteWorkerThread ){

        OcObDereferenceObject( g_ShadowGlobal.WriteWorkerThread );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( g_ShadowGlobal.WriteWorkerThread );
    }
}

//-------------------------------------------

NTSTATUS
OcCrShadowRequest(
    __inout POC_OPERATION_OBJECT    OperationObject
    )
{
    NTSTATUS    RC = STATUS_SUCCESS;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_IS_POINTER_VALID( OperationObject ) );
    ASSERT( 0x1 == g_ShadowGlobal.Flags.Initialized );
    ASSERT( OC_IS_POINTER_VALID( g_ShadowGlobal.WriteWorkerThread ) );
    ASSERT( OC_IS_POINTER_VALID( g_ShadowGlobal.WriteWorkerThread->PtrWorkItemListObject ) );
    ASSERT( OcIsOperationShadowedAsWriteRequest( &OperationObject->SecurityParameters ) || 
            OcIsOperationShadowedAsReadRequest( &OperationObject->SecurityParameters ) );
    //
    // the ObjectSentInShadowingModule flag is set in this function
    //
    ASSERT( 0x0 == OperationObject->Flags.ObjectSentInShadowingModule );

    //
    // send it in the shadow queue
    //
    {

        //
        // notify all others that this operation is in the 
        // shadow queue
        //
        OperationObject->Flags.ObjectSentInShadowingModule = 0x1;

        //
        // reference before sending
        //
        OcObReferenceObject( OperationObject );

        //
        // TO DO - consider the possibility of inserting 
        // the operation object in a list using its internal field
        // and processing this list from a thread with a loop
        // that picks up the entries from the list( this might be 
        // achieved by posting a worker routine with a loop in 
        // the g_ShadowGlobal.WriteWorkerThread thread )
        // this will reduce the load on the system pools because
        // OcWthPostWorkItemParam1 uses the NP pool for memory 
        // allocation
        //
        RC = OcWthPostWorkItemParam1( g_ShadowGlobal.WriteWorkerThread->PtrWorkItemListObject,
                                      FALSE,
                                      OcCrShadowWriterWR,
                                      (ULONG_PTR)OperationObject );
        if( !NT_SUCCESS( RC ) ){

            //
            // return the reference to the previous value
            //
            OcObDereferenceObject( OperationObject );

            //
            // I will be unable to shadow this request
            //
            OperationObject->Flags.ObjectSentInShadowingModule = 0x0;
        }
    }

    if( NT_SUCCESS( RC ) ){

    }

    return RC;
}

//-------------------------------------------

NTSTATUS
OcCrShadowWriterWR(
    IN ULONG_PTR    OperationObjectV//POC_OPERATION_OBJECT
    )
    /*
    the OperationObjectV object has been referenced when 
    the request was sent in the worker thread, this function
    derefernces this object
    */
{
    NTSTATUS   RC = STATUS_SUCCESS;
    KIRQL      OldIrql;
    POC_OPERATION_OBJECT    OperationObject = (POC_OPERATION_OBJECT)OperationObjectV;
    BOOLEAN    CompleteHere = FALSE;
    KEVENT     Event;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( 0x1 == OperationObject->Flags.ObjectSentInShadowingModule );
    ASSERT( 0x0 == OperationObject->Flags.ShadowingInProgress );

    //
    // TO DO - reserve and lock the space in a shadow file
    //

    if( 0x0 == OperationObject->Flags.DataInPrivateBuffer ){

        //
        // synchronize with the completion code in OcCmHkIrpCompletionRoutine
        // or OcCrFsmfPostOperation and OcCrUnlockAndFreeMdlForOperation
        //
        KeAcquireSpinLock( &OperationObject->SpinLock, &OldIrql );
        {// start of the lock

            OperationObject->Flags.ShadowingInProgress = 0x1;

            //
            // the second check is made with the acquired lock for
            // a proper syncronization with the completion routine
            //
            if( 0x0 == OperationObject->Flags.DataInPrivateBuffer ){

                if( 0x1 == OperationObject->Flags.WaitForCopingInPrivateBuffers ){

                    //
                    // if somebody decides to copy buffers himself then he 
                    // could not find an opportunity to postpone the completion
                    // to the shadow thread
                    //
                    ASSERT( 0x0 == OperationObject->Flags.CompleteInShadowThread );

                    //
                    // the shadowing thread must wait for the buffer copying
                    // which is currently ongoing
                    // TO DO - do not wait for completion - reinsert the 
                    // request in the shadow thread's queue
                    //
                    KeInitializeEvent( &Event, SynchronizationEvent, FALSE );
                    OperationObject->CopingInPrivateBuffersCompleteEvent = &Event;

                } else if( 0x0 == OperationObject->Flags.CompleteInShadowThread ){

                    //
                    // request has not been completed yet,
                    // so I am not able to know whether it will be
                    // possible to postpone the completion,
                    // receive data here else the request might be completed
                    // before the data will be shadowed
                    //

                    //
                    // copy data in a non paged private buffer,
                    // TO DO - process an error
                    //
                    RC = OcCrProcessOperationObjectPrivateBuffers( OperationObject,
                                                                   TRUE );
                    ASSERT( NT_SUCCESS( RC ) );
                    //ASSERT( 0x1 == OperationObject->Flags.DataInPrivateBuffer );

                } else if( 0x1 == OperationObject->Flags.CompleteInShadowThread ){

                    //
                    // the request completion has been postponed in the shadow thread,
                    // synchronize with the completion code in OcCmHkIrpCompletionRoutine
                    // or OcCrFsmfPostOperation, as the request is being completed here
                    // there is no need to copy buffers in a private buffer
                    //
                    CompleteHere = TRUE;
                }

            }// if( 0x0 == OperationObject->Flags.DataInPrivateBuffer )

        }// end of the lock
        KeReleaseSpinLock( &OperationObject->SpinLock, OldIrql );

        //
        // wait for the buffers exchange completion
        //
        if( NULL != OperationObject->CopingInPrivateBuffersCompleteEvent ){

            //
            // OperationObject->Flags.WaitForCopingInPrivateBuffers might have any value here!!!
            //
            //

            KeWaitForSingleObject( OperationObject->CopingInPrivateBuffersCompleteEvent,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
        }
    }

    //
    // do all shadowing requests here!
    //

    //
    // SHADOW HERE, SHADOW HERE, SHADOW HERE
    // SHADOW HERE, SHADOW HERE, SHADOW HERE
    // SHADOW HERE, SHADOW HERE, SHADOW HERE
    // SHADOW HERE, SHADOW HERE, SHADOW HERE
    // SHADOW HERE, SHADOW HERE, SHADOW HERE
    // SHADOW HERE, SHADOW HERE, SHADOW HERE
    //

    //
    // complete the request, the completion routine has been already
    // called and it found that the request had not yet been shadowed
    // and returned STATUS_MORE_PROCESSING_REQUIRED
    //
    if( TRUE == CompleteHere ){

        ASSERT( NULL != OperationObject->CompletionContext );

        //
        // TO DO - post completion in another thread to not overload the write thread!
        //
        OcCmHkContinueCompletion( OperationObject->CompletionContext );
    }

    //
    // dereference the object referenced in OcCrShadowDeviceRequest
    //
    OcObDereferenceObject( OperationObject );

    return RC;
}

//-------------------------------------------

VOID
OcCrShadowCopingInPrivateBuffersCompleted(
    IN POC_OPERATION_OBJECT    OperationObject
    )
{

    PKEVENT    PtrEvent;
    KIRQL      OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( 0x1 == OperationObject->Flags.WaitForCopingInPrivateBuffers );

    //
    // synchronize with OcCrShadowWriterWR
    //
    KeAcquireSpinLock( &OperationObject->SpinLock, &OldIrql );
    {// start of the lock

        PtrEvent = OperationObject->CopingInPrivateBuffersCompleteEvent;

        //
        // the shadowing thread should not wait
        // if it has not not reach the point where it
        // should wait for buffers exchange
        //
        if( NULL == PtrEvent )
            OperationObject->Flags.WaitForCopingInPrivateBuffers = 0x0;

    }// end of the lock
    KeReleaseSpinLock( &OperationObject->SpinLock, OldIrql );

    //
    // set the event on which the shadowing thread 
    // is waiting for buffers exchange
    //
    if( NULL != PtrEvent ){

        ASSERT( 0x1 == OperationObject->Flags.WaitForCopingInPrivateBuffers );

        KeSetEvent( PtrEvent, IO_DISK_INCREMENT, FALSE );

    }//if( NULL != PtrEvent )

}

//-------------------------------------------

NTSTATUS
OcCrShadowProcessOperationBuffersForFsd(
    IN POC_OPERATION_OBJECT    OperationObject
    )
    /*
    the function is called during the request completion,
    the function prepares data bufers for shadowing if needed
    and synchronizes execution with the shadowing thread
    */
{
    BOOLEAN    CopyInPrivateBuffers = FALSE;
    KIRQL      OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // if the operation should not be shadowed or 
    // the buffers have been already copied then
    // return the successful code, syncronization
    // while accessing OperationObject->Flags.DataInPrivateBuffer
    // is not needed, there can't be contention on this field
    //
    if( 0x0 == OperationObject->Flags.ObjectSentInShadowingModule || 
        0x1 == OperationObject->Flags.DataInPrivateBuffer )
        return STATUS_SUCCESS;

    //
    // It is ridiculous but the Microsoft guys did the following thing:
    // the operation can't be postponed by FltQueueDeferredIoWorkItem 
    // if any of the following statements is true
    //   - the operation is not an IRP-based I/O operation. 
    //   - the operation is a paging I/O operation. 
    //   - the TopLevelIrp field of the current thread is not NULL.
    //   - the target instance for the I/O operation is being torn down.
    // I agree with the 1st and 4th, but the 2nd and 3rd are ridiculous because
    // there is no any confinement for completing paging IO - it might be
    // completed in the context of any thread, even the one with non NULL TopIrp.
    // But they have done this and it is impossible to use 
    // FltQueueDeferredIoWorkItem because most of the shadowed 
    // write requests are paging IO. And there is no any information
    // about Cdb queues using for completion postponing.
    //
    // So I adopted the following technique - if the request's
    // shadowing has been done or data has been copied in 
    // a private buffer then continue completion if the
    // shadowing has not been done yet - allocate buffers
    // from Paging or NP pool depending on the IRQL and replace
    // them in the operation objects, do not wait here for 
    // shadowing completion -this might result in reducing the
    // system throughput or even deadlock if the ModPW or MapPW
    // has been stopped
    //

    //
    // synchronize with the shadow code in OcCrShadowWriterWR
    //

    KeAcquireSpinLock( &OperationObject->SpinLock, &OldIrql );
    {// start of the lock

        //
        // The valid behaviour for checking DataInPrivateBuffer
        // after the request was sent to a lower subsystem for processing -
        // is with the spin lock acquired to synchronize with the shadow
        // trhread worker routine.
        // The ShadowingInProgress flag is also checked - if it is set
        // then the shadowing thread has made its decision about this 
        // operation.
        //
        if( 0x0 == OperationObject->Flags.DataInPrivateBuffer && 
            0x0 == OperationObject->Flags.ShadowingInProgress ){

            ASSERT( 0x0 == OperationObject->Flags.ShadowingInProgress );

            //
            // exchange buffers, see above comments
            //
            OperationObject->Flags.WaitForCopingInPrivateBuffers = 0x1;
            CopyInPrivateBuffers = TRUE;
        }
    }// end of the lock
    KeReleaseSpinLock( &OperationObject->SpinLock, OldIrql );

    //
    // exchange the buffers to provide the shadowing thread 
    // with the valid buffers
    //
    if( CopyInPrivateBuffers ){

        NTSTATUS    RC;

        ASSERT( 0x1 == OperationObject->Flags.WaitForCopingInPrivateBuffers );

        RC = OcCrProcessOperationObjectPrivateBuffers( OperationObject,
                                                       TRUE );
        //
        // TO DO - error processing
        //
        ASSERT( NT_SUCCESS( RC ) );

        OcCrShadowCopingInPrivateBuffersCompleted( OperationObject );

    }//if( CopyInPrivateBuffers )

    return STATUS_SUCCESS;

}

//-------------------------------------------

NTSTATUS
OcCrShadowProcessOperationBuffersForDevice(
    __in POC_OPERATION_OBJECT    OperationObject,
    __inout POC_SHADOWED_REQUEST_COMPLETION_PARAMETERS    CompletionParam
    )
    /*
    the function is called during the request completion,
    the function prepares data bufers for shadowing if needed
    and synchronizes execution with the shadowing thread,
    the counterpart function used for the request completion is
    OcCrShadowCompleteDeviceRequest
    */
{
    KIRQL      OldIrql;
    BOOLEAN    CanBePostponed;
    BOOLEAN    CopyInPrivateBuffers = FALSE;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( TRUE == CompletionParam->CompleteSynchronously );

    //
    // if the operation should not be shadowed or 
    // the buffers have been already copied then
    // return the successful code, syncronization
    // while accessing OperationObject->Flags.DataInPrivateBuffer
    // is not needed, there can't be contention on this field
    //
    if( 0x0 == OperationObject->Flags.ObjectSentInShadowingModule || 
        0x1 == OperationObject->Flags.DataInPrivateBuffer || 
        0x1 == OperationObject->Flags.ShadowingInProgress )
        return STATUS_SUCCESS;

    //
    // synchronize with the shadow code in OcCrShadowWriterWR
    //

    //
    // do this before acquiring spin lock where the IRQL will be raised
    //
    CanBePostponed = OcCmHkCanCompletionBePostponed( CompletionParam->CompletionContext );

    KeAcquireSpinLock( &OperationObject->SpinLock, &OldIrql );
    {// start of the lock

        //
        // IRQL is DISPATCH_LEVEL!
        //

        //
        // Synchronize the flag checking with the shadow 
        // thread working routine.
        // The ShadowingInProgress flag is also checked - if it is set
        // then the shadowing thread has made its decision about this 
        // operation.
        //
        if( 0x0 == OperationObject->Flags.DataInPrivateBuffer && 
            0x0 == OperationObject->Flags.ShadowingInProgress ){

            //
            // the request has not been shadowed yet, so
            // allow the shadowing subsystem to process the
            // request and complete the request after processing
            //

            if( CanBePostponed ){

                CompletionParam->CompleteSynchronously = FALSE;

                OperationObject->Flags.CompleteInShadowThread = 0x1;

                //
                // save the pointer to completion context
                //
                OperationObject->CompletionContext = CompletionParam->CompletionContext;

                //
                // if the request should be completed
                // in the worker thread then prepare it fot this,
                // do this while the spin lock is helded - this 
                // prevents from a race with a thread in
                // which context the request is being completed
                //
                KeInitializeEvent( &CompletionParam->CompletionEvent, SynchronizationEvent, FALSE );
                OcCmHkInitializeCompletionWaitBlock( &CompletionParam->CompletionWaitBlock, 
                                                     &CompletionParam->CompletionEvent );

                //
                // prepare the completion context for being made pending,
                // the actual completion will take place in the shadowing
                // module's function OcCrShadowWriterWR
                //
                OcChHkPrepareCompletionContextForPostponing( CompletionParam->CompletionContext,
                                                             &CompletionParam->CompletionWaitBlock );

                //
                // must be marked as pending to properly being
                // completed in the context of the thread which
                // is not the same then that issued this request
                //
                ASSERT( OcCmHkGetIrpFromCompletionContext( CompletionParam->CompletionContext )->PendingReturned );

            } else {

                ASSERT( 0x0 == OperationObject->Flags.ShadowingInProgress );

                //
                // completion can't be postponed, copy data in the new buffer
                // because current buffers belongs to the Irp creator which
                // might free or change them before the shadowing module 
                // has been done its work
                //

                CopyInPrivateBuffers = TRUE;
                OperationObject->Flags.WaitForCopingInPrivateBuffers = 0x1;
            }

        }

    }// end of the lock
    KeReleaseSpinLock( &OperationObject->SpinLock, OldIrql );


    if( TRUE == CopyInPrivateBuffers ){

        NTSTATUS    RC;

        ASSERT( 0x1 == OperationObject->Flags.WaitForCopingInPrivateBuffers );

        //
        // completion can't be postponed, copy data in the new buffer
        // because current buffers belongs to the Irp creator which
        // might free or change them before the shadowing module 
        // has done its work
        //

        RC = OcCrProcessOperationObjectPrivateBuffers( OperationObject,
                                                       TRUE );
        //
        // TO DO - process an error
        //
        ASSERT( NT_SUCCESS( RC ) );

        OcCrShadowCopingInPrivateBuffersCompleted( OperationObject );

        //
        // data has been copied in the private buffer, so there is no
        // any reason to change the completion context to asynchronous
        //
        ASSERT( TRUE == CompletionParam->CompleteSynchronously );
    }

    return STATUS_SUCCESS;
}

//-------------------------------------------

NTSTATUS
OcCrShadowCompleteDeviceRequest(
    __in POC_SHADOWED_REQUEST_COMPLETION_PARAMETERS    CompletionParam
    )
    /*
    the function completes the request procesed by OcCrShadowProcessOperationBuffersForDevice
    */
{

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // no any additional processing is needed
    //
    if( CompletionParam->CompleteSynchronously ){

        return OcCmHkContinueCompletion( CompletionParam->CompletionContext );

    } else {

        //
        // wait if the request is synchronous and the Irp has not been
        // marked as pending by any driver in the stack,
        // actually it has been marked as pending in
        // OcChHkPrepareCompletionContextForPostponing
        //
        if( TRUE == CompletionParam->CompletionWaitBlock.WaitForCompletion ){

                ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
                ASSERT( &CompletionParam->CompletionEvent == CompletionParam->CompletionWaitBlock.CompletionEvent );

                KeWaitForSingleObject( &CompletionParam->CompletionEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL );
        }//if( TRUE == CompletionWaitBlock->WaitForCompletion )

        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    ASSERT( !"An unattainable path has been reached. The serious bug! Investigate immediately" );
}

//-------------------------------------------

