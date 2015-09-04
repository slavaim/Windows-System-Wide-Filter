/*
Author: Slava Imameev   
(c) 2007 Slava Imameev, All Rights Reserved

Revision history:
05.12.2007 ( December )
 Start
*/

#include "filter.h"
#include "proto.h"

//
// This file contains a simple code for the driver hooker,
// I try to keep this code as simple and benign as possible:
// there is no any premature optimization, no any object model 
// is used here, the code is simple and straightforward.
// The first and single goal is stability.
//

//---------------------------------------------------

typedef struct _PNP_FILTER_HOOKED_DRIVER{

    LIST_ENTRY         ListEntry;
    PDRIVER_OBJECT     DriverObject;
    DRIVER_OBJECT      ContentForDriverObject;
    PDRIVER_UNLOAD     OriginalDriverUnload;
    PDRIVER_DISPATCH   PnPMajorOriginalFunction;

} PNP_FILTER_HOOKED_DRIVER, *PPNP_FILTER_HOOKED_DRIVER;

//---------------------------------------------------

static KSPIN_LOCK    g_HookedDriversListSpinLock;
static LIST_ENTRY    g_HookedDriversList;
static NPAGED_LOOKASIDE_LIST    g_HookedDriversDescriptorsMemoryPool;

//---------------------------------------------------

VOID
OcFilterInitializeHookerEngine()
{

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    KeInitializeSpinLock( &g_HookedDriversListSpinLock );
    InitializeListHead( &g_HookedDriversList );

    ExInitializeNPagedLookasideList( &g_HookedDriversDescriptorsMemoryPool,
                                     NULL,
                                     NULL,
                                     0x0,
                                     sizeof( PNP_FILTER_HOOKED_DRIVER ),
                                     'HFcO',
                                     0x0 );

}

//---------------------------------------------------

__forceinline
VOID
OcFilterLockHookedDriverList(
    __out_ecount(sizeof(KIRQL)) PKIRQL   PtrOldIrql
    )
{
    KeAcquireSpinLock( &g_HookedDriversListSpinLock, PtrOldIrql );
}

//---------------------------------------------------

__forceinline
VOID
OcFilterUnLockHookedDriverList(
    __in KIRQL   OldIrql
    )
{
    ASSERT( DISPATCH_LEVEL == KeGetCurrentIrql() );
    ASSERT( 0x1 == g_HookedDriversListSpinLock );

    KeReleaseSpinLock( &g_HookedDriversListSpinLock, OldIrql );
}

//---------------------------------------------------

__forceinline
PPNP_FILTER_HOOKED_DRIVER
OcFilterReturnHookedDriverDescriptor(
    __in PDRIVER_OBJECT    DriverObject,
    __in BOOLEAN           LockHookedDriverList
    )
    /*
    the function return a descriptor with matching DriverObject
    */
{
    BOOLEAN                      Found = FALSE;
    PPNP_FILTER_HOOKED_DRIVER    PtrHookedDriver = NULL;
    KIRQL                        OldIrql;

    ASSERT( LockHookedDriverList? ( KeGetCurrentIrql() <= DISPATCH_LEVEL ):
                                  ( KeGetCurrentIrql() == DISPATCH_LEVEL ) );
    ASSERT( IO_TYPE_DRIVER == DriverObject->Type );

    if( LockHookedDriverList) OcFilterLockHookedDriverList( &OldIrql );
    {// start of the lock ( if required )

        PLIST_ENTRY                  request;

        for( request = g_HookedDriversList.Flink;
             request != &g_HookedDriversList;
             request = request->Flink ){

            PtrHookedDriver = CONTAINING_RECORD( request,
                                                 PNP_FILTER_HOOKED_DRIVER,
                                                 ListEntry );

            if( PtrHookedDriver->DriverObject != DriverObject )
                continue;

            Found = TRUE;
            break;

        }// for

    }// end of the lock
    if( LockHookedDriverList ) OcFilterUnLockHookedDriverList( OldIrql );

    return ( Found? PtrHookedDriver: NULL );
}

//---------------------------------------------------

VOID
OcFilterReferenceVirtualPdoExtension(
    __in PDEVICE_EXTENSION    deviceExtension
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL != deviceExtension );
    ASSERT( NULL == deviceExtension->Self );
    ASSERT( deviceExtension->ReferenceCount > 0x0 );

    InterlockedIncrement( &deviceExtension->ReferenceCount );
}

