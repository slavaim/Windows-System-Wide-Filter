/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
04.12.2006 ( December )
 Start
*/

#if !defined(_OC_RWLOCK_H_)
#define _OC_RWLOCK_H_

#include <ntddk.h>

//-----------------------------------------------------------

typedef struct _OC_RW_SPIN_LOCK{
    //
    // RwLock is equal to 0x01000000 for no owner, 
    // 0x00ffffff for one reader, 0x00fffffe for two readers,
    // and 0x00000000 for one writer
    //
    LONG    RwLock;
} OC_RW_SPIN_LOCK, *POC_RW_SPIN_LOCK;

//-----------------------------------------------------------

__forceinline
VOID
OcRwInitializeRwLock(
    IN POC_RW_SPIN_LOCK Lock
    )
{
    Lock->RwLock = 0x01000000;
}

//-----------------------------------------------------------

__forceinline
ULONG
OcRwRawTryLockForRead(
    IN POC_RW_SPIN_LOCK Lock
    )
    /*
    returns 0x1 if lock has been acquired for read
    returns 0x0 if lock has not been acquired for read
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // step 1 - try to acquire for read
    //
    if( InterlockedDecrement( &Lock->RwLock ) >= 0x0 )
        return 1;
    //
    // the lock has been acquired for write
    // undo the step 1
    //
    InterlockedIncrement( &Lock->RwLock );
    return 0;
}

//-----------------------------------------------------------

__forceinline
ULONG
OcRwRawTryLockForWrite(
    IN POC_RW_SPIN_LOCK Lock
    )
    /*
    returns 0x1 if lock has been acquired for write
    returns 0x0 if lock has not been acquired for write
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // step 1 - try to acquire for write
    //
    if( 0x01000000 == InterlockedCompareExchange( &Lock->RwLock, 
                                                  0x00000000, 
                                                  0x01000000 ) )
        return 1;
    //
    // the lock has been acquired for read
    //
    return 0;
}

//-----------------------------------------------------------

__forceinline
VOID
OcRwAcquireLockForRead(
    IN POC_RW_SPIN_LOCK Lock,
    OUT KIRQL* PtrOldIrql
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // disable the kernel preemptition
    //
    KeRaiseIrql( DISPATCH_LEVEL, PtrOldIrql );

    //
    // spin until lock is acquired
    //
    while( 0x0 == OcRwRawTryLockForRead( Lock ) ){
        //
        // to avoid starvaition when 
        // two threads compete for
        // lock I enable the kernel
        // preemption and reschedule 
        // if possible
        //
        KeLowerIrql( *PtrOldIrql );
        if( KeGetCurrentIrql() <= APC_LEVEL ){

            LARGE_INTEGER    Timeout;

            //
            // timeout to 10e-6 sec, i.e. (10e-7)*(10)
            Timeout.QuadPart = -(10i64);

            KeDelayExecutionThread( KernelMode,
                                    FALSE,
                                    &Timeout );
        }
        KeRaiseIrql( DISPATCH_LEVEL, PtrOldIrql );
    }//while
}

//-----------------------------------------------------------

__forceinline
VOID
OcRwReleaseReadLock(
    IN POC_RW_SPIN_LOCK Lock,
    IN KIRQL OldIrql
    )
{
    ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

    InterlockedIncrement( &Lock->RwLock );
    KeLowerIrql( OldIrql );
}

//-----------------------------------------------------------

__forceinline
VOID
OcRwAcquireLockForWrite(
    IN POC_RW_SPIN_LOCK Lock,
    OUT KIRQL* PtrOldIrql
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // disable the kernel preemptition
    //
    KeRaiseIrql( DISPATCH_LEVEL, PtrOldIrql );
    //
    // spin until lock is acquired
    //
    while( 0x0 == OcRwRawTryLockForWrite( Lock ) );
}

//-----------------------------------------------------------

__forceinline
VOID
OcRwReleaseWriteLock(
    IN POC_RW_SPIN_LOCK Lock,
    IN KIRQL OldIrql
    )
{
    ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

    /*
    //
    // Try to change in the loop, if I will be out of luck
    // the threads might starve forever. To reduce
    // possibility of such scenario I reschedule waiting 
    // thread in OcRwAcquireLockForRead, if possible.
    //
    while( 0x00000000 != InterlockedCompareExchange( &Lock->RwLock, 
                                                     0x01000000, 
                                                     0x00000000 ) );
                                                     */

    //
    // Simply add 0x01000000 to the current value,
    // others who content for the lock will notice that
    // the lock is free after they restore values after
    // unsuccessful attempts to acquire the lock
    //
    InterlockedExchangeAdd( &Lock->RwLock, 0x01000000 );

    KeLowerIrql( OldIrql );
}

//-----------------------------------------------------------

#endif//_OC_RWLOCK_H_
