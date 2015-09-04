/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
22.03.2006 ( March )
 Start
*/

/*
this file contains the code for the minifilter emulation
that is used for processing FSD requests received through
the driver object's dispatch functions
*/
#include "struct.h"
#include "proto.h"

//--------------------------------------------------------

__forceinline
VOID
OcCrCopyStackLocationParameters( 
    IN PIO_STACK_LOCATION    PtrIrpStack,
    IN OUT PFLT_PARAMETERS   Parameters
    );

__forceinline
VOID
OcCrFsdPerformDataStoreInitialization(
    IN OUT POC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore,
    IN PETHREAD    CallersThread OPTIONAL
    );

//--------------------------------------------------------

VOID
OcCrFsdInitMinifilterDataForIrp(
    IN PDEVICE_OBJECT    DeviceObject,
    IN PIRP              Irp,
    IN OUT POC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore
    )
    /*++
    This routine converts an IRP to the minifilter callback data.
    The callback data must be allocated on the stack!
    --*/
{
    PIO_STACK_LOCATION    PtrIrpStack;
    PFLT_CALLBACK_DATA    FltCallbackData;
    PFLT_IO_PARAMETER_BLOCK    Iopb;
    PFLT_PARAMETERS       Parameters;
    BOOLEAN               IsSystemBuffer = FALSE;
#if DBG
    ULONG_PTR             LowLimit;
    ULONG_PTR             HighLimit;

    IoGetStackLimits( &LowLimit, &HighLimit );

    //
    // My IO Manager uses minifilter emulation for the create requests processing
    // and allocates the callback data store from the lookaside list but not
    // on a stack as the minifilter emulator does, the following code
    // checks that the minifilter emulator behavies correctly and allocates memory 
    // for a callback data store on a stack
    //
    if( OcCrIsFileSystemDevObj( DeviceObject ) && 
        !( (LowLimit < (ULONG_PTR)FltCallbackDataStore) &&
           ((ULONG_PTR)FltCallbackDataStore < HighLimit-sizeof(*FltCallbackDataStore) ) ) ){

        KeBugCheckEx( OC_CORE_BUG_MFLTR_DAT_NOT_ON_STACK,
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)DeviceObject,
                      (ULONG_PTR)Irp,
                      (ULONG_PTR)FltCallbackDataStore );
    }
