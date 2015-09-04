/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/

#if !defined(_OC_HOOKER_STRUCT_H_)
#define _OC_HOOKER_STRUCT_H_

#define INITGUID

#include <ntddk.h>
#include <ocobject.h>
#include <octhreadpool.h>
#include <ochash.h>
#include <ocrwspinlock.h>
#include <OcHooker.h>

#define OC_HOOKER_BUG_CODES_BASE                 ( 0xAB400000 )
#define OC_HOOKER_BUG_UNINITIALIZED_CALLBACK     ( OC_HOOKER_BUG_CODES_BASE + 0x1 )
#define OC_HOOKER_BUG_ENTRY_NOT_FOUND_UNLOAD     ( OC_HOOKER_BUG_CODES_BASE + 0x2 )
#define OC_HOOKER_BUG_DELETED_ENTRY_FOUND        ( OC_HOOKER_BUG_CODES_BASE + 0x3 )
#define OC_HOOKER_BUG_NULL_FUNCTOR               ( OC_HOOKER_BUG_CODES_BASE + 0x4 )

//-----------------------------------------------------------

typedef struct _OC_HOOKER_CONNECT_OBJECT{

    OC_HOOKER_VERSION    Version;
    OC_HOOKER_VERSION    ClientVersion;

    //
    // set in a signal state when it is safe to 
    // unload the code with callbacks
    //
    PKEVENT    PtrDisconnectEvent;

    //
    // registered callbacks
    //
    OC_HOOKER_CALLBACKS_METHODS    CallbackMethods;

} OC_HOOKER_CONNECT_OBJECT, *POC_HOOKER_CONNECT_OBJECT;

//-----------------------------------------------------------

typedef struct _OC_HOOKER_GLOBAL{

    //
    // the driver object 
    //
    PDRIVER_OBJECT    DriverObject;

    //
    // the following device is used for communication
    // between the core and the hooker( this driver )
    //
    PDEVICE_OBJECT    ControlDeviceObject;

    //
    // object type for the hooked driver object
    //
    OC_OBJECT_TYPE    OcHookedDriverObjectType;

    //
    // object type for the connection object
    //
    OC_OBJECT_TYPE    ConnectObjectType;

    //
    // connection object
    //
    POC_HOOKER_CONNECT_OBJECT    PtrConnectionObject;

    //
    // read/write spin lock is used to protect the PtrConnectionObject field
    //
    OC_RW_SPIN_LOCK    RwSpinLock;

    //
    // hash of hooked drivers
    //
    POC_HASH_OBJECT    PtrDriverHashObject;

    //
    // resource that protects the hash from inserting
    // the duplicate entries, actually
    // only exclusive semantic is used, so it is
    // possible to elaborate a light version of the
    // lock that doesn't elevate the IRQL
    //
    ERESOURCE    DriverHashResource;

    PDRIVER_DISPATCH    InvalidRequestDispatchTable[ IRP_MJ_MAXIMUM_FUNCTION ];

} OC_HOOKER_GLOBAL, *POC_HOOKER_GLOBAL;

//-----------------------------------------------------------

typedef struct _OC_HOOKED_DRIVER_OBJECT{

    PDRIVER_OBJECT         PtrDriverObject;
    PDRIVER_DISPATCH       PtrOriginalFunctions[IRP_MJ_MAXIMUM_FUNCTION + 1];
    PVOID                  PtrOriginalDriverStart;
    PDRIVER_UNLOAD         PtrOriginalDriverUnload;
    PDRIVER_ADD_DEVICE     PtrOriginalAddDevice;
    PDRIVER_DISPATCH       PtrHookerFunction;

    //
    // This event is set to signal state when the
    // object reference count drops to zero. The
    // event is used to synchronize DriverUnload
    // with other routine, i.e. the driver
    // unload will wait on this event and the
    // system's DriverObject will be valid and
    // can be investigated in other routines
    //
    PKEVENT                 PtrUnloadEvent;

} OC_HOOKED_DRIVER_OBJECT, *POC_HOOKED_DRIVER_OBJECT;

//-----------------------------------------------------------

#endif//_OC_HOOKER_STRUCT_H_