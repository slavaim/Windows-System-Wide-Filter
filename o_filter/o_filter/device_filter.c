/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
09.05.2007 ( May )
 Start
*/

/*
this file contains the code for processing
request to devices that have been attached by this driver
through the request from the core driver but not from
the system's PnP manager
*/

#include "filter.h"
#include "proto.h"

//-------------------------------------------------

VOID
NTAPI
OcFilterFastIoDetachDevice(
    IN struct _DEVICE_OBJECT *SourceDevice,
    IN struct _DEVICE_OBJECT *TargetDevice
    )
    /*
    This functon is called by the system for each device object
    to which this driver has attached
    It is invoked on the fast path to detach from a device that
    is being deleted. This occurs when this driver has attached to a file
    system volume device object, and then, for some reason, the file system
    decides to delete that device (it is being dismounted, it was dismounted
    at some point in the past and its last reference has just gone away, etc.)

    SourceDevice - this driver's device object
    TargetDevice - device to be deleted

    Synchronizw this function's code with IRP_MN_REMOVE_DEVICE code

    */
{

    PDEVICE_EXTENSION    deviceExtension;
    PDEVICE_EXTENSION    devicePdoExtension;
    PDEVICE_OBJECT       Pdo = NULL;
    NTSTATUS             status;
    KIRQL                OldIrql;
    PLIST_ENTRY          request;

    //
    // synchronize with IRP_MN_REMOVE_DEVICE, the device might being removed now
    // in the code processing IRP_MN_REMOVE_DEVICE request, note
    // that there is no race between this code and IRP_MN_REMOVE_DEVICE code 
    // because if OcFilterRemoveDeviceFromTheList has been called then I will be 
    // unable to find the device in the list and note that OcFilterRemoveDeviceFromTheList 
    // is always called before calling an underlying device's driver which calls 
    // IoDeleteDevice which in turns calls this function. Pay attention that it is not possible
    // to remove the device from the list unless its remove lock has been acquired for removal,see
    // OcFilterReportExisitingDevices function - it presumes that if device's remove
    // lock is acquire then the device will not be removed from the list
    //

    KeAcquireSpinLock( &PnpFilterGlobal.SpinLock, &OldIrql );
    {//begin of the lock

        for( request = PnpFilterGlobal.ListHead.Flink; request != &PnpFilterGlobal.ListHead; request = request->Flink ){

            //
            // get the extension for the device
            //
            deviceExtension = CONTAINING_RECORD( request, DEVICE_EXTENSION, ListEntry );
            if( deviceExtension->Self == SourceDevice )
                break;

        }//for

    }//end of the lock
    KeReleaseSpinLock( &PnpFilterGlobal.SpinLock, OldIrql );

    if( request == &PnpFilterGlobal.ListHead ){

        //
        // device has not been found in the list, so it is currently removed
        // ( or has been already destroyed ) in IRP_MN_REMOVE_DEVICE code
        //
        return;
    }

    ASSERT( SourceDevice->DriverObject == PnpFilterGlobal.DriverObject );
    ASSERT( deviceExtension == (PDEVICE_EXTENSION) SourceDevice->DeviceExtension );
    ASSERT( DEVICE_TYPE_FIDO_NO_PNP == deviceExtension->Type );

    //
    // only devices attached in response to requests from an external client
    // are processed here
    //
    if( DEVICE_TYPE_FIDO_NO_PNP != deviceExtension->Type )
        return;

    ASSERT( DEVICE_TYPE_FIDO_NO_PNP == deviceExtension->Type &&
            OcDeviceNoPnPTypeInMiddleOfStack == deviceExtension->PnPDeviceType );

    status = IoAcquireRemoveLock (&deviceExtension->RemoveLock, NULL );
    ASSERT( NT_SUCCESS( status ) );
    if( NT_SUCCESS( status ) ){
        //
        // Wait for all outstanding requests to complete
        //
        DebugPrint(("Waiting for outstanding requests\n"));
        IoReleaseRemoveLockAndWait( &deviceExtension->RemoveLock, NULL );

    } else {

        return;
    }

    //
    // Remove device from the list after releasing
    // the remove lock, this preserves the lock
    // hierarchy. See theOcFilterReportExisitingDevices 
    // routine.
    //
    OcFilterRemoveDeviceFromTheList( deviceExtension );

    //
    // free all device's relations
    //
    OcFreeAllDeviceRelations( deviceExtension );

    //
    // remove the device
    //
    SET_NEW_PNP_STATE( deviceExtension, Deleted );
    OcFilterReportNewDeviceState( deviceExtension );

    //
    // save the pointer to the PDO extension
    //
    devicePdoExtension = deviceExtension->VirtualPdoExtension;
    if( NULL != devicePdoExtension )
        OcFilterReferenceVirtualPdoExtension( devicePdoExtension );
    else
        Pdo = devicePdoExtension->PhysicalDeviceObject;

    ASSERT( NULL != devicePdoExtension );

    //
    // clear device's extension before removing the device
    //
    FilterClearFilterDeviceExtension( deviceExtension );

    IoDetachDevice( deviceExtension->NextLowerDriver );
    IoDeleteDevice( SourceDevice );

    if( NULL != devicePdoExtension ){

        //
        // delete the virtual PDO
        //
        OcFilterDeleteVirtualPdoByExtension( devicePdoExtension );

        //
        // dereference the extension
        //
        OcFilterDereferenceVirtualPdoExtension( devicePdoExtension );

    } else if( NULL != Pdo ){

        OcFilterDeleteVirtualPdo( Pdo );
    }

}

//-------------------------------------------------

NTSTATUS
NTAPI
OcFilterAttachToDevice(
    IN PDEVICE_OBJECT    DeviceObject
    )
    /*
    the function is unsafe in the relation to 
    synchronization with the device stack tearing down,
    so the caller MUST provide this guarantee
    */
{
    NTSTATUS    RC;
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;
    PDEVICE_OBJECT       FilterDeviceObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
    if( NULL == PtrConnectionObject )
        return STATUS_INVALID_DEVICE_REQUEST;

    RC = FilterAddDeviceEx( PnpFilterGlobal.DriverObject,
                            DeviceObject,
                            OcDeviceNoPnPTypeInMiddleOfStack,
                            &FilterDeviceObject,
                            FALSE );
    if( NT_SUCCESS( RC ) ){

        PDEVICE_EXTENSION    deviceExtension;

        ASSERT( IO_TYPE_DEVICE == FilterDeviceObject->Type );
        ASSERT( FilterDeviceObject->DriverObject == PnpFilterGlobal.DriverObject );

        deviceExtension = (PDEVICE_EXTENSION)FilterDeviceObject->DeviceExtension;

        //
        // As we are successfully now back, we will
        // first set our state to Started.
        //

        SET_NEW_PNP_STATE( deviceExtension, Started );

        //
        // On the way up inherit FILE_REMOVABLE_MEDIA during Start.
        // This characteristic is available only after the driver stack is started!.
        //
        if( deviceExtension->NextLowerDriver->Characteristics & FILE_REMOVABLE_MEDIA ){

            DeviceObject->Characteristics |= FILE_REMOVABLE_MEDIA;
        }

        OcFilterReportNewDeviceState( deviceExtension );
    }

    OcObDereferenceObject( PtrConnectionObject );

    return RC;
}

//-------------------------------------------------

