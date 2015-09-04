/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
01.12.2006 ( December )
 Start
*/

#include <ochash.h>

static BOOLEAN          g_IsHashManagerInitialized = FALSE;
static OC_OBJECT_TYPE   g_OcHsHashObjectType;
static OC_OBJECT_TYPE   g_OcHsHashEntryObjectType;
static KEVENT           g_OcHsEventHashObjectTypeUninitialized;

#define OC_HASH_HEADS_ARRAY_TAG  'aHcO'

//-----------------------------------------------------

//
// object with object header
//
typedef struct _OC_HASH_ENTRY_OBJECT{

    //
    // all entries with the same hash index are 
    // connected in a double linked list
    //
    LIST_ENTRY    ListEntry;

    //
    // When this value greater than zero the removed
    // entry's context can't be processed in purge function,
    // because the entry is used by some other functions
    // that assume that context is valid. The purge
    // function may invalidate the context.
    // 
    ULONG    PurgeDisabeled;

    //
    // this field is the parameter for the hash key(index) function
    //
    ULONG_PTR    KeyValue;

    //
    // this field contains context associated with this hash entry
    //
    PVOID    Context;

    //
    // the function which is called for Context when 
    // the hash object is deleted
    //
    FunctionForContext    DeleteContextFunction;

#if DBG
    //
    // index of this entry in the hash array
    //
    ULONG              HashLineIndex;
    POC_HASH_OBJECT    PtrHashObject;
    KIRQL              ClientIrqlForDeleteFunction;
#endif//DBG

} OC_HASH_ENTRY_OBJECT, *POC_HASH_ENTRY_OBJECT;

//-----------------------------------------------------

static
VOID
NTAPI
OcHsDeleteHashObjectType(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    );

static
VOID
NTAPI
OcHsDeleteHashObject(
    IN POC_HASH_OBJECT    PtrHashObject
    );

static
VOID
NTAPI
OcHsDeleteHashEntryObject(
    IN POC_HASH_ENTRY_OBJECT    PtrHashEntryObject
    );

//-----------------------------------------------------

__forceinline
VOID
OcHsRemoveEntryFromTheHashLine( 
    IN POC_HASH_ENTRY_OBJECT    PtrHashEntryObject
    )
    /*
    The caller must lock the hash line!
    */
{
    //
    // first - remove from the list
    //
    RemoveEntryList( &PtrHashEntryObject->ListEntry );

    //
    // second - mark the entry as removed
    //
    InitializeListHead( &PtrHashEntryObject->ListEntry );

}

//-----------------------------------------------------

VOID 
FASTCALL 
OcHsSleep(
    IN ULONG ulMilSecs
    )
{
    KEVENT            kEvent;
    LARGE_INTEGER    qTimeout;

    qTimeout.QuadPart = 10000L;
    qTimeout.QuadPart *= ulMilSecs;
    qTimeout.QuadPart = -(qTimeout.QuadPart);

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    KeInitializeEvent(&kEvent,SynchronizationEvent,FALSE);

    KeWaitForSingleObject((PVOID)&kEvent,Executive,KernelMode,FALSE,&qTimeout);
}

//-----------------------------------------------------

ULONG 
NTAPI
OcHsUniversalHashKeyFunction( 
    IN struct _OC_HASH_OBJECT*    PtrHashObject,
    IN ULONG_PTR    KeyValue
    )
{
    return  (ULONG)( ( ( (ULONG_PTR)KeyValue )>>5) % PtrHashObject->NumberOfHashLines );
}

//-----------------------------------------------------

NTSTATUS
NTAPI
OcHsInitializeHashManager(
    IN PVOID   Context
    )
{
    OC_OBJECT_TYPE_INITIALIZER_VAR( ObjectTypeInitializer );

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( FALSE == g_IsHashManagerInitialized );
    ASSERT( TRUE == OcObIsObjectManagerInitialized() );

#if DBG
    if( FALSE == OcObIsObjectManagerInitialized() ){

        KeBugCheckEx( OC_HASH_OBJ_MANAGER_NOT_INITIALIZED,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)NULL,
                      (ULONG_PTR)NULL,
                      (ULONG_PTR)NULL );
    }
