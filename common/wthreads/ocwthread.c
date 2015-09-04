/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
24.11.2006 
 Start
*/

#include <ocwthread.h>
#include <vadefs.h>

#define OC_WORKITEM_PARAMETERS_MAX_NUMBER  10

#define GET_POINTER_TO_PARAMETERS_ARRAY( WorkItemHeader ) ( (PULONG_PTR)( (POC_WORKITEM_HEADER)WorkItemHeader+0x1 ) )

#define DECLARE_WORKITEM( NumberOfParameters )              \
    typedef struct OC_WORKITEM_##NumberOfParameters{        \
    OC_WORKITEM_HEADER    Header;                           \
    ULONG_PTR             Parameter[ NumberOfParameters ];  \
} OC_WORKITEM_##NumberOfParameters, *POC_WORKITEM_##NumberOfParameters;


DECLARE_WORKITEM( 0 )
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

static ULONG    g_OcWthSizeOfWorkItemStruct[ OC_WORKITEM_PARAMETERS_MAX_NUMBER + 0x1 ] = {
SIZE_OF_WORKITEM_STRUCT( 0 ),
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

//
// global exported variables
//
OC_OBJECT_TYPE    g_OcWthWorkItemListType;

//
// global static variables
//
static OC_OBJECT_TYPE           g_OcWthObjectType;
static KEVENT                   g_OcWthObjectTypeUninitializationEvent;
static KEVENT                   g_OcWthWorkItemListTypeUninitializationEvent;
static NPAGED_LOOKASIDE_LIST    g_OcWthNPagedListForWorkItems[ OC_WORKITEM_PARAMETERS_MAX_NUMBER + 0x1 ];
static BOOLEAN                  g_OcWthSubsystemInitialized = FALSE;

//-------------------------------------------------------------

static
VOID 
OcWthStopWorkerThreadAndWait(
    IN POC_OBJECT_BODY    ObjectBody
    );

VOID
OcWthProcessAllWorkItems(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject 
    );

static
VOID 
OcWthAllWorkerThreadsStopped(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    );

static
VOID
OcWthAllWorkItemListFreed(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    );

NTSTATUS 
OcWthCallCallWorkItemFunction( 
    IN PVOID       Function,
    IN ULONG       ParamsCount, 
    IN PULONG_PTR  ParametersArray 
    );

static
POC_WORKITEM_HEADER
OcWthAllocateWorkItem(
    IN ULONG    NumberOfParameters
    );

static
VOID
OcWthFreeWorkItem(
    IN POC_WORKITEM_HEADER    PtrWorkItemHeader
    );

//
// ZwWaitForSingleObject is not declared in the ntddk.h file
//
NTSYSAPI
NTSTATUS
NTAPI
ZwWaitForSingleObject (
    IN HANDLE           Handle,
    IN BOOLEAN          Alertable,
    IN PLARGE_INTEGER   Timeout OPTIONAL
);

//-------------------------------------------------------------

NTSTATUS
OcWthInitializeWorkerThreadsSubsystem(
    IN PVOID Context
    )
{
    int    i;
    OC_OBJECT_TYPE_INITIALIZER_VAR( TypeInitializer );

    ASSERT( FALSE == g_OcWthSubsystemInitialized );
    ASSERT( TRUE == OcObIsObjectManagerInitialized() );
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // protect us from the double initialization
    //

    if( TRUE == g_OcWthSubsystemInitialized ){

        KeBugCheckEx( OC_WTH_INTERNAL_INCONSISTENCY_2, 
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)NULL, 
                      (ULONG_PTR)NULL, 
                      (ULONG_PTR)NULL );

        return STATUS_SUCCESS;
    }

#if DBG
    if( ( sizeof( g_OcWthSizeOfWorkItemStruct )/sizeof( g_OcWthSizeOfWorkItemStruct[ 0x0 ] ) ) != (OC_WORKITEM_PARAMETERS_MAX_NUMBER+0x1) ||
        ( sizeof( g_OcWthNPagedListForWorkItems )/sizeof( g_OcWthNPagedListForWorkItems[ 0x0 ] ) ) != (OC_WORKITEM_PARAMETERS_MAX_NUMBER+0x1) || 
         sizeof( OC_WORKITEM_10 ) != g_OcWthSizeOfWorkItemStruct[ 10 ] || 
         FALSE == OcObIsObjectManagerInitialized() ){

        KeBugCheckEx( OC_WTH_INTERNAL_INCONSISTENCY_1, 
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)NULL, 
                      (ULONG_PTR)NULL, 
                      (ULONG_PTR)NULL );
    }