//---------------------------------------------------

BOOLEAN
OcFilterRenderVirtualPdoAsDeleted(
    __in PDEVICE_EXTENSION    deviceExtension
    )
    /*
     The function removes the deviceExtension
    from the list and markes it as deleting.
     The function returns TRUE if the caller is the 
    first who called this function, the function
    should not be sprinkled along the code - use
    it with caution or there will be a lot of 
    dangling references
    */
{

    KIRQL       OldIrql;
    BOOLEAN     CollisionWithAnotherDeletion = FALSE;

    //
    // remove from the list so nobody can find this entry
    //

    KeAcquireSpinLock( &PnpFilterGlobal.SpinLock, &OldIrql );
    {// start of the lock

        if( 0x0 == deviceExtension->Flags.RemovedFromList ){

            //
            // remove from the list, so nobody can reference it
            //

            ASSERT( !IsListEmpty( &deviceExtension->ListEntry ) );
            RemoveEntryList( &deviceExtension->ListEntry );
            deviceExtension->Flags.RemovedFromList = 0x1;
            ASSERT( (InitializeListHead( &deviceExtension->ListEntry ), TRUE ) );
        }

        //
        // check whether the extension for virtual PDO is being removed in another thread
        //
        if( 0x1 == deviceExtension->Flags.MarkedForDeletion )
            CollisionWithAnotherDeletion = TRUE;
        else
            deviceExtension->Flags.MarkedForDeletion = 0x1;

    }// end of the lock
    KeReleaseSpinLock( &PnpFilterGlobal.SpinLock, OldIrql );

    ASSERT( CollisionWithAnotherDeletion? 
            NULL != deviceExtension->DebugInfo.DeletingThread: 
            NULL == deviceExtension->DebugInfo.DeletingThread );

#if DBG
    deviceExtension->DebugInfo.DeletingThread = PsGetCurrentThread();
#endif//DBG

    return !CollisionWithAnotherDeletion;
}

//---------------------------------------------------

NTSTATUS
OcFilterDeleteVirtualPdoByExtensionInternal(
    __in PDEVICE_EXTENSION    deviceExtension
    )
    /*
    this is an internal function and should not be used
    carelessly, the caller MUST not acquire any lock 
    including the remove lock
    */
{
    BOOLEAN    IsExtensionLocked;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( NULL == deviceExtension->Self );
    ASSERT( 0x1 == deviceExtension->Flags.MarkedForDeletion );
    ASSERT( 0x1 == deviceExtension->Flags.RemovedFromList );

    //
    // So looking in the following assert you can legitimately ask - "Why
    // is the count compared to 0x1?", the answer is something vague -
    // the relation count may be 0x0 or 0x1, the former means that the 
    // the PDO has been inserted while processing device relations and has been
    // reported as missing through BusRelation request and the latter
    // means that either the PDO has been inserted while processing in AddDevice
    // or the system sent IRP_MN_REMOVE to a stack for this PDO while
    // the PDO had not been reported as removed from a bus relation
    //
    ASSERT( 0x0 == deviceExtension->RelationReferenceCount || 
            0x1 == deviceExtension->RelationReferenceCount );

    if( STATUS_SUCCESS == IoAcquireRemoveLock( &deviceExtension->RemoveLock, NULL ) ){

        //
        // acquire the lock
        //
        IsExtensionLocked = TRUE;
    }

    ASSERT( IsExtensionLocked );

    if( !IsExtensionLocked ){

        //
        // something went wrong with the logic
        //
        ASSERT( !"IoAcquireRemoveLock failed for a virtual device" );
#if DBG
        ASSERT( !"An extremely severe error! Investigate immediately" );
        KeBugCheckEx( OC_PNPFILTER_BUG_ACQUIRING_REMOVE_LOCK_FAILED,
                      (ULONG_PTR)deviceExtension,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)NULL,
                      (ULONG_PTR)NULL );
#endif//DBG
        return STATUS_INVALID_PARAMETER;
    }

    //
    // check that the extension is not in the list
    //
    ASSERT( deviceExtension != OcFilterReturnPdoExtensionUnsafe( deviceExtension->PhysicalDeviceObject ) );
    ASSERT( IsListEmpty( &deviceExtension->ListEntry ) );
    ASSERT( 0x0 == deviceExtension->ReferenceCount );

    //
    // synchronize for a removal and free the extension structure
    //

    //
    // Wait for all outstanding requests to complete
    //
    IoReleaseRemoveLockAndWait( &deviceExtension->RemoveLock, NULL );

    ASSERT( 0x0 == deviceExtension->ReferenceCount );

    //
    // the device is being removed, so prepare for this and notify a client -
    // emulate the IRP_MN_REMOVE receiving and call the client to 
    // report the change in the device state, it is possible that the
    // delete state will be reported twice - the first time from the 
    // OcFilterPnPMajorHookDispatch and the second time from this routine,
    // but as I can't rely on a fact that OcFilterPnPMajorHookDispatch has
    // been called( this is a hoook which can be redifined ) I make the second
    // and the final call here
    //
    SET_NEW_PNP_STATE( deviceExtension, Deleted );
    OcFilterReportNewDeviceState( deviceExtension );

    //
    // clear device's extension before removing the virtual device
    //
    FilterClearFilterDeviceExtension( deviceExtension );

    //
    // free the memory allocated for the "virtual device" extension,
    //
    ExFreeToNPagedLookasideList( &PnpFilterGlobal.DeviceExtensionMemoryPool,
                                 deviceExtension );

    return STATUS_SUCCESS;
}

