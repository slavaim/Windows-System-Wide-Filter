/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
05.12.2006 ( December )
 Start
*/
#if !defined(_OC_DEVDATABASE_H_)
#define _OC_DEVDATABASE_H_

#include <ntddk.h>
#include <ocobject.h>
#include <ocrwspinlock.h>
#include <usb100.h>
#include "remove_lock.h"

//--------------------------------------------------------------

struct _OC_DEVICE_OBJECT;

typedef enum _OC_EN_ENUMERATOR{
    en_OC_UNKNOWN_ENUM,// unknown enumerator type
    en_OC_GET_NEXT_PARENT,//special enumerator used in top-down traversing of the PnP tree to return a nearest predecessor
    en_ACPI,
    en_ACPI_HAL,
    en_BTH,
    en_BTHENUM,
    en_DISPLAY,
    en_FDC,
    en_HID,
    en_HTREE,
    en_IDE,
    en_ISAPNP,
    en_LPTENUM,
    en_PCI,
    en_PCIIDE,
    en_Root,
    en_SBP2,
    en_SCSI,
    en_STORAGE,
    en_SW,
    en_USB,
    en_USBPRINT,
    en_USBSTOR,
    en_1394,
    en_V1394,
    en_PCMCIA,
    en_WpdBusEnumRoot, //Windows Portable Devices
    en_SWMUXBUS, //Sierra Wireles laptop internal adapter's bus
    en_LastEnumerator // the last enumerator
} OC_EN_ENUMERATOR;

#define MAXIMUM_ENUMERATOR_NAME_LENGTH 0x10

typedef struct _OC_PNP_ENUMERATOR{
    OC_EN_ENUMERATOR  EnumeratorIndex;
    const WCHAR*      EnumeratorName;
} OC_PNP_ENUMERATOR, *POC_PNP_ENUMERATOR;

//--------------------------------------------------------------

//
// Device class GUIDs enum.
// For reference see devguid.h.
//
typedef enum _OC_EN_SETUP_CLASS_GUID{
    en_OC_GUID_DEVCLASS_UNKNOWN = 0x0,
    en_GUID_DEVCLASS_1394,
    en_GUID_DEVCLASS_1394DEBUG,
    en_GUID_DEVCLASS_61883,
    en_GUID_DEVCLASS_ADAPTER,
    en_GUID_DEVCLASS_APMSUPPORT,
    en_GUID_DEVCLASS_AVC,
    en_GUID_DEVCLASS_BATTERY,
    en_GUID_DEVCLASS_BIOMETRIC,
    en_GUID_DEVCLASS_BLUETOOTH,
    en_GUID_DEVCLASS_CDROM,
    en_GUID_DEVCLASS_COMPUTER,
    en_GUID_DEVCLASS_DECODER,
    en_GUID_DEVCLASS_DISKDRIVE,
    en_GUID_DEVCLASS_DISPLAY,
    en_GUID_DEVCLASS_DOT4,
    en_GUID_DEVCLASS_DOT4PRINT,
    en_GUID_DEVCLASS_ENUM1394,
    en_GUID_DEVCLASS_FDC,
    en_GUID_DEVCLASS_FLOPPYDISK,
    en_GUID_DEVCLASS_GPS,
    en_GUID_DEVCLASS_HDC,
    en_GUID_DEVCLASS_HIDCLASS,
    en_GUID_DEVCLASS_IMAGE,
    en_GUID_DEVCLASS_INFINIBAND,
    en_GUID_DEVCLASS_INFRARED,
    en_GUID_DEVCLASS_KEYBOARD,
    en_GUID_DEVCLASS_LEGACYDRIVER,
    en_GUID_DEVCLASS_MEDIA,
    en_GUID_DEVCLASS_MEDIUM_CHANGER,
    en_GUID_DEVCLASS_MODEM,
    en_GUID_DEVCLASS_MONITOR,
    en_GUID_DEVCLASS_MOUSE,
    en_GUID_DEVCLASS_MTD,
    en_GUID_DEVCLASS_MULTIFUNCTION,
    en_GUID_DEVCLASS_MULTIPORTSERIAL,
    en_GUID_DEVCLASS_NET,
    en_GUID_DEVCLASS_NETCLIENT,
    en_GUID_DEVCLASS_NETSERVICE,
    en_GUID_DEVCLASS_NETTRANS,
    en_GUID_DEVCLASS_NODRIVER,
    en_GUID_DEVCLASS_PCMCIA,
    en_GUID_DEVCLASS_PNPPRINTERS,
    en_GUID_DEVCLASS_PORTS,
    en_GUID_DEVCLASS_PRINTER,
    en_GUID_DEVCLASS_PRINTERUPGRADE,
    en_GUID_DEVCLASS_PROCESSOR,
    en_GUID_DEVCLASS_SBP2,
    en_GUID_DEVCLASS_SCSIADAPTER,
    en_GUID_DEVCLASS_SECURITYACCELERATOR,
    en_GUID_DEVCLASS_SMARTCARDREADER,
    en_GUID_DEVCLASS_SOUND,
    en_GUID_DEVCLASS_SYSTEM,
    en_GUID_DEVCLASS_TAPEDRIVE,
    en_GUID_DEVCLASS_UNKNOWN,
    en_GUID_DEVCLASS_USB,// OcCrGetUsbRequestedAccess
    en_GUID_DEVCLASS_VOLUME,// OcCrGetVolumeRequestedAccess
    en_GUID_DEVCLASS_VOLUMESNAPSHOT,
    en_GUID_DEVCLASS_WCEUSBS,
    en_GUID_FILE_SYSTEM,
    en_GUID_DEVCLASS_WPD,
    en_GUID_DEVCLASS_LAST// the last entry in enum
} OC_EN_SETUP_CLASS_GUID;