#endif//DBG

    if( FALSE == OcObIsObjectManagerInitialized() )
        return STATUS_INVALID_PARAMETER;

    //
    // protect ourselves
    //
    if( TRUE == g_IsHashManagerInitialized )
        return STATUS_SUCCESS;

    KeInitializeEvent( &g_OcHsEventHashObjectTypeUninitialized,
                       NotificationEvent,
                       FALSE );

    //
    // initialize the hash object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'hHcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_HASH_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcHsDeleteHashObject;
    ObjectTypeInitializer.Methods.DeleteObjectType = OcHsDeleteHashObjectType;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &g_OcHsHashObjectType );


    //
    // initialize the hash entry object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'eHcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_HASH_ENTRY_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcHsDeleteHashEntryObject;
    ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &g_OcHsHashEntryObjectType );

    g_IsHashManagerInitialized = TRUE;

    return STATUS_SUCCESS;
}

//-----------------------------------------------------

VOID
NTAPI
OcHsUninitializeHashManager(
    IN PVOID   Context
    )
{
    //
    // we must be able to wait for g_OcHsEventHashObjectTypeUninitialized
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcObIsObjectManagerInitialized() );

    //
    // protect ourselves
    //
    if( FALSE == g_IsHashManagerInitialized )
        return;

    OcObDeleteObjectType( &g_OcHsHashEntryObjectType );
    OcObDeleteObjectType( &g_OcHsHashObjectType );

    KeWaitForSingleObject( &g_OcHsEventHashObjectTypeUninitialized,
                           Executive,
                           KernelMode,
                           FALSE,
                           NULL );

}

//-----------------------------------------------------

VOID
NTAPI
OcHsDeleteHashObjectType(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    )
    /*
    Internal routine!
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    KeSetEvent( &g_OcHsEventHashObjectTypeUninitialized, 
                IO_DISK_INCREMENT,
                FALSE );
}

//-----------------------------------------------------

static
VOID
NTAPI
OcHsDeleteHashEntryObject(
    IN POC_HASH_ENTRY_OBJECT    PtrHashEntryObject
    )
    /*
    Internal routine!
    */
{
#if DBG
    if( !IsListEmpty( &PtrHashEntryObject->ListEntry ) ){
        //
        // the entry is still in the hash or not properly initialized
        //
        KeBugCheckEx( OC_HASH_DELETED_ENTRY_STILL_IN_HASH,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)PtrHashEntryObject,
                      (ULONG_PTR)NULL,
                      (ULONG_PTR)NULL );
    }
#endif//DBG

    //
    // I decided to check the IRQL as the DeleteContextFunction
    // is usually are not supposed to be called at elevated IRQL,
    // so I dereference the hash entry object at the IRQL of the
    // caller
    //
    ASSERT( KeGetCurrentIrql() <= PtrHashEntryObject->ClientIrqlForDeleteFunction );

    //
    // call the function provided by the hash client for context
    // when it was removed from the hash
    //
    if( NULL != PtrHashEntryObject->DeleteContextFunction ){

        ASSERT( PtrHashEntryObject->Context );
        PtrHashEntryObject->DeleteContextFunction( PtrHashEntryObject->Context );
    }

    /*
    if( !IsListEmpty( &PtrHashEntryObject->ListEntry ) ){

        //
        // something went wrong and the entry is in the hash but
        // entry's reference count has dropped to zero, try to save 
        // system in the consistent state and remove the entry from 
        // the hash line, else the has will contain pointer to
        // freed memory, the hash object will not be dereferenced
        // because I do not know the system's state and dangling
        // reference is better then an unexpectedly going out hash object
        //

        KIRQL    OldIrql;
        ULONG    HashLineIndex;

        HashLineIndex = PtrHashEntryObject->HashLineIndex;

        KeAcquireSpinLock( &(PtrHashObject->ArrayOfHashHeads[ HashLineIndex ].FineGrainedLock), &OldIrql );
        {
            OcHsRemoveEntryFromTheHashLine( PtrHashEntryObject );
        }
        KeReleaseSpinLock( &(PtrHashObject->ArrayOfHashHeads[ HashLineIndex ].FineGrainedLock), OldIrql );
    }
    */
}

