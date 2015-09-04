/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
12.04.2007 ( April )
 Start
*/

/*
This file contains the code for processing requests to disks
*/
#include "struct.h"
#include "proto.h"
#include <ntddscsi.h>
#undef DebugPrint
#include <scsi.h>
//#include <ntddstor.h>
#include <ntdddisk.h>
//#include <ntddcdrm.h>

//
// dcefined here because including ntddcdrom.h results in a conflict with another files
//
#define IOCTL_CDROM_BASE             FILE_DEVICE_CD_ROM
#define IOCTL_CDROM_PLAY_AUDIO_MSF   CTL_CODE(IOCTL_CDROM_BASE, 0x0006, METHOD_BUFFERED, FILE_READ_ACCESS)

//------------------------------------------------

BOOLEAN
OcCrIsIoclBufferGreatEnoughToBearScsiPassTroughRequest(
    IN POC_NODE_CTX CONST    Context
    )
    /*
    this function checks the validity of a buffer for an IOCTL operation
    */
{
    BOOLEAN     ReturnValue;
    ULONG       MinimumBufferSize = 0x0;
    ULONG       IoControlCode;
    ULONG       InputBufferLength;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxMinifilterDriverFlag ) );

    if( OcIsFlagOn( Context->Flags, OcNodeCtxMinifilterDriverFlag ) ){

        //
        // this is a call from an FSD or minifilter
        //
        IoControlCode = Context->RequestData.Data->Iopb->Parameters.DeviceIoControl.Common.IoControlCode;
        InputBufferLength = Context->RequestData.Data->Iopb->Parameters.DeviceIoControl.Common.InputBufferLength;

    } else {

        //
        // this is a call from a disk driver
        //
        PIO_STACK_LOCATION    pIrpStack;

        ASSERT( Context->RequestData.Irp->Type == IO_TYPE_IRP );

        pIrpStack = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );
        IoControlCode = pIrpStack->Parameters.DeviceIoControl.IoControlCode;
        InputBufferLength = pIrpStack->Parameters.DeviceIoControl.InputBufferLength;
    }

    ASSERT( IOCTL_SCSI_PASS_THROUGH == IoControlCode || 
            IOCTL_SCSI_PASS_THROUGH_DIRECT == IoControlCode );

    if( IOCTL_SCSI_PASS_THROUGH != IoControlCode &&
        IOCTL_SCSI_PASS_THROUGH_DIRECT != IoControlCode )
        return FALSE;

    if( IOCTL_SCSI_PASS_THROUGH == IoControlCode ){

#if defined (_AMD64_)
        if( OcIsFlagOn( Context->Flags, OcNodeCtx32bitProcessFlag ) )
            MinimumBufferSize = sizeof( SCSI_PASS_THROUGH32 );
        else
            MinimumBufferSize = sizeof( SCSI_PASS_THROUGH );
#else
        MinimumBufferSize = sizeof( SCSI_PASS_THROUGH );
#endif

    } else if( IOCTL_SCSI_PASS_THROUGH_DIRECT == IoControlCode ){

#if defined (_AMD64_)
        if( OcIsFlagOn( Context->Flags, OcNodeCtx32bitProcessFlag ) )
            MinimumBufferSize = sizeof( SCSI_PASS_THROUGH_DIRECT32 );
        else
            MinimumBufferSize = sizeof( SCSI_PASS_THROUGH_DIRECT );
#else
        MinimumBufferSize = sizeof( SCSI_PASS_THROUGH_DIRECT );
#endif

    }

    //
    // Validiate the user's buffer.
    //

    if( InputBufferLength < MinimumBufferSize )
        ReturnValue = FALSE;
    else
        ReturnValue = TRUE;

    return ReturnValue;
}

//------------------------------------------------

