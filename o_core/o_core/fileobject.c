/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
13.03.2007 ( March )
 Start
*/

/*
this file contains the code for the 
file objects management and bookkeeping
*/
#include "struct.h"
#include "proto.h"

/*
The main points of file objects and contexts management are

 - if a file object is exist in the system and has not received
   close request it can be found in the file objects hash

 - if a context has at least one opened file object
   then the context object can be found in the context hash

 - when a file receives the close request it is removed from
   the file objects hash, its context object's reference count for
   opened file objects is decremented, but the file object continues
   to reference the context object, the file object will be alive
   while it is referenced

 - when context object's reference counter of opened file objects drops
   to zero the context object is removed from the hash of context objects
   but will be alive while it is referenced through its object header

*/

//--------------------------------------------------------------

VOID
OcCrDropFoContext(
    IN POC_CONTEXT_OBJECT    FileContextObject
    );

VOID
OcCrDecrementContextOpenedFileObjectsCounter(
    IN POC_CONTEXT_OBJECT     ContextObject
    );

NTSTATUS
OcCrInsertFileObjectInContext(
    IN POC_FILE_OBJECT    FileObject
    );

//--------------------------------------------------------------

ULONG_PTR
OcCrGetSysFileObjectContext(
    IN PFILE_OBJECT    FileObject
    )
    /*
    the FileObject object must be already initialized, i.e. processed by 
    IRP_MJ_CREATE dispatch routine
    */
{

    BOOLEAN    IsFsdMounted;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // FOs with FO_DIRECT_DEVICE_OPEN or w/o VPB are initialized by 
    // a device itself, so there is no any reason to
    // make any assumptions about their context field
    //
    ASSERT( ( !OcIsFlagOn( FileObject->Flags, FO_DIRECT_DEVICE_OPEN ) && 
              ( NULL != FileObject->Vpb || NULL != FileObject->DeviceObject->Vpb ) )? 
             NULL != FileObject->FsContext :
             TRUE );

    //
    // I wrote in details the way of determing whether an 
    // FSD is mounted , I can afford this and do not 
    // optimize this function because it is called very 
    // rarely
    //
    if( FileObject->Flags & FO_DIRECT_DEVICE_OPEN ){

        IsFsdMounted = FALSE;

    } else {

        KIRQL      OldIrql;

        IoAcquireVpbSpinLock( &OldIrql );
        {// start of the VPB lock

            if( NULL != FileObject->Vpb && ( FileObject->Vpb->Flags & VPB_MOUNTED) )
                IsFsdMounted = TRUE;
            else if( FileObject->DeviceObject->Vpb && ( FileObject->DeviceObject->Vpb->Flags & VPB_MOUNTED) )
                IsFsdMounted = TRUE;
            else
                IsFsdMounted = FALSE;

        }// end  of the VPB lock
        IoReleaseVpbSpinLock( OldIrql );
    }

    //
    // If the file object has been initialized by an FSD then
    // the notion of context is applicable to this file obect, 
    // else instead the context the device object is used.
    //

    if( IsFsdMounted ){

        return (ULONG_PTR)FileObject->FsContext;

    } else {

        return (ULONG_PTR)FileObject->DeviceObject;
    }
}

//--------------------------------------------------------------

VOID
OcLockFoContextHashLine(
    IN ULONG_PTR    SysContext,
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    )
{
    ULONG    Index;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    Index = Global.PtrFoContextHashObject->KeyValueToHashIndexFunction( Global.PtrFoContextHashObject,
                                                                        SysContext );

    ASSERT( Index < OC_STATIC_ARRAY_SIZE( Global.FoCtxQueueLock ) );

    //
    // disable APCs delivering that might invoke an APC trying to acquire the lock
    //
    KeEnterCriticalRegion();
    OcQlAcquireLockWithContext( &Global.FoCtxQueueLock[ Index ], WaitBlock, SysContext );
}

//--------------------------------------------------------------

VOID
OcReleaseFoContextHashLine(
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    )
{
    ULONG    Index;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( 0x0 != WaitBlock->Context );
    ASSERT( KeAreApcsDisabled() );

    Index = Global.PtrFoContextHashObject->KeyValueToHashIndexFunction( Global.PtrFoContextHashObject,
                                                                        WaitBlock->Context );

    ASSERT( Index < OC_STATIC_ARRAY_SIZE( Global.FoCtxQueueLock ) );

    OcQlReleaseLockWithContext( &Global.FoCtxQueueLock[ Index ], WaitBlock );
    KeLeaveCriticalRegion();
}

//--------------------------------------------------------------

VOID
OcInsertFoContextHashLineBarrier(
    IN ULONG_PTR    SysContext,
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    )
{
    ULONG    Index;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    Index = Global.PtrFoContextHashObject->KeyValueToHashIndexFunction( Global.PtrFoContextHashObject,
                                                                        SysContext );

    ASSERT( Index < OC_STATIC_ARRAY_SIZE( Global.FoCtxQueueLock ) );

    //
    // disable APCs delivering that might invoke an APC trying to acquire the lock
    //
    KeEnterCriticalRegion();
    OcQlInsertBarrier( &Global.FoCtxQueueLock[ Index ], WaitBlock, SysContext );
}

//--------------------------------------------------------------

VOID
OcRemoveFoContextHashLineBarrier(
    IN POC_QUEUE_WAIT_BLOCK    WaitBlock
    )
{
    ULONG    Index;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( 0x0 != WaitBlock->Context );
    ASSERT( KeAreApcsDisabled() );

    Index = Global.PtrFoContextHashObject->KeyValueToHashIndexFunction( Global.PtrFoContextHashObject,
                                                                        WaitBlock->Context );

    ASSERT( Index < OC_STATIC_ARRAY_SIZE( Global.FoCtxQueueLock ) );

    OcQlRemoveBarrier( &Global.FoCtxQueueLock[ Index ], WaitBlock );
    KeLeaveCriticalRegion();
}

//--------------------------------------------------------------

POC_FILE_OBJECT
OcCrReferenceFileObjectBySystemObject(
    IN PFILE_OBJECT    SysFileObject
    )
    /*
    Return value - refernced object or NULL.

    The function rturns referenced file object
    if it exists in the file objects database.

    The caller must dereference the object.
    */
{
    return OcHsFindContextByKeyValue( Global.PtrFoHashObject,
                                      (ULONG_PTR)SysFileObject,
                                      OcObReferenceObject );
}

//--------------------------------------------------------------