#endif//DBG

    //
    // initialize the event that is set in a signal state when
    // all thread objects and object type will be deleted
    //
    KeInitializeEvent( &g_OcWthObjectTypeUninitializationEvent,
                       NotificationEvent,
                       FALSE );

    //
    // initialize worker thread object type 
    //
    OC_TOGGLE_TYPE_INITIALIZER( &TypeInitializer );
    TypeInitializer.Tag = 'TWcO';
    TypeInitializer.ObjectsBodySize = sizeof( OC_WORKER_THREAD_OBJECT );
    TypeInitializer.Methods.DeleteObject = OcWthStopWorkerThreadAndWait;
    TypeInitializer.Methods.DeleteObjectType = OcWthAllWorkerThreadsStopped;

    OcObInitializeObjectType( &TypeInitializer,
                              &g_OcWthObjectType );

    //
    // initialize the event that is set in a signal state when
    // all work items list objects and object type will be deleted
    //
    KeInitializeEvent( &g_OcWthWorkItemListTypeUninitializationEvent,
                       NotificationEvent,
                       FALSE );

    //
    // intialize the work item list object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &TypeInitializer );
    TypeInitializer.Tag = 'LWcO';
    TypeInitializer.ObjectsBodySize = sizeof( OC_WORK_ITEM_LIST_OBJECT );
    TypeInitializer.Methods.DeleteObject = NULL;
    TypeInitializer.Methods.DeleteObjectType = OcWthAllWorkItemListFreed;

    OcObInitializeObjectType( &TypeInitializer,
                              &g_OcWthWorkItemListType );

    //
    // initialize the work items allocator
    //

    for( i = 0x0; i <= OC_WORKITEM_PARAMETERS_MAX_NUMBER; ++i ){

        ExInitializeNPagedLookasideList( &g_OcWthNPagedListForWorkItems[ i ],
                                         NULL,
                                         NULL,
                                         0x0,
                                         g_OcWthSizeOfWorkItemStruct[ i ],
                                         OC_WORK_ITEM_TAG,
                                         0x0 );
    }//for 


    g_OcWthSubsystemInitialized = TRUE;

    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

VOID
OcWthUninitializeWorkerThreadsSubsystem(
    IN PVOID Context
    )
{
    int    i;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcObIsObjectManagerInitialized() );

    if( FALSE == g_OcWthSubsystemInitialized )
        return;

    OcObDeleteObjectType( &g_OcWthObjectType );

    //
    // wait for stopping of all threads
    //
    KeWaitForSingleObject( &g_OcWthObjectTypeUninitializationEvent,
                           Executive,
                           KernelMode,
                           FALSE,
                           NULL );

    //
    // uninitialize the work items allocator
    //
    for( i = 0x0; i < OC_WORKITEM_PARAMETERS_MAX_NUMBER; ++i ){

        ExDeleteNPagedLookasideList( &g_OcWthNPagedListForWorkItems[ i ] );
    }//for 

    //
    // delete the work item list object type
    //
    OcObDeleteObjectType( &g_OcWthWorkItemListType );

    //
    // wait for freeing of all work item objects
    //
    KeWaitForSingleObject( &g_OcWthWorkItemListTypeUninitializationEvent,
                           Executive,
                           KernelMode,
                           FALSE,
                           NULL );

    g_OcWthSubsystemInitialized = FALSE;
}

//-------------------------------------------------------------

BOOLEAN
OcWthIsWorkerThreadManagerInitialized()
{
    return g_OcWthSubsystemInitialized;
}

//-------------------------------------------------------------

VOID 
OcWthAllWorkerThreadsStopped(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    KeSetEvent( &g_OcWthObjectTypeUninitializationEvent,
                IO_DISK_INCREMENT,
                FALSE );
}

//-------------------------------------------------------------