typedef struct _OC_SETUP_CLASS_GUID{
    OC_EN_SETUP_CLASS_GUID    DevClassIndex;
    CONST GUID* CONST         PtrGuid;
} OC_SETUP_CLASS_GUID, *POC_SETUP_CLASS_GUID;

//--------------------------------------------------------------

//
// Bus type GUIDs enum.
// For reference see wdmguid.h
//
typedef enum _OC_EN_BUS_TYPE_GUID
{
    en_GUID_DL_BUS_TYPE_UNKNOWN,
    en_GUID_BUS_TYPE_INTERNAL,
    en_GUID_BUS_TYPE_PCMCIA,
    en_GUID_BUS_TYPE_PCI,
    en_GUID_BUS_TYPE_ISAPNP,
    en_GUID_BUS_TYPE_EISA,
    en_GUID_BUS_TYPE_MCA,
    en_GUID_BUS_TYPE_LPTENUM,
    en_GUID_BUS_TYPE_USBPRINT,
    en_GUID_BUS_TYPE_DOT4PRT,
    en_GUID_BUS_TYPE_SERENUM,
    en_GUID_BUS_TYPE_USB,
    en_GUID_BUS_TYPE_1394,
    en_GUID_BUS_TYPE_HID,
    en_GUID_BUS_TYPE_AVC,
    en_GUID_BUS_TYPE_IRDA,
    en_GUID_BUS_TYPE_SD
} OC_EN_BUS_TYPE_GUID;

typedef struct _OC_BUS_TYPE_GUID{
    OC_EN_BUS_TYPE_GUID    BusTypeIndex;
    const GUID*            PtrGuid;
} OC_BUS_TYPE_GUID, *POC_BUS_TYPE_GUID;

//--------------------------------------------------------