PCHAR
OcCrGetCdbFromScsiPassTroughRequest(
    IN POC_NODE_CTX CONST    Context,
    OUT PUCHAR    PtrCdbLength OPTIONAL
    )
{
    PCHAR       PtrCdb = NULL;
    ULONG       IoControlCode;
    PVOID       SystemBuffer;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxMinifilterDriverFlag ) );

    if( OcIsFlagOn( Context->Flags, OcNodeCtxMinifilterDriverFlag ) ){

        //
        // this is a call from an FSD or minifilter
        //
        IoControlCode = Context->RequestData.Data->Iopb->Parameters.DeviceIoControl.Common.IoControlCode;
        SystemBuffer = Context->RequestData.Data->Iopb->Parameters.DeviceIoControl.Buffered.SystemBuffer;

    } else {

        //
        // this is a call from a disk or volume driver
        //
        PIO_STACK_LOCATION    pIrpStack;

        ASSERT( IO_TYPE_IRP == Context->RequestData.Irp->Type );

        pIrpStack = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );
        IoControlCode = pIrpStack->Parameters.DeviceIoControl.IoControlCode;
        SystemBuffer = Context->RequestData.Irp->AssociatedIrp.SystemBuffer;
    }

    ASSERT( IOCTL_SCSI_PASS_THROUGH == IoControlCode || 
            IOCTL_SCSI_PASS_THROUGH_DIRECT == IoControlCode );

    if( IOCTL_SCSI_PASS_THROUGH != IoControlCode &&
        IOCTL_SCSI_PASS_THROUGH_DIRECT != IoControlCode )
        return NULL;

    ASSERT( OcCrIsIoclBufferGreatEnoughToBearScsiPassTroughRequest( Context ) );

    if( IOCTL_SCSI_PASS_THROUGH == IoControlCode ){

#if defined (_AMD64_)
        if( OcIsFlagOn( Context->Flags, OcNodeCtx32bitProcessFlag ) ){

            PtrCdb = ((PSCSI_PASS_THROUGH32)SystemBuffer)->Cdb;

            if( PtrCdbLength )
                *PtrCdbLength = ((PSCSI_PASS_THROUGH32)SystemBuffer)->CdbLength;

        } else {

            PtrCdb = ((PSCSI_PASS_THROUGH)SystemBuffer)->Cdb;

            if( PtrCdbLength )
                *PtrCdbLength = ((PSCSI_PASS_THROUGH)SystemBuffer)->CdbLength;
        }
#else
        PtrCdb = ((PSCSI_PASS_THROUGH)SystemBuffer)->Cdb;

        if( PtrCdbLength )
            *PtrCdbLength = ((PSCSI_PASS_THROUGH)SystemBuffer)->CdbLength;
#endif

    } else if( IOCTL_SCSI_PASS_THROUGH_DIRECT == IoControlCode ){

#if defined (_AMD64_)
        if( OcIsFlagOn( Context->Flags, OcNodeCtx32bitProcessFlag ) ){ 

            PtrCdb = ((PSCSI_PASS_THROUGH_DIRECT32)SystemBuffer)->Cdb;

            if( PtrCdbLength )
                *PtrCdbLength = ((PSCSI_PASS_THROUGH_DIRECT32)SystemBuffer)->CdbLength;

        } else {

            PtrCdb = ((PSCSI_PASS_THROUGH_DIRECT)SystemBuffer)->Cdb;

            if( PtrCdbLength )
                *PtrCdbLength = ((PSCSI_PASS_THROUGH_DIRECT)SystemBuffer)->CdbLength;

        }
#else
        PtrCdb = ((PSCSI_PASS_THROUGH_DIRECT)SystemBuffer)->Cdb;

        if( PtrCdbLength )
            *PtrCdbLength = ((PSCSI_PASS_THROUGH_DIRECT)SystemBuffer)->CdbLength;
#endif
    }

    return PtrCdb;
}

//------------------------------------------------

