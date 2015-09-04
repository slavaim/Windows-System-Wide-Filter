/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/

#include "filter.h"
#include "proto.h"

//
// set to TRUE to allow posting requests in a worker thread, FALSE otherwise
//
#define OCFLT_POSTPONE_IF    (TRUE)

//-------------------------------------------------

NTSTATUS
FilterQueryDeviceRelationsCompletionRoutine(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    );

static
VOID
OcFilterSetNewDeviceRelations(
    IN PDEVICE_EXTENSION    deviceExtension,
    IN PDEVICE_RELATIONS    PtrNewRealtions,
    IN DEVICE_RELATION_TYPE    DeviceRelationsType
    );

static
NTSTATUS
OcFilterPreStartCallback(
    IN PDEVICE_EXTENSION    deviceExtension,
    IN PIRP    Irp,
    OUT POC_FILTER_IRP_DECISION     PtrIrpDecision
    );

static
VOID
OcFilterReportReceivingDeviceUsageNotification(
    IN PDEVICE_EXTENSION    deviceExtension,
    IN ULONG_PTR            RequstId,
    IN DEVICE_USAGE_NOTIFICATION_TYPE    Type,
    IN BOOLEAN              InPath,
    IN PVOID                Buffer OPTIONAL
    );

static
VOID
OcFilterReportCompletingDeviceUsageNotification(
    IN PDEVICE_EXTENSION    deviceExtension,
    IN ULONG_PTR            RequstId,
    IN PIO_STATUS_BLOCK     StatusBlock
    );

//-------------------------------------------------

__forceinline
PDEVICE_OBJECT
FilterGetDeviceToReportForExtension(
    PDEVICE_EXTENSION    deviceExtension
    )
{

    //
    // check that the extension has not been deinitialized
    //
    ASSERT( (DEVICE_TYPE)(-1) != deviceExtension->Type );

    //
    // choose either the self device pointer( i.e. this is a filter's device extension )
    // or the PDO ( i.e. this is an extension for the "virtual device" )
    //
    return ( NULL != deviceExtension->Self )?
              deviceExtension->Self:
              deviceExtension->PhysicalDeviceObject;
}
//-------------------------------------------------

