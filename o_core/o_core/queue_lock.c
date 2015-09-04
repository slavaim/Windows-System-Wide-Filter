/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
19.03.2007 ( March )
 Start
*/

/*
This file contains the code for the queue lock.
In this file I assume that at least 8 bit types 
are atomic, i.e. accesses to 8 bits byte memory 
cell by a pointer aligned address are serialized.
This assumption is true on all of the machines 
that the GNU C library supports and on all POSIX 
systems I know of.
*/

#include "struct.h"
#include "proto.h"

//----------------------------------------------------------

__forceinline
POC_QUEUE_WAIT_BLOCK
OcQlGetNextWaitBlock(
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    )
    /*
    Internal function!
    */
{
    //
    // the unused least significant bits of the pointer
    // are used to store the flags
    //
    return (POC_QUEUE_WAIT_BLOCK)( ((ULONG_PTR)WaitBlock->Next) & ~( OC_WAIT_BLOCK_FLAGS_MASK ) );
}

//----------------------------------------------------------

__forceinline
ULONG_PTR
OcQlGetWaitBlockFlags(
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    )
    /*
    Internal function!
    */
{
    //
    // the unused least significant bits of the pointer
    // are used to store the flags
    //
    return (ULONG_PTR)( ((ULONG_PTR)WaitBlock->Next) & OC_WAIT_BLOCK_FLAGS_MASK );
}

//----------------------------------------------------------

__forceinline
VOID
OcQlSetWaitBlockFlags(
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock,
    IN ULONG_PTR    WaitBlockFlags
    )
    /*
    Internal function!
    */
{
    ASSERT( WaitBlockFlags <= OC_WAIT_BLOCK_FLAGS_MASK );

    //
    // the unused least significant bits of the pointer
    // are used to store the flags
    //
    ((ULONG_PTR)WaitBlock->Next) = ((ULONG_PTR)WaitBlock->Next) | WaitBlockFlags ;
}

//----------------------------------------------------------

__forceinline
VOID
OcQlSetNextWaitBlockAndFlags(
    IN POC_QUEUE_WAIT_BLOCK    PredecessorWaitBlock,
    IN POC_QUEUE_WAIT_BLOCK    NextWaitBlock,
    IN ULONG_PTR    PredecessorBlockFlags
    )
    /*
    Internal function!
    */
{
    ASSERT( PredecessorBlockFlags <= OC_WAIT_BLOCK_FLAGS_MASK );
    ASSERT( 0x0 == ( ((ULONG_PTR)NextWaitBlock) & OC_WAIT_BLOCK_FLAGS_MASK ) );

    //
    // the unused least significant bits of the pointer
    // are used to store the flags
    //
    PredecessorWaitBlock->Next = (POC_QUEUE_WAIT_BLOCK)( ((ULONG_PTR)NextWaitBlock) | PredecessorBlockFlags );
}

//----------------------------------------------------------

__forceinline
VOID
OcQlInitializeWaitBlock(
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    )
    /*
    Internal routine
    */
{
    WaitBlock->Next = NULL;
    KeInitializeEvent( &WaitBlock->Event, SynchronizationEvent, FALSE );
}

//----------------------------------------------------------

__forceinline
BOOLEAN
OcInsertWaitBlockInList(
    IN POC_QUEUE_LOCK    QueueLock,
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock,
    IN ULONG_PTR    PrevBlockFlags
    )
    /*
    returns TRUE if there is no other entries with the same context
    in the list, else returns FALSE.
    Internal routine
    */
{
    KIRQL      OldIrql;
    BOOLEAN    FirstContextEntry = TRUE;

    ASSERT( PrevBlockFlags <= OC_WAIT_BLOCK_FLAGS_MASK );
    ASSERT( NULL == OcQlGetNextWaitBlock( WaitBlock ) );

    KeAcquireSpinLock( &QueueLock->ListSpinLock, &OldIrql );
    {// start of the lock

        PLIST_ENTRY    request;

        //
        // start the search from the list end, because I need the last inserted lock
        //
        for( request = QueueLock->ListHead.Blink; request != &QueueLock->ListHead; request = request->Blink ){

            POC_QUEUE_WAIT_BLOCK    ListWaitBlock;

            ListWaitBlock = CONTAINING_RECORD( request, OC_QUEUE_WAIT_BLOCK, ListEntry );
            if( ListWaitBlock->Context == WaitBlock->Context ){

                ASSERT( NULL == OcQlGetNextWaitBlock( ListWaitBlock ) );
                ASSERT( OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE == OcQlGetWaitBlockFlags( WaitBlock ) || 
                        0x0 == OcQlGetWaitBlockFlags( WaitBlock ) );

                //
                // connect all waiting blocks with the same context
                // and move Flags to the predecessor
                //
                OcQlSetNextWaitBlockAndFlags( ListWaitBlock,
                                              WaitBlock,
                                              OcQlGetWaitBlockFlags( ListWaitBlock ) | PrevBlockFlags );

                ASSERT( PrevBlockFlags? OcIsFlagOn( OcQlGetWaitBlockFlags( ListWaitBlock ), PrevBlockFlags ): TRUE );

                FirstContextEntry = FALSE;
                break;
            }//if
        }//for

        //
        // insert the wait block in the list
        //
        InsertTailList( &QueueLock->ListHead, &WaitBlock->ListEntry );

    }// end of the lock
    KeReleaseSpinLock( &QueueLock->ListSpinLock, OldIrql );

    return FirstContextEntry;
}

