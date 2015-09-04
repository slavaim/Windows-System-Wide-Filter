/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
12.12.2006 ( December )
 Start
*/

#include "struct.h"
#include "proto.h"

//
// this file contains the Irps tracker code.
// The Irps tracker allows to know whether the Irp 
// has been processed at some upper level, for quick 
// processing the Irp tracker uses the Parameters.Others 
// field in the stack location of the filter device to
// save the OC_CORE_IRP_ENTRY structure( as known the
// system zeroes the stack location on completion, but
// it zeroes the lower stack location ), if filter decides to use 
// the stack location then the OC_CORE_IRP_ENTRY is 
// allocated from the pool. The tracker distinguishes the 
// allocated pointer from embedded by checking the Flag 
// field.
//

//-------------------------------------------------------------------

#define OC_IRP_TRACKER_ENTRY_FROM_POOL 0x0001

//
// the size of this structure must be smaller
// than the sizeof( IRP_STACK_LOCATION.Parameters.Others )
//
typedef struct _OC_IRP_TRACK_ENTRY{
    ULONG              Flags;
    PIRP               Irp;
    LIST_ENTRY         ListEntry;
} OC_IRP_TRACK_ENTRY, *POC_IRP_TRACK_ENTRY;

//-------------------------------------------------------------------

static BOOLEAN                  g_IrpTrackerInitialized = FALSE;
static LIST_ENTRY               g_HeadOfIrps;
static OC_RW_SPIN_LOCK          g_RwSpinLock;
static NPAGED_LOOKASIDE_LIST    g_TrackEntriesLookasideList;

//-------------------------------------------------------------------

VOID
OcCrInitializeIrpTracker()
{
    ASSERT( FALSE == g_IrpTrackerInitialized );

    //
    // protect from ourselves
    //
    if( TRUE == g_IrpTrackerInitialized )
        return;

    ExInitializeNPagedLookasideList( &g_TrackEntriesLookasideList,
                                     NULL, 
                                     NULL, 
                                     0,
                                     sizeof( OC_IRP_TRACK_ENTRY ),
                                     'ETcO', 
                                     0 );

    OcRwInitializeRwLock( &g_RwSpinLock );
    InitializeListHead( &g_HeadOfIrps );
}

//-------------------------------------------------------------------

VOID
OcCrUnInitializeIrpTracker()
{
    if( FALSE == g_IrpTrackerInitialized )
        return;

    ExDeleteNPagedLookasideList( &g_TrackEntriesLookasideList );

    g_IrpTrackerInitialized = FALSE;

}

//-------------------------------------------------------------------

NTSTATUS
OcCrTrackerCompletionRoutine(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
{
    if( Irp->PendingReturned ){

        IoMarkIrpPending(Irp);
    }

    OcCrTrackerRemoveEntry( (struct _OC_IRP_TRACK_ENTRY*)Context );

    return STATUS_SUCCESS;
}

//-------------------------------------------------------------------

struct _OC_IRP_TRACK_ENTRY*
OcCrInsertIrpInProcessedList(
    IN PIRP       Irp,
    IN BOOLEAN    AllocateEntry
    )
    /*
    this function can be called with AllocateEntry
    set to FALSE only for our filter device objects. 
    i.e. for those which has its own stack.
    */
{
    POC_IRP_TRACK_ENTRY    PtrIrpTrackEntry;
    KIRQL                  OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( sizeof( *PtrIrpTrackEntry) == sizeof( IoGetCurrentIrpStackLocation( Irp )->Parameters.Others ) );

    if( TRUE == AllocateEntry ){

        PtrIrpTrackEntry = ExAllocateFromNPagedLookasideList( &g_TrackEntriesLookasideList );
        if( NULL == PtrIrpTrackEntry )
            return NULL;

        RtlZeroMemory( PtrIrpTrackEntry, sizeof( *PtrIrpTrackEntry ) );
        OcSetFlag( PtrIrpTrackEntry->Flags, OC_IRP_TRACKER_ENTRY_FROM_POOL );

    } else {

        PtrIrpTrackEntry = (POC_IRP_TRACK_ENTRY)( &IoGetCurrentIrpStackLocation( Irp )->Parameters.Others );
        RtlZeroMemory( PtrIrpTrackEntry, sizeof( *PtrIrpTrackEntry ) );
    }

    PtrIrpTrackEntry->Irp = Irp;

    OcRwAcquireLockForWrite( &g_RwSpinLock, &OldIrql );
    {//start of the lock
        //
        // insert at the begin of the list,
        // because with the great probability
        // the lower filter will soon try
        // to find this Irp
        //
        InsertHeadList( &g_HeadOfIrps, &PtrIrpTrackEntry->ListEntry );
    }//end of the lock
    OcRwReleaseWriteLock( &g_RwSpinLock, OldIrql );

    //
    // if the entry has been allocated from the pool
    // the caller must call OcCrTrackerRemoveEntry
    // with the returned PtrIrpTrackEntry
    //
    if( FALSE == AllocateEntry ){

        IoSetCompletionRoutine( Irp, 
                                OcCrTrackerCompletionRoutine, 
                                PtrIrpTrackEntry, 
                                TRUE, TRUE, TRUE );

        //
        // return non NULL but invalid value
        // to prevent caller from inadvertent
        // double removing both in the completion
        // routine and in some other routine
        //
        ASSERT( NULL != OC_INVALID_POINTER_VALUE );
        return (struct _OC_IRP_TRACK_ENTRY*)OC_INVALID_POINTER_VALUE;
    } else 
        return (struct _OC_IRP_TRACK_ENTRY*)PtrIrpTrackEntry;
}

//-------------------------------------------------------------------

VOID
OcCrTrackerRemoveEntry(
    IN struct _OC_IRP_TRACK_ENTRY* PtrIrpTrackEntry
    )
{
    KIRQL    OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL != PtrIrpTrackEntry );
    ASSERT( OC_INVALID_POINTER_VALUE != (PULONG_PTR)PtrIrpTrackEntry );

    OcRwAcquireLockForWrite( &g_RwSpinLock, &OldIrql );
    {//start of the lock
        RemoveEntryList( &PtrIrpTrackEntry->ListEntry );
    }//end of the lock
    OcRwReleaseWriteLock( &g_RwSpinLock, OldIrql );

    if( OcIsFlagOn( PtrIrpTrackEntry->Flags, OC_IRP_TRACKER_ENTRY_FROM_POOL ) ){

        ExFreeToNPagedLookasideList( &g_TrackEntriesLookasideList,
                                     PtrIrpTrackEntry );
    }
}

//-------------------------------------------------------------------

BOOLEAN
OcCrTrackerIsIrpInList(
    IN PIRP Irp
    )
{
    KIRQL          OldIrql;
    PLIST_ENTRY    request;
    BOOLEAN        Found = FALSE;

    OcRwAcquireLockForRead( &g_RwSpinLock, &OldIrql );
    {//start of the lock
        for( request = g_HeadOfIrps.Flink; request != &g_HeadOfIrps; request = request->Flink ){

            POC_IRP_TRACK_ENTRY    PtrIrpTrackEntry;

            PtrIrpTrackEntry = CONTAINING_RECORD( request, OC_IRP_TRACK_ENTRY, ListEntry );
            if( PtrIrpTrackEntry->Irp == Irp ){
                Found = TRUE;
                break;
            }
        }//for
    }//end of the lock
    OcRwReleaseReadLock( &g_RwSpinLock, OldIrql );

    return Found;
}

//-------------------------------------------------------------------