NTSTATUS
OcCrProcessFileObjectCreating(
    IN PFILE_OBJECT        SysFileObject,
    IN POC_FILE_OBJECT_CREATE_INFO    CreationInfoObject OPTIONAL,
    IN BOOLEAN             CheckForDuplicate,
    OUT POC_FILE_OBJECT*   RefOcFileObject
    )
    /*
    This function processes the file object creating.
    the function returns the referenced file object in *RefOcFileObject
    the caller must derefrence it when it is not longer needed.

    If CreationInfoObject is NULL this means that the creator
    for this file object is unknown, usually this is a case
    of a stream file object. In this case the context object
    is used for initializing, if the conext object doesn't exist
    an error is returned.

    If check CheckForDuplicate is set to FALSE the checking for
    duplicate entry will not be done, this simplify creating and
    reduces the number of acquired locks.

    The counterpart function is OcCrProcessFileObjectCloseRequest.

    Do not be confused by hash functions that have a word "Context"
    in their names, this is just a coincidence and is not related with
    the file object's context.

    */
{
    NTSTATUS              RC;
    POC_FILE_OBJECT       FileObject = NULL;
    POC_FILE_OBJECT       DuplicateFileObject = NULL;
    POC_CONTEXT_OBJECT    ContextObject;
    OC_QUEUE_WAIT_BLOCK   WaitBlock;
    BOOLEAN               InsertedInHash = FALSE;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( FALSE == CheckForDuplicate || TRUE == CheckForDuplicate );

    //
    // create the new file object
    //
    RC = OcObCreateObject( &Global.OcFleObjectType,
                           &FileObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // initialize the file object's body
    //
    {
        RtlZeroMemory( FileObject, sizeof( *FileObject ) );
        FileObject->SysFileObject = SysFileObject;
        FileObject->FileObjectState = OC_FILE_OBJECT_OPENED;

        //
        // refrence and safe the creation info object,
        // the object will be dereferenced when the file object is deleted
        //
        if( CreationInfoObject ){

            OcObReferenceObject( CreationInfoObject );
            FileObject->CreationInfoObject = CreationInfoObject;
        }

        //
        // create or retrive the file object's context object
        // and associate it with the file object
        //
        RC = OcCrInsertFileObjectInContext( FileObject );
        if( !NT_SUCCESS( RC ) )
            goto __exit;
    }

    //
    // get the referenced context object
    //
    ContextObject = FileObject->ContextObject;
    ASSERT( NULL != ContextObject );

    //
    // if the FO is for a data stream describing a whole device, 
    // i.e. this is a direct open, then the file name and the 
    // device name are equal or the file has a FO_DIRECT_DEVICE_OPEN
    // flag, the latter is a redundant check
    //
    ASSERT( OcIsFlagOn( SysFileObject->Flags, FO_DIRECT_DEVICE_OPEN )?
            ( ContextObject->CreationInfoObject->VolumeNameLength == ContextObject->CreationInfoObject->FileName.Length ):
            TRUE );
    if( ContextObject->CreationInfoObject->VolumeNameLength == ContextObject->CreationInfoObject->FileName.Length || 
        OcIsFlagOn( SysFileObject->Flags, FO_DIRECT_DEVICE_OPEN ) ){

        FileObject->Flags.DirectDeviceOpen = 0x1;
    }

    //
    // serialize the insertion of file object in database
    // if there is possibility of concurrent file object creating
    // for example when a stream file object is found in concurrent threads
    //
    if( TRUE == CheckForDuplicate ){

        //
        // disable APCs delivering that might invoke an APC trying to acquire the lock
        //
        KeEnterCriticalRegion();
        OcQlAcquireLockWithContext( &ContextObject->ContexQueueLock,
                                    &WaitBlock,
                                    (ULONG_PTR)SysFileObject );

        //
        // the lock has been acquired, 
        // now try to find a duplicate entry in the hash,
        // do not be confused by the hash manager's 
        // function named OcHsFindContextByKeyValue - 
        // this name doesn't have any relations with
        // the file object context, just a coincidence
        //
        DuplicateFileObject = OcHsFindContextByKeyValue( Global.PtrFoHashObject,
                                                         (ULONG_PTR)FileObject->SysFileObject,
                                                         OcObReferenceObject );
        if( NULL != DuplicateFileObject ){
            //
            // the object is already in the hash
            // postpone objects excahging and newly 
            // created object's destroying until the lock
            // is released
            //
        }
    }

    ASSERT( NULL != FileObject->SysFileObject);
    if( NULL == DuplicateFileObject ){

        ASSERT( NULL == OcHsFindContextByKeyValue( Global.PtrFoHashObject,
                                                   (ULONG_PTR)FileObject->SysFileObject,
                                                   NULL ) );

        //
        // insert the newly created file object in the hash,
        // refrence it, so the object's refrence count will be 0x2 -
        // a one reference from OcObCreateObject and the other
        // when the object is being inserted in the hash
        //
        RC = OcHsInsertContextInHash( Global.PtrFoHashObject,
                                      (ULONG_PTR)FileObject->SysFileObject,
                                      FileObject,
                                      OcObReferenceObject );
        if( NT_SUCCESS( RC ) ){

            InsertedInHash = TRUE;

            ASSERT( NULL != OcHsFindContextByKeyValue( Global.PtrFoHashObject,
                                                       (ULONG_PTR)FileObject->SysFileObject,
                                                       NULL ) );
        }//if( NT_SUCCESS( RC ) )
    }

    //
    // release the acquired lock
    //
    if( TRUE == CheckForDuplicate ){

        OcQlReleaseLockWithContext( &ContextObject->ContexQueueLock,
                                    &WaitBlock );
        KeLeaveCriticalRegion();
    }

    //
    // if the duplicate file object has been found - use it
    // instead the newly created one
    //
    if( NULL != DuplicateFileObject ){

        //
        // destroy the newly created file object
        //
        OcCrProcessFileObjectRemovedFromHash( FileObject );

        //
        // use the object found in the hash,
        // the found object has been refrenced in 
        // OcHsFindContextByKeyValue, so there is no need
        // to reference it again
        //
        FileObject = DuplicateFileObject;
    }

__exit:

    if( !NT_SUCCESS( RC ) ){

        //
        // something went wrong, undo all
        //

        if( TRUE == InsertedInHash ){

            ASSERT( NULL != FileObject );

            //
            // file object was refrenced in OcObCreateObject
            // and after inserting in the hash
            //
            ASSERT( OcObGetObjectReferenceCount( FileObject ) == 0x2 );

            OcCrProcessFileObjectCloseRequest( FileObject->SysFileObject );
        }

        //
        // file object referenced either in OcObCreateObject 
        // or in OcHsFindContextByKeyValue
        //
        if( NULL != FileObject ){
            //
            // set the file object state to SLOSED
            // else the delete function will complain
            //
            FileObject->FileObjectState = OC_FILE_OBJECT_CLOSED;
            OcObDereferenceObject( FileObject );
        }

    } else {

        ASSERT( NULL != FileObject );
        ASSERT( NULL != FileObject->ContextObject );
        ASSERT( FileObject->ContextObject->OpenedFileObjectCounter >= 0x1 && FileObject->ContextObject->OpenedFileObjectCounter < 0xFFFF );

        //
        // return the refrenced file object
        // the object was referenced either in 
        // OcObCreateObject or in OcHsFindContextByKeyValue,
        // also the object was refrenced by the hash manager
        // the latter refrenced will be removed when the 
        // file object is removed from the hash
        //
        ASSERT( CheckForDuplicate? OcObGetObjectReferenceCount( FileObject ) >= 0x2:
                                   ( (OcObGetObjectReferenceCount( FileObject ) == 0x2) && InsertedInHash ) );

        *RefOcFileObject = FileObject;
    }

    return RC;
}

//--------------------------------------------------------------

NTSTATUS
OcCrInsertFileObjectInContext(
    IN POC_FILE_OBJECT    FileObject
    )
{
    NTSTATUS              RC;
    POC_CONTEXT_OBJECT    ContextObject;
    OC_QUEUE_WAIT_BLOCK   WaitBlock;
    BOOLEAN               RepeatAttempt;

    do{

        RC = STATUS_SUCCESS;
        ContextObject = NULL;
        RepeatAttempt = FALSE;

        ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

        //
        // retrieve a file object context from the hash
        //
        ContextObject = OcHsFindContextByKeyValue( Global.PtrFoContextHashObject,
                                                   OcCrGetSysFileObjectContext( FileObject->SysFileObject ),
                                                   OcObReferenceObject );
        if( NULL != ContextObject ){

            ULONG    OldValue;

            //
            // increment context's opened file objects reference counter
            //
            do{

                OldValue = ContextObject->OpenedFileObjectCounter;
                //
                // to check for 0x0 I need an exact value
                //
                OldValue = InterlockedCompareExchange( &ContextObject->OpenedFileObjectCounter, OldValue, OldValue );
                if( 0x0 == OldValue ){

                    //
                    // the object is being removed from the hash,
                    // we must create the new one
                    //
                    OcObDereferenceObject( ContextObject );
                    ContextObject = NULL;
                    break;
                }
            } while( ( (ULONG)InterlockedCompareExchange( &ContextObject->OpenedFileObjectCounter, OldValue+0x1, OldValue ) ) != OldValue );

            ASSERT( ContextObject ? ( ContextObject->OpenedFileObjectCounter >= 0x1 && ContextObject->OpenedFileObjectCounter < 0xFFFF ): TRUE );

        }//if( NULL != ContextObject )

        //
        // do not use "else if"
        //
        if( NULL == ContextObject && NULL != FileObject->CreationInfoObject ){

            //
            // The context object doesn't exist but I have the creation info one,
            // this is a case of opening new data stream by sending 
            // IRP_MJ_CREATE request
            //

            //
            // create the new context object
            //
            RC = OcObCreateObject( &Global.OcFileContextObjectType,
                                   &ContextObject );
            if( !NT_SUCCESS( RC ) ){
                //
                // exit the main loop
                //
                break;
            }

            //
            // initialize the context object body
            //
            RtlZeroMemory( ContextObject, sizeof( *ContextObject ) );

#if DBG
            InitializeListHead( &ContextObject->FileListHead );
            KeInitializeSpinLock( &ContextObject->FileListSpinLock );
#endif//DBG

            ContextObject->SystemContext = OcCrGetSysFileObjectContext( FileObject->SysFileObject );
            ContextObject->OpenedFileObjectCounter = 0x1;
            OcQlInitializeQueueLock( &ContextObject->ContexQueueLock );

            //
            // refrence a creation info object, it will be derefrenced
            // when the context object is deleted
            //
            OcObReferenceObject( FileObject->CreationInfoObject );
            ContextObject->CreationInfoObject = FileObject->CreationInfoObject;

            //
            // synchronize the inserting in the hash with OcCrProcessFileObjectClosing,
            // remember that this is not a strict syncronization because there is 
            // a possibility that OcCrProcessFileObjectClosing decremented
            // the context reference counter to zero but had not acquired lock
            // before decrementing or has not yet acquired lock before 
            // dropping the context
            //
            OcLockFoContextHashLine( ContextObject->SystemContext, &WaitBlock );
            {// the start of hash line locking

                POC_CONTEXT_OBJECT    FoundContextObject;

                //
                // APCs are disabled except special kernel mode APCs
                //

                //
                // try to retrieve a duplicate file object context from the hash
                //
                FoundContextObject = OcHsFindContextByKeyValue( Global.PtrFoContextHashObject,
                                                           ContextObject->SystemContext,
                                                           NULL// only check whether the context already exists
                                                          );
                if( NULL != FoundContextObject ){

                    //
                    // so somebody either sneaked in and insert the context or has not yet removed the old one
                    // the simplest way is to repeat all, this simplifies semantic
                    //

                    RepeatAttempt = TRUE;

                } else {

                    //
                    // insert the new context in the hash, reference the inserted object
                    //
                    RC = OcHsInsertContextInHash( Global.PtrFoContextHashObject,
                                                  ContextObject->SystemContext,
                                                  ContextObject,
                                                  OcObReferenceObject );
                }

            }// the end of hash line locking
            OcReleaseFoContextHashLine( &WaitBlock );

            if( TRUE == RepeatAttempt || !NT_SUCCESS( RC ) ){

                ASSERT( 0x1 == ContextObject->OpenedFileObjectCounter );

                //
                // the object has been created here, reset its reference counter
                //
                ContextObject->OpenedFileObjectCounter = 0x0;

                //
                // the object has been referenced in OcObCreateObject,
                // derefrence and destroy it
                //
                OcObDereferenceObject( ContextObject );
                ContextObject = NULL;
            }

        } else if( NULL == ContextObject && NULL == FileObject->CreationInfoObject ){

            //
            // So both the context and the creation info objects don't exist,
            // At this moment I do not process such cases, return an error.
            // This might be a case of a pure stream file object for which
            // user's objects have not been opened, such objects are usually
            // used for processing internal FSD's data streams.
            //
            RC = STATUS_OBJECT_PATH_NOT_FOUND;
        }

    }while( TRUE == RepeatAttempt );

    //
    // the exit from the function
    //
    if( NT_SUCCESS( RC ) ){
        //
        // the context object has been referenced as an object
        // and the opened file objects counter has been incremented
        //
        ASSERT( ContextObject );
        //
        // at least one FO is opened
        //
        ASSERT( ContextObject->OpenedFileObjectCounter >= 0x1 );
        //
        // the object was referenced when it was inserted in hash
        // and when it was retrived from the hash
        //
        ASSERT( OcObGetObjectReferenceCount( ContextObject ) >= 0x2 );

        //
        // the ContextObject has been refrenced either in OcHsFindContextByKeyValue
        // or in OcObCreateObject
        //
        FileObject->ContextObject = ContextObject;

#if DBG
        //
        // insert the file in the context's file objects list
        //
        {
            KIRQL    OldIrql;

            KeAcquireSpinLock( &FileObject->ContextObject->FileListSpinLock, &OldIrql );
            InsertTailList( &FileObject->ContextObject->FileListHead, &FileObject->FileListEntry );
            KeReleaseSpinLock( &FileObject->ContextObject->FileListSpinLock, OldIrql );
        }
#endif //DBG

    } else {

        //
        // there was an error
        //

        //
        // dereference the context referenced either in OcHsFindContextByKeyValue or
        // in OcObCreateObject
        //
        if( NULL != ContextObject )
            OcObDereferenceObject( ContextObject );
    }

    return RC;
}

//--------------------------------------------------------------

VOID
OcCrDecrementContextOpenedFileObjectsCounter(
    IN POC_CONTEXT_OBJECT     ContextObject
    )
{
    OC_QUEUE_WAIT_BLOCK    WaitBlock;
    BOOLEAN                RemoveBarrier = FALSE;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( ContextObject->OpenedFileObjectCounter >= 0x1 && ContextObject->OpenedFileObjectCounter < 0xFFFFF );

    if( 0x1 == ContextObject->OpenedFileObjectCounter ){
        //
        // with the great probability the context is about to be removed,
        // so lock the hash line for this context to help OcCrInsertFileObjectInContext 
        // in avoiding race conditions
        //
        OcInsertFoContextHashLineBarrier( ContextObject->SystemContext, &WaitBlock );
        RemoveBarrier = TRUE;
        //
        // APCs are disabled except special kernel mode APCs
        //
    }

    //
    // decrement the opened file objects counter
    //
    if( 0x0 == InterlockedDecrement( &ContextObject->OpenedFileObjectCounter ) ){

        if( FALSE == RemoveBarrier ){

            //
            // the context is being removed
            //
            OcInsertFoContextHashLineBarrier( ContextObject->SystemContext, &WaitBlock );
            RemoveBarrier = TRUE;
        }

        //
        // APCs are disabled except special kernel mode APCs
        //

        //
        // drop the context, i.e. remove it from the hash etc.
        //
        OcCrDropFoContext( ContextObject );
    }

    if( RemoveBarrier ){

        OcRemoveFoContextHashLineBarrier( &WaitBlock );
    }

}

//--------------------------------------------------------------

VOID
OcCrProcessContextForClosedFile(
    IN POC_FILE_OBJECT    FileObject
    )
    /*
    the function processes the context for the file object 
    that is about to being closed
    */
{
    POC_CONTEXT_OBJECT     ContextObject;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_FILE_OBJECT_CLOSED == FileObject->FileObjectState );

    ContextObject = FileObject->ContextObject;
    if( NULL == ContextObject )
        return;

    //
    // decrement the opened file objects counter, if it drops to 
    // zero remove the context from the hash, but
    // retain the context object in memory throught the 
    // file object's reference
    //
    OcCrDecrementContextOpenedFileObjectsCounter( ContextObject );

}