//----------------------------------------------------------

VOID
OcQlInitializeQueueLock(
    IN POC_QUEUE_LOCK    QueueLock
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    InitializeListHead( &QueueLock->ListHead );
    KeInitializeSpinLock( &QueueLock->ListSpinLock );
}

//----------------------------------------------------------

VOID
OcQlAcquireLockWithContext(
    IN POC_QUEUE_LOCK    QueueLock,
    IN OUT POC_QUEUE_WAIT_BLOCK    WaitBlock,
    IN ULONG_PTR    Context
    )
{
    BOOLEAN     LockGranted;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( 0x0 != Context );
    ASSERT( 0x0 == ( (ULONG_PTR)WaitBlock & OC_WAIT_BLOCK_FLAGS_MASK ) );
    ASSERT( KeAreApcsDisabled() );

    //
    // initialize the wait block
    //
    OcQlInitializeWaitBlock( WaitBlock );
    WaitBlock->Context = Context;
#if DBG
    WaitBlock->Signature = OC_QL_SIGNATURE;
#endif//DBG

    //
    // the lock can't be granted until there are older wait blocks
    // with the same context in the list
    //
    LockGranted = OcInsertWaitBlockInList( QueueLock,
                                           WaitBlock,
                                           0x0 );

    //
    // wait for the lock to be granted
    //
    if( FALSE == LockGranted ){

        KeWaitForSingleObject( &WaitBlock->Event,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL );

    }
}

//----------------------------------------------------------

VOID
OcQlReleaseLockWithContext(
    IN POC_QUEUE_LOCK    QueueLock,
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    )
{
#if DBG
    ULONG_PTR     Context = WaitBlock->Context;
#endif//DBG
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( KeAreApcsDisabled() );
    ASSERT( !OcIsFlagOn( OcQlGetWaitBlockFlags( WaitBlock ), OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE ) );
    ASSERT( OC_QL_SIGNATURE == WaitBlock->Signature );

    //
    // for this check of flag works correctly I only need the atomicity
    // in access to a pointer aligned memory byte cell
    //
    if( TRUE == OcIsFlagOn( OcQlGetWaitBlockFlags( WaitBlock ), OC_WAIT_BLOCK_BARRIER_FLAG ) ){

        KIRQL    OldIrql;

        //
        // the next block is a barrier which might be( or has been ) removed 
        // asynchronously, so before setting an event acquire the 
        // lock to synchronize with OcQlRemoveBarrier
        //

        //
        // remove the wait block from the list
        //
        KeAcquireSpinLock( &QueueLock->ListSpinLock, &OldIrql );
        {// start of the lock

            POC_QUEUE_WAIT_BLOCK    NextWaitBlock;

            //
            // once set the barrier flag is saved forever
            //
            ASSERT( OcIsFlagOn( WaitBlock->Flags, OC_WAIT_BLOCK_BARRIER_FLAG ) );

            //
            // get the next waiting block
            //
            NextWaitBlock = OcQlGetNextWaitBlock( WaitBlock );

            //
            // wake up the next waiting thread
            //
            if( NULL != NextWaitBlock ){

                ASSERT( Context == NextWaitBlock->Context );
                ASSERT( OC_QL_SIGNATURE == NextWaitBlock->Signature );

                KeSetEvent( &NextWaitBlock->Event, IO_DISK_INCREMENT, FALSE );
            }


            ASSERT( !IsListEmpty( &WaitBlock->ListEntry ) );
            RemoveEntryList( &WaitBlock->ListEntry );

        }// end of the lock
        KeReleaseSpinLock( &QueueLock->ListSpinLock, OldIrql );

    } else {

        KIRQL    OldIrql;
        POC_QUEUE_WAIT_BLOCK    NextWaitBlock;

        //
        // remove the wait block from the list
        //
        KeAcquireSpinLock( &QueueLock->ListSpinLock, &OldIrql );
        {// start of the lock
            //
            // get the next waiting block, it can't change because only
            // barrier's wait block can be removed asynchronously
            //
            NextWaitBlock = OcQlGetNextWaitBlock( WaitBlock );

            ASSERT( !( NextWaitBlock && OC_QL_SIGNATURE != NextWaitBlock->Signature ) );
            ASSERT( !OcIsFlagOn( OcQlGetWaitBlockFlags( WaitBlock ), OC_WAIT_BLOCK_BARRIER_FLAG ) );
            ASSERT( !IsListEmpty( &WaitBlock->ListEntry ) );

            RemoveEntryList( &WaitBlock->ListEntry );

        }// end of the lock
        KeReleaseSpinLock( &QueueLock->ListSpinLock, OldIrql );

        //
        // wake up the next waiting thread after releasing the lock,
        // the block couldn't go out because only barrier blocks
        // are removed asynchronously
        //
        if( NULL != NextWaitBlock ){

            ASSERT( OC_QL_SIGNATURE == NextWaitBlock->Signature );
            ASSERT( Context == NextWaitBlock->Context );

            KeSetEvent( &NextWaitBlock->Event, IO_DISK_INCREMENT, FALSE );
        }

    }

}

