/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
23.11.2006 
 Start
*/

/*
This file contains global data and common functions
*/

#include "struct.h"
#include "proto.h"

//-------------------------------------------------------

//
// global data
//
OC_GLOBAL    Global;

//-------------------------------------------------------

NTSTATUS
NTAPI
OcCrHookedDriverDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    )
{
    PDRIVER_DISPATCH      OriginalDriverDispatch;
    PIO_STACK_LOCATION    PtrIrpStackLocation;

    PtrIrpStackLocation = IoGetCurrentIrpStackLocation( Irp );

    switch( PtrIrpStackLocation->MinorFunction ){
        case IRP_MJ_PNP:
            OcCrProcessPnPRequestForHookedDriver( DeviceObject,
                                                  Irp );
            break;
        default:
            return OcCrProcessIoRequestFromHookedDriver( DeviceObject,
                                                       Irp,
                                                       PtrIrpStackLocation );
    }

    OriginalDriverDispatch = Global.DriverHookerExports.RetreiveOriginalDispatch(
                                DeviceObject->DriverObject,
                                PtrIrpStackLocation->MajorFunction );

    return OriginalDriverDispatch( DeviceObject, Irp );
}

//-------------------------------------------------------

NTSTATUS
NTAPI
OcCoreCreateDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    )
{
    NTSTATUS    RC = STATUS_SUCCESS;

    ASSERT( DeviceObject->DriverObject == Global.DriverObject );

    //
    //  Acquire the remove lock to avoid premature driver uninitialization,
    // the lock must be released when the file object is closed.
    //  Actually the kernel's IO Manager retains device objects and the driver 
    // object through the DeviceObject->ReferenceCount, but for uniformity 
    // the lock is acquired in create and released in close requests.
    //
    RC = OcRlAcquireRemoveLock( &Global.RemoveLock.Common );
    if( !NT_SUCCESS( RC ) ){

        Irp->IoStatus.Status = RC;
        Irp->IoStatus.Information = 0x0;

        IoCompleteRequest( Irp, IO_NO_INCREMENT );

        return RC;
    }

    Irp->IoStatus.Status = RC;
    Irp->IoStatus.Information = 0x0;

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    if( !NT_SUCCESS( RC ) )
        OcRlReleaseRemoveLock( &Global.RemoveLock.Common );

    return RC;
}

//-------------------------------------------------------

NTSTATUS
NTAPI
OcCoreCleanupDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    )
{
    NTSTATUS    RC = STATUS_SUCCESS;

    ASSERT( DeviceObject->DriverObject == Global.DriverObject );

    Irp->IoStatus.Status = RC;
    Irp->IoStatus.Information = 0x0;

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    return RC;
}

//-------------------------------------------------------

NTSTATUS
NTAPI
OcCoreCloseDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    )
{
    NTSTATUS    RC = STATUS_SUCCESS;

    ASSERT( DeviceObject->DriverObject == Global.DriverObject );

    Irp->IoStatus.Status = RC;
    Irp->IoStatus.Information = 0x0;

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    OcRlReleaseRemoveLock( &Global.RemoveLock.Common );

    return RC;
}

//-------------------------------------------------------

NTSTATUS
NTAPI
OcCoreDeviceControlDispatch(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP    Irp
    )
{
    NTSTATUS                      RC = STATUS_NOT_SUPPORTED;
    PIO_STACK_LOCATION            PtrIrpStack;
    PVOID                         PtrInputBuffer;
    PVOID                         PtrOutputBuffer;
    ULONG                         InputBufferLength;
    ULONG                         OutputBufferLength;
    ULONG                         IoControlCode;

    UNREFERENCED_PARAMETER( DeviceObject );

    PtrIrpStack = IoGetCurrentIrpStackLocation( Irp );

    //
    // set the default completion parameters
    //
    Irp->IoStatus.Information = 0x0;
    Irp->IoStatus.Status = RC;

    //
    // Get the pointer to the input/output buffer and it's length
    // for buffered I/O
    //
    PtrInputBuffer        = Irp->AssociatedIrp.SystemBuffer;
    InputBufferLength  = PtrIrpStack->Parameters.DeviceIoControl.InputBufferLength;
    PtrOutputBuffer       = Irp->AssociatedIrp.SystemBuffer;
    OutputBufferLength = PtrIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    IoControlCode         = PtrIrpStack->Parameters.DeviceIoControl.IoControlCode;

    switch( IoControlCode ){

        case IOCTL_DUMP_DEVICE_DATABASE:
            {
                UNICODE_STRING    FileName;

                RtlInitUnicodeString( &FileName, L"\\??\\C:\\devdatabasedump.txt" );
                RC = OcCrWriteDeviceDataBaseInFile( &FileName );
                Irp->IoStatus.Status = RC;
                break;
            }

        case IOCTL_QUERY_UNLOAD:
            {
                OcRemoveCoreControlDeviceObjectIdempotent();
                OcCrProcessQueryUnloadRequestIdempotent( FALSE );
                RC = STATUS_SUCCESS;
                Irp->IoStatus.Status = RC;
            }
            break;

        case IOCTL_CONNECT_DLDRIVER:
            {
                POCORE_TO_DLDRIVER    ConnectStruct;
            //
            // this is the only IOCTL with the method neither used
            // for he buffer passing!
            //

            //
            // the caller must be in the kernel mode
            //
            if( KernelMode != Irp->RequestorMode ){

                RC = STATUS_INVALID_DEVICE_REQUEST;
                Irp->IoStatus.Status = RC;
                break;
            }

            if( InputBufferLength < sizeof( *ConnectStruct ) ){

                RC = STATUS_BUFFER_TOO_SMALL;
                Irp->IoStatus.Status = RC;
                break;
            }

            ConnectStruct = (POCORE_TO_DLDRIVER)(IoGetCurrentIrpStackLocation( Irp ))->Parameters.DeviceIoControl.Type3InputBuffer;

            ASSERT( NULL != ConnectStruct );

            RC = OcEstablishDlDriverConnection( ConnectStruct );
            Irp->IoStatus.Status = RC;
            }
            break;
    }

    ASSERT( RC == Irp->IoStatus.Status );

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    return RC;
}

