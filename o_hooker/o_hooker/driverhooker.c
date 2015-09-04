/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
05.12.2006 ( December )
 Start
*/

#include "struct.h"
#include "proto.h"
#include <OcHooker.h>

//------------------------------------------------------------

#define OC_HOOKER_IO_INVALID_DEVICE_REQUEST   ( Global.DriverObject->MajorFunction[IRP_MJ_CREATE_NAMED_PIPE] )

//------------------------------------------------------------

static
VOID 
OcHookerDriverUnloadHook(
    IN PDRIVER_OBJECT DriverObject
    );

static
NTSTATUS
NTAPI
OcHoookerDispatchFunction(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    );

//------------------------------------------------------------

NTSTATUS
NTAPI 
OcHookerHookDriver(
    IN PDRIVER_OBJECT    DriverObject
    )
{
    NTSTATUS                    RC;
    POC_HOOKED_DRIVER_OBJECT    PtrOcDriverObject = NULL;
    BOOLEAN                     ResourceAcquired = FALSE;
    ULONG                       i;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // synchronize adding in the hash
    //
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite( &Global.DriverHashResource, TRUE );
    ResourceAcquired = TRUE;

    //
    // first try to find driver in the hash
    //
    PtrOcDriverObject = OcHsFindContextByKeyValue( Global.PtrDriverHashObject,
                                                   (ULONG_PTR)DriverObject,
                                                   OcObReferenceObject );
    if( NULL != PtrOcDriverObject ){

        RC = STATUS_SUCCESS;
        goto __exit;
    }

    //
    // create new device object
    //
    RC = OcObCreateObject( &Global.OcHookedDriverObjectType,
                           &PtrOcDriverObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    RtlZeroMemory( PtrOcDriverObject, sizeof( *PtrOcDriverObject ) );

    //
    // insert the device object in the hash, the key is 
    // OS kernel's device object
    //
    RC = OcHsInsertContextInHash( Global.PtrDriverHashObject,
                                  (ULONG_PTR)DriverObject,
                                  (PVOID)PtrOcDriverObject,
                                  OcObReferenceObject );
    if( !NT_SUCCESS( RC ) ){
        goto __exit;
    }

    //
    // hook the driver and initialize the driver object
    //
    PtrOcDriverObject->PtrDriverObject = DriverObject;
    PtrOcDriverObject->PtrHookerFunction = OcHoookerDispatchFunction;
    if( DriverObject->DriverUnload ){

        PtrOcDriverObject->PtrOriginalDriverUnload = DriverObject->DriverUnload;
        DriverObject->DriverUnload = OcHookerDriverUnloadHook;
    }

    for( i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++ ){

        //
        // do not hook power requests, because driver verifier wrongly concludes
        // that the hooker completed an iRP instead sending it down when the driver
        // hooked by the hooker has completed a power IRP
        //
        if( IRP_MJ_POWER == i ){

            PtrOcDriverObject->PtrOriginalFunctions[i] = DriverObject->MajorFunction[i];
            continue;
        }

        //
        // the function is hooked if it is not NULL( actually this is
        // impossible for well behaived drivers ) and is not equal to
        // IoInvalidDeviceRequest
        //
        if( NULL != DriverObject->MajorFunction[i] && 
            OC_HOOKER_IO_INVALID_DEVICE_REQUEST != DriverObject->MajorFunction[i]){

            //
            // save the original function
            //
            PtrOcDriverObject->PtrOriginalFunctions[i] = DriverObject->MajorFunction[i];

            //
            // hook the function
            //
            InterlockedExchangePointer( (PVOID*)&DriverObject->MajorFunction[i], (PVOID)PtrOcDriverObject->PtrHookerFunction );
        }
    }

__exit:

    if( ResourceAcquired ){

        ExReleaseResourceLite( &Global.DriverHashResource );
        KeLeaveCriticalRegion();
        ResourceAcquired = FALSE;
    }

    if( !NT_SUCCESS( RC ) ){

        //
        // something went wrong
        //
    }

    if( NULL != PtrOcDriverObject )
        OcObDereferenceObject( PtrOcDriverObject );

    return RC;
}

//------------------------------------------------------------

VOID 
OcHookerDriverUnloadHook(
    IN PDRIVER_OBJECT DriverObject
    )
{
    POC_HOOKED_DRIVER_OBJECT    PtrOcDriverObject = NULL;
    KEVENT                      UnloadEvent;

    KeInitializeEvent( &UnloadEvent, NotificationEvent, FALSE );

    PtrOcDriverObject = OcHsFindContextByKeyValue( Global.PtrDriverHashObject,
                                                   (ULONG_PTR)DriverObject,
                                                   OcObReferenceObject );
    if( NULL == PtrOcDriverObject ){

        ASSERT( " OcHooker: entry for hooked driver is not found!" );

#if DBG
        KeBugCheckEx( OC_HOOKER_BUG_ENTRY_NOT_FOUND_UNLOAD, 
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)DriverObject, 
                      (ULONG_PTR)&Global, 
                      (ULONG_PTR)NULL );
#endif//DBG
        return;
    }

    //
    // call the original DriverUnload
    //
    PtrOcDriverObject->PtrOriginalDriverUnload( DriverObject );

    //
    // set unload event
    //
    PtrOcDriverObject->PtrUnloadEvent = &UnloadEvent;

    //
    // dereference the entry referenced in OcHsFindContextByKeyValue
    //
    OcObDereferenceObject( PtrOcDriverObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrOcDriverObject );

    //
    // delete the driver entry from the hash
    //
    OcHsRemoveContextByKeyValue( Global.PtrDriverHashObject,
                                 (ULONG_PTR)DriverObject,
                                 OcObDereferenceObject );

#if DBG
    if( NULL != OcHsFindContextByKeyValue( Global.PtrDriverHashObject,
                                           (ULONG_PTR)DriverObject,
                                           NULL ) ){

        KeBugCheckEx( OC_HOOKER_BUG_DELETED_ENTRY_FOUND, 
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)DriverObject, 
                      (ULONG_PTR)&Global, 
                      (ULONG_PTR)NULL );
    }
