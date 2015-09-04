/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/
#ifndef _OC_CORE_PROTO_H_
#define _OC_CORE_PROTO_H_

#include "struct.h"

#if DBG
#define DebugPrint(_x_) \
               DbgPrint (CORE_DRIVERNAME_FOR_OUT); \
               DbgPrint _x_;

#define TRAP() DbgBreakPoint()

#else
#define DebugPrint(_x_)
#define TRAP()
#endif

#define OC_CORE_BUG_CODES_BASE              ( 0xAB300000 )
#define OC_CORE_BUG_DELETED_ENTRY_FOUND     ( OC_CORE_BUG_CODES_BASE + 0x1 )
#define OC_CORE_BUG_UNKNOWN_NODECTX         ( OC_CORE_BUG_CODES_BASE + 0x2 )
#define OC_CORE_BUG_ORPHAN_DIRTY_FLAG       ( OC_CORE_BUG_CODES_BASE + 0x3 )
#define OC_CORE_BUG_UNKNOWN_MFLTR_STATUS_CODE     ( OC_CORE_BUG_CODES_BASE + 0x4 )
#define OC_CORE_BUG_MFLTR_DAT_NOT_ON_STACK        ( OC_CORE_BUG_CODES_BASE + 0x5 )
#define OC_CORE_BUG_UNKNOWN_MFLTR_STATUS_CODE_ON_COMPLETION     ( OC_CORE_BUG_CODES_BASE + 0x6 )
#define OC_CORE_BUG_PDO_EQUAL_TO_ATTACHED_DEVICE     ( OC_CORE_BUG_CODES_BASE + 0x7 )
#define OC_CORE_BUG_UNSUPPORTED_REQUEST     ( OC_CORE_BUG_CODES_BASE + 0x8 )

///////////////////////////////////////////////////////
//
// init.c
//
///////////////////////////////////////////////////////

BOOLEAN
OcCrProcessQueryUnloadRequestIdempotent(
    IN BOOLEAN    Wait
    );

VOID
OcRemoveCoreControlDeviceObjectIdempotent();

///////////////////////////////////////////////////////
//
// common.c
//
///////////////////////////////////////////////////////

extern OC_GLOBAL    Global;

extern
NTSTATUS
NTAPI
OcCrHookedDriverDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

extern
NTSTATUS
NTAPI
OcCoreCreateDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

extern
NTSTATUS
NTAPI
OcCoreCleanupDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

extern
NTSTATUS
NTAPI
OcCoreCloseDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

extern
NTSTATUS
NTAPI
OcCoreDeviceControlDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

extern
long 
OcDummyExceptionFilter(
    IN PEXCEPTION_POINTERS  PtrExceptionPointers
    );

extern
long 
OcDummyExceptionFilterEx(
    IN PEXCEPTION_POINTERS    PtrExceptionPointers,
    IN NTSTATUS    ExceptionCode
    );

extern
NTSTATUS
OcCrGetValueFromKey(
    IN HANDLE    KeyHandle,
    IN PUNICODE_STRING    ValueName,
    IN KEY_VALUE_INFORMATION_CLASS   ValueInformationClass,
    OUT PVOID*    KeyValueInfo,
    OUT PULONG    InfoBufferLength
    );

extern
VOID
OcCrFreeValueFromKey(
    PVOID    KeyValueInfo
    );

extern
NTSTATUS
OcCrQueryObjectName(
    __in PVOID Object,
    __out POBJECT_NAME_INFORMATION*    PtrPtrDeviceNameInfo
    );

extern
VOID
OcCrFreeNameInformation(
    __in POBJECT_NAME_INFORMATION    PtrDeviceNameInfo
    );

extern
PDEVICE_OBJECT
OcCrIoGetAttachedDeviceReference(
    IN PDEVICE_OBJECT DeviceObject
    );

extern
PDEVICE_OBJECT
OcCrIoGetLowerDeviceObject(
    IN PDEVICE_OBJECT DeviceObject
    );

///////////////////////////////////////////////////////
//
// devdatabase.c
//
////////////////////////////////////////////////////////

extern
VOID
NTAPI
OcCrPnPFilterReportNewDevice(
    IN PDEVICE_OBJECT    Pdo,
    IN PDEVICE_OBJECT    AttachedDo,
    IN OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType,
    IN BOOLEAN           IsPdoInitialized
    );

