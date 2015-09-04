/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
24.11.2006 
 Start
*/

#include <objects.h>

#if !defined(_OC_WORKER_THREADS_H_)
#define _OC_WORKER_THREADS_H_

#define OC_THREAD_BUG_CODES_BASE           ( 0xAB0000000 )
#define OC_THREAD_BUG_UNATTAINABLE_CODE    ( OC_THREAD_BUG_CODES_BASE + 0x1 )
#define OC_THREAD_BUG_TOO_MANY_PARAMETERS  ( OC_THREAD_BUG_CODES_BASE + 0x2 )

#define OC_WORK_ITEM_TAG  'IWcO'

typedef struct _OC_WORKER_THREAD_OBJECT{

    //
    // current thread pointer
    //
    PETHREAD     Thread;

    //
    // the event on which the thread slleps waiting for
    // new jobs
    //
    KEVENT       WakeupEvent;

    //
    // the list of the deffered jobs for this thread
    //
    LIST_ENTRY   ListHead;

    //
    // the lock protecting the list of jobs
    //
    KSPIN_LOCK   LisSpinLock;

    //
    // if TRUE the thread will be terminated on the next wake up
    // after processing of all entries in the list
    //
    BOOLEAN      TerminateThread;
    
    ULONG        InternalId;
} OC_WORKER_THREAD_OBJECT, *POC_WORKER_THREAD_OBJECT;


typedef struct _OC_THREADS_CONTAINER_OBJECT{
    ULONG                       NumberOfEntries;
    POC_WORKER_THREAD_OBJECT    WorkerThreadObject[ 0x1 ];
} OC_THREADS_CONTAINER_OBJECT, *POC_THREADS_CONTAINER_OBJECT;


typedef NTSTATUS (NTAPI *Param1SysProc)( ULONG_PTR param1 );

typedef NTSTATUS (NTAPI *Param2SysProc)( 
                                   ULONG_PTR param1, 
                                   ULONG_PTR param2 );

typedef NTSTATUS (NTAPI *Param3SysProc)( ULONG_PTR param1, 
                                   ULONG_PTR param2, 
                                   ULONG_PTR param3 );

typedef NTSTATUS (NTAPI *Param4SysProc)( ULONG_PTR param1, 
                                   ULONG_PTR param2, 
                                   ULONG_PTR param3, 
                                   ULONG_PTR param4 );

typedef NTSTATUS (NTAPI *Param5SysProc)( ULONG_PTR param1, 
                                   ULONG_PTR param2, 
                                   ULONG_PTR param3, 
                                   ULONG_PTR param4,
                                   ULONG_PTR param5 );

typedef NTSTATUS (NTAPI *Param6SysProc)( ULONG_PTR param1, 
                                   ULONG_PTR param2, 
                                   ULONG_PTR param3, 
                                   ULONG_PTR param4,
                                   ULONG_PTR param5,
                                   ULONG_PTR param6 );


typedef NTSTATUS (NTAPI *Param7SysProc)( ULONG_PTR param1, 
                                   ULONG_PTR param2, 
                                   ULONG_PTR param3, 
                                   ULONG_PTR param4,
                                   ULONG_PTR param5,
                                   ULONG_PTR param6,
                                   ULONG_PTR param7 );


typedef NTSTATUS (NTAPI *Param8SysProc)( ULONG_PTR param1, 
                                   ULONG_PTR param2, 
                                   ULONG_PTR param3, 
                                   ULONG_PTR param4,
                                   ULONG_PTR param5,
                                   ULONG_PTR param6,
                                   ULONG_PTR param7,
                                   ULONG_PTR param8 );


typedef NTSTATUS (NTAPI *Param9SysProc)( ULONG_PTR param1, 
                                   ULONG_PTR param2, 
                                   ULONG_PTR param3, 
                                   ULONG_PTR param4,
                                   ULONG_PTR param5,
                                   ULONG_PTR param6,
                                   ULONG_PTR param7,
                                   ULONG_PTR param8,
                                   ULONG_PTR param9 );

typedef NTSTATUS (NTAPI *Param10SysProc)( ULONG_PTR param1, 
                                   ULONG_PTR param2, 
                                   ULONG_PTR param3, 
                                   ULONG_PTR param4,
                                   ULONG_PTR param5,
                                   ULONG_PTR param6,
                                   ULONG_PTR param7,
                                   ULONG_PTR param8,
                                   ULONG_PTR param9,
                                   ULONG_PTR param10 );


typedef struct _OC_WORKITEM_HEADER{
    LIST_ENTRY   ListEntry;
    ULONG        ParamCount;
    PVOID        FunctionAddress;
} OC_WORKITEM_HEADER, POC_WORKITEM_HEADER;

#endif//_OC_WORKER_THREADS_H_
