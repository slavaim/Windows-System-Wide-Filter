/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
30.08.2007 ( August )
 Start
*/

/*
this file contains the code for processing FireWire( IEEE1394 ) devices
*/

#include "struct.h"
#include "proto.h"
#include <1394.h>

//----------------------------------------------------

OC_ACCESS_RIGHTS 
OcCrGetIEEE1394RequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcIEEE1394DeviceObject
    )
    /*
    the function returns the access rights requested by the operation
    on a device( PDO, FDO or filter ) representing an IEEE1394 device
    */
{
    OC_ACCESS_RIGHTS      RequestedAccess = DEVICE_NO_ANY_ACCESS;
    PIO_STACK_LOCATION    IoStackLocation;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL != Context->RequestData.Irp );
    ASSERT( IO_TYPE_IRP == Context->RequestData.Irp->Type );
    ASSERT( en_GUID_DEVCLASS_1394 == (OcGetDeviceProperty( OcIEEE1394DeviceObject ))->SetupClassGuidIndex );
    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) );

    IoStackLocation = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );

    switch( IoStackLocation->MajorFunction ){

    PIRB    pIrb;

    case IRP_MJ_DEVICE_CONTROL:
    case IRP_MJ_INTERNAL_DEVICE_CONTROL:

        if( IoStackLocation->Parameters.DeviceIoControl.IoControlCode != IOCTL_1394_CLASS )
            break;

        pIrb = IoStackLocation->Parameters.Others.Argument1;
        if( NULL == pIrb )
            break;

        switch( pIrb->FunctionNumber )
        {
        case REQUEST_ASYNC_READ:
        case REQUEST_ASYNC_WRITE:
        case REQUEST_ASYNC_STREAM:
        case REQUEST_CONTROL:
        case REQUEST_ALLOCATE_ADDRESS_RANGE:
        case REQUEST_ISOCH_ALLOCATE_BANDWIDTH:
        case REQUEST_ISOCH_ALLOCATE_CHANNEL:
        case REQUEST_ISOCH_ALLOCATE_RESOURCES:
        case REQUEST_ISOCH_LISTEN:
        case REQUEST_ISOCH_TALK:

            RequestedAccess |= DEVICE_READ | DEVICE_WRITE;
            break;
        }
    }

    return RequestedAccess;
}

//-------------------------------------------------------------

OC_ACCESS_RIGHTS
OcCrConvertAnyRightsToIEEE1394Rights(
    IN OC_ACCESS_RIGHTS    AccessRights
    )
{
    return ( DEVICE_NO_ANY_ACCESS != AccessRights )? ( DEVICE_READ | DEVICE_WRITE ) : DEVICE_NO_ANY_ACCESS;
}

//----------------------------------------------------