//---------------------------------------------------

VOID
OcFilterDereferenceVirtualPdoExtension(
    __in PDEVICE_EXTENSION    deviceExtension
    )
    /*
    the caller must not hold the PnpFilterGlobal.SpinLock lock
    or a deadlock is imminent in OcFilterRenderVirtualPdoAsDeleted
    */
{

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL == deviceExtension->Self );
    ASSERT( deviceExtension->ReferenceCount > 0x0 && deviceExtension->ReferenceCount < 0xFFFF );

#if DBG
    //
    // the check for the condition that the caller should not hold the lock
    //
    {
        KIRQL    OldIrql;
        KeAcquireSpinLock( &PnpFilterGlobal.SpinLock, &OldIrql );
        KeReleaseSpinLock( &PnpFilterGlobal.SpinLock, OldIrql );
    }
#endif//DBG

    if( 0x0 != InterlockedDecrement( &deviceExtension->ReferenceCount ) )
        return;

    ASSERT( (NULL == deviceExtension->DebugInfo.DeletingThread)? 
        0x0 == deviceExtension->Flags.MarkedForDeletion:
        0x1 == deviceExtension->Flags.MarkedForDeletion );

    //
    // delete the extension
    //

    //
    // perform all operations which must be synchronous with the caller,
    // the return value is meaningless here - the extension either will be 
    // marked as deleted here or has been already marked as deleted
    //
    OcFilterRenderVirtualPdoAsDeleted( deviceExtension );

    ASSERT( 0x1 == deviceExtension->Flags.RemovedFromList );
    ASSERT( 0x1 == deviceExtension->Flags.MarkedForDeletion );

    if( KeGetCurrentIrql() == PASSIVE_LEVEL ){

        OcFilterDeleteVirtualPdoByExtensionInternal( deviceExtension );

    } else {

        NTSTATUS    RC;
        POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject;

        //
        // made the deletion in a worker thread
        //

        PtrWorkItemListObject = OcTplReferenceSharedWorkItemList( PnpFilterGlobal.ThreadsPoolObject );
        if( NULL != PtrWorkItemListObject ){

            RC = OcWthPostWorkItemParam1( PtrWorkItemListObject,
                                          FALSE,
                                          (Param1SysProc)OcFilterDeleteVirtualPdoByExtensionInternal,
                                          (ULONG_PTR)deviceExtension );

            OcObDereferenceObject( PtrWorkItemListObject );

            if( !NT_SUCCESS( RC ) ){

#if DBG
                ASSERT( !"An extremely severe error! Investigate immediately" );
                KeBugCheckEx( OC_PNPFILTER_BUG_DEFERRED_VPDO_DELETION_FAILED,
                    (ULONG_PTR)deviceExtension,
                    (ULONG_PTR)PtrWorkItemListObject,// invalid here! Just for reference!
                    (ULONG_PTR)(KeGetCurrentIrql()),
                    (ULONG_PTR)NULL );
#endif//DBG
            }

        } else {

#if DBG
            ASSERT( !"An extremely severe error! Investigate immediately" );
            KeBugCheckEx( OC_PNPFILTER_BUG_DEFERRED_VPDO_DELETION_FAILED,
                (ULONG_PTR)deviceExtension,
                (ULONG_PTR)NULL,
                (ULONG_PTR)(KeGetCurrentIrql()),
                (ULONG_PTR)NULL );
#endif//DBG
        }

    }
}