NTSTATUS
FilterPass (
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
/*++

Routine Description:

    The default dispatch routine.  If this driver does not recognize the
    IRP, then it should send it down, unmodified.
    If the device holds iris, this IRP must be queued in the device extension
    No completion routine is required.

    For demonstrative purposes only, we will pass all the (non-PnP) Irps down
    on the stack (as we are a filter driver). A real driver might choose to
    service some of these Irps.

    As we have NO idea which function we are happily passing on, we can make
    NO assumptions about whether or not it will be called at raised IRQL.
    For this reason, this function must be in put into non-paged pool
    (aka the default location).

Arguments:

   DeviceObject - pointer to a device object.

   Irp - pointer to an I/O Request Packet.

Return Value:

      NT status code

--*/
{
    PDEVICE_EXTENSION                deviceExtension;
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;
    OC_FILTER_IRP_DECISION           IrpDecision = OcFilterSendIrpToLowerSkipStack;
    NTSTATUS                         status;

    ASSERT( DEVICE_TYPE_FIDO == ((PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension)->Type || 
            DEVICE_TYPE_FIDO_NO_PNP == ((PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension)->Type );

    deviceExtension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;
    status = IoAcquireRemoveLock (&deviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS (status)) {
        Irp->IoStatus.Status = status;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL != PtrConnectionObject ){

        NTSTATUS                  RC;

        RC = PtrConnectionObject->CallbackMethods.DispatcherCallback( DeviceObject,
                                                                      deviceExtension->NextLowerDriver,
                                                                      Irp,
                                                                      &IrpDecision );

        OcObDereferenceObject( PtrConnectionObject );
        if( OcFilterReturnCode == IrpDecision ){

            IoReleaseRemoveLock( &deviceExtension->RemoveLock, Irp );
            return RC;
        }
        ASSERT( OcFilterSendIrpToLowerSkipStack == IrpDecision || 
                OcFilterSendIrpToLowerDontSkipStack == IrpDecision );

    }//if( NULL != PtrConnectionObject )

    //
    // if the stack location must be skipped - skip it,
    // if it must be copied to next, then it must has been 
    // copied by those who made this decision because copying 
    // now will remove the completion routine
    //
    if( OcFilterSendIrpToLowerSkipStack == IrpDecision )
        IoSkipCurrentIrpStackLocation (Irp);

    status = IoCallDriver( deviceExtension->NextLowerDriver, Irp );
    IoReleaseRemoveLock( &deviceExtension->RemoveLock, Irp ); 
    return status;
}

//-------------------------------------------------

NTSTATUS
FilterDispatchPnp (
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
/*++

Routine Description:

    The plug and play dispatch routines.

    Most of these the driver will completely ignore.
    In all cases it must pass on the IRP to the lower driver.

Arguments:

   DeviceObject - pointer to a device object.

   Irp - pointer to an I/O Request Packet.

Return Value:

      NT status code

--*/
{
    PDEVICE_EXTENSION         deviceExtension;
    PDEVICE_OBJECT            Pdo;
    PIO_STACK_LOCATION        irpStack;
    NTSTATUS                  status;
    OC_FILTER_IRP_DECISION    IrpDecision = OcFilterSendIrpToLowerSkipStack;

    PAGED_CODE();

    deviceExtension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;
    irpStack = IoGetCurrentIrpStackLocation(Irp);

    DebugPrint(("FilterDO %s IRP:0x%p \n",
                PnPMinorFunctionString(irpStack->MinorFunction), Irp));

    status = IoAcquireRemoveLock( &deviceExtension->RemoveLock, Irp );
    if( !NT_SUCCESS( status ) ){
        Irp->IoStatus.Status = status;
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    Pdo = deviceExtension->PhysicalDeviceObject;

    switch( irpStack->MinorFunction ){
    case IRP_MN_START_DEVICE:
        {
            OC_FILTER_IRP_DECISION    IrpDecision;

            //
            // The device is starting.
            // We cannot touch the device (send it any non pnp irps) until a
            // start device has been passed down to the lower drivers.
            //
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine( Irp,
                                    (PIO_COMPLETION_ROUTINE) FilterStartCompletionRoutine,
                                    NULL,
                                    TRUE,
                                    TRUE,
                                    TRUE);

            status = OcFilterPreStartCallback( deviceExtension,
                                               Irp,
                                               &IrpDecision );
            if( OcFilterReturnCode == IrpDecision ){

                //
                // only this code is processed, this code means that the 
                // device start IRP has been completed with an error
                // or has been pended. If the IRP has been completed
                // with an error the completion routine HAS NOT BEEN called!
                // So, if some resources must be freed in completion 
                // routine they MUST be freed here!
                //
                ASSERT( !NT_SUCCESS( status ) || STATUS_PENDING == status );
                return status;
            }

            //
            // the completion rotine has been set by this function!
            //
            ASSERT( OcFilterSendIrpToLowerDontSkipStack == IrpDecision );

            status = IoCallDriver(deviceExtension->NextLowerDriver, Irp);
            return status;
        }

    case IRP_MN_REMOVE_DEVICE:
        {

            PDEVICE_EXTENSION    devicePdoExtension;
            PDEVICE_OBJECT       Pdo;

            //
            // synchronize the code with OcFilterFastIoDetachDevice
            //

            //
            // Wait for all outstanding requests to complete
            //
            DebugPrint(("Waiting for outstanding requests\n"));
            IoReleaseRemoveLockAndWait(&deviceExtension->RemoveLock, Irp);

            //
            // Remove device from the list after releasing
            // the remove lock, this preserves the lock
            // hierarchy. See the OcFilterReportExisitingDevices 
            // routine.
            //
            OcFilterRemoveDeviceFromTheList( deviceExtension );

            //
            // free all device's relations
            //
            OcFreeAllDeviceRelations( deviceExtension );

            IoSkipCurrentIrpStackLocation(Irp);

            status = IoCallDriver( deviceExtension->NextLowerDriver, Irp );

            SET_NEW_PNP_STATE( deviceExtension, Deleted );
            OcFilterReportNewDeviceState( deviceExtension );

            Pdo = deviceExtension->PhysicalDeviceObject;

            //
            // save the pointer to the PDO extension
            //
            devicePdoExtension = deviceExtension->VirtualPdoExtension;
            if( NULL != devicePdoExtension )
                OcFilterReferenceVirtualPdoExtension( devicePdoExtension );

            //
            // clear device's extension before removing the device
            //
            FilterClearFilterDeviceExtension( deviceExtension );

            IoDetachDevice( deviceExtension->NextLowerDriver );
            IoDeleteDevice( DeviceObject );

            {// start of Virtual PDO removing

                //
                // remove the virtual PDO for the FiDO
                //

                //
                // the last attempt to find the PDO
                //
                if( NULL == devicePdoExtension )
                    devicePdoExtension = OcFilterReturnReferencedPdoExtensionLockIf( Pdo, FALSE );

                ASSERT( NULL != devicePdoExtension );
                if( NULL != devicePdoExtension ){

                    //
                    // delete the virtual PDO
                    //
                    OcFilterDeleteVirtualPdoByExtension( devicePdoExtension );

                    //
                    // free the reference which was made either when 
                    // the pointer was retrieved from filter extension 
                    // or in OcFilterReturnReferencedPdoExtensionLockIf
                    //
                    OcFilterDereferenceVirtualPdoExtension( devicePdoExtension );

                }

            }// end of Virtual PDO removing

        }

        return status;


    case IRP_MN_QUERY_STOP_DEVICE:
        SET_NEW_PNP_STATE(deviceExtension, StopPending);
        OcFilterReportNewDeviceState( deviceExtension );
        status = STATUS_SUCCESS;
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
        }
        OcFilterReportNewDeviceState( deviceExtension );
        status = STATUS_SUCCESS; // We must not fail this IRP.
        break;

    case IRP_MN_STOP_DEVICE:
        SET_NEW_PNP_STATE(deviceExtension, Stopped);
        OcFilterReportNewDeviceState( deviceExtension );
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_REMOVE_DEVICE:

        SET_NEW_PNP_STATE( deviceExtension, RemovePending );
        OcFilterReportNewDeviceState( deviceExtension );
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_SURPRISE_REMOVAL:

        SET_NEW_PNP_STATE( deviceExtension, SurpriseRemovePending );
        OcFilterReportNewDeviceState( deviceExtension );
        status = STATUS_SUCCESS;
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
        }
        OcFilterReportNewDeviceState( deviceExtension );
        status = STATUS_SUCCESS; // We must not fail this IRP.
        break;

    case IRP_MN_DEVICE_USAGE_NOTIFICATION:

        //
        // On the way down, pagable might become set. Mimic the driver
        // above us. If no one is above us, just set pagable.
        //
        if( ( NULL == DeviceObject->AttachedDevice ) ||
            ( DeviceObject->AttachedDevice->Flags & DO_POWER_PAGABLE ) ){

            DeviceObject->Flags |= DO_POWER_PAGABLE;
        }

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
        }

        //
        // report device usage request to a registered client
        //
        OcFilterReportReceivingDeviceUsageNotification( deviceExtension,
                                                        (ULONG_PTR)Irp,
                                                        irpStack->Parameters.UsageNotification.Type,
                                                        irpStack->Parameters.UsageNotification.InPath,
                                                        Irp->AssociatedIrp.SystemBuffer );

        IoCopyCurrentIrpStackLocationToNext( Irp );
        IoSetCompletionRoutine( Irp,
                                FilterDeviceUsageNotificationCompletionRoutine,
                                NULL,
                                TRUE, TRUE, TRUE );

        return IoCallDriver( deviceExtension->NextLowerDriver, Irp );

    case IRP_MN_QUERY_DEVICE_RELATIONS:

        if( IoGetCurrentIrpStackLocation( Irp )->Parameters.QueryDeviceRelations.Type >= 
            OC_STATIC_ARRAY_SIZE( deviceExtension->DeviceRelations ) ){
                //
                // save the IRP status as is, usually this is a bogus request to
                // check the correctnes of the drivers stack
                //
                status = Irp->IoStatus.Status;
                break;
        }

        IoCopyCurrentIrpStackLocationToNext( Irp );

        IoSetCompletionRoutine( Irp,
                                FilterQueryDeviceRelationsCompletionRoutine,
                                (PVOID)(IoGetCurrentIrpStackLocation( Irp )->Parameters.QueryDeviceRelations.Type),
                                TRUE, TRUE, TRUE );

        return IoCallDriver( deviceExtension->NextLowerDriver, Irp );

    default:
        {
            POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;
            PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
            if( NULL != PtrConnectionObject ){

                NTSTATUS                  RC;

                RC = PtrConnectionObject->CallbackMethods.DispatcherCallback( DeviceObject,
                                                                              deviceExtension->NextLowerDriver,
                                                                              Irp,
                                                                              &IrpDecision );

                OcObDereferenceObject( PtrConnectionObject );
                if( OcFilterReturnCode == IrpDecision ){

                    IoReleaseRemoveLock( &deviceExtension->RemoveLock, Irp );
                    return RC;
                }
                ASSERT( OcFilterSendIrpToLowerSkipStack == IrpDecision || 
                        OcFilterSendIrpToLowerDontSkipStack == IrpDecision );

            }//if( NULL != PtrConnectionObject )

            //
            // If you don't handle any IRP you must leave the
            // status as is.
            //
            status = Irp->IoStatus.Status;

            break;
        }
    }

    //
    // Pass the IRP down and forget it.
    //
    Irp->IoStatus.Status = status;
    if( OcFilterSendIrpToLowerSkipStack == IrpDecision )
        IoSkipCurrentIrpStackLocation (Irp);

    status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
    IoReleaseRemoveLock( &deviceExtension->RemoveLock, Irp ); 
    return status;
}

//-------------------------------------------------

NTSTATUS
FilterStartCompletionRoutine(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
/*++
Routine Description:
    A completion routine for use when calling the lower device objects to
    which our filter deviceobject is attached.
    The caller has acquired the device remove lock which must be freed
    by this function.

Arguments:

    DeviceObject - Pointer to deviceobject
    Irp          - Pointer to a PnP Irp.
    Context      - NULL
Return Value:

    NT Status is returned.

--*/

{

    PDEVICE_EXTENSION    deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    //
    // mark the Irp as pending if necessary
    //
    if( Irp->PendingReturned )
        IoMarkIrpPending( Irp );

    //
    // if there is not the PASSIVE_LEVEL then defer processing in the worker thread
    //
    if( OCFLT_POSTPONE_IF && ( KeGetCurrentIrql() > PASSIVE_LEVEL ) ){

        POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject;

        PtrWorkItemListObject = OcTplReferenceSharedWorkItemList( PnpFilterGlobal.ThreadsPoolObject );
        if( NULL != PtrWorkItemListObject ){

            NTSTATUS    RC;

            RC = OcWthPostWorkItemParam3( PtrWorkItemListObject,
                                     FALSE,
                                     (Param3SysProc)FilterStartCompletionRoutine,
                                     (ULONG_PTR)DeviceObject,
                                     (ULONG_PTR)Irp,
                                     (ULONG_PTR)Context );

            OcObDereferenceObject( PtrWorkItemListObject );

            //
            // if posting in the worker thread fails, then
            // complete the request or the system will hang
            //
            if( NT_SUCCESS( RC ) ){

                //
                // this is a successful branch!
                //
                return STATUS_MORE_PROCESSING_REQUIRED;
            }

            //
            // this is an error branch, complete the request
            // to unblock the PnP manager
            //
            ASSERT( !NT_SUCCESS( RC ) );
        }

        //
        // if we are here something went wrong and the request has
        // not been sent in the worker thread
        //
        ASSERT(!"PnPfilter : sending in the worker thread failed");

    } else if( NT_SUCCESS( Irp->IoStatus.Status ) ){

        ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

        //
        // As we are successfully now back, we will
        // first set our state to Started.
        //

        SET_NEW_PNP_STATE( deviceExtension, Started );

        //
        // set the state for the Pdo as PDO driver has been hooked
        // and I can't rely on hooking due to the possible unhook,
        // P.S. Do not lock the PDO extension as it is protected by
        // the FiDO lock
        //
        /*
        pdoDeviceExtension = OcFilterReturnPdoExtensionLockIf( deviceExtension->PhysicalDeviceObject, FALSE );
        if( NULL != pdoDeviceExtension ){
Revamp this!
            SET_NEW_PNP_STATE( pdoDeviceExtension, Started );
            
        } if( NULL != pdoDeviceExtension )
        */

        //
        // On the way up inherit FILE_REMOVABLE_MEDIA during Start.
        // This characteristic is available only after the driver stack is started!.
        //
        if( deviceExtension->NextLowerDriver->Characteristics & FILE_REMOVABLE_MEDIA ){

            DeviceObject->Characteristics |= FILE_REMOVABLE_MEDIA;
        }

        OcFilterReportNewDeviceState( deviceExtension );
        //
        // do not report the PDO state as the client should set
        // the PDO state acording to this FiDO new state
        //

    }//if( NT_SUCCESS( Irp->IoStatus.Status ) )

    IoCompleteRequest( Irp, IO_NO_INCREMENT );
    IoReleaseRemoveLock( &deviceExtension->RemoveLock, Irp );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Irp );

    //
    // I have already called the IoCompleteRequest
    //

    return STATUS_MORE_PROCESSING_REQUIRED;

}

//-------------------------------------------------

NTSTATUS
FilterQueryDeviceRelationsCompletionRoutine(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
/*++
Routine Description:
    A completion routine for use when calling the lower device objects to
    which our filter deviceobject is attached.
    The caller has acquired the device remove lock and this lock
    must be released in this function.

Arguments:

    DeviceObject - Pointer to deviceobject
    Irp          - Pointer to a PnP Irp.
    Context      - DEVICE_RELATION_TYPE
Return Value:

    NT Status is returned.

--*/

{
    PDEVICE_EXTENSION    deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    //
    // mark the Irp as pending if necessary
    //
    if( Irp->PendingReturned )
        IoMarkIrpPending( Irp );

    if( !NT_SUCCESS( Irp->IoStatus.Status ) )
        goto __continue_complete;

    //
    // if there is not the PASSIVE_LEVEL then defer processing in the worker thread
    //
    if( OCFLT_POSTPONE_IF && ( KeGetCurrentIrql() > PASSIVE_LEVEL ) ){

        POC_WORK_ITEM_LIST_OBJECT    PtrWorkItemListObject;

        PtrWorkItemListObject = OcTplReferenceSharedWorkItemList( PnpFilterGlobal.ThreadsPoolObject );
        if( NULL != PtrWorkItemListObject ){

            NTSTATUS    RC;

            RC = OcWthPostWorkItemParam3( PtrWorkItemListObject,
                                     FALSE,
                                     (Param3SysProc)FilterQueryDeviceRelationsCompletionRoutine,
                                     (ULONG_PTR)DeviceObject,
                                     (ULONG_PTR)Irp,
                                     (ULONG_PTR)Context );

            OcObDereferenceObject( PtrWorkItemListObject );

            //
            // if posting in the worker thread fails, then
            // complete request or system will hang
            //
            if( NT_SUCCESS( RC ) ){

                //
                // this is a successful branch!
                //
                return STATUS_MORE_PROCESSING_REQUIRED;
            }

            //
            // this is an error branch, complete the request
            // to unblock the PnP manager
            //
            ASSERT( !NT_SUCCESS( RC ) );
        }

        //
        // if we are here something went wrong and the request has
        // not been sent in the worker thread
        //
        ASSERT(!"PnPfilter : sending in the worker thread failed");

    } else {

        OcFilterSetNewDeviceRelations( deviceExtension,
                                 (PDEVICE_RELATIONS)Irp->IoStatus.Information,
                                 (DEVICE_RELATION_TYPE)Context );
    }

__continue_complete:

    IoCompleteRequest( Irp, IO_NO_INCREMENT );
    IoReleaseRemoveLock( &deviceExtension->RemoveLock, Irp ); 

    //
    // I have already called the IoCompleteRequest
    //

    return STATUS_MORE_PROCESSING_REQUIRED;

}

//-------------------------------------------------

NTSTATUS
FilterDeviceUsageNotificationCompletionRoutine(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
/*++
Routine Description:
    A completion routine for use when calling the lower device objects to
    which our filter deviceobject is attached.

Arguments:

    DeviceObject - Pointer to deviceobject
    Irp          - Pointer to a PnP Irp.
    Context      - NULL
Return Value:

    NT Status is returned.

--*/

{
    PDEVICE_EXTENSION       deviceExtension;

    UNREFERENCED_PARAMETER(Context);

    deviceExtension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;


    if (Irp->PendingReturned) {

        IoMarkIrpPending(Irp);
    }

    //
    // On the way up, pagable might become clear. Mimic the driver below us.
    //
    if (!(deviceExtension->NextLowerDriver->Flags & DO_POWER_PAGABLE)) {

        DeviceObject->Flags &= ~DO_POWER_PAGABLE;
    }

    OcFilterReportCompletingDeviceUsageNotification( deviceExtension,
                                                     (ULONG_PTR)Irp,
                                                     &Irp->IoStatus );

    IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 

    return STATUS_CONTINUE_COMPLETION;

}

//-------------------------------------------------

NTSTATUS
FilterDispatchPower(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP              Irp
    )
/*++

Routine Description:

    This routine is the dispatch routine for power irps.

Arguments:

    DeviceObject - Pointer to the device object.

    Irp - Pointer to the request packet.

Return Value:

    NT Status code
--*/
{
    PDEVICE_EXTENSION   deviceExtension;
    NTSTATUS    status;

    deviceExtension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;
    status = IoAcquireRemoveLock (&deviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS (status)) { // may be device is being removed.
        Irp->IoStatus.Status = status;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest (Irp, IO_NO_INCREMENT);
        return status;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    status = PoCallDriver(deviceExtension->NextLowerDriver, Irp);
    IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
    return status;
}

//-------------------------------------------------

VOID
OcFilterSetNewDeviceRelations(
    IN PDEVICE_EXTENSION    deviceExtension,
    IN PDEVICE_RELATIONS    PtrNewRelations,
    IN DEVICE_RELATION_TYPE    DeviceRelationsType
    )
{
    const static ULONG          MemoryTag = 'rDcO';
    PDEVICE_RELATIONS           PtrAllocDeviceRelations = NULL;
    PDEVICE_RELATIONS           PtrOldDeviceRelations = NULL;
    PDEVICE_RELATIONS_SHADOW    PtrOldDeviceRelationsShadow = NULL;
    ULONG                       SizeOfStructure = 0x0;
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    if( DeviceRelationsType >= OC_STATIC_ARRAY_SIZE( deviceExtension->DeviceRelations ) )
        return;

    if( NULL != PtrNewRelations )
        SizeOfStructure = FIELD_OFFSET( DEVICE_RELATIONS, Objects ) + 
        sizeof( PtrNewRelations->Objects[ 0x0 ])*PtrNewRelations->Count;

    if( 0x0 != SizeOfStructure )
        PtrAllocDeviceRelations = ExAllocatePoolWithTag( PagedPool, SizeOfStructure, MemoryTag );

    //
    // do not exit on allocation error, I must free the old relations in any case
    //

    if( NULL != PtrAllocDeviceRelations )
        RtlCopyMemory( PtrAllocDeviceRelations, PtrNewRelations, SizeOfStructure );

    //
    // save the new device relations
    //
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite( &deviceExtension->RelationResource, TRUE );
    {//start of the lock

        PtrOldDeviceRelations = deviceExtension->DeviceRelations[ DeviceRelationsType ];
        PtrOldDeviceRelationsShadow = deviceExtension->DeviceRelationsShadow[ DeviceRelationsType ];

        deviceExtension->DeviceRelations[ DeviceRelationsType ] = PtrAllocDeviceRelations;
        deviceExtension->DeviceRelationsShadow[ DeviceRelationsType ] = NULL;

    }//end of the lock
    ExReleaseResourceLite( &deviceExtension->RelationResource );
    KeLeaveCriticalRegion();

    //
    // add PDOs from the new relations to the list of "virtual devices"
    //
    if( NULL != PtrNewRelations ){

        PDEVICE_RELATIONS_SHADOW    NewDeviceRelationsShadow;

        NewDeviceRelationsShadow = 
            FilterProcessPdoInformationForRelation( PtrNewRelations, NULL, TRUE );

        ASSERT( NULL != NewDeviceRelationsShadow );

        //
        // save the new shadow relations
        //
        if( NULL != NewDeviceRelationsShadow ){

            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite( &deviceExtension->RelationResource, TRUE );
            {//start of the lock
                deviceExtension->DeviceRelationsShadow[ DeviceRelationsType ] = NewDeviceRelationsShadow;
            }//end of the lock
            ExReleaseResourceLite( &deviceExtension->RelationResource );
            KeLeaveCriticalRegion();

        }// if( NULL != DeviceRelationsShadow )
    }

    if( NULL != PtrOldDeviceRelations || NULL != PtrOldDeviceRelationsShadow){

        //
        // remove PDOs from the old relation from the "virtual devices" list
        //
        FilterProcessPdoInformationForRelation( PtrOldDeviceRelations,
                                                PtrOldDeviceRelationsShadow,
                                                FALSE );

        if( NULL != PtrOldDeviceRelations )
            ExFreePoolWithTag( (PVOID)PtrOldDeviceRelations, MemoryTag );

    }
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrOldDeviceRelations );

    //
    // report new relations, use the relations 
    // allocated by the lower drivers, because the 
    // PtrAllocDeviceRalations might be deleted through
    // the pointer in deviceExtension
    //
    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL != PtrConnectionObject ){

        //
        // use PtrNewRelations, because this memory will not be freed until this
        // function returns
        //
        PtrConnectionObject->CallbackMethods.RepotNewDeviceRelations(
            FilterGetDeviceToReportForExtension( deviceExtension ),
            DeviceRelationsType,
            PtrNewRelations );

        OcObDereferenceObject( PtrConnectionObject );
    }//if( NULL != PtrConnectionObject )
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrConnectionObject );

}

//-------------------------------------------------

VOID
OcFreeAllDeviceRelations(
    IN PDEVICE_EXTENSION    deviceExtension
    )
{
    int   i;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( deviceExtension->DeviceRelations ) ; ++i ){

        OcFilterSetNewDeviceRelations( deviceExtension,
                                 (PDEVICE_RELATIONS)NULL,
                                 (DEVICE_RELATION_TYPE)i );
    }
}

//-------------------------------------------------

VOID
OcFilterRemoveDeviceFromTheList(
    IN PDEVICE_EXTENSION   deviceExtension
    )
{
    KIRQL    OldIrql;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    //
    // it is not possible to remove the device from the list unles it remove lock is 
    // acquired for removal, see OcFilterReportExisitingDevices function - it presumes 
    // that if device's remove lock is acquire then the device will not be removed 
    // from the list
    //
    ASSERT( 0x1 == deviceExtension->RemoveLock.Common.Removed );

    KeAcquireSpinLock( &PnpFilterGlobal.SpinLock, &OldIrql );
    {//start of the lock
        RemoveEntryList( &deviceExtension->ListEntry );
    }//end of the lock
    KeReleaseSpinLock( &PnpFilterGlobal.SpinLock, OldIrql );

}

//-------------------------------------------------

VOID
OcFilterReportNewDeviceState(
    IN PDEVICE_EXTENSION    deviceExtension
    )
{
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( (DEVICE_TYPE)(-1) != deviceExtension->Type );

    //
    // call the registered callback
    //
    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL != PtrConnectionObject ){

        PtrConnectionObject->CallbackMethods.RepotNewDeviceState(
            FilterGetDeviceToReportForExtension( deviceExtension ),
            deviceExtension->DevicePnPState );

        OcObDereferenceObject( PtrConnectionObject );
    }//if( NULL != PtrConnectionObject )
}

