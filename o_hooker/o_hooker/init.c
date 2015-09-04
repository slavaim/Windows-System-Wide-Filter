/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/

#include "struct.h"
#include "proto.h"
#include <OcHooker.h>
#include <wdmsec.h> // for IoCreateDeviceSecure

/*
This file contains the code for initialization of the driver.
*/

//-------------------------------------------------

static
VOID 
DriverUnload(
    IN PDRIVER_OBJECT DriverObject
    );

//-------------------------------------------------

VOID
OcHookInitGlobalDataMemory()
{
    ULONG    i;
    OC_OBJECT_TYPE_INITIALIZER_VAR( ObjectTypeInitializer );

    ASSERT( OcObIsObjectManagerInitialized() );

    RtlZeroMemory( &Global, sizeof( Global ) );

    OcRwInitializeRwLock( &Global.RwSpinLock );
    ExInitializeResourceLite( &Global.DriverHashResource );

    //
    // initialize the hooked driver object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'rDcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_HOOKED_DRIVER_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcHookerDeleteDriverObject;
    ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &Global.OcHookedDriverObjectType );

    //
    // initialize the object type for the connection object
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'CHcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_HOOKER_CONNECT_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcFilterDeleteConnectObject;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &Global.ConnectObjectType );

    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( Global.InvalidRequestDispatchTable ); ++i ){

        Global.InvalidRequestDispatchTable[ i ] = OcHookerInvalidDeviceRequest;
    }
}

//-------------------------------------------------

NTSTATUS
OcHookCreateCommunicationDevice()
{
    UNICODE_STRING      ntDeviceName;
    UNICODE_STRING      symbolicLinkName;
    NTSTATUS            RC = STATUS_UNSUCCESSFUL;
    UNICODE_STRING      sddlString;

    PAGED_CODE();

    ASSERT( NULL != Global.DriverObject );
    ASSERT( NULL == Global.ControlDeviceObject );

    //
    // Initialize the unicode strings
    //
    RtlInitUnicodeString( &ntDeviceName, HOOKER_NTDEVICE_NAME_STRING );
    RtlInitUnicodeString( &symbolicLinkName, HOOKER_SYMBOLIC_NAME_STRING );

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

    RC = IoCreateDeviceSecure( Global.DriverObject,
                               0x0,
                               &ntDeviceName,
                               FILE_DEVICE_UNKNOWN,
                               FILE_DEVICE_SECURE_OPEN,
                               FALSE, 
                               &sddlString,
                               (LPCGUID)&GUID_SD_HOOKER_CONTROL_OBJECT,
                               &Global.ControlDeviceObject);

    if( NT_SUCCESS( RC ) ){

        Global.ControlDeviceObject->Flags |= DO_BUFFERED_IO;

        RC = IoCreateSymbolicLink( &symbolicLinkName, &ntDeviceName );
        if( !NT_SUCCESS( RC ) ){

            IoDeleteDevice( Global.ControlDeviceObject );
            Global.ControlDeviceObject = NULL;
            DebugPrint( ( "IoCreateSymbolicLink failed %x\n", RC ) );
            goto __exit;
        }

        Global.ControlDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    }else {

        DebugPrint(("IoCreateDevice failed %x\n", RC ));
    }

__exit:
    return RC;
}

//-------------------------------------------------

VOID
OcHookerRemoveControlDeviceObject()
{
    UNICODE_STRING    symbolicLinkName;

    if( NULL == Global.ControlDeviceObject )
        return;

    RtlInitUnicodeString( &symbolicLinkName, HOOKER_SYMBOLIC_NAME_STRING );
    IoDeleteSymbolicLink( &symbolicLinkName );

    if( NULL != Global.ControlDeviceObject )
        IoDeleteDevice( Global.ControlDeviceObject );

    Global.ControlDeviceObject = NULL;
}

//-------------------------------------------------

NTSTATUS 
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS    RC = STATUS_SUCCESS;

    //
    // initialize the object manager
    //
    RC = OcObInitializeObjectManager( NULL );
    if( !NT_SUCCESS( RC ) )
        return RC;

    OcHookInitGlobalDataMemory();
    Global.DriverObject = DriverObject;

    //
    // initialize the worker threads manager
    //
    RC = OcWthInitializeWorkerThreadsSubsystem( NULL );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // initialize the hash manager
    //
    RC = OcHsInitializeHashManager( NULL );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    RC = OcHookCreateCommunicationDevice();
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // create the hash for the hooked driver objects
    //
        //
    // create the hash for the device objects,
    // use the prime number for the hash
    //
    RC = OcHsCreateHash( 29, NULL, &Global.PtrDriverHashObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    DriverObject->MajorFunction[IRP_MJ_CREATE]     = 
    DriverObject->MajorFunction[IRP_MJ_CLOSE]      = 
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]    = 
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = OcHookerDispatchFunction;

__exit:
    if( !NT_SUCCESS( RC ) ){

        DriverUnload( DriverObject );
    }

    return RC;
}

//-------------------------------------------------

VOID 
DriverUnload(
    IN PDRIVER_OBJECT DriverObject
    )
    /*
    Actually, once loaded this driver can't be unloaded.
    This function is used only to clear resources if
    there was an error in DriverEntry.
    */
{
    OcHookerRemoveControlDeviceObject();

    OcObDeleteObjectType( &Global.OcHookedDriverObjectType );
    OcObDeleteObjectType( &Global.ConnectObjectType );

    if( Global.PtrDriverHashObject )
        OcObDereferenceObject( Global.PtrDriverHashObject );
    ExDeleteResourceLite( &Global.DriverHashResource );

    //
    // uninitialize the hash manager
    //
    OcHsUninitializeHashManager( NULL );

    //
    // uninitialize the worker threads subsystem,
    // as a side effect I will wait for stopping of 
    // all worker threads
    //
    OcWthUninitializeWorkerThreadsSubsystem( NULL );

    OcObUninitializeObjectManager( NULL );

}

//-------------------------------------------------