VOID
OcWthAllWorkItemListFreed(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    KeSetEvent( &g_OcWthWorkItemListTypeUninitializationEvent,
                IO_DISK_INCREMENT,
                FALSE );
}

//-------------------------------------------------------------

static
VOID
OcWthWorkerThreadRoutine(
    IN PVOID    Context
    )
    /*
    Internal routine!
    */
{
    //
    // remember about  THREAD_WAIT_OBJECTS !
    //
    PKEVENT   Events[ 0x2 ];
    POC_WORKER_THREAD_OBJECT    PtrWorkerTreadObject = ( POC_WORKER_THREAD_OBJECT )Context;
    POC_WORK_ITEM_LIST_OBJECT   PtrWorkItemListObject = PtrWorkerTreadObject->PtrWorkItemListObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    Events[ 0x0 ] = &PtrWorkItemListObject->WakeupEvent;
    Events[ 0x1 ] = &PtrWorkerTreadObject->WakeupEvent;

    while ( TRUE ){

        //
        // wait for the wake up events
        //
        KeWaitForMultipleObjects( sizeof( Events )/sizeof( Events[0x0] ),
                                  Events,
                                  WaitAny,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL,
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
        OcWthProcessAllWorkItems( PtrWorkItemListObject );

        if( PtrWorkerTreadObject->TerminateThread )
            PsTerminateSystemThread( STATUS_SUCCESS );
    }

#if DBG
    //
    // unattainable code
    //
    KeBugCheckEx( OC_WTH_BUG_UNATTAINABLE_CODE,
                  (ULONG_PTR)__LINE__,
                  (ULONG_PTR)Context,
                  (ULONG_PTR)PsGetCurrentThread(),
                  (ULONG_PTR)PsGetCurrentProcess() );
#endif//DBG
}

//-------------------------------------------------------------

VOID
OcWthProcessAllWorkItems(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject 
    )
{
    POC_WORKITEM_HEADER    PtrWorkItemHeader;
    PLIST_ENTRY            request;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    while( NULL != ( request = ExInterlockedRemoveHeadList( &PtrWorkItemListObject->ListHead, 
                                                            &PtrWorkItemListObject->ListSpinLock ) ) ){

        if( FALSE == PtrWorkItemListObject->PrivateList && !IsListEmpty( &PtrWorkItemListObject->ListHead ) ){

            //
            // wake up other thread, do not increment its priority, allow the
            // current thread to proceed
            //
            KeSetEvent( &PtrWorkItemListObject->WakeupEvent, IO_NO_INCREMENT, FALSE );
        }

        PtrWorkItemHeader = CONTAINING_RECORD( request, OC_WORKITEM_HEADER, ListEntry );

        OcWthCallCallWorkItemFunction( PtrWorkItemHeader->FunctionAddress,
                                       PtrWorkItemHeader->NumberOfParameters,
                                       GET_POINTER_TO_PARAMETERS_ARRAY( PtrWorkItemHeader ) );

        OcWthFreeWorkItem( PtrWorkItemHeader );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrWorkItemHeader )

    }//while
}

//-------------------------------------------------------------

NTSTATUS 
OcWthCallCallWorkItemFunction( 
    IN PVOID       Function,
    IN ULONG       ParamsCount, 
    IN PULONG_PTR  ParametersArray 
    )
{

    switch( ParamsCount )
    {
    case 0:    return ((Param0SysProc) Function)();

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
        KeBugCheckEx( OC_WTH_BUG_TOO_MANY_PARAMETERS, 
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)Function, 
                      (ULONG_PTR)ParamsCount, 
                      (ULONG_PTR)ParametersArray );
    }
}

//-------------------------------------------------------------

NTSTATUS
OcWthCreateWorkItemListObject(
    IN BOOLEAN    PrivateList,
    OUT POC_WORK_ITEM_LIST_OBJECT*    PtrPtrWorkItemListObject
    )
{
    NTSTATUS    RC;
    POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject; 

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    RC = OcObCreateObject( &g_OcWthWorkItemListType,
                           (POC_OBJECT_BODY*)&PtrWorkItemListObject );

    if( !NT_SUCCESS( RC ) )
        return RC;

    KeInitializeEvent( &PtrWorkItemListObject->WakeupEvent, SynchronizationEvent, FALSE );
    InitializeListHead( &PtrWorkItemListObject->ListHead );
    KeInitializeSpinLock( &PtrWorkItemListObject->ListSpinLock );
    PtrWorkItemListObject->PrivateList = PrivateList;

    *PtrPtrWorkItemListObject = PtrWorkItemListObject;

    return RC;
}

