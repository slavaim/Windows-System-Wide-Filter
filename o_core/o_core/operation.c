/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
03.04.2007 ( April )
 Start
*/

/*
this file contains the code for the 
file operation objects management
*/
#include "struct.h"
#include "proto.h"

//---------------------------------------------------

NTSTATUS
OcCrFillInDeviceWriteParameters( 
    __inout POC_OPERATION_OBJECT    OperationObject,
    __in POC_DEVICE_OBJECT    PtrOcDeviceObject
    );

NTSTATUS
OcCrFillInFsdWriteParameters( 
    __inout POC_OPERATION_OBJECT    OperationObject,
    __in PFLT_CALLBACK_DATA    Data
    );

NTSTATUS
OcCrFillInDeviceReadParameters( 
    __inout POC_OPERATION_OBJECT    OperationObject,
    __in POC_DEVICE_OBJECT    PtrOcDeviceObject
    );

NTSTATUS
OcCrFillInFsdReadParameters( 
    __inout POC_OPERATION_OBJECT    OperationObject,
    __in PFLT_CALLBACK_DATA    Data
    );

//---------------------------------------------------

__forceinline
POC_DEVICE_OBJECT
OcCrReferenceOperationQueueDevice(
    __in POC_DEVICE_OBJECT    OcDeviceObject
    )
    /*
    the returned device object is REFERENCED
    */
{
    POC_DEVICE_OBJECT    QueueDeviceObject;

    if( NULL != OcDeviceObject->Pdo )
        QueueDeviceObject = OcDeviceObject->Pdo;
    else
        QueueDeviceObject = OcDeviceObject;

    OcObReferenceObject( QueueDeviceObject );

    return QueueDeviceObject;
}

//---------------------------------------------------

VOID
OcCrUnlockAndFreeMdlForOperation(
    __in POC_OPERATION_OBJECT    OperationObject
    )
    /*
    the function unlocks and frees the MDL in operation
    object, this function must be called to unlock
    pages before returning from the operation completion
    to avoid the BSOD reporting that a process is completing
    with locked pages
    */
{
    PMDL    Mdl = NULL;
    PMDL*   PtrMdl = NULL;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

#if !DBG
    if( 0x0 == OperationObject->Flags.UnlockMdl &&
        0x0 == OperationObject->Flags.FreeMdl )
        return;
#endif//!DBG

    switch( OperationObject->MajorFunction ){

            case IRP_MJ_WRITE:
                PtrMdl = &OperationObject->OperationParameters.Write.MDL;
                break;

            case IRP_MJ_READ:
                PtrMdl = &OperationObject->OperationParameters.Read.MDL;
                break;
    }

    if( NULL == PtrMdl )
        return;

    //
    // get the MDL without any syncronization - I only want to know
    // whether the MDL is NULL or not
    //
    Mdl = *PtrMdl;

    //
    // copy data in the private buffer if this has not yet been done
    // as the MDL locking the request initiator's buffer is going to be destroyed
    //
    if( 0x0 == OperationObject->Flags.DataInPrivateBuffer && 
        NULL != Mdl &&
        ( OcIsOperationShadowedAsWriteRequest( &OperationObject->SecurityParameters ) || 
          OcIsOperationShadowedAsReadRequest( &OperationObject->SecurityParameters ) ) ){

              KIRQL    OldIrql;

              //
              // synchronize with OcCrShadowWriterWR
              //
              KeAcquireSpinLock( &OperationObject->SpinLock, &OldIrql );
              {// start of the lock

                  if( 0x0 == OperationObject->Flags.DataInPrivateBuffer ){

                      NTSTATUS    RC;

                      RC = OcCrProcessOperationObjectPrivateBuffers( OperationObject,
                                                                     TRUE );

                      //
                      // there should not be any error as all resources
                      // have been allocated and the only job for the CPU
                      // is to copy data from one resident buffer to another
                      //
                      ASSERT( NT_SUCCESS( RC ) );
                  }

              }// end of the lock
              KeReleaseSpinLock( &OperationObject->SpinLock, OldIrql );

    }

    //
    // I need to synchronize as this function can be called
    // from concurrent threads, at least it can be called from
    // different parts of the code without any syncronization
    // among them
    //
    do{
        Mdl = *PtrMdl;
    } while( Mdl != InterlockedCompareExchangePointer( PtrMdl, NULL, Mdl ) );

    //
    // check for an orphan MDL - the MDL must be from
    // Irp or minifilter or created by this driver, in the last
    // case at least UnlockMdl or FreeMdl must be set, in the
    // case of minifilter it is hard to make the following check so
    // the minifilter operation objects are skipped in this check
    //
    ASSERT( !( ( 0x0 == OperationObject->Flags.UnlockMdl && 
                 0x0 == OperationObject->Flags.FreeMdl ) && 
                 NULL != Mdl && 
                 0x0 == OperationObject->Flags.MinifilterOperation &&
                 NULL != OperationObject->Irp && 
                 Mdl != OperationObject->Irp->MdlAddress ) );

    if( NULL == Mdl )
        return;

    //
    // unlock and free the Mdl
    //

    //
    // data must have been copied to private buffer
    // as the MDL locking the user buffer is being destroyed
    //
    ASSERT( 0x1 == OperationObject->Flags.DataInPrivateBuffer );

    //
    // first unlock, this also unmaps the MDL
    //
    if( 0x1 == OperationObject->Flags.UnlockMdl )
        MmUnlockPages( Mdl );

    //
    // and then free
    //
    if( 0x1 == OperationObject->Flags.FreeMdl )
        IoFreeMdl( Mdl );

}

//---------------------------------------------------

