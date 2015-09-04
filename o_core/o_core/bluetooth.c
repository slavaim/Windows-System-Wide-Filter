/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
06.09.2007 ( September )
 Start
*/

/*
this file contains the code for processing Bluetooth( Bth ) devices
*/

#include "struct.h"
#include "proto.h"
#include <1394.h>

//----------------------------------------------------

OC_ACCESS_RIGHTS 
OcCrGetBluetoothRequestedAccess(
    __in POC_NODE_CTX CONST    Context,
    __in POC_DEVICE_OBJECT     OcBthDeviceObject
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
    ASSERT( en_GUID_DEVCLASS_BLUETOOTH == (OcGetDeviceProperty( OcBthDeviceObject ))->SetupClassGuidIndex );
    ASSERT( OcIsFlagOn( Context->Flags, OcNodeCtxHookedDriverFlag ) ||
            OcIsFlagOn( Context->Flags, OcNodeCtxPnPFilterDriverFlag ) );

    IoStackLocation = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );

    switch( IoStackLocation->MajorFunction ){

    case IRP_MJ_DEVICE_CONTROL:
    case IRP_MJ_INTERNAL_DEVICE_CONTROL:
    case IRP_MJ_READ:
    case IRP_MJ_WRITE:

        RequestedAccess |= DEVICE_READ | DEVICE_WRITE;
    }

    return RequestedAccess;
}

//----------------------------------------------------