//--------------------------------------------------------------

VOID
NTAPI
OcCrProcessFoContextRemovedFromHash(
    IN POC_CONTEXT_OBJECT    ContextObject
    )
{
    OcObDereferenceObject( ContextObject );
}

//--------------------------------------------------------------

VOID
OcCrDropFoContext(
    IN POC_CONTEXT_OBJECT    ContextObject
    )
    /*
    this function is called when the opened file object count
    drops to zero
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( 0x0 == ContextObject->OpenedFileObjectCounter );

    //
    // remove the object from the hash, derefrence it afer removing
    //
    OcHsRemoveContextByKeyValue( Global.PtrFoContextHashObject,
                                 ContextObject->SystemContext,
                                 OcCrProcessFoContextRemovedFromHash );
}

//--------------------------------------------------------------

VOID
NTAPI
OcCrDeleteFileObject(
    IN POC_FILE_OBJECT    FileObject
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_FILE_OBJECT_CLOSED == FileObject->FileObjectState );

    if( NULL != FileObject->CreationInfoObject )
        OcObDereferenceObject( FileObject->CreationInfoObject );

    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( FileObject->CreationInfoObject );

    //
    // the context object was referenced when was assigned to 
    // the file object in OcCrInsertFileObjectInContext,
    // the context's opened file objects counter was decremented
    // in OcCrProcessContextForClosedFile while IRP_MJ_CLOSE
    // was being processed
    //
    if( NULL != FileObject->ContextObject ){

#if DBG
        //
        // remove the file from the context's file objects list
        //
        {
            KIRQL    OldIrql;

            KeAcquireSpinLock( &FileObject->ContextObject->FileListSpinLock, &OldIrql );
            RemoveEntryList( &FileObject->FileListEntry );
            KeReleaseSpinLock( &FileObject->ContextObject->FileListSpinLock, OldIrql );
        }
#endif //DBG

        OcObDereferenceObject( FileObject->ContextObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( FileObject->ContextObject );
    }
}

//--------------------------------------------------------------

NTSTATUS
NTAPI
OcCrFreeFileObjectNameBufferWR(
    IN ULONG_PTR    FileNameBuffer//FileObjectCreateInfo->FileName.Buffer
    )
{
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    ExFreePoolWithTag( (PVOID)FileNameBuffer, Global.OcFileCreateInfoObjectType.Tag );

    return STATUS_SUCCESS;
}

//--------------------------------------------------------------

VOID
NTAPI
OcCrDeleteFileObjectInfo(
    IN POC_FILE_OBJECT_CREATE_INFO    FileObjectCreateInfo
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( IsListEmpty( &FileObjectCreateInfo->ListEntry ) );

    //
    // I must free the file name buffer allocated from the 
    // Paged System Pool, so I am posting the function in a worker thread,
    // If this postponing fails the memory will not be freed.
    //
    if( KeGetCurrentIrql() > APC_LEVEL && 
        NULL != FileObjectCreateInfo->FileName.Buffer && 
        NULL != Global.ThreadsPoolObject ){

            POC_WORK_ITEM_LIST_OBJECT    PtrWorkItem;

            PtrWorkItem = OcTplReferenceSharedWorkItemList( Global.ThreadsPoolObject );
            ASSERT( NULL != PtrWorkItem );
            if( NULL != PtrWorkItem ){

                //
                // the memory for the object will be freed after return from 
                // OcCrDeleteDeviceObject so it can't be sent in a worker routine as
                // a parameter, one of the solution was when the objects manager
                // posts all objects deletion called at elevated IRQL to a worker thread,
                // but this was not done in order to simplify the object manager, because
                // most of the objects are tolerant to calling at elevated IRQL
                //
                OcWthPostWorkItemParam1( PtrWorkItem,
                                         FALSE,
                                         (Param1SysProc)OcCrFreeFileObjectNameBufferWR,
                                         (ULONG_PTR)FileObjectCreateInfo->FileName.Buffer
                                        );

                OcObDereferenceObject( PtrWorkItem );
            }

            //
            // do not pay attention on errors
            //
            FileObjectCreateInfo->FileName.Length = FileObjectCreateInfo->FileName.MaximumLength = 0x0;
            FileObjectCreateInfo->FileName.Buffer = NULL;
    }

    //
    // check the IRQL, the postponing might have failed
    //
    if( KeGetCurrentIrql() <= APC_LEVEL && 
        NULL != FileObjectCreateInfo->FileName.Buffer ){

        OcCrFreeFileObjectNameBufferWR( (ULONG_PTR)FileObjectCreateInfo->FileName.Buffer );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( FileObjectCreateInfo->FileName.Buffer );
    }

    if( NULL != FileObjectCreateInfo->ObjectOnWhichFileIsOpened ){

        OcObDereferenceObject( FileObjectCreateInfo->ObjectOnWhichFileIsOpened );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( FileObjectCreateInfo->ObjectOnWhichFileIsOpened );
    }

}

//--------------------------------------------------------------

VOID
NTAPI
OcCrDeleteContextObject(
    IN POC_CONTEXT_OBJECT    FileContextObject
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( IsListEmpty( &FileContextObject->FileListHead ) );
    ASSERT( 0x0 == FileContextObject->OpenedFileObjectCounter );

    if( NULL != FileContextObject->CreationInfoObject ){

        OcObDereferenceObject( FileContextObject->CreationInfoObject );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( FileContextObject->CreationInfoObject );
    }

}

//--------------------------------------------------------------

VOID
OcCrDeleteContextObjectType(
    IN POC_OBJECT_TYPE    FileContextObjectType
    )
{
    UNREFERENCED_PARAMETER( FileContextObjectType );

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    KeSetEvent( &Global.OcContextObjectTypeUninitializationEvent,
                IO_DISK_INCREMENT,
                FALSE );
}

//--------------------------------------------------------------

VOID
NTAPI
OcCrProcessFileObjectRemovedFromHash(
    IN POC_FILE_OBJECT    FileObject
    )
    /*
    this function processes the file object that is
    about to being closed by the system or purged from the hash
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OC_FILE_OBJECT_OPENED == FileObject->FileObjectState );

    //
    // mark as closed, so SysFileObject field becomes invalid
    //
    FileObject->FileObjectState = OC_FILE_OBJECT_CLOSED;

    //
    // decrement the opened file objects counter due to the close
    //
    OcCrProcessContextForClosedFile( FileObject );

    //
    // the file object was referenced after inserting in the hash
    //
    ASSERT( OcObGetObjectReferenceCount( FileObject ) >= 0x1 );
    OcObDereferenceObject( FileObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( FileObject );

}

//--------------------------------------------------------------

VOID
OcCrProcessFileObjectCloseRequest(
    IN PFILE_OBJECT    SysFileObject
    )
    /*
    this function processes IRP_MJ_CLOSE request
    */
{
    //
    // remove the file object from the hash,
    // call OcCrProcessFileObjectRemovedFromHash
    // afer removing
    //
    OcHsRemoveContextByKeyValue( Global.PtrFoHashObject,
                                 (ULONG_PTR)SysFileObject,
                                 OcCrProcessFileObjectRemovedFromHash );
}

