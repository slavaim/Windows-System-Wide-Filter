/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
08.06.2007 ( June )
 Start
*/

/*
this file contains the shadow file management
*/

//--------------------------------------------------

typedef struct _OC_CR_SHADOW_FILE{

    //
    // the handle to the FSD file
    //
    HANDLE           FileHandle;

    //
    // the current file size
    //
    ULARGE_INTEGER   CurrentFileSize;

    //
    // the maximum allowed file size
    //
    ULARGE_INTEGER   MaximumFileSize;

    //
    // the current pointer, used if the mapping is not used
    //
    ULARGE_LONG      CurrentPointer;

    //
    // the list of mapped sections of the file,
    // protected by the MappedListRwLock lock
    //
    LIST_ENTRY       MappedSectionsListHead;

    //
    // the RW lock for the MappedSectionsListHead list
    //
    OC_RW_SPIN_LOCK  MappedSectionListRwLock;

} OC_CR_SHADOW_FILE, *POC_CR_SHADOW_FILE;

//--------------------------------------------------

typedef _OC_CR_MAPPED_FILE_SECTION{

    //
    // the following field is used to link 
    // entries in a list headed in OC_CR_SHADOW_FILE.MappedSectionsListHead
    //
    LIST_ENTRY            ListEntry;

    //
    // the section handle
    //
    HANDLE                Handle;

    //
    // where the section starts in a file
    //
    ULARGE_INTEGER        StartInFile;

    //
    // where the section ends in a file
    //
    ULARGE_INTEGER        EndInFile;

    //
    // referenced object for the file object
    //
    POC_CR_SHADOW_FILE    ShadowFileObject;

    //
    // the following is a head for the list of mapped views,
    // the list is protected by the SectionMappedViewsRwLock lock
    //
    LIST_ENTRY            SectionMappedViewsList;

    //
    // the RW lock for protecting SectionMappedViewsList
    //
    OC_RW_SPIN_LOCK       SectionMappedViewsRwLock;

} OC_CR_MAPPED_FILE_SECTION, *POC_CR_MAPPED_FILE_SECTION;

//--------------------------------------------------

typedef struct _OC_CR_SECTION_VIEW{

    //
    // the start and end adress in the system process
    //
    ULONG_PTR            BaseAddress;
    ULONG_PTR            EndAddress;

    //
    // where the mapped view starts in a file
    //
    ULARGE_INTEGER        ViewStartInFile;

    //
    // where the mapped view ends in a file
    //
    ULARGE_INTEGER        ViewEndInFile;

    //
    // the current pointer
    //
    ULONG_PTR             CurrentPointer;

    //
    // a referenced section object
    //
    POC_CR_MAPPED_FILE_SECTION    FileSectionObject;
}

//--------------------------------------------------

NTSTATUS
OcCrCreateShadowFile(
    IN PUNICODE_STRING    FileName,
    IN PULARGE_INTEGER    ThresholdFileSize,
    IN PKEVENT            ReferencedServiceEvent OPTIONAL,
    IN PKEVENT            ReferencedEventToSetWhenReleasingFile OPTIONAL,
    IN PULARGE_INTEGER    MaximumSizeDueToQuota OPTIONAL
    )
{
    NTSTATUS                RC = STATUS_SUCCESS;
    OBJECT_ATTRIBUTES       ObjectAttributes;
    IO_STATUS_BLOCK         IoStatus;
    HANDLE                  FileHandle = NULL;
    PFILE_OBJECT            FileObject = NULL;
    USHORT                  usCompressionFormat = COMPRESSION_FORMAT_NONE;
    HANDLE                  EventHandle;
    PKEVENT                 EventObject;
    BOOLEAN                 UseCache = FALSE;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // align the maximum size to the page boundary, this restriction
    // is imposed by our proprietary cache
    //
    if( NULL != MaximumSizeDueToQuota ){

        MaximumSizeDueToQuota->LowPart = MaximumSizeDueToQuota->LowPart & ~( PAGE_SIZE - 0x1 );
    }

    //
    // always use fast write!
    //
    UseCache = TRUE;//!( g_ShadowLevel > ShadowLevelBase );

    //RtlInitUnicodeString( &uFileName, L"\\DosDevices\\C:\\shadow_pio.dat" );

    InitializeObjectAttributes( &ObjectAttributes,
                                FileName,
                                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                NULL,
                                NULL
                                );

    //
    // TO DO - Protect file from changing attributes and other file information.
    //
    RC = ZwCreateFile( &FileHandle,
                       GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                       &ObjectAttributes,
                       &IoStatus,
                       NULL, 
                       FILE_ATTRIBUTE_NORMAL,
                       0x0,// sharing is disabled
                       FILE_SUPERSEDE,// replace the file if exist, because we must be the first and the last file owner
                       FILE_NON_DIRECTORY_FILE |
                       FILE_RANDOM_ACCESS |
                       FILE_NO_COMPRESSION | 
                       ( UseCache ? 0x0 : FILE_NO_INTERMEDIATE_BUFFERING ),
                       NULL,
                       0
                       );

    ASSERT( RC != STATUS_PENDING );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    EventObject = IoCreateSynchronizationEvent( NULL, &EventHandle );
    if( NULL != EventObject ){

        //
        // Event is created in the signal state
        //
        KeResetEvent( EventObject );

        //
        // disable the compression
        //
        RC = ZwFsControlFile( FileHandle, 
                              EventHandle,//Event
                              NULL,//Apc
                              NULL,//Apc Context
                              &IoStatus,
                              FSCTL_SET_COMPRESSION, 
                              &usCompressionFormat, 
                              sizeof( usCompressionFormat ), 
                              NULL, 
                              0 );

        if( STATUS_PENDING == RC ){

            KeWaitForSingleObject( EventObject, 
                                   Executive, 
                                   KernelMode, 
                                   FALSE, 
                                   NULL );

        }//if( STATUS_PENDING == RC )

        ZwClose( EventHandle );

    }//if( NULL != EventObject ){

    //
    // the FSD may not support the compression set request
    // or the event creation failed, in any case I set
    // FILE_NO_COMPRESSION attribute, hope this is enough.
    //
    RC = STATUS_SUCCESS;

    RC = ObReferenceObjectByHandle( FileHandle,
                                    FILE_ANY_ACCESS,
                                    *IoFileObjectType,
                                    KernelMode, //to avoid a security check set mode to Kernel
                                    (PVOID *)&FileObject,
                                    NULL );

    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    RC = DldSetNewBuffersFile( FileHandle, 
                               FileObject, 
                               ThresholdFileSize, 
                               ReferencedServiceEvent,
                               ReferencedEventToSetWhenReleasingFile,
                               MaximumSizeDueToQuota );
    //
    // Set file handle to NULL, because it has been grabed by
    // the DldSetNewBuffersFile and will be closed
    // when it is no longer needed.
    //
    FileHandle = NULL;
    if( !NT_SUCCESS( RC ) )
        goto __exit;

__exit:

    ASSERT( NT_SUCCESS( RC ) );

    if( !NT_SUCCESS( RC ) ){

        if( NULL != FileObject ){

            ObDereferenceObject( FileObject );
        }

        if( NULL != FileHandle ){

            ZwClose( FileHandle );
        }

    } else {

        ASSERT( NULL == FileHandle );
        ASSERT( FileObject );

        ObDereferenceObject( FileObject );
    }

    return RC;
}