//-------------------------------------------------

VOID
OcFilterReportPdoStateRelatedToPnpManager(
    __in PDEVICE_EXTENSION    deviceExtension
    )
    /*
    this function CAN be called only on the PnP path,
    because only this guarantees the syncronization with
    the other parts of the code and PnP manager
    */
{

    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( (DEVICE_TYPE)(-1) != deviceExtension->Type );

    //
    // call the registered callback
    //
    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL != PtrConnectionObject ){

        //
        // report a "new" device, the PDO is considered as an initialized one if
        // either the attached device presents( i.s. self device object is not NULL )
        // or the PDO device state is not NotStarted and not Deleted
        //
        PtrConnectionObject->CallbackMethods.ReportNewDevice( 
                deviceExtension->PhysicalDeviceObject,
                deviceExtension->Self,// may be NULL if the extension describes the "virtual device"
                deviceExtension->PnPDeviceType,
                ( NULL != deviceExtension->Self || 
                 ( NotStarted != deviceExtension->DevicePnPState && 
                   Deleted != deviceExtension->DevicePnPState ) ) );

        OcObDereferenceObject( PtrConnectionObject );
    }//if( NULL != PtrConnectionObject )

}

//-------------------------------------------------

VOID
OcFilterReportReceivingDeviceUsageNotification(
    IN PDEVICE_EXTENSION    deviceExtension,
    IN ULONG_PTR            RequstId,
    IN DEVICE_USAGE_NOTIFICATION_TYPE    Type,
    IN BOOLEAN              InPath,
    IN PVOID                Buffer OPTIONAL
    )
{
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // call the registered callback
    //
    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL != PtrConnectionObject ){

        PtrConnectionObject->CallbackMethods.UsageNotifyPreCallback(
            FilterGetDeviceToReportForExtension( deviceExtension ),
            RequstId,
            Type,
            InPath,
            Buffer );

        OcObDereferenceObject( PtrConnectionObject );
    }//if( NULL != PtrConnectionObject )
}

