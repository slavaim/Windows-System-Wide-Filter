/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
15.01.2007 ( January )
 Start
*/

/*
This file contains code for gathering information about devices
*/
#include "struct.h"
#include "proto.h"
#include <devguid.h>
#include <wdmguid.h>
#include "md5_hash.h"

#define OC_DEVICE_PROPERTY_MEM_TAG  'pDcO'
//#define OC_DEVPROPERTY_VALUE_BUFFER_TAG    'PDOc'

//-------------------------------------------------

//DEFINE_GUID( GUID_DL_UNKNOWN, 0x00000000L, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 );
EXTERN_C const GUID DECLSPEC_SELECTANY  GUID_OC_UNKNOWN
      = { 0x00000000L, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };

EXTERN_C const GUID DECLSPEC_SELECTANY  OC_GUID_FILE_SYSTEM
= { 0xe5f0b72a, 0x2a91, 0x4b27, { 0x82, 0x55, 0x7a, 0xaa, 0xe3, 0x2b, 0xe9, 0x5e } };

static OC_PNP_ENUMERATOR gEnumerator[] = {
    { en_OC_UNKNOWN_ENUM, L"DL_UNKNOWN_2005" },
    { en_ACPI,            L"ACPI" },
    { en_ACPI_HAL,        L"ACPI_HAL" },
    { en_BTH,             L"BTH" },
    { en_BTHENUM,         L"BTHENUM" },
    { en_DISPLAY,         L"DISPLAY" },
    { en_FDC,             L"FDC" },
    { en_HID,             L"HID" },
    { en_HTREE,           L"HTREE" },
    { en_IDE,             L"IDE" },
    { en_ISAPNP,          L"ISAPNP" },
    { en_LPTENUM,         L"LPTENUM" },
    { en_PCI,             L"PCI" },
    { en_PCIIDE,          L"PCIIDE" },
    { en_Root,            L"Root" },
    { en_SBP2,            L"SBP2" },
    { en_SCSI,            L"SCSI" },
    { en_STORAGE,         L"STORAGE" },
    { en_SW,              L"SW" },
    { en_USB,             L"USB" },
    { en_USBPRINT,        L"USBPRINT" },
    { en_USBSTOR,         L"USBSTOR" },
    { en_1394,            L"1394" },
    { en_V1394,           L"V1394" },
    { en_PCMCIA,          L"PCMCIA" },
    { en_WpdBusEnumRoot,  L"WpdBusEnumRoot" },
    { en_SWMUXBUS,        L"SWMUXBUS" }
};

//-------------------------------------------------