//-----------------------------------------------------

NTSTATUS
NTAPI
OcHsCreateHash(
    IN ULONG    NumberOfHashLines,
    IN HashKeyFunction    FuncValueToHashLineIndex OPTIONAL,//actually not used
    OUT POC_HASH_OBJECT*    PtrPtrHashObj
    )
{

    NTSTATUS           RC;
    POC_HASH_OBJECT    PtrHashObject;
    ULONG              HashLineIndex;

    ASSERT( TRUE == g_IsHashManagerInitialized );
#if DBG
    if( FALSE == g_IsHashManagerInitialized ){

        KeBugCheckEx( OC_HASH_HASH_MANAGER_NOT_INITIALIZED,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)NULL,
                      (ULONG_PTR)NULL,
                      (ULONG_PTR)NULL );
    }
#endif//DBG

    RC = OcObCreateObject( &g_OcHsHashObjectType,
                           &PtrHashObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // initialize the object's body
    //
    RtlZeroMemory( PtrHashObject, sizeof( *PtrHashObject ) );
    PtrHashObject->NumberOfHashLines = NumberOfHashLines;
    PtrHashObject->KeyValueToHashIndexFunction = OcHsUniversalHashKeyFunction;

    //
    // allocate the hash line's heads array
    //
    OcSetFlag( PtrHashObject->Flags, OcHashHeadsArrayFromPool );
    PtrHashObject->ArrayOfHashHeads = ExAllocatePoolWithTag( NonPagedPool, 
                                                             NumberOfHashLines*sizeof( OC_HASH_HEAD ),
                                                             OC_HASH_HEADS_ARRAY_TAG );

    if( NULL == PtrHashObject->ArrayOfHashHeads ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    //
    // initialize the hash heads
    //
    for( HashLineIndex = 0x0;
         HashLineIndex < NumberOfHashLines;
         ++HashLineIndex ){

             POC_HASH_HEAD    PtrHashLine;

             PtrHashLine = &PtrHashObject->ArrayOfHashHeads[ HashLineIndex ];
             RtlZeroMemory( PtrHashLine, sizeof( *PtrHashLine ) );
             InitializeListHead( &PtrHashLine->ListHead );
             OcRwInitializeRwLock( &PtrHashLine->FineGrainedRwLock );
    }//for

__exit:

    if( !NT_SUCCESS( RC ) ){

        OcObDereferenceObject( PtrHashObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrHashObject );

    } else {

        *PtrPtrHashObj = PtrHashObject;
    }

    return RC;
}

//-----------------------------------------------------

VOID
NTAPI
OcHsDeleteHashObject(
    IN POC_HASH_OBJECT    PtrHashObject
    )
    /*
    Internal routine!
    */
{

    //
    // free the hash entry objects array if it has been allocated from the 
    // pool by the hash manager
    //
    if( OcIsFlagOn( PtrHashObject->Flags, OcHashHeadsArrayFromPool ) && 
        NULL != PtrHashObject->ArrayOfHashHeads ){

        ExFreePoolWithTag( PtrHashObject->ArrayOfHashHeads, OC_HASH_HEADS_ARRAY_TAG );
    }
}

//-----------------------------------------------------

NTSTATUS
NTAPI
OcHsInsertContextInHash(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN ULONG_PTR    KeyValue,
    IN PVOID    Context,
    IN FunctionForContext    ContextFunction OPTIONAL//usually OcObReferenceObject
    )
    /*
    If the caller will touch Context in ContextFunction
    then Context must be allocated from NP pool.
    The ContextFunction function will be called if
    the context is inserted in the hash, if the insertion
    fails the function will not be called.
    This function doesn't bother about duplicate entries.
    */
{
    NTSTATUS    RC;
    POC_HASH_ENTRY_OBJECT    PtrHashEntryObject;
    ULONG       HashLineIndex;
    KIRQL       OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // NULL is a reserved value!
    //
    if( NULL == Context ){

#if DBG
        KeBugCheckEx( OC_HASH_CONTEXT_IS_NULL,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)PtrHashObject,
                      (ULONG_PTR)KeyValue,
                      (ULONG_PTR)ContextFunction );
#endif//DBG
        return STATUS_INVALID_PARAMETER_3;
    }

#if DBG
    {
        PVOID    DuplicateContext;

        DuplicateContext = OcHsFindContextByKeyValue( PtrHashObject,
                                                      KeyValue,
                                                      NULL );
        if( NULL != DuplicateContext ){

            ASSERT( "OcHsInsertContextInHash: An attempt to insert the existing KeyValue" );
        }
    }
#endif//DBG

    RC = OcObCreateObject( &g_OcHsHashEntryObjectType,
                           &PtrHashEntryObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    RtlZeroMemory( PtrHashEntryObject, sizeof( *PtrHashEntryObject) );

    HashLineIndex = PtrHashObject->KeyValueToHashIndexFunction( PtrHashObject,
                                                                KeyValue );

    //
    // initialize the hash entry object
    //
    PtrHashEntryObject->KeyValue = KeyValue;
    PtrHashEntryObject->Context = Context;
#if DBG
    PtrHashEntryObject->HashLineIndex = HashLineIndex;
    PtrHashEntryObject->PtrHashObject = PtrHashObject;
#endif//DBG

    //
    // refrence the hash object before inserting the entry,
    // object will be derefernced when the entry is removed
    //
    OcObReferenceObject( PtrHashObject );

    //
    // call the context function before inserting the context's hash entry in the hash line
    //
    if( NULL != ContextFunction )
        ContextFunction( Context );

    //
    // insert in the hash line
    //
    OcRwAcquireLockForWrite( &(PtrHashObject->ArrayOfHashHeads[ HashLineIndex ].FineGrainedRwLock), &OldIrql );
    {// start of the lock
        InsertHeadList( &(PtrHashObject->ArrayOfHashHeads[ HashLineIndex ].ListHead),
                        &PtrHashEntryObject->ListEntry );
    }// end of the lock
    OcRwReleaseWriteLock( &(PtrHashObject->ArrayOfHashHeads[ HashLineIndex ].FineGrainedRwLock), OldIrql );

    ASSERT( !IsListEmpty( &PtrHashEntryObject->ListEntry ) );

    return STATUS_SUCCESS;

}

//-----------------------------------------------------

__forceinline
VOID
OcHsFreeRemovedHashEntryObject(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN POC_HASH_ENTRY_OBJECT    PtrHashEntryObject
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // the empty list is the mark that the hash entry is removed
    //
    InitializeListHead( &PtrHashEntryObject->ListEntry );

    //
    // Dereference the hash entry object,
    // thus the reference from the hash is removed.
    // The caller must dereference the hash entry himself.
    //
    OcObDereferenceObject( PtrHashEntryObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrHashEntryObject );

    //
    // dereference the hash object that has been referenced when the entry
    // was inserted in the hash
    //
    OcObDereferenceObject( PtrHashObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrHashObject );
}

//-----------------------------------------------------

VOID
OcHsRemoveEntryFromHash(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN POC_HASH_ENTRY_OBJECT    PtrHashEntryObject
    )
    /*
    Internal routine!
    The PtrHashEntryObject must be referenced by the caller!
    If the caller doesn't reference the hash entry before calling the 
    OcHsRemoveEntryFromHash the system may BSOD because the 
    hash may be purged and the hash entry goes out before 
    OcHsRemoveEntryFromHash will be called.
    */
{
    KIRQL    OldIrql;
    ULONG    HashLineIndex;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // the empty list is the mark that the hash entry 
    // has been already removed
    //
    if( IsListEmpty( &PtrHashEntryObject->ListEntry ) )
        return;

    HashLineIndex = PtrHashObject->KeyValueToHashIndexFunction( PtrHashObject, 
                                                                PtrHashEntryObject->KeyValue );

    //
    // remove the entry from the hash line
    //
    OcRwAcquireLockForWrite( &(PtrHashObject->ArrayOfHashHeads[ HashLineIndex ].FineGrainedRwLock), &OldIrql );
    {//start of the lock
        OcHsRemoveEntryFromTheHashLine( PtrHashEntryObject );
    }//end of the lock
    OcRwReleaseWriteLock( &(PtrHashObject->ArrayOfHashHeads[ HashLineIndex ].FineGrainedRwLock), OldIrql );

    OcHsFreeRemovedHashEntryObject( PtrHashObject, PtrHashEntryObject );

}

//-----------------------------------------------------

VOID
NTAPI
OcHsPurgeAllEntriesFromHash(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN FunctionForContext    ContextFunction OPTIONAL//usually OcObDereferenceObject
    /*
    the ContextFunction function will be called for all removed context
    */
    )
{
    ULONG    HashLineIndex;

    //
    // I must be able to wait with rescheduling in this function
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    for( HashLineIndex=0x0; 
         HashLineIndex < PtrHashObject->NumberOfHashLines;
         ++HashLineIndex ){

             POC_HASH_HEAD    PtrHashLine;

             PtrHashLine = &PtrHashObject->ArrayOfHashHeads[ HashLineIndex ];

             while( TRUE ){

                 KIRQL            OldIrql;
                 PLIST_ENTRY      request;
                 POC_HASH_ENTRY_OBJECT    PtrHashEntryObject;

                 OcRwAcquireLockForWrite( &PtrHashLine->FineGrainedRwLock, &OldIrql );
                 {// start of the lock
                    request = RemoveHeadList( &PtrHashLine->ListHead );
                 }// end of the lock
                 OcRwReleaseWriteLock( &PtrHashLine->FineGrainedRwLock, OldIrql );
                 if( &PtrHashLine->ListHead == request )
                     break;

                 PtrHashEntryObject = CONTAINING_RECORD( request, OC_HASH_ENTRY_OBJECT, ListEntry );

                 //
                 // Wait until the purge on this entry is enabled.
                 // As entry removed from the hash line the counter 
                 // can't be increased.
                 //
                 while( 0x0 != InterlockedCompareExchange( &PtrHashEntryObject->PurgeDisabeled,
                                                           0x0,
                                                           0x0 ) ){

                         //
                         // sleep with rescheduling
                         //
                         OcHsSleep( 50 );
                 }//while

                 if( NULL != ContextFunction ){

                    ASSERT( NULL == PtrHashEntryObject->DeleteContextFunction );
                    PtrHashEntryObject->DeleteContextFunction = ContextFunction;
 #if DBG
                    PtrHashEntryObject->ClientIrqlForDeleteFunction = KeGetCurrentIrql();
#endif//DBG
                 }

                 //
                 // free the hash entry object
                 //
                 PtrHashEntryObject = CONTAINING_RECORD( request, OC_HASH_ENTRY_OBJECT, ListEntry );
                 OcHsFreeRemovedHashEntryObject( PtrHashObject, PtrHashEntryObject );
                 OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrHashEntryObject );
             }//while
    }//for

}

