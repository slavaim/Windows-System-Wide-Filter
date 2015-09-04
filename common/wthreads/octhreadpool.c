/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
28.11.2006 
 Start
*/

#include <octhreadpool.h>

#define OC_POOL_OBJECT_TO_WOTKER_THREAD_ARRAY( PoolObjectHeader )  ( (POC_WORKER_THREAD_OBJECT*)( (POC_THREAD_POOL_OBJECT)PoolObjectHeader + 0x1 ) )

//----------------------------------------------

static
VOID
OcTplDeleteObjectType(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    );

static
VOID
OcTplStopAllThreadPools(
    IN POC_THREAD_POOL_OBJECT    PtrThreadPoolObjHeader
    );

//----------------------------------------------

NTSTATUS
OcTplCreateThreadPool(
    IN ULONG    NumberOfThreads,
    IN BOOLEAN  EachThreadHasPrivateList,
    OUT POC_THREAD_POOL_OBJECT* PtrPtrPoolObject
    )
{
    NTSTATUS                         RC;
    ULONG                            SizeOfObject;
    POC_OBJECT_TYPE                  ThreadPoolObjectType = NULL;
    POC_THREAD_POOL_OBJECT           PtrThreadPoolObject = NULL;
    POC_WORKER_THREAD_OBJECT*        PtrWorkerThreadArray;
    ULONG                            i;
    OC_OBJECT_TYPE_INITIALIZER_VAR( TypeInitializer );

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( TRUE == OcObIsObjectManagerInitialized() );
    ASSERT( TRUE == OcWthIsWorkerThreadManagerInitialized() );

    //
    // calculate the object's body size
    //
    SizeOfObject = sizeof( OC_THREAD_POOL_OBJECT ) + 
                   sizeof( POC_WORKER_THREAD_OBJECT )*NumberOfThreads;

    //
    // allocate memory for the object type
    //
    ThreadPoolObjectType = ExAllocatePoolWithTag( NonPagedPool, 
                                                  sizeof( *ThreadPoolObjectType ),
                                                  'PTcO');
    if( NULL == ThreadPoolObjectType )
        return STATUS_INSUFFICIENT_RESOURCES;

    //
    // fill in the thread pool type initializer
    //
    OC_TOGGLE_TYPE_INITIALIZER( &TypeInitializer );
    TypeInitializer.Tag = 'PTcO';
    TypeInitializer.ObjectsBodySize = SizeOfObject;
    TypeInitializer.Methods.DeleteObject = OcTplStopAllThreadPools;
    TypeInitializer.Methods.DeleteObjectType = OcTplDeleteObjectType;

    OcObInitializeObjectType( &TypeInitializer,
                              ThreadPoolObjectType );

    //
    // create the thread pool object, only one object 
    // will exist and when it is deleted the
    // thread object type will also be deleted
    //

    RC = OcObCreateObject( ThreadPoolObjectType,
                           (POC_OBJECT_BODY*)&PtrThreadPoolObject );

    //
    // mark the object type as pended for 
    // deletion, so when the last object is deleted
    // the object type will be automatically deleted,
    // if the OcObCreateObject failed the object
    // type will be deleted immediatelly
    //
    OcObDeleteObjectType( ThreadPoolObjectType );

    //
    // object type has been marked as delted and becomes
    // invalid after dereferencing the last object
    //
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ThreadPoolObjectType );

    //
    // check the status returned by OcObCreateObject
    //
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // initialize the new pool object
    //
    RtlZeroMemory( (PVOID)PtrThreadPoolObject, SizeOfObject );
    PtrThreadPoolObject->NumberOfThreads = NumberOfThreads;
 
    //
    // create the shared work item list, if needed
    //
    if( FALSE == EachThreadHasPrivateList ){

        RC = OcWthCreateWorkItemListObject( FALSE, &PtrThreadPoolObject->PtrWorkItemListObject );
        if( !NT_SUCCESS( RC ) )
            goto __exit;
    }

    ASSERT( EachThreadHasPrivateList? NULL == PtrThreadPoolObject->PtrWorkItemListObject :
                                      NULL != PtrThreadPoolObject->PtrWorkItemListObject );
    //
    // create worker thread objects and save them in the pool object
    //
    PtrWorkerThreadArray = OC_POOL_OBJECT_TO_WOTKER_THREAD_ARRAY( PtrThreadPoolObject );
    for( i = 0x0; i<NumberOfThreads; ++i ){

        //
        // create worker thread
        //
        RC = OcWthCreateWorkerThread( i, 
                                      PtrThreadPoolObject->PtrWorkItemListObject,
                                      &PtrWorkerThreadArray[ i ] );

        if( !NT_SUCCESS( RC ) )
            break;
    }

    //
    // worker threads creating failed
    //
    if( !NT_SUCCESS( RC ) )
        goto __exit;