typedef struct _OC_DEVICE_PROPERTY_HEADER{

    struct {
        ULONG    Common:0x1;
        ULONG    USB:0x1;
    } PropertyType;

    //
    // setup class GUID index for class, 
    // see GUIDs in \HKLM\System\CCS\Control\Class or in devguid.h
    //
    OC_EN_SETUP_CLASS_GUID   SetupClassGuidIndex;

    //
    // GUID enum for the bus that the device is connected to.
    // See GUIDs in wdmguid.h.
    //
    OC_EN_BUS_TYPE_GUID      BusGuidIndex;

    //
    // device enumerator as in \HKLM\System\CCS\Control\Enum\Enumerator\InstanceId
    //
    OC_EN_ENUMERATOR         Enumerator;

    //
    // points to a NULL-terminated WCHAR string which
    // contains the name of the device's setup class
    //
    PWCHAR                   DevicePropertyClassName;

    //
    // points to a NULL-terminated WCHAR string which
    // contains a string describing the device, such as 
    // "Microsoft PS/2 Port Mouse", typically defined by the manufacturer. 
    //
    PWCHAR                   DevicePropertyDeviceDescription;

    //
    // points to a NULL-terminated WCHAR string which
    // contains the name of the driver-specific registry key
    //
    PWCHAR                   DevicePropertyDriverKeyName;

    //
    // points to a NULL-terminated WCHAR string which
    // contains the name of the PDO for this device
    //
    UNICODE_STRING           DevicePropertyPhysicalDeviceObjectName;

    //
    // points to a NULL-terminated WCHAR string which
    // contains the GUID for the device's setup class.
    //
    PWCHAR                   DevicePropertyClassGuid;

    //
    // points to a NULL-terminated WCHAR string which
    // contains  the name of the enumerator for the device, 
    // such as "PCI" or "root".
    //
    PWCHAR                   DevicePropertyEnumeratorName;

    //
    // contains the PnP registry string for the device
    //
    POBJECT_NAME_INFORMATION    DevicePropertyPnpRegistryString;

} OC_DEVICE_PROPERTY_HEADER, *POC_DEVICE_PROPERTY_HEADER;

//--------------------------------------------------------

typedef struct _OC_USB_DEVICE_DESCRIPTOR {

    //
    // the standdard descriptor is not fully initialized
    // as this requires to send an URB to a PDO which
    // might cause problems with some devices, the 
    // initialization is made through PnP database querying
    //
    USB_DEVICE_DESCRIPTOR             StandardDescriptor;

    PKEY_VALUE_PARTIAL_INFORMATION    CompatibleIds;

    ULONG                             UsbClassId;

    UCHAR                             id[ 16 ];

} OC_USB_DEVICE_DESCRIPTOR, *POC_USB_DEVICE_DESCRIPTOR;

//--------------------------------------------------------

//
// This is both a header for known ( i.e. 'concreate' ) device
// property objects and the property object for devices that 
// don't fall in any known category( USB, FireWire, Disk etc. )
//
typedef struct _OC_DEVICE_PROPERTY_OBJECT{

    OC_DEVICE_PROPERTY_HEADER    Header;

} OC_DEVICE_PROPERTY_OBJECT, *POC_DEVICE_PROPERTY_OBJECT;

//--------------------------------------------------------

//
// USB devices property object
//
typedef struct _OC_DEVICE_PROPERTY_USB_OBJECT{

    OC_DEVICE_PROPERTY_OBJECT         CommonProperty;

    OC_USB_DEVICE_DESCRIPTOR          UsbDescriptor;

} OC_DEVICE_PROPERTY_USB_OBJECT, *POC_DEVICE_PROPERTY_USB_OBJECT;

//--------------------------------------------------------

typedef struct _OC_DEVICE_RELATIONS{
    ULONG    Count;
    struct _OC_DEVICE_OBJECT*    Objects[ 0x1 ];
} OC_DEVICE_RELATIONS, *POC_DEVICE_RELATIONS;

//--------------------------------------------------------------

typedef struct _OC_RELATIONS_OBJECT{
    POC_DEVICE_RELATIONS    Relations;
} OC_RELATIONS_OBJECT, *POC_RELATIONS_OBJECT;

//--------------------------------------------------------------

struct _OC_FULL_DEVICE_TYPE_STACK;

typedef struct _OC_DEVICE_TYPE_OBJECT{
    struct _OC_FULL_DEVICE_TYPE_STACK*    TypeStack;
} OC_DEVICE_TYPE_OBJECT, *POC_DEVICE_TYPE_OBJECT;

//--------------------------------------------------------------