#endif//DBG

    //
    // wait until last reference to this entry goes
    //
    KeWaitForSingleObject( &UnloadEvent,
                           Executive,
                           KernelMode,
                           FALSE,
                           NULL );

}

//------------------------------------------------------------

__forceinline
PDRIVER_DISPATCH*
NTAPI
OcHookerRetreiveOriginalDispatchTable(
    IN PDRIVER_OBJECT    HookedDriverObject
    )
    /*
    TO DO - a cache for fast search
    */
{
    PDRIVER_DISPATCH*           OriginalDriverDispatchTable;
    POC_HOOKED_DRIVER_OBJECT    PtrOcDriverObject;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    PtrOcDriverObject = OcHsFindContextByKeyValue( Global.PtrDriverHashObject,
                                                   (ULONG_PTR)HookedDriverObject,
                                                   OcObReferenceObject );
    if( NULL == PtrOcDriverObject ){

        ASSERT( " OcHooker: entry for hooked driver is not found!" );

#if DBG
        KeBugCheckEx( OC_HOOKER_BUG_ENTRY_NOT_FOUND_UNLOAD, 
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)HookedDriverObject, 
                      (ULONG_PTR)&Global, 
                      (ULONG_PTR)0x0 );
#endif//DBG

        OriginalDriverDispatchTable = Global.InvalidRequestDispatchTable;
        goto __exit;
    }

    OriginalDriverDispatchTable = PtrOcDriverObject->PtrOriginalFunctions;

    OcObDereferenceObject( PtrOcDriverObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrOcDriverObject );

__exit:

    ASSERT( NULL != OriginalDriverDispatchTable );

    return OriginalDriverDispatchTable;
}

//------------------------------------------------------------

