/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
23.11.2006 
 Start
*/

/*
This file contains global data and common functions
*/

#include "struct.h"
#include "proto.h"
#include <OcHooker.h>

//------------------------------------------------------------------------

NTSTATUS
OcHookerConnectionRequired( 
    IN POC_HOOKER_CONNECT_INITIALIZER    PtrInputInitializer
    );

VOID
NTAPI
OcHookerDisconnect(
    IN PVOID    Context
    );

//------------------------------------------------------------------------

//
// global data
//
OC_HOOKER_GLOBAL    Global;

//------------------------------------------------------------------------

NTSTATUS
OcHookerDispatchFunction(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP              Irp
    )
{
    PIO_STACK_LOCATION           irpStack;
    NTSTATUS                     RC = STATUS_SUCCESS;

    irpStack = IoGetCurrentIrpStackLocation (Irp);

    Irp->IoStatus.Information = 0x0;

    switch (irpStack->MajorFunction) {
            case IRP_MJ_CREATE:
                DebugPrint(("Create \n"));
                break;

            case IRP_MJ_CLOSE:
                DebugPrint(("Close \n"));
                break;

            case IRP_MJ_CLEANUP:
                DebugPrint(("Cleanup \n"));
                break;

            case IRP_MJ_DEVICE_CONTROL:
                RC = STATUS_INVALID_DEVICE_REQUEST;
                break;

            case IRP_MJ_INTERNAL_DEVICE_CONTROL:
                DebugPrint(("DeviceIoControl\n"));
                switch( irpStack->Parameters.DeviceIoControl.IoControlCode ){
                case IOCTL_OC_CONNECT_TO_HOOKER:{

                    PVOID    InputBuffer = Irp->AssociatedIrp.SystemBuffer;
                    ULONG    InputBufferLength  = irpStack->Parameters.DeviceIoControl.InputBufferLength;

                    if( InputBufferLength < sizeof( OC_HOOKER_CONNECT_INITIALIZER ) ){

                        RC = STATUS_INVALID_BUFFER_SIZE;
                        break;
                    }
                    RC = OcHookerConnectionRequired( (POC_HOOKER_CONNECT_INITIALIZER)InputBuffer );
                    break;
                }//case IOCTL_OC_CONNECT_TO_HOOKER
                default:
                    RC = STATUS_INVALID_PARAMETER;
                    break;
                }//switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
                break;
            default:
                RC = STATUS_INVALID_DEVICE_REQUEST;
                break;
    }

    //
    // complete the request
    //
    ASSERT( 0x0 == Irp->IoStatus.Information );
    Irp->IoStatus.Status = RC;
    IoCompleteRequest( Irp, IO_DISK_INCREMENT );

    return RC;
}

//------------------------------------------------------------------------