static OC_SETUP_CLASS_GUID gDeviceClass[] = {
    { en_OC_GUID_DEVCLASS_UNKNOWN,      &GUID_OC_UNKNOWN  },
    { en_GUID_DEVCLASS_1394,            &GUID_DEVCLASS_1394 },
    { en_GUID_DEVCLASS_1394DEBUG,       &GUID_DEVCLASS_1394DEBUG },
    { en_GUID_DEVCLASS_61883,           &GUID_DEVCLASS_61883 },
    { en_GUID_DEVCLASS_ADAPTER,         &GUID_DEVCLASS_ADAPTER },
    { en_GUID_DEVCLASS_APMSUPPORT,      &GUID_DEVCLASS_APMSUPPORT },
    { en_GUID_DEVCLASS_AVC,             &GUID_DEVCLASS_AVC },
    { en_GUID_DEVCLASS_BATTERY,         &GUID_DEVCLASS_BATTERY },
    { en_GUID_DEVCLASS_BIOMETRIC,       &GUID_DEVCLASS_BIOMETRIC },
    { en_GUID_DEVCLASS_BLUETOOTH,       &GUID_DEVCLASS_BLUETOOTH },
    { en_GUID_DEVCLASS_CDROM,           &GUID_DEVCLASS_CDROM },
    { en_GUID_DEVCLASS_COMPUTER,        &GUID_DEVCLASS_COMPUTER },
    { en_GUID_DEVCLASS_DECODER,         &GUID_DEVCLASS_DECODER },
    { en_GUID_DEVCLASS_DISKDRIVE,       &GUID_DEVCLASS_DISKDRIVE },
    { en_GUID_DEVCLASS_DISPLAY,         &GUID_DEVCLASS_DISPLAY },
    { en_GUID_DEVCLASS_DOT4,            &GUID_DEVCLASS_DOT4 },
    { en_GUID_DEVCLASS_DOT4PRINT,       &GUID_DEVCLASS_DOT4PRINT },
    { en_GUID_DEVCLASS_ENUM1394,        &GUID_DEVCLASS_ENUM1394 },
    { en_GUID_DEVCLASS_FDC,             &GUID_DEVCLASS_FDC },
    { en_GUID_DEVCLASS_FLOPPYDISK,      &GUID_DEVCLASS_FLOPPYDISK },
    { en_GUID_DEVCLASS_GPS,             &GUID_DEVCLASS_GPS },
    { en_GUID_DEVCLASS_HDC,             &GUID_DEVCLASS_HDC },
    { en_GUID_DEVCLASS_HIDCLASS,        &GUID_DEVCLASS_HIDCLASS },
    { en_GUID_DEVCLASS_IMAGE,           &GUID_DEVCLASS_IMAGE },
    { en_GUID_DEVCLASS_INFINIBAND,      &GUID_DEVCLASS_INFINIBAND },
    { en_GUID_DEVCLASS_INFRARED,        &GUID_DEVCLASS_INFRARED },
    { en_GUID_DEVCLASS_KEYBOARD,        &GUID_DEVCLASS_KEYBOARD },
    { en_GUID_DEVCLASS_LEGACYDRIVER,    &GUID_DEVCLASS_LEGACYDRIVER },
    { en_GUID_DEVCLASS_MEDIA,           &GUID_DEVCLASS_MEDIA },
    { en_GUID_DEVCLASS_MEDIUM_CHANGER,  &GUID_DEVCLASS_MEDIUM_CHANGER },
    { en_GUID_DEVCLASS_MODEM,           &GUID_DEVCLASS_MODEM },
    { en_GUID_DEVCLASS_MONITOR,         &GUID_DEVCLASS_MONITOR },
    { en_GUID_DEVCLASS_MOUSE,           &GUID_DEVCLASS_MOUSE },
    { en_GUID_DEVCLASS_MTD,             &GUID_DEVCLASS_MTD },
    { en_GUID_DEVCLASS_MULTIFUNCTION,   &GUID_DEVCLASS_MULTIFUNCTION },
    { en_GUID_DEVCLASS_MULTIPORTSERIAL, &GUID_DEVCLASS_MULTIPORTSERIAL },
    { en_GUID_DEVCLASS_NET,             &GUID_DEVCLASS_NET },
    { en_GUID_DEVCLASS_NETCLIENT,       &GUID_DEVCLASS_NETCLIENT },
    { en_GUID_DEVCLASS_NETSERVICE,      &GUID_DEVCLASS_NETSERVICE },
    { en_GUID_DEVCLASS_NETTRANS,        &GUID_DEVCLASS_NETTRANS },
    { en_GUID_DEVCLASS_NODRIVER,        &GUID_DEVCLASS_NODRIVER },
    { en_GUID_DEVCLASS_PCMCIA,          &GUID_DEVCLASS_PCMCIA },
    { en_GUID_DEVCLASS_PNPPRINTERS,     &GUID_DEVCLASS_PNPPRINTERS },
    { en_GUID_DEVCLASS_PORTS,           &GUID_DEVCLASS_PORTS },
    { en_GUID_DEVCLASS_PRINTER,         &GUID_DEVCLASS_PRINTER },
    { en_GUID_DEVCLASS_PRINTERUPGRADE,  &GUID_DEVCLASS_PRINTERUPGRADE },
    { en_GUID_DEVCLASS_PROCESSOR,       &GUID_DEVCLASS_PROCESSOR },
    { en_GUID_DEVCLASS_SBP2,            &GUID_DEVCLASS_SBP2 },
    { en_GUID_DEVCLASS_SCSIADAPTER,     &GUID_DEVCLASS_SCSIADAPTER },
    { en_GUID_DEVCLASS_SECURITYACCELERATOR, &GUID_DEVCLASS_SECURITYACCELERATOR },
    { en_GUID_DEVCLASS_SMARTCARDREADER, &GUID_DEVCLASS_SMARTCARDREADER },
    { en_GUID_DEVCLASS_SOUND,           &GUID_DEVCLASS_SOUND },
    { en_GUID_DEVCLASS_SYSTEM,          &GUID_DEVCLASS_SYSTEM },
    { en_GUID_DEVCLASS_TAPEDRIVE,       &GUID_DEVCLASS_TAPEDRIVE },
    { en_GUID_DEVCLASS_UNKNOWN,         &GUID_DEVCLASS_UNKNOWN },
    { en_GUID_DEVCLASS_USB,             &GUID_DEVCLASS_USB },
    { en_GUID_DEVCLASS_VOLUME,          &GUID_DEVCLASS_VOLUME },
    { en_GUID_DEVCLASS_VOLUMESNAPSHOT,  &GUID_DEVCLASS_VOLUMESNAPSHOT },
    { en_GUID_DEVCLASS_WCEUSBS,         &GUID_DEVCLASS_WCEUSBS },
    { en_GUID_FILE_SYSTEM,              &OC_GUID_FILE_SYSTEM },
    { en_GUID_DEVCLASS_WPD,             &GUID_DEVCLASS_WPD }
};

//-------------------------------------------------

static OC_BUS_TYPE_GUID gBusType[] = {
    { en_GUID_DL_BUS_TYPE_UNKNOWN, &GUID_OC_UNKNOWN }, 
    { en_GUID_BUS_TYPE_INTERNAL,   &GUID_BUS_TYPE_INTERNAL },
    { en_GUID_BUS_TYPE_PCMCIA,     &GUID_BUS_TYPE_PCMCIA },
    { en_GUID_BUS_TYPE_PCI,        &GUID_BUS_TYPE_PCI },
    { en_GUID_BUS_TYPE_ISAPNP,     &GUID_BUS_TYPE_ISAPNP },
    { en_GUID_BUS_TYPE_EISA,       &GUID_BUS_TYPE_EISA },
    { en_GUID_BUS_TYPE_MCA,        &GUID_BUS_TYPE_MCA },
    { en_GUID_BUS_TYPE_LPTENUM,    &GUID_BUS_TYPE_LPTENUM },
    { en_GUID_BUS_TYPE_USBPRINT,   &GUID_BUS_TYPE_USBPRINT },
    { en_GUID_BUS_TYPE_DOT4PRT,    &GUID_BUS_TYPE_DOT4PRT },
    { en_GUID_BUS_TYPE_SERENUM,    &GUID_BUS_TYPE_SERENUM },
    { en_GUID_BUS_TYPE_USB,        &GUID_BUS_TYPE_USB },
    { en_GUID_BUS_TYPE_1394,       &GUID_BUS_TYPE_1394 },
    { en_GUID_BUS_TYPE_HID,        &GUID_BUS_TYPE_HID },
    { en_GUID_BUS_TYPE_AVC,        &GUID_BUS_TYPE_AVC },
    { en_GUID_BUS_TYPE_IRDA,       &GUID_BUS_TYPE_IRDA },
    { en_GUID_BUS_TYPE_SD,         &GUID_BUS_TYPE_SD }
};