//-----------------------------------------------------

PVOID
NTAPI
OcHsFindContextByKeyValue(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN ULONG_PTR    KeyValue,
    IN FunctionForContext    ContextFunction OPTIONAL//usually OcObReferenceObject
    )
    /*
    the ContextFunction will be called at DISPATCH_LEVEL with the locked
    hash line!
    */
{
    ULONG            HashLineIndex;
    POC_HASH_HEAD    PtrHashLine;
    KIRQL            OldIrql;
    PVOID            FoundContext = NULL;
    POC_HASH_ENTRY_OBJECT    PtrHashEntryObject;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    HashLineIndex = PtrHashObject->KeyValueToHashIndexFunction( PtrHashObject, KeyValue );
    PtrHashLine = &PtrHashObject->ArrayOfHashHeads[ HashLineIndex ];

    OcRwAcquireLockForRead( &PtrHashLine->FineGrainedRwLock, &OldIrql );
    {//start of the lock

        PLIST_ENTRY    request;

        for( request = PtrHashLine->ListHead.Flink; 
             request != &PtrHashLine->ListHead;
             request = request->Flink ){

                 PtrHashEntryObject = CONTAINING_RECORD( request, OC_HASH_ENTRY_OBJECT, ListEntry );

                 if( KeyValue != PtrHashEntryObject->KeyValue )
                     continue;

                 //
                 // I've got it!
                 //
                 FoundContext = PtrHashEntryObject->Context;

                 //
                 // Reference the hash entry object to avoid its disappearing.
                 // I do this as I don't want to call the ContextFunction while
                 // the hash line is locked, because this blocks the hash line for
                 // an unpredictable time and leads to deadlocks, I will call it 
                 // after unlocking the hash line, but I want to be sure that the
                 // PtrHashEntryObject is valid.
                 //
                 OcObReferenceObject( PtrHashEntryObject );

                 //
                 // disable purge on this entry
                 //
                 InterlockedIncrement( &PtrHashEntryObject->PurgeDisabeled );

                 //
                 // break the "for" loop
                 //
                 break;
        }//end of the "for" loop

    }//end of the lock
    OcRwReleaseReadLock( &PtrHashLine->FineGrainedRwLock, OldIrql );

    if( NULL != FoundContext ){

        if( NULL != ContextFunction )
            ContextFunction( FoundContext );

        ASSERT( PtrHashEntryObject->PurgeDisabeled > 0x0 );
        //
        // enable purge
        //
        InterlockedDecrement( &PtrHashEntryObject->PurgeDisabeled );
        ASSERT( ((LONG)PtrHashEntryObject->PurgeDisabeled) >= 0x0 );

        //
        // dereference the entry that has been referenced in the "for" loop
        //
        OcObDereferenceObject( PtrHashEntryObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrHashEntryObject );
    }

    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrHashEntryObject );

    return FoundContext;
}

