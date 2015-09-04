/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/
/*
This file contains structures for communication
between PnP filter and core module
*/

#if !defined(_PNP_FILTER_CONTROL_H_)
#define _PNP_FILTER_CONTROL_H_

#ifndef _NTIFS_INCLUDED_
#include <ntddk.h>
#endif //_NTIFS_
#include <ocrwspinlock.h>

// {1DA53BCA-F834-4c0b-AC63-AB48A27A2FDD} - generated using guidgen.exe
DEFINE_GUID(GUID_SD_FILTER_CONTROL_OBJECT, 
0x1da53bca, 0xf834, 0x4c0b, 0xac, 0x63, 0xab, 0x48, 0xa2, 0x7a, 0x2f, 0xdd);

#ifdef __cplusplus
extern "C" {
#endif//__cplusplus

//------------------------------------------------------------

#define PNPFILTER_DRIVER_OBJECT_NAME        L"ocpnpfilterdriver"
#define PNPFILTER_CONTROL_OBJECT_NAME       L"OcPnPFilterControlDevice"
#define NTDEVICE_DIRECTORY                  L"\\Device\\"
#define NTDOS_DEVICE_DIRECTORY              L"\\DosDevices\\"
#define NTDEVICE_NAME_STRING_PNPFILTER      L"\\Device\\"PNPFILTER_CONTROL_OBJECT_NAME
#define SYMBOLIC_NAME_STRING_PNPFILTER      L"\\DosDevices\\"PNPFILTER_CONTROL_OBJECT_NAME

//------------------------------------------------------------

//
// These are the states Filter transition to upon
// receiving a specific PnP Irp. Refer to the PnP Device States
// diagram in DDK documentation for better understanding.
//

typedef enum _DEVICE_PNP_STATE {

    NotStarted = 0,         // Not started yet
    Started,                // Device has received the START_DEVICE IRP
    StopPending,            // Device has received the QUERY_STOP IRP
    Stopped,                // Device has received the STOP_DEVICE IRP
    RemovePending,          // Device has received the QUERY_REMOVE IRP
    SurpriseRemovePending,  // Device has received the SURPRISE_REMOVE IRP
    Deleted                 // Device has received the REMOVE_DEVICE IRP

} DEVICE_PNP_STATE;

#define    PNP_TYPE( _CLASS, _TYPE )  ( _CLASS*0x10000 + _TYPE )// the result is _OC_DEVICE_OBJECT_PNP_TYPE type
#define    GET_PNP_TYPE_CLASS( __OC_DEVICE_OBJECT_PNP_TYPE ) ( __OC_DEVICE_OBJECT_PNP_TYPE/0x10000 )

//
// currently I have 5 classes
// 0x0 - all devices, type unknown
// 0x1 - lower device in the stack, possibly Non PnP device
// 0x2 - possibly Non PnP device in the middle of the stack
// 0x3 - PnP device in the middle of stack
// 0x4 - device type is known, PnP Device
// the transition is possible only from a group with the 
// smaller class number to a group with the bigger class
// number, i.e. when the possible set of device type is 
// getting narrow, transition inside group is prohibited
//
typedef enum _OC_DEVICE_OBJECT_PNP_TYPE{
    OcDevicePnpTypeUnknow = PNP_TYPE( 0x0, 0x0 ),
    OcDevicePnPTypePdo = PNP_TYPE( 0x4, 0x0 ),// PnP Pdo device
    OcDevicePnPTypeFilterDo = PNP_TYPE( 0x4, 0x1 ),// our filter's device
    OcDevicePnPTypeFunctionalDo = PNP_TYPE( 0x4, 0x2 ),
    OcDevicePnPTypeInMiddleOfStack = PNP_TYPE( 0x3, 0x0 ), // actually device type unknown
    OcDeviceNoPnPTypeInMiddleOfStack = PNP_TYPE( 0x2, 0x0 ), // the device type is unknown and it is possibly not a PnP device
    OcDeviceLowerNoPnPType = PNP_TYPE( 0x1, 0x0 ) // actually this device is used as PDO for non PnP device's stacks
} OC_DEVICE_OBJECT_PNP_TYPE;

typedef struct _OC_CORE_EXTENSION_HEADER{
    OC_RW_SPIN_LOCK    RwLock;
    PVOID              Context;
} OC_CORE_EXTENSION_HEADER, *POC_CORE_EXTENSION_HEADER;

typedef enum _OC_FILTER_IRP_DECISION{

    //
    // send Irp to lower device, skip the current stack
    //
    OcFilterSendIrpToLowerSkipStack = 0x0,

    //
    // send Irp to lower device, do not skip the current stack
    //
    OcFilterSendIrpToLowerDontSkipStack = 0x1,

    //
    // exit from function with returned code
    //
    OcFilterReturnCode = 0x2

} OC_FILTER_IRP_DECISION, *POC_FILTER_IRP_DECISION;

//------------------------------------------------------------

//
// the callback is called at PASSIVE_LEVEL
//
typedef
VOID
(NTAPI *OcPnPFilterReportNewDevice)(
    IN PDEVICE_OBJECT    Pdo,
    IN PDEVICE_OBJECT    AttachedDo,
    IN OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType,
    IN BOOLEAN           IsPdoInitialized
    );

//
// the callback should be called at PASSIVE_LEVEL
// but due to an erroneous upper filter might
// be called at an elevated IRQL, the callee
// must process this correctly
//
typedef 
VOID
(NTAPI *OcPnPFilterRepotNewDeviceState)(
    IN PDEVICE_OBJECT    DeviceObject,
    IN DEVICE_PNP_STATE    NewState
    );

//
// the callback is called at PASSIVE_LEVEL
// and at APC_LEVEL then reporting 
// about already existing devices, i.e.
// after connecting to the PnP filter driver
//
typedef 
VOID
(NTAPI *OcPnPFilterRepotNewDeviceRelations)(
    IN PDEVICE_OBJECT    DeviceObject,
    IN DEVICE_RELATION_TYPE    RelationType,
    IN PDEVICE_RELATIONS    DeviceRelations OPTIONAL// may be NULL 
                                                    // or might be allocated
                                                    // from the paged pool
    );

//
// the following callback is used as a preoperation callback
// for IRP_MN_DEVICE_USAGE_NOTIFICATION
//
typedef 
VOID
(NTAPI *OcPnPFilterDeviceUsageNotificationPreCallback)(
    IN PDEVICE_OBJECT    DeviceObject,
    IN ULONG_PTR         RequstId,
    IN DEVICE_USAGE_NOTIFICATION_TYPE    Type,
    IN BOOLEAN           InPath,
    IN PVOID             Buffer OPTIONAL
    );


//
// the following callback is used as a postoperation callback
// for IRP_MN_DEVICE_USAGE_NOTIFICATION
//
typedef 
VOID
(NTAPI *OcPnPFilterDeviceUsageNotificationPostCallback)(
    IN PDEVICE_OBJECT    DeviceObject,
    IN ULONG_PTR         RequstId,
    IN PIO_STATUS_BLOCK  StatusBlock
    );

typedef
NTSTATUS
(NTAPI *OcPnPDispatcherCallback)(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PDEVICE_OBJECT    NextLowerDeviceObject,
    IN PIRP    Irp,
    OUT POC_FILTER_IRP_DECISION    PtrIrpDecision
    );

typedef
NTSTATUS
(NTAPI *OcPnPPreStartCallback)( 
    PDEVICE_OBJECT    Self,
    PDEVICE_OBJECT    NextLowerDriver,
    PIRP    Irp,
    POC_FILTER_IRP_DECISION    PtrIrpDecision
    );

//
// called by the client of the PnP filter
// when it wants to disconnect
//
typedef
VOID
( NTAPI *OcPnPFilterDisconnectCallback )(
    IN PVOID Context
    );

//
// called by the client to order the filter to attach its 
// device to the object, the caller must guarantee that the
// device to which the attachment is made will not go out,
// also the caller should be prepared to receive a recursive 
// new device request and a device start request, the pre-start
// callback will not be called because it is called only for 
// devices reported by the PnP manager
//
typedef
NTSTATUS
( NTAPI *OcFilterAttachToDeviceCallback )(
    IN PDEVICE_OBJECT    DeviceObject// attach to this device, it will be reported as PDO
    );

typedef
VOID
( NTAPI *OcPnPFilterReportDevicesCallback )();

//------------------------------------------------------------

#define OC_CURRENT_FILTER_VERSION    ((ULONG)0x1)

//------------------------------------------------------------

typedef struct _OC_FILTER_VERSION{
    ULONG    Version;
    ULONG    Size;
} OC_FILTER_VERSION, *POC_FILTER_VERSION;

typedef struct _OC_FILTER_CALLBACK_METHODS{
    OcPnPFilterReportNewDevice            ReportNewDevice;
    OcPnPFilterRepotNewDeviceState        RepotNewDeviceState;
    OcPnPFilterRepotNewDeviceRelations    RepotNewDeviceRelations;
    OcPnPFilterDeviceUsageNotificationPreCallback      UsageNotifyPreCallback;
    OcPnPFilterDeviceUsageNotificationPostCallback     UsageNotifyPostCallback;
    OcPnPPreStartCallback                 PreStartCallback OPTIONAL;
    OcPnPDispatcherCallback               DispatcherCallback;
} OC_FILTER_CALLBACK_METHODS, *POC_FILTER_CALLBACK_METHODS;

typedef struct _OC_FILTER_CALLBACK_METHODS_EXPORT{
    OC_FILTER_VERSION                   Version;
    OcPnPFilterDisconnectCallback       Disconnect;
    OcPnPFilterReportDevicesCallback    ReportDevices;
    OcFilterAttachToDeviceCallback      AttachToNonPnPDevice;
} OC_FILTER_CALLBACK_METHODS_EXPORT, *POC_FILTER_CALLBACK_METHODS_EXPORT;

typedef struct _OC_PNP_FILTER_CONNECT_INITIALIZER{

    OC_FILTER_VERSION    Version;

    //
    // set in a signal state when it is safe to 
    // unload the code with callbacks
    //
    PKEVENT    PtrDisconnectEvent;

    //
    // PnP filter initializes this structure
    //
    POC_FILTER_CALLBACK_METHODS_EXPORT    PnPFilterExports;

    //
    // callbacks from internal source
    //
    OC_FILTER_CALLBACK_METHODS    CallbackMethods;

} OC_PNP_FILTER_CONNECT_INITIALIZER, *POC_PNP_FILTER_CONNECT_INITIALIZER;

//------------------------------------------------------------

typedef NTSTATUS ( NTAPI *OcFilterAddDeviceCallbackFTI )(
    __in PDEVICE_OBJECT    Pdo
    );

typedef struct _OC_PNP_FILTER_FIRST_TIME_START{
    OC_FILTER_VERSION               Version;
    OcFilterAddDeviceCallbackFTI    AddDeviceFTI;
} OC_PNP_FILTER_FIRST_TIME_START, *POC_PNP_FILTER_FIRST_TIME_START;

//------------------------------------------------------------

#define FILE_DEVICE_OCFILTER           0x00008888
#define OCFILTER_IOCTL_INDEX           0x888

//
// IOCTL_OC_CONNECT_TO_FILTER - request a connection to the filter.
// The input buffer contains an initialized OC_PNP_FILTER_CONNECT_INITIALIZER
// structure
//
#define IOCTL_OC_CONNECT_TO_FILTER    CTL_CODE( FILE_DEVICE_OCFILTER, OCFILTER_IOCTL_INDEX + 0, METHOD_BUFFERED, FILE_ANY_ACCESS )
#define IOCTL_OC_CONNECT_FOR_FIRST_TIME_START    CTL_CODE( FILE_DEVICE_OCFILTER, OCFILTER_IOCTL_INDEX + 1, METHOD_BUFFERED, FILE_ANY_ACCESS )

//
// request to disconnect
//
//#define IOCTL_OC_DISCONNECT_FROM_FILTER    CTL_CODE( FILE_DEVICE_OCFILTER, OCFILTER_IOCTL_INDEX + 1, METHOD_BUFFERED, FILE_ANY_ACCESS )

//------------------------------------------------------------

#ifdef __cplusplus
}
#endif//__cplusplus

#endif//_PNP_FILTER_CONTROL_H_