//-------------------------------------------------------

long 
OcDummyExceptionFilter(
    IN PEXCEPTION_POINTERS  PtrExceptionPointers 
    )
    /*
    Description:
        Filter for exceptions

    Expected Interrupt Level (for execution) :
        Any

    Return Value: EXCEPTION_EXECUTE_HANDLER
    */
{

    UNREFERENCED_PARAMETER( PtrExceptionPointers );

#if DBG
    ASSERT( !"Exception! Investigate immediatelly!" );
#endif

    return EXCEPTION_EXECUTE_HANDLER;
}

//-------------------------------------------------------

long 
OcDummyExceptionFilterEx(
    IN PEXCEPTION_POINTERS    PtrExceptionPointers,
    IN NTSTATUS    ExceptionCode
    )
    /*
    Description:
        Filter for exceptions

    Expected Interrupt Level (for execution) :
        Any

    Return Value: EXCEPTION_EXECUTE_HANDLER
    */
{

    UNREFERENCED_PARAMETER( ExceptionCode );

#if DBG
    ASSERT( !"Exception! Investigate immediatelly!" );
#endif

    return OcDummyExceptionFilter( PtrExceptionPointers );
}

//------------------------------------------------------------

NTSTATUS
OcCrGetValueFromKey(
    IN HANDLE    KeyHandle,
    IN PUNICODE_STRING    ValueName,
    IN KEY_VALUE_INFORMATION_CLASS   ValueInformationClass,
    OUT PVOID*    KeyValueInfo,
    OUT PULONG    InfoBufferLength
    )
    /*++
    the caller must free the (*KeyValueInfo) buffer
    by calling OcCrFreeValueFromKey,
    the returned buffer is in the PAGED POOL!
    --*/
{
    NTSTATUS         RC = STATUS_SUCCESS;
    PVOID            Buffer = NULL;
    ULONG            BufferSize;
    BOOLEAN          StringTerminatorLength =  2*sizeof( WCHAR );

    //
    // In this function I always
    // allocate buffers with the additional 4 bytes
    // but do not report about this four bytes
    // to the ZwQueryValueKey, I use this four
    // bytes to set string terminator i.e. L'\0'.
    //

    BufferSize = sizeof( KEY_VALUE_PARTIAL_INFORMATION );
    Buffer = ExAllocatePool( PagedPool, BufferSize + StringTerminatorLength );
    if( NULL == Buffer ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    RC = ZwQueryValueKey( KeyHandle,
                          ValueName,
                          ValueInformationClass,
                          Buffer,
                          BufferSize,
                          &BufferSize );

    if( !NT_SUCCESS( RC ) && 
        RC != STATUS_BUFFER_TOO_SMALL && 
        RC != STATUS_BUFFER_OVERFLOW )
        goto __exit;

    if( STATUS_BUFFER_TOO_SMALL == RC || STATUS_BUFFER_OVERFLOW == RC ){

        ExFreePool( Buffer ); 

        Buffer = ExAllocatePoolWithTag( PagedPool,
                                        BufferSize + StringTerminatorLength,
                                        'VRcO' );
        if( NULL == Buffer ){

            RC = STATUS_INSUFFICIENT_RESOURCES;
            goto __exit;
        }

        RC = ZwQueryValueKey( KeyHandle, 
                              ValueName, 
                              ValueInformationClass, 
                              Buffer,
                              BufferSize,
                              &BufferSize );
    }

    if( !NT_SUCCESS( RC ) )
        goto __exit;

    ASSERT( StringTerminatorLength >= ( 0x2*sizeof( WCHAR ) ) );

    //
    // Set the zero teminator to use RtlInitUnicodeSting without fear of its absence.
    // Actually the buffer size is at least for two WCHAR bigger than the value returned 
    // returned from the ZwQueryValueKey
    //
    ((PWCHAR)Buffer)[ BufferSize/sizeof( WCHAR ) ] = L'\0';

    //
    // actually 2 additional WCHARs were allocated in ExAllocatePool
    //
    BufferSize = BufferSize + 2*sizeof( WCHAR );

    *KeyValueInfo = Buffer;
    Buffer = NULL;

    *InfoBufferLength = BufferSize;

__exit:

    if( NULL != Buffer )
        ExFreePool( Buffer );

    return RC;
}


VOID
OcCrFreeValueFromKey(
    PVOID    KeyValueInfo
    )
{
    ExFreePoolWithTag( KeyValueInfo, 'VRcO' );
}

//------------------------------------------------------------

NTSTATUS
OcCrQueryObjectName(
    __in PVOID Object,
    __out POBJECT_NAME_INFORMATION*    PtrPtrDeviceNameInfo
    )
    /*
    In case of success the function returns the object name information
    allocated from PAGED POLL, a caller must free the memory
    by calling ExFreePool.The NULL is returned if the device doesn't have a name,
    this was done to reduce the pool fragmentation caused by allocation of small
    chunks of memory for empty names.

    Remember that it is safe to call this function for device objects
    because for device objects there is no any query Irp is sent,
    because there is no registered query callback for the device 
    object type which can send an Irp. This is not true in case of file
    object - the query Irp will be sent and this usually results in a deadlock.
    */
{
    NTSTATUS                    RC;
    ULONG                       NameInfoLength;
    POBJECT_NAME_INFORMATION    PtrDeviceNameInfo = NULL;

    RC = ObQueryNameString( Object,
                            NULL,
                            0x0,
                            &NameInfoLength );

    if( STATUS_INFO_LENGTH_MISMATCH == RC && 0x0 != NameInfoLength ){

        PtrDeviceNameInfo = (POBJECT_NAME_INFORMATION)ExAllocatePoolWithTag( PagedPool,
                                                                             NameInfoLength,
                                                                             'NDcO' );
        if( NULL != PtrDeviceNameInfo ){

            RC = ObQueryNameString( Object,
                                    PtrDeviceNameInfo,
                                    NameInfoLength,
                                    &NameInfoLength );
            if( !NT_SUCCESS( RC ) ){

                ExFreePoolWithTag( PtrDeviceNameInfo, 'NDcO' );
                PtrDeviceNameInfo = NULL;

            }//if( !NT_SUCCESS( RC ) )

        }//if( NULL != PtrDeviceNameInfo )

    }

    ASSERT( NT_SUCCESS( RC ) );
    ASSERT( NT_SUCCESS( RC ) && !( NULL == PtrDeviceNameInfo && 0x0 != NameInfoLength ) );

    if( NT_SUCCESS( RC ) )
        *PtrPtrDeviceNameInfo = PtrDeviceNameInfo;

    return RC;
}

//------------------------------------------------------------

VOID
OcCrFreeNameInformation(
    __in POBJECT_NAME_INFORMATION    PtrDeviceNameInfo
    )
    /*
    the function frees the memory allocated by OcCrQueryObjectName
    */
{
    ExFreePoolWithTag( PtrDeviceNameInfo, 'NDcO' );
}

//------------------------------------------------------------

PDEVICE_OBJECT
OcCrIoGetAttachedDeviceReference(
    IN PDEVICE_OBJECT DeviceObject
    )
    /*
    the function returns the highest referenced device in the stack
    */
{

    if( NULL != Global.SystemFunctions.IoGetAttachedDeviceReference ){

        return Global.SystemFunctions.IoGetAttachedDeviceReference( DeviceObject );

    } else {

        KIRQL    OldIrql;

        //
        // raise the IRQL to protect from the scheduler on the current CPU
        //
        OldIrql = KeRaiseIrqlToDpcLevel();
        {
            DeviceObject = IoGetAttachedDevice( DeviceObject );
            ObReferenceObject( DeviceObject );
        }
        KeLowerIrql( OldIrql );
    }

    return DeviceObject;
}

//------------------------------------------------------------

PDEVICE_OBJECT
OcCrIoGetLowerDeviceObject(
    IN PDEVICE_OBJECT DeviceObject
    )

/*++

Routine Description:

    This routine returns the underlying level device object associated with
    the specified device.

Arguments:

    DeviceObject - Supplies a pointer to the device for which the underlying device
    object is to be found.

Return Value:

    The function value is a reference to the lowest underlying device attached
    to the specified device.

--*/

{
    PDEVICE_OBJECT    LowerDeviceObject;
    KIRQL            OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    if( NULL != Global.SystemFunctions.IoGetLowerDeviceObject )
        return Global.SystemFunctions.IoGetLowerDeviceObject( DeviceObject );

    //
    // raise the IRQL to protect from the scheduler on the current CPU
    //
    OldIrql = KeRaiseIrqlToDpcLevel();
    {
        LowerDeviceObject = (( PDEVOBJ_EXTENSION_W2K )( DeviceObject->DeviceObjectExtension ))->AttachedTo;
        if( NULL != LowerDeviceObject )
            ObReferenceObject( LowerDeviceObject );
    }
    KeLowerIrql( OldIrql );

    return LowerDeviceObject;
}

//------------------------------------------------------------