//--------------------------------------------------------------

NTSTATUS
OcCrInsertFileCreateInfoObjectInDeviceList(
    IN POC_FILE_OBJECT_CREATE_INFO    FileCreateInfo
    )
    /*
    the FileCreateInfo is inserted in the FileCreateInfo->ObjectOnWhichFileIsOpened
    object list( CreateRequestsListHead ), 
    the FileCreateInfo->ObjectOnWhichFileIsOpened remove lock is acquired and 
    the info object is refrenced, so the caller must remove object from the list, 
    dereference it and release the VolumeDevice remove lock, all this is done by 
    OcCrRemoveFileCreateInfoObjectFromDeviceList
    */
{
    NTSTATUS             RC;
    KIRQL                OldIrql;
    POC_DEVICE_OBJECT    VolumeDevice = FileCreateInfo->ObjectOnWhichFileIsOpened;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( IsListEmpty( &FileCreateInfo->ListEntry ) );

    //
    // before inserting the object in the list acquire the remove lock,
    // the lock will be released when the info object is removed from the list
    //
    RC = OcRlAcquireRemoveLock( &VolumeDevice->RemoveLock.Common );
    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // reference the info object before inserting in the list, 
    // the object will be dereferenced when it is removed from the list
    //
    OcObReferenceObject( FileCreateInfo );

    OcRwAcquireLockForWrite( &VolumeDevice->DeviceRequests.CreateRequestsListRwLock, &OldIrql );
    {// start of the create requests list's lock
        InsertTailList( &VolumeDevice->DeviceRequests.CreateRequestsListHead, 
                        &FileCreateInfo->ListEntry );
    }// end of the create requests list's lock
    OcRwReleaseWriteLock( &VolumeDevice->DeviceRequests.CreateRequestsListRwLock, OldIrql );

#if DBG
    FileCreateInfo->DevObjWhichListContainsThisObject = VolumeDevice;
#endif//DBG

    return RC;
}