OC_ACCESS_RIGHTS
OcCrGetCdbRequestedAccess(
    IN PUCHAR pCdb,//PCDB  SCSIOP_READ_CD etc.
    IN UCHAR CdbLength
    )
    /*
    This function is called to analyze a SCSI command and returns the requested access for it.

    pCdb      - SCSI command buffer.
    CdbLength - SCSI command buffer length.
    */
{
    if( 0x0 == CdbLength ){

        ASSERT(!"ocore: GetCdbRequestedAccess - an invalid CdbLength.");
        return 0;
    }

    if( NULL == pCdb ){

        ASSERT(!"ocore: GetCdbRequestedAccess - an invalid Cdb pointer");
        return 0;
    }

    switch (pCdb[0]){

        case SCSIOP_FORMAT_UNIT: // FORMAT UNIT
            return DEVICE_DISK_FORMAT;

        case SCSIOP_START_STOP_UNIT: // START/STOP UNIT
        case SCSIOP_LOAD_UNLOAD_SLOT: // LOAD/UNLOAD MEDIUM
            return DEVICE_EJECT_MEDIA;

        case SCSIOP_WRITE6://WRITE(6)
        case SCSIOP_WRITE:// WRITE (10)
        case SCSIOP_WRITE_VERIFY:// WRITE AND VERIFY (10)
        case SCSIOP_SYNCHRONIZE_CACHE: // SYNCHRONIZE CACHE
        case SCSIOP_WRITE_DATA_BUFF:// 0x3b
        case 0xAA://WRITE(12)
        case SCSIOP_WRITE16:// 16 byte commands can't be sent via ATAPI
        case SCSIOP_SEND_DVD_STRUCTURE:// used to send structure description of the data which will be written to the disk
        case SCSIOP_SEND_CUE_SHEET:// used to send structure desription of the information at Session-at-once write mode
        //
        // the MODE SELECT command is used to
        // set the new mode for the CD drive
        // for example to switch it from the 
        // current mode to raw mode before
        // raw read operation, so this command
        // has been excluded from the controlled operations
        //
        //case SCSIOP_MODE_SELECT:   // MODE SELECT
        //case SCSIOP_MODE_SELECT10: // MODE SELECT (10)
            return DEVICE_WRITE;

        case SCSIOP_PLAY_AUDIO: // PLAY AUDIO (10)
        case 0xA5: // PLAY AUDIO (12)
        case SCSIOP_PLAY_AUDIO_MSF: // PLAY AUDIO MSF
        case SCSIOP_CLOSE_TRACK_SESSION: // CLOSE TRACK/SESSION
            return DEVICE_PLAY_AUDIO_CD;

        case SCSIOP_BLANK: // BLANK
            return DEVICE_DELETE;

        case SCSIOP_READ6:
        case SCSIOP_READ_REVERSE:
        case SCSIOP_READ:
        case SCSIOP_READ_DATA_BUFF:
        case SCSIOP_READ_TOC:
        case SCSIOP_READ_HEADER:
        case SCSIOP_READ_DVD_STRUCTURE:
        case SCSIOP_READ_CD_MSF:
        case SCSIOP_READ_CD: // READ CD
        case SCSIOP_READ16:// 16 byte commands can't be sent via ATAPI
            return DEVICE_READ;

        default: // Easy Media Creator additional commands: AD 52 5C 25 4A
            //dprintf("ocore: SCSI CMD: %X\n",pCdb[0]);
            break;

    }

    return 0;
}

//------------------------------------------------

OC_ACCESS_RIGHTS 
OcCrGetDiskIoctlRequestedAccess(
    IN POC_NODE_CTX CONST    Context
    )
{
    OC_ACCESS_RIGHTS      RequestedAccess = 0;
    ULONG                 IoControlCode;

    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxMinifilterDriverFlag ) );

    if( OcIsFlagOn( Context->Flags, OcNodeCtxMinifilterDriverFlag ) ){

        //
        // this is a call from an FSD or minifilter
        //
        IoControlCode = Context->RequestData.Data->Iopb->Parameters.DeviceIoControl.Common.IoControlCode;

    } else {

        //
        // this is a call from a disk or volume driver
        //
        PIO_STACK_LOCATION    pIrpStack;

        ASSERT( Context->RequestData.Irp->Type == IO_TYPE_IRP );

        pIrpStack = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );
        IoControlCode = pIrpStack->Parameters.DeviceIoControl.IoControlCode;
    }

    switch( IoControlCode )
    {
    case IOCTL_DISK_REASSIGN_BLOCKS:
    case SMART_SEND_DRIVE_COMMAND:

        RequestedAccess |= DEVICE_DIRECT_WRITE;
        break;

    case IOCTL_DISK_VERIFY:// format
    case IOCTL_DISK_FORMAT_TRACKS:
    case IOCTL_DISK_FORMAT_TRACKS_EX:
    case IOCTL_DISK_DELETE_DRIVE_LAYOUT:
    case IOCTL_DISK_SET_DRIVE_LAYOUT:
    case IOCTL_DISK_SET_PARTITION_INFO:
    case IOCTL_DISK_SET_PARTITION_INFO_EX:
    case IOCTL_DISK_GROW_PARTITION:
    case IOCTL_DISK_UPDATE_DRIVE_SIZE:

        RequestedAccess |= DEVICE_DISK_FORMAT;
        break;

    case IOCTL_STORAGE_LOAD_MEDIA:// eject
    case IOCTL_STORAGE_LOAD_MEDIA2:
    case IOCTL_STORAGE_EJECT_MEDIA:
    case IOCTL_DISK_EJECT_MEDIA:

        RequestedAccess |= DEVICE_EJECT_MEDIA;
        break;

    case IOCTL_SCSI_PASS_THROUGH:
    case IOCTL_SCSI_PASS_THROUGH_DIRECT:
        {
            PUCHAR    PtrCdb;
            UCHAR     CdbLength;

            if( FALSE == OcCrIsIoclBufferGreatEnoughToBearScsiPassTroughRequest( Context ) )
                break;

            PtrCdb = OcCrGetCdbFromScsiPassTroughRequest( Context, &CdbLength );
            ASSERT( PtrCdb );

            if( NULL != PtrCdb )
                RequestedAccess |= OcCrGetCdbRequestedAccess( PtrCdb, CdbLength );

            break;
        }
    }

    return RequestedAccess;
}

