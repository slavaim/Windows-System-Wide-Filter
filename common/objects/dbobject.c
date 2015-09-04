#include "dbobject.h"



ULONG   g_CrRefCount;
KSPIN_LOCK    g_CrSpinLock;
LIST_ENTRY    g_CrListHead;
NPAGED_LOOKASIDE_LIST    g_CrLookasideList;


//----------------------------------------------------

VOID
CrInitializeObjectList()
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    
    DebugPrint(("->CrInitializeObjectList\n"));
    
    InitializeListHead(&g_CrListHead);
    KeInitializeSpinLock(&g_CrSpinLock);
      
    ExInitializeNPagedLookasideList(&g_CrLookasideList,
                                        NULL,
                                        NULL,
                                        0x0,
                                        sizeof(CR_OBJECT_INFO),
                                        '10rC',
                                        0x0 );
    
}


//----------------------------------------------------
VOID
CrObDereferenceObjectWithTag(    
    __in ULONG Tag
        )
{
    PCR_OBJECT_INFO     PtrObjectInfo;
    PCR_OBJECT_REF      PtrObjectRef;
    PLIST_ENTRY         ListObjReq,ListObjRefReq;
    BOOLEAN     Found = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&g_CrSpinLock, &OldIrql );

    for( ListObjReq = g_CrListHead.Flink;
        ListObjReq != &g_CrListHead;
        ListObjReq = ListObjReq->Flink )
    {
        PtrObjectInfo = CONTAINING_RECORD(ListObjReq,
            CR_OBJECT_INFO,
            ListEntry);

        if (PtrObjectInfo->Tag != Tag)
            continue;
        
                    
        PtrObjectRef = CONTAINING_RECORD(PtrObjectInfo->ListHead.Flink,
            CR_OBJECT_REF,
            ListEntry);
        
        Found = TRUE;
        break;
        
    }
    if (Found == TRUE) {
        DebugPrint(("->CrObDereferenceObjectWithTag: RefCount[%d] Tag[%c%c%c%c]\n",
                PtrObjectInfo->RefCount,PRINT_TAG(PtrObjectInfo->Tag)));
        ExFreePool(PtrObjectRef->PtrFileName);
        RemoveEntryList(&PtrObjectRef->ListEntry);
        InterlockedDecrement(&PtrObjectInfo->RefCount);

        if(InterlockedCompareExchange(&PtrObjectInfo->RefCount,0x0, 0x0)==0x0)
        {
            RemoveEntryList(&PtrObjectInfo->ListEntry);
            InterlockedDecrement(&g_CrRefCount);
        }
    }


    

    KeReleaseSpinLock(&g_CrSpinLock,OldIrql);
}



//----------------------------------------------------


VOID
CrPrintObjectList()                     
{        
    PCR_OBJECT_INFO     PtrObjectInfo;
    PCR_OBJECT_REF      PtrObjectRef;
    PLIST_ENTRY         ListObjReq,ListObjRefReq;
    
    
    DebugPrint(("\n%%%-------------------------------------\n",g_CrRefCount));
    DebugPrint(("->CrPrintObjectList: RefCount[%d]\n",g_CrRefCount));

    for( ListObjReq = g_CrListHead.Flink;
        ListObjReq != &g_CrListHead;
        ListObjReq = ListObjReq->Flink )
    {
        PtrObjectInfo = CONTAINING_RECORD(ListObjReq,
            CR_OBJECT_INFO,
            ListEntry);

        DebugPrint(("->CrPrintObjectList: Tag[%c%c%c%c] RefCount[%d]\n",
                PRINT_TAG(PtrObjectInfo->Tag),PtrObjectInfo->RefCount));
        
        for( ListObjRefReq = PtrObjectInfo->ListHead.Flink;
            ListObjRefReq != &PtrObjectInfo->ListHead;
            ListObjRefReq = ListObjRefReq->Flink )
        {
            PtrObjectRef = CONTAINING_RECORD(ListObjRefReq,
                CR_OBJECT_REF,
                ListEntry);
            
            DebugPrint(("->CrPrintObjectList: File[%s] Line[%d]\n",
                PtrObjectRef->PtrFileName,PtrObjectRef->NumberLine));

            
        }        
    }    
}

//----------------------------------------------------


