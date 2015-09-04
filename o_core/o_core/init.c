/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
23.11.2006 
 Start
*/

#include "struct.h"
#include "proto.h"
#include <PnPFilterControl.h>

/*
This file contains the code for initialization of the driver 
and its deinitialization.
*/

//-------------------------------------------------

static
VOID 
DriverUnload(
    IN PDRIVER_OBJECT DriverObject
    );

static
NTSTATUS
OcCrConnectToPnpFilter();

static
NTSTATUS
OcCrConnectToHooker();

static
BOOLEAN
OcCrDisconnectFromExternalServicesProvidersIdempotent(
    IN BOOLEAN    Wait
    );

static
NTSTATUS
OcCreateCoreControlObject();

//-------------------------------------------------

VOID
OcCrInitObjectTypes()
{
    OC_OBJECT_TYPE_INITIALIZER_VAR( ObjectTypeInitializer );

    ASSERT( OcObIsObjectManagerInitialized() );

    //
    // initialize the device property object types
    //
    {// start of device types initializing

        //
        // All objects type have the OcObjectTypeZeroObjectBody flag,
        // so the object bodies will be zeroed automatically
        // in the OcObCreateObject.
        //

        //
        // common( unknown devices ) property
        //
        OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
        ObjectTypeInitializer.Flags |= OcObjectTypeZeroObjectBody;
        ObjectTypeInitializer.Tag = '0TcO';
        ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_DEVICE_PROPERTY_OBJECT );
        ObjectTypeInitializer.Methods.DeleteObject = OcCrFreeDevicePropertyObject;
        ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

        OcObInitializeObjectType( &ObjectTypeInitializer,
                                  &Global.OcDevicePropertyCommonType );

        //
        // USB devices property
        //
        OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
        ObjectTypeInitializer.Flags |= OcObjectTypeZeroObjectBody;
        ObjectTypeInitializer.Tag = '1TcO';
        ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_DEVICE_PROPERTY_USB_OBJECT );
        ObjectTypeInitializer.Methods.DeleteObject = OcCrFreeDevicePropertyObject;
        ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

        OcObInitializeObjectType( &ObjectTypeInitializer,
                                  &Global.OcDevicePropertyUsbType );

    }// end of device types initializing

    //
    // initialize the device object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'vDcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_DEVICE_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteDeviceObject;
    ObjectTypeInitializer.Methods.DeleteObjectType = OcCrDeleteDeviceObjectType;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &Global.OcDeviceObjectType );

    //
    // initialize the device relations object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'RDcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_RELATIONS_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteRelationsObject;
    ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &Global.OcDeviceRelationsObjectType );

    //
    // initialize the device type object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'TDcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_DEVICE_TYPE_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteDeviceTypeObject;
    ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &Global.OcDeviceTypeObjectType );

    //
    // initialize the file object and context object types
    //
    {

        //
        // the context object type
        //
        OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
        ObjectTypeInitializer.Tag = 'OFcO';
        ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_CONTEXT_OBJECT );
        ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteContextObject;
        ObjectTypeInitializer.Methods.DeleteObjectType = OcCrDeleteContextObjectType;

        OcObInitializeObjectType( &ObjectTypeInitializer,
                                  &Global.OcFileContextObjectType );

        //
        // the file object type
        //
        OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
        ObjectTypeInitializer.Tag = 'OFcO';
        ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_FILE_OBJECT );
        ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteFileObject;
        ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

        OcObInitializeObjectType( &ObjectTypeInitializer,
                                  &Global.OcFleObjectType );

        //
        // the file object information object type
        //
        OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
        ObjectTypeInitializer.Tag = 'IFcO';
        ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_FILE_OBJECT_CREATE_INFO );
        ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteFileObjectInfo;
        ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

        OcObInitializeObjectType( &ObjectTypeInitializer,
                                  &Global.OcFileCreateInfoObjectType );
    }

    //
    // the operation object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'POcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_OPERATION_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteOperationObject;
    ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &Global.OcOperationObject );
}

//-------------------------------------------------

VOID
OcCrDeleteDevicePropertyTypes()
{
    OcObDeleteObjectType( &Global.OcDevicePropertyUsbType );
    OcObDeleteObjectType( &Global.OcDevicePropertyCommonType );
}

//-------------------------------------------------

ULONG
OcGetNumberOfProcessorUnits()
{
    ULONG    NumberOfProcessorUnits = 0x0;

#ifdef _AMD64_
    {
        KAFFINITY   ProcessorsMask;
        ULONG       i;
        ProcessorsMask = KeQueryActiveProcessors();
        i = 0x0;
        while( i < ( sizeof( ProcessorsMask )*0x8 ) ){

            if( 0x0 != ( ProcessorsMask & (((ULONG_PTR)0x1)<<i) ) )
                ++NumberOfProcessorUnits;

            ++i;
        }
    }
#else//_AMD64_
    {
        NTSTATUS    RC;
        SYSTEM_BASIC_INFORMATION    SystemInformation;

        //
        // get system information, do not check return value- in case of error
        // default settings will be used for nil value in infor
        //
        RC = ZwQuerySystemInformation( SystemBasicInformation,
                                       &SystemInformation,
                                       sizeof( SystemInformation ),
                                       NULL );

        if( NT_SUCCESS( RC ) )
            NumberOfProcessorUnits = SystemInformation.NumberProcessors;

        ASSERT( NT_SUCCESS( RC ) );
    }
#endif//else _AMD64_

    ASSERT( 0x0 != NumberOfProcessorUnits );

    //
    // in case of error set number of CPUs to two - 
    // two cors CPUs are experiencing the great proliferation
    //
    if( 0x0 == NumberOfProcessorUnits )
        NumberOfProcessorUnits = 0x2;

    return NumberOfProcessorUnits;
}