//------------------------------------------------

BOOLEAN
OcCrIsMajorScsiRequest(
    __in POC_DEVICE_OBJECT    OcDeviceObject,
    __in PIRP    Irp
    )
    /*
    The function returns TRUE if the request 
    is a SCSI request to a device with a SCSI
    interface.
    If this function returns TRUE then an SRB can be found in 
    IoGetCurrentIrpStackLocation( Irp )->Parameters.Scsi.Srb .
    */
{
    PIO_STACK_LOCATION    StackLocation = IoGetCurrentIrpStackLocation( Irp );
    ULONG                 IoControlCode;
    POC_DEVICE_PROPERTY_HEADER   DevicePropertyObject;

    //
    // SCSI request mat be sent either as an internal IOCTL or a pure SCSI request,
    // and they both have the same major code
    //
    if( IRP_MJ_SCSI != StackLocation->MajorFunction )
        return FALSE;

    //
    // only PDOs or lower filrers have a pure SCSI interface
    //
    if( OcDevicePnPTypePdo != OcDeviceObject->DevicePnPType && 
        !( OcDevicePnPTypeFilterDo == OcDeviceObject->DevicePnPType && 0x1 == OcDeviceObject->Flags.LowerFilter ) )
        return FALSE;

    //
    // check that the device has disk interface
    //
    DevicePropertyObject = OcGetDeviceProperty( OcDeviceObject );
    if( NULL == DevicePropertyObject )
        return FALSE;
    else if( en_GUID_DEVCLASS_DISKDRIVE != DevicePropertyObject->SetupClassGuidIndex && 
             en_GUID_DEVCLASS_CDROM != DevicePropertyObject->SetupClassGuidIndex )
        return FALSE;

    ASSERT( en_GUID_DEVCLASS_DISKDRIVE == DevicePropertyObject->SetupClassGuidIndex || 
            en_GUID_DEVCLASS_CDROM == DevicePropertyObject->SetupClassGuidIndex );

    //
    // if this is an IRP_MJ_INTERNAL_DEVICE_CONTROL then the
    // possible IOCTLS are ( for example see CdRomUpdateGeometryCompletion )
    // - IOCTL_SCSI_EXECUTE_IN
    // - IOCTL_SCSI_EXECUTE_OUT
    // - IOCTL_SCSI_EXECUTE_NONE
    //
    // if this is a IRP_MJ_SCSI the IOCTL field should be zero
    //

    IoControlCode = StackLocation->Parameters.DeviceIoControl.IoControlCode;

    if( IOCTL_SCSI_EXECUTE_IN != IoControlCode &&
        IOCTL_SCSI_EXECUTE_OUT != IoControlCode &&
        IOCTL_SCSI_EXECUTE_NONE != IoControlCode &&
        0x0 != IoControlCode )
        return FALSE;

    //
    // this is a scsi request with an SRB in irpStack->Parameters.Scsi.Srb
    //
    return TRUE;
}

//------------------------------------------------