extern
VOID
NTAPI
OcCrPnPFilterRepotNewDeviceState(
    IN PDEVICE_OBJECT    DeviceObject,
    IN DEVICE_PNP_STATE    NewState
    );

extern
VOID
NTAPI
OcCrPnPFilterRepotNewDeviceRelations(
    IN PDEVICE_OBJECT    DeviceObject,
    IN DEVICE_RELATION_TYPE    RelationType,
    IN PDEVICE_RELATIONS    DeviceRelations OPTIONAL// may be NULL 
                                                    // or might be allocated
                                                    // from the paged pool
    );

extern
NTSTATUS
NTAPI
OcPnPFilterPreStartCallback(
    PDEVICE_OBJECT    Self,
    PDEVICE_OBJECT    NextLowerDriver,
    PIRP    Irp,
    POC_FILTER_IRP_DECISION    PtrIrpDecision
    );

extern
VOID
NTAPI
OcCrPnPFilterDeviceUsageNotificationPreOperationCallback(
    IN PDEVICE_OBJECT    DeviceObject,
    IN ULONG_PTR         RequstId,
    IN DEVICE_USAGE_NOTIFICATION_TYPE    Type,
    IN BOOLEAN           InPath,
    IN PVOID             Buffer OPTIONAL
    );

extern
VOID
NTAPI
OcCrPnPFilterDeviceUsageNotificationPostOperationCallback(
    IN PDEVICE_OBJECT    DeviceObject,
    IN ULONG_PTR         RequstId,
    IN PIO_STATUS_BLOCK  StatusBlock
    );

extern
VOID
NTAPI
OcCrDeleteDeviceObjectType(
    IN POC_OBJECT_TYPE    PtrObjectType
    );

extern
POC_DEVICE_OBJECT
OcCrGetLowerPdoDevice(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject,
    IN OC_EN_ENUMERATOR*    EnumVectorToFind OPTIONAL,
    IN ULONG    VectorSize
    );

extern
VOID
OcCrProcessReportedDevices();


typedef
BOOLEAN
(*PtrOcCrNodeFunction)(
    IN POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    IN PVOID    Context
    );

extern
VOID
OcCrTraverseTopDown(
    IN POC_DEVICE_OBJECT    PtrOcTopDeviceObject,
    IN PtrOcCrNodeFunction  OcCrNodeFunction,
    IN PVOID                Context
    );

extern
NTSTATUS
OcCrTraverseFromDownToTop(
    IN POC_DEVICE_OBJECT    PtrDeviceObject,
    IN PtrOcCrNodeFunction  OcCrNodeFunction,
    IN PVOID                Context
    );

__forceinline
BOOLEAN
OcCrIsDeviceOnPagingPath(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject
    )
{
    //
    // check both the device and its PDO object because
    // in case of hooked drivers only PDO( or lower device ) 
    // contains the paging path flag because hooked driver's
    // devices do not process the PnP requests - they are 
    // processed by the attached PnP filter's device and flags
    // are transferred to the PDO
    //
    return  ( 0x1 == PtrOcDeviceObject->DeviceUsage.DeviceUsageTypePaging || 
              ( PtrOcDeviceObject->Pdo && 
                0x1 == PtrOcDeviceObject->Pdo->DeviceUsage.DeviceUsageTypePaging ) );
}

extern
VOID
OcCrEmulatePnpManagerForMachine(
    __in POC_PNP_CALLBACKS      PnpCallbacks
    );

///////////////////////////////////////////////////////////////
//
// deviceinfo.c
//
///////////////////////////////////////////////////////////////

extern
VOID
OcDereferenceDevicesAndFreeDeviceRelationsMemory(
    IN PDEVICE_RELATIONS DeviceRelations
    );

extern
NTSTATUS
OcCrGetDeviceProperty(
    IN PDEVICE_OBJECT    PhysicalDeviceObject,
    IN DEVICE_REGISTRY_PROPERTY    DeviceProperty,
    OUT PVOID*    PtrBuffer,
    OUT ULONG*    PtrValidDataLength OPTIONAL,
    OUT ULONG*    PtrBufferLength OPTIONAL
    );

extern
OC_EN_SETUP_CLASS_GUID
OcCrGetDeviceSetupClassGuidIndex(
    __in PUNICODE_STRING    SetupGuidString
    );

