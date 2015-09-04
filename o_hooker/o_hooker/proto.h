/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/
#ifndef _OC_HOOKER_PROTO_H_
#define _OC_HOOKER_PROTO_H_

#include "struct.h"

#if DBG
#define DebugPrint(_x_) \
               DbgPrint (" OcHooker:"); \
               DbgPrint _x_;

#define TRAP() DbgBreakPoint()

#else
#define DebugPrint(_x_)
#define TRAP()
#endif


///////////////////////////////////////////////////////
//
// init.c
//
///////////////////////////////////////////////////////


///////////////////////////////////////////////////////
//
// common.c
//
///////////////////////////////////////////////////////

extern
NTSTATUS
OcHookerDispatchFunction(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP              Irp
    );

extern
VOID
NTAPI
OcFilterDeleteConnectObject(
    IN POC_HOOKER_CONNECT_OBJECT    PtrConnectObject
    );

extern
VOID
NTAPI 
OcHookerDeleteDriverObject(
    IN POC_HOOKED_DRIVER_OBJECT    PtrOcDriverObject
    );

extern
POC_HOOKER_CONNECT_OBJECT
OcHookerReferenceCurrentConnectObject();

extern
NTSTATUS
NTAPI
OcHookerInvalidDeviceRequest(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

extern OC_HOOKER_GLOBAL    Global;

////////////////////////////////////////////////////////////
//
// driverhooker.c
//
////////////////////////////////////////////////////////////

extern
PDRIVER_DISPATCH
NTAPI
OcHookerRetreiveOriginalDispatch(
    IN PDRIVER_OBJECT    HookedDriverObject,
    IN ULONG    MajorFunctionIndex
    );

extern
NTSTATUS
NTAPI 
OcHookerHookDriver(
    IN PDRIVER_OBJECT    DriverObject
    );

extern
BOOLEAN
NTAPI
OcHoookerAcquireCookieForCompletionRoutine(
    IN POC_HOOKER_COMPLETION_CONTEXT    Header
    );

extern
NTSTATUS
OcHookerCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context//POC_HOOKER_COMPLETION_CONTEXT
    );

#endif//_OC_HOOKER_PROTO_H_