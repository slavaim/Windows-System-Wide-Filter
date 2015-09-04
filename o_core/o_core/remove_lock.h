/*
  Author: Slava Imameev
  Copyright (c) 2007  , Slava Imameev
  All Rights Reserved.
*/

#ifndef _OC_REMOVE_LOCK_H_
#define _OC_REMOVE_LOCK_H_

//----------------------------------------------------

typedef enum {
    OcFreeRemoveLock = 0x0,
    OcDebugRemoveLock = 0x1
} OC_REMOVE_LOCK_LOCK_TYPE;

//----------------------------------------------------

typedef struct _OC_REMOVE_LOCK_HEADER {
    OC_REMOVE_LOCK_LOCK_TYPE    Type;
    ULONG                       ReferenceCount;
    KEVENT                      RemoveEvent;
    ULONG                       Removed;
} OC_REMOVE_LOCK_HEADER, *POC_REMOVE_LOCK_HEADER;

//----------------------------------------------------

typedef struct _OC_REMOVE_LOCK_FREE{
    OC_REMOVE_LOCK_HEADER    Common;
} OC_REMOVE_LOCK_FREE, *POC_REMOVE_LOCK_FREE;

//----------------------------------------------------

VOID
NTAPI
OcRlInitializeRemoveLock(
    IN POC_REMOVE_LOCK_HEADER    RemoveLock,
    IN OC_REMOVE_LOCK_LOCK_TYPE    Type
    );

//----------------------------------------------------

NTSTATUS
NTAPI
OcRlAcquireRemoveLock(
    IN POC_REMOVE_LOCK_HEADER LockHeader
    );

//----------------------------------------------------

VOID
NTAPI
OcRlReleaseRemoveLock(
    IN POC_REMOVE_LOCK_HEADER LockHeader
    );

//----------------------------------------------------

VOID
NTAPI
OcRlReleaseRemoveLockAndWait(
    IN POC_REMOVE_LOCK_HEADER LockHeader
    );

//----------------------------------------------------

#endif//_OC_REMOVE_LOCK_H_

