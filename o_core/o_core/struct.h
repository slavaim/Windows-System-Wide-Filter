/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/
#ifndef _OC_CORE_STRUCT_H_
#define _OC_CORE_STRUCT_H_

#include <ntifs.h>
#undef INITGUID // clever and astute MS's guys define INITGUID in fltkernel.h
#include <fltkernel.h>
#include <wdmsec.h> // for IoCreateDeviceSecure
#include <limits.h>

#include <OcKernelUserMode.h>
#include <PnPFilterControl.h>
#include <OcHooker.h>
#include <ocobject.h>
#include <octhreadpool.h>
#include <ochash.h>
#include "queue_lock.h"
#include "remove_lock.h"
#include "devdatabase.h"
#include "fileobject.h"
#include "ntddkex.h"
#include "fsminifilter_emul.h"
#include "operation.h"
#include "DlDriver2Ocore.h"

//----------------------------------------------------

#define OC_FO_CTX_HASH_LINES_NUMBER   127

//----------------------------------------------------

typedef struct _OC_CMHK_WAIT_BLOCK{
    PKEVENT    CompletionEvent;
    BOOLEAN    WaitForCompletion;
} OC_CMHK_WAIT_BLOCK, *POC_CMHK_WAIT_BLOCK;

//----------------------------------------------------

typedef enum {
    OcNodeCtxHookedDriverFlag = 0x1,    //OC_HOOKED_DRV_NODE_CTX
    OcNodeCtxPnPFilterDriverFlag = 0x2, //OC_PNPFLTR_DRV_NODE_CTX
    OcNodeCtxMinifilterDriverFlag = 0x4, //OC_MINIFLTR_DRV_NODE_CTX
    OcNodePendingFlag = 0x8,
    OcNodeAloocatedFromPool = 0x10,
    //
    // the following flag is set if the caller 
    // is a 32 bits process on a 64 bits OS or
    // this is a 32 bits system
    //
    OcNodeCtx32bitProcessFlag = 0x20,
    OcNodeOcDeviceObjectReferenced = 0x40,
    OcNodeCtxAllFlags = 0xFFFFFFF
} OC_NODE_CTX_FLAGS;

typedef enum {

    //
    // call the original driver
    //
    OcIrpDecisionCallOriginal = 0x1,

    //
    // irp has been pended or completed
    //
    OcIrpDecisionReturnStatus = 0x2

} OC_HOOKED_DRV_IRP_DECISION;

//typedef ULONG    OC_ACCESS_RIGHTS;
typedef ULONG    OC_DEVICE_TYPE;

#define OC_NODE_CTX_SIGNATURE    0xFAFAFAFA

typedef struct _OC_NODE_CTX{

#if DBG
    ULONG             Signature;//OC_NODE_CTX_SIGNATURE
    LIST_ENTRY        ListEntry;
    ULONG             Size;
#endif//DBG

    OC_NODE_CTX_FLAGS         Flags;

    //
    // the Irp which is currently being processed or
    // minifilter callback data
    //
    union{
        PIRP                  Irp;
        PFLT_CALLBACK_DATA    Data;
    } RequestData;

    //
    // device object that receives the Irp( either hooked or PnP filter's device )
    // always NULL for minifilters
    //
    PDEVICE_OBJECT       OriginalDeviceObject OPTIONAL;
    POC_DEVICE_OBJECT    OcOriginalDeviceObject OPTIONAL;

    OC_REQUEST_SECURITY_PARAMETERS    SecurityParameters;

    struct{

        //
        // set to 0x1 before calling the callback 
        // for an upper device and switched off by 
        // the upper device's callback
        // 
        ULONG             UpperDevice:0x1;

        //
        // set to 0x1 after calling OcCrGetDeviceRequestedAccess for
        // an upper device in the device chain
        //
        ULONG             AccessRightsReceived:0x1;

        //
        // set to ox1 if the request is for direct device read-write
        //
        ULONG             DirectDeviceOpenRequest:0x1;

    } RequestCurrentParameters;

    //
    // status set by device node function
    //
    union{
        NTSTATUS                     StatusCode;//OUT
        FLT_PREOP_CALLBACK_STATUS    MiniFilterStatus;//OUT
    };
} OC_NODE_CTX, *POC_NODE_CTX;