//-------------------------------------------------

VOID
OcInitGlobalFieldsThatAlwaysSucceed()
{
    ULONG             i;
    UNICODE_STRING    KernelFunctionName;

    ASSERT( OcObIsObjectManagerInitialized() );

    RtlZeroMemory( &Global, sizeof( Global ) );

    ExInitializeResourceLite( &Global.DeviceHashResource );

    //
    // Create the events related with the external services
    // providers in a signal state, this means
    // that nothing has been intialized and KeWaitFor*
    // will return immediatelly. When the corresponding
    // subsystem is initialized an event will be reset
    //
    {
        KeInitializeEvent( &Global.PnPFilterDisconnectionEvent,
                           NotificationEvent,
                           TRUE );

        KeInitializeEvent( &Global.HookerDisconnectionEvent,
                           NotificationEvent,
                           TRUE );
    }

    KeInitializeEvent( &Global.OcDeviceObjectTypeUninitializationEvent,
                       NotificationEvent,
                       FALSE );

    KeInitializeEvent( &Global.OcContextObjectTypeUninitializationEvent,
                       NotificationEvent,
                       FALSE );

    OcCrInitObjectTypes();

    RtlInitUnicodeString( &KernelFunctionName, L"IoOpenDeviceRegistryKey" );
    Global.SystemFunctions.IoOpenDeviceRegistryKey = MmGetSystemRoutineAddress( &KernelFunctionName );

    RtlInitUnicodeString( &KernelFunctionName, L"IoGetDeviceProperty" );
    Global.SystemFunctions.IoGetDeviceProperty = MmGetSystemRoutineAddress( &KernelFunctionName );

    RtlInitUnicodeString( &KernelFunctionName, L"IoGetAttachedDeviceReference" );
    Global.SystemFunctions.IoGetAttachedDeviceReference = MmGetSystemRoutineAddress( &KernelFunctionName );

    RtlInitUnicodeString( &KernelFunctionName, L"IoGetDeviceAttachmentBaseRef" );
    Global.SystemFunctions.IoGetDeviceAttachmentBaseRef = MmGetSystemRoutineAddress( &KernelFunctionName );

    RtlInitUnicodeString( &KernelFunctionName, L"IoGetLowerDeviceObject" );
    Global.SystemFunctions.IoGetLowerDeviceObject = MmGetSystemRoutineAddress( &KernelFunctionName );

    //
    // TO DO, create the driver which will import and export this function, if 
    // its load failed then there is no minifilter manager in a system
    //
    Global.MinifilterFunctions.FltRegisterFilter = FltRegisterFilter;
    Global.MinifilterFunctions.FltUnregisterFilter = FltUnregisterFilter;
    Global.MinifilterFunctions.FltStartFiltering = FltStartFiltering;
    Global.MinifilterFunctions.FltGetRoutineAddress = FltGetRoutineAddress;

    OcRlInitializeRemoveLock( &Global.RemoveLock.Common, OcFreeRemoveLock );

    //
    // initialize the global device object list
    //
    InitializeListHead( &Global.DevObjListHead );
    OcRwInitializeRwLock( &Global.DevObjListLock );

    //
    // init minifilter descriptor
    //
    Global.FsdMinifilter.State = OC_FSD_MF_NOT_REGISTERED;

    //
    // get the system's memory size
    //
    Global.SystemSize = MmQuerySystemSize();

    //
    // get the number of processors
    //
    Global.NumberOfProcessorUnits = OcGetNumberOfProcessorUnits();

    //
    // set the number which is used as a base for determination
    // about the number of different worker threads
    //
    if( Global.NumberOfProcessorUnits <= 0x2 )
        Global.BaseNumberOfWorkerThreads = 0x2;
    else
        Global.BaseNumberOfWorkerThreads = Global.NumberOfProcessorUnits;

    for( i = 0x0; i< OC_STATIC_ARRAY_SIZE( Global.FoCtxQueueLock ); ++i ){

        OcQlInitializeQueueLock( &Global.FoCtxQueueLock[ i ] );
    }

    ASSERT( Global.BaseNumberOfWorkerThreads >= 0x2 );

}

//-------------------------------------------------