//-------------------------------------------------
/*
NTSTATUS
OcCrGetDevicePropertyInBuffer (
    IN PDEVICE_OBJECT    PhysicalDeviceObject,
    IN DEVICE_REGISTRY_PROPERTY    DeviceProperty
    OUT PWCHAR*    PtrPtrPropertyValueBuffer,
    OUT PULONG     PtrResultLength
    )
    
    The caller must free the returned buffer by calling 
    OcCrFreeDevicePropertyBuffer( *PtrPtrPropertyValueString )
    The buffer is allocated from the paged pool.
    
{
    NTSTATUS  RC;
    ULONG     ResultLength;
    PVOID     PropertyValueBuffer;
    ULONG     i;

    //
    // see IoGetDeviceProperty requirements in the DDK
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );
    ASSERT( Global.SystemFunctions.IoGetDeviceProperty );

    RC = Global.SystemFunctions.IoGetDeviceProperty( PhysicalDeviceObject, 
                                                     DeviceProperty,
                                                     0x0,
                                                     NULL,
                                                     &ResultLength );
    if( STATUS_BUFFER_TOO_SMALL != RC ){

        ASSERT( !NT_SUCCESS( RC ) );

        if( NT_SUCCESS( RC ) )
            RC = STATUS_INVALID_DEVICE_REQUEST;

        return RC;
    }

    ASSERT( OcCrIsPdo( PhysicalDeviceObject ) );

    PropertyValueBuffer = ExAllocatePoolWithTag( PagedPool,
                                                 ResultLength + sizeof( WCHAR ),
                                                 OC_DEVPROPERTY_VALUE_BUFFER_TAG );
    if( NULL == PropertyValueBuffer )
        return STATUS_INSUFFICIENT_RESOURCES;

    RC = Global.SystemFunctions.IoGetDeviceProperty( PhysicalDeviceObject, 
                                                     DeviceProperty,
                                                     ResultLength,
                                                     PropertyValueBuffer,
                                                     &ResultLength );

    if( !NT_SUCCESS( RC ) ){
        ExFreePoolWithTag( PropertyValueBuffer, OC_DEVPROPERTY_VALUE_BUFFER_TAG );
    } else {
        *PtrPtrPropertyValueBuffer = PropertyValueBuffer;
        *PtrResultLength = ResultLength;
    }

    return RC;
}

//-------------------------------------------------

VOID
OcCrFreeDevicePropertyBuffer(
    PVOID    PropertyValueBuffer
    )
{
    ExFreePoolWithTag( PropertyValueBuffer, OC_DEVPROPERTY_VALUE_BUFFER_TAG );
}
*/
//-------------------------------------------------

NTSTATUS
OcCrGetDeviceEnumeratorIndex(
    IN PDEVICE_OBJECT        PhysicalDeviceObject,
    OUT OC_EN_ENUMERATOR*    PtrDeviceEnumeratorIndex
    )
{
    NTSTATUS RC;
    ULONG ResultLength;
    WCHAR EnumeratorName[ MAXIMUM_ENUMERATOR_NAME_LENGTH ];
    ULONG i;

    //
    // see IoGetDeviceProperty requirements in the DDK
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );
    ASSERT( Global.SystemFunctions.IoGetDeviceProperty );

    RC = Global.SystemFunctions.IoGetDeviceProperty( PhysicalDeviceObject, 
                                                     DevicePropertyEnumeratorName,
                                                     sizeof( EnumeratorName ),
                                                     EnumeratorName,
                                                     &ResultLength );
    if( !NT_SUCCESS( RC ) )
        return RC;

    ASSERT( OcCrIsPdo( PhysicalDeviceObject ) );

    for( i = 0x1; i < OC_STATIC_ARRAY_SIZE( gEnumerator ); ++i ){

        if( 0x0 == _wcsicmp( EnumeratorName, gEnumerator[ i ].EnumeratorName ) ){

            ( *PtrDeviceEnumeratorIndex ) = gEnumerator[ i ].EnumeratorIndex;
            break;
        }//if
    }//for

    if( i == OC_STATIC_ARRAY_SIZE( gEnumerator ) )
        ( *PtrDeviceEnumeratorIndex ) = en_OC_UNKNOWN_ENUM;

    ASSERT( en_OC_UNKNOWN_ENUM != *PtrDeviceEnumeratorIndex );

    return RC;
}

//-------------------------------------------------

VOID
OcCrProcessPnPRequestForHookedDriver(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    )
{
    PIO_STACK_LOCATION    PtrIrpStack;

    PtrIrpStack = IoGetCurrentIrpStackLocation( Irp );
    switch( PtrIrpStack->MinorFunction ){
    ASSERT( IRP_MJ_PNP == PtrIrpStack->MajorFunction );
    case IRP_MN_REMOVE_DEVICE:
        //
        // remove the device from the hash, this must be a PDO
        //
        OcHsRemoveContextByKeyValue( Global.PtrDeviceHashObject,
                                     (ULONG_PTR)DeviceObject,
                                     OcCrCleanupDeviceAfterRemovingFromHash );
        break;
    }//switch
}

//------------------------------------------------

__forceinline
BOOLEAN
OcCrAreGuidsEqual(
    __in GUID* CONST          Value,
    __in CONST GUID* CONST    Comparand
    )
{
    if( Value == Comparand )
        return TRUE;
    else
        return ( sizeof(GUID) == RtlCompareMemory( Value, Comparand, sizeof(GUID) ) );
}