typedef struct _OC_DEVICE_OBJECT{

    //
    // the following fields - 
    //  -Enumerator
    //  -Pdo
    //  -DependFrom( and its lock RwSpinLock )
    // are the most frequently used one, so I put
    // them at the start of the structure in hoping
    // that they will fall in a one CPU's cache line
    //

    //
    // device enumerator as in \HKLM\System\CCS\Control\Enum\Enumerator\InstanceId,
    // valid only for the PDO object
    //
    OC_EN_ENUMERATOR             Enumerator;

    //
    // NULL for the PDO, not NULL for FiDO and FDO
    //
    struct _OC_DEVICE_OBJECT*    Pdo;

    //
    // the DeviceType, DependFrom and DeviceRelations arrays are protected 
    // by the RwSpinLock lock, usually these fields non NULL only for PDO
    //
    struct _OC_DEVICE_OBJECT*    DependFrom[ TargetDeviceRelation + 0x1 ];// protected by the RwSpinLock lock
    POC_RELATIONS_OBJECT         DeviceRelations[ TargetDeviceRelation + 0x1 ];// protected by the RwSpinLock lock
    POC_DEVICE_TYPE_OBJECT       DeviceType;// protected by the RwSpinLock lock
    OC_RW_SPIN_LOCK              RwSpinLock;

    //
    // the following field is used to speed up top down processing
    // it cashes the last found PDO during the PnP stack top-down
    // traversing, the object is referenced and protected
    // by the RwTraversingSpinLock
    //
    struct _OC_DEVICE_OBJECT*    NextTopDownTraversingPdo;
    OC_RW_SPIN_LOCK              RwTraversingSpinLock;

    OC_DEVICE_OBJECT_PNP_TYPE    DevicePnPType;

    //
    // The device state, as the state is 
    // reported in completion routine only 
    // PDO state makes sence
    //
    DEVICE_PNP_STATE             PnPState;

    PDEVICE_OBJECT               KernelDeviceObject;

    //
    // this device contains the device's information, not NULL
    // only for the PDO
    //
    POC_DEVICE_PROPERTY_OBJECT   DevicePropertyObject;

    //
    // flags
    //
    struct {
        ULONG    CripplePdo:0x1;
        ULONG    ParentChecked:0x1;
        ULONG    SpyFileObjects:0x1;
        ULONG    DeviceStartPending:0x1;
        ULONG    AttemptToFindFdoDone:0x1;// make sense only if the DevicePnPType is OcDevicePnPTypePdo
        ULONG    FdoFound:0x1;// make sense only if the DevicePnPType is OcDevicePnPTypePdo
        ULONG    UpperFilter:0x1;// make sense only if the DevicePnPType is OcDevicePnPTypeFilterDo
        ULONG    LowerFilter:0x1;// make sense only if the DevicePnPType is OcDevicePnPTypeFilterDo
        ULONG    DeviceIsNotTreeRoot:0x1;
        ULONG    DeviceIsProcessedTreeRoot:0x1;
    } Flags;

    //
    // device usage
    //
    struct {
        ULONG    DeviceUsageTypePaging:0x1;
        ULONG    DeviceUsageTypeHibernation:0x1;
        ULONG    DeviceUsageTypeDumpFile:0x1;
    }  DeviceUsage;

    //
    // the device object name returned by ObQueryNameString,
    // allocated from the PagedPool!
    //
    POBJECT_NAME_INFORMATION     DeviceNameInfo;

    //
    // besides being inserted in the hash all device objects
    // are linked in the global double-linked list protected
    // by the global lock
    //
    LIST_ENTRY                   ListEntry;

    //
    // the object can't be removed from the global list and 
    // the hash and can't be deinitialized if the lock is held,
    // so this lock is used when there is necessity to guarantee
    // that the device will be alive until some critical
    // processing has stopped. This lock is usually used
    // to block the device from receiving an IRP_MN_REMOVE
    // request.
    //
    OC_REMOVE_LOCK_FREE          RemoveLock;

    //
    // the event is set in a signal state after the 
    // eligible device's parent has been found,
    // Used only for the PDO! FDO and FiDO events
    // might be in a nonsignal state forever.
    //
    KEVENT                       PnPTreeBuildCompletedEvent;

    //
    // the list of upper devices for the PDO,
    // usually the devices in this list have
    // been found while processing some request
    // and their drivers have been hooked, but 
    // these devices are neither PnP filter devices no
    // PDO devices
    // The list is used to remove upper devices when
    // a PDO is removed, because I can trust only
    // to PnP filter devices, for other I might not
    // receive PnP requests because their drivers
    // might have been unhooked or rehooked.
    // It is unsafe to use this list in any place except
    // the device initializing and device removing code!
    //
    union{
        LIST_ENTRY               PdoHeadForListOfUpperDevices;
        LIST_ENTRY               EntryForListOfUpperDevices;
    } UpperDevicesList;

    //
    // the lock protects UpperDeviceList, used only for PDO
    //
    KSPIN_LOCK                   PdoUpperDevicesListSpinLock;

    //
    // The following lists contains requests that have been
    // issued on a device and have not been completed yet.
    // Usually these lists are used only for PDOs.
    //
    struct {

        //
        // this list contains OC_FILE_OBJECT_CREATE_INFO structures
        //
        LIST_ENTRY         CreateRequestsListHead;
        OC_RW_SPIN_LOCK    CreateRequestsListRwLock;

        //
        // an IO requests list contains OC_OPERATION_OBJECT
        //
        LIST_ENTRY         IoRequestsListHead;
        OC_RW_SPIN_LOCK    IoRequestsListRwLock;

    } DeviceRequests;

} OC_DEVICE_OBJECT, *POC_DEVICE_OBJECT;