//----------------------------------------------------

#define OC_ALL_NODE_TYPE_FLAGS   ( OcNodeCtxHookedDriverFlag | \
                                   OcNodeCtxPnPFilterDriverFlag | \
                                   OcNodeCtxMinifilterDriverFlag )

__forceinline
OC_NODE_CTX_FLAGS
OcCrGetNodeTypeFlag(
    IN POC_NODE_CTX    NodeCtx
    )
{
    //
    // the context is valid iff the only one flag is set
    //
    ASSERT( ( OcIsFlagOn( NodeCtx->Flags, OcNodeCtxHookedDriverFlag ) && 
             !OcIsFlagOn( NodeCtx->Flags, OcNodeCtxPnPFilterDriverFlag ) && 
             !OcIsFlagOn( NodeCtx->Flags, OcNodeCtxMinifilterDriverFlag ) )
             ||
            ( !OcIsFlagOn( NodeCtx->Flags, OcNodeCtxHookedDriverFlag ) && 
              OcIsFlagOn( NodeCtx->Flags, OcNodeCtxPnPFilterDriverFlag ) && 
              !OcIsFlagOn( NodeCtx->Flags, OcNodeCtxMinifilterDriverFlag ) ) 
             ||
            ( !OcIsFlagOn( NodeCtx->Flags, OcNodeCtxHookedDriverFlag ) && 
              !OcIsFlagOn( NodeCtx->Flags, OcNodeCtxPnPFilterDriverFlag ) && 
              OcIsFlagOn( NodeCtx->Flags, OcNodeCtxMinifilterDriverFlag ) ) );

    return ( NodeCtx->Flags & OC_ALL_NODE_TYPE_FLAGS );
}

//----------------------------------------------------

typedef struct _OC_HOOKED_DRV_NODE_CTX{
    OC_NODE_CTX                   Common;
    PDEVICE_OBJECT                HookedSysDeviceObject;//IN
    OC_HOOKED_DRV_IRP_DECISION    HookedDrvIrpDecision;//OUT
} OC_HOOKED_DRV_NODE_CTX, *POC_HOOKED_DRV_NODE_CTX;

//----------------------------------------------------

typedef struct _OC_PNPFLTR_DRV_NODE_CTX{
    OC_NODE_CTX               Common;
    PDEVICE_OBJECT            NextLowerDeviceObject;//IN
    OC_FILTER_IRP_DECISION    PnPFltrIrpDecision;//OUT
} OC_PNPFLTR_DRV_NODE_CTX, *POC_PNPFLTR_DRV_NODE_CTX;

//----------------------------------------------------

typedef struct _OC_MINIFLTR_DRV_NODE_CTX{
    OC_NODE_CTX            Common;
    PCFLT_RELATED_OBJECTS  FltObjects;//IN
    POC_FILE_OBJECT        FileObject OPTIONAL;//IN
    PVOID*                 CompletionContext;//OUT
} OC_MINIFLTR_DRV_NODE_CTX, *POC_MINIFLTR_DRV_NODE_CTX;


typedef union _OC_UNITED_DRV_NODE_CTX{
    OC_NODE_CTX                Common;
    OC_HOOKED_DRV_NODE_CTX     HookedDrvCtx;
    OC_PNPFLTR_DRV_NODE_CTX    FilterDrvCtx;
    OC_MINIFLTR_DRV_NODE_CTX   MinifilterDrvCtx;
} OC_UNITED_DRV_NODE_CTX, *POC_UNITED_DRV_NODE_CTX;

//----------------------------------------------------