//---------------------------------------------------

PDEVICE_EXTENSION
OcFilterReturnReferencedPdoExtensionLockIf(
    __in PDEVICE_OBJECT    Pdo,
    __in BOOLEAN           ReturnLockedExtension
    )
    /*
    If ReturnLockedExtension is TRUE the function acquires 
    the remove lock before returning, the caller MUST call
    IoReleaseRemoveLock.
    The function can find only thise PDOs which was not removed
    from the global list, i.e. PDOs which stack is not torn down or
    being torn down because they have not been reported in the last 
    BusRelation requests
    */
{

    KIRQL                        OldIrql;
    PDEVICE_EXTENSION            deviceExtension = NULL;
    BOOLEAN                      Found = FALSE;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    KeAcquireSpinLock( &PnpFilterGlobal.SpinLock, &OldIrql );
    {//begin of the lock

        PLIST_ENTRY         request;

        //
        // get the device and acquire its remove lock
        // skip devices in a removing state
        //
        for( request = PnpFilterGlobal.ListHead.Flink ; 
             request != &PnpFilterGlobal.ListHead; 
             request = request->Flink ){

                //
                // get the extension for the device
                //
                deviceExtension = CONTAINING_RECORD( request,
                                                     DEVICE_EXTENSION,
                                                     ListEntry );

                //
                // the entry should be removed from the list before delition
                //
                ASSERT( 0x0 == deviceExtension->Flags.MarkedForDeletion );

                if( Pdo != deviceExtension->PhysicalDeviceObject || 
                    NULL != deviceExtension->Self )
                    continue;

                //
                // acquire the remove lock while the spin lock is helded
                // the remove lock disables the device removal,
                // if the device in the removing state the
                // STATUS_DELETE_PENDING code is usually returned
                //
                if( ReturnLockedExtension && 
                    !NT_SUCCESS( IoAcquireRemoveLock( &deviceExtension->RemoveLock, NULL ) ) ){

                    ASSERT( !"A removing lock acquisition failed" );
                    Found = FALSE;

                } else { //if( STATUS_DELETE_PENDING == LockRC )

                    Found = TRUE;
                }

#if DBG
                if( !Found )
                    deviceExtension = NULL;
#endif//DBG
                if( Found )
                    OcFilterReferenceVirtualPdoExtension( deviceExtension );

                //
                // the entry with the valid device has been found and locked, 
                // report this device
                //
                break;
        }//for

    }//end of the lock
    KeReleaseSpinLock( &PnpFilterGlobal.SpinLock, OldIrql );

    if( FALSE == Found )
        deviceExtension = NULL;

    return deviceExtension;
}

//---------------------------------------------------

#if DBG
PDEVICE_EXTENSION
OcFilterReturnPdoExtensionUnsafe(
    __in PDEVICE_OBJECT    Pdo
    )
{
    PDEVICE_EXTENSION    deviceExtension;

    deviceExtension = OcFilterReturnReferencedPdoExtensionLockIf( Pdo, FALSE );
    if( NULL != deviceExtension ){

        OcFilterDereferenceVirtualPdoExtension( deviceExtension );
    }

    return deviceExtension;
}
#endif//DBG

//---------------------------------------------------

__forceinline
VOID
OcFilterRemoveHookedDriverDescriptorFromList(
    __inout PPNP_FILTER_HOOKED_DRIVER    PtrHookedDriver
    )
    /*
    the caller must lock the list
    */
{
    ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

    RemoveEntryList( &PtrHookedDriver->ListEntry );
    InitializeListHead( &PtrHookedDriver->ListEntry );
}

//---------------------------------------------------