__exit:

    if( !NT_SUCCESS( RC ) ){

        if( NULL != PtrThreadPoolObject )
            OcObDereferenceObject( PtrThreadPoolObject );

        //
        // object type has been marked as delted and becomes
        // invalid after dereferencing the last object
        //
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ThreadPoolObjectType );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrThreadPoolObject );

    } else {

        *PtrPtrPoolObject = PtrThreadPoolObject;
    }

    return RC;
}

//----------------------------------------------

VOID
OcTplDeleteObjectType(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    )
    /*
    Internal routine!
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    ExFreePoolWithTag( (PVOID)ObjectType, ObjectType->Tag );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ObjectType );
}

//----------------------------------------------

VOID
OcTplStopAllThreadPools(
    IN POC_THREAD_POOL_OBJECT    PtrThreadPoolObject
    )
{
    POC_WORKER_THREAD_OBJECT*    PtrWorkerThreadArray;
    ULONG i;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcObIsObjectManagerInitialized() );

    PtrWorkerThreadArray = OC_POOL_OBJECT_TO_WOTKER_THREAD_ARRAY( PtrThreadPoolObject );

    for( i = 0x0; i<PtrThreadPoolObject->NumberOfThreads; ++i ){

        if( NULL == PtrWorkerThreadArray[ i ] )
            continue;

        //
        // dereference object, if this is the last refrence the 
        // thread will be synchroniously terminated
        //
        OcObDereferenceObject( PtrWorkerThreadArray[ i ] );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrWorkerThreadArray[ i ] );
    }

    if( PtrThreadPoolObject->PtrWorkItemListObject )
        OcObDereferenceObject( PtrThreadPoolObject->PtrWorkItemListObject );

}

//----------------------------------------------

POC_WORKER_THREAD_OBJECT
OcTplReferenceThreadByIndex(
    IN POC_THREAD_POOL_OBJECT    PtrThreadPoolObject,
    IN ULONG    ThreadIndex
    )
{
    POC_WORKER_THREAD_OBJECT*    PtrWorkerThreadArray;

    ASSERT( ThreadIndex <= PtrThreadPoolObject->NumberOfThreads );
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    PtrWorkerThreadArray = OC_POOL_OBJECT_TO_WOTKER_THREAD_ARRAY( PtrThreadPoolObject );
    OcObReferenceObject( PtrWorkerThreadArray[ ThreadIndex ] );

    return PtrWorkerThreadArray[ ThreadIndex ];
}

//----------------------------------------------

POC_WORK_ITEM_LIST_OBJECT
OcTplReferenceSharedWorkItemList(
    IN POC_THREAD_POOL_OBJECT    PtrThreadPoolObject
    )
    /*
    May return NULL if all pool's thread have private work item list objects
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( TRUE == OcWthIsWorkerThreadManagerInitialized() );

    if( NULL != PtrThreadPoolObject->PtrWorkItemListObject )
        OcObReferenceObject( PtrThreadPoolObject->PtrWorkItemListObject );

    return PtrThreadPoolObject->PtrWorkItemListObject;
}

//----------------------------------------------

