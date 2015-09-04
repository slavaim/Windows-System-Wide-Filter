/*
  Author: Slava Imameev
  Copyright (c) 2007  , Slava Imameev
  All Rights Reserved.
*/

/*
 This file contains code for the load image notification.
 The user of this code must call OcCrInitializeImageNotifySubsystem before
 using any other function from this file and OcCrUninitializeImageNotifySubsystem
 when it's no longer needed in functions from this file, 
 if OcCrInitializeImageNotifySubsystem returns an error than no any
 function can be used because the initialization failed.
*/

#include "struct.h"
#include "proto.h"

//---------------------------------------------------

typedef struct _OC_LOAD_IMAGE_NOTIFY_HEADER{

    //
    // the full size of the buffer which this header describes
    //
    ULONG             Size;

    //
    // sent by the client, used to notify the client
    //
    HANDLE            NotifyClientEventHandle;

    //
    // sent by the client, used to notify the driver
    //
    HANDLE            NotifyDriverEventHandle;

    //
    // used to sent the image info to the client
    //
    IMAGE_INFO        ImageInfo;

    //
    // used to save the file name, the buffer with the name is saved in the
    // remainder of the buffer starting from the Name field
    //
    UNICODE_STRING    ImageName;
    WCHAR             Name[0x1];//variable size array
} OC_LOAD_IMAGE_NOTIFY_HEADER, *POC_LOAD_IMAGE_NOTIFY_HEADER;

//---------------------------------------------------

typedef struct _OC_REGISTRY_FOR_LOAD_IMAGE_NOTIFICATION{

    //
    // if Buffer is NULL the previous registration is dismissed
    //
    POC_LOAD_IMAGE_NOTIFY_HEADER    Buffer OPTIONAL;
    ULONG    BufferSize;
} OC_REGISTRY_FOR_LOAD_IMAGE_NOTIFICATION, *POC_REGISTRY_FOR_LOAD_IMAGE_NOTIFICATION;

//---------------------------------------------------

typedef struct _OC_LOAD_IMAGE_CLIENT_OBJECT{
    POC_LOAD_IMAGE_NOTIFY_HEADER    NotifyBufferSystemVA;
    PMDL                      NotifyBufferMDL;
    USHORT                    MaxBytesInName;
    PKEVENT                   NotifyClientEvent;
    PKEVENT                   NotifyDriverEvent;
} OC_LOAD_IMAGE_CLIENT_OBJECT, *POC_LOAD_IMAGE_CLIENT_OBJECT;

//---------------------------------------------------

typedef struct _OC_LOAD_IMAGE_GLOBAL_DATA{
    BOOLEAN       Initialized;
    KSPIN_LOCK    SpinLock;
    KEVENT        AllClientsWentEvent;
    POC_LOAD_IMAGE_CLIENT_OBJECT    CurrentClient;
    //
    // object type for load image notify objects
    //
    OC_OBJECT_TYPE    OcLoadImageNotifyObjectType;
    PLOAD_IMAGE_NOTIFY_ROUTINE    RegisteredNotifyRoutine;
} OC_LOAD_IMAGE_GLOBAL_DATA, *POC_LOAD_IMAGE_GLOBAL_DATA;

//---------------------------------------------------

VOID
NTAPI
OcCrDeleteLoadImageNotifyObject(
    IN PVOID    Object
    );

VOID
NTAPI
OcCrDeleteLoadImageNotifyObjectType(
    IN POC_OBJECT_TYPE    PtrObjectType
    );

VOID
OcCrLoadImageNotifyRoutine(
    IN PUNICODE_STRING FullImageName,
    IN HANDLE ProcessId,                // pid into which image is being mapped
    IN PIMAGE_INFO ImageInfo
    );

//---------------------------------------------------

static OC_LOAD_IMAGE_GLOBAL_DATA    GlobalLoadImageNotify;

//---------------------------------------------------