VOID
OcFilterDriverUnloadHook(
    __in PDRIVER_OBJECT    DriverObject
    )
{
    KIRQL                        OldIrql;
    PPNP_FILTER_HOOKED_DRIVER    PtrHookedDriver;
    PDRIVER_UNLOAD               OriginalDriverUnload;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    OcFilterLockHookedDriverList( &OldIrql );
    {// start of the lock

        PtrHookedDriver = OcFilterReturnHookedDriverDescriptor( DriverObject, FALSE );
        if( NULL == PtrHookedDriver ){
#if DBG
            ASSERT( !"Extremely severe error! Investigate immediately" );
            KeBugCheckEx( OC_PNPFILTER_BUG_UNKNOWN_HOOKED_DRIVER_ON_UNLOAD,
                          (ULONG_PTR)DriverObject,
                          (ULONG_PTR)&g_HookedDriversList,
                          (ULONG_PTR)&g_HookedDriversDescriptorsMemoryPool,
                          (ULONG_PTR)0x0 );
#endif//DBG
            //
            // end of the lock
            //
            OcFilterUnLockHookedDriverList( OldIrql );
            return;
        }

        OriginalDriverUnload = PtrHookedDriver->OriginalDriverUnload;
        ASSERT( NULL != OriginalDriverUnload );

        //
        // unhook driver before removing entry from the list
        //
        ASSERT( !IsListEmpty( &PtrHookedDriver->ListEntry ) );
        InterlockedExchangePointer( (PVOID*)&DriverObject->DriverUnload,
                                    (PVOID)PtrHookedDriver->OriginalDriverUnload );
        InterlockedExchangePointer( (PVOID*)&DriverObject->MajorFunction[ IRP_MJ_PNP ],
                                    (PVOID)PtrHookedDriver->PnPMajorOriginalFunction );

        //
        // remove entry from the list
        //
        OcFilterRemoveHookedDriverDescriptorFromList( PtrHookedDriver );

    }// end of the lock
    OcFilterUnLockHookedDriverList( OldIrql );

    ASSERT( PtrHookedDriver->DriverObject == DriverObject &&
            PtrHookedDriver->ContentForDriverObject.DriverStartIo == DriverObject->DriverStartIo &&
            PtrHookedDriver->ContentForDriverObject.DriverExtension == DriverObject->DriverExtension );

    //
    // free the memory
    //
    ExFreeToNPagedLookasideList( &g_HookedDriversDescriptorsMemoryPool,
                                 PtrHookedDriver );

    //
    // call the original function
    //
    OriginalDriverUnload( DriverObject );
}

//---------------------------------------------------