//------------------------------------------------

OC_EN_SETUP_CLASS_GUID
OcCrGetDeviceSetupClassGuidIndex(
    __in PUNICODE_STRING    SetupGuidString
    )
{
    OC_EN_SETUP_CLASS_GUID    SetupGuidEnum = en_OC_GUID_DEVCLASS_UNKNOWN;
    GUID        Guid;
    ULONG       i;
    NTSTATUS    RC;

    RC = RtlGUIDFromString( SetupGuidString, &Guid );
    if( !NT_SUCCESS( RC ) )
        return en_OC_GUID_DEVCLASS_UNKNOWN;

    for( i = 1; i< sizeof( gDeviceClass )/sizeof( gDeviceClass[ 0 ] ) ; ++i ){

        if( OcCrAreGuidsEqual( &Guid, gDeviceClass[ i ].PtrGuid ) ){

            SetupGuidEnum = gDeviceClass[ i ].DevClassIndex;
            break;
        }
    }

    ASSERT( en_OC_GUID_DEVCLASS_UNKNOWN != SetupGuidEnum );

    return SetupGuidEnum;
}

//--------------------------------------------------------------

NTSTATUS
OcCrGetDeviceProperty(
    IN PDEVICE_OBJECT    PhysicalDeviceObject,
    IN DEVICE_REGISTRY_PROPERTY    DeviceProperty,
    OUT PVOID*    PtrBuffer,
    OUT ULONG*    PtrValidDataLength OPTIONAL,
    OUT ULONG*    PtrBufferLength OPTIONAL
    )
    /*
    The returned buffer's size may be greater than the 
    size of the valid data in it, i.e.
    {{ valid data } garbage }.
    The caller can write in the buffer until *PtrBufferLength.
    The caller must free the returned buffer by calling
    OcCrFreeDevicePropertyBuffer( *PtrBuffer )
    */
{
    NTSTATUS   RC;
    ULONG      ResultLength;
    ULONG      BufferLength;
    PVOID      PoolAllocatedBuffer = NULL;

    //
    // see IoGetDeviceProperty requirements in DDK
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );
    ASSERT( OcCrIsPdo( PhysicalDeviceObject ) );
    ASSERT( Global.SystemFunctions.IoGetDeviceProperty );

    RC = Global.SystemFunctions.IoGetDeviceProperty( PhysicalDeviceObject, 
                                                     DeviceProperty,
                                                     0x0,
                                                     NULL,
                                                     &ResultLength );
    if( STATUS_BUFFER_TOO_SMALL != RC ){

        ASSERT( !NT_SUCCESS( RC ) );

        if( NT_SUCCESS( RC ) )
            RC = STATUS_INVALID_DEVICE_REQUEST;

        return RC;
    }

    BufferLength = ResultLength;
    PoolAllocatedBuffer = ExAllocatePoolWithTag( NonPagedPool,
                                                 BufferLength,
                                                 OC_DEVICE_PROPERTY_MEM_TAG );
    if( NULL == PoolAllocatedBuffer ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        return RC;
    }

    RC = Global.SystemFunctions.IoGetDeviceProperty( PhysicalDeviceObject, 
                                                     DeviceProperty,
                                                     BufferLength,
                                                     PoolAllocatedBuffer,
                                                     &ResultLength );

    if( !NT_SUCCESS( RC ) ){

        if( NULL != PoolAllocatedBuffer )
            ExFreePoolWithTag( PoolAllocatedBuffer, OC_DEVICE_PROPERTY_MEM_TAG );

    } else {

        ASSERT( BufferLength >= ResultLength );

        *PtrBuffer = PoolAllocatedBuffer;

        if( NULL != PtrValidDataLength )
            *PtrValidDataLength = ResultLength;

        if( NULL != PtrBufferLength )
            *PtrBufferLength = BufferLength;

    }

    return RC;

}

VOID
OcCrFreeDevicePropertyBuffer(
    IN PVOID    Buffer
    )
{
    ExFreePoolWithTag( Buffer, OC_DEVICE_PROPERTY_MEM_TAG );
}

//-------------------------------------------------

VOID
OcDereferenceDevicesAndFreeDeviceRelationsMemory(
    IN PDEVICE_RELATIONS DeviceRelations
    )
{
    ULONG    i;

    ASSERT( DeviceRelations );

    if( NULL == DeviceRelations )
        return;

    for( i=0; i<DeviceRelations->Count; ++i){

        ASSERT( NULL != DeviceRelations->Objects[ i ] );

        //
        // self-defence
        //
        if( NULL == DeviceRelations->Objects[ i ] )
            continue;

        ObDereferenceObject( DeviceRelations->Objects[ i ] );
    }

    ExFreePool( DeviceRelations );
}

//-------------------------------------------------

NTSTATUS
OcQueryDeviceRelations(
    IN PDEVICE_OBJECT           DeviceObject,
    IN DEVICE_RELATION_TYPE     RelationsType,
    OUT PDEVICE_RELATIONS*      ReferencedDeviceRelations OPTIONAL
    )