NTSTATUS
OcCrInitializeImageNotifySubsystem()
{
    NTSTATUS    RC;
    OC_OBJECT_TYPE_INITIALIZER_VAR( ObjectTypeInitializer );

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( FALSE == GlobalLoadImageNotify.Initialized );

    KeInitializeSpinLock( &GlobalLoadImageNotify.SpinLock );
    KeInitializeEvent( &GlobalLoadImageNotify.AllClientsWentEvent,
                       NotificationEvent,
                       FALSE );

    //
    // initialize the load image notify object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'iLcO';
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_LOAD_IMAGE_CLIENT_OBJECT );
    ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteLoadImageNotifyObject;
    ObjectTypeInitializer.Methods.DeleteObjectType = OcCrDeleteLoadImageNotifyObjectType;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &GlobalLoadImageNotify.OcLoadImageNotifyObjectType );

    GlobalLoadImageNotify.Initialized = TRUE;

    //
    // registry for load image notification
    //
    RC = PsSetLoadImageNotifyRoutine( OcCrLoadImageNotifyRoutine );
    if( NT_SUCCESS( RC ) ){

        GlobalLoadImageNotify.RegisteredNotifyRoutine = OcCrLoadImageNotifyRoutine;

    } else {

        GlobalLoadImageNotify.RegisteredNotifyRoutine = NULL;
        OcCrUninitializeImageNotifySubsystemIdempotent();
    }

    return RC;
}

//---------------------------------------------------

VOID
OcCrUninitializeImageNotifySubsystemIdempotent()
/*
    The function must have an idempotent behavior!
*/
{
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    if( FALSE == GlobalLoadImageNotify.Initialized )
        return;

    //
    // unregister our notification routine
    //
    if( NULL != GlobalLoadImageNotify.RegisteredNotifyRoutine )
        PsRemoveLoadImageNotifyRoutine( GlobalLoadImageNotify.RegisteredNotifyRoutine );

    //
    // delete the last client, no lock is needed because
    // no new requests are possible
    //
    if( NULL != GlobalLoadImageNotify.CurrentClient ){

        NTSTATUS    RC;

        RC = OcCrRegistryClientForNotification( NULL, 0x0, KernelMode );

        ASSERT( NT_SUCCESS( RC ) );
        ASSERT( NULL == GlobalLoadImageNotify.CurrentClient );
    }

    OcObDeleteObjectType( &GlobalLoadImageNotify.OcLoadImageNotifyObjectType );

    //
    // wait until all clients go
    //
    KeWaitForSingleObject( &GlobalLoadImageNotify.AllClientsWentEvent,
                           Executive,
                           KernelMode,
                           FALSE,
                           NULL );

    GlobalLoadImageNotify.Initialized = FALSE;
}

//---------------------------------------------------