extern
VOID
OcCrFreeDevicePropertyBuffer(
    IN PVOID    Buffer
    );

extern
VOID
NTAPI
OcCrFreeDevicePropertyObject(
    IN POC_OBJECT_BODY    Object
    );

extern
NTSTATUS
OcQueryDeviceRelations(
    IN PDEVICE_OBJECT           DeviceObject,
    IN DEVICE_RELATION_TYPE     RelationsType,
    OUT PDEVICE_RELATIONS*      ReferencedDeviceRelations OPTIONAL
    );

extern
VOID
OcCrFreeCompatibleIdsStringForDevice(
    __in PKEY_VALUE_PARTIAL_INFORMATION    CompatibleIds
    );

extern
NTSTATUS
OcCrInitializeUsbDeviceDescriptor(
    __in PDEVICE_OBJECT    Pdo,
    __inout POC_USB_DEVICE_DESCRIPTOR    UsbDescriptor
    );

extern
NTSTATUS
OcCrGetDeviceRegistryKeyString(
    __in PDEVICE_OBJECT    InitializedPdo,
    __out POBJECT_NAME_INFORMATION*    PtrPnPKeyNameInfo
    );

extern
VOID
OcCrFreeDeviceRegistryKeyString(
    __in POBJECT_NAME_INFORMATION    PnPKeyNameInfo
    );

///////////////////////////////////////////////////////////////
//
// load_imagy_notify.c
//
////////////////////////////////////////////////////////////////

extern
NTSTATUS
OcCrInitializeImageNotifySubsystem();

extern
VOID
OcCrUninitializeImageNotifySubsystemIdempotent();

extern
NTSTATUS
OcCrRegistryClientForNotification(
    IN PVOID    Buffer,
    IN ULONG    BufferSize,
    IN MODE     PreviousMode//usually ExGetPreviousMode
    );

//////////////////////////////////////////////////////////////
//
// dumpdevdatabase.c
//
//////////////////////////////////////////////////////////////

extern
NTSTATUS
OcCrWriteDeviceDataBaseInFile(
    IN PUNICODE_STRING    FileName
    );

///////////////////////////////////////////////////////////////
//
// iomngr.c
//
///////////////////////////////////////////////////////////////

extern
NTSTATUS
OcCrProcessIoRequestFromHookedDriver(
    IN PDEVICE_OBJECT    SysDeviceObject,
    IN PIRP    Irp,
    IN PIO_STACK_LOCATION    PtrIrpStackLocation
    );

extern
NTSTATUS
NTAPI 
OcCrPnPFilterIoDispatcherCallback(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PDEVICE_OBJECT    LowerDeviceObject,
    IN PIRP    Irp,
    OUT POC_FILTER_IRP_DECISION    PtrIrpDecision
    );

extern
VOID
OcCrInitializeIoManager();

extern
VOID
OcCrUnInitializeIoManager();

extern
VOID
OcCrProcessRequestWithResult(
    IN OUT POC_NODE_CTX    NodeContext,
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject
    );

typedef BOOLEAN ( *OcCrDeviceIprRequestCompletedOrPending )(
    __in POC_NODE_CTX CONST    Context
    );

extern
BOOLEAN
OcCrHookedDrvRequestCompletedOrPending(
    __in POC_NODE_CTX CONST    Context
    );

extern
BOOLEAN
OcCrPnPFilterDrvRequestCompletedOrPending(
    __in POC_NODE_CTX CONST   Context
    );

extern
BOOLEAN
OcCrNodeFunctionHookedDriver(
    __in POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    __inout PVOID    Context
    );

extern
BOOLEAN
OcCrNodeFunctionPnPFilterDevice(
    __in POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    __inout PVOID    Context
    );

extern
VOID
OcCrProcessIrpBasedDeviceRequest(
    __in POC_DEVICE_OBJECT    PtrOcDeviceObject,
    __inout POC_NODE_CTX      Context,
    __in PtrOcCrNodeFunction  OcCrNodeFunction,
    __in OcCrDeviceIprRequestCompletedOrPending    RequestCompletedOrPending
    );

///////////////////////////////////////////////////////////////
//
// ioworkerthread.c
//
///////////////////////////////////////////////////////////////

