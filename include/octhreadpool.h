/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
28.11.2006 
 Start
*/

#include <ocobject.h>
#include <ocwthread.h>

#if !defined(_OC_WORKER_THREADS_POOL_H_)
#define _OC_WORKER_THREADS_POOL_H_

typedef struct _OC_THREAD_POOL_OBJECT{
    ULONG                        NumberOfThreads;
    POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject;
    //
    // array of POC_WORKER_THREAD_OBJECT   array[ NumberOfEntries ];
    //
} OC_THREAD_POOL_OBJECT, *POC_THREAD_POOL_OBJECT;

extern
NTSTATUS
OcTplCreateThreadPool(
    IN ULONG    NumberOfThreads,
    IN BOOLEAN  EachThreadHasPrivateList,
    OUT POC_THREAD_POOL_OBJECT* PtrPtrPoolObject
    );

extern
POC_WORKER_THREAD_OBJECT
OcTplReferenceThreadByIndex(
    IN POC_THREAD_POOL_OBJECT    PtrThreadPoolObject,
    IN ULONG ThreadIndex
    );

extern
POC_WORK_ITEM_LIST_OBJECT
OcTplReferenceSharedWorkItemList(
    IN POC_THREAD_POOL_OBJECT    PtrThreadPoolObject
    );

#if DBG
VOID
OcTplTest(
    IN POC_THREAD_POOL_OBJECT    PtrThreadPoolObject
    );

VOID
OcTestThreadPoolManager();
#endif//DBG

#endif//_OC_WORKER_THREADS_POOL_H_