//--------------------------------------------------------------

VOID
OcCrRemoveFileCreateInfoObjectFromDeviceList(
    IN POC_FILE_OBJECT_CREATE_INFO    FileCreateInfo
    )
    /*
    this function undoes all that has been done by OcCrInsertFileCreateInfoObjectInDeviceList
    */
{
    KIRQL                OldIrql;
    POC_DEVICE_OBJECT    VolumeDevice = FileCreateInfo->ObjectOnWhichFileIsOpened;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( VolumeDevice == FileCreateInfo->DevObjWhichListContainsThisObject );
    ASSERT( !IsListEmpty( &FileCreateInfo->ListEntry ) );

    //
    // for safety - check whether the object is in any list
    //
    if( IsListEmpty( &FileCreateInfo->ListEntry ) )
        return;

    //
    // the object is in the list
    //

    OcRwAcquireLockForWrite( &VolumeDevice->DeviceRequests.CreateRequestsListRwLock, &OldIrql );
    {// start of the create requests list's lock
        RemoveEntryList( &FileCreateInfo->ListEntry );
    }// end of the create requests list's lock
    OcRwReleaseWriteLock( &VolumeDevice->DeviceRequests.CreateRequestsListRwLock, OldIrql );

#if DBG
    FileCreateInfo->DevObjWhichListContainsThisObject = NULL;
#endif//DBG

    //
    // mark the object as removed from the list
    //
    InitializeListHead( &FileCreateInfo->ListEntry );

    //
    // release the volume's remove lock, acquired when the info 
    // object was inserted in the list
    //
    OcRlReleaseRemoveLock( &VolumeDevice->RemoveLock.Common );

    //
    // dereference the object, it was referenced when it was inserted in the list
    //
    OcObDereferenceObject( FileCreateInfo );

}

//--------------------------------------------------------------

#define OC_STANDARD_PATH_SEPARATOR    ( L'\\' )
#define OC_STANDARD_PATH_SEPARATOR_STRING    ( L"\\" )