typedef struct _OC_IO_MANAGER_GLOBAL{

#if DBG
    //
    // all contexts alocated from the pools
    // are linked in a double linked list,
    // see _OC_NODE_CTX.ListEntry
    //
    LIST_ENTRY    NodeCtxListHead;
    KSPIN_LOCK    ListSpinLock;
    ULONG         NumberOfAllocatedCtxEntries;
#endif//DBG

    ULONG                    Initialized;
    ULONG                    MemoryTag;
    NPAGED_LOOKASIDE_LIST    NodeCtxHookedDrvLookasideList;
    NPAGED_LOOKASIDE_LIST    NodeCtxPnPDrvLookasideList;

} OC_IO_MANAGER_GLOBAL, *POC_IO_MANAGER_GLOBAL;

//----------------------------------------------------

typedef NTKERNELAPI NTSTATUS ( NTAPI *OcIoOpenDeviceRegistryKeyPtr )(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG DevInstKeyType,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE DevInstRegKey
    );

typedef NTKERNELAPI NTSTATUS ( NTAPI *OcIoGetDevicePropertyPtr)(
    IN PDEVICE_OBJECT DeviceObject,
    IN DEVICE_REGISTRY_PROPERTY DeviceProperty,
    IN ULONG BufferLength,
    OUT PVOID PropertyBuffer,
    OUT PULONG ResultLength
    );

typedef NTKERNELAPI PDEVICE_OBJECT ( NTAPI *OcIoGetAttachedDeviceReferencePtr)(
    IN PDEVICE_OBJECT  DeviceObject
    );

typedef NTKERNELAPI PDEVICE_OBJECT ( NTAPI *OcIoGetDeviceAttachmentBaseRefPtr)(
    IN PDEVICE_OBJECT  DeviceObject
    );

typedef NTKERNELAPI PDEVICE_OBJECT ( NTAPI *OcCrIoGetLowerDeviceObjectPtr )(
    IN PDEVICE_OBJECT  DeviceObject
    );

typedef struct _SYSTEM_FUNCTIONS{
    OcIoOpenDeviceRegistryKeyPtr         IoOpenDeviceRegistryKey;
    OcIoGetDevicePropertyPtr             IoGetDeviceProperty;
    OcIoGetAttachedDeviceReferencePtr    IoGetAttachedDeviceReference;
    OcIoGetDeviceAttachmentBaseRefPtr    IoGetDeviceAttachmentBaseRef;
    OcCrIoGetLowerDeviceObjectPtr        IoGetLowerDeviceObject;
} SYSTEM_FUNCTIONS, *PSYSTEM_FUNCTIONS;

//----------------------------------------------------

typedef NTSTATUS ( FLTAPI *OcFltRegisterFilterPtr)(
    IN PDRIVER_OBJECT Driver,
    IN CONST FLT_REGISTRATION *Registration,
    OUT PFLT_FILTER *RetFilter);

typedef VOID ( FLTAPI *OcFltUnregisterFilterPtr) (
    IN PFLT_FILTER Filter
    );

typedef NTSTATUS ( FLTAPI *OcFltStartFilteringPtr) (
    IN PFLT_FILTER Filter
    );

typedef PVOID ( FLTAPI *OcFltGetRoutineAddressPtr) (
    IN PCSTR FltMgrRoutineName
    );

typedef BOOLEAN ( FLTAPI *OcFltIs32bitProcessPtr)(
  IN PFLT_CALLBACK_DATA  CallbackData  OPTIONAL
  );

typedef struct _MINIFILTER_FUNCTIONS{
    OcFltRegisterFilterPtr      FltRegisterFilter;
    OcFltUnregisterFilterPtr    FltUnregisterFilter;
    OcFltStartFilteringPtr      FltStartFiltering;
    OcFltIs32bitProcessPtr      FltIs32bitProcess;
    //
    // FltGetRoutineAddress can be used only after calling FltRegisterFilter
    //
    OcFltGetRoutineAddressPtr   FltGetRoutineAddress;
} MINIFILTER_FUNCTIONS, *PMINIFILTER_FUNCTIONS;

