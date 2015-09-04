/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
24.11.2006 
 Start
*/

#include <ocwthread.h>

#define OC_WORKITEM_PARAMETERS_MAX_NUMBER  10

#define DECLARE_WORKITEM( NumberOfParameters )              \
    typedef struct OC_WORKITEM_##NumberOfParameters{        \
    OC_WORKITEM_HEADER    Header;                           \
    ULONG_PTR             Parameter[ NumberOfParameters ];  \
} OC_WORKITEM_##NumberOfParameters, *POC_WORKITEM_##NumberOfParameters;


DECLARE_WORKITEM( 1 )
DECLARE_WORKITEM( 2 )
DECLARE_WORKITEM( 3 )
DECLARE_WORKITEM( 4 )
DECLARE_WORKITEM( 5 )
DECLARE_WORKITEM( 6 )
DECLARE_WORKITEM( 7 )
DECLARE_WORKITEM( 8 )
DECLARE_WORKITEM( 9 )
DECLARE_WORKITEM( 10 )

#define SIZE_OF_WORKITEM_STRUCT( NumberOfParameters ) sizeof( OC_WORKITEM_##NumberOfParameters )

static ULONG    g_SizeOfWorkItemStruct[ OC_WORKITEM_PARAMETERS_MAX_NUMBER ] = {
SIZE_OF_WORKITEM_STRUCT( 1 ),
SIZE_OF_WORKITEM_STRUCT( 2 ),
SIZE_OF_WORKITEM_STRUCT( 3 ),
SIZE_OF_WORKITEM_STRUCT( 4 ),
SIZE_OF_WORKITEM_STRUCT( 5 ),
SIZE_OF_WORKITEM_STRUCT( 6 ),
SIZE_OF_WORKITEM_STRUCT( 7 ),
SIZE_OF_WORKITEM_STRUCT( 8 ),
SIZE_OF_WORKITEM_STRUCT( 9 ),
SIZE_OF_WORKITEM_STRUCT( 10 )
};


//OC_OBJECT_TYPE    OcThreadContainerObjectType;
static OC_OBJECT_TYPE           g_OcWorkerThreadObjectType;
static KEVENT                   g_WorkerThreadObjectTypeUninitializationEvent;
static NPAGED_LOOKASIDE_LIST    g_NPagedListForWorkItems[ OC_WORKITEM_PARAMETERS_MAX_NUMBER ];
static BOOLEAN                  g_OcWorkerThreadsSubsystemInitialized = TRUE;

//-------------------------------------------------------------

static
VOID 
OcStopWorkerThreadWait(
    IN POC_OBJECT_BODY    ObjectBody
    );

VOID
OcProcessAllWorkItems(
    POC_WORKER_THREAD_OBJECT    PtrWorkerTreadObject
    );

VOID 
OcAllWorkerThreadsStopped(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    );

NTSTATUS 
OcCallCallWorkItemFunction( 
    IN PVOID       Function,
    IN ULONG       ParamsCount, 
    IN PULONG_PTR  ParametersArray 
    );

VOID
OcFreeWorkItem(
    IN POC_WORKITEM_HEADER    PtrWorkItemHeader
    );

//-------------------------------------------------------------