//-------------------------------------------------

VOID
OcFilterReportCompletingDeviceUsageNotification(
    IN PDEVICE_EXTENSION    deviceExtension,
    IN ULONG_PTR            RequstId,
    IN PIO_STATUS_BLOCK     StatusBlock
    )
{
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // call the registered callback
    //
    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL != PtrConnectionObject ){

        PtrConnectionObject->CallbackMethods.UsageNotifyPostCallback(
            FilterGetDeviceToReportForExtension( deviceExtension ),
            RequstId,
            StatusBlock );

        OcObDereferenceObject( PtrConnectionObject );
    }//if( NULL != PtrConnectionObject )
}

//-------------------------------------------------

NTSTATUS
OcFilterPreStartCallback(
    IN PDEVICE_EXTENSION    deviceExtension,
    IN PIRP    Irp,
    OUT POC_FILTER_IRP_DECISION     PtrIrpDecision
    )
{
    NTSTATUS    RC = STATUS_SUCCESS;
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    *PtrIrpDecision = OcFilterSendIrpToLowerDontSkipStack;

    //
    // call the registered callback
    //
    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL != PtrConnectionObject ){

        //
        // pre start callback is not compulsory
        //
        if( NULL != PtrConnectionObject->CallbackMethods.PreStartCallback ){

            RC = PtrConnectionObject->CallbackMethods.PreStartCallback( 
                FilterGetDeviceToReportForExtension( deviceExtension ),
                deviceExtension->NextLowerDriver,
                Irp,
                PtrIrpDecision );
        }

        OcObDereferenceObject( PtrConnectionObject );
    }//if( NULL != PtrConnectionObject )

    return RC;
}

