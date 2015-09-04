/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
01.12.2006 ( December ) 
 Start
*/

#if !defined(_OC_HASH_H_)
#define _OC_HASH_H_

#include <ocrwspinlock.h>
#include <ocobject.h>

//-----------------------------------------------------

#define OC_HASH_BUG_CODES_BASE                 ( 0xAB100000 )
#define OC_HASH_OBJ_MANAGER_NOT_INITIALIZED    ( OC_HASH_BUG_CODES_BASE + 0x1 )
#define OC_HASH_DELETED_ENTRY_STILL_IN_HASH    ( OC_HASH_BUG_CODES_BASE + 0x2 )
#define OC_HASH_HASH_MANAGER_NOT_INITIALIZED   ( OC_HASH_BUG_CODES_BASE + 0x3 )
#define OC_HASH_CONTEXT_IS_NULL                ( OC_HASH_BUG_CODES_BASE + 0x4 )

//-----------------------------------------------------

struct   _OC_HASH_OBJECT;

typedef 
ULONG 
( NTAPI *HashKeyFunction )( 
    IN struct _OC_HASH_OBJECT*    PtrHashObject,
    IN ULONG_PTR    KeyValue
    );

//
// the FunctionForContext type functions
// are called before inserting, deleting or returning the context, 
// if needed the call is made atomically
// at DISPATCH_LEVEL, no any hash 
// function can be called from the FunctionForContext
// type function, because this function might be
// called with the locked hash
//
typedef
VOID
( NTAPI *FunctionForContext )(
    IN PVOID    Context
    );

//-----------------------------------------------------

typedef
VOID
( NTAPI *FunctionForContextEx )(
    IN PVOID    Context,
    IN PVOID    ContextEx
    );

//-----------------------------------------------------

//
// not an object!
//
typedef struct _OC_HASH_HEAD{

    //
    // head of the list connecting entries with equal hash keys
    //
    LIST_ENTRY    ListHead;

    //
    // this lock is used to protect the list
    //
    OC_RW_SPIN_LOCK    FineGrainedRwLock;

} OC_HASH_HEAD, *POC_HASH_HEAD;

//-----------------------------------------------------

typedef enum{
    OcHashHeadsArrayFromPool = 0x1,
    OcHashAllFlags = 0xFFFFFFFF
} OcHsHashObjectFlags;

//
// object with object header
//
typedef struct _OC_HASH_OBJECT{

    OcHsHashObjectFlags    Flags;

    //
    // function which maps values to keys
    //
    HashKeyFunction    KeyValueToHashIndexFunction;

    //
    // number of entries in the ArrayOfHashHeads array
    //
    ULONG    NumberOfHashLines;

    //
    // pointer to the array of hash heads
    //
    POC_HASH_HEAD    ArrayOfHashHeads;

} OC_HASH_OBJECT, *POC_HASH_OBJECT;

//-----------------------------------------------------

extern
NTSTATUS
NTAPI
OcHsInitializeHashManager(
    IN PVOID   Context
    );

extern
VOID
NTAPI
OcHsUninitializeHashManager(
    IN PVOID   Context
    );

extern
NTSTATUS
NTAPI
OcHsCreateHash(
    IN ULONG    NumberOfHashLines,
    IN HashKeyFunction    FuncValueToHashLineIndex OPTIONAL,//actually not used
    OUT POC_HASH_OBJECT*    PtrPtrHashObj
    );

extern
NTSTATUS
NTAPI
OcHsInsertContextInHash(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN ULONG_PTR    KeyValue,
    IN PVOID    Context,
    IN FunctionForContext    ContextFunction OPTIONAL//usually OcObReferenceObject
    );

extern
VOID
NTAPI
OcHsRemoveContextByKeyValue(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN ULONG_PTR    KeyValue,
    IN FunctionForContext    ContextFunction OPTIONAL//usually OcObDereferenceObject
    );

extern
PVOID
NTAPI
OcHsFindContextByKeyValue(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN ULONG_PTR    KeyValue,
    IN FunctionForContext    ContextFunction OPTIONAL//usually OcObReferenceObject
    );

extern
VOID
NTAPI
OcHsPurgeAllEntriesFromHash(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN FunctionForContext    ContextFunction OPTIONAL//usually OcObDereferenceObject
    );

extern
VOID
NTAPI
OcHsTraverseAllEntriesInHash(
    IN POC_HASH_OBJECT    PtrHashObject,
    IN FunctionForContextEx    ContextFunctionEx,
    IN PVOID    ContextEx
    );

extern
ULONG 
NTAPI
OcHsUniversalHashKeyFunction( 
    IN struct _OC_HASH_OBJECT*    PtrHashObject,
    IN ULONG_PTR    KeyValue
    );

#if DBG

VOID
NTAPI
OcTestHashManager();

#endif//DBG

//-----------------------------------------------------

#endif//_OC_HASH_H_