//-----------------------------------------------------

VOID
NTAPI
OcHsRemoveContextByKeyValue(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN ULONG_PTR    KeyValue,
    IN FunctionForContext    ContextFunction OPTIONAL//usually OcObDereferenceObject
    )
    /*
    the caller should be aware that the ContextFunction might be called
    in the context of the thread different from the caller's thread, at
    an elevated IRQL and after this function returns - the ContextFunction is called
    when the OC_HASH_ENTRY_OBJECT is being deleted, which might not happen in this
    function - this behaviour is defined by the locking hierarchy in
    the hash manager - all Context functions are called after releasing
    the hash line lock and referencing the OC_HASH_ENTRY_OBJECT object,
    so there is a possibility of a situation when the 
    OC_HASH_ENTRY_OBJECT->Context for object removed from the list is 
    provided as a parameter for the ContextFunction called by 
    OcHsFindContextByKeyValue, this is possible because OcHsRemoveContextByKeyValue 
    might sneak in after releasing the hash line lock and before
    calling the ContextFunction, so the lifetime of OC_HASH_ENTRY_OBJECT->Context
    should be defined by the lifetime of OC_HASH_ENTRY_OBJECT and the hash
    manager client should be aware about such a possibility - he can receive
    the Context for which the has entry has been removed, but the ContextFunction
    passed to OcHsRemoveContextByKeyValue has not yet been called, so the Context
    is valid
    */
{
    ULONG            HashLineIndex;
    POC_HASH_HEAD    PtrHashLine;
    KIRQL            OldIrql;
    BOOLEAN          EntryFound = FALSE;
    POC_HASH_ENTRY_OBJECT    PtrHashEntryObject;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    HashLineIndex = PtrHashObject->KeyValueToHashIndexFunction( PtrHashObject, KeyValue );
    PtrHashLine = &PtrHashObject->ArrayOfHashHeads[ HashLineIndex ];

    OcRwAcquireLockForWrite( &PtrHashLine->FineGrainedRwLock, &OldIrql );
    {//start of the lock

        PLIST_ENTRY    request;

        for( request = PtrHashLine->ListHead.Flink; 
             request != &PtrHashLine->ListHead;
             request = request->Flink ){

                 PtrHashEntryObject = CONTAINING_RECORD( request, OC_HASH_ENTRY_OBJECT, ListEntry );

                 if( KeyValue != PtrHashEntryObject->KeyValue )
                     continue;

                 //
                 // I've got it!
                 //
                 EntryFound = TRUE;

                 //
                 // Remove the entry from the hash list.
                 //
                 OcHsRemoveEntryFromTheHashLine( PtrHashEntryObject );

                 //
                 // break the "for" loop
                 //
                 break;
        }//for

    }//end of the lock
    OcRwReleaseWriteLock( &PtrHashLine->FineGrainedRwLock, OldIrql );

    if( FALSE == EntryFound )
        return;

    if( NULL != ContextFunction ){

        ASSERT( NULL == PtrHashEntryObject->DeleteContextFunction );
        PtrHashEntryObject->DeleteContextFunction = ContextFunction;
 #if DBG
        PtrHashEntryObject->ClientIrqlForDeleteFunction = KeGetCurrentIrql();
#endif//DBG
    }

    ASSERT( OcObGetObjectReferenceCount( PtrHashObject ) >= 0x1 );
    ASSERT( OcObGetObjectReferenceCount( PtrHashEntryObject ) >= 0x1 );

    OcHsFreeRemovedHashEntryObject( PtrHashObject, PtrHashEntryObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrHashEntryObject );
}