/*++

Routine Description:

    This function builds and sends a pnp irp to get the relations for a device.

Arguments:

    DeviceObject - This is a device in a device stack to which 
                   an irp will be sent.

Return Value:

   NT status code and ReferencedDeviceRelations.
   Function returns refernced device objects in *deviceRelations structure allocated 
   from the pool( paged or nonpaged ) by some of the underlying drivers, also function 
   may return NULL in *deviceRelations and success as a returned code.
   If the ReferencedDeviceRelations is NULL the relations is freed inside this function.

    The caller must dereference all the PDOs and free the *deviceRelations when 
    they are not longer required.

--*/
{

    KEVENT                  Event;
    NTSTATUS                RC;
    PIRP                    irp;
    IO_STATUS_BLOCK         ioStatusBlock;
    PIO_STACK_LOCATION      irpStack;

    //
    // we are going to wait for an Irp completion
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // Only the PnP subsystem can query the Bus Relations
    //
    ASSERT( BusRelations != RelationsType );

    ASSERT( IO_TYPE_DEVICE == DeviceObject->Type );
    ASSERT( IO_TYPE_DRIVER == DeviceObject->DriverObject->Type );

    if( BusRelations == RelationsType )
        return STATUS_INVALID_DEVICE_REQUEST;

    KeInitializeEvent( &Event, NotificationEvent, FALSE );

    irp = IoBuildSynchronousFsdRequest( IRP_MJ_PNP,
                                        DeviceObject,
                                        NULL,
                                        0,
                                        NULL,
                                        &Event,
                                        &ioStatusBlock );

    if( irp == NULL ) {

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    irpStack = IoGetNextIrpStackLocation( irp );
    irpStack->MinorFunction = IRP_MN_QUERY_DEVICE_RELATIONS;
    irpStack->Parameters.QueryDeviceRelations.Type = RelationsType;

    //
    // Initialize the status to error in case the bus driver decides not to
    // set it correctly. Driver Verifier bug checks drivers which sent PnP IRP
    // without STATUS_NOT_SUPPORTED status set.
    //

    irp->IoStatus.Status = STATUS_NOT_SUPPORTED ;

    RC = IoCallDriver( DeviceObject, irp );

    if( STATUS_PENDING == RC ) {

        KeWaitForSingleObject( &Event,
                               Executive, 
                               KernelMode, 
                               FALSE, 
                               NULL );

        RC = ioStatusBlock.Status;
    }

    if( NT_SUCCESS( RC ) ){

        if( NULL != ReferencedDeviceRelations )
            *ReferencedDeviceRelations = (PDEVICE_RELATIONS)ioStatusBlock.Information;
        else if( NULL != (PVOID)ioStatusBlock.Information )
             OcDereferenceDevicesAndFreeDeviceRelationsMemory( (PDEVICE_RELATIONS)ioStatusBlock.Information );

    }//if( NT_SUCCESS( status ) )

__exit:
    return RC;

};

//-------------------------------------------------

NTSTATUS
OcCrGetDeviceRegistryKeyString(
    __in PDEVICE_OBJECT    InitializedPdo,
    __out POBJECT_NAME_INFORMATION*    PtrPnPKeyNameInfo
    )
    /*
    the caller must free the returned object by calling
    */
{
    NTSTATUS    RC;
    HANDLE      PnPKeyHandle = NULL;
    PVOID       deviceKeyObject = NULL;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcCrIsPdo( InitializedPdo ) );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );

    RC = OcCrOpenDeviceKey( InitializedPdo,
                            &PnPKeyHandle );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

    //
    // Reference the key object
    //
    RC = ObReferenceObjectByHandle ( PnPKeyHandle,
                                     KEY_QUERY_VALUE,
                                     NULL,// can be NULL for KernelMode access mode
                                     KernelMode, //to avoid checking security set mode to Kernel
                                     (PVOID *)&deviceKeyObject,
                                     NULL );

    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

    //
    // get a key's name, such as "\REGISTRY\MACHINE\SYSTEM\ControlSet004\Enum\USB\Vid_0ea0&Pid_2168\611042017A1100EA\Device Parameters"
    //
    RC = OcCrQueryObjectName( deviceKeyObject,
                              PtrPnPKeyNameInfo );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

__exit:

    if( NULL != PnPKeyHandle )
        ZwClose( PnPKeyHandle );

    if( NULL != deviceKeyObject )
        ObDereferenceObject( deviceKeyObject );

    return RC;
}

VOID
OcCrFreeDeviceRegistryKeyString(
    __in POBJECT_NAME_INFORMATION    PnPKeyNameInfo
    )
{
    OcCrFreeNameInformation( PnPKeyNameInfo );
}

//-------------------------------------------------