NTSTATUS
OcCrIoPostponeNodeFunctionInWorkerThread(
    IN PtrOcCrNodeFunction    NodeFunction,
    IN POC_DEVICE_OBJECT    NodeDeviceObject,
    IN PVOID    Context
    );

///////////////////////////////////////////////////////////////
//
// filesystems.c
//
///////////////////////////////////////////////////////////////

extern
NTSTATUS
OcCrInitializeFsdSubsystem();

extern
NTSTATUS
OcCrStartFsdFiltering();

extern
VOID
OcCrUninitializeFsdSubsystemIdempotent(
    IN BOOLEAN    Wait
    );

extern
VOID
OcCrUnRegistreFsRegistrationChangeIdempotent(
    IN PDRIVER_OBJECT    CoreDriverObject
    );

__forceinline
BOOLEAN
OcCrIsFileSystemDevObj(
    IN PDEVICE_OBJECT   DeviceObject
    )
{
    return (BOOLEAN)( 
             FILE_DEVICE_DISK_FILE_SYSTEM == DeviceObject->DeviceType ||
             FILE_DEVICE_CD_ROM_FILE_SYSTEM == DeviceObject->DeviceType ||
             FILE_DEVICE_TAPE_FILE_SYSTEM == DeviceObject->DeviceType ||
             FILE_DEVICE_NETWORK_FILE_SYSTEM == DeviceObject->DeviceType ||
             FILE_DEVICE_DFS_FILE_SYSTEM == DeviceObject->DeviceType 
             );
}

extern
VOID
OcCrProcessFsdIrpRequestToHookedDriver(
    IN POC_HOOKED_DRV_NODE_CTX    HookedDrvContext,
    IN PIO_STACK_LOCATION    PtrIrpStackLocation
    );

extern
OC_ACCESS_RIGHTS
OcCrGetFsdRequestedAccess(
    IN POC_MINIFLTR_DRV_NODE_CTX    MnfltContext,
    IN POC_DEVICE_OBJECT     OcVolumeObject
    );

//////////////////////////////////////////////////////////////////////////
//
// fsminifilter.c
//
//////////////////////////////////////////////////////////////////////////

extern
NTSTATUS
OcCrFsmfRegisterMinifilter();

extern
VOID
OcCrFsmfUnregisterMinifilterIdempotent();

//////////////////////////////////////////////////////////////////////////
//
// comphooker.c
//
//////////////////////////////////////////////////////////////////////////

//
// only STATUS_SUCCESS or STATUS_MORE_PROCESSING_REQUIRED
// can be return by this function, if the callback doesn't call
// OcCmHkContinueCompletion( CompletionContext ) it must
// return STATUS_MORE_PROCESSING_REQUIRED else it 
// must return the value returned by OcCmHkContinueCompletion
//
typedef NTSTATUS (*OcCmHkIrpCompletionCallback)( 
    IN PVOID CompletionContext,// actually OC_CMHK_CONTEXT
    IN PVOID Context// CompletionContext->Context
    );

extern
VOID
OcCmHkInitializeCompletionHooker();

extern
VOID
OcCmHkUninitializeCompletionHooker();

extern
NTSTATUS
OcCmHkContinueCompletion(
    IN PVOID CompletionContext//actually OC_CMHK_CONTEXT
    );

extern
NTSTATUS
OcCmHkHookCompletionRoutineInCurrentStack(
    IN PIRP    Irp,
    IN OcCmHkIrpCompletionCallback    Callback,
    IN PVOID    CallbackContext
    );

extern
VOID
OcChHkPrepareCompletionContextForPostponing(
    IN PVOID    CompletionContextV,
    IN POC_CMHK_WAIT_BLOCK    CompletionWaitBlock OPTIONAL
   );

extern
NTSTATUS
OcCmHkPostponeCallbackInWorkerThread(
    IN OcCmHkIrpCompletionCallback    Callback,
    IN PVOID    CompletionContextV,//POC_CMHK_CONTEXT
    IN PVOID    CallbackContext,
    IN POC_CMHK_WAIT_BLOCK    CompletionWaitBlock
    );

extern
NTSTATUS
OcCmHkIrpCompletionTestCallback( 
    IN PVOID CompletionContext,// actually OC_CMHK_CONTEXT
    IN PVOID Context// CompletionContext->Context
    );