//-----------------------------------------------------

VOID
NTAPI
OcHsTraverseAllEntriesInHash(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN FunctionForContextEx    ContextFunctionEx,
    IN PVOID    ContextEx
    )
    /*
    The caller must guarantee that no entries will be removed or inserted
    during the hash traversing!
    The ContextFunction function will be called for every found context.
    */
{
    ULONG    HashLineIndex;

    ASSERT( ContextFunctionEx );

    //
    // I must be able to wait with rescheduling in this function
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    for( HashLineIndex=0x0; 
         HashLineIndex < PtrHashObject->NumberOfHashLines;
         ++HashLineIndex ){

             POC_HASH_HEAD    PtrHashLine;
             PLIST_ENTRY      request;

             PtrHashLine = &PtrHashObject->ArrayOfHashHeads[ HashLineIndex ];
             request = &PtrHashLine->ListHead;

             while( TRUE ){

                 KIRQL            OldIrql;
                 POC_HASH_ENTRY_OBJECT    PtrHashEntryObject;

                 OcRwAcquireLockForRead( &PtrHashLine->FineGrainedRwLock, &OldIrql );
                 {//start of the lock

                     //
                     // get the next entry
                     //
                     if( IsListEmpty( request ) ){

                         //
                         // So the caller commited the breach of the contract
                         // and removed the entry or the hash line
                         // is empty, in any case I wash my hand!
                         //
                         request = &PtrHashLine->ListHead;

                     } else {

                         request = request->Flink;
                     }

                     if( request != &PtrHashLine->ListHead ){

                         PtrHashEntryObject = CONTAINING_RECORD( request, OC_HASH_ENTRY_OBJECT, ListEntry );

                         OcObReferenceObject( PtrHashEntryObject );

                         //
                         // disable purging for this entry
                         //
                         InterlockedIncrement( &PtrHashEntryObject->PurgeDisabeled );
                     }

                 }//end of the lock
                 OcRwReleaseReadLock( &PtrHashLine->FineGrainedRwLock, OldIrql );

                 if( request == &PtrHashLine->ListHead )
                     break;

                 ContextFunctionEx( PtrHashEntryObject->Context, ContextEx );

                 //
                 // enable purging for this entry
                 //
                 InterlockedDecrement( &PtrHashEntryObject->PurgeDisabeled );

                 OcObDereferenceObject( PtrHashEntryObject );

             }//while
    }//for

}

//-----------------------------------------------------