//-------------------------------------------------

VOID
NTAPI
OcFilterReportExisitingDevices()
{
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;
    PLIST_ENTRY                      request = &PnpFilterGlobal.ListHead;
    PDEVICE_EXTENSION                deviceExtension = NULL;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // call the registered callbacks to emulate
    // a new device arrival
    //

    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL == PtrConnectionObject )
        return;

    //
    // go through the list
    //
    while( TRUE ){

        KIRQL                OldIrql;
        DEVICE_PNP_STATE     DevicePnPState;

        KeAcquireSpinLock( &PnpFilterGlobal.SpinLock, &OldIrql );
        {//begin of the lock

            //
            // deviceExtension can be NULL only at the start of the loop
            //
            ASSERT( ( NULL == deviceExtension ) ? ( &PnpFilterGlobal.ListHead == request ) : ( &PnpFilterGlobal.ListHead != request ) );

            //
            // release the device lock for the previous reported device, 
            // why I do this while the lock is acquired see the IRP_MN_REMOVE_DEVICE code
            //
            if( NULL != deviceExtension ){

                IoReleaseRemoveLock( &deviceExtension->RemoveLock, NULL );
                deviceExtension = NULL;
            }

            //
            // get the device and acquire its remove lock
            // skip devices in a removing state
            //
            for( request = request->Flink ; 
                 request != &PnpFilterGlobal.ListHead; 
                 request = request->Flink ){

                //
                // get the extension for the device
                //
                deviceExtension = CONTAINING_RECORD( request,
                                                     DEVICE_EXTENSION,
                                                     ListEntry );

                //
                // acquire the remove lock while the spin lock is helded
                // the remove lock disables the device removal,
                // if the device in the removing state the
                // STATUS_DELETE_PENDING code is usually returned
                //
                if( !NT_SUCCESS( IoAcquireRemoveLock( &deviceExtension->RemoveLock, NULL ) ) ){

                    //
                    // set extension to NULL, so the 
                    // IoReleaseRemoveLock will not be called.
                    // 23.01.2007 - actually this assignment is 
                    // not needed, but saved here for safety
                    //
                    deviceExtension = NULL;

                    //
                    // the entry is being removed, skip it
                    //
                    continue;
                }//if( STATUS_DELETE_PENDING == LockRC )

                //
                // the entry with the valid device has been found and locked, 
                // report this device
                //
                break;
            }//for

        }//end of the lock
        KeReleaseSpinLock( &PnpFilterGlobal.SpinLock, OldIrql );
        if( request == &PnpFilterGlobal.ListHead )
            break;

        //
        // Now the device entry in the list is locked, because the
        // entry is removed only after IoReleaseRemoveLockAndWait.
        // The device will be valid until IoReleaseRemoveLock
        // is called.
        //

        //
        // Devices in the list are in a topological order!
        // The order is the order in that AddDevice is called!
        //

        //
        // Read the device state until it not changed,
        // because this code is not syncronized
        // with the code changing the DevicePnPState
        // field.
        // The callee must be cognizant
        // about this and properly processes the 
        // situation when another thread sneaks in 
        // and changes and reports a new device state between
        // the loop and RepotNewDeviceState call.
        //
        DevicePnPState = deviceExtension->DevicePnPState;
        while( DevicePnPState != deviceExtension->DevicePnPState )
            DevicePnPState = deviceExtension->DevicePnPState;

        //
        // report a "new" device, the PDO is considered as an initialized one if
        // either the attached device presents( i.s. self device object is not NULL )
        // or the PDO device state is not NotStarted and not Deleted
        //
        PtrConnectionObject->CallbackMethods.ReportNewDevice( 
            deviceExtension->PhysicalDeviceObject,
            deviceExtension->Self,// may be NULL if the extension describes the "virtual device"
            deviceExtension->PnPDeviceType,
            ( NULL != deviceExtension->Self || 
              ( NotStarted != DevicePnPState && Deleted != DevicePnPState ) ) );

        //
        // report the current device state,
        // read it until it not changed,
        // because this code is not syncronized
        // with the code changing the DevicePnPState
        // field. The callee must be cognizant
        // about this and properly processes the 
        // situation when another thread sneaks in 
        // and changes and reports a new device state between
        // the loop and RepotNewDeviceState call.
        //
        DevicePnPState = deviceExtension->DevicePnPState;
        while( DevicePnPState != deviceExtension->DevicePnPState )
            DevicePnPState = deviceExtension->DevicePnPState;

        PtrConnectionObject->CallbackMethods.RepotNewDeviceState( FilterGetDeviceToReportForExtension( deviceExtension ),
                                                                  DevicePnPState );

        //
        // Report the device relations.
        //
        KeEnterCriticalRegion();
        ExAcquireResourceSharedLite( &deviceExtension->RelationResource, TRUE );
        {
            ULONG    i;

            for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( deviceExtension->DeviceRelations ) ; ++i ){

                PtrConnectionObject->CallbackMethods.RepotNewDeviceRelations(
                    FilterGetDeviceToReportForExtension( deviceExtension ),
                    (DEVICE_RELATION_TYPE)i,
                    deviceExtension->DeviceRelations[ i ] );
            }//for
        }
        ExReleaseResourceLite( &deviceExtension->RelationResource );
        KeLeaveCriticalRegion();

        //
        // report the device usage only for the paging file usage
        //
        if( 0x1 == deviceExtension->DeviceUsage.DeviceUsageTypePaging ){

            IO_STATUS_BLOCK     StatusBlock;

            StatusBlock.Status = STATUS_SUCCESS;
            StatusBlock.Information = 0x0;

            OcFilterReportReceivingDeviceUsageNotification( deviceExtension,
                                                            0x0,
                                                            DeviceUsageTypePaging,
                                                            TRUE,
                                                            NULL );

            OcFilterReportCompletingDeviceUsageNotification( deviceExtension,
                                                             0x0,
                                                             &StatusBlock );
        }

    }//while( TRUE )

    OcObDereferenceObject( PtrConnectionObject );
}

