/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
24.11.2006 
 Start
*/

#include <ntddk.h>
#include <ocobject.h>

#if !defined(_OC_WORKER_THREADS_H_)
#define _OC_WORKER_THREADS_H_

#define OC_WTH_BUG_CODES_BASE            ( 0xAB000000 )
#define OC_WTH_BUG_UNATTAINABLE_CODE     ( OC_WTH_BUG_CODES_BASE + 0x1 )
#define OC_WTH_BUG_TOO_MANY_PARAMETERS   ( OC_WTH_BUG_CODES_BASE + 0x2 )
#define OC_WTH_INTERNAL_INCONSISTENCY_1  ( OC_WTH_BUG_CODES_BASE + 0x3 )
#define OC_WTH_INTERNAL_INCONSISTENCY_2  ( OC_WTH_BUG_CODES_BASE + 0x4 )

#define OC_WORK_ITEM_TAG  'IWcO'

//
// this the object body for the work item object
//
typedef struct _OC_WORK_ITEM_LIST_OBJECT{

    //
    // the event on which threads sllep waiting for
    // new jobs
    //
    KEVENT       WakeupEvent;

    //
    // the list of the deffered work items
    //
    LIST_ENTRY   ListHead;

    //
    // the lock protecting the list of work items
    //
    KSPIN_LOCK   ListSpinLock;

    //
    // the following fields shows whether this
    // list private or shared by several threads
    //
    BOOLEAN    PrivateList;

} OC_WORK_ITEM_LIST_OBJECT, *POC_WORK_ITEM_LIST_OBJECT;


//
// The _OC_WORKER_THREAD_OBJECT struct defines the body
// of the thread object, the object has standard header
// and may be referenced and derefrenced with OcOb* functions.
// When the thread object's refrence count drops to
// zero the thread will be terminated and all allocated 
// memory will be freed.
//
typedef struct _OC_WORKER_THREAD_OBJECT{

    //
    // current thread pointer
    //
    PETHREAD     Thread;

    //
    // the thread may have its own 
    // work item object, i.e. private list,
    // or share the list with the other threads
    //
    POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject;

    //
    // this event is used to wake up the thread that is described
    // by this object
    //
    KEVENT       WakeupEvent;

    //
    // if TRUE the thread will be terminated on the next wake up
    // after processing of all entries in the list
    //
    BOOLEAN      TerminateThread;
    
    ULONG        InternalId;

} OC_WORKER_THREAD_OBJECT, *POC_WORKER_THREAD_OBJECT;


typedef NTSTATUS (NTAPI *Param0SysProc)();

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

//
// work item can be allocated on a stack,
// so be modest in your desire to add new fields
//
typedef struct _OC_WORKITEM_HEADER{

    LIST_ENTRY   ListEntry;

    //
    // the following event is set in a signal state
    // when the function has been called
    //
    PKEVENT      CallerEvent OPTIONAL;

    //
    // the function to call in a worker thread
    //
    PVOID        FunctionAddress;

    //
    // compact the Flags and number of parameters
    // in 64bit OS they allocate a single 64 bit slot
    //
    ULONG        NumberOfParameters;
    struct{
        ULONG   AllocatedOnStack:0x1;
    }  Flags;

} OC_WORKITEM_HEADER, *POC_WORKITEM_HEADER;

extern
NTSTATUS
OcWthInitializeWorkerThreadsSubsystem(
    IN PVOID Context
    );

extern
VOID
OcWthUninitializeWorkerThreadsSubsystem(
    IN PVOID Context
    );

extern
NTSTATUS
OcWthCreateWorkItemListObject(
    IN BOOLEAN    PrivateList,
    OUT POC_WORK_ITEM_LIST_OBJECT*    PtrPtrWorkItemListObject
    );

extern
NTSTATUS
OcWthCreateWorkerThread(
    IN ULONG    InternalId,
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject OPTIONAL,
    OUT POC_WORKER_THREAD_OBJECT    *PtrPtrWorkerThreadObject
    );

extern
BOOLEAN
OcWthIsWorkerThreadManagerInitialized();

extern
NTSTATUS
OcWthPostWorkItemParam0(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param0SysProc    Function
    );

extern
NTSTATUS
OcWthPostWorkItemParam1(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param1SysProc    Function,
    IN ULONG_PTR    Parameter1
    );

extern
NTSTATUS
OcWthPostWorkItemParam2(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param2SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2
    );

extern
NTSTATUS
OcWthPostWorkItemParam3(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param3SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3
    );

extern
NTSTATUS
OcWthPostWorkItemParam4(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param4SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4
    );

extern
NTSTATUS
OcWthPostWorkItemParam5(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param5SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5
    );

extern
NTSTATUS
OcWthPostWorkItemParam6(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param6SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5,
    IN ULONG_PTR    Parameter6
    );

extern
NTSTATUS
OcWthPostWorkItemParam7(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param7SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5,
    IN ULONG_PTR    Parameter6,
    IN ULONG_PTR    Parameter7
    );

extern
NTSTATUS
OcWthPostWorkItemParam8(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param8SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5,
    IN ULONG_PTR    Parameter6,
    IN ULONG_PTR    Parameter7,
    IN ULONG_PTR    Parameter8
    );

extern
NTSTATUS
OcWthPostWorkItemParam9(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param9SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5,
    IN ULONG_PTR    Parameter6,
    IN ULONG_PTR    Parameter7,
    IN ULONG_PTR    Parameter8,
    IN ULONG_PTR    Parameter9
    );

extern
NTSTATUS
OcWthPostWorkItemParam10(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param10SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5,
    IN ULONG_PTR    Parameter6,
    IN ULONG_PTR    Parameter7,
    IN ULONG_PTR    Parameter8,
    IN ULONG_PTR    Parameter9,
    IN ULONG_PTR    Parameter10
    );

extern OC_OBJECT_TYPE    g_OcWthWorkItemListType;

#endif//_OC_WORKER_THREADS_H_
