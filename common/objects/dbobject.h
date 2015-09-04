#ifndef _DBOBJECT_H_
#define _DBOBJECT_H_

#include <ntddk.h>

#if DBG
#define DebugPrint(_x_) \
    DbgPrint("[mtest-001]"); \
    DbgPrint _x_;
#else
#define DebugPrint(_x_)
#endif

#define CR_MEM_TAG  '30mM'

#ifdef DBG
#define CrObReferenceObjectWithTag( Tag ) \
    CrReferenceObjectWithTag(__FILE__,__LINE__,Tag);    
#else
#define CrObReferenceObjectWithTag( Tag )  
#endif

#define PRINT_TAG(tag) \
	((UCHAR *)&(tag))[0], ((UCHAR *)&(tag))[1], ((UCHAR *)&(tag))[2], ((UCHAR *)&(tag))[3]

/*
typedef struct _CR_OBJECT_LIST{

    ULONG   RefCount;
    ULONG   TypeObject; // type of objects
    
    KSPIN_LOCK    SpinLock;
    KIRQL         OldIrql;    
    LIST_ENTRY    ListHead;
    NPAGED_LOOKASIDE_LIST    LookasideList;

} CR_OBJECT_LIST, *PCR_OBJECT_LIST;
*/


typedef struct _CR_OBJECT_INFO{
    ULONG       RefCount;
    ULONG       Tag;

    LIST_ENTRY    ListEntry;
    
    LIST_ENTRY    ListHead;
    NPAGED_LOOKASIDE_LIST    LookasideList;

    PVOID    PtrObject;   

} CR_OBJECT_INFO, *PCR_OBJECT_INFO;


typedef struct _CR_OBJECT_REF{
    
    LIST_ENTRY    ListEntry;

    PUCHAR  PtrFileName;
    ULONG   NumberLine;  
} CR_OBJECT_REF, *PCR_OBJECT_REF;


VOID
CrInitializeObjectList();


VOID
CrObDereferenceObjectWithTag(
    __in ULONG Tag
        );

NTSTATUS
CrReferenceObjectWithTag(
                __in PUCHAR PtrFileName,
                __in ULONG  NumberLine,
                __in ULONG Tag
                   );


VOID
CrPrintObjectList();

VOID
CrDeleteObjectList();

#endif