//-------------------------------------------------

NTSTATUS
NTAPI
OcFilterAddDeviceFTI(
    __in PDEVICE_OBJECT    Pdo
    )
    /*
    This is a callback used by the driver which
    loads this driver at a first time, i.e. after the 
    system has started and initialized all PnP 
    tree nodes.
    The function must be called from the system
    thread context, this prerequisite helps
    to avoid some complications and makes the 
    process to look like a standard device 
    initialization launched by the PnP manager
    */
{
    NTSTATUS    RC;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );
    ASSERT( NULL != Pdo );
    ASSERT( OcFilterIsPdo( Pdo ) );

    //
    // add the PDO information in the database
    //
    RC = FilterAddDeviceEx( PnpFilterGlobal.DriverObject,
                            Pdo,
                            OcDevicePnPTypePdo,
                            NULL,
                            FALSE );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // attach an FiDO to the device stack
    //
    RC = FilterAddDeviceEx( PnpFilterGlobal.DriverObject,
                            Pdo,
                            OcDevicePnPTypeFilterDo,
                            NULL,
                            FALSE );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // invalidate the device relations, the system
    // will send the BusRelation request and the
    // function returns after the request being processed,
    // the system doesn't have to send this request in
    // the current thread context
    //
    PnpFilterGlobal.IoSynchronousInvalidateDeviceRelations( Pdo,
                                                            BusRelations );

__exit:
    return RC;
}

//-------------------------------------------------