VOID
CrDeleteObjectList()                     
{        
    PCR_OBJECT_INFO     PtrObjectInfo;
    PCR_OBJECT_REF      PtrObjectRef;
    PLIST_ENTRY         ListObjReq,ListObjRefReq;
    KIRQL   OldIrql;
/*
    if(InterlockedCompareExchange(&g_CrRefCount,0x0, 0x0)!=0x0)
    {
        DebugPrint(("->CrDeleteObjectList:[ERROR] InterlockedCompareExchange\n"));
        return;
    }
*/
    DebugPrint(("->CrDeleteObjectList:\n"));

    KeAcquireSpinLock(&g_CrSpinLock, &OldIrql );

    for( ListObjReq = g_CrListHead.Flink;
        ListObjReq != &g_CrListHead;
        ListObjReq = ListObjReq->Flink )
    {
        PtrObjectInfo = CONTAINING_RECORD(ListObjReq,
            CR_OBJECT_INFO,
            ListEntry);

        
        for( ListObjRefReq = PtrObjectInfo->ListHead.Flink;
            ListObjRefReq != &PtrObjectInfo->ListHead;
            ListObjRefReq = ListObjRefReq->Flink )
        {
            PtrObjectRef = CONTAINING_RECORD(ListObjRefReq,
                CR_OBJECT_REF,
                ListEntry);

            ExFreePool(PtrObjectRef->PtrFileName);
        }
        ExDeleteNPagedLookasideList(&PtrObjectInfo->LookasideList); 
    }
    
    ExDeleteNPagedLookasideList(&g_CrLookasideList); 

    KeReleaseSpinLock(&g_CrSpinLock,OldIrql);  

    DebugPrint(("<-CrDeleteObjectList:\n")); 
}



//----------------------------------------------------

NTSTATUS
CrReferenceObjectWithTag(
                __in PUCHAR PtrFileName,
                __in ULONG  NumberLine,
                __in ULONG Tag
                   )                   
{
    PCR_OBJECT_INFO     PtrObjectInfo;
    PCR_OBJECT_REF  PtrObjectRef;
    PLIST_ENTRY         request;
    BOOLEAN     Found = FALSE;
    KIRQL OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );


    KeAcquireSpinLock(&g_CrSpinLock, &OldIrql );

    for( request = g_CrListHead.Flink;
        request != &g_CrListHead;
        request = request->Flink )
    {
            PtrObjectInfo = CONTAINING_RECORD(request,
                                CR_OBJECT_INFO,
                                ListEntry);

            if(PtrObjectInfo->Tag!=Tag)
                continue;

            Found = TRUE;
            break;         

    }

    if (!Found) {
        PtrObjectInfo = (PCR_OBJECT_INFO )ExAllocateFromNPagedLookasideList(&g_CrLookasideList);
        if( NULL == PtrObjectInfo )
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory( PtrObjectInfo, sizeof( *PtrObjectInfo ) );        
        InitializeListHead(&PtrObjectInfo->ListHead);        

        PtrObjectInfo->Tag = Tag;

        ExInitializeNPagedLookasideList(&PtrObjectInfo->LookasideList,
            NULL,
            NULL,
            0x0,
            sizeof(CR_OBJECT_INFO),
            '10rC',
            0x0 );
    

        InsertTailList(&g_CrListHead, &PtrObjectInfo->ListEntry );
        InterlockedIncrement(&g_CrRefCount);
    }

    
    PtrObjectRef = (PCR_OBJECT_REF)ExAllocateFromNPagedLookasideList(&PtrObjectInfo->LookasideList);
    if( NULL == PtrObjectRef)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(PtrObjectRef, sizeof( *PtrObjectRef));        
    PtrObjectRef->PtrFileName = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,strlen(PtrFileName)+1,CR_MEM_TAG);
    if (PtrObjectRef->PtrFileName==NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory( PtrObjectRef->PtrFileName, strlen(PtrFileName)+1);
    RtlCopyMemory(PtrObjectRef->PtrFileName,PtrFileName,strlen(PtrFileName));    
    PtrObjectRef->NumberLine = NumberLine;
    
    InsertTailList(&PtrObjectInfo->ListHead, &PtrObjectRef->ListEntry );

    InterlockedIncrement(&PtrObjectInfo->RefCount);
    
        
    KeReleaseSpinLock(&g_CrSpinLock,OldIrql);   


    return STATUS_SUCCESS;
}