#endif//DBG

    //
    // Irp's stack must be valid
    //
    ASSERT( Irp->CurrentLocation <= Irp->StackCount );

    OcCrFsdPerformDataStoreInitialization( FltCallbackDataStore, 
                                           Irp->Tail.Overlay.Thread );

    FltCallbackDataStore->Irp = Irp;

    PtrIrpStack = IoGetCurrentIrpStackLocation( Irp );

    FltCallbackData = &FltCallbackDataStore->FltCallbackData;
    Iopb = FltCallbackData->Iopb;
    Parameters = &Iopb->Parameters;

    Iopb->MajorFunction = PtrIrpStack->MajorFunction;
    Iopb->MinorFunction = PtrIrpStack->MinorFunction;
    Iopb->IrpFlags = Irp->Flags;
    Iopb->OperationFlags = PtrIrpStack->Flags;
    Iopb->TargetFileObject = PtrIrpStack->FileObject;

    //
    // retrieve the normalized parameters
    //
    switch( PtrIrpStack->MajorFunction ){

        case IRP_MJ_CREATE:

            Parameters->Create.SecurityContext = PtrIrpStack->Parameters.Create.SecurityContext;
            Parameters->Create.Options = PtrIrpStack->Parameters.Create.Options;
            Parameters->Create.FileAttributes = PtrIrpStack->Parameters.Create.FileAttributes;
            Parameters->Create.ShareAccess = PtrIrpStack->Parameters.Create.ShareAccess;
            Parameters->Create.EaLength = PtrIrpStack->Parameters.Create.EaLength;
            Parameters->Create.EaBuffer = Irp->AssociatedIrp.SystemBuffer;
            Parameters->Create.AllocationSize = Irp->Overlay.AllocationSize;

            //
            // this request always uses a system buffer for the EaBuffer
            //
            IsSystemBuffer = TRUE;

            break;

        case IRP_MJ_CREATE_NAMED_PIPE:

            Parameters->CreatePipe.SecurityContext = PtrIrpStack->Parameters.Create.SecurityContext;
            Parameters->CreatePipe.Options = PtrIrpStack->Parameters.Create.Options;
            Parameters->CreatePipe.ShareAccess = PtrIrpStack->Parameters.Create.ShareAccess;
            Parameters->CreatePipe.Parameters = PtrIrpStack->Parameters.Others.Argument4;

            break;

        case IRP_MJ_CLOSE:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        case IRP_MJ_READ:

            Parameters->Read.Length = PtrIrpStack->Parameters.Read.Length;
            Parameters->Read.ByteOffset = PtrIrpStack->Parameters.Read.ByteOffset;

            Parameters->Read.MdlAddress = Irp->MdlAddress;

            //
            // this request obeys the device object's flag, except 
            // the Paging IO, which always use direct IO, and MDL
            // requests.
            // P.S. MDL flags are invalid if the file has been
            // opened with FO_NO_INTERMEDIATE_BUFFERING. They are 
            // also invalid in combination with synchronous calls 
            // (Irp Flag or file open option).
            //
            if( (( IRP_MN_MDL_DPC | IRP_MN_COMPLETE_MDL |IRP_MN_COMPLETE_MDL_DPC ) & 
                  PtrIrpStack->MinorFunction ) && 
                FALSE == IoIsOperationSynchronous( Irp ) && 
                !OcIsFlagOn( PtrIrpStack->FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING ) ){

                    //
                    // Lanman request, a cached request
                    // The MDL is NULL if this is IRP_MN_MDL because 
                    // the MDL should be allocated by the FSD
                    // and returned to the caller
                    //

            } else if( NULL != Irp->MdlAddress ){

                Parameters->Read.ReadBuffer = Irp->UserBuffer;

            } else if( DeviceObject->Flags & DO_BUFFERED_IO ){

                Parameters->Read.ReadBuffer = Irp->AssociatedIrp.SystemBuffer;

                //
                // the system has allocated the buffer from the NP pool
                //
                IsSystemBuffer = TRUE;

            } else if( DeviceObject->Flags & DO_DIRECT_IO ){

                if( Irp->MdlAddress )
                    Parameters->Read.ReadBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );//may be Irp->UserBuffer;

            } else {

                Parameters->Read.ReadBuffer = Irp->UserBuffer;
            }

            break;

        case IRP_MJ_WRITE:

            Parameters->Write.Length = PtrIrpStack->Parameters.Write.Length;
            Parameters->Write.ByteOffset = PtrIrpStack->Parameters.Write.ByteOffset;

            Parameters->Write.MdlAddress = Irp->MdlAddress;

            //
            // this request obeys the device object's flag, except 
            // the Paging IO requests, which always use direct IO, and
            // MDL requests
            // P.S. MDL flags are invalid if the file has been
            // opened with FO_NO_INTERMEDIATE_BUFFERING. They are 
            // also invalid in combination with synchronous calls 
            // (Irp Flag or file open option).
            //
            if( (( IRP_MN_MDL_DPC | IRP_MN_COMPLETE_MDL |IRP_MN_COMPLETE_MDL_DPC ) & 
                  PtrIrpStack->MinorFunction ) && 
                FALSE == IoIsOperationSynchronous( Irp ) && 
                !OcIsFlagOn( PtrIrpStack->FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING ) ){

                    //
                    // Lanman request, a cached request
                    // The MDL is NULL if this is IRP_MN_MDL because 
                    // the MDL should be allocated by the FSD
                    // and returned to the caller
                    //

            } else if( NULL != Irp->MdlAddress ){

                Parameters->Write.WriteBuffer = Irp->UserBuffer;

            } else if( DeviceObject->Flags & DO_BUFFERED_IO ){

                Parameters->Write.WriteBuffer = Irp->AssociatedIrp.SystemBuffer;

                //
                // the system has allocated the buffer from the NP pool
                //
                IsSystemBuffer = TRUE;

            } else if( DeviceObject->Flags & DO_DIRECT_IO ){

                if( Irp->MdlAddress )
                    Parameters->Write.WriteBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );//may be Irp->UserBuffer;

            } else {

                Parameters->Write.WriteBuffer = Irp->UserBuffer;
            }

            break;

        case IRP_MJ_QUERY_INFORMATION:

            Parameters->QueryFileInformation.Length = PtrIrpStack->Parameters.QueryFile.Length;
            Parameters->QueryFileInformation.FileInformationClass = PtrIrpStack->Parameters.QueryFile.FileInformationClass;
            Parameters->QueryFileInformation.InfoBuffer = Irp->AssociatedIrp.SystemBuffer;

            //
            // this request always uses a system buffer
            //
            IsSystemBuffer = TRUE;

            break;

        case IRP_MJ_SET_INFORMATION:
            {

            Parameters->SetFileInformation.Length = PtrIrpStack->Parameters.SetFile.Length;
            Parameters->SetFileInformation.FileInformationClass = PtrIrpStack->Parameters.SetFile.FileInformationClass;

            //
            // For rename or link operations. If InfoBuffer->FileName contains a 
            // fully qualified file name, or if InfoBuffer->RootDirectory is non-NULL, 
            // the Parameters->SetFileInformation.ParentOfTarget is a file object 
            // pointer for the parent directory of the file that is the target of 
            // the operation. Otherwise it is NULL. The operating system
            // checks the above conditions and initialize the Parameters.SetFile.FileObject
            // by the pointer to the target directory FO.
            //
            Parameters->SetFileInformation.ParentOfTarget = PtrIrpStack->Parameters.SetFile.FileObject;

            //
            // Handle is the biggest data type in the union, so
            // the DeleteHandle spans entire union.
            //
            Parameters->SetFileInformation.DeleteHandle = PtrIrpStack->Parameters.SetFile.DeleteHandle;
            Parameters->SetFileInformation.InfoBuffer = Irp->AssociatedIrp.SystemBuffer;

            //
            // this request always uses a system buffer
            //
            IsSystemBuffer = TRUE;

            break;
            }

        case IRP_MJ_QUERY_EA:

            Parameters->QueryEa.Length = PtrIrpStack->Parameters.QueryEa.Length;
            Parameters->QueryEa.EaList = PtrIrpStack->Parameters.QueryEa.EaList;
            Parameters->QueryEa.EaListLength = PtrIrpStack->Parameters.QueryEa.EaListLength;
            Parameters->QueryEa.EaIndex = PtrIrpStack->Parameters.QueryEa.EaIndex;

            Parameters->QueryEa.MdlAddress = Irp->MdlAddress;

            //
            // this request obeys the device object's flag
            //
            if( DeviceObject->Flags & DO_BUFFERED_IO ){

                Parameters->QueryEa.EaBuffer = Irp->AssociatedIrp.SystemBuffer;

                //
                // the system has allocated the buffer from the NP pool
                //
                IsSystemBuffer = TRUE;

            } else if( DeviceObject->Flags & DO_DIRECT_IO ){

                if( Irp->MdlAddress )
                    Parameters->QueryEa.EaBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );//may be Irp->UserBuffer;

            } else {

                Parameters->QueryEa.EaBuffer = Irp->UserBuffer;
            }

            break;

        case IRP_MJ_SET_EA:

            Parameters->SetEa.Length = PtrIrpStack->Parameters.SetEa.Length;

            Parameters->SetEa.MdlAddress = Irp->MdlAddress;

            //
            // this request obeys the device object's flag
            //
            if( DeviceObject->Flags & DO_BUFFERED_IO ){

                Parameters->SetEa.EaBuffer = Irp->AssociatedIrp.SystemBuffer;

                //
                // the system has allocated the buffer from the NP pool
                //
                IsSystemBuffer = TRUE;

            } else if( DeviceObject->Flags & DO_DIRECT_IO ){

                if( Irp->MdlAddress )
                    Parameters->SetEa.EaBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );//may be Irp->UserBuffer;

            } else {

                Parameters->SetEa.EaBuffer = Irp->UserBuffer;
            }

            break;

        case IRP_MJ_FLUSH_BUFFERS:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        case IRP_MJ_QUERY_VOLUME_INFORMATION:

            Parameters->QueryVolumeInformation.Length = PtrIrpStack->Parameters.QueryVolume.Length;
            Parameters->QueryVolumeInformation.FsInformationClass = PtrIrpStack->Parameters.QueryVolume.FsInformationClass;
            Parameters->QueryVolumeInformation.VolumeBuffer = Irp->AssociatedIrp.SystemBuffer;

            //
            // this request always uses a system buffer
            //
            IsSystemBuffer = TRUE;

            break;

        case IRP_MJ_SET_VOLUME_INFORMATION:

            Parameters->SetVolumeInformation.Length = PtrIrpStack->Parameters.SetVolume.Length;
            Parameters->SetVolumeInformation.FsInformationClass = PtrIrpStack->Parameters.SetVolume.FsInformationClass;
            Parameters->SetVolumeInformation.VolumeBuffer = Irp->AssociatedIrp.SystemBuffer;

            //
            // this request always uses a system buffer
            //
            IsSystemBuffer = TRUE;

            break;

        case IRP_MJ_DIRECTORY_CONTROL:

            if( IRP_MN_NOTIFY_CHANGE_DIRECTORY == PtrIrpStack->MinorFunction ){

                //
                // IRP_MN_NOTIFY_CHANGE_DIRECTORY
                //
                Parameters->DirectoryControl.NotifyDirectory.CompletionFilter = PtrIrpStack->Parameters.NotifyDirectory.CompletionFilter;
                Parameters->DirectoryControl.NotifyDirectory.Length = PtrIrpStack->Parameters.NotifyDirectory.Length;

            } else {

                //
                // IRP_MN_QUERY_DIRECTORY or IRP_MN_QUERY_OLE_DIRECTORY
                //
                Parameters->DirectoryControl.QueryDirectory.FileIndex = PtrIrpStack->Parameters.QueryDirectory.FileIndex;
                Parameters->DirectoryControl.QueryDirectory.FileInformationClass = PtrIrpStack->Parameters.QueryDirectory.FileInformationClass;
                Parameters->DirectoryControl.QueryDirectory.FileName = PtrIrpStack->Parameters.QueryDirectory.FileName;
                Parameters->DirectoryControl.QueryDirectory.Length = PtrIrpStack->Parameters.QueryDirectory.Length;
            }

            //
            // this request obeys the device object's flag
            //
            if( DeviceObject->Flags & DO_BUFFERED_IO ){

                if( IRP_MN_NOTIFY_CHANGE_DIRECTORY == PtrIrpStack->MinorFunction ){

                    Parameters->DirectoryControl.NotifyDirectory.MdlAddress = Irp->MdlAddress;
                    Parameters->DirectoryControl.NotifyDirectory.DirectoryBuffer = Irp->AssociatedIrp.SystemBuffer;

                } else {

                    Parameters->DirectoryControl.QueryDirectory.MdlAddress = Irp->MdlAddress;
                    Parameters->DirectoryControl.QueryDirectory.DirectoryBuffer = Irp->AssociatedIrp.SystemBuffer;

                }

                //
                // the system has allocated the buffer from the NP pool
                //
                IsSystemBuffer = TRUE;

            } else if( DeviceObject->Flags & DO_DIRECT_IO ){

                if( IRP_MN_NOTIFY_CHANGE_DIRECTORY == PtrIrpStack->MinorFunction ){

                    Parameters->DirectoryControl.NotifyDirectory.MdlAddress = Irp->MdlAddress;
                    if( Irp->MdlAddress )
                        Parameters->DirectoryControl.NotifyDirectory.DirectoryBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );//may be Irp->UserBuffer;

                } else {

                    Parameters->DirectoryControl.QueryDirectory.MdlAddress = Irp->MdlAddress;
                    if( Irp->MdlAddress )
                        Parameters->DirectoryControl.QueryDirectory.DirectoryBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );//may be Irp->UserBuffer;

                }

            } else {

                if( IRP_MN_NOTIFY_CHANGE_DIRECTORY == PtrIrpStack->MinorFunction ){

                    Parameters->DirectoryControl.NotifyDirectory.MdlAddress = Irp->MdlAddress;
                    Parameters->DirectoryControl.NotifyDirectory.DirectoryBuffer = Irp->UserBuffer;

                } else {

                    Parameters->DirectoryControl.QueryDirectory.MdlAddress = Irp->MdlAddress;
                    Parameters->DirectoryControl.QueryDirectory.DirectoryBuffer = Irp->UserBuffer;

                }

            }
            break;

        case IRP_MJ_FILE_SYSTEM_CONTROL:

            if( IRP_MN_VERIFY_VOLUME == PtrIrpStack->MinorFunction ){

                Parameters->FileSystemControl.VerifyVolume.DeviceObject = PtrIrpStack->Parameters.VerifyVolume.DeviceObject;
                Parameters->FileSystemControl.VerifyVolume.Vpb = PtrIrpStack->Parameters.VerifyVolume.Vpb;

            } else {

                //
                // IRP_MN_USER_FS_REQUEST
                //
                Parameters->FileSystemControl.Common.FsControlCode = PtrIrpStack->Parameters.FileSystemControl.FsControlCode;
                Parameters->FileSystemControl.Common.InputBufferLength = PtrIrpStack->Parameters.FileSystemControl.InputBufferLength;
                Parameters->FileSystemControl.Common.OutputBufferLength = PtrIrpStack->Parameters.FileSystemControl.OutputBufferLength;

                switch( METHOD_FROM_CTL_CODE( PtrIrpStack->Parameters.FileSystemControl.FsControlCode ) )
                {
                case METHOD_BUFFERED:

                    Parameters->FileSystemControl.Buffered.SystemBuffer = Irp->AssociatedIrp.SystemBuffer;

                    break;

                case METHOD_IN_DIRECT:
                case METHOD_OUT_DIRECT:

                    Parameters->FileSystemControl.Direct.InputSystemBuffer = Irp->AssociatedIrp.SystemBuffer;
                    Parameters->FileSystemControl.Direct.OutputMdlAddress = Irp->MdlAddress;
                    if( Irp->MdlAddress )
                        Parameters->FileSystemControl.Direct.OutputBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );

                    break;

                case METHOD_NEITHER:

                    Parameters->FileSystemControl.Neither.InputBuffer = PtrIrpStack->Parameters.FileSystemControl.Type3InputBuffer;
                    Parameters->FileSystemControl.Neither.OutputBuffer = Irp->UserBuffer;
                    Parameters->FileSystemControl.Neither.OutputMdlAddress = Irp->MdlAddress;

                    break;

                default:
                    break;
                }//switch( METHOD_FROM_CTL_CODE(ctrlCode) )
            }
            break;

        case IRP_MJ_DEVICE_CONTROL:

            Parameters->DeviceIoControl.Common.IoControlCode = PtrIrpStack->Parameters.DeviceIoControl.IoControlCode;
            Parameters->DeviceIoControl.Common.InputBufferLength = PtrIrpStack->Parameters.DeviceIoControl.InputBufferLength;
            Parameters->DeviceIoControl.Common.OutputBufferLength = PtrIrpStack->Parameters.DeviceIoControl.OutputBufferLength;

            switch( METHOD_FROM_CTL_CODE( PtrIrpStack->Parameters.DeviceIoControl.IoControlCode ) )
            {
            case METHOD_BUFFERED:

                Parameters->DeviceIoControl.Buffered.SystemBuffer = Irp->AssociatedIrp.SystemBuffer;

                break;

            case METHOD_IN_DIRECT:
            case METHOD_OUT_DIRECT:

                Parameters->DeviceIoControl.Direct.InputSystemBuffer = Irp->AssociatedIrp.SystemBuffer;
                Parameters->DeviceIoControl.Direct.OutputMdlAddress = Irp->MdlAddress;
                if( Irp->MdlAddress )
                    Parameters->DeviceIoControl.Direct.OutputBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );

                break;

            case METHOD_NEITHER:

                Parameters->DeviceIoControl.Neither.InputBuffer = PtrIrpStack->Parameters.DeviceIoControl.Type3InputBuffer;
                Parameters->DeviceIoControl.Neither.OutputBuffer = Irp->UserBuffer;
                Parameters->DeviceIoControl.Neither.OutputMdlAddress = Irp->MdlAddress;

                break;

            default:
                break;
            }//switch( METHOD_FROM_CTL_CODE(ctrlCode) )
            break;

        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        case IRP_MJ_SHUTDOWN:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        case IRP_MJ_LOCK_CONTROL:

            Parameters->LockControl.ByteOffset = PtrIrpStack->Parameters.LockControl.ByteOffset;
            Parameters->LockControl.Key = PtrIrpStack->Parameters.LockControl.Key;
            Parameters->LockControl.Length = PtrIrpStack->Parameters.LockControl.Length;
            //
            // the other paramaters for the FastIO
            //
            break;

        case IRP_MJ_CLEANUP:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        case IRP_MJ_CREATE_MAILSLOT:

            Parameters->CreateMailslot.SecurityContext = PtrIrpStack->Parameters.Create.SecurityContext;
            Parameters->CreateMailslot.Options = PtrIrpStack->Parameters.Create.Options;
            Parameters->CreateMailslot.ShareAccess = PtrIrpStack->Parameters.Create.ShareAccess;
            Parameters->CreateMailslot.Parameters = PtrIrpStack->Parameters.Others.Argument4;

            break;

        case IRP_MJ_QUERY_SECURITY:

            Parameters->QuerySecurity.Length = PtrIrpStack->Parameters.QuerySecurity.Length;
            Parameters->QuerySecurity.SecurityInformation = PtrIrpStack->Parameters.QuerySecurity.SecurityInformation;
            Parameters->QuerySecurity.SecurityBuffer = Irp->UserBuffer;
            Parameters->QuerySecurity.MdlAddress = Irp->MdlAddress;

            break;

        case IRP_MJ_SET_SECURITY:

            Parameters->QuerySecurity.Length = PtrIrpStack->Parameters.QuerySecurity.Length;
            Parameters->QuerySecurity.SecurityInformation = PtrIrpStack->Parameters.QuerySecurity.SecurityInformation;
            Parameters->QuerySecurity.SecurityBuffer = Irp->UserBuffer;
            Parameters->QuerySecurity.MdlAddress = Irp->MdlAddress;

            break;

        case IRP_MJ_POWER:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        case IRP_MJ_SYSTEM_CONTROL:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        case IRP_MJ_DEVICE_CHANGE:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        case IRP_MJ_QUERY_QUOTA:

            Parameters->QueryQuota.Length = PtrIrpStack->Parameters.QueryQuota.Length;
            Parameters->QueryQuota.SidList = PtrIrpStack->Parameters.QueryQuota.SidList;
            Parameters->QueryQuota.SidListLength = PtrIrpStack->Parameters.QueryQuota.SidListLength;
            Parameters->QueryQuota.StartSid = PtrIrpStack->Parameters.QueryQuota.StartSid;

            Parameters->QueryQuota.MdlAddress = Irp->MdlAddress;

            //
            // this request obeys the device object's flag
            //
            if( DeviceObject->Flags & DO_BUFFERED_IO ){

                Parameters->QueryQuota.QuotaBuffer = Irp->AssociatedIrp.SystemBuffer;

                //
                // the system has allocated the buffer from the NP pool
                //
                IsSystemBuffer = TRUE;

            } else if( DeviceObject->Flags & DO_DIRECT_IO ){

                if( Irp->MdlAddress )
                    Parameters->QueryQuota.QuotaBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );//may be Irp->UserBuffer;

            } else {

                Parameters->QueryQuota.QuotaBuffer = Irp->UserBuffer;
            }

            break;

        case IRP_MJ_SET_QUOTA:

            Parameters->SetQuota.Length = PtrIrpStack->Parameters.SetQuota.Length;
            Parameters->SetQuota.MdlAddress = Irp->MdlAddress;

            //
            // this request obeys the device object's flag
            //
            if( DeviceObject->Flags & DO_BUFFERED_IO ){

                Parameters->SetQuota.QuotaBuffer = Irp->AssociatedIrp.SystemBuffer;

                //
                // the system has allocated the buffer from the NP pool
                //
                IsSystemBuffer = TRUE;

            } else if( DeviceObject->Flags & DO_DIRECT_IO ){

                if( Irp->MdlAddress )
                    Parameters->SetQuota.QuotaBuffer = MmGetMdlVirtualAddress( Irp->MdlAddress );//may be Irp->UserBuffer;

            } else {

                Parameters->SetQuota.QuotaBuffer = Irp->UserBuffer;
            }

            break;

        case IRP_MJ_PNP:
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;

        default:
#if DBG
            KeBugCheckEx( 0xABCD0004,
                          (ULONG_PTR)__LINE__,
                          (ULONG_PTR)DeviceObject,
                          (ULONG_PTR)Irp,
                          (ULONG_PTR)PtrIrpStack->MajorFunction );
#endif//DBG
            OcCrCopyStackLocationParameters( PtrIrpStack, Parameters );
            break;
    }

    if( IsSystemBuffer )
        OcSetFlag( FltCallbackData->Flags, FLTFL_CALLBACK_DATA_SYSTEM_BUFFER );

    //
    // this is an Irp based operation
    //
    OcSetFlag( FltCallbackData->Flags, FLTFL_CALLBACK_DATA_IRP_OPERATION );

    //
    // the memory must be allocated on the thread's stack
    //
    OcSetFlag( FltCallbackDataStore->Flags, OC_FLT_CALLBACK_DATA_STACK_ALLOCATION );

}

//-------------------------------------------------------------------

__forceinline
VOID
OcCrCopyStackLocationParameters( 
    IN PIO_STACK_LOCATION    PtrIrpStack,
    IN OUT PFLT_PARAMETERS   Parameters
    )
{
    Parameters->Others.Argument1 = PtrIrpStack->Parameters.Others.Argument1;
    Parameters->Others.Argument2 = PtrIrpStack->Parameters.Others.Argument2;
    Parameters->Others.Argument3 = PtrIrpStack->Parameters.Others.Argument3;
    Parameters->Others.Argument4 = PtrIrpStack->Parameters.Others.Argument4;

    Parameters->Others.Argument5 = (PVOID)NULL;
    Parameters->Others.Argument6.QuadPart = (LONGLONG)0x0i64;
}

