/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/

//
// use Win2k compatible lists
//
#define _WIN2K_COMPAT_SLIST_USAGE 1

#include <ntddk.h>
#include <wdmsec.h> // for IoCreateDeviceSecure
#include <initguid.h>
//#include <dontuse.h>
#include <ocobject.h>
#include <octhreadpool.h>
#include <PnPFilterControl.h>
#include <ocrwspinlock.h>

//
// the following definition is from the ntifs.h file
//
#if (NTDDI_VERSION >= NTDDI_WIN2K)
NTKERNELAPI
BOOLEAN
IoIsSystemThread(
    __in PETHREAD Thread
    );
#endif // NTDDI_VERSION

//
// GUID definition are required to be outside of header inclusion pragma to avoid
// error during precompiled headers.
//

#if !defined(_FILTER_H_)
#define _FILTER_H_

#define DRIVERNAME "filter.sys: "

#define OC_PNPFILTER_BUG_CODES_BASE                 ( 0xAB200000 )
#define OC_PNPFILTER_BUG_UNINITIALIZED_CALLBACK     ( OC_PNPFILTER_BUG_CODES_BASE + 0x1 )
#define OC_PNPFILTER_BUG_UNKNOWN_HOOKED_DRIVER_ON_UNLOAD   ( OC_PNPFILTER_BUG_CODES_BASE + 0x2 )
#define OC_PNPFILTER_BUG_UNKNOWN_HOOKED_DRIVER_ON_PNP      ( OC_PNPFILTER_BUG_CODES_BASE + 0x3 )
#define OC_PNPFILTER_BUG_ACQUIRING_REMOVE_LOCK_FAILED      ( OC_PNPFILTER_BUG_CODES_BASE + 0x4 )
#define OC_PNPFILTER_BUG_DEFERRED_VPDO_DELETION_FAILED     ( OC_PNPFILTER_BUG_CODES_BASE + 0x5 )
#define OC_PNPFILTER_BUG_INSERTED_VIRT_PDO_NOT_FOUND       ( OC_PNPFILTER_BUG_CODES_BASE + 0x6 )

#if BUS_LOWER

#undef DRIVERNAME
#define DRIVERNAME "BFdoLwr.sys: "

#endif

#if BUS_UPPER

#undef DRIVERNAME
#define DRIVERNAME "BFdoUpr.sys: "

#endif

#if DEVICE_LOWER

#undef DRIVERNAME
#define DRIVERNAME "DevLower.sys: "

#endif

#if DEVICE_UPPER

#undef DRIVERNAME
#define DRIVERNAME "DevUpper.sys: "

#endif

#if CLASS_LOWER

#undef DRIVERNAME
#define DRIVERNAME "ClsLower.sys: "

#endif

#if CLASS_UPPER

#undef DRIVERNAME
#define DRIVERNAME "Oxxx PnP filter: "

#endif

#if DBG
#define DebugPrint(_x_) \
               DbgPrint (DRIVERNAME); \
               DbgPrint _x_;

#define TRAP() DbgBreakPoint()

#else//DBG
#define DebugPrint(_x_)
#define TRAP()
#endif//DBG

//--------------------------------------------------

#ifndef  STATUS_CONTINUE_COMPLETION //required to build driver in Win2K and XP build environment
//
// This value should be returned from completion routines to continue
// completing the IRP upwards. Otherwise, STATUS_MORE_PROCESSING_REQUIRED
// should be returned.
//
#define STATUS_CONTINUE_COMPLETION      STATUS_SUCCESS

#endif//STATUS_CONTINUE_COMPLETION

#define POOL_TAG   'iFcO'

//--------------------------------------------------

typedef enum _DEVICE_TYPE {

    DEVICE_TYPE_INVALID = 0,         // Invalid Type;
    DEVICE_TYPE_FIDO,                // Device is a filter device.
    DEVICE_TYPE_FIDO_NO_PNP,         // Device is attached by the call from the external driver, not from PnP filter
    DEVICE_TYPE_CDO,                 // Device is a control device.

} DEVICE_TYPE;

