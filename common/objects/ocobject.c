/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
24.11.2006 
 Start
*/

#include <ocobject.h>

static BOOLEAN    g_OcObManagerInitialized = FALSE;

//----------------------------------------------------

static
VOID
OcObDeleteObject(
    IN POC_OBJECT_HEADER    ObjectHeader
    );

//----------------------------------------------------

#define OCOB_HEADER_TO_BODY(_ObjectHeader_)  ((POC_OBJECT_BODY)((POC_OBJECT_HEADER)_ObjectHeader_ + 0x1))
#define OCOB_BODY_TO_HEADER(_ObjectBody_)    ((POC_OBJECT_HEADER)_ObjectBody_ - 0x1)

__forceinline
POC_OBJECT_HEADER
OcObObjectBodyToHeader(
    IN POC_OBJECT_BODY    ObjectBody
    )
    /*
    Internal function!
    */
{
#if DBG

    POC_OBJECT_HEADER   ObjectHeader;
    ObjectHeader = OCOB_BODY_TO_HEADER( ObjectBody );
    ASSERT( OcTypeObject == ObjectHeader->CommonHeader.Type );

    return ObjectHeader;

#else//DBG

    return OCOB_BODY_TO_HEADER( ObjectBody );

#endif//DBG
}

//----------------------------------------------------

__forceinline
VOID
OcObLockObjectType(
    IN POC_OBJECT_TYPE    PtrObjectType
    )
    /*
    Internal function!
    */
{
    KIRQL    OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_IS_POINTER_VALID( PtrObjectType ) );

    KeAcquireSpinLock( &PtrObjectType->SpinLock, &OldIrql );
    PtrObjectType->OldIrql = OldIrql;
}

//----------------------------------------------------

__forceinline
VOID
OcObUnlockObjectType(
    IN POC_OBJECT_TYPE    PtrObjectType
    )
    /*
    Internal function!
    */
{
#if DBG
    KIRQL    OldIrql;
#endif//DBG
    ASSERT( PtrObjectType->OldIrql <= KeGetCurrentIrql() );
    ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

#if DBG
    OldIrql = PtrObjectType->OldIrql;
    PtrObjectType->OldIrql = DISPATCH_LEVEL + 0x1;
    KeReleaseSpinLock( &PtrObjectType->SpinLock, OldIrql );
#else
    KeReleaseSpinLock( &PtrObjectType->SpinLock, PtrObjectType->OldIrql );
#endif//DBG

}

//----------------------------------------------------

VOID
OcObInitializeObjectType(
    IN POC_OBJECT_TYPE_INITIALIZER    PtrObjectTypeInitializer,
    IN OUT POC_OBJECT_TYPE    PtrObjectType
    )
    /*
    The caller must allocate the room for the object type 
    from the nonpaged pool
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( 0x0 != PtrObjectTypeInitializer->ObjectsBodySize );
    ASSERT( 0x0 == ( PtrObjectTypeInitializer->Flags & OcObjectTypeMarkedForDeletion ) );
    ASSERT( TRUE == g_OcObManagerInitialized );

    RtlZeroMemory( PtrObjectType, sizeof( *PtrObjectType ) );

    InitializeListHead( &PtrObjectType->ListHead );
    KeInitializeSpinLock( &PtrObjectType->SpinLock );
    PtrObjectType->ObjectsBodySize = PtrObjectTypeInitializer->ObjectsBodySize;
    PtrObjectType->Methods = PtrObjectTypeInitializer->Methods;
    PtrObjectType->Tag = PtrObjectTypeInitializer->Tag;
    //PtrObjectType->Context = PtrObjectTypeInitializer->Context;

    //
    // instead OcObjectTypeMarkedForDeletion flag the clients must use 
    // the OcObDeleteObjectType function
    //
    PtrObjectType->Flags = PtrObjectTypeInitializer->Flags & ~(OcObjectTypeMarkedForDeletion);

    if( !OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeUseStdPoolAllocator ) ){

        ExInitializeNPagedLookasideList( &PtrObjectType->LookasideList,
                                         NULL,
                                         NULL,
                                         0x0,
                                         PtrObjectType->ObjectsBodySize + sizeof( OC_OBJECT_HEADER ),
                                         PtrObjectType->Tag,
                                         0x0 );
    }

#if DBG
    PtrObjectType->OldIrql = DISPATCH_LEVEL + 0x1;
    PtrObjectType->CommonHeader.Type = OcTypeObjectType;
#endif//DBG
}

//----------------------------------------------------

VOID
OcObDeleteObjectType(
    IN POC_OBJECT_TYPE    PtrObjectType
    )
    /*
    The function checks the object type's reference count
    and deletes the object type if the reference count is zero,
    else the object type is marked and will be deleted when
    the refrence count drops to zero
    */
{

    OcObDeleteObjectTypeMethod    DeleteObjectTypeMethod;

    ASSERT( OcTypeObjectType == PtrObjectType->CommonHeader.Type );

    if( 0x0 != InterlockedCompareExchange( &PtrObjectType->RefCount, 0x0, 0x0 ) ){

        OcSetFlag( PtrObjectType->Flags, OcObjectTypeMarkedForDeletion );
        return;
    }

    if( !OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeUseStdPoolAllocator ) )
        ExDeleteNPagedLookasideList( &PtrObjectType->LookasideList );

    //
    // get the pointer to the notify routine
    //
    DeleteObjectTypeMethod = PtrObjectType->Methods.DeleteObjectType;

