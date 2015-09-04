/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
24.11.2006 
 Start
*/

//
// use Win2k compatible lists
//
#ifndef _WIN2K_COMPAT_SLIST_USAGE
#define _WIN2K_COMPAT_SLIST_USAGE 1
#endif

#include <ntddk.h>

#if !defined(_OC_OBJECT_H_)
#define _OC_OBJECT_H_

//----------------------------------------------------

#define OcSetFlag( Flag, Value )    ( (VOID) ((Flag) |= (Value)) )
#define OcClearFlag( Flag, Value )  ( (VOID) ((Flag) &= ~(Value)) )
#define OcIsFlagOn( Flag, Value )   ( (BOOLEAN) (((Flag)&(Value)) != 0x0) )

#if DBG
#define OC_INVALID_POINTER_VALUE  ((ULONG_PTR)0x1)
#define OC_IS_POINTER_VALID( _Ptr_ )  ( OC_INVALID_POINTER_VALUE != ((ULONG_PTR)(_Ptr_)) && NULL != (PVOID)(_Ptr_) )
#define OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( _Ptr_ )  ( *(PULONG_PTR)&(_Ptr_) = OC_INVALID_POINTER_VALUE );
#else//DBG
#define OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( _Ptr_ ) 
#endif//DBG

#define OC_STATIC_ARRAY_SIZE( Array ) ( sizeof( Array )/sizeof( Array[ 0x0 ] ) )

#define OC_IS_BOOLEAN( _BoooleanValue ) ( TRUE == (_BoooleanValue) || FALSE == (_BoooleanValue) )

//----------------------------------------------------

typedef PVOID  POC_OBJECT_BODY;
struct _OC_OBJECT_TYPE;

//----------------------------------------------------

typedef 
VOID
( NTAPI *OcObDeleteObjectMethod )(
    IN POC_OBJECT_BODY    ObjectBody
    );

typedef 
VOID 
( NTAPI *OcObDeleteObjectTypeMethod )(
    IN struct _OC_OBJECT_TYPE  *ObjectType
    );

//----------------------------------------------------

typedef struct _OC_OBJECT_METHODS{

    //
    // the delete method is called when
    // the object's refrence count drops
    // to zero
    //
    OcObDeleteObjectMethod    DeleteObject OPTIONAL;
 
    //
    // the following method is called after 
    // uninitialization of object type, so
    // the callee can for example
    // free the memory allocated for the
    // object type
    //
    OcObDeleteObjectTypeMethod    DeleteObjectType OPTIONAL;

} OC_OBJECT_METHODS, *POC_OBJECT_METHODS;

//----------------------------------------------------

typedef enum {

    OcObjectTypeZeroFlag = 0x0,

    //
    // If the following flag is set then the object type
    // will be deleted when its reference count drops
    // to zero. Also, you may set this flag when intializing
    // the object type, then only one object of
    // this type might be created and after deleting of that
    // object the object type will also be deleted.
    //
    OcObjectTypeMarkedForDeletion = 0x1,

    // 
    // If the following flag is set then the standard pool
    // allocator is used for objects, useful when you know that
    // the number of objects will be small and don't want to 
    // waste space for lookaside lists.
    //
    OcObjectTypeUseStdPoolAllocator = 0x2,

    //
    // The following flags orders to insert all objects in the 
    // object type's list.
    //
    OcObjectTypeObjectsInList = 0x4,
    
    //
    // The object's body memory will be zeroed by 
    // the Object Manager after allocation, 
    // so the caller doesn't have to zero the object's
    // body
    //
    OcObjectTypeZeroObjectBody = 0x8,

    OcObjectTypeAllFlags = 0xFFFFFFFF
} OC_OBJECT_TYPE_FLAGS;

//----------------------------------------------------

typedef enum {
    OcTypeObjectType = 0xABCD0000,
    OcTypeObject = 0xABCD1234
} OC_TYPE;

typedef struct _OC_OBCOMMON_HEADER{
    OC_TYPE    Type;
    ULONG      Size;
    ULONG      AdditionalBufferSize;
} OC_OBCOMMON_HEADER, *POC_OBCOMMON_HEADER;

//----------------------------------------------------