//-------------------------------------------------------------

NTSTATUS
OcWthCreateWorkerThread(
    IN ULONG    InternalId,
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject OPTIONAL,
    OUT POC_WORKER_THREAD_OBJECT    *PtrPtrWorkerThreadObject
    )
{
    NTSTATUS                    RC;
    POC_WORKER_THREAD_OBJECT    PtrWorkerThreadObject = NULL;
    HANDLE                      ThreadHandle = NULL;

    ASSERT( g_OcWthSubsystemInitialized );
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    RC = OcObCreateObject( &g_OcWthObjectType,
                           (POC_OBJECT_BODY*)&PtrWorkerThreadObject );

    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // initialize all fields before starting the thread
    //
    RtlZeroMemory( PtrWorkerThreadObject, sizeof( *PtrWorkerThreadObject ) );
    PtrWorkerThreadObject->InternalId = InternalId;
    PtrWorkerThreadObject->TerminateThread = FALSE;
    KeInitializeEvent( &PtrWorkerThreadObject->WakeupEvent, SynchronizationEvent, FALSE );

    if( NULL == PtrWorkItemListObject ){

        RC = OcWthCreateWorkItemListObject( TRUE, &PtrWorkerThreadObject->PtrWorkItemListObject );

    } else {

        ASSERT( FALSE == PtrWorkItemListObject->PrivateList );
        PtrWorkerThreadObject->PtrWorkItemListObject = PtrWorkItemListObject;
        OcObReferenceObject( PtrWorkItemListObject );
    }

    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // start the thread
    //
    RC = PsCreateSystemThread( &ThreadHandle,
                               (ACCESS_MASK)0L, 
                               NULL, 
                               NULL, 
                               NULL, 
                               OcWthWorkerThreadRoutine, 
                               (PVOID)PtrWorkerThreadObject );
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

        POC_WORK_ITEM_LIST_OBJECT    PtrThreadWorkItemListObject = PtrWorkerThreadObject->PtrWorkItemListObject;

        //
        // reference the work item list object, because if the thread has started
        // but has not been referenced the thread object's delete method might
        // premature delete the work item list object, i.e. before thread termination.
        // This work around is needed because the thread object might be 
        // partially initialized.
        //
        if( NULL != PtrThreadWorkItemListObject )
            OcObReferenceObject( PtrThreadWorkItemListObject );

        //
        // Dereference the object, the side effect is that this thread will
        // wait for the worker thread termination in OcObDeleteObjectMethod.
        // If the system thread object has not been referenced the wait will 
        // be done in the following call to the ZwWaitForSingleObject function.
        //
        OcObDereferenceObject( (POC_OBJECT_BODY)PtrWorkerThreadObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrWorkerThreadObject );

        //
        // wait for the thread termination
        //
        if( NULL != ThreadHandle )
            ZwWaitForSingleObject( ThreadHandle, FALSE, NULL );

        if( NULL != PtrThreadWorkItemListObject )
            OcObDereferenceObject( PtrThreadWorkItemListObject );
    }

    if( NULL != ThreadHandle )
        ZwClose( ThreadHandle );

    if( NT_SUCCESS( RC ) )
        *PtrPtrWorkerThreadObject = PtrWorkerThreadObject;

    return RC;
}

//-------------------------------------------------------------