NTSTATUS
OcInitializeWorkerThreadsSubsystem()
{
    int    i;

    OC_OBJECT_TYPE_INITIALIZER( TypeInitializer );

    //
    // initialize worker thread object type 
    //

    TypeInitializer.Tag = 'TWcO';
    TypeInitializer.ObjectsBodySize = sizeof( OC_WORKER_THREAD_OBJECT );
    TypeInitializer.Methods.DeleteObject = OcStopWorkerThreadWait;
    TypeInitializer.Methods.DeleteObjectType = OcAllWorkerThreadsStopped;

    OcObInitializeObjectType( &g_OcWorkerThreadObjectType,
                              &TypeInitializer );

    //
    // initialize the event that is set in signal state when
    // all thread objects and object type will be deleted
    //
    KeInitializeEvent( &g_WorkerThreadObjectTypeUninitializationEvent,
                       NotificationEvent,
                       FALSE );

    //
    // initialize the work items allocator
    //
    for( i = 0x0; i < OC_WORKITEM_PARAMETERS_MAX_NUMBER; ++i ){

        ExInitializeNPagedLookasideList( &g_NPagedListForWorkItems[ i ],
                                         NULL,
                                         NULL,
                                         0x0,
                                         g_SizeOfWorkItemStruct[ i ],
                                         OC_WORK_ITEM_TAG,
                                         0x0 );
    }//for 

    g_OcWorkerThreadsSubsystemInitialized = TRUE;

    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

VOID
OcUninitializeWorkerThreadsSubsystem()
{
    int    i;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    if( FALSE == g_OcWorkerThreadsSubsystemInitialized )
        return;

    OcObDeleteObjectType( &g_OcWorkerThreadObjectType );

    //
    // wait for stopping of all threads
    //
    KeWaitForSingleObject( &g_WorkerThreadObjectTypeUninitializationEvent,
                           Executive,
                           KernelMode,
                           FALSE,
                           NULL );

    //
    // uninitialize the work items allocator
    //
    for( i = 0x0; i < OC_WORKITEM_PARAMETERS_MAX_NUMBER; ++i ){

        ExDeleteNPagedLookasideList( &g_NPagedListForWorkItems[ i ] );
    }//for 
}

//-------------------------------------------------------------

VOID 
OcAllWorkerThreadsStopped(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    KeSetEvent( &g_WorkerThreadObjectTypeUninitializationEvent,
                IO_DISK_INCREMENT,
                FALSE );
}

//-------------------------------------------------------------

static
VOID
OcStopWorkerThreadWait(
    IN POC_OBJECT_BODY    ObjectBody
    )
    /*
    Internal routine!
    */
{
    POC_WORKER_THREAD_OBJECT    PtrWorkerTreadObject = ( POC_WORKER_THREAD_OBJECT )ObjectBody;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // mark the thread as terminated
    //
    PtrWorkerTreadObject->TerminateThread = TRUE;

    //
    // wake up the thread
    //
    KeSetEvent( &PtrWorkerTreadObject->WakeupEvent, IO_DISK_INCREMENT, FALSE );

    if( PtrWorkerTreadObject->Thread ){

        //
        // wait for the thread termination
        //
        KeWaitForSingleObject(  PtrWorkerTreadObject->Thread,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL );

       //
       // free the thread system object
       //
       ObDereferenceObject( PtrWorkerTreadObject->Thread );
    }
}

//-------------------------------------------------------------

static
VOID
OcWorkerThreadRoutine(
    IN PVOID    Context
    )
    /*
    Internal routine!
    */
{
    POC_WORKER_THREAD_OBJECT    PtrWorkerTreadObject = ( POC_WORKER_THREAD_OBJECT )Context;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    while ( TRUE ){

        //
        // wait for the wake up event
        //
        KeWaitForSingleObject( &PtrWorkerTreadObject->WakeupEvent,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL );

        //
        // check that the structure has been properly initialized,
        // the Thread fiels can be checked only after KeWaitForSingleObject
        // because the thread is started with the Thread field set to NULL
        //
        ASSERT( PsGetCurrentThread() == PtrWorkerTreadObject->Thread );
        
        //
        // process the list of deferred jobs
        //
        OcProcessAllWorkItems( PtrWorkerTreadObject );

        if( PtrWorkerTreadObject->TerminateThread )
            PsTerminateSystemThread( STATUS_SUCCESS );
    }

#if DBG
    //
    // unattainable code
    //
    KeBugCheckEx( OC_THREAD_BUG_UNATTAINABLE_CODE,
                  (ULONG_PTR)__LINE__,
                  (ULONG_PTR)Context,
                  (ULONG_PTR)PsGetCurrentThread(),
                  (ULONG_PTR)PsGetCurrentProcess() );
#endif//DBG
}

//-------------------------------------------------------------

VOID
OcProcessAllWorkItems(
    IN POC_WORKER_THREAD_OBJECT    PtrWorkerTreadObject
    )
{
    POC_WORKITEM_HEADER    PtrWorkItemHeader;
    PLIST_ENTRY            request;

    while ( NULL != ( request = ExInterlockedRemoveHeadList( &PtrWorkerTreadObject->ListHead, 
                                                             &PtrWorkerTreadObject->LisSpinLock ) ) ){

        PtrWorkItemHeader = CONTAINING_RECORD( request, OC_WORKITEM_HEADER, ListEntry );

        OcCallCallWorkItemFunction( PtrWorkItemHeader->FunctionAddress,
                                    PtrWorkItemHeader->ParamCount,
                                    ( PULONG_PTR )( PtrWorkItemHeader+0x1 ) );

        OcFreeWorkItem( PtrWorkItemHeader );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrWorkItemHeader )

    }//while
}

//-------------------------------------------------------------

VOID
OcFreeWorkItem(
    IN POC_WORKITEM_HEADER    PtrWorkItemHeader
    )
{
#ifndef USE_STD_ALLOCATOR
    ExFreeToNPagedLookasideList( g_NPagedListForWorkItems[ PtrWorkItemHeader->ParamCount ], (PVOID)PtrWorkItemHeader );
#else//USE_STD_ALLOCATOR
    ExFreePoolWithTag( (PVOID)PtrWorkItemHeader, OC_WORK_ITEM_TAG );
#endif//USE_STD_ALLOCATOR
}


//-------------------------------------------------------------