OC_ACCESS_RIGHTS 
OcCrGetVolumeRequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcVolumeDeviceObject
    )
    /*
    the function returns the access rights requested by the operation
    on a PDO representing the standard volume, i.e. the device on which
    FSD can be mounted, on the 2k/XP systems volume devices are created by the
    ftdisk and dmio drivers, on Vista this is the volmngr driver.

    OcVolumeDeviceObject - either the actual PDO or filter device object

    */
{
    OC_ACCESS_RIGHTS      RequestedAccess = DEVICE_NO_ANY_ACCESS;
    PIO_STACK_LOCATION    IoStackLocation;
    ULONG                 CreateDisposition;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL != Context->RequestData.Irp );
    ASSERT( IO_TYPE_IRP == Context->RequestData.Irp->Type );
    ASSERT( en_GUID_DEVCLASS_VOLUME == (OcGetDeviceProperty( OcVolumeDeviceObject ))->SetupClassGuidIndex );
    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) );

    IoStackLocation = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );

    switch( IoStackLocation->MajorFunction ){

    case IRP_MJ_CLOSE:
        break;

    case IRP_MJ_CREATE:

        CreateDisposition = ( IoStackLocation->Parameters.Create.Options >> 24 ) & 0xFF;

        //
        // remeber that this is a direct device open, this is needed
        // because if there is no any other rights will be set but
        // I still will need to know that this is a direct open
        //
        RequestedAccess |= DIRECT_DEVICE_OPEN;

        switch( CreateDisposition ){

        case FILE_SUPERSEDE:
            RequestedAccess |= DEVICE_DELETE;
        case FILE_CREATE:
        case FILE_OVERWRITE:
        case FILE_OVERWRITE_IF://Ultra Edit can delete file if read/write denied
            RequestedAccess |= DEVICE_DIRECT_WRITE;
        case FILE_OPEN:
        case FILE_OPEN_IF:

            if( OcIsFlagOn( IoStackLocation->Parameters.Create.SecurityContext->DesiredAccess, WRITE_ACCESS ) )
                RequestedAccess |= DEVICE_DIRECT_WRITE;

            if( OcIsFlagOn( IoStackLocation->Parameters.Create.SecurityContext->DesiredAccess, READ_ACCESS ) )
                RequestedAccess |= DEVICE_DIRECT_READ;
            break;
        }//switch( CreateDisposition ){

        break;

    case IRP_MJ_READ:

        //
        // if the file object is present then thi is a request to direct read 
        // by using a file object but not a read request from FSD
        //
        if( NULL != IoStackLocation->FileObject ){

#if DBG
            POC_FILE_OBJECT    OcFileObject = OcCrRetriveReferencedFileObject( IoStackLocation->FileObject );
            if( NULL != OcFileObject ){

                ASSERT( 0x1 == OcFileObject->Flags.DirectDeviceOpen );
                OcObDereferenceObject( OcFileObject );
            }
#endif//DBG

            RequestedAccess |= DEVICE_DIRECT_READ;
        }
        break;

    case IRP_MJ_WRITE:

        //
        // if the file object is present then thi is a request to direct read 
        // by using a file object but not a read request from FSD
        //
        if( NULL != IoStackLocation->FileObject ){

#if DBG
            POC_FILE_OBJECT    OcFileObject = OcCrRetriveReferencedFileObject( IoStackLocation->FileObject );
            if( NULL != OcFileObject ){

                ASSERT( 0x1 == OcFileObject->Flags.DirectDeviceOpen );
                OcObDereferenceObject( OcFileObject );
            }
#endif//DBG

            RequestedAccess |= DEVICE_DIRECT_WRITE;
        }
        break;

    case IRP_MJ_DEVICE_CONTROL:
    case IRP_MJ_INTERNAL_DEVICE_CONTROL:

        //
        // call the function for processing SCSI IO requests,
        // in most cases this is a request from the either upper filter
        // or from the user mode, the Windows kernel or official drivers
        // never send such requests at a volume level.
        // I not check here for a file object because this
        // might be direct write from the tools using
        // stand alone driver or upper filter.
        //
        RequestedAccess |= OcCrGetDiskIoctlRequestedAccess( Context );

        break;
    }

    //
    // REMOVE THIS
    //
#if DBG
    RequestedAccess |= DEVICE_DIRECT_READ;
#endif//DBG

    return RequestedAccess;
}

//------------------------------------------------