NTSTATUS
OcCrGetFileObjectCreationInfo(
    IN const struct _OC_NODE_CTX*    CommonContext,
    IN PFLT_CALLBACK_DATA    CallbackData,
    IN POC_DEVICE_OBJECT     VolumeDevice,
    IN OC_REQUEST_TYPE       RequestType,
    OUT POC_FILE_OBJECT_CREATE_INFO*    PtrReferencedFileInfoObject
    )
    /*
    the function can be called only for file objects received in IRP_MJ_CREATE
    path, because the RelativeFileObject is uded to create the full file name

    if RequestFromMinifilter is TRUE then the caller is the minifilter
    else the caller is the FSDs hooker

    if the call was successfull the caller must dereference *ReferencedFileInfoObject 
    object when it's not longer needed
    */
{
    NTSTATUS                RC;
    POC_FILE_OBJECT_CREATE_INFO    ReferencedFileInfoObject = NULL;
    PFILE_OBJECT            FileObject;
    POC_FILE_OBJECT         RelatedFileObject = NULL;
    UNICODE_STRING          PathSeparator;
    UNICODE_STRING          FullFileName;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( IRP_MJ_CREATE == CallbackData->Iopb->MajorFunction );

    //
    // initialize the standard path separator
    //
    PathSeparator.Buffer = OC_STANDARD_PATH_SEPARATOR_STRING;
    PathSeparator.Length = sizeof( OC_STANDARD_PATH_SEPARATOR );
    PathSeparator.MaximumLength = PathSeparator.Length;

    FullFileName.Buffer = NULL;

    FileObject = CallbackData->Iopb->TargetFileObject;

    ASSERT( NULL != FileObject );

    //
    // must be IRP_MJ_CREATE path
    //
    ASSERT( NULL == FileObject->FsContext );
    ASSERT( NULL != VolumeDevice->DevicePropertyObject );
    ASSERT( !( NULL != VolumeDevice->DevicePropertyObject && 
               en_GUID_DEVCLASS_MOUSE != VolumeDevice->DevicePropertyObject->Header.SetupClassGuidIndex &&
               en_GUID_DEVCLASS_KEYBOARD != VolumeDevice->DevicePropertyObject->Header.SetupClassGuidIndex &&
               NULL != FileObject->FsContext2 ) );

    //
    // if this is a call from the FSDs hooker, then try to find
    // a file info created by minifilter's callback, also do this
    // for a request from a device w/o mounted FSD
    //
    if( ( OC_REQUEST_FROM_HOOKED_FSD == RequestType && OC_FSD_MF_REGISTERED == Global.FsdMinifilter.State ) ||
        OC_REQUEST_FROM_DEVICE == RequestType ){

        KIRQL    OldIrql;

        OcRwAcquireLockForRead( &VolumeDevice->DeviceRequests.CreateRequestsListRwLock, &OldIrql );
        {// start of the create requests list's lock

            PLIST_ENTRY                    request;
            POC_FILE_OBJECT_CREATE_INFO    PtrFOI;

            for( request = VolumeDevice->DeviceRequests.CreateRequestsListHead.Blink; 
                 request != &VolumeDevice->DeviceRequests.CreateRequestsListHead;
                 request = request->Blink ){

                     PtrFOI = CONTAINING_RECORD( request, OC_FILE_OBJECT_CREATE_INFO, ListEntry );
                     if( PtrFOI->SysFileObject == FileObject ){

                         //
                         // the minifilter have already created the create info
                         // object, so use it
                         //
                         ReferencedFileInfoObject = PtrFOI;
                         OcObReferenceObject( ReferencedFileInfoObject );
                         break;
                     }
            }

        }// end of the create requests list's lock
        OcRwReleaseReadLock( &VolumeDevice->DeviceRequests.CreateRequestsListRwLock, OldIrql );
    }

    if( NULL != ReferencedFileInfoObject ){

        //
        // the object has been found, I am lucky!
        //
        RC = STATUS_SUCCESS;

    } else {

        //
        // create a new object
        //

        PUNICODE_STRING   RelatedFileName = NULL;
        PUNICODE_STRING   VolumeObjectName = NULL;
        USHORT            VolumeNameLength = 0x0;

        //
        // get the volume device object's name
        //
        if( VolumeDevice->DeviceNameInfo )
            VolumeObjectName = &VolumeDevice->DeviceNameInfo->Name;
        else if( VolumeDevice->Pdo && VolumeDevice->Pdo->DeviceNameInfo )
            VolumeObjectName = &VolumeDevice->Pdo->DeviceNameInfo->Name;
        else if( VolumeDevice->DevicePropertyObject && VolumeDevice->DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer )
            VolumeObjectName = &VolumeDevice->DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName;
        else if( VolumeDevice->Pdo && VolumeDevice->Pdo->DevicePropertyObject && 
                 VolumeDevice->Pdo->DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer )
            VolumeObjectName = &VolumeDevice->Pdo->DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName;

        ASSERT( NULL != VolumeObjectName );

        if( NULL != VolumeObjectName ){

            VolumeNameLength = VolumeObjectName->Length;
            ASSERT( L'\0' != VolumeObjectName->Buffer[ VolumeNameLength/sizeof( WCHAR ) - 0x1 ] );
        }

        //
        // get the related file object's name
        //
        if( NULL != FileObject->RelatedFileObject ){

            //
            // try to retrieve the related file object
            //
            RelatedFileObject = OcHsFindContextByKeyValue( Global.PtrFoHashObject,
                                                           (ULONG_PTR)FileObject->RelatedFileObject,
                                                           OcObReferenceObject );
            if( NULL != RelatedFileObject ){

                //
                // Found!
                //

                RelatedFileName = &RelatedFileObject->CreationInfoObject->FileName;

                //
                // the related file object's name already contains the volume name
                //
                ASSERT( VolumeNameLength <= RelatedFileName->Length );
                VolumeObjectName = NULL;
                VolumeNameLength = RelatedFileObject->CreationInfoObject->VolumeNameLength;

            } else {

                //
                // TO DO use FltMgr's FltGetFileNameInformation to get the
                // realted file object name, because the FO's FileName 
                // might be invalid because has been changed by FSD
                //

                //
                // use the relative to the volume name
                //
                RelatedFileName = &FileObject->RelatedFileObject->FileName;
            }
        }

        //
        // Compose a full qualified file name, I insert the separator myself if
        // it is not present. Actually, an underlying FSD should fail a request
        // if a file name doesn't start from the separator and there is no related
        // file object or if a file name starts from the separator and a related 
        // file object is present and doesn't describe a direct disk opening. But for 
        // logging purposes I need a name that a caller might have supposed, 
        // so I insert the separator myself.
        //

        //
        // get the length and allocate a buffer
        //
        FullFileName.Length = 0x0;
        FullFileName.MaximumLength = ( VolumeObjectName? ( VolumeObjectName->Length + PathSeparator.Length ): 0x0 ) +
                                     ( RelatedFileName? ( RelatedFileName->Length + PathSeparator.Length ): 0x0 ) +
                                     FileObject->FileName.Length + 
                                     sizeof( L'\0' );

        FullFileName.Buffer = ExAllocatePoolWithTag( PagedPool, 
                                                     FullFileName.MaximumLength,
                                                     Global.OcFileCreateInfoObjectType.Tag );
        if( NULL == FullFileName.Buffer ){

            RC = STATUS_INSUFFICIENT_RESOURCES;
            goto __exit;
        }

        //
        // compose the name from ( volume name ) + ( related file object ) + ( file name )
        //
        if( VolumeObjectName && 0x0 != VolumeObjectName->Length )
            RtlAppendUnicodeStringToString( &FullFileName, VolumeObjectName );

        //
        // add the related file object name
        //
        if( RelatedFileName && 
            0x0 != RelatedFileName->Length && 
            L'\0' != RelatedFileName->Buffer[ 0x0 ] ){

            //
            // add the separator if needed
            //
            if( 0x0 != FullFileName.Length && 
                OC_STANDARD_PATH_SEPARATOR != FullFileName.Buffer[ FullFileName.Length/sizeof( WCHAR ) - 0x1 ] && 
                OC_STANDARD_PATH_SEPARATOR != RelatedFileName->Buffer[ 0x0 ] ){

                RtlAppendUnicodeStringToString( &FullFileName, &PathSeparator );
            }

            RtlAppendUnicodeStringToString( &FullFileName, RelatedFileName );
        }

        //
        // add the trailing file name
        //
        if( 0x0 != FileObject->FileName.Length && 
            L'\0' != FileObject->FileName.Buffer[ 0x0 ] ){

            //
            // add the separator if needed
            //
            if( 0x0 != FullFileName.Length && 
                OC_STANDARD_PATH_SEPARATOR != FullFileName.Buffer[ FullFileName.Length/sizeof( WCHAR ) - 0x1 ] && 
                OC_STANDARD_PATH_SEPARATOR != FileObject->FileName.Buffer[ 0x0 ] ){

                    RtlAppendUnicodeStringToString( &FullFileName, &PathSeparator );
            }

            RtlAppendUnicodeStringToString( &FullFileName, &FileObject->FileName );
        }

        ASSERT( ( 0x0 != VolumeNameLength ) && ( FullFileName.Length >= VolumeNameLength ) );

        //
        // if the target directory should be opened then trancate the name,
        // the truncated name is the actual path that the create operation opens
        //
        if( OcIsFlagOn( CallbackData->Iopb->OperationFlags, SL_OPEN_TARGET_DIRECTORY ) && 
            0x0 != FullFileName.Length ){

            //
            // move through all trailing separators
            //
            while( FullFileName.Length > VolumeNameLength && 
                   OC_STANDARD_PATH_SEPARATOR == FullFileName.Buffer[ FullFileName.Length/sizeof( WCHAR ) - 0x1 ] ){

                FullFileName.Length -= sizeof( WCHAR );
            }

            //
            // go to the directory containing the file
            //
            while( FullFileName.Length > VolumeNameLength && 
                   OC_STANDARD_PATH_SEPARATOR != FullFileName.Buffer[ FullFileName.Length/sizeof( WCHAR ) - 0x1 ] ){

                FullFileName.Length -= sizeof( WCHAR );
            }

        }//if( OcIsFlagOn( CallbackData->Iopb->OperationFlags, SL_OPEN_TARGET_DIRECTORY )

        ASSERT( VolumeNameLength <= FullFileName.Length );

        //
        // now create the file object
        //

        RC = OcObCreateObject( &Global.OcFileCreateInfoObjectType,
                               &ReferencedFileInfoObject );

        if(!NT_SUCCESS( RC ) )
            goto __exit;

        //
        // initialize the object's body
        //
        RtlZeroMemory( ReferencedFileInfoObject, sizeof( *ReferencedFileInfoObject ) );
#if DBG
        ReferencedFileInfoObject->Signature = OC_FILE_OBJECT_CREATE_INFO_SIGNATURE;
#endif//DBG
        ReferencedFileInfoObject->SysFileObject = FileObject;
        InitializeListHead( &ReferencedFileInfoObject->ListEntry );

        //
        // refrence the device on which the file is being opened
        // save the pointer, the object will be dereferenced 
        // when the info object is deleted
        //
        OcObReferenceObject( VolumeDevice );
        ReferencedFileInfoObject->ObjectOnWhichFileIsOpened = VolumeDevice;

        //
        // swap buffers, the byffer will be freed when the creation
        // info object is deleted
        //
        ReferencedFileInfoObject->FileName = FullFileName;
        FullFileName.Buffer = NULL;

        ReferencedFileInfoObject->VolumeNameLength = VolumeNameLength;

    }// end of the new object creating

    //
    // now I have a referenced file info object
    //

__exit:

    if( NT_SUCCESS( RC ) ){

        //
        // if the request should be shadowed - remeber this
        //
        if( OcIsOperationShadowedAsWriteRequest( &CommonContext->SecurityParameters ) )
            ReferencedFileInfoObject->Flags.ShadowWriteRequests = 0x1;

        if( OcIsOperationShadowedAsReadRequest( &CommonContext->SecurityParameters ) )
            ReferencedFileInfoObject->Flags.ShadowReadRequests = 0x1;

        *PtrReferencedFileInfoObject = ReferencedFileInfoObject;

    } else {

        if( NULL != ReferencedFileInfoObject ){

            OcObDereferenceObject( ReferencedFileInfoObject );
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ReferencedFileInfoObject );

        }//if( NULL != ReferencedFileInfoObject )

    }

    if( NULL != RelatedFileObject )
        OcObDereferenceObject( RelatedFileObject );

    if( NULL != FullFileName.Buffer )
        ExFreePoolWithTag( FullFileName.Buffer, Global.OcFileCreateInfoObjectType.Tag );

    return RC;
}