#if DBG
    RtlFillMemory( PtrObjectType, sizeof( *PtrObjectType ), 0x0C );
#endif//DBG

    //
    // notify about the object type uninitialization
    //
    if( NULL != DeleteObjectTypeMethod ){

        DeleteObjectTypeMethod( PtrObjectType );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrObjectType );
    }
}

//----------------------------------------------------

NTSTATUS
OcObCreateObjectEx(
    IN POC_OBJECT_TYPE      PtrObjectType,
    IN ULONG                AdditionalBufferSize,
    OUT POC_OBJECT_BODY*    PtrPtrObjectBody
    )
    /*
    This function allocates memory for the object's body
    and header and initialized the object's header.
    The object's body is not initialized or zeroed in this
    function, this is a caller's responsibility!
    The AdditionalBuffer parameter is used to allocate the buffer
    at the end of the object, if AdditionalBuffer is not 0x0 then the
    object must have OcObjectTypeUseStdPoolAllocator set.
    */
{
    POC_OBJECT_HEADER    PtrObjectHeader;
    POC_OBJECT_BODY      PtrObjectBody;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( TRUE == OcObIsObjectManagerInitialized() );

    //
    // a variable sized object can't be allocated from a look-aside list
    //
    if( 0x0 != AdditionalBufferSize && 
        !OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeUseStdPoolAllocator ) )
        return STATUS_INVALID_PARAMETER_2;

#ifndef USE_STD_ALLOCATOR
    if( !OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeUseStdPoolAllocator ) ){

        //ASSERT( 0x0 == AdditionalBuffer );

        PtrObjectHeader = ( POC_OBJECT_HEADER )ExAllocateFromNPagedLookasideList( &PtrObjectType->LookasideList );

    } else {
#endif//USE_STD_ALLOCATOR
        PtrObjectHeader = ( POC_OBJECT_HEADER )ExAllocatePoolWithTag( 
                                                 NonPagedPool, 
                                                 PtrObjectType->ObjectsBodySize + sizeof( OC_OBJECT_HEADER ) + AdditionalBufferSize,
                                                 PtrObjectType->Tag 
                                                 );
#ifndef USE_STD_ALLOCATOR
    }
#endif//USE_STD_ALLOCATOR
    if( NULL == PtrObjectHeader )
        return STATUS_INSUFFICIENT_RESOURCES;

    //
    // initialize object's header
    //
    RtlZeroMemory( PtrObjectHeader, sizeof( *PtrObjectHeader ) );
    PtrObjectHeader->RefCount = 0x1;
    PtrObjectHeader->ObjectType = PtrObjectType;

#if DBG
    PtrObjectHeader->CommonHeader.Type = OcTypeObject;
    PtrObjectHeader->CommonHeader.AdditionalBufferSize = AdditionalBufferSize;
#endif//DBG

    //
    // increment the object type's refrence count
    // because the new object of this type
    // has been created
    //
    InterlockedIncrement( &PtrObjectType->RefCount );

    //
    // insert the new object into object type's list
    //
    if( OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeObjectsInList ) ){

        OcObLockObjectType( PtrObjectType );
        {
            InsertTailList( &PtrObjectType->ListHead, &PtrObjectHeader->ListEntry );
        }
        OcObUnlockObjectType( PtrObjectType );
    }
    
    //
    // zero the object's body
    //
    if( OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeZeroObjectBody ) ){

        RtlZeroMemory(  OCOB_HEADER_TO_BODY( PtrObjectHeader ), PtrObjectType->ObjectsBodySize + AdditionalBufferSize );
    }

    //
    // get object's body
    //
    PtrObjectBody = OCOB_HEADER_TO_BODY( PtrObjectHeader );
    *PtrPtrObjectBody = PtrObjectBody;

    return STATUS_SUCCESS;
}

//----------------------------------------------------

NTSTATUS
OcObCreateObject(
    IN POC_OBJECT_TYPE      PtrObjectType,
    OUT POC_OBJECT_BODY*    PtrPtrObjectBody
    )
    /*
    This function allocates memory for the object's body
    and header and initialized the object's header.
    The object's body is not initialized or zeroed in this
    function, this is a caller's responsibility!
    */
{
    return OcObCreateObjectEx( PtrObjectType, 0x0, PtrPtrObjectBody );
}

//----------------------------------------------------