NTSTATUS
OcFilterPnPMajorHookDispatch(
    __in PDEVICE_OBJECT    DeviceObject,
    __in PIRP    Irp
    )
{
    PPNP_FILTER_HOOKED_DRIVER    PtrHookedDriver;
    PIO_STACK_LOCATION           irpStack;
    PDEVICE_EXTENSION            deviceExtension;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    irpStack = IoGetCurrentIrpStackLocation(Irp);

    ASSERT( IRP_MJ_PNP == irpStack->MajorFunction );

    PtrHookedDriver = OcFilterReturnHookedDriverDescriptor( DeviceObject->DriverObject, TRUE );
    if( NULL == PtrHookedDriver ){
#if DBG
        ASSERT( !"Extremely severe error! Investigate immediately" );
        KeBugCheckEx( OC_PNPFILTER_BUG_UNKNOWN_HOOKED_DRIVER_ON_PNP,
                      (ULONG_PTR)DeviceObject,
                      (ULONG_PTR)&g_HookedDriversList,
                      (ULONG_PTR)&g_HookedDriversDescriptorsMemoryPool,
                      (ULONG_PTR)0x0 );
#endif//DBG
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest( Irp, IO_NO_INCREMENT );
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // find the "virtual device's" extenstion
    // and set the device's PnP state
    //

    deviceExtension = OcFilterReturnReferencedPdoExtensionLockIf( DeviceObject, TRUE );
    if( NULL == deviceExtension ){
#if DBG
        if( OcFilterIsPdo( DeviceObject ) ){

            PDEVICE_OBJECT    AttachedDevice;

            //
            // check whether there is any filter device in the stack,
            // if yes then there is an error - a PDO should have been found
            // for this filter
            //
            AttachedDevice = DeviceObject->AttachedDevice;
            while( NULL != AttachedDevice ){

                if( AttachedDevice->DriverObject == PnpFilterGlobal.DriverObject ){
                    ASSERT( !"A PDO has not been found while should have been in the list" );
                }

                AttachedDevice = AttachedDevice->AttachedDevice;

            }// while
        }
#endif//DBG
        goto __call_original;
    }

    ASSERT( NULL != deviceExtension );

    //
    // this should be a "virtual PDO", the system sometimes sends
    // PnP requests to non Pnp devices such as MUP
    //
    ASSERT( ( OcDevicePnPTypePdo == deviceExtension->PnPDeviceType? 
               OcFilterIsPdo( deviceExtension->PhysicalDeviceObject ): 
               OcDeviceLowerNoPnPType == deviceExtension->PnPDeviceType )
               && 
            NULL == deviceExtension->Self );

    //
    // the following code was duplicated from FilterDispatchPnp,
    // so consult with it for any change, not all request are
    // reorted to the client, because it is very hard
    // to set a completion routine here as required to
    // divide notification to two parts - a pre-request part
    // and a post-request part, the client should
    // hook the driver and perform this processing by itself
    //

    switch( irpStack->MinorFunction ){

            case IRP_MN_START_DEVICE:
                {
                    PDEVICE_OBJECT    FilterDevice = NULL;

                    //
                    // ideally the following code shoud run in completion routine,
                    // but this is very hard to achieve this without our
                    // own stack location in the IRP and I made a decision to refrain
                    // from the completion routine hooking in this filter
                    //

                    //
                    // add a filter device to the stack if there is no any
                    // filter device, the device will be removed in any
                    // case even if the start device will fail because
                    // the system calls FastIoDetachDevice when IoDeleteDevice
                    // is called for a lower device.
                    // This adding will have the serious consequences - 
                    // all the stacks growing from this device will be hooked
                    // so I will be able to trace any device stack, even having
                    // unknown busses in the middle.
                    // It is important that this method of filter attaching
                    // doesn't have an inherited race condition - the PnP manager
                    // serializes the device start and removal for us.
                    //
                    if( 0x0 == deviceExtension->Flags.FilterAttached ){

                        NTSTATUS          RC;
                        PDEVICE_OBJECT    FilterDevice;

                        RC = FilterAddDeviceEx( PnpFilterGlobal.DriverObject,
                                                DeviceObject, // this is a PDO
                                                OcDevicePnPTypeFilterDo,
                                                &FilterDevice,
                                                FALSE );

                        if( !NT_SUCCESS( RC ) ){

                            FilterDevice = NULL;
                        }

                        ASSERT( NT_SUCCESS( RC ) );
                        ASSERT( 0x1 == deviceExtension->Flags.FilterAttached );
                    }

                    ASSERT( 0x1 == deviceExtension->Flags.FilterAttached );

                    //
                    // set the new states starting from the PDO
                    //
                    SET_NEW_PNP_STATE( deviceExtension, Started );

                    //
                    // report that the PDO is in the started state
                    //
                    OcFilterReportNewDeviceState( deviceExtension );

                    if( NULL != FilterDevice ){

                        //
                        // mark the newly created filter device as started
                        // this device won't receive start request from the PnP Manager
                        //
                        SET_NEW_PNP_STATE( (PDEVICE_EXTENSION)FilterDevice->DeviceExtension, Started );

                        //
                        // report that the filter device is in the started state
                        //
                        OcFilterReportNewDeviceState( (PDEVICE_EXTENSION)FilterDevice->DeviceExtension );

                    } else {

                        //
                        // so there is a subtle moment here - the client
                        // might have not received a notification about 
                        // adding the FiDO( if the attaching to the stack failed,
                        // see above ) so it considers the PDO as 
                        // an uninitialized PDO, but will receive the 
                        // notification about the transition to the 
                        // Start state, so to provide a usual sequence 
                        // for a client I call the client second time to 
                        // report that the PDO is initialized
                        //
                        OcFilterReportPdoStateRelatedToPnpManager( deviceExtension );

                    }

                }
                break;

            case IRP_MN_REMOVE_DEVICE:
                //
                // set the device state as deleted, the device will be
                // removed when the parent device is removed or
                // the device is not reported in BusRelation, it is
                // possible to retain the device after receiving
                // IRP_MN_REMOVE_DEVICE, so the lock is not released here
                // for removal
                //
                SET_NEW_PNP_STATE( deviceExtension, Deleted );

                //
                // free all device's relations
                //
                OcFreeAllDeviceRelations( deviceExtension );

                OcFilterReportNewDeviceState( deviceExtension );
                break;

            case IRP_MN_QUERY_STOP_DEVICE:
                SET_NEW_PNP_STATE(deviceExtension, StopPending);
                OcFilterReportNewDeviceState( deviceExtension );
                break;

            case IRP_MN_CANCEL_STOP_DEVICE:

                //
                // Check to see whether you have received cancel-stop
                // without first receiving a query-stop. This could happen if someone
                // above us fails a query-stop and passes down the subsequent
                // cancel-stop.
                //

                if( StopPending == deviceExtension->DevicePnPState )
                {
                    //
                    // We did receive a query-stop, so restore.
                    //
                    RESTORE_PREVIOUS_PNP_STATE( deviceExtension );
                    OcFilterReportNewDeviceState( deviceExtension );
                }
                break;

            case IRP_MN_STOP_DEVICE:
                SET_NEW_PNP_STATE(deviceExtension, Stopped);
                OcFilterReportNewDeviceState( deviceExtension );
                break;

            case IRP_MN_QUERY_REMOVE_DEVICE:
                SET_NEW_PNP_STATE( deviceExtension, RemovePending );
                OcFilterReportNewDeviceState( deviceExtension );
                break;

            case IRP_MN_SURPRISE_REMOVAL:
                SET_NEW_PNP_STATE( deviceExtension, SurpriseRemovePending );
                OcFilterReportNewDeviceState( deviceExtension );
                break;

            case IRP_MN_CANCEL_REMOVE_DEVICE:
                //
                // Check to see whether you have received cancel-remove
                // without first receiving a query-remove. This could happen if
                // someone above us fails a query-remove and passes down the
                // subsequent cancel-remove.
                //

                if( RemovePending == deviceExtension->DevicePnPState ){
                    //
                    // We did receive a query-remove, so restore.
                    //
                    RESTORE_PREVIOUS_PNP_STATE(deviceExtension);
                    OcFilterReportNewDeviceState( deviceExtension );
                }
                break;

            case IRP_MN_DEVICE_USAGE_NOTIFICATION:

                switch( irpStack->Parameters.UsageNotification.Type ){

                    case DeviceUsageTypePaging:

                        if( irpStack->Parameters.UsageNotification.InPath )
                            deviceExtension->DeviceUsage.DeviceUsageTypePaging = 0x1;
                        else
                            deviceExtension->DeviceUsage.DeviceUsageTypePaging = 0x0;

                        break;

                    case DeviceUsageTypeHibernation:

                        if( irpStack->Parameters.UsageNotification.InPath )
                            deviceExtension->DeviceUsage.DeviceUsageTypeHibernation = 0x1;
                        else
                            deviceExtension->DeviceUsage.DeviceUsageTypeHibernation = 0x0;

                        break;

                    case DeviceUsageTypeDumpFile:

                        if( irpStack->Parameters.UsageNotification.InPath )
                            deviceExtension->DeviceUsage.DeviceUsageTypeDumpFile = 0x1;
                        else
                            deviceExtension->DeviceUsage.DeviceUsageTypeDumpFile = 0x0;

                        break;

                    }// switch( irpStack->Parameters.UsageNotification.Type )

                break;
    }// switch( irpStack->MinorFunction )

    //
    // release the lock acquired by OcFilterReturnReferencedPdoExtensionLockIf
    //
    IoReleaseRemoveLock( &deviceExtension->RemoveLock, NULL );

    //
    // dereference the extension referenced in OcFilterReturnReferencedPdoExtensionLockIf
    //
    OcFilterDereferenceVirtualPdoExtension( deviceExtension );

__call_original:

    ASSERT( PtrHookedDriver->DriverObject == DeviceObject->DriverObject &&
            PtrHookedDriver->ContentForDriverObject.DriverStartIo == DeviceObject->DriverObject->DriverStartIo &&
            PtrHookedDriver->ContentForDriverObject.DriverExtension == DeviceObject->DriverObject->DriverExtension );

    return PtrHookedDriver->PnPMajorOriginalFunction( DeviceObject, Irp );
}

//---------------------------------------------------

__forceinline
BOOLEAN
OcFilterIsDriverHookedByFilter(
    __in PDRIVER_OBJECT   DriverObject
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( IO_TYPE_DRIVER == DriverObject->Type );

    return ( OcFilterPnPMajorHookDispatch == DriverObject->MajorFunction[ IRP_MJ_PNP ] );
}

//---------------------------------------------------

NTSTATUS
OcFilterHookDriver(
    __in PDRIVER_OBJECT    DriverObject
    )
{
    NTSTATUS    RC = STATUS_SUCCESS;
    KIRQL       OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( IO_TYPE_DRIVER == DriverObject->Type );

    //
    // if DriverUnload and PnP dispatcher are both NULL then
    // there is no need in hooking
    //
    if( OcFilterIsDriverHookedByFilter( DriverObject ) || 
        ( NULL == DriverObject->DriverUnload &&
          NULL == DriverObject->MajorFunction[ IRP_MJ_PNP ] ) )
        return RC;

    OcFilterLockHookedDriverList( &OldIrql );
    {// start of the lock

        BOOLEAN                      AlreadyHooked = FALSE;
        PPNP_FILTER_HOOKED_DRIVER    PtrNewHookedDriver = NULL;
        PPNP_FILTER_HOOKED_DRIVER    PtrHookedDriver;

        PtrHookedDriver = OcFilterReturnHookedDriverDescriptor( DriverObject, FALSE );
        if( NULL != PtrHookedDriver ){

            //
            // check whether the entry describes this driver object
            //
            if( PtrHookedDriver->DriverObject == DriverObject &&
                PtrHookedDriver->ContentForDriverObject.DriverStartIo == DriverObject->DriverStartIo &&
                PtrHookedDriver->ContentForDriverObject.DriverExtension == DriverObject->DriverExtension ){

                //
                // the driver has been already hooked,
                // there might be a paranoid case when a driver's
                // unload function had been rehooked then the driver 
                // had been reloaded and after loading became 
                // indistinguishable from the previous load,
                // in this case the driver is not hooked - 
                // I choose this case as the more benign in 
                // compare to hooking, because some driver
                // might rehook the whole driver and this looks
                // like the described paranoid case but this driver
                // might not tolerate rehooking
                //
                AlreadyHooked = TRUE;

            } else if( PtrHookedDriver->DriverObject == DriverObject ){

                //
                // the staled entry has been found, reuse it
                //
                OcFilterRemoveHookedDriverDescriptorFromList( PtrHookedDriver );
                RtlZeroMemory( PtrHookedDriver, sizeof( *PtrHookedDriver) );
                PtrNewHookedDriver = PtrHookedDriver;
            }

        }// if( NULL != PtrHookedDriver )

        if( FALSE == AlreadyHooked ){

            if( NULL == PtrNewHookedDriver )
                PtrNewHookedDriver = ExAllocateFromNPagedLookasideList( &g_HookedDriversDescriptorsMemoryPool );

            if( NULL != PtrNewHookedDriver ){

                //
                // initialize the new entry
                //
                InitializeListHead( &PtrNewHookedDriver->ListEntry );
                PtrNewHookedDriver->DriverObject = DriverObject;
                PtrNewHookedDriver->ContentForDriverObject = *DriverObject;
                PtrNewHookedDriver->OriginalDriverUnload = DriverObject->DriverUnload;
                PtrNewHookedDriver->PnPMajorOriginalFunction = DriverObject->MajorFunction[ IRP_MJ_PNP ];

                //
                // insert the new entry in the list before hooking the driver
                //
                InsertTailList( &g_HookedDriversList, &PtrNewHookedDriver->ListEntry );

                //
                // hook the driver
                //
                ASSERT( !IsListEmpty( &PtrNewHookedDriver->ListEntry ) );

                if( NULL != PtrNewHookedDriver->OriginalDriverUnload )
                    InterlockedExchangePointer( (PVOID*)&DriverObject->DriverUnload,
                                                (PVOID)OcFilterDriverUnloadHook );

                if( NULL != PtrNewHookedDriver->PnPMajorOriginalFunction )
                    InterlockedExchangePointer( (PVOID*)&DriverObject->MajorFunction[ IRP_MJ_PNP ],
                                                (PVOID)OcFilterPnPMajorHookDispatch );

            } else {

                RC = STATUS_INSUFFICIENT_RESOURCES;
            }

        }//if( FALSE == AlreadyHooked )

    }// end of the lock
    OcFilterUnLockHookedDriverList( OldIrql );

    return RC;
}

//---------------------------------------------------