static
VOID
OcWthFreeWorkItem(
    IN POC_WORKITEM_HEADER    PtrWorkItemHeader
    )
    /*
    Internal routine
    */
{
    BOOLEAN     IsAllocatedOnStack = (0x0 != PtrWorkItemHeader->Flags.AllocatedOnStack );

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( (TRUE == IsAllocatedOnStack)? (NULL!=PtrWorkItemHeader->CallerEvent): (NULL == PtrWorkItemHeader->CallerEvent) );

    if( NULL != PtrWorkItemHeader->CallerEvent ){

        KeSetEvent( PtrWorkItemHeader->CallerEvent,
                    IO_DISK_INCREMENT,
                    FALSE );
    }

    //
    // free the work item if it has not been allocated on the stack
    //
    if( FALSE == IsAllocatedOnStack ){

#ifndef USE_STD_ALLOCATOR
        ExFreeToNPagedLookasideList( &g_OcWthNPagedListForWorkItems[ PtrWorkItemHeader->NumberOfParameters ], (PVOID)PtrWorkItemHeader );
#else//USE_STD_ALLOCATOR
        ExFreePoolWithTag( (PVOID)PtrWorkItemHeader, OC_WORK_ITEM_TAG );
#endif//USE_STD_ALLOCATOR

    }
}

//-------------------------------------------------------------