NTSTATUS
OcCrGetUsbDeviceDescriptor(
    __in PDEVICE_OBJECT    InitializedUsbBusPdo,
    __inout PUSB_DEVICE_DESCRIPTOR    UsbDevDesc
    )
{
#define STATE_PARSE_NOTHING 0x0
#define STATE_PARSE_VID 0x1
#define STATE_PARSE_PID 0x2
#define STATE_READ_VID 0x3
#define STATE_READ_PID 0x4

#define MAX_PID_DIGITS 8

    NTSTATUS RC = STATUS_SUCCESS;
    PWCHAR   buffer = NULL;
    ULONG    bufferLength = 0;
    ULONG    i;
    LONG     k;
    ULONG    state = STATE_PARSE_NOTHING;
    UCHAR    digit;
    BOOLEAN  StateParsePidWasPassed = FALSE;
    BOOLEAN  StateParseVidWasPassed = FALSE;

    static WCHAR U_VID[]= L"VID";
    static WCHAR L_VID[]= L"vid";
    static WCHAR U_PID[]= L"PID";
    static WCHAR L_PID[]= L"pid";

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );

    RC = OcCrGetDeviceProperty( InitializedUsbBusPdo,
                                DevicePropertyHardwareID,
                                &buffer,
                                &bufferLength,
                                NULL );

    ASSERT( NT_SUCCESS( RC ) && NULL != buffer );

    if( 0x0 == bufferLength || !NT_SUCCESS( RC ) ){

        ASSERT( NULL == buffer );

        //
        // this usually happens on the system shutdown
        //
        if( NT_SUCCESS( RC ) )
            return STATUS_SYSTEM_SHUTDOWN;
        else
            return RC;
    }

    memset( UsbDevDesc, 0, sizeof( *UsbDevDesc ) );

    for( i=0, k=0; i<bufferLength/sizeof(buffer[0]); ++i,++k ){

        switch(state){

        case STATE_PARSE_NOTHING:
            {
//                dprintf("ocore:GetUsbDeviceDescriptor state STATE_PARSE_NOTHING\n ");

                if( buffer[i]==U_VID[0] || buffer[i]==L_VID[0] )
                {
                    StateParseVidWasPassed = TRUE;
                    state = STATE_PARSE_VID;
                    k=0;
                } 
                else if( buffer[i]==U_PID[0] || buffer[i]==L_PID[0] )
                {
                    StateParsePidWasPassed = TRUE;
                    state = STATE_PARSE_PID;
                    k=0;
                } 
            }
            break;
        case STATE_PARSE_VID:
            {
//                dprintf("ocore:GetUsbDeviceDescriptor state STATE_PARSE_VID k=%d\n ",k);

                if( L'\0'!=U_VID[k] && ( buffer[i]!=U_VID[k] && buffer[i]!=L_VID[k] ) )
                {
                    state = STATE_PARSE_NOTHING;
                    k=-1;
                }
                else if( L'\0'==U_VID[k] )
                {
                    state = STATE_READ_VID;
                    k=-1;
                }
            }
            break;
        case STATE_PARSE_PID:
            {
//                dprintf("ocore:GetUsbDeviceDescriptor state STATE_PARSE_PID k=%d\n ",k);

                if( L'\0'!=U_PID[k] && ( buffer[i]!=U_PID[k] && buffer[i]!=L_PID[k] ) )
                {
                    state = STATE_PARSE_NOTHING;
                    k=-1;
                }
                else if( L'\0'==U_PID[k] )
                {
                    state = STATE_READ_PID;
                    k=-1;
                }
            }
            break;
        case STATE_READ_VID:
        case STATE_READ_PID:
            {
                //dprintf("ocore:GetUsbDeviceDescriptor state 0x%X\n",state);
                if( 0==k )
                {
                    if(STATE_READ_VID==state)
                        UsbDevDesc->idVendor = 0;
                    else
                        UsbDevDesc->idProduct = 0;
                }

                if( buffer[i]==U_VID[0] || buffer[i]==L_VID[0] )
                {
                    state = STATE_PARSE_VID;
                    k=-1;
                    break;
                } 
                else if( buffer[i]==U_PID[0] || buffer[i]==L_PID[0] )
                {
                    state = STATE_PARSE_PID;
                    k=-1;
                    break;
                } 
                
                if( !( buffer[i]>=L'0' && buffer[i]<=L'9' ) &&
                    !( buffer[i]>=L'A' && buffer[i]<=L'F' ) &&
                    !( buffer[i]>=L'a' && buffer[i]<=L'f' ) )
                {
                    if( 0!=k )//we have some digits
                        state = STATE_PARSE_NOTHING;
                    else//k==0
                        k=-1;
                    
                    break;
                }
                else if( k==MAX_PID_DIGITS )
                {
                    state = STATE_PARSE_NOTHING;
                    k=-1;
                    break;
                }
                
                if( buffer[i]>=L'0' && buffer[i]<=L'9' )
                    digit = buffer[i]-L'0';
                else if( buffer[i]>=L'A' && buffer[i]<=L'F' ) 
                    digit = 0xA+buffer[i]-L'A';
                else if( buffer[i]>=L'a' && buffer[i]<=L'f' ) 
                    digit = 0xA+buffer[i]-L'a';
                
                if( STATE_READ_VID == state )
                    UsbDevDesc->idVendor = UsbDevDesc->idVendor*0x10+digit;
                else
                    UsbDevDesc->idProduct = UsbDevDesc->idProduct*0x10+digit;
            }
            break;
        }
    }

    if( NULL != buffer )
        OcCrFreeDevicePropertyBuffer( buffer );

    /*
    if( FALSE == StateParsePidWasPassed || FALSE == StateParseVidWasPassed )
        UsbDevDesc->NotUsbDeviceString = TRUE;
    else
        UsbDevDesc->NotUsbDeviceString = FALSE;
    */

    return RC;
}

//---------------------------------------------------------------