//--------------------------------------------------------------

VOID
OcCrCheckSharedCacheMapFileObject(
    IN POC_FILE_OBJECT    FileObject
    )
    /*
    This function gets a file object that backs 
    file object's shared cache map and 
    initializes it.

    Use this function with an extream caution! You must
    be sure that the shared cache map's object is valid
    and a close request has not been initiated when this 
    function will be working
    */
{
    NTSTATUS           RC;
    PFILE_OBJECT       CacheMapSysFileObject;
    POC_FILE_OBJECT    CacheMapOcFileObject;

    if( 0x1 == FileObject->ContextObject->Flags.SharedCacheMapFoChecked || 
        NULL == FileObject->SysFileObject->SectionObjectPointer )
        return;

    //
    // process a case when a Shared Cache Map has been intialized and
    // a stream file object is used to support it
    //
    CacheMapSysFileObject = CcGetFileObjectFromSectionPtrs( FileObject->SysFileObject->SectionObjectPointer );
    if( NULL == CacheMapSysFileObject )
        return;

    //
    // remember that shared cache map's file object is checked
    //
    FileObject->ContextObject->Flags.SharedCacheMapFoChecked = 0x1;

    RC = OcCrProcessFileObjectCreating( CacheMapSysFileObject,
                                        NULL,
                                        TRUE,
                                        &CacheMapOcFileObject );
    if( NT_SUCCESS( RC ) ){

        //
        // derefrence the object referenced in OcCrProcessFileObjectCreating
        // the hash manager will retain the object through its reference
        //
        OcObDereferenceObject( CacheMapOcFileObject );

    }//if( NT_SUCCESS( RC ) )

}

//-------------------------------------------------------------