NTSTATUS
OcCrRegistryClientForNotification(
    IN PVOID    Buffer,
    IN ULONG    BufferSize,
    IN MODE     PreviousMode//usually ExGetPreviousMode
    )
{
    NTSTATUS    RC;
    ULONG       MaxBytesInName;
    POC_LOAD_IMAGE_CLIENT_OBJECT    NotifyObject = NULL;

    ASSERT( UserMode == ExGetPreviousMode() );
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    if( FALSE == GlobalLoadImageNotify.Initialized )
        return STATUS_INVALID_DEVICE_REQUEST;

    if( NULL == Buffer ){

        //
        // remove the current client
        //
        RC = STATUS_SUCCESS;
        goto __exit;
    }

    if( BufferSize < sizeof( OC_LOAD_IMAGE_NOTIFY_HEADER ) )
        return STATUS_BUFFER_TOO_SMALL;

    //
    // create the object which will be used for notification
    //
    RC = OcObCreateObject( &GlobalLoadImageNotify.OcLoadImageNotifyObjectType,
                           &NotifyObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    RtlZeroMemory( NotifyObject, sizeof( *NotifyObject ) );

    //
    // Get the object pointer from the handle. Note we must be in the context
    // of the process that created the handle.
    //
    RC = ObReferenceObjectByHandle( ((POC_LOAD_IMAGE_NOTIFY_HEADER)Buffer)->NotifyClientEventHandle,
                                    SYNCHRONIZE,
                                    *ExEventObjectType,
                                    PreviousMode,
                                    &NotifyObject->NotifyClientEvent,
                                    NULL
                                   );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    RC = ObReferenceObjectByHandle( ((POC_LOAD_IMAGE_NOTIFY_HEADER)Buffer)->NotifyDriverEventHandle,
                                    SYNCHRONIZE,
                                    *ExEventObjectType,
                                    PreviousMode,
                                    &NotifyObject->NotifyDriverEvent,
                                    NULL
                                   );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // allocate and initalize an MDL that describes the buffer
    //
    NotifyObject->NotifyBufferMDL = IoAllocateMdl( Buffer,
                                                   BufferSize,
                                                   FALSE,
                                                   FALSE,
                                                   NULL);

    if( NULL == NotifyObject->NotifyBufferMDL ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    //
    // finish building the MDL - Fill in the "page portion" 
    //
    try{

        //
        // check the user buffer, this is some form of 
        // preliminary check, because user can asynchronously
        // change the VAD tree
        //
        if( UserMode == PreviousMode )
            ProbeForWrite( Buffer, BufferSize, 0x1 );

        //
        // lock the buffer
        //
        MmProbeAndLockPages( NotifyObject->NotifyBufferMDL,
                             PreviousMode,
                             IoWriteAccess );

    } except ( OcDummyExceptionFilter( GetExceptionInformation() ) ){

        //
        // free the allocatted MDL, this is done because there is 
        // no documented method to know whether the MDL is locked, 
        // the flags are not documented, so in cleanup code there 
        // is an assumption that if an MDL is present it has been locked
        //
        IoFreeMdl( NotifyObject->NotifyBufferMDL );
        NotifyObject->NotifyBufferMDL = NULL;

        RC = GetExceptionCode();
        goto __exit;
    }

    //
    // map to the system space and get the address
    //
    NotifyObject->NotifyBufferSystemVA = MmGetSystemAddressForMdlSafe( NotifyObject->NotifyBufferMDL, 
                                                                       NormalPagePriority );

    if( NULL == NotifyObject->NotifyBufferSystemVA ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    MaxBytesInName = BufferSize - FIELD_OFFSET( OC_LOAD_IMAGE_NOTIFY_HEADER, Name );
    NotifyObject->MaxBytesInName = ( MaxBytesInName>USHRT_MAX )? USHRT_MAX : (USHORT)MaxBytesInName;

__exit:

    if( !NT_SUCCESS( RC ) ){

        //
        // delete the partially created object
        //
        OcObDereferenceObject( NotifyObject );

    } else {

        //
        // NotifyObject may be NULL if this is a current client removal
        //
        KIRQL    OldIrql;
        POC_LOAD_IMAGE_CLIENT_OBJECT    OldNotifyObject;

        //
        // exchange the old object and the new one
        //
        KeAcquireSpinLock( &GlobalLoadImageNotify.SpinLock, &OldIrql );
        {//start of the lock
            OldNotifyObject = GlobalLoadImageNotify.CurrentClient;
            GlobalLoadImageNotify.CurrentClient = NotifyObject;
        }//end of the lock
        KeReleaseSpinLock( &GlobalLoadImageNotify.SpinLock, OldIrql );

        //
        // delete the old object
        //
        if( NULL != OldNotifyObject ){

            //
            // wake up waiting thread
            //
            KeSetEvent( OldNotifyObject->NotifyDriverEvent,
                        IO_DISK_INCREMENT,
                        FALSE );

            OcObDereferenceObject( OldNotifyObject );
        }
    }

    return RC;

}

//---------------------------------------------------

VOID
NTAPI
OcCrDeleteLoadImageNotifyObject(
    IN PVOID    Object
    )
    /*
    the object might be partially initialized!
    */
{
    POC_LOAD_IMAGE_CLIENT_OBJECT    PtrNotifyObject;

    PtrNotifyObject = (POC_LOAD_IMAGE_CLIENT_OBJECT)Object;

    ASSERT( NULL != PtrNotifyObject->NotifyBufferSystemVA );
    ASSERT( NULL != PtrNotifyObject->NotifyBufferMDL );

    if( NULL != PtrNotifyObject->NotifyBufferMDL ){

        if( NULL != PtrNotifyObject->NotifyBufferSystemVA )
            MmUnmapLockedPages( PtrNotifyObject->NotifyBufferSystemVA,
                                PtrNotifyObject->NotifyBufferMDL );

        MmUnlockPages( PtrNotifyObject->NotifyBufferMDL );

        IoFreeMdl( PtrNotifyObject->NotifyBufferMDL );
    }

    if( NULL != PtrNotifyObject->NotifyClientEvent )
        ObDereferenceObject( PtrNotifyObject->NotifyClientEvent );

    if( NULL != PtrNotifyObject->NotifyDriverEvent )
        ObDereferenceObject( PtrNotifyObject->NotifyDriverEvent );
}

//---------------------------------------------------

VOID
NTAPI
OcCrDeleteLoadImageNotifyObjectType(
    IN POC_OBJECT_TYPE    PtrObjectType
    )
{
    ASSERT( &GlobalLoadImageNotify.OcLoadImageNotifyObjectType == PtrObjectType );

    KeSetEvent( &GlobalLoadImageNotify.AllClientsWentEvent,
                IO_DISK_INCREMENT,
                FALSE );
}

//---------------------------------------------------

VOID
OcCrLoadImageNotifyRoutine(
    IN PUNICODE_STRING FullImageName,
    IN HANDLE ProcessId,// pid into which image is being mapped
    IN PIMAGE_INFO ImageInfo
    )
{
    KIRQL    OldIrql;
    POC_LOAD_IMAGE_CLIENT_OBJECT    PtrNotifyObject;
    USHORT   NameLength;

    ASSERT( TRUE == GlobalLoadImageNotify.Initialized );

    //
    // get the current registered client
    //
    KeAcquireSpinLock( &GlobalLoadImageNotify.SpinLock, &OldIrql );
    {//start of the lock
        PtrNotifyObject = GlobalLoadImageNotify.CurrentClient;
        if( NULL != PtrNotifyObject )
            OcObReferenceObject( PtrNotifyObject );
    }//end of the lock
    KeReleaseSpinLock( &GlobalLoadImageNotify.SpinLock, OldIrql );

    if( NULL == PtrNotifyObject )
        return;

    //
    // save the image info
    //
    PtrNotifyObject->NotifyBufferSystemVA->ImageInfo = *ImageInfo;

    //
    // copy the name
    //
    NameLength = min( FullImageName->Length, PtrNotifyObject->MaxBytesInName );
    PtrNotifyObject->NotifyBufferSystemVA->ImageName.Length = NameLength;
    PtrNotifyObject->NotifyBufferSystemVA->ImageName.MaximumLength = NameLength;
    RtlCopyMemory( PtrNotifyObject->NotifyBufferSystemVA->Name,
                   FullImageName->Buffer,
                   NameLength );

    //
    // notify the client
    //
    KeSetEvent( PtrNotifyObject->NotifyClientEvent, IO_DISK_INCREMENT, FALSE );

    //
    // wait for response
    //
    KeWaitForSingleObject( PtrNotifyObject->NotifyDriverEvent,
                           Executive,
                           KernelMode,
                           FALSE,
                           NULL );

    OcObDereferenceObject( PtrNotifyObject );
}

//---------------------------------------------------