//----------------------------------------------------

#define OC_FSD_MF_NOT_REGISTERED   0x0L
#define OC_FSD_MF_REGISTERED       0x1L

typedef struct _OC_MINIFILTER_GLOBAL{

    //
    // FSD minifilter
    //
    PFLT_FILTER      FilterHandle;

    //
    // Current filter's state
    // OC_FSD_MF_NOT_REGISTERED if the filter has not been registered with the manager
    // OC_FSD_MF_REGISTERED if the filter has been successfully registered with the manager
    //
    ULONG            State;

    FLT_OPERATION_REGISTRATION    OcCrFsmfRegisteredCallbacks[ IRP_MJ_MAXIMUM_FUNCTION + FLT_INTERNAL_OPERATION_COUNT ];

} OC_MINIFILTER_GLOBAL, *POC_MINIFILTER_GLOBAL;

//----------------------------------------------------

typedef struct _OC_GLOBAL{

    //
    // the driver object 
    //
    PDRIVER_OBJECT    DriverObject;

    //
    // the PnP filter's communication device object
    //
    PDEVICE_OBJECT    PnPFilterDeviceObject;

    //
    // the hooker driver's communication device object
    //
    PDEVICE_OBJECT    HookerDeviceObject;

    //
    // the following device is used for communication
    // between the kernel and user mode
    //
    PDEVICE_OBJECT    ControlDeviceObject;

    //
    // worker threads for querying PnP device relations
    //
    POC_THREAD_POOL_OBJECT    ThreadsPoolObject;

    //
    // worker threads for start device processing
    //
    POC_THREAD_POOL_OBJECT    StartDeviceThreadsPoolObject;

    //
    // worker threads for processing the completion at a low IRQL
    //
    POC_THREAD_POOL_OBJECT    IrpCompletionThreadsPoolObject;

    //
    // worker threads for processing the IO requests( IRPs ) at a low IRQL
    //
    POC_THREAD_POOL_OBJECT    IoRequestsThreadsPoolObject;

    //
    // the thread that is used for querying devices' properties
    //
    POC_WORKER_THREAD_OBJECT    QueryDevicePropertyThread;

    //
    // the event is set in a signal state when
    // it is safe to unmap the code of the callbacks
    // reported to the PnP filter
    //
    KEVENT    PnPFilterDisconnectionEvent;

    //
    // the event is set in a signal state when
    // it is safe to unmap the code of the callbacks 
    // reported to the driver hooker
    //
    KEVENT    HookerDisconnectionEvent;

    //
    // function exported by the PnP filter
    //
    OC_FILTER_CALLBACK_METHODS_EXPORT    PnPFilterExports;

    //
    // functions exported by the hooker driver
    //
    OC_HOOKER_CALLBACK_METHODS_EXPORT    DriverHookerExports;

    //
    // cookie reported by the hooker
    //
    ULONG_PTR    HookerCookie;

    //
    // set to TRUE after the information about existing devices
    // has been collected
    //
    BOOLEAN    DeviceInformationCollected;

    MM_SYSTEMSIZE    SystemSize;
    ULONG            NumberOfProcessorUnits;
    ULONG            BaseNumberOfWorkerThreads;

    //
    // object type for the device object
    //
    OC_OBJECT_TYPE    OcDeviceObjectType;

    //
    // type for the OC_DEVICE_PROPERTY_USB_OBJECT
    //
    OC_OBJECT_TYPE    OcDevicePropertyUsbType;

    //
    // type for the OC_DEVICE_PROPERTY_OBJECT, i.e. unknown device
    //
    OC_OBJECT_TYPE    OcDevicePropertyCommonType;

    //
    // object type for the device relations
    //
    OC_OBJECT_TYPE    OcDeviceRelationsObjectType;

    //
    // object type for file object
    //
    OC_OBJECT_TYPE    OcFleObjectType;

    //
    // object type for file object
    //
    OC_OBJECT_TYPE    OcFileContextObjectType;

    //
    // object type for file object's information
    //
    OC_OBJECT_TYPE    OcFileCreateInfoObjectType;

    //
    // object type for operation object
    //
    OC_OBJECT_TYPE    OcOperationObject;

    //
    // object type for device's type object
    //
    OC_OBJECT_TYPE    OcDeviceTypeObjectType;

    //
    // a hash for device objects for that I receive requests, i.e.
    // the driver is hooked or there are PnP filter's devices
    //
    POC_HASH_OBJECT    PtrDeviceHashObject;

    //
    // a hash for the file objects' contexts
    //
    POC_HASH_OBJECT    PtrFoContextHashObject;

    //
    // used for synchronizing FO contexts inserting in the hash
    //
    OC_QUEUE_LOCK      FoCtxQueueLock[ OC_FO_CTX_HASH_LINES_NUMBER ];

    //
    // a hash for the file objects
    //
    POC_HASH_OBJECT    PtrFoHashObject;

    //
    // set in a signal state when all device objects are removed
    // and the device object type has been uninitialized
    //
    KEVENT    OcDeviceObjectTypeUninitializationEvent;

    //
    // set in a signal state when all file objects and their
    // contexts have been removed and the context object type
    // has been uninitialized
    //
    KEVENT    OcContextObjectTypeUninitializationEvent;

    //
    // set to current thread before calling Global.PnPFilterExports.ReportDevices
    // and to NULL after calling, so I can do some sanity checks
    // based on the knowledge that I in the context of the
    // thread receiving fake notification requests
    //
    PETHREAD    ThreadCallingReportDevice;

    //
    // resource that protects the hash from inserting
    // the duplicate entries, actually
    // only exclusive semantic is used, so it is
    // possible to elaborate a light version of the
    // lock that doesn't elevate the IRQL
    //
    ERESOURCE    DeviceHashResource;

    SYSTEM_FUNCTIONS        SystemFunctions;
    MINIFILTER_FUNCTIONS    MinifilterFunctions;

    //
    // this lock is used to protect from premature driver 
    // uninitialization, it is acquired before posting
    // the processing in some deffered threads which
    // this driver can't controll( opposite to its own
    // thread pool ) i.e. system's worker thread, also
    // it is acquired in many functions just for 
    // simplicity because the driver unload code 
    // waits for this lock releasing and then
    // waits for completing of all other requests.
    // Also, all who want to know whether the driver 
    // is unloading can try to acquire the remove lock.
    //
    OC_REMOVE_LOCK_FREE    RemoveLock;

    ULONG    ConcurrentUnloadQueryCounter;

    //
    // beside being inserted in the hash device objects
    // are linked in the list
    //
    LIST_ENTRY         DevObjListHead;
    OC_RW_SPIN_LOCK    DevObjListLock;

    //
    // set to TRUE if the IoRegisterFsRegistrationChange was called successfully
    //
    ULONG            FsdRegistrationChangeInit;
    ULONG            FsdSubsystemInit;

    //
    // the minifilter descriptor
    //
    OC_MINIFILTER_GLOBAL    FsdMinifilter;

    //
    // the IO manager descriptor
    //
    OC_IO_MANAGER_GLOBAL    IoManager;

    //
    // this list is used for OC_FLT_CALLBACK_DATA_STORE structure allocations
    //
    NPAGED_LOOKASIDE_LIST   FltCallbackDataStoreLaList;

    //
    // the string with the full name for the control device symbolic link
    //
    PWCHAR                  FullSymbolicLinkStr;

} OC_GLOBAL, *POC_GLOBAL;

