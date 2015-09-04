/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
12.12.2006 ( December )
 Start
*/

#if !defined(_OC_CORE_IRP_H_)
#define _OC_CORE_IRP_H_

struct _OC_IRP_TRACK_ENTRY;

extern
VOID
OcCrInitializeIrpTracker();

extern
VOID
OcCrUnInitializeIrpTracker();

extern
struct _OC_IRP_TRACK_ENTRY*
OcCrInsertIrpInProcessedList(
    IN PIRP       Irp,
    IN BOOLEAN    AllocateEntry
    );

extern
BOOLEAN
OcCrTrackerIsIrpInList(
    IN PIRP Irp
    );

extern
VOID
OcCrTrackerRemoveEntry(
    IN struct _OC_IRP_TRACK_ENTRY*    PtrIrpTrackEntry
    );

#endif//_OC_CORE_IRP_H_