//--------------------------------------------------------------

#if defined( DBG )

//
// unsafe function, used only in debug for ASSERT()
//
__forceinline
BOOLEAN
OcCrIsPdo(
    IN PDEVICE_OBJECT    DeviceObject
    )
{
    return OcIsFlagOn( DeviceObject->Flags, DO_BUS_ENUMERATED_DEVICE );
}
#endif//#if defined( DBG )

//--------------------------------------------------------------

extern
VOID
NTAPI
OcCrDeleteRelationsObject(
    IN POC_RELATIONS_OBJECT    PtrDeviceRelationsObject
    );

extern
VOID
NTAPI
OcCrDeleteDeviceTypeObject(
    IN POC_DEVICE_TYPE_OBJECT    PtrDeviceRelationsObject
    );

extern
VOID
NTAPI
OcCrDeleteDeviceObject(
    IN POC_DEVICE_OBJECT    PtrDeviceObject
    );

extern
VOID
OcCrProcessPnPRequestForHookedDriver(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

extern
VOID
NTAPI
OcCrCleanupDeviceAfterRemovingFromHash(
    IN POC_DEVICE_OBJECT    PtrDeviceObject
    );

extern
NTSTATUS
OcCrGetDeviceEnumeratorIndex(
    IN PDEVICE_OBJECT        PhysicalDeviceObject,
    OUT OC_EN_ENUMERATOR*    PtrDeviceEnumeratorIndex
    );

extern
NTSTATUS
OcCrAddInDatabaseDeviceInMiddleOfStack(
    IN PDEVICE_OBJECT    DeviceInMiddle,
    OUT POC_DEVICE_OBJECT*    PtrPtrOcMiddleDeviceObject
    );

extern
POC_DEVICE_OBJECT
OcCrGetDeviceObjectOnWhichFsdMounted(
    IN PFILE_OBJECT    FileObject
    );

extern
NTSTATUS
OcCrOpenDeviceKey(
    __in PDEVICE_OBJECT    InitializedPdo,
    __inout HANDLE*    KeyHandle
    );

__forceinline
POC_DEVICE_PROPERTY_HEADER
OcGetDeviceProperty(
    __in POC_DEVICE_OBJECT    OcDeviceObject
    )
    /*
    the function returns a property object for a device or NULL,
    the returned object is not referenced, because it is retained by the
    device or its PDO which in turns retained by the device
    */
{
    if( NULL != OcDeviceObject->DevicePropertyObject )
        return &OcDeviceObject->DevicePropertyObject->Header;
    else if( NULL != OcDeviceObject->Pdo && NULL != OcDeviceObject->Pdo->DevicePropertyObject )
        return &OcDeviceObject->Pdo->DevicePropertyObject->Header;
    else
        return NULL;
}

//--------------------------------------------------------------

#endif//_OC_DEVDATABASE_H_