//
// A common header for the device extensions of the Filter and control
// device objects
//

//--------------------------------------------------

typedef struct _COMMON_DEVICE_DATA
{
    OC_CORE_EXTENSION_HEADER    CoreHeader;
    DEVICE_TYPE       Type;

} COMMON_DEVICE_DATA, *PCOMMON_DEVICE_DATA;

//--------------------------------------------------

struct _DEVICE_RELATIONS_SHADOW;

//--------------------------------------------------

typedef struct _DEVICE_EXTENSION
{
    COMMON_DEVICE_DATA;

    //
    // A back pointer to the device object.
    //

    PDEVICE_OBJECT    Self;

    //
    // The top of the stack before this filter was added if the device
    // is for PnP filter, if the extension descibes Pdo the field is NULL
    //

    PDEVICE_OBJECT    NextLowerDriver;

    //
    // current PnP state of the device
    //

    DEVICE_PNP_STATE    DevicePnPState;

    //
    // Remembers the previous pnp state
    //

    DEVICE_PNP_STATE    PreviousPnPState;

    //
    // Removelock to track IRPs so that device can be removed and
    // the driver can be unloaded safely. Also used when the extension
    // describes PDO.
    //
    IO_REMOVE_LOCK    RemoveLock;

    //
    // the relations reference count is used for the "virtual devices" bookkeeping,
    // this reference is incremented each time a PDO has been reported
    // in any relation and is decremennted when a PDO has not been reported
    //
    ULONG             RelationReferenceCount;

    //
    // the refrence count is used to avoid premature deletion
    // of virtual PDO structure, doesn't used for real devices
    //
    ULONG             ReferenceCount;

    PDEVICE_OBJECT    PhysicalDeviceObject;

    //
    // a referenced device extension for the "virtual PDO"
    //
    struct _DEVICE_EXTENSION*    VirtualPdoExtension;

    //
    // device relations, never used for PDO
    //
    PDEVICE_RELATIONS           DeviceRelations[ TargetDeviceRelation + 0x1 ];
    struct _DEVICE_RELATIONS_SHADOW*    DeviceRelationsShadow[ TargetDeviceRelation + 0x1 ];

    //
    // resource is used to synchronize access to the device relations
    //
    ERESOURCE    RelationResource;

    //
    // all device extension are connected in the 
    // double linked list
    //
    LIST_ENTRY    ListEntry;

    //
    // Device type
    //
    OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType;

    //
    // device usage
    //
    struct {
        ULONG    DeviceUsageTypePaging:0x1;
        ULONG    DeviceUsageTypeHibernation:0x1;
        ULONG    DeviceUsageTypeDumpFile:0x1;
    }  DeviceUsage;

    struct {
        ULONG    FilterAttached:0x1;
        ULONG    RemovedFromList:0x1;// valid only for virtual PDOs
        ULONG    MarkedForDeletion:0x1;// valid only for virtual PDOs
    } Flags;

#if DBG
    struct {
    PETHREAD     DeletingThread;
    } DebugInfo;
#endif //DBG
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

//--------------------------------------------------

typedef struct _CONTROL_DEVICE_EXTENSION {

    COMMON_DEVICE_DATA;

    ULONG   Deleted; // False if the deviceobject is valid, TRUE if it's deleted

    PVOID   ControlData; // Store your control data here

} CONTROL_DEVICE_EXTENSION, *PCONTROL_DEVICE_EXTENSION;

//--------------------------------------------------

typedef struct _DEVICE_RELATIONS_SHADOW {
    ULONG Count;
    PDEVICE_EXTENSION Objects[1];  // variable length
} DEVICE_RELATIONS_SHADOW, *PDEVICE_RELATIONS_SHADOW;

//--------------------------------------------------

typedef struct _OC_PNP_FILTER_CONNECT_OBJECT{

    OC_FILTER_VERSION    Version;
    OC_FILTER_VERSION    ClientVersion;

    //
    // set in a signal state when it is safe to 
    // unload the code with callbacks
    //
    PKEVENT    PtrDisconnectEvent;

    //
    // registered callbacks
    //
    OC_FILTER_CALLBACK_METHODS    CallbackMethods;

} OC_PNP_FILTER_CONNECT_OBJECT, *POC_PNP_FILTER_CONNECT_OBJECT;

//--------------------------------------------------

typedef NTKERNELAPI NTSTATUS( NTAPI *OcIoAttachDeviceToDeviceStackSafePtr)(
    IN PDEVICE_OBJECT  SourceDevice,
    IN PDEVICE_OBJECT  TargetDevice,
    IN OUT PDEVICE_OBJECT  *AttachedToDeviceObject 
    );

//--------------------------------------------------

typedef NTKERNELAPI VOID ( NTAPI *DlIoSynchronousInvalidateDeviceRelationsPtr)(
    PDEVICE_OBJECT DeviceObject,
    DEVICE_RELATION_TYPE Type
    );

//--------------------------------------------------

typedef struct _PNP_FILTER_GLOBAL{

    PDRIVER_OBJECT    DriverObject;

    OC_OBJECT_TYPE    ConnectObjectType;

    //
    // device object for communication with 
    // this filter driver
    //
    PDEVICE_OBJECT    ControlDeviceObject;

    //
    // the object for the pool of worker threads
    //
    POC_THREAD_POOL_OBJECT    ThreadsPoolObject;

    //
    // If TRUE the driver has been initialized properly
    // If FALSE then there was an error but the
    // driver is loaded, the STATUS_SUCCESS was returned 
    // from the DriverEntry but no any device object will be 
    // created. The filter is loaded only to 
    // aboid the BSOD with STATUS_INACCESSIBLE_BOOT_DEVICE.
    //
    BOOLEAN    IsFilterInNormalState;

    //
    // connection object
    //
    POC_PNP_FILTER_CONNECT_OBJECT    PtrConnectionObject;

    //
    // read/write spin lock is used to protect the PtrConnectionObject field
    //
    OC_RW_SPIN_LOCK    RwSpinLock;

    //
    // head of the list for the device extensions
    //
    LIST_ENTRY    ListHead;

    //
    // lock for protecting the list of device extensions
    //
    KSPIN_LOCK    SpinLock;

    //
    // the resource is used to synchronize insertion-removal of "virtual devices'"
    // device extensions in the list
    //
    ERESOURCE     VirtualDevicesListResource;

    //
    // the function which is not always exported by the kernel
    //
    OcIoAttachDeviceToDeviceStackSafePtr    IoAttachDeviceToDeviceStackSafe;

    //
    // the function which is not exported by the old kernels
    //
    DlIoSynchronousInvalidateDeviceRelationsPtr    IoSynchronousInvalidateDeviceRelations;

    //
    // Fast IO dispatch table for this driver, used to intercepr the call to IoDeleteDevice
    // for devices to which this driver has attached, because the PnP filter had not done this
    //
    FAST_IO_DISPATCH        FastIoDispatch;

    //
    // the memory pool is used to allocate device extensions for the "virtual devices"
    // i.e. the devices for which only information have been saved but no actual
    // filter devices were created
    //
    NPAGED_LOOKASIDE_LIST    DeviceExtensionMemoryPool;

    //
    // the number of the virtual PDOs
    //
    ULONG    VirtualPdoCount;

} PNP_FILTER_GLOBAL, *PPNP_FILTER_GLOBAL;

//--------------------------------------------------

extern PNP_FILTER_GLOBAL    PnpFilterGlobal;

//--------------------------------------------------

#endif//_FILTER_H_