PDRIVER_DISPATCH
NTAPI
OcHookerRetreiveOriginalDispatch(
    IN PDRIVER_OBJECT    HookedDriverObject,
    IN ULONG    MajorFunctionIndex
    )
{
    PDRIVER_DISPATCH    OriginalDriverDispatch;
    PDRIVER_DISPATCH*   OriginalDriverDispatchTable;

    ASSERT( MajorFunctionIndex <= IRP_MJ_MAXIMUM_FUNCTION );
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    OriginalDriverDispatchTable = OcHookerRetreiveOriginalDispatchTable( HookedDriverObject );
    OriginalDriverDispatch = OriginalDriverDispatchTable[ MajorFunctionIndex ];

    ASSERT( NULL != OriginalDriverDispatch );

    if( NULL == OriginalDriverDispatch )
        OriginalDriverDispatch = OcHookerInvalidDeviceRequest;

    return OriginalDriverDispatch;
}

//------------------------------------------------------------

static
NTSTATUS
NTAPI
OcHoookerDispatchFunction(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    )
{
    POC_HOOKER_CONNECT_OBJECT    PtrConnectionObject;
    NTSTATUS    RC;

    //
    // getv the referenced connection object
    //
    PtrConnectionObject = OcHookerReferenceCurrentConnectObject();
    if( NULL == PtrConnectionObject ){

        PDRIVER_DISPATCH    OriginalDriverDispatch;

        OriginalDriverDispatch = OcHookerRetreiveOriginalDispatch( 
                                   DeviceObject->DriverObject,
                                   IoGetCurrentIrpStackLocation( Irp )->MajorFunction 
                                   );

        ASSERT( NULL != OriginalDriverDispatch );
        if( NULL == OriginalDriverDispatch ){

#if DBG
            KeBugCheckEx( OC_HOOKER_BUG_NULL_FUNCTOR,
                          (ULONG_PTR)__LINE__,
                          (ULONG_PTR)DeviceObject,
                          (ULONG_PTR)Irp,
                          (ULONG_PTR)NULL );
#endif//DBG
            Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
            IoCompleteRequest( Irp, IO_DISK_INCREMENT );
            return STATUS_INVALID_DEVICE_REQUEST;
        }

        return OriginalDriverDispatch( DeviceObject, Irp );
    }

    RC = PtrConnectionObject->CallbackMethods.DriverDispatch( DeviceObject,
                                                              Irp );

    //
    // dereference the connection object refernced in OcHookerReferenceCurrentConnectObject
    //
    OcObDereferenceObject( PtrConnectionObject );

    return RC;
}

//------------------------------------------------------------

BOOLEAN
NTAPI
OcHoookerAcquireCookieForCompletionRoutine(
    IN POC_HOOKER_COMPLETION_CONTEXT     HookerCtx
    )
{
    KIRQL                        OldIrql;
    POC_HOOKER_CONNECT_OBJECT    PtrConnectionObject = NULL;
    ULONG_PTR                    Cookie = HookerCtx->HookerCookie;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( HookerCtx->CompletionRoutine );
    ASSERT( HookerCtx->Context );

    OcRwAcquireLockForRead( &Global.RwSpinLock, &OldIrql );
    {//start of the lock

        if( Cookie == (ULONG_PTR)Global.PtrConnectionObject ){

            PtrConnectionObject = Global.PtrConnectionObject;
            OcObReferenceObject( PtrConnectionObject );
        }

    }//end of the lock
    OcRwReleaseReadLock( &Global.RwSpinLock, OldIrql );

    return (BOOLEAN)( PtrConnectionObject != NULL );
}

//------------------------------------------------------------

NTSTATUS
OcHookerCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context//POC_HOOKER_COMPLETION_CONTEXT
    )
{
    NTSTATUS    RC;
    POC_HOOKER_COMPLETION_CONTEXT     HookerCtx = (POC_HOOKER_COMPLETION_CONTEXT)Context;
    POC_HOOKER_CONNECT_OBJECT    PtrConnectionObject = (POC_HOOKER_CONNECT_OBJECT)HookerCtx->HookerCookie;

    RC = HookerCtx->CompletionRoutine( DeviceObject,
                                       Irp,
                                       HookerCtx->Context );
    //
    // after calling a completion routine the Header might be invalid
    // because might have been freed inside the completion routine
    //
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( HookerCtx );

    //
    // derefrence object referenced in OcHoookerAcquireCookieForCompletionRoutine
    //
    OcObDereferenceObject( PtrConnectionObject );

    return RC;
}

//------------------------------------------------------------