VOID
NTAPI
OcCrDeleteOperationObject(
    __in POC_OPERATION_OBJECT    OperationObject
    )
{

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( _OC_OPERATION_OBJ_SIGNATURE == OperationObject->Signature );
    ASSERT( (!IsListEmpty( &OperationObject->ListEntry ))?
              ( NULL != OperationObject->QueueDeviceObject ):
              ( NULL == OperationObject->QueueDeviceObject ) );

    //
    // remove the operation object from the device's requests list
    //
    if( NULL != OperationObject->QueueDeviceObject && 
        !IsListEmpty( &OperationObject->ListEntry ) ){

            KIRQL   OldIrql;

            ASSERT( OperationObject->DeviceObject );
            ASSERT( !IsListEmpty( &OperationObject->QueueDeviceObject->DeviceRequests.IoRequestsListHead ) );

            //
            // remove the object from the PDO's list
            //
            OcRwAcquireLockForWrite( &OperationObject->QueueDeviceObject->DeviceRequests.IoRequestsListRwLock, &OldIrql );
            {// start of the write lock
                RemoveEntryList( &OperationObject->ListEntry );
            }// end of the write lock
            OcRwReleaseWriteLock( &OperationObject->QueueDeviceObject->DeviceRequests.IoRequestsListRwLock, OldIrql );
    }

    switch( OperationObject->MajorFunction ){

        case IRP_MJ_CREATE:

            if( NULL != OperationObject->OperationParameters.Create.FltCallbackDataStore ){

                ExFreeToNPagedLookasideList( &Global.FltCallbackDataStoreLaList, (PVOID)OperationObject->OperationParameters.Create.FltCallbackDataStore );
                OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( OperationObject->OperationParameters.Create.FltCallbackDataStore );
            }

            if( NULL != OperationObject->OperationParameters.Create.RefCreationInfo ){
                ASSERT( OC_FILE_OBJECT_CREATE_INFO_SIGNATURE == OperationObject->OperationParameters.Create.RefCreationInfo->Signature );
                OcObDereferenceObject( OperationObject->OperationParameters.Create.RefCreationInfo );
                OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( OperationObject->OperationParameters.Create.RefCreationInfo );
            }

            break;

        case IRP_MJ_WRITE:
        case IRP_MJ_READ:
            //
            // nothing to do here, the MDL is freed by OcCrUnlockAndFreeMdlForOperation
            //
            break;

    }

    //
    // fre the MDL describing the user buffer
    //
    OcCrUnlockAndFreeMdlForOperation( OperationObject );

    //
    // free the private buffer used for shadowing
    //
    if( NULL != OperationObject->PrivateBufferInfo.Buffer ){

        ASSERT( OcCrPrivateBufferFromSystemPool == OperationObject->PrivateBufferInfo.Type /*||
                OcCrPrivateBufferFromMappedMemory == OperationObject->PrivateBufferInfo.Type*/ );

        OcCrFreeShadowBuffer( OperationObject->PrivateBufferInfo.Buffer, OperationObject->PrivateBufferInfo.Type );
    }

    //
    // derefrence all referenced objects
    //
    if( NULL != OperationObject->FileObject ){

        OcObDereferenceObject( OperationObject->FileObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( OperationObject->FileObject );
    }

    if( NULL != OperationObject->DeviceObject ){

        OcObDereferenceObject( OperationObject->DeviceObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( OperationObject->DeviceObject );
    }

    if( NULL != OperationObject->QueueDeviceObject ){

        OcObDereferenceObject( OperationObject->QueueDeviceObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( OperationObject->QueueDeviceObject );
    }

#if DBG
    switch( OperationObject->MajorFunction ){
        case IRP_MJ_WRITE:
            ASSERT( NULL == OperationObject->OperationParameters.Write.MDL );
            break;
        case IRP_MJ_READ:
            ASSERT( NULL == OperationObject->OperationParameters.Read.MDL );
            break;
    }
#endif//DBG
}

//---------------------------------------------------

VOID
OcCrInsertOperationObjectInDeviceList(
    __in POC_DEVICE_OBJECT    OcDeviceObject,
    __inout POC_OPERATION_OBJECT    OperationObject
    )
{
    POC_DEVICE_OBJECT    QueueDeviceObject;
    KIRQL                OldIrql;

    QueueDeviceObject = OcCrReferenceOperationQueueDevice( OcDeviceObject );

    //
    // save the REFERENCED pointer
    //
    OperationObject->QueueDeviceObject = QueueDeviceObject;

    //
    // insert the object in the PDO's list
    //

    OcRwAcquireLockForWrite( &QueueDeviceObject->DeviceRequests.IoRequestsListRwLock, &OldIrql );
    {// start of the write lock
        InsertTailList( &QueueDeviceObject->DeviceRequests.IoRequestsListHead,
                        &OperationObject->ListEntry );
    }// end of the write lock
    OcRwReleaseWriteLock( &QueueDeviceObject->DeviceRequests.IoRequestsListRwLock, OldIrql );

}

//---------------------------------------------------

NTSTATUS
OcCrCreateOperationObjectForDevice(
    __in POC_DEVICE_OBJECT     PtrOcDeviceObject,
    __in CONST POC_NODE_CTX    NodeContext,
    __inout POC_OPERATION_OBJECT*    PtrOperationObject
    )
    /*
    the function returns initialized and referenced
    operation object
    */
{
    NTSTATUS                RC;
    POC_OPERATION_OBJECT    OperationObject;
    BOOLEAN                 CopyDataInPrivateBuffer = FALSE;
    PIRP                    Irp = NodeContext->RequestData.Irp;
    PIO_STACK_LOCATION      StackLocation = IoGetCurrentIrpStackLocation( Irp );

    //
    // I am going to touch buffers
    //
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( NULL != Irp );

    //
    // allocate the object
    //
    RC = OcObCreateObject( &Global.OcOperationObject,
                           &OperationObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // initialize the object's body
    //
    RtlZeroMemory( OperationObject, sizeof( *OperationObject ) );
    InitializeListHead( &OperationObject->ListEntry );
    KeInitializeSpinLock( &OperationObject->SpinLock );
    OperationObject->Irp = Irp;
    OperationObject->MajorFunction = StackLocation->MajorFunction;
    OperationObject->RequestorThread = Irp->Tail.Overlay.Thread;
    OperationObject->SecurityParameters = NodeContext->SecurityParameters;

#if DBG
    OperationObject->Signature = _OC_OPERATION_OBJ_SIGNATURE;
#endif//DBG

    OperationObject->DeviceObject = PtrOcDeviceObject;
    OcObReferenceObject( PtrOcDeviceObject );

    if( IRP_MJ_CREATE != OperationObject->MajorFunction && 
        NULL != StackLocation->FileObject ){

            //
            // try to retrieve a file object stored in the databases
            //
            OperationObject->FileObject = OcCrRetriveReferencedFileObject( StackLocation->FileObject );
    }
#if DBG
    else if( IRP_MJ_CREATE == OperationObject->MajorFunction ){
        ASSERT( NULL == OcCrRetriveReferencedFileObject( StackLocation->FileObject ) );
    }
#endif//DBG

    //
    // create request parameters are initialized by the caller
    // for all other requests the initialization is here
    //
    switch( OperationObject->MajorFunction ){

        case IRP_MJ_WRITE:
            RC = OcCrFillInDeviceWriteParameters( OperationObject, PtrOcDeviceObject );
            CopyDataInPrivateBuffer = TRUE;
            break;
        case IRP_MJ_READ:
            RC = OcCrFillInDeviceReadParameters( OperationObject, PtrOcDeviceObject );
            CopyDataInPrivateBuffer = FALSE;
            break;
        case IRP_MJ_DEVICE_CONTROL:
            break;
        case IRP_MJ_INTERNAL_DEVICE_CONTROL://sane as IRP_MJ_SCSI
            if( TRUE == OcCrIsMajorScsiRequest( PtrOcDeviceObject, Irp ) ){
                //
                // this is an IRP_MJ_SCSI request
                //
            }
            break;

    }

    if( !NT_SUCCESS( RC ) )
        goto __exit;

    if( OcIsOperationShadowedAsWriteRequest( &OperationObject->SecurityParameters ) || 
        OcIsOperationShadowedAsReadRequest( &OperationObject->SecurityParameters ) ){

        RC = OcCrProcessOperationObjectPrivateBuffers( OperationObject,
                                                       CopyDataInPrivateBuffer );
        if( !NT_SUCCESS( RC ) )
            goto __exit;
    }

__exit:

    if( !NT_SUCCESS( RC ) ){

        //
        // release resources
        //
        OcObDereferenceObject( OperationObject );

    } else {

        *PtrOperationObject = OperationObject;

        //
        // insert the object in the PDO's list,
        // the device object will be retained by 
        // the reference from the operation object
        //
        OcCrInsertOperationObjectInDeviceList( PtrOcDeviceObject , OperationObject );
    }

    return RC;
}

//---------------------------------------------------

NTSTATUS
OcCrFillInDeviceWriteParameters( 
    __inout POC_OPERATION_OBJECT    OperationObject,
    __in POC_DEVICE_OBJECT    PtrOcDeviceObject
    )
    /*
    this function fills in the Write parameters for operation object,
    can be used only for non FSD created devices
    */
{
    NTSTATUS              RC = STATUS_SUCCESS;
    PIRP                  Irp = OperationObject->Irp;
    PIO_STACK_LOCATION    StackLocation = IoGetCurrentIrpStackLocation( Irp );
    PDEVICE_OBJECT        SystemDeviceObject = PtrOcDeviceObject->KernelDeviceObject;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( NULL != OperationObject->Irp );
    ASSERT( IRP_MJ_WRITE == OperationObject->MajorFunction );
    ASSERT( NULL == OperationObject->OperationParameters.Write.SystemBuffer );
    ASSERT( NULL == OperationObject->OperationParameters.Write.UserBuffer );
    ASSERT( NULL == OperationObject->OperationParameters.Write.MDL );
    ASSERT( !OcCrIsFileSystemDevObj( PtrOcDeviceObject->KernelDeviceObject ) );

    OperationObject->OperationParameters.Write.BufferLength = StackLocation->Parameters.Write.Length;
    OperationObject->OperationParameters.Write.ByteOffset = StackLocation->Parameters.Write.ByteOffset;

    if( OcIsFlagOn( SystemDeviceObject->Flags, DO_BUFFERED_IO ) ){

        //
        // buffered IO
        //
        OperationObject->OperationParameters.Write.SystemBuffer = Irp->AssociatedIrp.SystemBuffer;

    } else if( OcIsFlagOn( SystemDeviceObject->Flags, DO_DIRECT_IO ) ){

        //
        // direct IO, the MDL has been already locked by the caller
        // TO DO - create the own copy of this MDL
        //
        OperationObject->OperationParameters.Write.MDL = Irp->MdlAddress;

        ASSERT( 0x0 == OperationObject->Flags.FreeMdl );
        ASSERT( 0x0 == OperationObject->Flags.UnlockMdl );

    } else {

        //
        // neither IO
        //
        OperationObject->OperationParameters.Write.UserBuffer = Irp->UserBuffer;

        //
        // check the buffer if it comes from a user mode subsystemc
        //
        if( UserMode == Irp->RequestorMode ){

            __try{

                //
                // check that the caller is not trying to write 
                // data from the kernel space, I am not going to
                // shadow such requests because an attempt to read
                // an invalid kernel region will result in a BSOD
                //
                ProbeForRead( OperationObject->OperationParameters.Write.UserBuffer,
                              OperationObject->OperationParameters.Write.BufferLength,
                              0x1 );

            } __except( EXCEPTION_EXECUTE_HANDLER ){

                RC = GetExceptionCode();
            }//__except

        }//if( UserMode == Irp->RequestorMode )

        if( !NT_SUCCESS( RC ) )
            goto __exit_from_neither;

        /*
        //
        // do not create an MDL, it won't be used
        //

        //
        // the MDL will be freed when the object is being deleted
        //
        OperationObject->Flags.FreeMdl = 0x1;

        //
        // allocate an MDL for describing and locking the user buffer
        //
        OperationObject->OperationParameters.Write.MDL = IoAllocateMdl( 
            OperationObject->OperationParameters.Write.UserBuffer,
            OperationObject->OperationParameters.Write.BufferLength,
            FALSE,
            FALSE,
            NULL );

        if( NULL == OperationObject->OperationParameters.Write.MDL ){

            RC = STATUS_INSUFFICIENT_RESOURCES;
            goto __exit_from_neither;
        }

        try{

            MmProbeAndLockPages( OperationObject->OperationParameters.Write.MDL,
                                 Irp->RequestorMode,
                                 IoReadAccess );

            //
            // unlock the MDL when the object is being deleted
            //
            OperationObject->Flags.UnlockMdl = 0x1;

        }__except( EXCEPTION_EXECUTE_HANDLER ){

            RC = GetExceptionCode();
        }//__except
        */

__exit_from_neither: ;
    }

    return RC;
}


//---------------------------------------------------

NTSTATUS
OcCrFillInDeviceReadParameters( 
    __inout POC_OPERATION_OBJECT    OperationObject,
    __in POC_DEVICE_OBJECT    PtrOcDeviceObject
    )
    /*
    this function fills in the Write parameters for operation object,
    can be used only for non FSD created devices
    */
{
    NTSTATUS              RC = STATUS_SUCCESS;
    PIRP                  Irp = OperationObject->Irp;
    PIO_STACK_LOCATION    StackLocation = IoGetCurrentIrpStackLocation( Irp );
    PDEVICE_OBJECT        SystemDeviceObject = PtrOcDeviceObject->KernelDeviceObject;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( NULL != OperationObject->Irp );
    ASSERT( IRP_MJ_READ == OperationObject->MajorFunction );
    ASSERT( NULL == OperationObject->OperationParameters.Read.SystemBuffer );
    ASSERT( NULL == OperationObject->OperationParameters.Read.UserBuffer );
    ASSERT( NULL == OperationObject->OperationParameters.Read.MDL );
    ASSERT( !OcCrIsFileSystemDevObj( PtrOcDeviceObject->KernelDeviceObject ) );

    OperationObject->OperationParameters.Read.BufferLength = StackLocation->Parameters.Read.Length;
    OperationObject->OperationParameters.Read.ByteOffset = StackLocation->Parameters.Read.ByteOffset;

    if( OcIsFlagOn( SystemDeviceObject->Flags, DO_BUFFERED_IO ) ){

        //
        // buffered IO
        //
        OperationObject->OperationParameters.Read.SystemBuffer = Irp->AssociatedIrp.SystemBuffer;

    } else if( OcIsFlagOn( SystemDeviceObject->Flags, DO_DIRECT_IO ) ){

        //
        // direct IO, the MDL has been already locked by the caller
        // TO DO - create the own copy of this MDL
        //
        OperationObject->OperationParameters.Read.MDL = Irp->MdlAddress;

        ASSERT( 0x0 == OperationObject->Flags.FreeMdl );
        ASSERT( 0x0 == OperationObject->Flags.UnlockMdl );

    } else {

        //
        // neither IO
        //
        OperationObject->OperationParameters.Read.UserBuffer = Irp->UserBuffer;

        //
        // check the buffer if it comes from a user mode subsystemc
        //
        if( UserMode == Irp->RequestorMode ){

            __try{

                //
                // check that the caller is not trying to write 
                // data from the kernel space, I am not going to
                // shadow such requests because an attempt to write
                // in a kernel region will result in a BSOD
                //
                ProbeForWrite( OperationObject->OperationParameters.Read.UserBuffer,
                               OperationObject->OperationParameters.Read.BufferLength,
                               0x1 );

            } __except( EXCEPTION_EXECUTE_HANDLER ){

                RC = GetExceptionCode();
            }//__except

        }//if( UserMode == Irp->RequestorMode )

        if( !NT_SUCCESS( RC ) )
            goto __exit_from_neither;

        //
        // create an MDL which will be used on completion
        //

        //
        // allocate an MDL for describing and locking the user buffer
        //
        OperationObject->OperationParameters.Read.MDL = IoAllocateMdl( 
            OperationObject->OperationParameters.Read.UserBuffer,
            OperationObject->OperationParameters.Read.BufferLength,
            FALSE,
            FALSE,
            NULL );

        if( NULL == OperationObject->OperationParameters.Read.MDL ){

            RC = STATUS_INSUFFICIENT_RESOURCES;
            goto __exit_from_neither;
        }

        //
        // the MDL will be freed when the object is being deleted
        //
        OperationObject->Flags.FreeMdl = 0x1;

        __try{

            MmProbeAndLockPages( OperationObject->OperationParameters.Read.MDL,
                                 Irp->RequestorMode,
                                 IoWriteAccess );

            //
            // unlock the MDL when the object is being deleted
            //
            OperationObject->Flags.UnlockMdl = 0x1;

        }__except( EXCEPTION_EXECUTE_HANDLER ){

            RC = GetExceptionCode();
        }//__except

__exit_from_neither: ;
    }

    return RC;
}

//---------------------------------------------------

BOOLEAN
OcCrIsRequestInDeviceQueue(
    __in POC_DEVICE_OBJECT    OcDeviceObject,
    __in PIRP    Irp
    )
{
    POC_DEVICE_OBJECT    ReferencedQueueDevice;
    KIRQL                OldIrql;
    BOOLEAN              Found = FALSE;
    BOOLEAN              LowestDeviceReached = TRUE;

    ReferencedQueueDevice = OcCrReferenceOperationQueueDevice( OcDeviceObject );

    if( IsListEmpty( &ReferencedQueueDevice->DeviceRequests.IoRequestsListHead ) ){

        OcObDereferenceObject( ReferencedQueueDevice );
        return FALSE;
    }

    //
    // if the operation object for this operation will be found
    // then mark it as processed because all other subsequent 
    // operations with the same buffers are intended to 
    // write new data and must be processed while the found 
    // operation object might still be alive due to references
    // ( e.g. uncompleted operation )
    // 
    if( OcDeviceObject == ReferencedQueueDevice )
        LowestDeviceReached = TRUE;

    //
    // find an operation object with the same buffers
    //
    OcRwAcquireLockForRead( &ReferencedQueueDevice->DeviceRequests.IoRequestsListRwLock, &OldIrql );
    {// start of the read lock

        PLIST_ENTRY    request;

        //
        // start from the end, the most recent request are at the end of the list
        //
        for( request = ReferencedQueueDevice->DeviceRequests.IoRequestsListHead.Blink;
             request != &ReferencedQueueDevice->DeviceRequests.IoRequestsListHead;
             request = request->Blink ){

                 POC_OPERATION_OBJECT    OperationObject;

                 OperationObject = CONTAINING_RECORD( request, OC_OPERATION_OBJECT, ListEntry );

                 ASSERT( _OC_OPERATION_OBJ_SIGNATURE == OperationObject->Signature );

                 if( 0x1 == OperationObject->Flags.HadHisTimeOnStack )
                     continue;

                 if( OperationObject->Irp == Irp ){

                     Found = TRUE;
                     goto __tail;
                 }

                 //
                 // the Irp's major function code might has been changed 
                 // while Irp traversed through the device stack,
                 // do not use it for comparision
                 //
                 if( IRP_MJ_WRITE == OperationObject->MajorFunction ){

                     if( NULL != OperationObject->OperationParameters.Write.MDL && 
                         Irp->MdlAddress == OperationObject->OperationParameters.Write.MDL ){

                             Found = TRUE;
                             goto __tail;
                     }

                     if( NULL != OperationObject->OperationParameters.Write.SystemBuffer && 
                         Irp->AssociatedIrp.SystemBuffer == OperationObject->OperationParameters.Write.SystemBuffer ){

                             Found = TRUE;
                             goto __tail;
                     }

                 } else if( IRP_MJ_READ == OperationObject->MajorFunction ){

                     if( NULL != OperationObject->OperationParameters.Read.MDL && 
                         Irp->MdlAddress == OperationObject->OperationParameters.Read.MDL ){

                             Found = TRUE;
                             goto __tail;
                     }

                     if( NULL != OperationObject->OperationParameters.Read.SystemBuffer && 
                         Irp->AssociatedIrp.SystemBuffer == OperationObject->OperationParameters.Read.SystemBuffer ){

                             Found = TRUE;
                             goto __tail;
                     }

                 }//if( IRP_MJ_READ == OperationObject->MajorFunction )

__tail:
                 if( Found && OperationObject->DeviceObject == OcDeviceObject ){

                     //
                     // mark the found operation object as processed,
                     // either it reached the lower of the stack and 
                     // I see the subsequent request or someone is trying
                     // to flood the system with requests - process them 
                     // to shadow all data
                     //

                     OperationObject->Flags.HadHisTimeOnStack = 0x1;
                     Found = FALSE;
                 }

                 if( Found && LowestDeviceReached )
                     OperationObject->Flags.HadHisTimeOnStack = 0x1;

                 if( Found )
                     break;

        }//for

    }// end of the read lock
    OcRwReleaseReadLock( &ReferencedQueueDevice->DeviceRequests.IoRequestsListRwLock, OldIrql );

    OcObDereferenceObject( ReferencedQueueDevice );

    return Found;
}

//---------------------------------------------------

NTSTATUS
OcCrCreateOperationObjectForFsd(
    __in POC_DEVICE_OBJECT    PtrOcVolumeDeviceObject,
    __in POC_MINIFLTR_DRV_NODE_CTX    MnfltContext,
    __inout POC_OPERATION_OBJECT*     PtrOperationObject
    )
    /*
    the function returns initialized and referenced
    operation object
    */
{
    NTSTATUS                RC;
    POC_OPERATION_OBJECT    OperationObject;
    PFLT_CALLBACK_DATA      Data = MnfltContext->Common.RequestData.Data;
    POC_FILE_OBJECT         FileObject = MnfltContext->FileObject;//may be NULL
    BOOLEAN                 CopyDataInPrivateBuffer = FALSE;

    //
    // I am going to touch buffers
    //
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    //
    // allocate the object
    //
    RC = OcObCreateObject( &Global.OcOperationObject,
                           &OperationObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // initialize the object's body
    //
    RtlZeroMemory( OperationObject, sizeof( *OperationObject ) );
    InitializeListHead( &OperationObject->ListEntry );
    KeInitializeSpinLock( &OperationObject->SpinLock );
    OperationObject->MajorFunction = Data->Iopb->MajorFunction;
    OperationObject->RequestorThread = Data->Thread;
    OperationObject->SecurityParameters = MnfltContext->Common.SecurityParameters;
    OperationObject->Flags.MinifilterOperation = 0x1;

#if DBG
    OperationObject->Signature = _OC_OPERATION_OBJ_SIGNATURE;
#endif//DBG

    OperationObject->DeviceObject = PtrOcVolumeDeviceObject;
    OcObReferenceObject( PtrOcVolumeDeviceObject );

    if( IRP_MJ_CREATE != OperationObject->MajorFunction ){

            ASSERT( NULL != FileObject );

            //
            // try to retrieve a file object stored in the databases
            //
            OperationObject->FileObject = FileObject;
            OcObReferenceObject( OperationObject->FileObject );
    }
#if DBG
    else if( IRP_MJ_CREATE == OperationObject->MajorFunction ){
        ASSERT( NULL == FileObject );
        ASSERT( NULL == OcCrRetriveReferencedFileObject( Data->Iopb->TargetFileObject ) );
    }
#endif//DBG

    //
    // create request parameters are initialized by the caller
    // for all other requests the initialization is here
    //
    switch( OperationObject->MajorFunction ){

        case IRP_MJ_WRITE:
            RC = OcCrFillInFsdWriteParameters( OperationObject, Data );
            CopyDataInPrivateBuffer = TRUE;
            break;

        case IRP_MJ_READ:
            RC = OcCrFillInFsdReadParameters( OperationObject, Data );
            CopyDataInPrivateBuffer = FALSE;
            break;

    }

    if( !NT_SUCCESS( RC ) )
        goto __exit;

    if( OcIsOperationShadowedAsWriteRequest( &OperationObject->SecurityParameters ) || 
        OcIsOperationShadowedAsReadRequest( &OperationObject->SecurityParameters ) ){

        RC = OcCrProcessOperationObjectPrivateBuffers( OperationObject,
                                                       CopyDataInPrivateBuffer );
        if( !NT_SUCCESS( RC ) )
            goto __exit;
    }

__exit:

    if( !NT_SUCCESS( RC ) ){

        //
        // release the resources
        //
        OcObDereferenceObject( OperationObject );

    } else {

        *PtrOperationObject = OperationObject;

        //
        // insert the object in the device list,
        // the device object will be retained by 
        // the reference from the operation object
        //
        OcCrInsertOperationObjectInDeviceList( PtrOcVolumeDeviceObject , OperationObject );
    }

    return RC;
}

//---------------------------------------------------

NTSTATUS
OcCrFillInFsdWriteParameters( 
    __inout POC_OPERATION_OBJECT    OperationObject,
    __in PFLT_CALLBACK_DATA    Data
    )
    /*
    this function fills in the Write parameters for operation object in case of FSD
    */
{
    NTSTATUS              RC = STATUS_SUCCESS;
    PIRP                  Irp = OperationObject->Irp;
    PFLT_PARAMETERS       Parameters = &Data->Iopb->Parameters;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( IRP_MJ_WRITE == OperationObject->MajorFunction );
    ASSERT( NULL == OperationObject->OperationParameters.Write.SystemBuffer );
    ASSERT( NULL == OperationObject->OperationParameters.Write.UserBuffer );
    ASSERT( NULL == OperationObject->OperationParameters.Write.MDL );

    OperationObject->OperationParameters.Write.BufferLength = Parameters->Write.Length;
    OperationObject->OperationParameters.Write.ByteOffset = Parameters->Write.ByteOffset;

    OperationObject->OperationParameters.Write.MDL = Parameters->Write.MdlAddress;

    if( FLT_IS_SYSTEM_BUFFER( Data ) )
        OperationObject->OperationParameters.Write.SystemBuffer = Parameters->Write.WriteBuffer;
    else
        OperationObject->OperationParameters.Write.UserBuffer = Parameters->Write.WriteBuffer;

    if( NULL != OperationObject->OperationParameters.Write.UserBuffer &&
        NULL == OperationObject->OperationParameters.Write.MDL ){

            //
            // process the user's buffer
            //

            //
            // check the buffer if it comes from a user mode subsystem
            //
            if( UserMode == Data->RequestorMode ){

                __try{

                    //
                    // check that the caller is not trying to write 
                    // data from the kernel space, I am not going to
                    // shadow such requests because an attempt to read
                    // an invalid kernel region will result in a BSOD
                    //
                    ProbeForRead( OperationObject->OperationParameters.Write.UserBuffer,
                                  OperationObject->OperationParameters.Write.BufferLength,
                                  0x1 );

                } __except( EXCEPTION_EXECUTE_HANDLER ){

                    RC = GetExceptionCode();
                }//__except

            }//if( UserMode == Data->RequestorMode )

            if( !NT_SUCCESS( RC ) )
                goto __exit_from_buffer_locking;

            /*
            //
            // do not create an MDL, it won't be needed
            //

            //
            // the MDL will be freed when the object is being deleted
            //
            OperationObject->Flags.FreeMdl = 0x1;

            //
            // allocate an MDL for describing and locking the user buffer
            //
            Mdl = IoAllocateMdl( OperationObject->OperationParameters.Write.UserBuffer,
                                 OperationObject->OperationParameters.Write.BufferLength,
                                 FALSE,
                                 FALSE,
                                 NULL );
            if( NULL == Mdl ){

                RC = STATUS_INSUFFICIENT_RESOURCES;
                goto __exit_from_buffer_locking;
            }

            OperationObject->OperationParameters.Write.MDL = Mdl;

            try{

                MmProbeAndLockPages( OperationObject->OperationParameters.Write.MDL,
                                     Data->RequestorMode,
                                     IoReadAccess );

                //
                // unlock the MDL when the object is being deleted
                //
                OperationObject->Flags.UnlockMdl = 0x1;

            }__except( EXCEPTION_EXECUTE_HANDLER ){

                RC = GetExceptionCode();
            }//__except
            */

__exit_from_buffer_locking: ;
    }

    return RC;
}

//---------------------------------------------------

NTSTATUS
OcCrFillInFsdReadParameters( 
    __inout POC_OPERATION_OBJECT    OperationObject,
    __in PFLT_CALLBACK_DATA    Data
    )
    /*
    this function fills in the Read parameters for operation object in case of FSD
    */
{
    NTSTATUS              RC = STATUS_SUCCESS;
    PIRP                  Irp = OperationObject->Irp;
    PFLT_PARAMETERS       Parameters = &Data->Iopb->Parameters;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( IRP_MJ_READ == OperationObject->MajorFunction );
    ASSERT( NULL == OperationObject->OperationParameters.Read.SystemBuffer );
    ASSERT( NULL == OperationObject->OperationParameters.Read.UserBuffer );
    ASSERT( NULL == OperationObject->OperationParameters.Read.MDL );

    OperationObject->OperationParameters.Read.BufferLength = Parameters->Read.Length;
    OperationObject->OperationParameters.Read.ByteOffset = Parameters->Read.ByteOffset;

    OperationObject->OperationParameters.Read.MDL = Parameters->Read.MdlAddress;

    if( FLT_IS_SYSTEM_BUFFER( Data ) )
        OperationObject->OperationParameters.Read.SystemBuffer = Parameters->Read.ReadBuffer;
    else
        OperationObject->OperationParameters.Read.UserBuffer = Parameters->Read.ReadBuffer;

    if( NULL != OperationObject->OperationParameters.Read.UserBuffer &&
        NULL == OperationObject->OperationParameters.Read.MDL ){

            PMDL    Mdl;

            //
            // process the user's buffer
            //

            //
            // check the buffer if it comes from a user mode subsystem
            //
            if( UserMode == Data->RequestorMode ){

                __try{

                    //
                    // check that the caller is not trying to read 
                    // in the kernel space, I am not going to
                    // shadow such requests because an attempt to write
                    // in a kernel region will result in a BSOD
                    //
                    ProbeForWrite( OperationObject->OperationParameters.Read.UserBuffer,
                                   OperationObject->OperationParameters.Read.BufferLength,
                                   0x1 );

                } __except( EXCEPTION_EXECUTE_HANDLER ){

                    RC = GetExceptionCode();
                }//__except

            }//if( UserMode == Data->RequestorMode )

            if( !NT_SUCCESS( RC ) )
                goto __exit_from_buffer_locking;

            //
            // create an MDL which will be used to shadow read data on completion
            //

            //
            // allocate an MDL for describing and locking the user buffer
            //
            Mdl = IoAllocateMdl( OperationObject->OperationParameters.Read.UserBuffer,
                                 OperationObject->OperationParameters.Read.BufferLength,
                                 FALSE,
                                 FALSE,
                                 NULL );
            if( NULL == Mdl ){

                RC = STATUS_INSUFFICIENT_RESOURCES;
                goto __exit_from_buffer_locking;
            }

            //
            // the MDL will be freed when the object is being deleted
            //
            OperationObject->Flags.FreeMdl = 0x1;

            OperationObject->OperationParameters.Read.MDL = Mdl;

            __try{

                MmProbeAndLockPages( OperationObject->OperationParameters.Read.MDL,
                                     Data->RequestorMode,
                                     IoWriteAccess );

                //
                // unlock the MDL when the object is being deleted
                //
                OperationObject->Flags.UnlockMdl = 0x1;

            }__except( EXCEPTION_EXECUTE_HANDLER ){

                RC = GetExceptionCode();
            }//__except

__exit_from_buffer_locking: ;
    }

    return RC;
}

//---------------------------------------------------

BOOLEAN
OcCrIsRequestInFsdDeviceQueue(
    __in POC_DEVICE_OBJECT     OcVolumeDeviceObject,
    __in PFLT_CALLBACK_DATA    Data,
    __in POC_FILE_OBJECT    FileObject
    )
    /*
    this function should not be called for minifilter's requests
    because minifilter callbacks are always the upper receiver of the request,
    the PnP filter's devices are not used for FSD requests' processing
    */
{
    POC_DEVICE_OBJECT    ReferencedQueueDevice;
    KIRQL                OldIrql;
    BOOLEAN              Found = FALSE;

    ReferencedQueueDevice = OcCrReferenceOperationQueueDevice( OcVolumeDeviceObject );

    if( IsListEmpty( &ReferencedQueueDevice->DeviceRequests.IoRequestsListHead ) || 
        NULL == FileObject ){

        OcObDereferenceObject( ReferencedQueueDevice );
        return FALSE;
    }

    //
    // find an operation object with the same buffers
    //
    OcRwAcquireLockForRead( &ReferencedQueueDevice->DeviceRequests.IoRequestsListRwLock, &OldIrql );
    {// start of the read lock

        PLIST_ENTRY        request;
        PFLT_PARAMETERS    Parameters = &Data->Iopb->Parameters;

        //
        // start from the end, the most recent request are at the end of the list
        //
        for( request = ReferencedQueueDevice->DeviceRequests.IoRequestsListHead.Blink;
             request != &ReferencedQueueDevice->DeviceRequests.IoRequestsListHead;
             request = request->Blink ){

                 POC_OPERATION_OBJECT    OperationObject;

                 OperationObject = CONTAINING_RECORD( request, OC_OPERATION_OBJECT, ListEntry );

                 ASSERT( _OC_OPERATION_OBJ_SIGNATURE == OperationObject->Signature );

                 if( NULL == OperationObject->FileObject )
                     continue;

                 if( OperationObject->FileObject->ContextObject != FileObject->ContextObject ){

                     //
                     // different data streams
                     //
                     continue;
                 }

                 if( 0x1 == OperationObject->Flags.HadHisTimeOnStack ){

                     //
                     // the found operation object is for request 
                     // which has been processed through the device stack
                     //
                     continue;
                 }

                 //
                 // the Irp's major function code might has been changed 
                 // while Irp traversed through the device stack,
                 // do not use it for comparision
                 //
                 if( IRP_MJ_WRITE == OperationObject->MajorFunction ){

                     if( OperationObject->OperationParameters.Write.ByteOffset.QuadPart != Parameters->Write.ByteOffset.QuadPart ||
                         OperationObject->OperationParameters.Write.BufferLength != Parameters->Write.Length ){

                             //
                             // buffer length or offset is not the same
                             //
                             continue;
                     }

                     if( NULL != OperationObject->OperationParameters.Write.MDL && 
                         Parameters->Write.MdlAddress == OperationObject->OperationParameters.Write.MDL ){

                             Found = TRUE;
                             goto __tail;
                     }

                     if( NULL != OperationObject->OperationParameters.Write.SystemBuffer && 
                         FLT_IS_SYSTEM_BUFFER( Data ) &&
                         Parameters->Write.WriteBuffer == OperationObject->OperationParameters.Write.SystemBuffer ){

                             Found = TRUE;
                             goto __tail;
                     }

                     if( NULL != OperationObject->OperationParameters.Write.UserBuffer && 
                         !FLT_IS_SYSTEM_BUFFER( Data ) &&
                         Data->Thread == OperationObject->RequestorThread && 
                         Parameters->Write.WriteBuffer == OperationObject->OperationParameters.Write.UserBuffer ){

                             Found = TRUE;
                             goto __tail;
                     }

                 }//if( IRP_MJ_WRITE == OperationObject->MajorFunction )

__tail:
                 if( Found ){

                     //
                     // mark the found operation object as processed,
                     // because this function is called only for
                     // hooked FSD'd entry points which are
                     // always lower request receivers in the FSD stack
                     //

                     OperationObject->Flags.HadHisTimeOnStack = 0x1;
                     break;
                 }

        }//for

    }// end of the read lock
    OcRwReleaseReadLock( &ReferencedQueueDevice->DeviceRequests.IoRequestsListRwLock, OldIrql );

    OcObDereferenceObject( ReferencedQueueDevice );

    return Found;
}

//---------------------------------------------------

NTSTATUS
OcCrProcessOperationObjectPrivateBuffers(
    __in POC_OPERATION_OBJECT    OperationObject,
    __in BOOLEAN                 CopyDataInPrivateBuffer
    )
    /*
    the function copy data in a private buffer or allocates
    a private buffer to facilitate the copying on completion,
    the function generates page faults for pages 
    backed by pagingfiles or mapped files, so the
    recursive entries are possible while calling this function
    */
{
    NTSTATUS    RC = STATUS_SUCCESS;

    //
    // if this function should be called at dispatch
    // level then it must not generate page faults
    // for pages backed by a pagingfile - see 
    // OcCrAllocateShadowBuffer
    //
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( 0x0 == OperationObject->Flags.DataInPrivateBuffer );

    if( 0x1 == OperationObject->Flags.PrivateBufferAllocated && 
        0x1 == OperationObject->Flags.DataInPrivateBuffer )
        return RC;

    if( IRP_MJ_WRITE == OperationObject->MajorFunction && 
        0x0 != OperationObject->OperationParameters.Write.BufferLength ){

        PVOID                     PrivateBuffer;

        ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

        if( 0x0 == OperationObject->Flags.PrivateBufferAllocated ){

            OC_PRIVATE_BUFFER_TYPE    BufferType;

            ASSERT( NULL == OperationObject->PrivateBufferInfo.Buffer );

            PrivateBuffer = OcCrAllocateShadowBuffer( OperationObject->OperationParameters.Write.BufferLength,
                                                      TRUE,
                                                      &BufferType );
            if( NULL == PrivateBuffer )
                return STATUS_INSUFFICIENT_RESOURCES;

            //
            // buffer will be freed when the object is being deleted
            //
            OperationObject->PrivateBufferInfo.Buffer = PrivateBuffer;
            OperationObject->PrivateBufferInfo.Type = BufferType;

            //
            // remember that the buffer has been allocated
            //
            OperationObject->Flags.PrivateBufferAllocated = 0x1;

        } else {

            //
            // this is a second call to this function, the buffer
            // has been already allocated
            //
            PrivateBuffer = OperationObject->PrivateBufferInfo.Buffer;
            ASSERT( PrivateBuffer );
        }

        if( CopyDataInPrivateBuffer ){

            ASSERT( 0x1 == OperationObject->Flags.PrivateBufferAllocated );
            ASSERT( 0x0 == OperationObject->Flags.DataInPrivateBuffer );

            if( NULL != OperationObject->OperationParameters.Write.MDL ){

                PVOID    SystemAddress;

                //
                // the pages will be unmaped when they are being unlocked in MmUnlockPages
                //
                SystemAddress = MmGetSystemAddressForMdlSafe( OperationObject->OperationParameters.Write.MDL,
                                                              NormalPagePriority );
                if( NULL != SystemAddress ){

                    ASSERT( OperationObject->OperationParameters.Write.BufferLength <= MmGetMdlByteCount( OperationObject->OperationParameters.Write.MDL ) );
                    RtlCopyMemory( PrivateBuffer,
                                   SystemAddress,
                                   OperationObject->OperationParameters.Write.BufferLength );

                } else {

                    RC = STATUS_INSUFFICIENT_RESOURCES;
                }

            } else if( NULL != OperationObject->OperationParameters.Write.SystemBuffer ){

                RtlCopyMemory( PrivateBuffer,
                               OperationObject->OperationParameters.Write.SystemBuffer,
                               OperationObject->OperationParameters.Write.BufferLength );

            } else if( NULL != OperationObject->OperationParameters.Write.UserBuffer ){

                ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

                __try{

                    //
                    // checking by using ProbeForRead must have been already made
                    //
                    RtlCopyMemory( PrivateBuffer,
                                   OperationObject->OperationParameters.Write.UserBuffer,
                                   OperationObject->OperationParameters.Write.BufferLength );


                } __except( EXCEPTION_EXECUTE_HANDLER ){

                    RC = GetExceptionCode();
                }//__except

            }// else if

            //
            // remember that operation's buffers have been saved in the private non volatile buffers
            //
            OperationObject->Flags.DataInPrivateBuffer = 0x1;
        }// if( CopyDataInPrivateBuffer )

    } else if( IRP_MJ_READ == OperationObject->MajorFunction && 
               0x0 != OperationObject->OperationParameters.Read.BufferLength ){

        PVOID                     PrivateBuffer;

        if( 0x0 == OperationObject->Flags.PrivateBufferAllocated ){

            OC_PRIVATE_BUFFER_TYPE    BufferType;

            //
            // this is a read request, so there is nothing to copy in a private buffer
            // before the data will be read to the caller supplied buffer,
            // but the privatre buffer will be allocated here to facilitate the 
            // data copying on completion which is usually done at DISPATCH_LEVEL, 
            // actually I can call this function on completion but it will be not
            // a wise thing as this put the CPU at DISPATCH_LEVEL for a 
            // sufficiently long time
            //

            ASSERT( NULL == OperationObject->PrivateBufferInfo.Buffer );

            PrivateBuffer = OcCrAllocateShadowBuffer( OperationObject->OperationParameters.Read.BufferLength,
                                                      FALSE,// buffer might be used at DISPATCH_LEVEL
                                                      &BufferType );

            if( NULL == PrivateBuffer )
                return STATUS_INSUFFICIENT_RESOURCES;

            //
            // buffer will be freed when the object is being deleted
            //
            OperationObject->PrivateBufferInfo.Buffer = PrivateBuffer;
            OperationObject->PrivateBufferInfo.Type = BufferType;

            //
            // remember that the buffer has been allocated
            //
            OperationObject->Flags.PrivateBufferAllocated = 0x1;

        } else {

            //
            // this is a second call to this function, the buffer
            // has been already allocated
            //
            PrivateBuffer = OperationObject->PrivateBufferInfo.Buffer;
            ASSERT( PrivateBuffer );
        }

        ASSERT( 0x0 == OperationObject->Flags.DataInPrivateBuffer );

        //
        // remember that the buffer has been allocated
        //
        OperationObject->Flags.PrivateBufferAllocated = 0x1;

        if( CopyDataInPrivateBuffer ){

            ASSERT( 0x1 == OperationObject->Flags.PrivateBufferAllocated );
            ASSERT( 0x0 == OperationObject->Flags.DataInPrivateBuffer );

            if( NULL != OperationObject->OperationParameters.Read.MDL ){

                PVOID    SystemAddress;

                //
                // the pages will be unmaped when they are being unlocked in MmUnlockPages
                //
                SystemAddress = MmGetSystemAddressForMdlSafe( OperationObject->OperationParameters.Read.MDL,
                                                              NormalPagePriority );
                if( NULL != SystemAddress ){

                    ASSERT( OperationObject->OperationParameters.Read.BufferLength <= MmGetMdlByteCount( OperationObject->OperationParameters.Read.MDL ) );
                    RtlCopyMemory( PrivateBuffer,
                                   SystemAddress,
                                   OperationObject->OperationParameters.Read.BufferLength );

                } else {

                    RC = STATUS_INSUFFICIENT_RESOURCES;
                }

            } else if( NULL != OperationObject->OperationParameters.Write.SystemBuffer ){

                RtlCopyMemory( PrivateBuffer,
                               OperationObject->OperationParameters.Read.SystemBuffer,
                               OperationObject->OperationParameters.Read.BufferLength );

            } else if( NULL != OperationObject->OperationParameters.Read.UserBuffer ){

                ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

                __try{

                    //
                    // checking by using ProbeForWrite must have been already made
                    //
                    RtlCopyMemory( PrivateBuffer,
                                   OperationObject->OperationParameters.Read.UserBuffer,
                                   OperationObject->OperationParameters.Read.BufferLength );


                } __except( EXCEPTION_EXECUTE_HANDLER ){

                    RC = GetExceptionCode();
                }//__except

            }// else if

            //
            // remember that operation's buffers have been saved in the private non volatile buffers
            //
            OperationObject->Flags.DataInPrivateBuffer = 0x1;
        }// if( CopyDataInPrivateBuffer )

    } else if( IRP_MJ_INTERNAL_DEVICE_CONTROL == OperationObject->MajorFunction || 
               IRP_MJ_DEVICE_CONTROL == OperationObject->MajorFunction ){

        //
        // emulate the normal behaviour( thoug the buffer is not allocated, but
        // the flags plays a significant role in the shadowing subsystem behaviour )
        //
        /*
        OperationObject->Flags.PrivateBufferAllocated = 0x1;
        if( CopyDataInPrivateBuffer )
            OperationObject->Flags.DataInPrivateBuffer = 0x1;
        */

        //
        // TO DO
        //
        ASSERT( NT_SUCCESS( RC ) );

    } else {

        /*
        ASSERT( !"An unsupported request has been sent to OcCrProcessOperationObjectPrivateBuffers" );
#if DBG
        KeBugCheckEx( OC_CORE_BUG_UNSUPPORTED_REQUEST,
                      (ULONG_PTR)OperationObject,
                      (ULONG_PTR)CopyDataInPrivateBuffer,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)NULL );
#endif//DBG

        RC = STATUS_INVALID_PARAMETER;
        */

    }

    return RC;
}

//---------------------------------------------------