//----------------------------------------------------

//
// Extensions structure for the DeviceObjectExtension field in 
// the _DEVICE_OBJECT structure.
// Used only for the Windows 2000.
//
typedef struct _DEVOBJ_EXTENSION_W2K {

    CSHORT          Type;
    USHORT          Size;

    //
    // Public part of the DeviceObjectExtension structure
    //

    PDEVICE_OBJECT  DeviceObject;               // owning device object

// end_ntddk end_nthal end_ntifs end_wdm

    //
    // Universal Power Data - all device objects must have this
    //

    ULONG           PowerFlags;             // see ntos\po\pop.h
                                            // WARNING: Access via PO macros
                                            // and with PO locking rules ONLY.

    //
    //    Pointer to the non-universal power data
    //  Power data that only some device objects need is stored in the
    //  device object power extension -> DOPE
    //  see po.h
    //

    PVOID            Dope;

    //
    // power state information
    //

    //
    // Device object extension flags.  Protected by the IopDatabaseLock.
    //

    ULONG            ExtensionFlags;

    //
    // PnP manager fields
    //

    PVOID           DeviceNode;

    //
    // AttachedTo is a pointer to the device object that this device
    // object is attached to.  The attachment chain is now doubly
    // linked: this pointer and DeviceObject->AttachedDevice provide the
    // linkage.
    //

    PDEVICE_OBJECT  AttachedTo;

    //
    // Doubly-linked list of file objects
    //

    LIST_ENTRY      FileObjectList;

// begin_ntddk begin_wdm begin_nthal begin_ntifs

} DEVOBJ_EXTENSION_W2K, *PDEVOBJ_EXTENSION_W2K;

