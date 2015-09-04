
/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/

#include "filter.h"
#include "proto.h"

//-------------------------------------------------

PNP_FILTER_GLOBAL    PnpFilterGlobal;

//-------------------------------------------------

static
VOID
OcFilterDeleteConnectObject(
    IN POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectObject
    );

static
NTSTATUS
OcFilterConnectionRequired( 
    IN POC_PNP_FILTER_CONNECT_INITIALIZER    PtrInputInitializer
    );

VOID
NTAPI
OcFilterDisconnect(
    IN PVOID    Context
    );

static
NTSTATUS
OcInitFilterConnectionForFirstTimeStart(
    __in POC_PNP_FILTER_FIRST_TIME_START    FirstTimeStartStruct
    );

//-------------------------------------------------

NTSTATUS
OcCreatePnPFilterControlObject()
{
    UNICODE_STRING      ntDeviceName;
    UNICODE_STRING      symbolicLinkName;
    NTSTATUS            RC = STATUS_UNSUCCESSFUL;
    UNICODE_STRING      sddlString;
    PCONTROL_DEVICE_EXTENSION   deviceExtension;

    PAGED_CODE();

    ASSERT( NULL != PnpFilterGlobal.DriverObject );
    ASSERT( NULL == PnpFilterGlobal.ControlDeviceObject );

    //
    // Initialize the unicode strings
    //
    RtlInitUnicodeString( &ntDeviceName, NTDEVICE_NAME_STRING_PNPFILTER );
    RtlInitUnicodeString( &symbolicLinkName, SYMBOLIC_NAME_STRING_PNPFILTER );

    //
    // Initialize a security descriptor string. Refer to SDDL docs in the SDK
    // for more info.
    //
    RtlInitUnicodeString( &sddlString, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

    //
    // Create a named deviceobject so that applications or drivers
    // can directly talk to us without going throuhg the entire stack.
    // This call could fail if there are not enough resources or
    // another deviceobject of same name exists (name collision).
    // Let us use the new IoCreateDeviceSecure and specify a security
    // descriptor (SD) that allows only System and Admin groups to access the 
    // control device. Let us also specify a unique guid to allow administrators 
    // to change the SD if he desires to do so without changing the driver. 
    // The SD will be stored in 
    // HKLM\SYSTEM\CCSet\Control\Class\<GUID>\Properties\Security.
    // An admin can override the SD specified in the below call by modifying
    // the registry.
    //

    RC = IoCreateDeviceSecure( PnpFilterGlobal.DriverObject,
                               sizeof(CONTROL_DEVICE_EXTENSION),
                               &ntDeviceName,
                               FILE_DEVICE_UNKNOWN,
                               FILE_DEVICE_SECURE_OPEN,
                               FALSE, 
                               &sddlString,
                               (LPCGUID)&GUID_SD_FILTER_CONTROL_OBJECT,
                               &PnpFilterGlobal.ControlDeviceObject);

    if( NT_SUCCESS( RC ) ){

        PnpFilterGlobal.ControlDeviceObject->Flags |= DO_BUFFERED_IO;

        RC = IoCreateSymbolicLink( &symbolicLinkName, &ntDeviceName );
        if( !NT_SUCCESS( RC ) ){

            IoDeleteDevice( PnpFilterGlobal.ControlDeviceObject );
            PnpFilterGlobal.ControlDeviceObject = NULL;
            DebugPrint( ( "IoCreateSymbolicLink failed %x\n", RC ) );
            goto __exit;
        }

        deviceExtension = PnpFilterGlobal.ControlDeviceObject->DeviceExtension;
        deviceExtension->Type = DEVICE_TYPE_CDO;
        deviceExtension->ControlData = NULL;
        deviceExtension->Deleted = FALSE;

        PnpFilterGlobal.ControlDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    }else {

        DebugPrint(("IoCreateDevice failed %x\n", RC ));
    }

__exit:
    ASSERT( NT_SUCCESS( RC ) );
    return RC;
}

//-------------------------------------------------

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

Arguments:

    DriverObject - pointer to the driver object

    RegistryPath - pointer to a unicode string representing the path,
                   to driver-specific key in the registry.

Return Value:

    STATUS_SUCCESS always!

--*/
{
    NTSTATUS            RC = STATUS_SUCCESS;
    ULONG               ulIndex;
    PDRIVER_DISPATCH*   dispatch;
    UNICODE_STRING      KernelFunctionName;
    OC_OBJECT_TYPE_INITIALIZER_VAR( TypeInitializer );

    UNREFERENCED_PARAMETER (RegistryPath);

    DebugPrint( ( "Entered the Driver Entry\n" ) );

    OcFilterInitializeHookerEngine();

    PnpFilterGlobal.DriverObject = DriverObject;
    InitializeListHead( &PnpFilterGlobal.ListHead );
    KeInitializeSpinLock( &PnpFilterGlobal.SpinLock );
    OcRwInitializeRwLock( &PnpFilterGlobal.RwSpinLock );

    ExInitializeNPagedLookasideList( &PnpFilterGlobal.DeviceExtensionMemoryPool,
                                     NULL,
                                     NULL,
                                     0x0,
                                     sizeof( DEVICE_EXTENSION ),
                                     'EFcO',
                                     0x0 );

    ExInitializeResourceLite( &PnpFilterGlobal.VirtualDevicesListResource );

    PnpFilterGlobal.FastIoDispatch.SizeOfFastIoDispatch = sizeof( PnpFilterGlobal.FastIoDispatch );
    PnpFilterGlobal.FastIoDispatch.FastIoCheckIfPossible = OcFilterFastIoCheckIfPossible;
    PnpFilterGlobal.FastIoDispatch.FastIoRead = OcFilterFastIoRead;
    PnpFilterGlobal.FastIoDispatch.FastIoWrite = OcFilterFastIoWrite;
    PnpFilterGlobal.FastIoDispatch.FastIoQueryBasicInfo = OcFilterFastIoQueryBasicInfo;
    PnpFilterGlobal.FastIoDispatch.FastIoQueryStandardInfo = OcFilterFastIoQueryStandardInfo;
    PnpFilterGlobal.FastIoDispatch.FastIoLock = OcFilterFastIoLock;
    PnpFilterGlobal.FastIoDispatch.FastIoUnlockSingle = OcFilterFastIoUnlockSingle;
    PnpFilterGlobal.FastIoDispatch.FastIoUnlockAll = OcFilterFastIoUnlockAll;
    PnpFilterGlobal.FastIoDispatch.FastIoUnlockAllByKey = OcFilterFastIoUnlockAllByKey;
    PnpFilterGlobal.FastIoDispatch.FastIoDeviceControl = OcFilterFastIoDeviceControl;
    PnpFilterGlobal.FastIoDispatch.FastIoDetachDevice = OcFilterFastIoDetachDevice;
    PnpFilterGlobal.FastIoDispatch.FastIoQueryNetworkOpenInfo = OcFilterFastIoQueryNetworkOpenInfo;
    PnpFilterGlobal.FastIoDispatch.MdlRead = OcFilterFastIoMdlRead;
    PnpFilterGlobal.FastIoDispatch.MdlReadComplete = OcFilterFastIoMdlReadComplete;
    PnpFilterGlobal.FastIoDispatch.PrepareMdlWrite = OcFilterFastIoPrepareMdlWrite;
    PnpFilterGlobal.FastIoDispatch.MdlWriteComplete = OcFilterFastIoMdlWriteComplete;
    PnpFilterGlobal.FastIoDispatch.FastIoReadCompressed = OcFilterFastIoReadCompressed;
    PnpFilterGlobal.FastIoDispatch.FastIoWriteCompressed = OcFilterFastIoWriteCompressed;
    PnpFilterGlobal.FastIoDispatch.MdlReadCompleteCompressed = OcFilterFastIoMdlReadCompleteCompressed;
    PnpFilterGlobal.FastIoDispatch.MdlWriteCompleteCompressed = OcFilterFastIoMdlWriteCompleteCompressed;
    PnpFilterGlobal.FastIoDispatch.FastIoQueryOpen = OcFilterFastIoQueryOpen;

    RtlInitUnicodeString( &KernelFunctionName, L"IoAttachDeviceToDeviceStackSafe" );
    PnpFilterGlobal.IoAttachDeviceToDeviceStackSafe = MmGetSystemRoutineAddress( &KernelFunctionName );

    RtlInitUnicodeString( &KernelFunctionName, L"IoSynchronousInvalidateDeviceRelations" );
    PnpFilterGlobal.IoSynchronousInvalidateDeviceRelations = MmGetSystemRoutineAddress( &KernelFunctionName );

    //
    // as I've not done the workaround for the case when the function
    // is not exported I added this assert
    //
    ASSERT( NULL != PnpFilterGlobal.IoSynchronousInvalidateDeviceRelations );

    //
    // Create dispatch points
    //
    for( ulIndex = 0, dispatch = DriverObject->MajorFunction;
         ulIndex <= IRP_MJ_MAXIMUM_FUNCTION;
         ulIndex++, dispatch++ ) {

        *dispatch = FilterDispatchIo;
    }

    //
    // PnP dispatchers
    //
    DriverObject->MajorFunction[IRP_MJ_PNP]   = FilterDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = FilterDispatchPower;
    DriverObject->DriverExtension->AddDevice  = FilterAddDevice;

    //
    // communication dispatchers
    //
    DriverObject->MajorFunction[IRP_MJ_CREATE]     = 
    DriverObject->MajorFunction[IRP_MJ_CLOSE]      = 
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]    = 
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = FilterDispatchIo;

    //
    // FastIO table
    //
    DriverObject->FastIoDispatch = &PnpFilterGlobal.FastIoDispatch;

    //
    // do not pay attention to the returned code, load in any case,
    // because if the disk's filter is not loaded the system will
    // BSOD with an unacessible boot device error
    //
    RC = OcCreatePnPFilterControlObject();
    ASSERT( NULL != PnpFilterGlobal.ControlDeviceObject );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // initialize the object manager
    //
    RC = OcObInitializeObjectManager( NULL );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // initialize the worker threads manager
    //
    RC = OcWthInitializeWorkerThreadsSubsystem( NULL );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // create the worker threads pool with the shared work item list
    //
    RC = OcTplCreateThreadPool( 0x3,
                                FALSE,//shared work item list
                                &PnpFilterGlobal.ThreadsPoolObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // initialize the object type for the connection object
    //
    OC_TOGGLE_TYPE_INITIALIZER( &TypeInitializer );
    TypeInitializer.Tag = 'CFcO';
    TypeInitializer.ObjectsBodySize = sizeof( OC_PNP_FILTER_CONNECT_OBJECT );
    TypeInitializer.Methods.DeleteObject = OcFilterDeleteConnectObject;

    OcObInitializeObjectType( &TypeInitializer,
                              &PnpFilterGlobal.ConnectObjectType );

__exit:

    ASSERT( NT_SUCCESS( RC ) );

    if( NT_SUCCESS( RC ) )
        PnpFilterGlobal.IsFilterInNormalState = TRUE;
    else
        PnpFilterGlobal.IsFilterInNormalState = FALSE;

    //
    // return success in any case, because the disk filters must be loaded
    //
    RC = STATUS_SUCCESS;

    return RC;
}

//-------------------------------------------------

VOID
FilterClearFilterDeviceExtension(
    __inout PDEVICE_EXTENSION    deviceExtension
    )
{

    if( NULL == deviceExtension->Self ){

        //
        // this is a virtual PDO
        //
        DebugPrint(("Deleting a virtual PDO for a 0x%p physical PDO\n", deviceExtension->PhysicalDeviceObject));
        InterlockedDecrement( &PnpFilterGlobal.VirtualPdoCount );
    }

    if( NULL != deviceExtension->VirtualPdoExtension ){

        //
        // decrement the relation refrence count which
        // was inceremented in AddDeviceEx when the filter was
        // attached to the stack
        //
        InterlockedDecrement( &deviceExtension->VirtualPdoExtension->RelationReferenceCount );

        //
        // dereference the PDO object refrenced in AddDeviceEx
        //
        OcFilterDereferenceVirtualPdoExtension( deviceExtension->VirtualPdoExtension );

    }

    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( deviceExtension->VirtualPdoExtension );

    //
    // clear device's extension before removing the device( i.e. filter device )
    // or freeing the extension ( i.e. virtual device )
    //
    ExDeleteResourceLite( &deviceExtension->RelationResource );

    //
    // mark the extension as freed
    //
    deviceExtension->Type = (DEVICE_TYPE)(-1);
}

//-------------------------------------------------

NTSTATUS
FilterAddDeviceEx(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType,
    IN OUT PDEVICE_OBJECT    *PtrFiletrDeviceObject OPTIONAL,
    IN BOOLEAN    CalledByPnPFromAddDevice
    )
/*++

Routine Description:

    Depending on PnPDeviceType either the Plug & Play subsystem is 
    handing us a brand new PDO, for which we
    (by means of INF registration) have been asked to provide a driver, or
    the client wants a filter be attached to a device send as Pdo parameter 
    though this might not necessary be a PDO.

    We need to determine if we need to be in the driver stack for the device.
    Create a function device object to attach to the stack
    Initialize that device object
    Return status success.

    Remember: We can NOT actually send ANY non pnp IRPS to the given driver
    stack, UNTIL we have received an IRP_MN_START_DEVICE.

    The caller should guarantee the impossibility of the race condition when 
    the stack to which the attaching will be made is not being torn down during
    the attaching process.

Arguments:

    DriverObject - pointer to a driver object.

    PhysicalDeviceObject -  pointer to a device object created by the
                            underlying bus driver.

    PnPDeviceType - defines the type of the device to be added, if there ia a 
                    lower stack device or PDO when the caller doesn't want to attach
                    and create any device but wants to save information about the PDO

    CalledByPnPFromAddDevice - this parameters indicates whether the function
                               is called by the PnP manager while building the
                               device stack or the call is made by another 
                               subsystem and the device might receive requests
                               so the device object being attached must be
                               initialized before attachment

Return Value:

    NT status code.

--*/
{
    NTSTATUS                status = STATUS_SUCCESS;
    PDEVICE_OBJECT          deviceObject = NULL;
    PDEVICE_EXTENSION       deviceExtension;
    PDEVICE_EXTENSION       devicePdoExtension = NULL;
    ULONG                   deviceType = FILE_DEVICE_UNKNOWN;
    ULONG                   deviceFlags = 0x0;
    ULONG                   deviceCharacteristic = FILE_DEVICE_SECURE_OPEN;
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;
    BOOLEAN                 RemoveVirtualPdo = FALSE;
    BOOLEAN                 IsPdoStack = ( OcDevicePnPTypeFilterDo == PnPDeviceType ||
                                           OcDevicePnPTypePdo == PnPDeviceType );
    BOOLEAN                 AttachDeviceToStack = ( OcDevicePnPTypePdo != PnPDeviceType &&
                                                    OcDeviceLowerNoPnPType != PnPDeviceType );

    PAGED_CODE();

    ASSERT( OcDeviceLowerNoPnPType == PnPDeviceType ||
            OcDeviceNoPnPTypeInMiddleOfStack == PnPDeviceType ||
            OcDevicePnPTypeFilterDo == PnPDeviceType || 
            OcDevicePnPTypePdo == PnPDeviceType );

    //
    // if something went wrong then do not try to filter devices' requests
    //
    if( FALSE == PnpFilterGlobal.IsFilterInNormalState )
        return STATUS_SUCCESS;

    if( AttachDeviceToStack ){

        //
        // IoIsWdmVersionAvailable(1, 0x20) returns TRUE on os after Windows 2000.
        //
        if( !IoIsWdmVersionAvailable(1, 0x20) || 
             FALSE == CalledByPnPFromAddDevice ){

            //
            // save the device flags and characteristics if either
            //   - this is a Windows 2000 system
            //   - the attachment is being made not on AddDevice path
            //

            //
            // Win2K system bugchecks if the filter attached to a storage device
            // doesn't specify the same DeviceType as the device it's attaching
            // to. This bugcheck happens in the filesystem when you disable
            // the devicestack whose top level deviceobject doesn't have a VPB.
            // To workaround we will get the toplevel object's DeviceType and
            // specify that in IoCreateDevice.
            //
            deviceObject = IoGetAttachedDeviceReference( PhysicalDeviceObject );

            deviceType = deviceObject->DeviceType;
            deviceFlags = ( deviceObject->Flags &
                            ( DO_BUFFERED_IO |
                              DO_DIRECT_IO |
                              DO_SUPPORTS_TRANSACTIONS ) );
            deviceCharacteristic = ( deviceObject->Characteristics & FILE_DEVICE_SECURE_OPEN );

            ObDereferenceObject( deviceObject );
        }

        //
        // create the virtual device for the PDO before creating 
        // and attaching the filter device object,
        // but at first try to find the virtual PDO in the list -
        // it may have been inserted in the list when
        // the lower filter device has been attached,
        // the returned PDO will be locked
        // IMPORTANT NOTE - sometimes this function is called
        // when the remove lock has been acquired if the PDO has been
        // created, but it is OK to reacquire it again
        //
        devicePdoExtension = OcFilterReturnReferencedPdoExtensionLockIf( PhysicalDeviceObject, TRUE );
        if( NULL == devicePdoExtension ){

            OC_DEVICE_OBJECT_PNP_TYPE    PdoType;

            PdoType = IsPdoStack? OcDevicePnPTypePdo: OcDeviceLowerNoPnPType;

            ASSERT( NULL == OcFilterReturnPdoExtensionUnsafe( PhysicalDeviceObject ) );

            status = OcFilterAddVirtualDeviceForPdo( PhysicalDeviceObject, PdoType );
            if( !NT_SUCCESS (status) ){
                //
                // Returning failure here prevents the entire stack from functioning,
                // but most likely the rest of the stack will not be able to create
                // device objects either, so it is still OK.
                //
                return status;
            }

            //
            // get the locked device extension for the just created virtual PDO
            //
            devicePdoExtension = OcFilterReturnReferencedPdoExtensionLockIf( PhysicalDeviceObject, TRUE );
            ASSERT( NULL != devicePdoExtension );
            if( NULL == devicePdoExtension )
                return STATUS_UNSUCCESSFUL;

            //
            // remeber that the PDO has been created here,
            // so in case of a failure it should be removed,
            // but if the PDO has been created before calling 
            // this function then there is possibility that 
            // it was locked before calling this function, so
            // if an attempt to remove is made a deadlock 
            // will lock the thread forether
            //

            RemoveVirtualPdo = TRUE;
        }

        ASSERT( NULL != devicePdoExtension && 
                PhysicalDeviceObject == devicePdoExtension->PhysicalDeviceObject && 
                NULL == devicePdoExtension->Self );

        //
        // Create a filter device object.
        //

        status = IoCreateDevice( DriverObject,
                                 sizeof (DEVICE_EXTENSION),
                                 NULL,  // No Name
                                 deviceType,
                                 deviceCharacteristic,
                                 FALSE,
                                 &deviceObject);


        if (!NT_SUCCESS (status)) {

            //
            // Returning failure here prevents the entire stack from functioning,
            // but most likely the rest of the stack will not be able to create
            // device objects either, so it is still OK.
            //

            //
            // unlock the extension
            //
            IoReleaseRemoveLock( &devicePdoExtension->RemoveLock, NULL );

            //
            // delete the virtual PDO before returning
            //
            if( RemoveVirtualPdo )
                OcFilterDeleteVirtualPdoByExtension( devicePdoExtension );

            OcFilterDereferenceVirtualPdoExtension( devicePdoExtension );

            return status;
        }

        DebugPrint( ( "AddDevice PDO (0x%p) FDO (0x%p)\n",
                      PhysicalDeviceObject, deviceObject ) );

        deviceExtension = (PDEVICE_EXTENSION) deviceObject->DeviceExtension;

    } else {

        deviceExtension = ExAllocateFromNPagedLookasideList( &PnpFilterGlobal.DeviceExtensionMemoryPool );
        if( NULL == deviceExtension )
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory( (PVOID)deviceExtension, sizeof( *deviceExtension ) );

    if( OcDeviceNoPnPTypeInMiddleOfStack == PnPDeviceType )
        deviceExtension->Type = DEVICE_TYPE_FIDO_NO_PNP;
    else
        deviceExtension->Type = DEVICE_TYPE_FIDO;

    deviceExtension->PnPDeviceType = PnPDeviceType;

    deviceExtension->PhysicalDeviceObject = PhysicalDeviceObject;

    ExInitializeResourceLite( &deviceExtension->RelationResource );

    OcRwInitializeRwLock( &deviceExtension->CoreHeader.RwLock );

    deviceExtension->Self = deviceObject;// NULL for "virtual devices"

    //
    // Let us use remove lock to keep count of IRPs so that we don't 
    // deteach and delete our deviceobject until all pending I/Os in our
    // devstack are completed. Remlock is required to protect us from
    // various race conditions where our driver can get unloaded while we
    // are still running dispatch or completion code.
    //

    IoInitializeRemoveLock( &deviceExtension->RemoveLock , 
                            POOL_TAG,
                            1, // MaxLockedMinutes 
                            100); // HighWatermark, this parameter is 
                                  // used only on checked build. Specifies 
                                  // the maximum number of outstanding 
                                  // acquisitions allowed on the lock

    if( AttachDeviceToStack ){

        ASSERT( NULL != deviceObject);
        ASSERT( NULL != devicePdoExtension );

        if( FALSE == CalledByPnPFromAddDevice ){

            //
            // The device stack has been already built and is working now.
            // Propagate flags from Device Object we are trying to attach to.
            // Note that we do this before the actual attachment to make sure
            // the flags are properly set once we are attached (since an IRP
            // can come in immediately after attachment but before the flags would
            // be set)
            //

            OcSetFlag( deviceObject->Flags, deviceFlags );

        }

        //
        // the assert are failed by Win2k OSc that are older than 
        // Win2k with Rollup Update SP4
        //
        ASSERT( NULL != PnpFilterGlobal.IoAttachDeviceToDeviceStackSafe );

        if( NULL != PnpFilterGlobal.IoAttachDeviceToDeviceStackSafe ){

            PnpFilterGlobal.IoAttachDeviceToDeviceStackSafe( deviceObject,
                                                             PhysicalDeviceObject,
                                                             &deviceExtension->NextLowerDriver );

        } else {

            deviceExtension->NextLowerDriver = IoAttachDeviceToDeviceStack (
                                                deviceObject,
                                                PhysicalDeviceObject);
        }

        //
        // Failure for attachment is an indication of a broken plug & play system.
        //

        if( NULL == deviceExtension->NextLowerDriver ){

            //
            // clear device's extension before removing the device
            //
            FilterClearFilterDeviceExtension( deviceExtension );
            IoDeleteDevice( deviceObject );

            //
            // unlock the extension
            //
            IoReleaseRemoveLock( &devicePdoExtension->RemoveLock, NULL );

            //
            // delete the virtual PDO before returning
            //
            if( RemoveVirtualPdo )
                OcFilterDeleteVirtualPdoByExtension( devicePdoExtension );

            //
            // dereference the PDO extension refrenced in OcFilterReturnReferencedPdoExtensionLockIf
            //
            OcFilterDereferenceVirtualPdoExtension( devicePdoExtension );

            return STATUS_UNSUCCESSFUL;
        }

        if( TRUE == CalledByPnPFromAddDevice ){

            //
            // the function has been called by the PnP Manager on the AddDevice
            // path for a new device so there is impossible to receive any IRP
            // before returning from this function
            //

            deviceObject->Flags |= deviceExtension->NextLowerDriver->Flags &
                                   ( DO_BUFFERED_IO | DO_DIRECT_IO |
                                     DO_POWER_PAGABLE | DO_SUPPORTS_TRANSACTIONS );

            deviceObject->DeviceType = deviceExtension->NextLowerDriver->DeviceType;

            deviceObject->Characteristics = 
                            deviceExtension->NextLowerDriver->Characteristics;
        }

        //
        // Set the initial state of the Filter DO
        //

        INITIALIZE_PNP_STATE( deviceExtension );

        DebugPrint(("AddDevice: %p to %p->%p \n", deviceObject,
            deviceExtension->NextLowerDriver,
            PhysicalDeviceObject));

        deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

        //
        // mark the virtual PDO's extension as having at least 
        // one filter attached, do not lock the device because 
        // it is impossible for the device to be deleted
        // when AddDevice is being called.
        //

        ASSERT( NULL != devicePdoExtension );
        devicePdoExtension->Flags.FilterAttached = 0x1;

        //
        // increment the reference count to prevent from
        // premature delition after releasing the lock
        //
        OcFilterReferenceVirtualPdoExtension( devicePdoExtension );

        //
        // save the pointer to the PDO extension
        //
        deviceExtension->VirtualPdoExtension = devicePdoExtension;

        //
        // incerement the relation reference count to prevent the
        // PDO extension from removing from the the global list when
        // it is removed from all relations but have an attached filter
        // in which case the IRP_MN_REMOVE received by the FiDO is used
        // to delete the PDO
        //
        InterlockedIncrement( &devicePdoExtension->RelationReferenceCount );

        //
        // call the registered callback to notify about Pdo,
        // in many cases the PDO notification has been already done
        // throught the device relations callback, but there is nothing
        // wrong to send the notification two times,
        //
        // do not notify in the following cases 
        //   - non PnP lower devices without attached filter device
        //   - PnP Pdo without attached filter 
        // the reason is due to situation when the caller might
        // not receive the notification about lower device( i.e.
        // virtual PDO ) removing if filter attaching failed and
        // driver has been unhooked, this is not the case for PDOs
        // reported on relations as the notification is always
        // sent when the PDO has not been reported in the device
        // relations but the device reported in relation is not
        // reported here as they are reported through the device 
        // relations interface callback
        //
        ASSERT( AttachDeviceToStack );
        ASSERT( OcDeviceLowerNoPnPType != deviceExtension->PnPDeviceType );

        PtrConnectionObject = OcFilterReferenceCurrentConnectObject();
        if( NULL != PtrConnectionObject ){

            //
            // the FDO device object might be NULL if the PDO is added when 
            // some of the relations is being processed ( usually BusRelation ),
            // if FDO is attached then the PDO has been initialized,
            // if this is not a PnP stack then consider the "stack" as 
            // an initialized one in any case
            //

            PtrConnectionObject->CallbackMethods.ReportNewDevice( PhysicalDeviceObject,
                deviceObject,
                deviceExtension->PnPDeviceType,
                ( NULL != deviceObject ) || !IsPdoStack );

            OcObDereferenceObject( PtrConnectionObject );

        }//if( NULL != PtrConnectionObject )

    } else {

        //
        // Set the initial state of the "virtual device"
        //

        INITIALIZE_PNP_STATE( deviceExtension );

        //
        // set the reference count
        //
        deviceExtension->ReferenceCount = 0x1;

        //
        // if this is a PDO then set the relation count to 0x1
        // because the Pdo inserted either in AddDevice or
        // while processing a device relation in both cases 
        // the bumped relation reference count will retain the
        // PDO from premature deletion while processing the 
        // relation requests because if zero relation count
        // is discovered in that phase a PDO is removed
        //
        if( OcDevicePnPTypePdo == PnPDeviceType )
            deviceExtension->RelationReferenceCount = 0x1;

        InterlockedIncrement( &PnpFilterGlobal.VirtualPdoCount );

        DebugPrint(("Creating a virtual PDO for a 0x%p physical PDO\n", deviceExtension->PhysicalDeviceObject));
    }

    //
    // insert device in the list only after initializing 
    // the extension and attaching it to the
    // lower device object. Pay attention that between
    // the devices in the list the partial odering is exist - 
    // the lower parent devices always precedes the upper
    // child devices.
    //
    ExInterlockedInsertTailList( &PnpFilterGlobal.ListHead,
                                 &deviceExtension->ListEntry,
                                 &PnpFilterGlobal.SpinLock );

    if( NULL != PtrFiletrDeviceObject )
        *PtrFiletrDeviceObject = deviceObject;

    if( NULL != devicePdoExtension ){

        //
        // unlock the PDO extension locked in OcFilterReturnReferencedPdoExtensionLockIf
        //
        IoReleaseRemoveLock( &devicePdoExtension->RemoveLock, NULL );

        //
        // dereference the PDO extension refrenced in OcFilterReturnReferencedPdoExtensionLockIf
        //
        OcFilterDereferenceVirtualPdoExtension( devicePdoExtension );

    }

    return STATUS_SUCCESS;

}

//---------------------------------------------------

NTSTATUS
FilterAddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject
    )
/*++

Routine Description:

    The Plug & Play subsystem is handing us a brand new PDO, for which we
    (by means of INF registration) have been asked to provide a driver.

    We need to determine if we need to be in the driver stack for the device.
    Create a function device object to attach to the stack
    Initialize that device object
    Return status success.

    Remember: We can NOT actually send ANY non pnp IRPs to the given driver
    stack, UNTIL we have received the IRP_MN_START_DEVICE request.
--*/
{
    return FilterAddDeviceEx( DriverObject,
                              PhysicalDeviceObject,
                              OcDevicePnPTypeFilterDo,
                              NULL,
                              TRUE );
}

//---------------------------------------------------

NTSTATUS
OcFilterAddVirtualDeviceForPdo(
    __in PDEVICE_OBJECT    Pdo,
    __in OC_DEVICE_OBJECT_PNP_TYPE    PdoType
    )
    /*
    the caller is responsible for keeping
    the system from adding duplicate 
    virtual devices and for synchronizing
    with the device stack tering down or
    receiving the remove request
    */
{
    NTSTATUS    RC;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcDevicePnPTypePdo == PdoType || 
            OcDeviceLowerNoPnPType == PdoType );

    //
    // check for duplication
    //
    ASSERT( NULL == OcFilterReturnPdoExtensionUnsafe( Pdo ) );

    //
    // hook the device's driver if it has not been hooked
    //
    RC = OcFilterHookDriver( Pdo->DriverObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // ad a new virtual device, 
    // the device extension will be referenced
    //
    RC = FilterAddDeviceEx( PnpFilterGlobal.DriverObject,
                            Pdo,
                            PdoType,
                            NULL,
                            FALSE );

    ASSERT( NT_SUCCESS( RC ) );

    return RC;
}

//---------------------------------------------------

VOID
OcFilterDeleteVirtualPdoByExtension(
    __in PDEVICE_EXTENSION    deviceExtension
    )
    /*
    this function can be called concurrently in several threads
    */
{

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( deviceExtension->ReferenceCount > 0x0 );
    ASSERT( NULL == deviceExtension->Self );

    //
    // perform all operations which must be synchronous with the caller
    //
    if( OcFilterRenderVirtualPdoAsDeleted( deviceExtension ) ){

        //
        // this is a first caller for this function, so
        // dereference the extension here as it was referenced
        // when was created in FilterAddDeviceEx, i.e. the initial
        // reference count was set to 0x1
        //
        OcFilterDereferenceVirtualPdoExtension( deviceExtension );
    }

}

//---------------------------------------------------

VOID
OcFilterDeleteVirtualPdo(
    __in PDEVICE_OBJECT    Pdo
    )
    /*
    this function can be called concurrently in several threads
    */
{
    PDEVICE_EXTENSION    deviceExtension;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // I allow the NULL input parameter because this
    // helps to manage the simple programming model
    // w/o regard to whether a device has a PDO or not
    //
    if( NULL == Pdo )
        return;

    ASSERT( OcFilterIsPdo( Pdo ) );

    //
    // get the extension without lock
    //
    deviceExtension = OcFilterReturnReferencedPdoExtensionLockIf( Pdo, FALSE );
    if( NULL == deviceExtension )
        return;

    ASSERT( deviceExtension->ReferenceCount > 0x0 );

    //
    // start the deletion - remove from the list and mark as deleted
    //
    OcFilterDeleteVirtualPdoByExtension( deviceExtension );

    //
    // dereference the extension
    //
    OcFilterDereferenceVirtualPdoExtension( deviceExtension );
}

//---------------------------------------------------

PDEVICE_RELATIONS_SHADOW
FilterProcessPdoInformationForRelation(
    __in_opt PDEVICE_RELATIONS    InputDeviceRelations OPTIONAL,
    __in_opt PDEVICE_RELATIONS_SHADOW    InputDeviceRelationsShadow OPTIONAL,// used only if AddVirtualDevicesToList is FALSE
    __in BOOLEAN    AddVirtualDevicesToList // if TRUE then add devices, else - remove them
    )
    /*
    if AddVirtualDevicesToList is TRUE the function allocates, initializes and
    returns the shadow device relations, to free the shadow relations it must be 
    provided to this function as the InputDeviceRelationsShadow parameter with
    AddVirtualDevicesToList set to FALSE
    */
{
    PDEVICE_RELATIONS           DeviceRelations = NULL;
    PDEVICE_RELATIONS_SHADOW    DeviceRelationsShadow = NULL;
    ULONG                       RelationSize;
    ULONG                       RelationMemoryTag = 'RDcO';
    BOOLEAN                     GlobalSpinLockIsAcquiredAtLevel_0 = FALSE;

    //
    // a waiting function will be called
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( NULL != InputDeviceRelations );
    ASSERT( TRUE == AddVirtualDevicesToList || FALSE == AddVirtualDevicesToList );
    ASSERT( InputDeviceRelationsShadow? ( InputDeviceRelationsShadow->Count <= InputDeviceRelations->Count ): TRUE );
    ASSERT( AddVirtualDevicesToList? NULL == InputDeviceRelationsShadow : NULL != InputDeviceRelationsShadow );

    if( TRUE == AddVirtualDevicesToList && NULL != InputDeviceRelationsShadow ){

        ASSERT( !"The caller violated the function's calling semantics for FilterProcessPdoInformationForRelation" );
        //
        // let's continue with the corrected parameter, butb the caller might end with 
        // dangling refrenced counts and consumes resources
        //
        InputDeviceRelationsShadow = NULL;
    }

    //
    // create a relations copy which can be changed,
    // I allocate memory before acquiring the resource and 
    // try to acquire resources without waiting if the 
    // acquiring fails the memory is freed and the resource
    // is acquired with waiting allowed this has been done
    // to avoid a surge in memory allocations when a lot of threads
    // wait on the resource with the memory allocated in advance
    //
    RelationSize = max( ( FIELD_OFFSET( DEVICE_RELATIONS, Objects ) + 
                          sizeof( InputDeviceRelations->Objects[0] ) * InputDeviceRelations->Count ),
                        ( FIELD_OFFSET( DEVICE_RELATIONS_SHADOW, Objects ) + 
                          sizeof( InputDeviceRelationsShadow->Objects[0] ) * InputDeviceRelations->Count ) );

    //
    // allocate memory and do not check the result - the attempt will be repeated later
    //
    DeviceRelations = ExAllocatePoolWithTag( NonPagedPool, RelationSize, RelationMemoryTag );
    if( TRUE == AddVirtualDevicesToList )
        DeviceRelationsShadow = ExAllocatePoolWithTag( NonPagedPool, RelationSize, RelationMemoryTag );

    //
    // enter critical region
    //
    KeEnterCriticalRegion();
    if( FALSE == ExAcquireResourceExclusiveLite( &PnpFilterGlobal.VirtualDevicesListResource, FALSE ) ){

        if( NULL != DeviceRelations ){
            ExFreePoolWithTag( DeviceRelations, RelationMemoryTag );
            DeviceRelations = NULL;
        }// if( NULL != DeviceRelations )

        if( NULL != DeviceRelationsShadow ){
            ExFreePoolWithTag( DeviceRelationsShadow, RelationMemoryTag );
            DeviceRelationsShadow = NULL;
        }// if( NULL != DeviceRelationsShadow )

        //
        // acquire resource with the waiting allowed
        //
        ExAcquireResourceExclusiveLite( &PnpFilterGlobal.VirtualDevicesListResource, TRUE );
    }// if( FALSE == ExAcquireResourceExclusiveLite
    {// start of the exclusive lock

        KIRQL    OldIrql;

        if( NULL == DeviceRelations ){

            DeviceRelations = ExAllocatePoolWithTag( NonPagedPool, RelationSize, RelationMemoryTag );
            if( NULL == DeviceRelations )
                goto __exit_from_lock;
        }

        if( NULL == DeviceRelationsShadow && TRUE == AddVirtualDevicesToList ){

            DeviceRelationsShadow = ExAllocatePoolWithTag( NonPagedPool, RelationSize, RelationMemoryTag );
            if( NULL == DeviceRelationsShadow ){
                ExFreePoolWithTag( DeviceRelations, RelationMemoryTag );
                goto __exit_from_lock;
            }// if( NULL == DeviceRelationsShadow )
        }

        //
        // copy the content of the relation array
        //
        RtlCopyMemory( DeviceRelations, InputDeviceRelations, RelationSize );

        //
        // set the initial count
        //
        if( DeviceRelationsShadow )
            DeviceRelationsShadow->Count = 0x0;

        //
        // Actually, if InputDeviceRelationsShadow is not provided and the 
        // AddVirtualDevicesToList parameter is FALSE there will be a deadlock
        // because to traverse the list the PnpFilterGlobal.SpinLock is acquired and the
        // same lock is acquired when the OcFilterDereferenceVirtualPdoExtension function
        // is called and the refrence count drops to zero
        //
        if( NULL == InputDeviceRelationsShadow ){

            //
            // acquire the global list lock to traverse the list, if 
            // InputDeviceRelationsShadow is provided then the list
            // is not used for traversing so the lock is not acquired, also see comments
            // above about the deadlock
            //
            KeAcquireSpinLock( &PnpFilterGlobal.SpinLock, &OldIrql );
            GlobalSpinLockIsAcquiredAtLevel_0 = TRUE;
        }
        {// start of the spin lock at level 0

            //
            // traverse through the list and compare the found
            // device extension's devices with the devices in the relation
            //

            PLIST_ENTRY    request;
            ULONG          i;
#if DBG
            PLIST_ENTRY    previous_request = NULL;
#endif//DBG

            //
            // if InputDeviceRelationsShadow is provided - then use it as
            // it contains the referenced device extensions, pay attention that
            // if InputDeviceRelationsShadow is NULL then no device will be 
            // removed from the list inside the loop, so the list is traversed
            // simply by moving pointer to the next list entry, but if 
            // InputDeviceRelationsShadow is provided then the devices
            // might be removed from the list inside this loop
            //
            for( InputDeviceRelationsShadow? 
                    (request = &((InputDeviceRelationsShadow->Objects[ i=0x0 ])->ListEntry)) : 
                    (request = PnpFilterGlobal.ListHead.Flink) ;
                 InputDeviceRelationsShadow? 
                    ( i < InputDeviceRelationsShadow->Count ): 
                    ( request != &PnpFilterGlobal.ListHead ) ; 
                 InputDeviceRelationsShadow? 
                    (request = &((InputDeviceRelationsShadow->Objects[ ++i ])->ListEntry)) :
                    (request = request->Flink) ){

                PDEVICE_EXTENSION       deviceExtension;
                ULONG                   i;
                BOOLEAN                 RemoveEntry = FALSE;

#if DBG
                ASSERT( request != previous_request );
                previous_request = request;
#endif//DBG

                deviceExtension = CONTAINING_RECORD( request,
                                                     DEVICE_EXTENSION,
                                                     ListEntry );

                //
                // do not check non-PDO entries, i.e. entries that either 
                // describe the filters's real device objects and have 
                // been allocated by IoCreateDevice as a device's extension
                // or not a PnP Pdo device at all
                //
                if( NULL != deviceExtension->Self || 
                    OcDevicePnPTypePdo != deviceExtension->PnPDeviceType ){

                    ASSERT( NULL == InputDeviceRelationsShadow );

                    //
                    // move to the next list entry, i.e. the next device extension
                    //
                    request = request->Flink;
                    continue;
                }

                ASSERT( NULL == deviceExtension->Self );

                for( i = 0x0 ; i != DeviceRelations->Count; ++i ){

                    //
                    // self-defence
                    //
                    if( NULL == DeviceRelations->Objects[ i ] )
                        continue;

                    if( deviceExtension->PhysicalDeviceObject != DeviceRelations->Objects[ i ] )
                        continue;

                    //
                    // reference the device extension which must describe a PDO
                    //
                    ASSERT( deviceExtension->ReferenceCount > 0x0 );
                    ASSERT( NULL == deviceExtension->Self );

                    if( AddVirtualDevicesToList ){

                        ASSERT( NULL == InputDeviceRelationsShadow );

                        OcFilterReferenceVirtualPdoExtension( deviceExtension );
                        InterlockedIncrement( &deviceExtension->RelationReferenceCount );

                        ASSERT( DeviceRelationsShadow );
                        //
                        // save the referenced device extension to be able to dereference
                        // it when the relation is being removed and the device extension
                        // has been removed from the global list, without this shadow
                        // relation the result would be the dangling reference
                        //
                        if( NULL != DeviceRelationsShadow ){

                            DeviceRelationsShadow->Objects[ DeviceRelationsShadow->Count ] = deviceExtension;
                            DeviceRelationsShadow->Count += 0x1;

                        }

                    } else {

                        ASSERT( NULL != InputDeviceRelationsShadow );

                        InterlockedDecrement( &deviceExtension->RelationReferenceCount );

                        //
                        // the device might be removed from the list and torn down
                        // in OcFilterDereferenceVirtualPdoExtension if the
                        // refrence count drops to zero
                        //
                        OcFilterDereferenceVirtualPdoExtension( deviceExtension );
                        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( deviceExtension );

                    }// else

                    //
                    // purge the found device from the relation
                    //
                    DeviceRelations->Count -= 0x1;
                    if( i != DeviceRelations->Count )
                        DeviceRelations->Objects[ i ] = DeviceRelations->Objects[ i+0x1 ];

                    break;

                }// for all devices in DeviceRelation

                if( 0x0 == DeviceRelations->Count )
                    break;

            }// for all DeviceExtensions

        }// end of the spin lock at level 0
        if( TRUE == GlobalSpinLockIsAcquiredAtLevel_0 ){

            ASSERT( NULL == InputDeviceRelationsShadow );
            KeReleaseSpinLock( &PnpFilterGlobal.SpinLock, OldIrql );
        }

__exit_from_lock: NOTHING;
    }// end of the exclusive lock
    ExReleaseResourceLite( &PnpFilterGlobal.VirtualDevicesListResource );
    KeLeaveCriticalRegion();

    if( NULL != DeviceRelations )
        ExFreePoolWithTag( DeviceRelations, RelationMemoryTag );

    //
    // delete the shadow relations as it was allocated in this function
    //
    if( NULL != InputDeviceRelationsShadow )
        ExFreePoolWithTag( InputDeviceRelationsShadow, RelationMemoryTag );

    return DeviceRelationsShadow;
}

//---------------------------------------------------

NTSTATUS
FilterDispatchIo(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP              Irp
    )
/*++

Routine Description:

    This routine is the dispatch routine for non passthru irps.
    We will check the input device object to see if the request
    is meant for the control device object. If it is, we will
    handle and complete the IRP, if not, we will pass it down to 
    the lower driver.
    
Arguments:

    DeviceObject - Pointer to the device object.

    Irp - Pointer to the request packet.

Return Value:

    NT Status code
--*/
{
    PIO_STACK_LOCATION           irpStack;
    NTSTATUS                     status;
    PCONTROL_DEVICE_EXTENSION    deviceExtension;
    PCOMMON_DEVICE_DATA          commonData;

    commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;


    //
    // Please note that this is a common dispatch point for controlobject and
    // filter deviceobject attached to the pnp stack. 
    //
    if( DEVICE_TYPE_FIDO == commonData->Type || DEVICE_TYPE_FIDO_NO_PNP == commonData->Type ){
        //
        // We will just  the request down as we are not interested in handling
        // requests that come on the PnP stack.
        //
        return FilterPass( DeviceObject, Irp );
    }
 
    ASSERT( commonData->Type == DEVICE_TYPE_CDO );

    deviceExtension = (PCONTROL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    //
    // Else this is targeted at our control deviceobject so let's handle it.
    // Here we will handle the IOCTl requests that come from the app.
    // We don't have to worry about acquiring remlocks for I/Os that come 
    // on our control object because the I/O manager takes reference on our 
    // deviceobject when it initiates a request to our device and that keeps
    // our driver from unloading when we have pending I/Os. But we still
    // have to watch out for a scenario where another driver can send 
    // requests to our deviceobject directly without opening an handle.
    //
    if( !deviceExtension->Deleted && 
        TRUE == PnpFilterGlobal.IsFilterInNormalState ) { //if not deleted

        status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        irpStack = IoGetCurrentIrpStackLocation (Irp);

        switch (irpStack->MajorFunction) {
            case IRP_MJ_CREATE:
                DebugPrint(("Create \n"));
                break;

            case IRP_MJ_CLOSE:
                DebugPrint(("Close \n"));
                break;

            case IRP_MJ_CLEANUP:
                DebugPrint(("Cleanup \n"));
                break;

            case IRP_MJ_DEVICE_CONTROL:
                status = STATUS_INVALID_DEVICE_REQUEST;
                break;

            case IRP_MJ_INTERNAL_DEVICE_CONTROL:
                DebugPrint(("DeviceIoControl\n"));
                switch (irpStack->Parameters.DeviceIoControl.IoControlCode) {
                    case IOCTL_OC_CONNECT_TO_FILTER:
                    {
                        PVOID    InputBuffer = Irp->AssociatedIrp.SystemBuffer;
                        ULONG    InputBufferLength  = irpStack->Parameters.DeviceIoControl.InputBufferLength;

                        if( InputBufferLength < sizeof( OC_PNP_FILTER_CONNECT_INITIALIZER ) ){

                            status = STATUS_INVALID_BUFFER_SIZE;
                            break;
                        }
                        status = OcFilterConnectionRequired( (POC_PNP_FILTER_CONNECT_INITIALIZER)InputBuffer );
                        break;
                    }
                    case IOCTL_OC_CONNECT_FOR_FIRST_TIME_START:
                    {
                        PVOID    InputBuffer = Irp->AssociatedIrp.SystemBuffer;
                        ULONG    InputBufferLength  = irpStack->Parameters.DeviceIoControl.InputBufferLength;

                        if( InputBufferLength < sizeof( OC_PNP_FILTER_FIRST_TIME_START ) ){

                            status = STATUS_INVALID_BUFFER_SIZE;
                            break;
                        }

                        status = OcInitFilterConnectionForFirstTimeStart( (POC_PNP_FILTER_FIRST_TIME_START)InputBuffer );
                        if( NT_SUCCESS( status ) ){
                            //
                            // as the buffered method is used the system should know 
                            // how many bytes to copy in the caller's buffer
                            //
                            Irp->IoStatus.Information = sizeof( OC_PNP_FILTER_FIRST_TIME_START );
                        }

                        break;
                    }
                    default:
                        status = STATUS_INVALID_PARAMETER;
                        break;
                }
            default:
                break;
        }
    } else {
        ASSERTMSG(FALSE, "Requests being sent to a dead device\n");
        status = STATUS_DEVICE_REMOVED;
    }

    Irp->IoStatus.Status = status;
    IoCompleteRequest (Irp, IO_NO_INCREMENT);
    return status;
}

//---------------------------------------------------

static
VOID
OcFilterDeleteConnectObject(
    IN POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectObject
    )
{
    KeSetEvent( PtrConnectObject->PtrDisconnectEvent,
                IO_DISK_INCREMENT,
                FALSE );
}

//---------------------------------------------------

POC_PNP_FILTER_CONNECT_OBJECT
OcFilterReferenceCurrentConnectObject()
    /*
    function returns the refrenced connection object
    the caller must dereference it when it is not needed
    */
{
    KIRQL    OldIrql;
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    OcRwAcquireLockForRead( &PnpFilterGlobal.RwSpinLock, &OldIrql );
    {//start of the lock
        PtrConnectionObject = PnpFilterGlobal.PtrConnectionObject;
        if( NULL != PtrConnectionObject )
            OcObReferenceObject( PtrConnectionObject );
    }//end of the lock
    OcRwReleaseReadLock( &PnpFilterGlobal.RwSpinLock, OldIrql );

    return PtrConnectionObject;
}

//---------------------------------------------------

NTSTATUS
OcFilterConnectionRequired( 
    IN POC_PNP_FILTER_CONNECT_INITIALIZER    PtrInputInitializer
    )
{
    NTSTATUS    RC;
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectObject = NULL;
    KIRQL       OldIrql;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    if( NULL != PnpFilterGlobal.PtrConnectionObject ){
        //
        // somebidy has already connected to this filter
        //
        return STATUS_TOO_MANY_OPENED_FILES;
    }

    ASSERT( !( PtrInputInitializer->Version.Version < OC_CURRENT_FILTER_VERSION || 
               PtrInputInitializer->PnPFilterExports->Version.Version < OC_CURRENT_FILTER_VERSION ) );

    if( PtrInputInitializer->Version.Version < OC_CURRENT_FILTER_VERSION || 
        PtrInputInitializer->PnPFilterExports->Version.Version < OC_CURRENT_FILTER_VERSION )
        return STATUS_INVALID_DEVICE_REQUEST;

    ASSERT( !( NULL == PtrInputInitializer->CallbackMethods.DispatcherCallback ||
               NULL == PtrInputInitializer->CallbackMethods.ReportNewDevice ||
               NULL == PtrInputInitializer->CallbackMethods.RepotNewDeviceRelations ||
               NULL == PtrInputInitializer->CallbackMethods.RepotNewDeviceState || 
               NULL == PtrInputInitializer->CallbackMethods.UsageNotifyPostCallback || 
               NULL == PtrInputInitializer->CallbackMethods.UsageNotifyPreCallback ) );

    if( NULL == PtrInputInitializer->CallbackMethods.DispatcherCallback ||
        NULL == PtrInputInitializer->CallbackMethods.ReportNewDevice ||
        NULL == PtrInputInitializer->CallbackMethods.RepotNewDeviceRelations ||
        NULL == PtrInputInitializer->CallbackMethods.RepotNewDeviceState || 
        NULL == PtrInputInitializer->CallbackMethods.UsageNotifyPostCallback || 
        NULL == PtrInputInitializer->CallbackMethods.UsageNotifyPreCallback )
        return STATUS_INVALID_PARAMETER;

    RC = OcObCreateObject( &PnpFilterGlobal.ConnectObjectType,
                           &PtrConnectObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    RtlZeroMemory( PtrConnectObject, sizeof( *PtrConnectObject ) );

    PtrConnectObject->Version.Version = OC_CURRENT_FILTER_VERSION;
    PtrConnectObject->Version.Size = sizeof( *PtrConnectObject );
    PtrConnectObject->ClientVersion.Version = PtrInputInitializer->Version.Version;
    PtrConnectObject->ClientVersion.Size = PtrConnectObject->Version.Size;

    PtrConnectObject->PtrDisconnectEvent = PtrInputInitializer->PtrDisconnectEvent;

    PtrConnectObject->CallbackMethods = PtrInputInitializer->CallbackMethods;
    /*
    PtrConnectObject->CallbackMethods.ReportNewDevice = PtrInputInitializer->CallbackMethods.ReportNewDevice;
    PtrConnectObject->CallbackMethods.RepotNewDeviceRelations = PtrInputInitializer->CallbackMethods.RepotNewDeviceRelations;
    PtrConnectObject->CallbackMethods.RepotNewDeviceState = PtrInputInitializer->CallbackMethods.RepotNewDeviceState;
    PtrConnectObject->CallbackMethods.DispatcherCallback = PtrInputInitializer->CallbackMethods.DispatcherCallback;
    PtrConnectObject->CallbackMethods.PreStartCallback = PtrInputInitializer->CallbackMethods.PreStartCallback;
    add the usage notification and other new callbacks if you uncomment this
    */

#if DBG
    {
        ULONG i;
        for( i = 0x0; i < sizeof(PtrConnectObject->CallbackMethods)/sizeof(ULONG_PTR); ++i ){

            if( ((ULONG_PTR)0x0) == ((PULONG_PTR)&PtrConnectObject->CallbackMethods)[i] ){

                //
                // You may argue that the prestart callback is not compulsory, 
                // but it is provided in any case,
                // so if it is NULL something went wrong!
                //

                KeBugCheckEx( OC_PNPFILTER_BUG_UNINITIALIZED_CALLBACK, 
                              (ULONG_PTR)__LINE__,
                              (ULONG_PTR)i, 
                              (ULONG_PTR)PtrConnectObject, 
                              (ULONG_PTR)PtrInputInitializer );
            }
        }//for
    }
#endif//DBG
    //
    // initialize this filter's exports
    //
    PtrInputInitializer->PnPFilterExports->Disconnect = OcFilterDisconnect;
    PtrInputInitializer->PnPFilterExports->ReportDevices = OcFilterReportExisitingDevices;
    PtrInputInitializer->PnPFilterExports->AttachToNonPnPDevice = OcFilterAttachToDevice;

    OcRwAcquireLockForWrite( &PnpFilterGlobal.RwSpinLock, &OldIrql );
    {//start of the lock
        if( NULL == PnpFilterGlobal.PtrConnectionObject )
            PnpFilterGlobal.PtrConnectionObject = PtrConnectObject;
        else
            RC = STATUS_TOO_MANY_OPENED_FILES;
    }//end of the lock
    OcRwReleaseWriteLock( &PnpFilterGlobal.RwSpinLock, OldIrql );

    if( !NT_SUCCESS( RC ) ){

        if( NULL != PtrConnectObject )
            OcObDereferenceObject( PtrConnectObject );
    }

    return RC;
}

//---------------------------------------------------

VOID
NTAPI
OcFilterDisconnect(
    IN PVOID    Context
    )
{
    KIRQL    OldIrql;
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    OcRwAcquireLockForWrite( &PnpFilterGlobal.RwSpinLock, &OldIrql );
    {//start of the lock
        PtrConnectionObject = PnpFilterGlobal.PtrConnectionObject;
        PnpFilterGlobal.PtrConnectionObject = NULL;
    }//end of the lock
    OcRwReleaseWriteLock( &PnpFilterGlobal.RwSpinLock, OldIrql );

    if( NULL != PtrConnectionObject )
        OcObDereferenceObject( PtrConnectionObject );

    return;
}

//---------------------------------------------------

static
NTSTATUS
OcInitFilterConnectionForFirstTimeStart(
    __in POC_PNP_FILTER_FIRST_TIME_START    FirstTimeStartStruct
    )
{
    NTSTATUS    RC = STATUS_SUCCESS;

    if( FirstTimeStartStruct->Version.Version < OC_CURRENT_FILTER_VERSION )
        return STATUS_INVALID_DEVICE_REQUEST;

    FirstTimeStartStruct->AddDeviceFTI = OcFilterAddDeviceFTI;

    return RC;
}

//---------------------------------------------------

#if DBG

PCHAR
PnPMinorFunctionString (
    UCHAR MinorFunction
)
{
    switch (MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            return "IRP_MN_START_DEVICE";
        case IRP_MN_QUERY_REMOVE_DEVICE:
            return "IRP_MN_QUERY_REMOVE_DEVICE";
        case IRP_MN_REMOVE_DEVICE:
            return "IRP_MN_REMOVE_DEVICE";
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            return "IRP_MN_CANCEL_REMOVE_DEVICE";
        case IRP_MN_STOP_DEVICE:
            return "IRP_MN_STOP_DEVICE";
        case IRP_MN_QUERY_STOP_DEVICE:
            return "IRP_MN_QUERY_STOP_DEVICE";
        case IRP_MN_CANCEL_STOP_DEVICE:
            return "IRP_MN_CANCEL_STOP_DEVICE";
        case IRP_MN_QUERY_DEVICE_RELATIONS:
            return "IRP_MN_QUERY_DEVICE_RELATIONS";
        case IRP_MN_QUERY_INTERFACE:
            return "IRP_MN_QUERY_INTERFACE";
        case IRP_MN_QUERY_CAPABILITIES:
            return "IRP_MN_QUERY_CAPABILITIES";
        case IRP_MN_QUERY_RESOURCES:
            return "IRP_MN_QUERY_RESOURCES";
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            return "IRP_MN_QUERY_RESOURCE_REQUIREMENTS";
        case IRP_MN_QUERY_DEVICE_TEXT:
            return "IRP_MN_QUERY_DEVICE_TEXT";
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            return "IRP_MN_FILTER_RESOURCE_REQUIREMENTS";
        case IRP_MN_READ_CONFIG:
            return "IRP_MN_READ_CONFIG";
        case IRP_MN_WRITE_CONFIG:
            return "IRP_MN_WRITE_CONFIG";
        case IRP_MN_EJECT:
            return "IRP_MN_EJECT";
        case IRP_MN_SET_LOCK:
            return "IRP_MN_SET_LOCK";
        case IRP_MN_QUERY_ID:
            return "IRP_MN_QUERY_ID";
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            return "IRP_MN_QUERY_PNP_DEVICE_STATE";
        case IRP_MN_QUERY_BUS_INFORMATION:
            return "IRP_MN_QUERY_BUS_INFORMATION";
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            return "IRP_MN_DEVICE_USAGE_NOTIFICATION";
        case IRP_MN_SURPRISE_REMOVAL:
            return "IRP_MN_SURPRISE_REMOVAL";

        default:
            return "unknown_pnp_irp";
    }
}

#endif//DBG

//---------------------------------------------------