NTSTATUS 
OcCrGetUsbUniqueDeviceId(
    __in PDEVICE_OBJECT    InitializedUsbBusPdo,
    __in PUSB_DEVICE_DESCRIPTOR    UsbDevDesc,
    __inout MD5_CTX*    mdContext
    )
{

    NTSTATUS       RC = STATUS_SUCCESS;
    ULONGLONG      deviceIdHash = 0x0i64; 
    HANDLE         deviceKeyHandle = NULL;
    PFILE_OBJECT   deviceKeyObject = NULL;
    ULONG          i,m;
    ULONG          deviceIdLength = 0x0, startIndexForDeviceId = 0x0 ;
    POBJECT_NAME_INFORMATION   NameInfoBuffer = NULL;
    UNICODE_STRING             InstanceId;
    UNICODE_STRING             UpcasedInstanceId;
    PID_VID                    PidVid;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcCrIsPdo( InitializedUsbBusPdo ) );
    ASSERT( NULL != Global.SystemFunctions.IoOpenDeviceRegistryKey );

    PidVid.Pid = UsbDevDesc->idProduct;
    PidVid.Vid = UsbDevDesc->idVendor;

    //
    // open the device's PnP key
    //
    RC = Global.SystemFunctions.IoOpenDeviceRegistryKey( InitializedUsbBusPdo,
                                                         PLUGPLAY_REGKEY_DEVICE,
                                                         KEY_QUERY_VALUE,
                                                         &deviceKeyHandle );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

    //
    // Reference the key object
    //
    RC = ObReferenceObjectByHandle ( deviceKeyHandle,
                                     KEY_QUERY_VALUE,
                                     NULL,// can be NULL for KernelMode access mode
                                     KernelMode, //to avoid checking security set mode to Kernel
                                     (PVOID *)&deviceKeyObject,
                                     NULL );

    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

    //
    // get a key's name, such as "\REGISTRY\MACHINE\SYSTEM\ControlSet004\Enum\USB\Vid_0ea0&Pid_2168\611042017A1100EA\Device Parameters"
    //
    RC = OcCrQueryObjectName( deviceKeyObject,
                              &NameInfoBuffer );

    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

    if( NameInfoBuffer->Name.Length < 0x3 ){

        RC = STATUS_INVALID_PARAMETER;
        goto __exit;
    }

    RtlZeroMemory( mdContext, sizeof( *mdContext ) );

    //
    // the name has the form
    // \REGISTRY\MACHINE\SYSTEM\ControlSetXXX\Enum\Enumerator\DeviceID\InstanceID\.....
    // our gain is to get an InstanceID, this is a Unique Device Id
    //
    for( i = 0x0, m = 0x0; i<( NameInfoBuffer->Name.Length/sizeof( WCHAR )-0x1 ) && m<0x9; ++i ){
        
        if( L'\\' == NameInfoBuffer->Name.Buffer[ i ] && 
            L'\\' != NameInfoBuffer->Name.Buffer[ i+0x1 ] ){

            ++m;

            if( 0x8 == m && 0x0 == startIndexForDeviceId ){

                startIndexForDeviceId = i + 1;

            }
        } else if( 0x8 == m ){

            ++deviceIdLength;
        }
    }

    ASSERT( 0x9 == m );

    if( m != 0x9 ){

        RC = STATUS_INVALID_PARAMETER;
        goto __exit;
    }

    InstanceId.Buffer = &NameInfoBuffer->Name.Buffer[ startIndexForDeviceId ];
    InstanceId.Length = InstanceId.MaximumLength = (USHORT)deviceIdLength * sizeof( WCHAR );

    for( i = 0x0; i< InstanceId.Length/sizeof( WCHAR ); ++i ){

        if( L'&' == InstanceId.Buffer[ i ] ){
            //
            // the device does not have the unique ID, this ID has been generated
            // by the usb port driver
            //
            goto __exit;
        }
    }

    RC = RtlUpcaseUnicodeString( &UpcasedInstanceId, &InstanceId, TRUE );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

    MD5Init( mdContext );

    MD5Update( mdContext, 
               (unsigned char*)UpcasedInstanceId.Buffer, 
               UpcasedInstanceId.Length );

    MD5Update( mdContext, 
               (unsigned char*)&PidVid, 
               sizeof( PidVid ) );

    MD5Final( mdContext );

    RtlFreeUnicodeString( &UpcasedInstanceId );

__exit:

    if( NULL == deviceKeyObject )
        ObDereferenceObject( deviceKeyObject );

    if( NULL != deviceKeyHandle )
        ZwClose( deviceKeyHandle );

    if( NULL != NameInfoBuffer )
        OcCrFreeNameInformation( NameInfoBuffer );

    return RC;
}

//---------------------------------------------------------------------

NTSTATUS
OcCrGetCompatibleIdsStringForDevice(
    __in PDEVICE_OBJECT    InitializedUsbBusPdo,
    __out PKEY_VALUE_PARTIAL_INFORMATION*    CompatibleIds
    )
    /*
    the caller must free the returned buffer 
    by calling OcCrFreeCompatibleIdsStringForDevice
    */
{
    NTSTATUS          RC = STATUS_SUCCESS;
    HANDLE            deviceKeyHandle = NULL;
    UNICODE_STRING    ValueName;
    PVOID             Buffer = NULL;
    ULONG             BufferSize;
    PKEY_VALUE_PARTIAL_INFORMATION    pValuePartialInfo;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcCrIsPdo( InitializedUsbBusPdo ) );

    //
    // it is possible to use IoGetDeviceProperty with the 
    // DevicePropertyCompatibleIDs
    // instead of the direct reading the registry
    //

    RC = OcCrOpenDeviceKey( InitializedUsbBusPdo, &deviceKeyHandle );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

    ASSERT( deviceKeyHandle );

    //
    // Now we have a key handle for a device subkey,
    // namely the handle for the key such as the following -  
    // "\REGISTRY\MACHINE\SYSTEM\ControlSet004\Enum\USB\Vid_0ea0&Pid_2168\611042017A1100EA"
    //

    //
    // Get the compatible ID
    //

    RtlInitUnicodeString( &ValueName, L"CompatibleIDs" );

    RC = OcCrGetValueFromKey( deviceKeyHandle,
                              &ValueName,
                              KeyValuePartialInformation,
                              &Buffer,
                              &BufferSize );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    if( ((PKEY_VALUE_PARTIAL_INFORMATION)Buffer)->Type != REG_MULTI_SZ || 
        ((PKEY_VALUE_PARTIAL_INFORMATION)Buffer)->DataLength <= sizeof( WCHAR ) ){

        RC = STATUS_OBJECT_PATH_INVALID;
        goto __exit;
    }

    //
    // copy in the non paged pool
    //
    pValuePartialInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag( 
                                        NonPagedPool, BufferSize, 'VKcO' );
    if( NULL == pValuePartialInfo ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    RtlCopyMemory( pValuePartialInfo, Buffer, BufferSize );

    *CompatibleIds = pValuePartialInfo;

__exit:

    if( NULL != Buffer )
        OcCrFreeValueFromKey( Buffer );

    if( NULL != deviceKeyHandle )
        ZwClose( deviceKeyHandle );

    return RC;
}