static
VOID
OcObDeleteObject(
    IN POC_OBJECT_HEADER    PtrObjectHeader
    )
    /*
    Internal function! 
    Deinitializes the object and frees object's memory.
    */
{
    POC_OBJECT_TYPE      PtrObjectType;

    //
    // the object manager can work at DISPATCH_LEVEL
    // but if the object's deletion method
    // can't be called at DISPATCH_LEVEL then
    // the OcObDereferenceObject must be called
    // at lower IRQL defined by the object's
    // deletion method
    //
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // delete the object
    //
    ASSERT( 0x0 == PtrObjectHeader->RefCount );
    ASSERT( OcTypeObject == PtrObjectHeader->CommonHeader.Type );

    PtrObjectType = PtrObjectHeader->ObjectType;
    ASSERT( OcTypeObjectType == PtrObjectType->CommonHeader.Type );

    //
    // TO DO - send in a worker thread if the IRQL > PASSIVE level
    //

    //
    // call the delete method, if it exists
    //
    if( PtrObjectType->Methods.DeleteObject ){

        PtrObjectType->Methods.DeleteObject( OCOB_HEADER_TO_BODY( PtrObjectHeader ) );
    }//if( PtrObjectType->Methods.DeleteObject )

    //
    // remove the object from the object type's list
    //
    if( OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeObjectsInList ) ){

        OcObLockObjectType( PtrObjectType );
        {
            RemoveEntryList( &PtrObjectHeader->ListEntry );
        }
        OcObUnlockObjectType( PtrObjectType );
    }

    //
    // free the memory allocated for the object
    //
#ifndef USE_STD_ALLOCATOR
    if( !OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeUseStdPoolAllocator ) ){
        ExFreeToNPagedLookasideList( &PtrObjectType->LookasideList, (PVOID)PtrObjectHeader );
    } else {
#endif//USE_STD_ALLOCATOR
       ExFreePoolWithTag( (PVOID)PtrObjectHeader, PtrObjectType->Tag );
#ifndef USE_STD_ALLOCATOR
    }
#endif//USE_STD_ALLOCATOR
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrObjectHeader );

    //
    // decrement the object type's reference count,
    //
    if( 0x0 == InterlockedDecrement( &PtrObjectType->RefCount ) && 
        OcIsFlagOn( PtrObjectType->Flags, OcObjectTypeMarkedForDeletion ) ){

        //
        // delete the object's type if it has been martked for deletion
        //
        OcObDeleteObjectType( PtrObjectType );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrObjectType );
    }//if( OcIsFlagOn( PtrObjectType->Flags, OcObjectMarkedForDeletion ) )
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrObjectType );

}

//----------------------------------------------------

VOID
OcObDereferenceObject(
    IN POC_OBJECT_BODY    ObjectBody
    )
{
    POC_OBJECT_HEADER    PtrObjectHeader;

    //
    // the object manager can work at DISPATCH_LEVEL
    // but if the object's deletion method
    // can't be called at DISPATCH_LEVEL then
    // the OcObDereferenceObject must be called
    // at lower IRQL defined by the object's
    // deletion method
    //
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_IS_POINTER_VALID( ObjectBody ) );

    //
    // convert the object's body to object's header
    //
    PtrObjectHeader = OcObObjectBodyToHeader( ObjectBody );

    ASSERT( PtrObjectHeader->RefCount > 0x0 );

    if( 0x0 != InterlockedDecrement( &PtrObjectHeader->RefCount ) )
        return;

    //
    // the refernce count drops to zero, so nobody 
    // references this object and it must be deleted
    //
    OcObDeleteObject( PtrObjectHeader );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrObjectHeader );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ObjectBody );
}

//----------------------------------------------------

VOID
OcObReferenceObject(
    IN POC_OBJECT_BODY    ObjectBody
    )
{
    POC_OBJECT_HEADER    PtrObjectHeader;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_IS_POINTER_VALID( ObjectBody ) );

    //
    // convert the object's body to object's header
    //
    PtrObjectHeader = OcObObjectBodyToHeader( ObjectBody );

    ASSERT( PtrObjectHeader->RefCount > 0x0 );

    InterlockedIncrement( &PtrObjectHeader->RefCount );
}

//----------------------------------------------------

NTSTATUS
OcObInitializeObjectManager(
    IN PVOID Context
    )
{
    ASSERT( FALSE == g_OcObManagerInitialized );

    g_OcObManagerInitialized = TRUE;
    return STATUS_SUCCESS;
}

//----------------------------------------------------

VOID
OcObUninitializeObjectManager(
    IN PVOID Context
    )
{
    if( FALSE == g_OcObManagerInitialized )
        return;

    g_OcObManagerInitialized = FALSE;
    return;
}

//----------------------------------------------------

BOOLEAN
OcObIsObjectManagerInitialized()
{
    return g_OcObManagerInitialized;
}

//----------------------------------------------------

#if DBG

ULONG
NTAPI
OcObGetObjectReferenceCount(
    IN POC_OBJECT_BODY    ObjectBody
    )
    /*
    this is totaly unsafe function and must be used only for debug purposes
    */
{
    POC_OBJECT_HEADER    PtrObjectHeader;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // convert the object's body to object's header
    //
    PtrObjectHeader = OcObObjectBodyToHeader( ObjectBody );

    return PtrObjectHeader->RefCount;
}

#endif//DBG

//----------------------------------------------------