__forceinline
VOID
OcCmHkInitializeCompletionWaitBlock(
    IN POC_CMHK_WAIT_BLOCK    PtrCompletionWaitBlock,
    IN PKEVENT    Event
    )
{
    PtrCompletionWaitBlock->CompletionEvent = Event;
    PtrCompletionWaitBlock->WaitForCompletion = FALSE;
}

extern
PIRP
OcCmHkGetIrpFromCompletionContext(
    IN PVOID    CompletionContext
    );

extern
KIRQL
OcCmHkGetIrqlFromCompletionContext(
    IN PVOID    CompletionContext
    );

__forceinline
BOOLEAN
OcCmHkCanCompletionBePostponed(
     IN PVOID    CompletionContext//POC_CMHK_CONTEXT
    )
{
    //
    // the postponing of a completion for an Irp 
    // that has not been marked as pended can't 
    // be done at DISPATCH_LEVEL
    //
    return (!( DISPATCH_LEVEL == OcCmHkGetIrqlFromCompletionContext( CompletionContext ) && 
               FALSE == ( OcCmHkGetIrpFromCompletionContext( CompletionContext ) )->PendingReturned ) );
}

//////////////////////////////////////////////////////////////////////////////
//
// fileobject
//
//////////////////////////////////////////////////////////////////////////////

//
// see fileobject.h
//

//////////////////////////////////////////////////////////////////////////////
//
// operation.c
//
//////////////////////////////////////////////////////////////////////////////

extern
VOID
OcCrUnlockAndFreeMdlForOperation(
    __in POC_OPERATION_OBJECT    OperationObject
    );

extern
NTSTATUS
OcCrCreateOperationObjectForDevice(
    __in POC_DEVICE_OBJECT     PtrOcDeviceObject,
    __in CONST POC_NODE_CTX    NodeContext,
    __inout POC_OPERATION_OBJECT*    PtrOperationObject
    );


extern
NTSTATUS
OcCrCreateOperationObjectForFsd(
    __in POC_DEVICE_OBJECT    PtrOcVolumeDeviceObject,
    __in POC_MINIFLTR_DRV_NODE_CTX    MnfltContext,
    __inout POC_OPERATION_OBJECT*    PtrOperationObject
    );

extern
BOOLEAN
OcCrIsRequestInFsdDeviceQueue(
    __in POC_DEVICE_OBJECT    PtrOcVolumeDeviceObject,
    __in PFLT_CALLBACK_DATA    Data,
    __in POC_FILE_OBJECT    FileObject
    );

extern
BOOLEAN
OcCrIsRequestInDeviceQueue(
    __in POC_DEVICE_OBJECT    OcDeviceObject,
    __in PIRP    Irp
    );

//////////////////////////////////////////////////////////////////////////////
//
// disk.c
//
//////////////////////////////////////////////////////////////////////////////

extern
OC_ACCESS_RIGHTS 
OcCrGetDiskIoctlRequestedAccess(
    IN POC_NODE_CTX CONST    Context
    );

extern
BOOLEAN
OcCrIsMajorScsiRequest(
    __in POC_DEVICE_OBJECT    OcDeviceObject,
    __in PIRP    Irp
    );

extern
OC_ACCESS_RIGHTS 
OcCrGetVolumeRequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcVolumeDeviceObject
    );

extern
OC_ACCESS_RIGHTS 
OcCrGetOpticalDiskDriveRequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcDvdDeviceObject
    );

extern
OC_ACCESS_RIGHTS 
OcCrGetDiskDriveRequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcDiskDeviceObject
    );

///////////////////////////////////////////////////////////////////////////////
//
// shadow.c
//
///////////////////////////////////////////////////////////////////////////////

extern
NTSTATUS
OcCrShadowRequest(
    __inout POC_OPERATION_OBJECT    OperationObject
    );

extern
NTSTATUS
OcCrInitializeShadowingSubsystem();

extern
VOID
OcCrUnInitializeShadowingSubsystem();

extern
VOID
OcCrShadowCopingInPrivateBuffersCompleted(
    IN POC_OPERATION_OBJECT    OperationObject
    );

extern
NTSTATUS
OcCrShadowProcessOperationBuffersForFsd(
    IN POC_OPERATION_OBJECT    OperationObject
    );

extern
NTSTATUS
OcCrShadowProcessOperationBuffersForDevice(
    __in POC_OPERATION_OBJECT    OperationObject,
    __inout POC_SHADOWED_REQUEST_COMPLETION_PARAMETERS   CompletionParam
    );