NTSTATUS
OcCreateCoreControlObject(
    __in PUNICODE_STRING RegistryPath
    )
{
    UNICODE_STRING      ntDeviceName;
    UNICODE_STRING      symbolicLinkName;
    NTSTATUS            RC = STATUS_UNSUCCESSFUL;
    UNICODE_STRING      sddlString;
    HANDLE              RegKeyHandle = NULL;
    OBJECT_ATTRIBUTES   RegKeyAttr;
    PWCHAR              ControlDeviceNameStr = NULL;
    ULONG               StrLengthInBytes = 0x0;
    PWCHAR              FullControlDeviceNameStr = NULL;
    PWCHAR              FullSymbolicLinkStr = NULL;
    UNICODE_STRING      ControlDeviceValueKey;
    PKEY_VALUE_PARTIAL_INFORMATION    KeyValuInfo = NULL;
    ULONG                             KeyValuInfoLength;

    PAGED_CODE();

    ASSERT( NULL != Global.DriverObject );
    ASSERT( NULL == Global.ControlDeviceObject );

    //
    // Initialize the unicode strings
    //
    //RtlInitUnicodeString( &ntDeviceName, NTDEVICE_NAME_STRING_CORE );
    //RtlInitUnicodeString( &symbolicLinkName, SYMBOLIC_NAME_STRING_CORE );

    //
    // open and query the registry for the device name
    //
    InitializeObjectAttributes( &RegKeyAttr,
                                RegistryPath,
                                0x0,
                                NULL,
                                NULL );

    RC = ZwOpenKey( &RegKeyHandle,
                    GENERIC_READ,
                    &RegKeyAttr );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    RtlInitUnicodeString( &ControlDeviceValueKey, L"ControlDeviceName" );
    RC = OcCrGetValueFromKey( RegKeyHandle,
                              &ControlDeviceValueKey,
                              KeyValuePartialInformation,
                              &KeyValuInfo,
                              &KeyValuInfoLength );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    if( REG_SZ != KeyValuInfo->Type ){

        RC = STATUS_INVALID_PARAMETER;
        goto __exit;
    }

    //
    // retrieve the name from the structure
    //
    ControlDeviceNameStr = (PWCHAR)&KeyValuInfo->Data;
    StrLengthInBytes = KeyValuInfo->DataLength;

    //
    // zero terminate the string, this is a precautious measure
    //
    ControlDeviceNameStr[ StrLengthInBytes/sizeof( WCHAR ) - 0x1 ] = L'\0';

    //
    // allocate a bufer for the full device name
    //
    FullControlDeviceNameStr = ExAllocatePoolWithTag( PagedPool,
                                sizeof( NT_DEVICE_DIRECTORY ) + StrLengthInBytes,
                                'tScO' );
    if( NULL == FullControlDeviceNameStr ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    //
    // concatenate the directory name and the device name
    //
    RtlCopyMemory( FullControlDeviceNameStr,
                   NT_DEVICE_DIRECTORY,
                   sizeof( NT_DEVICE_DIRECTORY ) );

    RtlCopyMemory( FullControlDeviceNameStr + sizeof( NT_DEVICE_DIRECTORY )/sizeof( WCHAR ) - 0x1,
                   ControlDeviceNameStr,
                   StrLengthInBytes );

    RtlInitUnicodeString( &ntDeviceName, FullControlDeviceNameStr );

    //
    // allocate a buffer for the full symbolic link name
    //
    FullSymbolicLinkStr = ExAllocatePoolWithTag( PagedPool,
                                sizeof( NT_SYM_LINK_DIRECTORY ) + StrLengthInBytes,
                                'tScO' );
    if( NULL == FullSymbolicLinkStr ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    //
    // concatenate the directory name and the symbolic link name
    //
    RtlCopyMemory( FullSymbolicLinkStr,
                   NT_SYM_LINK_DIRECTORY,
                   sizeof( NT_SYM_LINK_DIRECTORY ) );

    RtlCopyMemory( FullSymbolicLinkStr + sizeof( NT_SYM_LINK_DIRECTORY )/sizeof( WCHAR ) - 0x1,
                   ControlDeviceNameStr,
                   StrLengthInBytes );

    RtlInitUnicodeString( &symbolicLinkName, FullSymbolicLinkStr );

    //
    // save the symbolic name string
    //
    ASSERT( NULL == Global.FullSymbolicLinkStr );
    Global.FullSymbolicLinkStr = FullSymbolicLinkStr;
    FullSymbolicLinkStr = NULL;

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
                               (LPCGUID)&GUID_SD_CORE_CONTROL_OBJECT,
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

    if( NULL != FullSymbolicLinkStr )
        ExFreePoolWithTag( FullSymbolicLinkStr, 'tScO' );

    if( NULL != FullControlDeviceNameStr )
        ExFreePoolWithTag( FullControlDeviceNameStr, 'tScO' );

    if( NULL != RegKeyHandle )
        ZwClose( RegKeyHandle );

    if( NULL != KeyValuInfo )
        OcCrFreeValueFromKey( KeyValuInfo );

    return RC;
}

//-------------------------------------------------

VOID
OcRemoveCoreControlDeviceObjectIdempotent()
{
    PDEVICE_OBJECT    ControlDeviceObject;
    PWCHAR            FullSymbolicLinkStr;

    //
    // at first delete the symbolic link as this also
    // frees the buffer allocated for the name and 
    // this is a benign operation if the symbolic link 
    // doesn't exist
    //
    FullSymbolicLinkStr = Global.FullSymbolicLinkStr;
    if( FullSymbolicLinkStr == InterlockedCompareExchangePointer( &Global.FullSymbolicLinkStr, NULL, FullSymbolicLinkStr ) &&
        NULL != FullSymbolicLinkStr ){

        UNICODE_STRING    symbolicLinkName;

        RtlInitUnicodeString( &symbolicLinkName, FullSymbolicLinkStr );
        IoDeleteSymbolicLink( &symbolicLinkName );

        //
        // free the memory allocated for the symbolic link name
        //
        ExFreePoolWithTag( FullSymbolicLinkStr, 'tScO' );
    }

    ASSERT( NULL == Global.FullSymbolicLinkStr );

    //
    // delete the communication device object
    //
    ControlDeviceObject = Global.ControlDeviceObject;
    if( ControlDeviceObject != InterlockedCompareExchangePointer( &Global.ControlDeviceObject, NULL, ControlDeviceObject ) )
        return;

    if( NULL == ControlDeviceObject )
        return;

    ASSERT( NULL == Global.ControlDeviceObject );

    IoDeleteDevice( ControlDeviceObject );

}

//-------------------------------------------------

NTSTATUS 
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS     RC = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER( RegistryPath );

    DbgBreakPoint();
    //
    // The first step is initializing of the object manager
    //
    RC = OcObInitializeObjectManager( NULL );
    if( !NT_SUCCESS( RC ) ){

        ASSERT( !"OcObInitializeObjectManager failed" );
        return RC;
    }

    //
    // initialize the global data fields which initialization can't fail
    //
    OcInitGlobalFieldsThatAlwaysSucceed();
    Global.DriverObject = DriverObject;

    //
    // lock the driver from the premature uninitialization in DriverUnload
    //
    RC = OcRlAcquireRemoveLock( &Global.RemoveLock.Common );
    if( !NT_SUCCESS( RC ) ){

        ASSERT( !"OcRlAcquireRemoveLock failed" );
        goto __exit;
    }

    //
    // initialize the worker threads manager
    //
    RC = OcWthInitializeWorkerThreadsSubsystem( NULL );
    if( !NT_SUCCESS( RC ) ){

        ASSERT( !"OcWthInitializeWorkerThreadsSubsystem failed" );
        goto __exit;
    }

    //
    // initialize the hash manager
    //
    RC = OcHsInitializeHashManager( NULL );
    if( !NT_SUCCESS( RC ) ){

        ASSERT( !"OcHsInitializeHashManager failed" );
        goto __exit;
    }

    //
    // initialize security descriptors subsystem
    //
    RC = OcSdInitializeSecurityDescriptorsSubsystem();
    if( !NT_SUCCESS( RC ) ){

        ASSERT( !"OcSdInitializeSecurityDescriptorsSubsystem failed" );
        goto __exit;
    }

    //
    // create the communication device object
    //
    RC = OcCreateCoreControlObject( RegistryPath );
    if( !NT_SUCCESS( RC ) ){

        ASSERT( !"OcCreateCoreControlObject failed" );
        goto __exit;
    }

    //
    // initialize the completion hooker manager
    //
    OcCmHkInitializeCompletionHooker();

    //
    // initialize the IO Manager
    //
    OcCrInitializeIoManager();

    //
    // initialize the DlDriver connection subsystem
    //
    OcInitializeDlDriverConnectionSubsystem();

    RC = OcCrInitializeShadowingSubsystem();
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // create the worker threads pool with shared work items list
    //
    RC = OcTplCreateThreadPool( Global.BaseNumberOfWorkerThreads,
                                FALSE,//shared work items list
                                &Global.ThreadsPoolObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // create the worker threads pool with shared work items list
    //
    RC = OcTplCreateThreadPool( Global.BaseNumberOfWorkerThreads,
                                FALSE,//shared work items list
                                &Global.StartDeviceThreadsPoolObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // create the worker threads pool with shared work items list
    //
    RC = OcTplCreateThreadPool( 0x3*Global.BaseNumberOfWorkerThreads + 0x1,
                                FALSE,//shared work items list
                                &Global.IrpCompletionThreadsPoolObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // create the worker threads pool with shared work items list
    //
    RC = OcTplCreateThreadPool( 0x3*Global.BaseNumberOfWorkerThreads,
                                FALSE,//shared work items list
                                &Global.IoRequestsThreadsPoolObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // create the worker thread for device's property querying
    //
    RC = OcWthCreateWorkerThread( 0x0,
                                  NULL,//create private queue
                                  &Global.QueryDevicePropertyThread );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // create the hash for the device objects,
    // use the prime number for the hash
    //
    RC = OcHsCreateHash( 127, NULL, &Global.PtrDeviceHashObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // if the FuncValueToHashLineIndex will be provided then arrange the OcLockFoContextHashLine
    //
    RC = OcHsCreateHash( OC_FO_CTX_HASH_LINES_NUMBER, NULL, &Global.PtrFoContextHashObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    RC = OcHsCreateHash( OC_FO_CTX_HASH_LINES_NUMBER, NULL, &Global.PtrFoHashObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

#if DBG
    OcTplTest( Global.ThreadsPoolObject );
#endif//DBG

    //
    // connect to the filter
    //
    RC = OcCrConnectToPnpFilter();
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // connect to the hooker
    //
    RC = OcCrConnectToHooker();
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //OcCrInitializeImageNotifySubsystem();

    DriverObject->MajorFunction[ IRP_MJ_CREATE ] = OcCoreCreateDispatch;
    DriverObject->MajorFunction[ IRP_MJ_CLEANUP ] = OcCoreCleanupDispatch;
    DriverObject->MajorFunction[ IRP_MJ_CLOSE ] = OcCoreCloseDispatch;
    DriverObject->MajorFunction[ IRP_MJ_DEVICE_CONTROL ] = OcCoreDeviceControlDispatch;

    DriverObject->DriverUnload = DriverUnload;

    //
    // start the FSD subsystem
    //
    RC = OcCrInitializeFsdSubsystem();
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // from here the driver starts to receive IO and PnP requests
    //
    {
        //
        // iomngr and FSD subsystem share some services, for example for 
        // create requests processing
        //
        ASSERT( TRUE == Global.FsdSubsystemInit );

        //
        // told the PnP filter to report about devices
        //
        Global.ThreadCallingReportDevice = PsGetCurrentThread();
        {
            Global.PnPFilterExports.ReportDevices();
        }
        Global.ThreadCallingReportDevice = NULL;

        Global.DeviceInformationCollected = TRUE;

        //
        // now process all reported devices for they
        // report about their children
        //
        ASSERT( NULL == Global.ThreadCallingReportDevice );
        OcCrProcessReportedDevices();

        //
        // start the FSD subsystem only after gathering 
        // device information, because the FSD subsystem 
        // uses the iomngr subsystem to retrieve device names
        //
        RC = OcCrStartFsdFiltering();
        if( !NT_SUCCESS( RC ) )
            goto __exit;
    }

__exit:

    ASSERT( NT_SUCCESS( RC ) );

    if( !NT_SUCCESS( RC ) )
        DriverUnload( DriverObject );

    return RC;
}

//-------------------------------------------------

VOID 
DriverUnload(
    IN PDRIVER_OBJECT DriverObject
    )
{

    UNREFERENCED_PARAMETER( DriverObject );

    //
    // disconnect from the PnP filter, hooker and other service providers
    // and wait untill there are no instruction pointers in callbacks
    //
    OcCrDisconnectFromExternalServicesProvidersIdempotent( TRUE );

    //
    // wait until all critical paths have been completed,
    // all who want to know whether the driver is unloading
    // can try to acquire the remove lock
    //
    OcRlReleaseRemoveLockAndWait( &Global.RemoveLock.Common );

    //
    // the first action is disconnecting from the
    // PnP filter and hooker, this stops calling of callbacks
    // and I will be sure that there is no
    // synchronization issue with the PnP filter, hooker
    // and assynchronious calling of their callbacks.
    //
    OcCrProcessQueryUnloadRequestIdempotent( TRUE );

    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.PnPFilterExports.Disconnect );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.PnPFilterDeviceObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.DriverHookerExports.Disconnect );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.HookerDeviceObject );

    //
    // stop the shadowing subsystem
    //
    OcCrUnInitializeShadowingSubsystem();

    //
    // purge the hash and delete it
    //
    if( NULL != Global.PtrFoHashObject ){

        //
        // purge all entries from the hash
        //
        OcHsPurgeAllEntriesFromHash( Global.PtrFoHashObject,
                                     OcCrProcessFileObjectRemovedFromHash );

        //
        // delete the hash
        //
        OcObDereferenceObject( Global.PtrFoHashObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.PtrFoHashObject );
    }

    //
    // purge the hash and delete it, actually it must be cleared during
    // file objects removing
    //
    if( NULL != Global.PtrFoContextHashObject ){

        //
        // purge all entries from the hash, dereference them
        //
        OcHsPurgeAllEntriesFromHash( Global.PtrFoContextHashObject,
                                     OcCrProcessFoContextRemovedFromHash );

        //
        // delete the hash
        //
        OcObDereferenceObject( Global.PtrFoContextHashObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.PtrFoContextHashObject );
    }

    //
    // purge the hash and delete it
    //
    if( NULL != Global.PtrDeviceHashObject ){

        //
        // purge all entries from the hash, dereference them
        //
        OcHsPurgeAllEntriesFromHash( Global.PtrDeviceHashObject,
                                     OcCrCleanupDeviceAfterRemovingFromHash );

        //
        // delete the hash
        //
        OcObDereferenceObject( Global.PtrDeviceHashObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.PtrDeviceHashObject );
    }

    OcCrDeleteDevicePropertyTypes();

    {
        //
        // delete the operation object type
        //
        OcObDeleteObjectType( &Global.OcOperationObject );

        //
        // first delete the file object and then the context type and create info
        //
        OcObDeleteObjectType( &Global.OcFleObjectType );
        OcObDeleteObjectType( &Global.OcFileContextObjectType );
        OcObDeleteObjectType( &Global.OcFileCreateInfoObjectType );

        //
        // wait for the deleting of all file and context objects
        //
        KeWaitForSingleObject( &Global.OcContextObjectTypeUninitializationEvent,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL);

        //
        // first delete the relations object, because the
        // relations objects reference the device objects
        //
        OcObDeleteObjectType( &Global.OcDeviceRelationsObjectType );
        OcObDeleteObjectType( &Global.OcDeviceObjectType );
        OcObDeleteObjectType( &Global.OcDeviceTypeObjectType );

        //
        // wait for the deleting of all device objects
        //
        KeWaitForSingleObject( &Global.OcDeviceObjectTypeUninitializationEvent,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL);
    }

    //
    // the general purpose threads pool is used by the device objects manager,
    // so it must be uninitialized after the device objects manager
    //
    if( NULL != Global.ThreadsPoolObject ){

        OcObDereferenceObject( Global.ThreadsPoolObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.ThreadsPoolObject );
    }

    //
    // derefrence the threads pools, when the reference drops to
    // zero the pool's threads will be stopped synchronously
    //

    if( NULL != Global.StartDeviceThreadsPoolObject ){

        OcObDereferenceObject( Global.StartDeviceThreadsPoolObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.StartDeviceThreadsPoolObject );
    }

    if( NULL != Global.IrpCompletionThreadsPoolObject ){

        OcObDereferenceObject( Global.IrpCompletionThreadsPoolObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.IrpCompletionThreadsPoolObject );
    }

    if( NULL != Global.IoRequestsThreadsPoolObject ){

        OcObDereferenceObject( Global.IoRequestsThreadsPoolObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.IoRequestsThreadsPoolObject );
    }

    if( NULL != Global.QueryDevicePropertyThread ){

        OcObDereferenceObject( Global.QueryDevicePropertyThread );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Global.QueryDevicePropertyThread );
    }

    //
    // Now uninitialize the subsystem managers
    //

    //
    // uninitialize the IO MAnager
    //
    OcCrUnInitializeIoManager();

    //
    // uninitialize security descriptors subsystem
    //
    OcSdUninitializeSecurityDescriptorsSubsystem();

    //
    // uninitialize the hash manager
    //
    OcHsUninitializeHashManager( NULL );

    //
    // uninitialize the worker threads subsystem,
    // as a side effect this function will wait 
    // for stopping of all worker threads
    //
    OcWthUninitializeWorkerThreadsSubsystem( NULL );

    //
    // uninitialize objects subsystem after all 
    // subsystems that depend on it
    //
    OcObUninitializeObjectManager( NULL );

    //
    // delete the device objects
    //
    OcRemoveCoreControlDeviceObjectIdempotent();

    //
    // uninitialize the completion hooker manager
    //
    OcCmHkUninitializeCompletionHooker();

    ExDeleteResourceLite( &Global.DeviceHashResource );

}

//-------------------------------------------------

BOOLEAN
OcCrDisconnectFromExternalServicesProvidersIdempotent(
    IN BOOLEAN    Wait
    )
/*
  The function must have an idempotent behavior!
  This function is used to break the connections with
  the external services providers such as
  PnP filter and drivers hooker to allow another
  client to connect to them.
  The caller must synchronise the calling of this functon.
*/
{
    BOOLEAN    FullDisconnection = TRUE;

    /////////////////////////////////////////////////////////////////
    //
    // Disconnection section
    //
    //////////////////////////////////////////////////////////////////

    //
    // unregister FSD notification
    //
    OcCrUnRegistreFsRegistrationChangeIdempotent( Global.DriverObject );

    //
    // unregiser the minifilter
    //
    OcCrFsmfUnregisterMinifilterIdempotent();

    //
    // disconnect from the PnP filter
    //
    if( NULL != Global.PnPFilterExports.Disconnect ){

        Global.PnPFilterExports.Disconnect( NULL );
        Global.PnPFilterExports.Disconnect = NULL;

    }

    //
    // now disconnect from the drivers hooker
    //
    if( NULL != Global.DriverHookerExports.Disconnect ){

        Global.DriverHookerExports.Disconnect( NULL );
        Global.DriverHookerExports.Disconnect = NULL;
    }

    /////////////////////////////////////////////////////////////////
    //
    // Waiting section
    //
    //////////////////////////////////////////////////////////////////

    //
    // now start waiting if necessary
    //

    if( Wait ){

        //
        // wait until there is no instruction
        // pointer in any callback
        //
        KeWaitForSingleObject( &Global.PnPFilterDisconnectionEvent,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL );

        //
        // wait until there is no instruction
        // pointer in any callback
        //
        KeWaitForSingleObject( &Global.HookerDisconnectionEvent,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL );

    } else {

        //
        // only check the event's state
        //
        NTSTATUS    RC;
        LARGE_INTEGER    Timeout = { 0x0 };

        RC = KeWaitForSingleObject( &Global.PnPFilterDisconnectionEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    &Timeout );

        if( !NT_SUCCESS( RC ) )
            FullDisconnection = FALSE;

        RC = KeWaitForSingleObject( &Global.HookerDisconnectionEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    &Timeout );

        if( !NT_SUCCESS( RC ) )
            FullDisconnection = FALSE;

    }

    /////////////////////////////////////////////////////////////////
    //
    // Releasing objects section
    //
    //////////////////////////////////////////////////////////////////

    //
    // dereference the connection objects
    //
    if( NULL != Global.PnPFilterDeviceObject ){

        ObDereferenceObject( Global.PnPFilterDeviceObject );
        Global.PnPFilterDeviceObject = NULL;
    }

    if( NULL != Global.HookerDeviceObject ){

        ObDereferenceObject( Global.HookerDeviceObject );
        Global.HookerDeviceObject = NULL;
    }

    return FullDisconnection;
}

//-------------------------------------------------

BOOLEAN
OcCrProcessQueryUnloadRequestIdempotent(
    IN BOOLEAN    Wait
    )
    /*
    The function must have an idempotent behavior
    */
{
    BOOLEAN    FullDisconnection;

    if( 0x0 != InterlockedCompareExchange( &Global.ConcurrentUnloadQueryCounter, 0x1, 0x0 ) ){

        //
        // the client has commited the breach of the contract with
        // the driver and has issued the unload query from
        // concurrent threads, in this case this is a client's
        // responsibilty to synchronize execution!
        //
        return FALSE;
    }

    FullDisconnection = OcCrDisconnectFromExternalServicesProvidersIdempotent( Wait );

    OcCrUninitializeFsdSubsystemIdempotent( Wait );

    //
    // purge the file objects hash, this is an idempotent operation
    //
    if( NULL != Global.PtrFoHashObject ){

        //
        // purge all entries from the hash
        //
        OcHsPurgeAllEntriesFromHash( Global.PtrFoHashObject,
                                     OcCrProcessFileObjectRemovedFromHash );
    }

    //
    // purge the file object contexts hash, this is an idempotent operation
    //
   if( NULL != Global.PtrFoContextHashObject ){

        //
        // purge all entries from the hash, dereference them
        //
        OcHsPurgeAllEntriesFromHash( Global.PtrFoContextHashObject,
                                     OcCrProcessFoContextRemovedFromHash );
   }

    //
    // purge the device objects hash, this is an idempotent operation
    //
    if( NULL != Global.PtrDeviceHashObject ){

        //
        // purge all entries from the hash, dereference them
        //
        OcHsPurgeAllEntriesFromHash( Global.PtrDeviceHashObject,
                                     OcCrCleanupDeviceAfterRemovingFromHash );
    }

    //
    // stop notifying the client about images loading
    //
    //OcCrUninitializeImageNotifySubsystemIdempotent();

    InterlockedExchange( &Global.ConcurrentUnloadQueryCounter, 0x0 );

    return FullDisconnection;
}

//-------------------------------------------------

static
NTSTATUS
OcCrConnectToPnpFilter()
{
    NTSTATUS                  RC;
    HANDLE                    hDeviceHandle = NULL;
    OBJECT_ATTRIBUTES         ObjectAttributes;
    IO_STATUS_BLOCK           IoStatusBlock;
    UNICODE_STRING            ObjectName;
    PFILE_OBJECT              PnPFilterFileObject = NULL;
    OC_PNP_FILTER_CONNECT_INITIALIZER    FilterConnectInitializer = {0x0};
    KEVENT                    Event;
    PIRP                      Irp = NULL;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    KeInitializeEvent( &Event, SynchronizationEvent, FALSE );

    RtlInitUnicodeString( &ObjectName, NTDEVICE_NAME_STRING_PNPFILTER );

    InitializeObjectAttributes( &ObjectAttributes,
                                &ObjectName, 
                                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, 
                                NULL, 
                                NULL );

    //
    // open the PnP filter's communication device
    //
    RC = ZwCreateFile( &hDeviceHandle, 
                       FILE_READ_ACCESS | FILE_WRITE_ACCESS, 
                       &ObjectAttributes, 
                       &IoStatusBlock, 
                       0, 
                       FILE_ATTRIBUTE_NORMAL, 
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                       FILE_OPEN, 
                       FILE_NON_DIRECTORY_FILE, 
                       NULL, 
                       0 );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS(RC) )
        return RC;

    RC = ObReferenceObjectByHandle( hDeviceHandle, 
                                    FILE_ANY_ACCESS,
                                    *IoFileObjectType,
                                    KernelMode,
                                    &PnPFilterFileObject,
                                    NULL );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){
        goto __exit;
    }

    //
    // initialize the header for PnP filter exported callbacks
    //
    Global.PnPFilterExports.Version.Version = OC_CURRENT_FILTER_VERSION;
    Global.PnPFilterExports.Version.Size = sizeof( Global.PnPFilterExports );

    //
    // initialize the connection object initializer
    //
    FilterConnectInitializer.Version.Version = OC_CURRENT_FILTER_VERSION;
    FilterConnectInitializer.Version.Size = sizeof( FilterConnectInitializer );
    FilterConnectInitializer.PtrDisconnectEvent = &Global.PnPFilterDisconnectionEvent;
    FilterConnectInitializer.CallbackMethods.ReportNewDevice = OcCrPnPFilterReportNewDevice;
    FilterConnectInitializer.CallbackMethods.RepotNewDeviceState = OcCrPnPFilterRepotNewDeviceState;
    FilterConnectInitializer.CallbackMethods.RepotNewDeviceRelations = OcCrPnPFilterRepotNewDeviceRelations;
    FilterConnectInitializer.CallbackMethods.UsageNotifyPreCallback = OcCrPnPFilterDeviceUsageNotificationPreOperationCallback;
    FilterConnectInitializer.CallbackMethods.UsageNotifyPostCallback = OcCrPnPFilterDeviceUsageNotificationPostOperationCallback;
    FilterConnectInitializer.CallbackMethods.PreStartCallback = OcPnPFilterPreStartCallback;
    FilterConnectInitializer.CallbackMethods.DispatcherCallback = OcCrPnPFilterIoDispatcherCallback;
    FilterConnectInitializer.PnPFilterExports = &Global.PnPFilterExports;

    //
    // create an Irp for an internal device IOCTL
    //
    Irp = IoBuildDeviceIoControlRequest( IOCTL_OC_CONNECT_TO_FILTER,
                                         PnPFilterFileObject->DeviceObject,
                                         (PVOID)&FilterConnectInitializer,
                                         FilterConnectInitializer.Version.Size,
                                         (PVOID)NULL,
                                         0x0,
                                         TRUE,
                                         &Event,
                                         &IoStatusBlock );
    ASSERT( NULL != Irp );
    if( NULL == Irp ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    //
    // call the PnP filter
    //
    RC = IoCallDriver( PnPFilterFileObject->DeviceObject, Irp );
    Irp = NULL;
    if( STATUS_PENDING == RC ){

        KeWaitForSingleObject( &Event,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL);

        RC = IoStatusBlock.Status;
    }
    ASSERT( NT_SUCCESS( RC ) );

__exit:

    if( NT_SUCCESS( RC ) ){

        ASSERT( PnPFilterFileObject );
        ObReferenceObject( PnPFilterFileObject->DeviceObject );
        Global.PnPFilterDeviceObject = PnPFilterFileObject->DeviceObject;

        //
        // all has gone smoothy, so reset the event, 
        // it will be set in a signal state by the 
        // the service provider( i.e. PnP filter )
        //
        KeResetEvent( &Global.PnPFilterDisconnectionEvent );
    }

    if( NULL != hDeviceHandle )
        ZwClose( hDeviceHandle );

    if( NULL != PnPFilterFileObject )
        ObDereferenceObject( PnPFilterFileObject);
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PnPFilterFileObject );

    if( NULL != Irp )
        IoFreeIrp( Irp );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Irp );

    return RC;
}

//-------------------------------------------------

static
NTSTATUS
OcCrConnectToHooker()
{
    NTSTATUS                  RC;
    HANDLE                    hDeviceHandle = NULL;
    OBJECT_ATTRIBUTES         ObjectAttributes;
    IO_STATUS_BLOCK           IoStatusBlock;
    UNICODE_STRING            ObjectName;
    PFILE_OBJECT              HookerFileObject = NULL;
    OC_HOOKER_CONNECT_INITIALIZER    HookerConnectInitializer = {0x0};
    KEVENT                    Event;
    PIRP                      Irp = NULL;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    KeInitializeEvent( &Event, SynchronizationEvent, FALSE );

    RtlInitUnicodeString( &ObjectName, HOOKER_NTDEVICE_NAME_STRING );

    InitializeObjectAttributes( &ObjectAttributes,
                                &ObjectName, 
                                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, 
                                NULL, 
                                NULL );

    //
    // open the PnP filter's communication device
    //
    RC = ZwCreateFile( &hDeviceHandle, 
                       FILE_READ_ACCESS | FILE_WRITE_ACCESS, 
                       &ObjectAttributes, 
                       &IoStatusBlock, 
                       0, 
                       FILE_ATTRIBUTE_NORMAL, 
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                       FILE_OPEN, 
                       FILE_NON_DIRECTORY_FILE, 
                       NULL, 
                       0 );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS(RC) )
        return RC;

    RC = ObReferenceObjectByHandle( hDeviceHandle, 
                                    FILE_ANY_ACCESS,
                                    *IoFileObjectType,
                                    KernelMode,
                                    &HookerFileObject,
                                    NULL );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){
        goto __exit;
    }

    //
    // initialize the header for the hooker exported callbacks
    //
    Global.DriverHookerExports.Version.Version = OC_CURRENT_HOOKER_VERSION;
    Global.DriverHookerExports.Version.Size = sizeof( Global.DriverHookerExports );

    //
    // initialize the connection object initializer
    //
    HookerConnectInitializer.Version.Version = OC_CURRENT_HOOKER_VERSION;
    HookerConnectInitializer.Version.Size = sizeof( HookerConnectInitializer );
    HookerConnectInitializer.PtrDisconnectEvent = &Global.HookerDisconnectionEvent;
    HookerConnectInitializer.CallbackMethods.DriverDispatch = OcCrHookedDriverDispatch;
    HookerConnectInitializer.DriverHookerExports = &Global.DriverHookerExports;

    //
    // create an Irp for an internal device IOCTL
    //
    Irp = IoBuildDeviceIoControlRequest( IOCTL_OC_CONNECT_TO_HOOKER,
                                         HookerFileObject->DeviceObject,
                                         (PVOID)&HookerConnectInitializer,
                                         HookerConnectInitializer.Version.Size,
                                         (PVOID)NULL,
                                         0x0,
                                         TRUE,
                                         &Event,
                                         &IoStatusBlock );
    ASSERT( NULL != Irp );
    if( NULL == Irp ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    //
    // call the hooker
    //
    RC = IoCallDriver( HookerFileObject->DeviceObject, Irp );
    Irp = NULL;
    if( STATUS_PENDING == RC ){

        KeWaitForSingleObject( &Event,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL);

        RC = IoStatusBlock.Status;
    }
    ASSERT( NT_SUCCESS( RC ) );

__exit:

    if( NT_SUCCESS( RC ) ){

        ASSERT( HookerFileObject );
        ObReferenceObject( HookerFileObject->DeviceObject );
        Global.HookerDeviceObject = HookerFileObject->DeviceObject;

        //
        // all has gone smoothy, so reset the event, 
        // it will be set in a signal state by the 
        // the service provider( i.e. Hooker driver )
        //
        KeResetEvent( &Global.HookerDisconnectionEvent );
    }

    if( NULL != hDeviceHandle )
        ZwClose( hDeviceHandle );

    if( NULL != HookerFileObject )
        ObDereferenceObject( HookerFileObject);
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( HookerFileObject );

    if( NULL != Irp )
        IoFreeIrp( Irp );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( Irp );

    return RC;
}

//-------------------------------------------------

