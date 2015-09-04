/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/

#if !defined(_PNP_FILTER_PROTO_H_)
#define _PNP_FILTER_PROTO_H_

#define INITIALIZE_PNP_STATE(_Data_)    \
        (_Data_)->DevicePnPState =  NotStarted;\
        (_Data_)->PreviousPnPState = NotStarted;

#if DBG
    //
    // moving from any state to NotStarted is an error because
    // the NotStarted state is an initial state in which 
    // transition is impossible
    //
    #define SET_NEW_PNP_STATE(_Data_, _state_) \
        ASSERT( !( NotStarted == (_state_) && \
                   NotStarted != (_Data_)->DevicePnPState ) );\
        (_Data_)->PreviousPnPState = (_Data_)->DevicePnPState;\
        (_Data_)->DevicePnPState = (_state_);
#else
    #define SET_NEW_PNP_STATE(_Data_, _state_) \
        (_Data_)->PreviousPnPState =  (_Data_)->DevicePnPState;\
        (_Data_)->DevicePnPState = (_state_);
#endif

#define RESTORE_PREVIOUS_PNP_STATE(_Data_)   \
        (_Data_)->DevicePnPState =   (_Data_)->PreviousPnPState;\


#if defined( DBG )

//
// unsafe function, used only in debug for ASSERT()
//
__forceinline
BOOLEAN
OcFilterIsPdo(
    IN PDEVICE_OBJECT    DeviceObject
    )
{
    return OcIsFlagOn( DeviceObject->Flags, DO_BUS_ENUMERATED_DEVICE );
}
#endif//#if defined( DBG )

/////////////////////////////////////////////////////////////////
//
// filter.c
//
/////////////////////////////////////////////////////////////////

DRIVER_INITIALIZE DriverEntry;

DRIVER_ADD_DEVICE FilterAddDevice;

DRIVER_DISPATCH FilterDispatchIo;

extern
NTSTATUS
OcFilterAddVirtualDeviceForPdo(
    __in PDEVICE_OBJECT    Pdo,
    __in OC_DEVICE_OBJECT_PNP_TYPE    PdoType
    );

extern
VOID
OcFilterDeleteVirtualPdoByExtension(
    __in PDEVICE_EXTENSION    deviceExtension
    );

extern
VOID
OcFilterDeleteVirtualPdo(
    __in PDEVICE_OBJECT    Pdo
    );

extern
VOID
FilterClearFilterDeviceExtension(
    __inout PDEVICE_EXTENSION    deviceExtension
    );

extern
PCHAR
PnPMinorFunctionString (
    UCHAR MinorFunction
);

extern
POC_PNP_FILTER_CONNECT_OBJECT
OcFilterReferenceCurrentConnectObject();

extern
NTSTATUS
FilterAddDeviceEx(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType,
    IN OUT PDEVICE_OBJECT    *PtrFiletrDeviceObject OPTIONAL,
    IN BOOLEAN    CalledByPnPFromAddDevice
    );

extern
VOID
OcFilterRemoveDeviceFromTheList(
    IN PDEVICE_EXTENSION   deviceExtension
    );

extern
VOID
OcFreeAllDeviceRelations(
    IN IN PDEVICE_EXTENSION    deviceExtension
    );

extern
PDEVICE_RELATIONS_SHADOW
FilterProcessPdoInformationForRelation(
    __in PDEVICE_RELATIONS    InputDeviceRelations,
    __in_opt PDEVICE_RELATIONS_SHADOW    DeviceRelationsShadow OPTIONAL,
    __in BOOLEAN    AddToList
    );

/////////////////////////////////////////////////////////////////
//
// pnpfilter.c
//
/////////////////////////////////////////////////////////////////

DRIVER_DISPATCH FilterDispatchPnp;

DRIVER_DISPATCH FilterDispatchPower;

DRIVER_DISPATCH FilterPass;

IO_COMPLETION_ROUTINE FilterDeviceUsageNotificationCompletionRoutine;

extern
NTSTATUS
FilterQueryDeviceRelationsCompletionRoutine(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    );

extern
NTSTATUS
FilterStartCompletionRoutine(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    );

extern
VOID
OcFilterReportNewDeviceState(
    IN PDEVICE_EXTENSION    deviceExtension
    );

extern
VOID
OcFilterReportPdoStateRelatedToPnpManager(
    __in PDEVICE_EXTENSION    deviceExtension
    );

extern
VOID
NTAPI
OcFilterReportExisitingDevices(
    );

extern
NTSTATUS
NTAPI
OcFilterAddDeviceFTI(
    __in PDEVICE_OBJECT    Pdo
    );

/////////////////////////////////////////////////////////////////////
//
// device_filter.c
//
/////////////////////////////////////////////////////////////////////

extern
VOID
NTAPI
OcFilterFastIoDetachDevice(
    IN struct _DEVICE_OBJECT *SourceDevice,
    IN struct _DEVICE_OBJECT *TargetDevice
    );

extern
NTSTATUS
NTAPI
OcFilterAttachToDevice(
    IN PDEVICE_OBJECT    DeviceObject
    );

//////////////////////////////////////////////////////////////////////
//
// fastio.c
//
///////////////////////////////////////////////////////////////////////