extern
NTSTATUS
OcCrShadowCompleteDeviceRequest(
    __in POC_SHADOWED_REQUEST_COMPLETION_PARAMETERS    CompletionParam
    );

////////////////////////////////////////////////////////////////////////////////
//
// usb.c
//
////////////////////////////////////////////////////////////////////////////////

extern
OC_ACCESS_RIGHTS 
OcCrGetUsbRequestedAccess(
    IN POC_NODE_CTX CONST    Context,
    IN POC_DEVICE_OBJECT     OcUsbDeviceObject
    );

extern
OC_ACCESS_RIGHTS
OcCrConvertToUsbRights(
    __in OC_ACCESS_RIGHTS    AccessRights
    );


/////////////////////////////////////////////////////////////////////////////////
//
// buffers.c
//
/////////////////////////////////////////////////////////////////////////////////

extern
PVOID
OcCrAllocateShadowBuffer(
    __in ULONG     BufferSize,
    __in BOOLEAN   CanBePagedOut,
    __inout POC_PRIVATE_BUFFER_TYPE    BufferType
    );

extern
VOID
OcCrFreeShadowBuffer(
    __in PVOID    ShadowBuffer,
    __in OC_PRIVATE_BUFFER_TYPE    BufferType
    );

/////////////////////////////////////////////////////////////////////////////////
//
// ieee1394.c
//
/////////////////////////////////////////////////////////////////////////////////

extern
OC_ACCESS_RIGHTS 
OcCrGetIEEE1394RequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcIEEE1394DeviceObject
    );

extern
OC_ACCESS_RIGHTS
OcCrConvertAnyRightsToIEEE1394Rights(
    __in OC_ACCESS_RIGHTS    AccessRights
    );

/////////////////////////////////////////////////////////////////////////////////
//
// bluetooth.c
//
/////////////////////////////////////////////////////////////////////////////////

extern
OC_ACCESS_RIGHTS 
OcCrGetBluetoothRequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcBthDeviceObject
    );

/////////////////////////////////////////////////////////////////////////////////
//
// security_descr.c
//
/////////////////////////////////////////////////////////////////////////////////

extern
NTSTATUS
OcSdInitializeSecurityDescriptorsSubsystem();

extern
VOID
OcSdUninitializeSecurityDescriptorsSubsystem();

typedef struct _OC_CHECK_SECURITY_REQUEST{
    __in OC_SD_TYPE           SdType;
    __in OC_DEVICE_TYPE       DeviceType;
    __in PSECURITY_SUBJECT_CONTEXT    SubjectSecurityContext;
    __in BOOLEAN              SubjectContextLocked;
    __in OC_ACCESS_RIGHTS     DesiredAccess;
    __in KPROCESSOR_MODE      AccessMode;
    __out PACCESS_MASK        GrantedAccess;
    __out PNTSTATUS           AccessStatus;
    __out PBOOLEAN            IsAccessGranted;
} OC_CHECK_SECURITY_REQUEST, *POC_CHECK_SECURITY_REQUEST;

extern
BOOLEAN
OcIsAccessGrantedSafe(
    __inout POC_CHECK_SECURITY_REQUEST    Request
    );

extern
BOOLEAN
OcCrGetFullDeviceType(
    __in POC_DEVICE_OBJECT                PtrOcTopDeviceObject,
    __inout POC_FULL_DEVICE_TYPE_STACK    PtrFullDeviceTypeStack
    );

////////////////////////////////////////////////////////////////////
//
// dldriverconnection.c
//
////////////////////////////////////////////////////////////////////

extern
POC_PNP_CALLBACKS
OcDlDrvConnectionHandleToPnpCalbacks(
    __in OC_DLDRVR_CONNECTION_HANDLE    ConnectionHandle
    );

extern
NTSTATUS
OcEstablishDlDriverConnection(
    __inout POCORE_TO_DLDRIVER    ConnectStruct
    );

extern
VOID
OcInitializeDlDriverConnectionSubsystem();

extern
OC_DLDRVR_CONNECTION_HANDLE
OcReferenceDlDriverConnection();

extern
VOID
OcDereferenceDlDriverConnection(
    __in OC_DLDRVR_CONNECTION_HANDLE    ConnectionHandle
    );

#endif//_OC_CORE_PROTO_H_