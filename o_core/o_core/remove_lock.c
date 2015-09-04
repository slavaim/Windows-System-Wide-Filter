/*
  Author: Slava Imameev
  Copyright (c) 2007  , Slava Imameev
  All Rights Reserved.
*/

/*
  This file contains the lock which is acquired to retain the 
  driver in memory and prevent premature unloading. For example
  if the lock is acquired in the DriverEntry and for each successfull 
  IRP_MJ_CREATE, reaquired for each deferred routine and released for 
  each IRP_MJ_CLOSE the driver won't be unloaded until the last file 
  object has been closed and the last deferred routine has been completed.
  Actually the kernel's IO Manager retains device objects and the driver 
  object through the DeviceObject->ReferenceCount, but for uniformity 
  the lock is acquired in create and released in close requests.
  This lock is used when it is impossible to synchronize deferred or
  asynchronous routines with the driver unloading by using 
  the object model( OcOb* ). It seems that I use the remove lock
  only for reinsurance.
  Also, because the remove lock is OcRlReleaseRemoveLockAndWait at the 
  begin of the DriverUnload code the remove lock can be used to check 
  whether the driver is unloading.
*/

#include "struct.h"
#include "proto.h"

#define OC_RL_REFERENCE_COUNT_DISTRUST_THRESHOLD    ( ULONG_MAX - 0x100 )

//------------------------------------------------------------------

VOID
NTAPI
OcRlSetRemoveLockInSignalState(
    IN POC_REMOVE_LOCK_HEADER    RemoveLock
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    KeSetEvent( &RemoveLock->RemoveEvent, IO_NO_INCREMENT, FALSE );
}

//-----------------------------------------------------

VOID
NTAPI
OcRlInitializeRemoveLock(
    IN POC_REMOVE_LOCK_HEADER    RemoveLock,
    IN OC_REMOVE_LOCK_LOCK_TYPE    Type
    )
/*++
    This routine is called to initialize the remove lock.
--*/
{
    POC_REMOVE_LOCK_HEADER    Header = RemoveLock;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    RtlZeroMemory( Header, sizeof( *Header ) );
    Header->Type = Type;
    Header->Removed = FALSE;
    Header->ReferenceCount = 0x1;
    KeInitializeEvent( &Header->RemoveEvent, NotificationEvent, FALSE );
}

//------------------------------------------------------------------

NTSTATUS
NTAPI
OcRlAcquireRemoveLock(
    IN POC_REMOVE_LOCK_HEADER LockHeader
    )

/*++

Routine Description:

    This routine is called to acquire the remove lock.

Return Value:

    Returns whether or not the remove lock was obtained.
    If the lock was not obtained the caller mus not call IoReleaseRemoveLock.

--*/

{
    NTSTATUS    RC;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( LockHeader->ReferenceCount < OC_RL_REFERENCE_COUNT_DISTRUST_THRESHOLD );

    //
    // Grab the remove lock
    //

    InterlockedIncrement( &LockHeader->ReferenceCount );

    if( FALSE == InterlockedCompareExchange( &LockHeader->Removed, FALSE, FALSE ) ){

        RC = STATUS_SUCCESS;

    } else {

        //
        // someone wants the resource protected by the lock to be removed,
        // to avoid starvation of the waiting thread release the remove
        // lock and return the error
        //
        if( 0x0 == (InterlockedDecrement( &LockHeader->ReferenceCount ) ) )
            OcRlSetRemoveLockInSignalState( LockHeader );

        RC = STATUS_DELETE_PENDING;
    }

    return RC;
}

//---------------------------------------------------------------------

VOID
NTAPI
OcRlReleaseRemoveLock(
    IN POC_REMOVE_LOCK_HEADER LockHeader
    )
/*++
    This routine is called to release the remove lock.
--*/

{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( LockHeader->ReferenceCount < OC_RL_REFERENCE_COUNT_DISTRUST_THRESHOLD );

    if( 0x0 == InterlockedDecrement( &LockHeader->ReferenceCount ) ){

        //
        // signal that it is safe to remove protected resource
        //
        OcRlSetRemoveLockInSignalState( LockHeader );;

    }

    return;
}

//---------------------------------------------------------------------

VOID
NTAPI
OcRlReleaseRemoveLockAndWait(
    IN POC_REMOVE_LOCK_HEADER LockHeader
    )
/*++
    This routine is called when the client would like to delete the remove-
    locked resource.
    This routine will block until all the remove locks have completed.
    The caller must acquire remove lock before calling this routine and must 
    not call OcRlReleaseRemoveLock!
    Multiple callers may call this routine, i.e. it is thread safe.
--*/
{

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( LockHeader->ReferenceCount < OC_RL_REFERENCE_COUNT_DISTRUST_THRESHOLD );

    //
    // mark as removed
    //
    if( FALSE == InterlockedCompareExchange( &LockHeader->Removed, TRUE, FALSE ) ){

        ASSERT( LockHeader->ReferenceCount >= 0x2 );

        //
        // decrement the counter incremented when the lock was initialized,
        // this is done only once, InterlockedCompareExchange protects from the
        // concurrent entering in this block
        //
        InterlockedDecrement( &LockHeader->ReferenceCount );
    }

    if( (InterlockedDecrement( &LockHeader->ReferenceCount )) > 0x0 ){

        ASSERT( LockHeader->ReferenceCount < OC_RL_REFERENCE_COUNT_DISTRUST_THRESHOLD );

        KeWaitForSingleObject( &LockHeader->RemoveEvent,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL );
    }

}