NTSTATUS
OcCrProcessFoCreateRequest(
    IN const struct _OC_NODE_CTX*    CommonContext,
    IN PFLT_CALLBACK_DATA    Data,
    IN POC_DEVICE_OBJECT     PtrOcDeviceObject,
    IN OC_REQUEST_TYPE       RequestType,
    OUT POC_FILE_OBJECT_CREATE_INFO*    OutRefCreationInfo
    )
    /*
    in case of success the function returns a refrenced
    file object cration info object in RefCreationInfo, 
    the caller must dereference this object when it is
    not longer needed
    */
{
    NTSTATUS    RC;
    POC_FILE_OBJECT_CREATE_INFO    RefCreationInfo = NULL;

    RC = OcCrGetFileObjectCreationInfo( CommonContext,
                                        Data,
                                        PtrOcDeviceObject,
                                        RequestType,
                                        &RefCreationInfo );

    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) ){

        return RC;
    }//if( !NT_SUCCESS( RC ) )

    //
    // insert in the list of file info objects if this 
    // is a call from the minifilter, if this is a call from
    // the FSD hooker then there is no need to insert the
    // information in the list, because I won't see any 
    // create request for this file anymore
    //
    ASSERT( ( OC_REQUEST_FROM_MINIFILTER == RequestType )? (IsListEmpty( &RefCreationInfo->ListEntry )): TRUE );
    if( ( TRUE == RequestType || OC_REQUEST_FROM_DEVICE == RequestType ) && IsListEmpty( &RefCreationInfo->ListEntry ) ){

        ASSERT( IsListEmpty( &RefCreationInfo->ListEntry ) );

        RC = OcCrInsertFileCreateInfoObjectInDeviceList( RefCreationInfo );
        ASSERT( NT_SUCCESS( RC ) );
        if( !NT_SUCCESS( RC ) ){

            //
            // delete the created info object
            //
            OcObDereferenceObject( RefCreationInfo );
            return RC;
        }//if( !NT_SUCCESS( RC ) )
    }

    //
    // check the reference counter
    //
#if DBG
    if( OC_REQUEST_FROM_MINIFILTER == RequestType ){
        //
        // the creation info object has been already referenced in OcCrGetFileObjectCreationInfo
        // and in OcCrInsertFileCreateInfoObjectInDeviceList
        // so its reference count is 0x2 and this is a new object
        //
        ASSERT( 0x2 == OcObGetObjectReferenceCount( RefCreationInfo ) );
    } else if( OC_FSD_MF_REGISTERED != Global.FsdMinifilter.State && OC_REQUEST_FROM_DEVICE != RequestType ){
        //
        // this is a new object with reference coun set to 0x1 in OcCrGetFileObjectCreationInfo
        // and the object has not been inserted in the list
        //
        ASSERT( 0x1 == OcObGetObjectReferenceCount( RefCreationInfo ) );
    } else {
        //
        // this is an object that has been created before 
        // calling OcCrGetFileObjectCreationInfo and 
        // has been found by OcCrGetFileObjectCreationInfo
        // or this is a request to a device w/o mounted FSD
        //
        ASSERT( 0x1 <= OcObGetObjectReferenceCount( RefCreationInfo ) );
    }
#endif//DBG

    *OutRefCreationInfo = RefCreationInfo;

    return RC;
}

//--------------------------------------------------------------

NTSTATUS
OcCrProcessFoCreateRequestCompletion(
    IN PIO_STATUS_BLOCK    IoStatusBlock,
    IN POC_FILE_OBJECT_CREATE_INFO    CreationInfo,
    IN PFILE_OBJECT    TargetFileObject,
    IN OC_REQUEST_TYPE    RequestType
    )
{
    NTSTATUS           RC = STATUS_SUCCESS;
    POC_FILE_OBJECT    RefFileObject = NULL;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    //
    // remove the creation info object from the 
    // device's list where it was inserted in the preoperation
    // callback to facilitate info object search by the 
    // FSD hooker
    //
    if( !IsListEmpty( &CreationInfo->ListEntry ) )
        OcCrRemoveFileCreateInfoObjectFromDeviceList( CreationInfo );

    ASSERT( IsListEmpty( &CreationInfo->ListEntry ) );

    //
    // if the creation was successful create the file object
    //
    if( !NT_SUCCESS( IoStatusBlock->Status ) || STATUS_REPARSE == IoStatusBlock->Status ){

        //
        // nothing to do
        //
        return STATUS_SUCCESS;
    }

    if( OC_REQUEST_FROM_MINIFILTER == RequestType || OC_REQUEST_FROM_DEVICE == RequestType ){

        //
        // if the request is from the minifilter when check for a file object 
        // in the hash, because the FSD or lower device hooker with a greate probability 
        // has already created a file object, because its completion routines 
        // is called before the minifilter callback or upper devices' callbacks
        //
        RefFileObject = OcHsFindContextByKeyValue( Global.PtrFoHashObject,
                                                   (ULONG_PTR)TargetFileObject,
                                                   OcObReferenceObject );
    }

    if( NULL == RefFileObject ){

        RC = OcCrProcessFileObjectCreating( TargetFileObject,
                                            CreationInfo,
                                            FALSE,
                                            &RefFileObject );
        ASSERT( NT_SUCCESS( RC ) );
    }

    if( NT_SUCCESS( RC ) ){

        //
        // copy the shadow flags
        //
        if( 0x1 == CreationInfo->Flags.ShadowWriteRequests && 
            0x0 == RefFileObject->ContextObject->Flags.ShadowWriteRequests ){

            RefFileObject->ContextObject->Flags.ShadowWriteRequests = 0x1;
        }

        if( 0x1 == CreationInfo->Flags.ShadowReadRequests && 
            0x0 == RefFileObject->ContextObject->Flags.ShadowReadRequests ){

            RefFileObject->ContextObject->Flags.ShadowReadRequests = 0x1;
        }

        //
        // check a file object backing a cache map for FSD initialized FO
        //
        if( OC_REQUEST_FROM_MINIFILTER == RequestType || OC_REQUEST_FROM_HOOKED_FSD == RequestType )
            OcCrCheckSharedCacheMapFileObject( RefFileObject );

        //
        // remember that all file object on the
        // device must be spied on
        //
        if( 0x0 == CreationInfo->ObjectOnWhichFileIsOpened->Flags.SpyFileObjects )
            CreationInfo->ObjectOnWhichFileIsOpened->Flags.SpyFileObjects = 0x1;

        //
        // if the file should be shadowed then send the notification
        //
        if( 0x1 == RefFileObject->ContextObject->Flags.ShadowWriteRequests || 
            0x1 == RefFileObject->ContextObject->Flags.ShadowReadRequests ){
            //
            // TO DO, write the information in the shadow file
            //
        }

        //
        // dereference the file object
        //
        OcObDereferenceObject( RefFileObject );


    } else {

        //
        // there was an errors
        // cancel the opening
        // TO DO
        //
    }

    return RC;
}

//--------------------------------------------------------------

POC_FILE_OBJECT
OcCrRetriveReferencedFileObject(
    IN PFILE_OBJECT    SystemFileObject
    )
    /*
    the function returns a referenced file object,
    ac caller must dereference it when it is not longer needed
    */
{
    POC_FILE_OBJECT    RefFileObject;

    RefFileObject = OcHsFindContextByKeyValue( Global.PtrFoHashObject,
                                               (ULONG_PTR)SystemFileObject,
                                               OcObReferenceObject );

    return RefFileObject;
}

//--------------------------------------------------------------