extern
BOOLEAN
OcFilterFastIoCheckIfPossible (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in ULONG Length,
    __in BOOLEAN Wait,
    __in ULONG LockKey,
    __in BOOLEAN CheckForReadOperation,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoRead (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in ULONG Length,
    __in BOOLEAN Wait,
    __in ULONG LockKey,
    __out_bcount(Length) PVOID Buffer,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoWrite (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in ULONG Length,
    __in BOOLEAN Wait,
    __in ULONG LockKey,
    __in_bcount(Length) PVOID Buffer,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoQueryBasicInfo (
    __in PFILE_OBJECT FileObject,
    __in BOOLEAN Wait,
    __out_bcount(sizeof(FILE_BASIC_INFORMATION)) PFILE_BASIC_INFORMATION Buffer,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoQueryStandardInfo (
    __in PFILE_OBJECT FileObject,
    __in BOOLEAN Wait,
    __out_bcount(sizeof(FILE_STANDARD_INFORMATION)) PFILE_STANDARD_INFORMATION Buffer,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoLock (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in PLARGE_INTEGER Length,
    __in PEPROCESS ProcessId,
    __in ULONG Key,
    __in BOOLEAN FailImmediately,
    __in BOOLEAN ExclusiveLock,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoUnlockSingle (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in PLARGE_INTEGER Length,
    __in PEPROCESS ProcessId,
    __in ULONG Key,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoUnlockAll (
    __in PFILE_OBJECT FileObject,
    __in PEPROCESS ProcessId,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoUnlockAllByKey (
    __in PFILE_OBJECT FileObject,
    __in PVOID ProcessId,
    __in ULONG Key,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoDeviceControl (
    __in PFILE_OBJECT FileObject,
    __in BOOLEAN Wait,
    __in_bcount_opt(InputBufferLength) PVOID InputBuffer,
    __in ULONG InputBufferLength,
    __out_bcount_opt(OutputBufferLength) PVOID OutputBuffer,
    __in ULONG OutputBufferLength,
    __in ULONG IoControlCode,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoQueryNetworkOpenInfo (
    __in PFILE_OBJECT FileObject,
    __in BOOLEAN Wait,
    __out_bcount(sizeof(FILE_NETWORK_OPEN_INFORMATION)) PFILE_NETWORK_OPEN_INFORMATION Buffer,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoMdlRead (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in ULONG Length,
    __in ULONG LockKey,
    __deref_out PMDL *MdlChain,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoMdlReadComplete (
    __in PFILE_OBJECT FileObject,
    __in PMDL MdlChain,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoPrepareMdlWrite (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in ULONG Length,
    __in ULONG LockKey,
    __deref_out PMDL *MdlChain,
    __inout PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoMdlWriteComplete (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in PMDL MdlChain,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoReadCompressed (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in ULONG Length,
    __in ULONG LockKey,
    __out_bcount(Length) PVOID Buffer,
    __deref_out PMDL *MdlChain,
    __inout PIO_STATUS_BLOCK IoStatus,
    __out_bcount(CompressedDataInfoLength) struct _COMPRESSED_DATA_INFO *CompressedDataInfo,
    __in ULONG CompressedDataInfoLength,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoWriteCompressed (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in ULONG Length,
    __in ULONG LockKey,
    __in_bcount(Length) PVOID Buffer,
    __deref_out PMDL *MdlChain,
    __inout PIO_STATUS_BLOCK IoStatus,
    __out_bcount(CompressedDataInfoLength) struct _COMPRESSED_DATA_INFO *CompressedDataInfo,
    __in ULONG CompressedDataInfoLength,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoMdlReadCompleteCompressed (
    __in PFILE_OBJECT FileObject,
    __in PMDL MdlChain,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoMdlWriteCompleteCompressed (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER FileOffset,
    __in PMDL MdlChain,
    __in PDEVICE_OBJECT DeviceObject
    );

extern
BOOLEAN
OcFilterFastIoQueryOpen (
    __in PIRP Irp,
    __out_bcount(sizeof(FILE_NETWORK_OPEN_INFORMATION)) PFILE_NETWORK_OPEN_INFORMATION NetworkInformation,
    __in PDEVICE_OBJECT DeviceObject
    );

//////////////////////////////////////////////////////////////////////
//
// pnpfilter_hook.c
//
///////////////////////////////////////////////////////////////////////

extern
VOID
OcFilterInitializeHookerEngine();

extern
NTSTATUS
OcFilterHookDriver(
    __in PDRIVER_OBJECT    DriverObject
    );

extern
PDEVICE_EXTENSION
OcFilterReturnReferencedPdoExtensionLockIf(
    __in PDEVICE_OBJECT    Pdo,
    __in BOOLEAN           ReturnLockedExtension
    );

#if DBG
extern
PDEVICE_EXTENSION
OcFilterReturnPdoExtensionUnsafe(
    __in PDEVICE_OBJECT    Pdo
    );
#endif//DBG

extern
VOID
OcFilterDereferenceVirtualPdoExtension(
    __in PDEVICE_EXTENSION    deviceExtension
    );

extern
VOID
OcFilterReferenceVirtualPdoExtension(
    __in PDEVICE_EXTENSION    deviceExtension
    );

extern
BOOLEAN
OcFilterRenderVirtualPdoAsDeleted(
    __in PDEVICE_EXTENSION    deviceExtension
    );

#endif//_PNP_FILTER_PROTO_H_