NTSTATUS 
OcCallCallWorkItemFunction( 
    IN PVOID       Function,
    IN ULONG       ParamsCount, 
    IN PULONG_PTR  ParametersArray 
    )
{

    switch( ParamsCount )
    {
    case 1:    return ((Param1SysProc) Function)( 
                                          ParametersArray[ 0 ] );

    case 2:    return ((Param2SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ] );

    case 3: return ((Param3SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ], 
                                          ParametersArray[ 2 ] );

    case 4: return ((Param4SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ], 
                                          ParametersArray[ 2 ], 
                                          ParametersArray[ 3 ] );

    case 5: return ((Param5SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ], 
                                          ParametersArray[ 2 ], 
                                          ParametersArray[ 3 ],
                                          ParametersArray[ 4 ] );

    case 6: return ((Param6SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ], 
                                          ParametersArray[ 2 ], 
                                          ParametersArray[ 3 ],
                                          ParametersArray[ 4 ],
                                          ParametersArray[ 5 ] );

    case 7: return ((Param7SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ], 
                                          ParametersArray[ 2 ], 
                                          ParametersArray[ 3 ],
                                          ParametersArray[ 4 ],
                                          ParametersArray[ 5 ],
                                          ParametersArray[ 6 ] );

    case 8: return ((Param8SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ], 
                                          ParametersArray[ 2 ], 
                                          ParametersArray[ 3 ],
                                          ParametersArray[ 4 ],
                                          ParametersArray[ 5 ],
                                          ParametersArray[ 6 ],
                                          ParametersArray[ 7 ] );

    case 9: return ((Param9SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ], 
                                          ParametersArray[ 2 ], 
                                          ParametersArray[ 3 ],
                                          ParametersArray[ 4 ],
                                          ParametersArray[ 5 ],
                                          ParametersArray[ 6 ],
                                          ParametersArray[ 7 ],
                                          ParametersArray[ 8 ] );

    case 10: return ((Param10SysProc) Function)( 
                                          ParametersArray[ 0 ], 
                                          ParametersArray[ 1 ], 
                                          ParametersArray[ 2 ], 
                                          ParametersArray[ 3 ],
                                          ParametersArray[ 4 ],
                                          ParametersArray[ 5 ],
                                          ParametersArray[ 6 ],
                                          ParametersArray[ 7 ],
                                          ParametersArray[ 8 ],
                                          ParametersArray[ 9 ]);

    default:
        KeBugCheckEx( OC_THREAD_BUG_TOO_MANY_PARAMETERS, 
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)Function, 
                      (ULONG_PTR)ParamsCount, 
                      (ULONG_PTR)ParametersArray );
    }
}

//-------------------------------------------------------------

NTSTATUS
OcCreateWorkerThread(
    IN ULONG    InternalId,
    OUT POC_WORKER_THREAD_OBJECT *PtrPtrWorkerThreadObject
    )
{
    NTSTATUS                    RC;
    POC_WORKER_THREAD_OBJECT    PtrWorkerThreadObject = NULL;
    HANDLE                      ThreadHandle = NULL;

    ASSERT( g_OcWorkerThreadsSubsystemInitialized );

    RC = OcObCreateObject( &g_OcWorkerThreadObjectType,
                           (POC_OBJECT_BODY*)&PtrWorkerThreadObject );

    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // initialize all fields before starting the thread
    //
    RtlZeroMemory( PtrWorkerThreadObject, sizeof( *PtrWorkerThreadObject ) );
    KeInitializeEvent( &PtrWorkerThreadObject->WakeupEven, SynchronizationEvent, FALSE );
    InitializeListHead( &PtrWorkerThreadObject->ListHead );
    KeInitializeSpinLock( &PtrWorkerThreadObject->LisSpinLock );
    PtrWorkerThreadObject->InternalId = InternalId;
    PtrWorkerThreadObject->TerminateThread = FALSE;

    //
    // start the thread
    //
    RC = PsCreateSystemThread( &ThreadHandle,
                               (ACCESS_MASK)0L, 
                               NULL, 
                               NULL, 
                               NULL, 
                               OcWorkerThreadRoutine, 
                               NULL );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    RC = ObReferenceObjectByHandle( ThreadHandle, 
                                    THREAD_ALL_ACCESS, 
                                    NULL, 
                                    KernelMode, 
                                    &PtrWorkerThreadObject->Thread, 
                                    NULL );

    if( !NT_SUCCESS( RC ) )
        goto __exit;

__exit:

    if( !NT_SUCCESS( RC ) && NULL != PtrWorkerThreadObject ){

        //
        // dereference the object, the side effect is that this thread will
        // wait for the worker thread termination in OcObDeleteObjectMethod
        //
        OcObDereferenceObject( (POC_OBJECT_BODY)PtrWorkerThreadObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrWorkerThreadObject );

        //
        // wait for the thread termination
        //
        if( NULL != ThreadHandle )
            ZwWaitForSingleObject( ThreadHandle, FALSE, NULL );
    }

    if( NULL != ThreadHandle )
        ZwClose( ThreadHandle );

    if( NT_SUCCESS( RC ) )
        *PtrPtrWorkerThreadObject = PtrWorkerThreadObject;

    return RC;
}

//-------------------------------------------------------------