OC_ACCESS_RIGHTS
OcCrGetRequestedAccessForIrpMjScsiRequest(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcScsiInterfaceDeviceObject
    )
    /*
    the function returns access rights requested by an IRP_MJ_SCSI request
    send to a device with SCSI interface
    */
{
    OC_ACCESS_RIGHTS      RequestedAccess = DEVICE_NO_ANY_ACCESS;
    PIO_STACK_LOCATION    pIrpStack;
    PCHAR                 PtrCdb = NULL;
    UCHAR                 CdbLength;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OcCrIsMajorScsiRequest( OcScsiInterfaceDeviceObject, Context->RequestData.Irp ) );

    pIrpStack = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );

    if( IRP_MJ_SCSI != pIrpStack->MajorFunction ){

        ASSERT( !"Not IRP_MJ_SCSI request is passed to OcCrGetRequestedAccessForIrpMjScsiRequest!" );
        return DEVICE_NO_ANY_ACCESS;
    }

    if( SRB_FUNCTION_EXECUTE_SCSI == pIrpStack->Parameters.Scsi.Srb->Function ){

        PtrCdb = pIrpStack->Parameters.Scsi.Srb->Cdb;
        CdbLength = pIrpStack->Parameters.Scsi.Srb->CdbLength;
        ASSERT( 0x0 != CdbLength );

    } else {

        PtrCdb = NULL;
    }

    if( NULL != PtrCdb )
        RequestedAccess |= OcCrGetCdbRequestedAccess( PtrCdb, CdbLength );

    return RequestedAccess;
}

//------------------------------------------------

OC_ACCESS_RIGHTS 
OcCrGetOpticalDiskDriveRequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcDvdDeviceObject
    )
    /*
    the function returns the access rights requested by the operation
    on a FDO or a PDO representing the CD or DVD drive, i.e. the device created
    by the cdrom.sys driver acting as a Functional device or device created by the
    atapi.sys or similar bus driver

    OcDvdDeviceObject - FDO, PDO or upper or lower filter

    */
{
    OC_ACCESS_RIGHTS      RequestedAccess = DEVICE_NO_ANY_ACCESS;
    PIO_STACK_LOCATION    IoStackLocation;
    ULONG                 CreateDisposition;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL != Context->RequestData.Irp );
    ASSERT( IO_TYPE_IRP == Context->RequestData.Irp->Type );
    ASSERT( en_GUID_DEVCLASS_CDROM == (OcGetDeviceProperty( OcDvdDeviceObject ))->SetupClassGuidIndex );
    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) );

    IoStackLocation = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );

    switch( IoStackLocation->MajorFunction ){

    case IRP_MJ_CLOSE:
        break;

    case IRP_MJ_CREATE:

        CreateDisposition = ( IoStackLocation->Parameters.Create.Options >> 24 ) & 0xFF;

        //
        // remeber that this is a direct device open, this is needed
        // because if there is no any other rights will be set but
        // I still will need to know that this is a direct open
        //
        RequestedAccess |= DIRECT_DEVICE_OPEN;

        switch( CreateDisposition ){

        case FILE_SUPERSEDE:
            RequestedAccess |= DEVICE_DELETE;
        case FILE_CREATE:
        case FILE_OVERWRITE:
        case FILE_OVERWRITE_IF://Ultra Edit can delete file if read/write denied
            RequestedAccess |= DEVICE_DIRECT_WRITE;
        case FILE_OPEN:
        case FILE_OPEN_IF:

            if( OcIsFlagOn( IoStackLocation->Parameters.Create.SecurityContext->DesiredAccess, WRITE_ACCESS ) )
                RequestedAccess |= DEVICE_DIRECT_WRITE;

            if( OcIsFlagOn( IoStackLocation->Parameters.Create.SecurityContext->DesiredAccess, READ_ACCESS ) )
                RequestedAccess |= DEVICE_DIRECT_READ;
            break;
        }//switch( CreateDisposition ){

        break;

    case IRP_MJ_READ:

        //
        // if the file object is present then thi is a request to direct read 
        // by using a file object but not a read request from FSD
        //
        if( NULL != IoStackLocation->FileObject ){

#if DBG
            POC_FILE_OBJECT    OcFileObject = OcCrRetriveReferencedFileObject( IoStackLocation->FileObject );
            if( NULL != OcFileObject ){

                ASSERT( 0x1 == OcFileObject->Flags.DirectDeviceOpen );
                OcObDereferenceObject( OcFileObject );
            }
#endif//DBG

            RequestedAccess |= DEVICE_DIRECT_READ;
        }
        break;

    case IRP_MJ_WRITE:

        //
        // if the file object is present then thi is a request to direct read 
        // by using a file object but not a read request from FSD
        //
        if( NULL != IoStackLocation->FileObject ){

#if DBG
            POC_FILE_OBJECT    OcFileObject = OcCrRetriveReferencedFileObject( IoStackLocation->FileObject );
            if( NULL != OcFileObject ){

                ASSERT( 0x1 == OcFileObject->Flags.DirectDeviceOpen );
                OcObDereferenceObject( OcFileObject );
            }
#endif//DBG

            RequestedAccess |= DEVICE_DIRECT_WRITE;
        }
        break;

    case IRP_MJ_INTERNAL_DEVICE_CONTROL:// same as IRP_MJ_SCSI

        if( OcCrIsMajorScsiRequest( OcDvdDeviceObject, Context->RequestData.Irp ) ){

            //
            // this is a pure SCSI request, i.e. the request to a lower filter or a PDO
            //
            RequestedAccess |= OcCrGetRequestedAccessForIrpMjScsiRequest( Context, OcDvdDeviceObject );
            break;
        }
        //
        // NO BREAK here! Continue checking in case of an upper filter of a FDO!
        //
        ASSERT( !OcCrIsMajorScsiRequest( OcDvdDeviceObject, Context->RequestData.Irp ) );
        //
        // NO BREAK HERE! Continue checking in case of an upper filter of a FDO!
        //
    case IRP_MJ_DEVICE_CONTROL:

        ASSERT( !OcCrIsMajorScsiRequest( OcDvdDeviceObject, Context->RequestData.Irp ) );
        //
        // this is either 
        //  - a IRP_MJ_DEVICE_CONTROL or IRP_MJ_INTERNAL_DEVICE_CONTROL request to 
        //    an upper filter or FDO or IRP_MJ_DEVICE_CONTROL
        //  - a pure IRP_MJ_DEVICE_CONTROL request
        //
        switch( IoStackLocation->Parameters.DeviceIoControl.IoControlCode ){

        case IOCTL_CDROM_PLAY_AUDIO_MSF:

            //
            // play an audio cd
            //
            RequestedAccess |= DEVICE_PLAY_AUDIO_CD;
            break;
        }//switch( pIrpStack->Parameters.DeviceIoControl.IoControlCode )

        //
        // call the function for processing SCSI IO requests,
        // in most cases this is a request from the either upper filter
        // or from the user mode, the Windows kernel or official drivers
        // never send such requests at a volume level.
        // I not check here for a file object because this
        // might be direct write from the tools using
        // stand alone driver or upper filter.
        //
        RequestedAccess |= OcCrGetDiskIoctlRequestedAccess( Context );

        break;
    }

    //
    // REMOVE THIS
    //