NTSTATUS
OcHookerConnectionRequired( 
    IN POC_HOOKER_CONNECT_INITIALIZER    PtrInputInitializer
    )
{
    NTSTATUS    RC;
    POC_HOOKER_CONNECT_OBJECT    PtrConnectObject = NULL;
    KIRQL       OldIrql;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    if( NULL != Global.PtrConnectionObject ){
        //
        // somebidy has already connected to this hooker
        //
        return STATUS_TOO_MANY_OPENED_FILES;
    }

    ASSERT( !( PtrInputInitializer->Version.Version < OC_CURRENT_HOOKER_VERSION || 
               PtrInputInitializer->DriverHookerExports->Version.Version < OC_CURRENT_HOOKER_VERSION ) );

    if( PtrInputInitializer->Version.Version < OC_CURRENT_HOOKER_VERSION || 
        PtrInputInitializer->DriverHookerExports->Version.Version < OC_CURRENT_HOOKER_VERSION )
        return STATUS_INVALID_DEVICE_REQUEST;

    ASSERT( !( NULL == PtrInputInitializer->CallbackMethods.DriverDispatch ) );

    if( NULL == PtrInputInitializer->CallbackMethods.DriverDispatch )
        return STATUS_INVALID_PARAMETER;

    RC = OcObCreateObject( &Global.ConnectObjectType,
                           &PtrConnectObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // initialize the object's body
    //
    {
        RtlZeroMemory( PtrConnectObject, sizeof( *PtrConnectObject ) );

        PtrConnectObject->Version.Version = OC_CURRENT_HOOKER_VERSION;
        PtrConnectObject->Version.Size = sizeof( *PtrConnectObject );
        PtrConnectObject->ClientVersion.Version = PtrInputInitializer->Version.Version;
        PtrConnectObject->ClientVersion.Size = PtrConnectObject->Version.Size;

        PtrConnectObject->PtrDisconnectEvent = PtrInputInitializer->PtrDisconnectEvent;

        PtrConnectObject->CallbackMethods.DriverDispatch = PtrInputInitializer->CallbackMethods.DriverDispatch;
    }

#if DBG
    {
        ULONG i;
        for( i = 0x0; i < sizeof(PtrConnectObject->CallbackMethods)/sizeof(ULONG_PTR); ++i ){

            if( ((ULONG_PTR)0x0) == ((PULONG_PTR)&PtrConnectObject->CallbackMethods)[i] ){

                KeBugCheckEx( OC_HOOKER_BUG_UNINITIALIZED_CALLBACK, 
                              (ULONG_PTR)__LINE__,
                              (ULONG_PTR)i, 
                              (ULONG_PTR)PtrConnectObject, 
                              (ULONG_PTR)PtrInputInitializer );
            }
        }//for
    }
#endif//DBG

    //
    // initialize this hooker's exports
    //
    PtrInputInitializer->DriverHookerExports->Disconnect = OcHookerDisconnect;
    PtrInputInitializer->DriverHookerExports->RetreiveOriginalDispatch = OcHookerRetreiveOriginalDispatch;
    PtrInputInitializer->DriverHookerExports->HookDriver = OcHookerHookDriver;
    PtrInputInitializer->DriverHookerExports->AcquireCookieForCompletion = OcHoookerAcquireCookieForCompletionRoutine;
    PtrInputInitializer->DriverHookerExports->HookerCompletionRoutine = OcHookerCompletionRoutine;
    PtrInputInitializer->DriverHookerExports->Cookie = (ULONG_PTR)PtrConnectObject;

    OcRwAcquireLockForWrite( &Global.RwSpinLock, &OldIrql );
    {//start of the lock
        if( NULL == Global.PtrConnectionObject )
            Global.PtrConnectionObject = PtrConnectObject;
        else
            RC = STATUS_TOO_MANY_OPENED_FILES;
    }//end of the lock
    OcRwReleaseWriteLock( &Global.RwSpinLock, OldIrql );

    if( !NT_SUCCESS( RC ) ){

        if( NULL != PtrConnectObject )
            OcObDereferenceObject( PtrConnectObject );

    }

    return RC;
}

//------------------------------------------------------------------------

VOID
NTAPI
OcHookerDisconnect(
    IN PVOID    Context
    )
{
    KIRQL    OldIrql;
    POC_HOOKER_CONNECT_OBJECT    PtrConnectionObject;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    OcRwAcquireLockForWrite( &Global.RwSpinLock, &OldIrql );
    {//start of the lock
        PtrConnectionObject = Global.PtrConnectionObject;
        Global.PtrConnectionObject = NULL;
    }//end of the lock
    OcRwReleaseWriteLock( &Global.RwSpinLock, OldIrql );

    if( NULL != PtrConnectionObject )
        OcObDereferenceObject( PtrConnectionObject );

    return;
}

//------------------------------------------------------------------------

VOID
NTAPI
OcFilterDeleteConnectObject(
    IN POC_HOOKER_CONNECT_OBJECT    PtrConnectObject
    )
{
    KeSetEvent( PtrConnectObject->PtrDisconnectEvent,
                IO_DISK_INCREMENT,
                FALSE );
}

//------------------------------------------------------------------------

VOID
NTAPI 
OcHookerDeleteDriverObject(
    IN POC_HOOKED_DRIVER_OBJECT    PtrOcDriverObject
    )
{
    if( NULL != PtrOcDriverObject->PtrUnloadEvent ){

        KeSetEvent( PtrOcDriverObject->PtrUnloadEvent,
                    IO_DISK_INCREMENT,
                    FALSE );
    }
}

//------------------------------------------------------------------------

POC_HOOKER_CONNECT_OBJECT
OcHookerReferenceCurrentConnectObject()
    /*
    function returns the refrenced connection object
    the caller must dereference it when it is not needed
    */
{
    KIRQL    OldIrql;
    POC_HOOKER_CONNECT_OBJECT    PtrConnectionObject;

    OcRwAcquireLockForRead( &Global.RwSpinLock, &OldIrql );
    {//start of the lock
        PtrConnectionObject = Global.PtrConnectionObject;
        if( NULL != PtrConnectionObject )
            OcObReferenceObject( PtrConnectionObject );
    }//end of the lock
    OcRwReleaseReadLock( &Global.RwSpinLock, OldIrql );

    return PtrConnectionObject;
}

//------------------------------------------------------------------------

NTSTATUS
NTAPI
OcHookerInvalidDeviceRequest(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    )
{
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    IoCompleteRequest( Irp, IO_DISK_INCREMENT );
    return STATUS_INVALID_DEVICE_REQUEST;
}

//------------------------------------------------------------------------