//--------------------------------------------------------

__forceinline
VOID
OcCrFsdPerformDataStoreInitialization(
    IN OUT POC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore,
    IN PETHREAD    CallersThread OPTIONAL
    )
{
    PFLT_CALLBACK_DATA    FltCallbackData;
    PVOID*                PointerToConstPonter;

    RtlZeroMemory( FltCallbackDataStore, sizeof( *FltCallbackDataStore ) );

#if DBG
    FltCallbackDataStore->Signature = OC_FLT_CALLBACK_DATA_STORE_SIGNATURE;
#endif//DBG

    //
    // modify the const data
    //
    FltCallbackData = &FltCallbackDataStore->FltCallbackData;
    PointerToConstPonter = (PVOID*)&FltCallbackData->Iopb;
    *PointerToConstPonter = &FltCallbackDataStore->Iopb;

    PointerToConstPonter = (PVOID*)&FltCallbackData->Thread;
    *PointerToConstPonter = CallersThread;

    FltCallbackData->RequestorMode = ExGetPreviousMode();

}

//-------------------------------------------------------------------

BOOLEAN
OcCrFltEmulIs32bitProcess(
    __in PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects
    )
    /*
    this function is an equivalent of FltIs32bitProcess function
    */
{
    //
    // FltIs32bitProcess can be called only at APC_LEVEL or lower levels
    //
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

#if defined( _AMD64_ )

    if( OcCrFltIsEmulatedCall( FltObjects ) ){

        //
        // this is an emulated callback
        //
        PIRP    Irp;

        //
        // get the related Irp and query it
        //
        Irp = OcCrGetIrpForEmulatedCallbackData( Data );

        return IoIs32bitProcess( Irp );

    }

    //
    // this is a call from the minifilter manager
    //
    return Global.MinifilterFunctions.FltIs32bitProcess( Data );

#else
    //
    // this is a 32 bits system
    //
    return TRUE;
#endif//defined( _AMD64_ )

}

//-------------------------------------------------------------------