#if DBG
    RequestedAccess |= DEVICE_DIRECT_READ;
#endif//DBG

    return RequestedAccess;
}

//------------------------------------------------

OC_ACCESS_RIGHTS 
OcCrGetDiskDriveRequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcDiskDeviceObject
    )
    /*
    the function returns the access rights requested by the operation
    on an FDO, PDO or Filters for disk drivers, i.e. the FDO device created
    by the disk.sys or PDO from atapi.sys

    OcDiskDeviceObject - FDO, PDO or upper or lower filter

    */
{
    OC_ACCESS_RIGHTS      RequestedAccess = DEVICE_NO_ANY_ACCESS;
    PIO_STACK_LOCATION    IoStackLocation;
    ULONG                 CreateDisposition;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL != Context->RequestData.Irp );
    ASSERT( IO_TYPE_IRP == Context->RequestData.Irp->Type );
    ASSERT( en_GUID_DEVCLASS_DISKDRIVE == (OcGetDeviceProperty( OcDiskDeviceObject ))->SetupClassGuidIndex || 
            en_GUID_DEVCLASS_FLOPPYDISK == (OcGetDeviceProperty( OcDiskDeviceObject ))->SetupClassGuidIndex );
    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) );

    IoStackLocation = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );

    switch( IoStackLocation->MajorFunction ){

    case IRP_MJ_CLOSE:
        break;

    case IRP_MJ_CREATE:

        CreateDisposition = ( IoStackLocation->Parameters.Create.Options >> 24 ) & 0xFF;

        //
        // remeber that this is a direct device open, this is needed
        // because if there is no any other rights will be set but
        // I still will need to know that this is a direct open
        //
        RequestedAccess |= DIRECT_DEVICE_OPEN;

        switch( CreateDisposition ){

        case FILE_SUPERSEDE:
            RequestedAccess |= DEVICE_DELETE;
        case FILE_CREATE:
        case FILE_OVERWRITE:
        case FILE_OVERWRITE_IF://Ultra Edit can delete file if read/write denied
            RequestedAccess |= DEVICE_DIRECT_WRITE;
        case FILE_OPEN:
        case FILE_OPEN_IF:

            if( OcIsFlagOn( IoStackLocation->Parameters.Create.SecurityContext->DesiredAccess, WRITE_ACCESS ) )
                RequestedAccess |= DEVICE_DIRECT_WRITE;

            if( OcIsFlagOn( IoStackLocation->Parameters.Create.SecurityContext->DesiredAccess, READ_ACCESS ) )
                RequestedAccess |= DEVICE_DIRECT_READ;
            break;
        }//switch( CreateDisposition ){

        break;

    case IRP_MJ_READ:

        //
        // if the file object is present then thi is a request to direct read 
        // by using a file object but not a read request from FSD
        //
        if( NULL != IoStackLocation->FileObject ){

#if DBG
            POC_FILE_OBJECT    OcFileObject = OcCrRetriveReferencedFileObject( IoStackLocation->FileObject );
            if( NULL != OcFileObject ){

                ASSERT( 0x1 == OcFileObject->Flags.DirectDeviceOpen );
                OcObDereferenceObject( OcFileObject );
            }
#endif//DBG

            RequestedAccess |= DEVICE_DIRECT_READ;
        }
        break;

    case IRP_MJ_WRITE:

        //
        // if the file object is present then this is a request to direct read 
        // by using a file object but not a read request from FSD,
        // this is with the great probability the user's request
        //
        if( NULL != IoStackLocation->FileObject ){

#if DBG
            POC_FILE_OBJECT    OcFileObject = OcCrRetriveReferencedFileObject( IoStackLocation->FileObject );
            if( NULL != OcFileObject ){

                ASSERT( 0x1 == OcFileObject->Flags.DirectDeviceOpen );
                OcObDereferenceObject( OcFileObject );
            }
#endif//DBG

            RequestedAccess |= DEVICE_DIRECT_WRITE;
        }
        break;

    case IRP_MJ_INTERNAL_DEVICE_CONTROL:// same as IRP_MJ_SCSI

        if( OcCrIsMajorScsiRequest( OcDiskDeviceObject, Context->RequestData.Irp ) ){

            //
            // this is a pure SCSI request, i.e. the request to a lower filter or a PDO
            //
            RequestedAccess |= OcCrGetRequestedAccessForIrpMjScsiRequest( Context, OcDiskDeviceObject );
            break;
        }
        //
        // NO BREAK here! Continue checking in case of an upper filter of a FDO!
        //
        ASSERT( !OcCrIsMajorScsiRequest( OcDiskDeviceObject, Context->RequestData.Irp ) );
        //
        // NO BREAK HERE! Continue checking in case of an upper filter of a FDO!
        //
    case IRP_MJ_DEVICE_CONTROL:

        ASSERT( !OcCrIsMajorScsiRequest( OcDiskDeviceObject, Context->RequestData.Irp ) );
        //
        // this is either 
        //  - a IRP_MJ_DEVICE_CONTROL or IRP_MJ_INTERNAL_DEVICE_CONTROL request to 
        //    an upper filter or FDO or IRP_MJ_DEVICE_CONTROL
        //  - a pure IRP_MJ_DEVICE_CONTROL request
        //

        //
        // call the function for processing SCSI IO requests,
        // in most cases this is a request from the either upper filter
        // or from the user mode, the Windows kernel or official drivers
        // usually do not send such requests at a disk level.
        // I not check here for a file object because this
        // might be direct write from the tools using
        // stand alone driver or upper filter.
        //
        RequestedAccess |= OcCrGetDiskIoctlRequestedAccess( Context );

        break;
    }

    //
    // REMOVE THIS
    //
#if DBG
    RequestedAccess |= DEVICE_DIRECT_READ;
#endif//DBG

    return RequestedAccess;
}

//------------------------------------------------