static
POC_WORKITEM_HEADER
OcWthAllocateWorkItem(
    IN ULONG    NumberOfParameters
    )
    /*
    Internal routine
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NumberOfParameters <= OC_WORKITEM_PARAMETERS_MAX_NUMBER );

#ifndef USE_STD_ALLOCATOR
    return ExAllocateFromNPagedLookasideList( &g_OcWthNPagedListForWorkItems[ NumberOfParameters ] );
#else//USE_STD_ALLOCATOR
    return ExAllocatePoolWithTag( NonPagedPool, g_OcWthSizeOfWorkItemStruct[ NumberOfParameters ], OC_WORK_ITEM_TAG );
#endif//USE_STD_ALLOCATOR
}

//-------------------------------------------------------------

static
VOID
OcWthStopWorkerThreadAndWait(
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

    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrWorkerTreadObject->Thread );

    //
    // derefrence the work item list object for this thread,
    // if the object is not private it might be alive 
    // for some period of time due to the reference from
    // the other threads
    //
    if( PtrWorkerTreadObject->PtrWorkItemListObject )
        OcObDereferenceObject( PtrWorkerTreadObject->PtrWorkItemListObject );

    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrWorkerTreadObject->PtrWorkItemListObject );

}

//-------------------------------------------------------------

static
NTSTATUS
OcWthPostWorkItemInternal(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN PVOID    Function,
    IN ULONG    NumberOfParameters,
    ...//parameters must have size equals to sizeof( ULONG_PTR )
    )
    /*
    Internal routine!
    The counterpart for this function is an internal 
    function OcWthStopWorkerThreadAndWait
    which is called by the OcObject Manager when the thread object's
    reference count drops to zero
    */
{
    POC_WORKITEM_HEADER    PtrWorkItemHeader;
    OC_WORKITEM_HEADER     StaticWorkItemHeader;
    KEVENT                 StaticEvent;
    PULONG_PTR             ParametersArray;
    ULONG                  NumberOfCopiedParameters;
    va_list                arg_list;

    ASSERT( SynchronousCall? (KeGetCurrentIrql() <= APC_LEVEL):(KeGetCurrentIrql() <= DISPATCH_LEVEL) );
    ASSERT( NumberOfParameters <= OC_WORKITEM_PARAMETERS_MAX_NUMBER );

    //
    // allocate a work item, allocate on the stack for a syncronous call
    //
    if( SynchronousCall ){

        PtrWorkItemHeader = &StaticWorkItemHeader;
        PtrWorkItemHeader->Flags.AllocatedOnStack = 0x1;

        //
        // for synchronous call I need an event to wait for completion,
        // create a notification event
        //
        KeInitializeEvent( &StaticEvent, NotificationEvent, FALSE );
        PtrWorkItemHeader->CallerEvent = &StaticEvent;

    } else {

        PtrWorkItemHeader = OcWthAllocateWorkItem( NumberOfParameters );
        if( NULL == PtrWorkItemHeader )
            return STATUS_INSUFFICIENT_RESOURCES;

        PtrWorkItemHeader->Flags.AllocatedOnStack = 0x0;
        PtrWorkItemHeader->CallerEvent = NULL;
    }

    PtrWorkItemHeader->FunctionAddress = Function;
    PtrWorkItemHeader->NumberOfParameters = NumberOfParameters;

    //
    // get pointer to parameters array
    //
    ParametersArray = GET_POINTER_TO_PARAMETERS_ARRAY( PtrWorkItemHeader );

#if defined(_AMD64_) && !defined(_M_AMD64)
#error "defined(_M_AMD64) is not defined! must be defined for _crt_va_start && _crt_va_end "
#endif//defined(_AMD64_) && !defined(_M_AMD64)

    //
    // fill in the parameters
    //
    NumberOfCopiedParameters = 0x0;
    _crt_va_start( arg_list, NumberOfParameters );
    while( (++NumberOfCopiedParameters) <= NumberOfParameters ){
        *ParametersArray = _crt_va_arg( arg_list, ULONG_PTR );
        ++ParametersArray;
    }
    _crt_va_end( arg_list );

    //
    // insert new work item in the thread's list
    //
    ExInterlockedInsertTailList( &PtrWorkItemListObject->ListHead,
                                 &PtrWorkItemHeader->ListEntry,
                                 &PtrWorkItemListObject->ListSpinLock );

    //
    // notify the thread about the new work item
    //
    KeSetEvent( &PtrWorkItemListObject->WakeupEvent,
                IO_DISK_INCREMENT,
                FALSE );

    //
    // wait for completion in case of synchronous request
    //
    if( SynchronousCall ){

        KeWaitForSingleObject( &StaticEvent,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL);
    }

    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam0(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN    SynchronousCall,
    IN Param0SysProc    Function
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      0 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam1(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN Param1SysProc    Function,
    IN ULONG_PTR    Parameter1
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      1,
                                      Parameter1 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam2(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN Param2SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      2,
                                      Parameter1,
                                      Parameter2 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam3(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN Param3SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      3,
                                      Parameter1,
                                      Parameter2,
                                      Parameter3 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam4(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN Param4SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      4,
                                      Parameter1,
                                      Parameter2,
                                      Parameter3,
                                      Parameter4 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam5(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN Param5SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      5,
                                      Parameter1,
                                      Parameter2,
                                      Parameter3,
                                      Parameter4,
                                      Parameter5 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam6(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN Param6SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5,
    IN ULONG_PTR    Parameter6
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      6,
                                      Parameter1,
                                      Parameter2,
                                      Parameter3,
                                      Parameter4,
                                      Parameter5,
                                      Parameter6 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam7(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN Param7SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5,
    IN ULONG_PTR    Parameter6,
    IN ULONG_PTR    Parameter7
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      7,
                                      Parameter1,
                                      Parameter2,
                                      Parameter3,
                                      Parameter4,
                                      Parameter5,
                                      Parameter6,
                                      Parameter7 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam8(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
    IN Param8SysProc    Function,
    IN ULONG_PTR    Parameter1,
    IN ULONG_PTR    Parameter2,
    IN ULONG_PTR    Parameter3,
    IN ULONG_PTR    Parameter4,
    IN ULONG_PTR    Parameter5,
    IN ULONG_PTR    Parameter6,
    IN ULONG_PTR    Parameter7,
    IN ULONG_PTR    Parameter8
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      8,
                                      Parameter1,
                                      Parameter2,
                                      Parameter3,
                                      Parameter4,
                                      Parameter5,
                                      Parameter6,
                                      Parameter7,
                                      Parameter8 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam9(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
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
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      9,
                                      Parameter1,
                                      Parameter2,
                                      Parameter3,
                                      Parameter4,
                                      Parameter5,
                                      Parameter6,
                                      Parameter7,
                                      Parameter8,
                                      Parameter9 );
}

//-------------------------------------------------------------

NTSTATUS
OcWthPostWorkItemParam10(
    IN POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject,
    IN BOOLEAN  SynchronousCall,
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
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    return OcWthPostWorkItemInternal( PtrWorkItemListObject,
                                      SynchronousCall,
                                      (PVOID)Function,
                                      10,
                                      Parameter1,
                                      Parameter2,
                                      Parameter3,
                                      Parameter4,
                                      Parameter5,
                                      Parameter6,
                                      Parameter7,
                                      Parameter8,
                                      Parameter9,
                                      Parameter10 );
}

//-------------------------------------------------------------