//----------------------------------------------------------

VOID
OcQlInsertBarrier(
    IN POC_QUEUE_LOCK    QueueLock,
    IN OUT POC_QUEUE_WAIT_BLOCK    BarrierWaitBlock,
    IN ULONG_PTR    Context
    )
    /*
    the barier is used for preventing acquiring the resource,
    but doesn't enter in a wait loop if the resource has
    been already acquired, i.e. noboby can cross the barrier
    until it has been removed
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( 0x0 == ( (ULONG_PTR)BarrierWaitBlock & OC_WAIT_BLOCK_FLAGS_MASK ) );

    //
    // initialize the wait block
    //
    OcQlInitializeWaitBlock( BarrierWaitBlock );
    BarrierWaitBlock->Context = Context;
#if DBG
    BarrierWaitBlock->Signature = OC_QL_SIGNATURE;
#endif//DBG

    OcQlSetWaitBlockFlags( BarrierWaitBlock, OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE );

    ASSERT( OcIsFlagOn( OcQlGetWaitBlockFlags( BarrierWaitBlock ), OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE ) );

    OcInsertWaitBlockInList( QueueLock,
                             BarrierWaitBlock,
                             OC_WAIT_BLOCK_BARRIER_FLAG );

    ASSERT( OcIsFlagOn( OcQlGetWaitBlockFlags( BarrierWaitBlock ), OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE ) );
}

//----------------------------------------------------------

VOID
OcQlRemoveBarrier(
    IN POC_QUEUE_LOCK    QueueLock,
    IN POC_QUEUE_WAIT_BLOCK    BarrierWaitBlock
    )
{

    KIRQL      OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OcIsFlagOn( OcQlGetWaitBlockFlags( BarrierWaitBlock ), OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE ) );
    ASSERT( OC_QL_SIGNATURE == BarrierWaitBlock->Signature );

    KeAcquireSpinLock( &QueueLock->ListSpinLock, &OldIrql );
    {// start of the lock

        BOOLEAN    PrevWasFound = FALSE;
        PLIST_ENTRY    request;

        //
        // start the search from the barrier list in a backward direction, 
        // because I need the block inserted before the barrier
        //
        for( request = BarrierWaitBlock->ListEntry.Blink; request != &QueueLock->ListHead; request = request->Blink ){

            POC_QUEUE_WAIT_BLOCK    PrevWaitBlock;

            PrevWaitBlock = CONTAINING_RECORD( request, OC_QUEUE_WAIT_BLOCK, ListEntry );

            ASSERT( OC_QL_SIGNATURE == PrevWaitBlock->Signature );

            //
            // get the next waiting block for the PrevWaitBlock and
            // compare the pointers, because the Context field might 
            // be set to 0x0 in OcQlReleaseLockWithContext
            //
            if( OcQlGetNextWaitBlock( PrevWaitBlock ) == BarrierWaitBlock ){

                ASSERT( BarrierWaitBlock->Context == PrevWaitBlock->Context );
                ASSERT( OcIsFlagOn( OcQlGetWaitBlockFlags( PrevWaitBlock ), OC_WAIT_BLOCK_BARRIER_FLAG ) );


                //
                // connect the block before the barrier and after it, save the 
                // barrier flag to synchronize with OcQlReleaseLockWithContext,
                // OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE is a flag that describes the
                // block in which it is set, so there is no need to copy it in 
                // the previous block
                //
                OcQlSetNextWaitBlockAndFlags( PrevWaitBlock,
                                              OcQlGetNextWaitBlock( BarrierWaitBlock ),
                                              OcQlGetWaitBlockFlags( PrevWaitBlock ) | 
                                              ( OcQlGetWaitBlockFlags( BarrierWaitBlock ) & ~(OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE) ) );

                PrevWasFound = TRUE;
                break;

            }//if
        }//for

        //
        // if the previous wait block has not been found
        // then it was removed( or has never existed ) 
        // and the event for the next wait block must be 
        // set in a signal state
        //
        if( FALSE == PrevWasFound && 
            NULL != OcQlGetNextWaitBlock( BarrierWaitBlock ) ){

                KeSetEvent( &OcQlGetNextWaitBlock( BarrierWaitBlock )->Event,
                            IO_DISK_INCREMENT,
                            FALSE );
        }

        //
        // remove the barrier's wait block from the list
        //
        RemoveEntryList( &BarrierWaitBlock->ListEntry );

    }// end of the lock
    KeReleaseSpinLock( &QueueLock->ListSpinLock, OldIrql );
}

//----------------------------------------------------------

