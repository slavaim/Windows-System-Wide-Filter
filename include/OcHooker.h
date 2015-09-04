/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
06.12.2006 ( December )
 Start
*/

#if !defined(_OC_HOOKER_H_)
#define _OC_HOOKER_H_

// {1DBA781F-8E1E-41f2-9763-0D8C006CB643} - generated using guidgen.exe
DEFINE_GUID(GUID_SD_HOOKER_CONTROL_OBJECT, 
0x1dba781f, 0x8e1e, 0x41f2, 0x97, 0x63, 0xd, 0x8c, 0x0, 0x6c, 0xb6, 0x43);


#ifdef __cplusplus
extern "C" {
#endif//__cplusplus

//------------------------------------------------------------------

//
// the name for the driver object and for the device that is used for communication
// between this driver and the core, i.e. Global.ControlDeviceObject
//
#define HOOKER_DRIVERNAME_FOR_OUT             "OcHookDriver: "
#define HOOKER_DRIVER_OBJECT_NAME             L"OcHookDriver"
#define HOOKER_CONTROL_OBJECT_NAME            L"OcHookControlDevice"
#define HOOKER_NTDEVICE_DIRECTORY             L"\\Device\\"
#define HOOKER_NTDOS_DEVICE_DIRECTORY         L"\\DosDevices\\"
#define HOOKER_NTDEVICE_NAME_STRING           L"\\Device\\"HOOKER_CONTROL_OBJECT_NAME
#define HOOKER_SYMBOLIC_NAME_STRING           L"\\DosDevices\\"HOOKER_CONTROL_OBJECT_NAME


//------------------------------------------------------------------

struct _OC_HOOKER_COMPLETION_CONTEXT;

typedef
NTSTATUS
( NTAPI *OcHookerHookDriverCallback )(
    IN PDRIVER_OBJECT DriverObject
    );

//
// called by the client of the hooker
// when it wants to disconnect
//
typedef
VOID
(NTAPI *OcHookerDisconnectCallback)(
    IN PVOID Context
    );

typedef
PDRIVER_DISPATCH
(NTAPI *OcHookerRetreiveOriginalDispatchCallback)(
    IN PDRIVER_OBJECT    HookedDriverObject,
    IN ULONG    MajorFunctionIndex
    );

typedef
NTSTATUS
(NTAPI *OcCrHookedDriverDispatchCallback)(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

typedef
BOOLEAN
(NTAPI *OcHoookerAcquireCookieForCompletionCallback)(
    IN struct _OC_HOOKER_COMPLETION_CONTEXT*     HookerCtx
    );

//------------------------------------------------------------------

#define OC_CURRENT_HOOKER_VERSION    ((ULONG)0x1)

typedef struct _OC_HOOKER_VERSION{
    ULONG    Version;
    ULONG    Size;
} OC_HOOKER_VERSION, *POC_HOOKER_VERSION;

typedef struct _OC_HOOKER_CALLBACK_METHODS_EXPORT{

    OC_HOOKER_VERSION               Version;

    //
    // called by the client of the hooker
    // when it wants to disconnect
    //
    OcHookerDisconnectCallback    Disconnect;

    //
    // called by the client to hook driver
    //
    OcHookerHookDriverCallback    HookDriver;

    //
    // called by the client to retrieve the original
    // dispatch function for a hooked driver
    //
    OcHookerRetreiveOriginalDispatchCallback    RetreiveOriginalDispatch;

    //
    // called by the client to acquire cookie when setting a completion hook
    // using the routine provided by the hooker( i.e. HokerCompletionRoutine )
    //
    OcHoookerAcquireCookieForCompletionCallback    AcquireCookieForCompletion;

    //
    // HokerCompletionRoutine is provided by the hooker for using 
    // in completion routines hooking, the context for this routine
    // is OC_HOOKER_COMPLETION_CONTEXT structure and it must 
    // be allocated and freed by a client
    //
    PIO_COMPLETION_ROUTINE    HookerCompletionRoutine;

    //
    // the cookie is returned to the caller by the hooker
    //
    ULONG_PTR    Cookie;

} OC_HOOKER_CALLBACK_METHODS_EXPORT, *POC_HOOKER_CALLBACK_METHODS_EXPORT;

typedef struct _OC_HOOKER_CALLBACKS_METHODS{
    //
    // called by the hooker for registered client
    //
    OcCrHookedDriverDispatchCallback    DriverDispatch;
} OC_HOOKER_CALLBACKS_METHODS, *POC_HOOKER_CALLBACKS_METHODS;

typedef struct _OC_HOOKER_CONNECT_INITIALIZER{

    OC_HOOKER_VERSION    Version;

    //
    // set in a signal state when it is safe to 
    // unload the code with callbacks
    //
    PKEVENT    PtrDisconnectEvent;

    //
    // Hooker initializes this structure
    //
    POC_HOOKER_CALLBACK_METHODS_EXPORT    DriverHookerExports;

    //
    // callbacks from internal source
    //
    OC_HOOKER_CALLBACKS_METHODS    CallbackMethods;

} OC_HOOKER_CONNECT_INITIALIZER, *POC_HOOKER_CONNECT_INITIALIZER;

//------------------------------------------------------------------

typedef struct _OC_HOOKER_COMPLETION_CONTEXT{

    //
    // set by the caller to a cookie returned by the hooker during the handshake
    //
    ULONG_PTR    HookerCookie;

    //
    // completion routine that will be called
    //
    PIO_COMPLETION_ROUTINE CompletionRoutine;

    //
    // context that will be provided for CompletionRoutine
    //
    PVOID    Context;

    //
    // the depth of the hook, used to catch an extremely long sequence of hooks
    // which usually indicate an error
    //
    ULONG    Depth;

} OC_HOOKER_COMPLETION_CONTEXT, *POC_HOOKER_COMPLETION_CONTEXT;

//------------------------------------------------------------------

#define FILE_DEVICE_OCHOOKER           0x00009999
#define OCHOOKER_IOCTL_INDEX           0x999

//
// IOCTL_OC_CONNECT_TO_HOOKER  - request a connection to the hooker.
// The input buffer contains an initialized OC_HOOKER_CONNECT_INITIALIZER
// structure.
//
#define IOCTL_OC_CONNECT_TO_HOOKER    CTL_CODE( FILE_DEVICE_OCHOOKER, OCHOOKER_IOCTL_INDEX + 0, METHOD_BUFFERED, FILE_ANY_ACCESS )

//------------------------------------------------------------------

#ifdef __cplusplus
}
#endif//__cplusplus

#endif// _OC_HOOKER_H_