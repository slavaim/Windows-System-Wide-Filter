/*
  Author: Slava Imameev
  Copyright (c) 2007  , Slava Imameev
  All Rights Reserved.
*/

#ifndef _OC_QUEUE_LOCK
#define _OC_QUEUE_LOCK

//----------------------------------------------------------

typedef struct _OC_QUEUE_LOCK{
    LIST_ENTRY    ListHead;
    KSPIN_LOCK    ListSpinLock;
} OC_QUEUE_LOCK, *POC_QUEUE_LOCK;

//----------------------------------------------------------

#define OC_WAIT_BLOCK_BARRIER_FLAG  ((ULONG_PTR)0x1)
#define OC_WAIT_BLOCK_BARRIER_FLAG_NATIVE  ((ULONG_PTR)0x2)
#define OC_WAIT_BLOCK_FLAGS_MASK    ((ULONG_PTR)0x3)

#define OC_QL_SIGNATURE 0xAB123409

typedef struct _OC_QUEUE_WAIT_BLOCK{

#if DBG
    //
    // should be OC_QL_SIGNATURE
    //
    ULONG         Signature;
#endif//DBG

    //
    // all requests for locks and all granted locks are 
    // connected in a linked list
    //
    LIST_ENTRY    ListEntry;

    //
    // context used for this lock
    //
    ULONG_PTR     Context;

    //
    // next request for lock with the same context
    //
    union{
    struct _OC_QUEUE_WAIT_BLOCK*    Next;
    ULONG_PTR                       Flags;// two least significant bits are used for flags
    };

    //
    // the event that is set in a signal state when the lock is granted
    //
    KEVENT    Event;

} OC_QUEUE_WAIT_BLOCK, *POC_QUEUE_WAIT_BLOCK;

//----------------------------------------------------------

extern
VOID
OcQlInitializeQueueLock(
    IN POC_QUEUE_LOCK    QueueLock
    );

extern
VOID
OcQlAcquireLockWithContext(
    IN POC_QUEUE_LOCK    QueueLock,
    IN OUT POC_QUEUE_WAIT_BLOCK    WaitBlock,
    IN ULONG_PTR    Context
    );

extern
VOID
OcQlReleaseLockWithContext(
    IN POC_QUEUE_LOCK    QueueLock,
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    );

extern
VOID
OcQlInsertBarrier(
    IN POC_QUEUE_LOCK    QueueLock,
    IN OUT POC_QUEUE_WAIT_BLOCK    BarrierWaitBlock,
    IN ULONG_PTR    Context
    );

extern
VOID
OcQlRemoveBarrier(
    IN POC_QUEUE_LOCK    QueueLock,
    IN POC_QUEUE_WAIT_BLOCK    BarrierWaitBlock
    );

//----------------------------------------------------------

#endif//_OC_QUEUE_LOCK