VOID
OcCrFreeCompatibleIdsStringForDevice(
    __in PKEY_VALUE_PARTIAL_INFORMATION    CompatibleIds
    )
{
    ExFreePoolWithTag( CompatibleIds, 'VKcO' );
}

//---------------------------------------------------------------------

ULONG
OcCrGetUsbClassFromCompatibleIds(
    __in PKEY_VALUE_PARTIAL_INFORMATION    CompatibleIds
    )
{
    ULONG    i;
    ULONG    UsbClass = 0x0;
    ULONG    RemainingNameLength;
    PWCHAR   RemainingString;
    USHORT   CompatibleIdsStringLength;
    UNICODE_STRING    Prefix;
    UNICODE_STRING    CompatibleIdsString;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // retrieve the class ID from the first descriptor
    //
    RtlInitUnicodeString( &Prefix, L"USB\\Class_" );
    RtlInitUnicodeString( &CompatibleIdsString, (PWCHAR)CompatibleIds->Data );

    if( CompatibleIdsString.Length <= Prefix.Length )
        return UsbClass;

    //
    // check the prefix
    //
    CompatibleIdsStringLength = CompatibleIdsString.Length;
    CompatibleIdsString.Length = Prefix.Length;
    if( 0x0 != RtlCompareUnicodeString( &Prefix, &CompatibleIdsString, TRUE ) )
        return UsbClass;

    //
    // get the remaining string( i.e. w/o prefix )
    //
    CompatibleIdsString.Length = CompatibleIdsStringLength;
    RemainingNameLength = ( CompatibleIdsString.Length - Prefix.Length )/sizeof( WCHAR );
    RemainingString = &CompatibleIdsString.Buffer[ Prefix.Length/sizeof( WCHAR ) ];

#define WcharToNumber( WCHAR_LETTER )  ( (WCHAR_LETTER) - L'0' )

    for( i = 0x0; i<RemainingNameLength ;++i ){

        USHORT    Number = WcharToNumber( RemainingString[ i ] );

        if( !( 0x0<=Number && Number<=0x9 ) )
            break;

        UsbClass = UsbClass*0x10 + WcharToNumber( RemainingString[ i ] );
    }

    return    UsbClass;
#undef WcharToNumber
}

//---------------------------------------------------------------

NTSTATUS
OcCrInitializeUsbDeviceDescriptor(
    __in PDEVICE_OBJECT    InitializedUsbPdo,
    __inout POC_USB_DEVICE_DESCRIPTOR    UsbDescriptor
    )
    /*
    the caller must allocate UsbDescriptor and free it when it is
    no longer needed
    */
{
    NTSTATUS    RC;
    MD5_CTX     md5Context;
    ULONG       UsbClassId;
    PKEY_VALUE_PARTIAL_INFORMATION    CompatibleIdsString = NULL;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcCrIsPdo( InitializedUsbPdo ) );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );

    //
    // retrieve the standard USB descriptor
    //
    RC = OcCrGetUsbDeviceDescriptor( InitializedUsbPdo,
                                     &UsbDescriptor->StandardDescriptor );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        RC = STATUS_INVALID_DEVICE_REQUEST;
        goto __exit;
    }

    //
    // initialize the device ID which is used as a key for
    // setting of the white list properties
    //
    RC = OcCrGetUsbUniqueDeviceId( InitializedUsbPdo,
                                   &UsbDescriptor->StandardDescriptor,
                                   &md5Context );
    ASSERT( NT_SUCCESS( RC ) );
    if( NT_SUCCESS( RC ) ){

        ASSERT( sizeof( UsbDescriptor->id ) ==  sizeof( md5Context.digest ) );
        RtlCopyMemory( UsbDescriptor->id, md5Context.digest, sizeof( md5Context.digest ) );

    } else {

        goto __exit;
    }

    //
    // get a Usb CompatibleIds and Class ID. it is legitimate to 
    // return an error, for example, root usb hubs don't have the
    // CompatibleID string in the registry
    //
    RC = OcCrGetCompatibleIdsStringForDevice( InitializedUsbPdo,
                                              &CompatibleIdsString );
    if( NT_SUCCESS( RC ) ){

        //
        // get the USB class ID
        //
        UsbClassId = OcCrGetUsbClassFromCompatibleIds( CompatibleIdsString );

        //
        // save the found values in the USB property descriptor
        //
        UsbDescriptor->CompatibleIds = CompatibleIdsString;
        UsbDescriptor->UsbClassId = UsbClassId;

        //
        // the string will be freed when the object is being deleted
        //
        CompatibleIdsString = NULL;

    } else {

        //
        // continue the execution
        //
        RC = STATUS_SUCCESS;
        UsbDescriptor->CompatibleIds = NULL;
        UsbDescriptor->UsbClassId = 0x0;
    }

    //
    // there is a good place to propagate the white list settings
    //
    //DldSetDeviceWhiteListState( PtrDeviceInfo );

__exit:

    if( !NT_SUCCESS( RC ) ){

        if( NULL != CompatibleIdsString )
            OcCrFreeCompatibleIdsStringForDevice( CompatibleIdsString );
    }

    return RC;
}

//-------------------------------------------------