//----------------------------------------------------

//
// the following structure is used to simplify the 
// parameters propagation between functions
//
typedef struct _OC_SHADOWED_REQUEST_COMPLETION_PARAMETERS{
    PVOID                   CompletionContext;//actually POC_CMHK_CONTEXT
    KEVENT                  CompletionEvent;
    OC_CMHK_WAIT_BLOCK      CompletionWaitBlock;
    BOOLEAN                 CompleteSynchronously;
} OC_SHADOWED_REQUEST_COMPLETION_PARAMETERS, *POC_SHADOWED_REQUEST_COMPLETION_PARAMETERS;


//----------------------------------------------------

//
// the following enum defines the types of security descriptor
//
typedef enum {
    OcAccessSd = 0x0,// defnes access permissions to a device
    OcLogAllowedSd,  // defines what types of allowed requests to log
    OcLogDeniedSd,    // defines what types of denied requests to log
    OcMaximumSdType
} OC_SD_TYPE;

//----------------------------------------------------

//
// the following structure defines the device node types
//
typedef struct _OC_FULL_DEVICE_TYPE{

    //
    // the type of the FDO defined by the setup class
    //
    OC_DEVICE_TYPE      FdoMajorType;

    //
    // the type of the PDO defined by the enumerating bus,
    // implicitly defined by the setup class of the FDO
    // creating PDO
    //
    OC_DEVICE_TYPE      PdoMajorType;

    //
    // the Pdo minor type defines the translation from
    // Fdo type to Pdo type, this translation is used
    // to convert an FDO level permisions to a PDO level one
    //
    OC_DEVICE_TYPE      PdoMinorType;

} OC_FULL_DEVICE_TYPE, *POC_FULL_DEVICE_TYPE;

#define OC_TYPE_STACK_MEM_TAG    ((ULONG)'STcO')

typedef struct _OC_FULL_DEVICE_TYPE_STACK{
    USHORT                NumberOfEntries;
    USHORT                NumberOfValidEntries;// <= NumberOfEntries
    OC_FULL_DEVICE_TYPE   FullDeviceType[ 0x1 ];// actually an array with NumberOfEntries entries
} OC_FULL_DEVICE_TYPE_STACK, *POC_FULL_DEVICE_TYPE_STACK;

//----------------------------------------------------

#define OC_DLDRVR_CONNECTION_HANDLE    ULONG_PTR
#define OC_INVALID_DLDRV_CONNECTION    ((ULONG_PTR)NULL)

//----------------------------------------------------


#endif//_OC_CORE_STRUCT_H_