typedef struct _OC_OBJECT_TYPE{

#ifdef DBG
    OC_OBCOMMON_HEADER    CommonHeader;
#endif//DBG

    //
    // object type's reference count
    // is incremented when new object
    // of this type is created
    //
    ULONG   RefCount;

    //
    // Flags
    //
    OC_OBJECT_TYPE_FLAGS    Flags;

    //
    // the spin lock to protect the 
    // fileds of this structure and
    // the list of objects
    //
    KSPIN_LOCK    SpinLock;
    KIRQL         OldIrql;

    //
    // all objects are linked in a double
    // linked list
    //
    LIST_ENTRY    ListHead;

    //
    // object's methods
    //
    OC_OBJECT_METHODS    Methods;

    //
    // the llokaside list from which the objects are allocated
    //
    NPAGED_LOOKASIDE_LIST    LookasideList;

    //
    // this pointer is used to save the user's context
    // which may be used for example during an 
    // object type removing
    //
    PVOID    Context;

    //
    // size of the object's body
    //
    ULONG    ObjectsBodySize;

    //
    // Tag for the lookaside list
    //
    ULONG    Tag;

} OC_OBJECT_TYPE, *POC_OBJECT_TYPE;

//----------------------------------------------------

typedef struct _OC_OBJECT_TYPE_INITIALIZER{

    OC_OBJECT_TYPE_FLAGS    Flags;
    ULONG                   Tag;
    ULONG                   ObjectsBodySize;
    OC_OBJECT_METHODS       Methods;
    //
    // this context is passed as a parameter
    // through the OC_OBJECT_TYPE.Context field
    //
    //PVOID                   Context;

} OC_OBJECT_TYPE_INITIALIZER, *POC_OBJECT_TYPE_INITIALIZER;

#define  OC_OBJECT_TYPE_INITIALIZER_VAR( _Name_ )  OC_OBJECT_TYPE_INITIALIZER  _Name_={0x0}
#define  OC_TOGGLE_TYPE_INITIALIZER( PtrTypeInitializer ) \
        RtlZeroMemory( PtrTypeInitializer, sizeof( OC_OBJECT_TYPE_INITIALIZER ) );

/*
__forceinline
PVOID
OcObGetObjectTypeUserContext(
    IN POC_OBJECT_TYPE    ObjectType
    )
{
    return ObjectType->Context;
}
*/

//----------------------------------------------------

typedef struct _OC_OBJECT_HEADER{

#ifdef DBG
    OC_OBCOMMON_HEADER    CommonHeader;
    ULONG                 AdditionalBufferSize;
#endif//DBG

    //
    // when reference count becomes zero
    // the object is deleted
    //
    ULONG    RefCount;

    //
    // list entry for the list of objects
    //
    LIST_ENTRY    ListEntry;

    POC_OBJECT_TYPE    ObjectType;

    //
    // Body
    //
    //ULONG_PTR    Body;

} OC_OBJECT_HEADER, *POC_OBJECT_HEADER;

//----------------------------------------------------

extern
NTSTATUS
OcObInitializeObjectManager(
    IN PVOID Context
    );

extern
VOID
OcObUninitializeObjectManager(
    IN PVOID Context
    );

extern
BOOLEAN
OcObIsObjectManagerInitialized();

extern
VOID
OcObInitializeObjectType(
    IN POC_OBJECT_TYPE_INITIALIZER    PtrObjectTypeInitializer,
    IN OUT POC_OBJECT_TYPE    PtrObjectType
    );

extern
VOID
OcObDeleteObjectType(
    IN POC_OBJECT_TYPE    PtrObjectType
    );

extern
NTSTATUS
OcObCreateObjectEx(
    IN POC_OBJECT_TYPE      PtrObjectType,
    IN ULONG                AdditionalBufferSize,
    OUT POC_OBJECT_BODY*    PtrPtrObjectBody
    );

extern
NTSTATUS
OcObCreateObject(
    IN POC_OBJECT_TYPE    PtrObjectType,
    OUT POC_OBJECT_BODY*    ObjectBody
    );

extern
VOID
OcObReferenceObject(
    IN POC_OBJECT_BODY    ObjectBody
    );

extern
VOID
OcObDereferenceObject(
    IN POC_OBJECT_BODY    ObjectBody
    );

#if DBG
extern
ULONG
NTAPI
OcObGetObjectReferenceCount(
    IN POC_OBJECT_BODY    ObjectBody
    );
#endif//DBG

//----------------------------------------------------

#endif//_OC_OBJECT_H_
