/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
26.04.2007 ( April )
 Start
*/

/*
this file contains the code for usb devices' 
requests processing
*/

#include "struct.h"
#include "proto.h"
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>
#include <usbdi.h>

//-------------------------------------------------------------

#define    OC_DEFAULT_USB_RIGHTS   ( DEVICE_READ | DEVICE_WRITE )

//-------------------------------------------------------------

OC_ACCESS_RIGHTS 
OcCrGetUsbRequestedAccess(
    IN POC_NODE_CTX CONST    Context,
    IN POC_DEVICE_OBJECT     OcUsbDeviceObject
    )
    /*
    the function returns access requested by the operation on the USB bus PDO
    */
{
    OC_ACCESS_RIGHTS    RequestedAccess = DEVICE_NO_ANY_ACCESS;
    PURB                Urb;
    USHORT              Function;
    PIO_STACK_LOCATION  StackLocation;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL != Context->RequestData.Irp );
    ASSERT( IO_TYPE_IRP == Context->RequestData.Irp->Type );
    ASSERT( en_GUID_DEVCLASS_USB == (OcGetDeviceProperty( OcUsbDeviceObject ))->SetupClassGuidIndex );

    StackLocation = IoGetCurrentIrpStackLocation( Context->RequestData.Irp );

    if( IRP_MJ_INTERNAL_DEVICE_CONTROL != StackLocation->MajorFunction )
        return RequestedAccess;

    if( IOCTL_INTERNAL_USB_SUBMIT_URB != StackLocation->Parameters.DeviceIoControl.IoControlCode )
        return RequestedAccess;

    Urb = (PURB)StackLocation->Parameters.Others.Argument1;
    Function = Urb->UrbHeader.Function;

    switch( Function )
    {
#define CASE_URB_FUNCTION_COMMON( _URB_FUNCTION, _URB_STRUCT)            \
    case _URB_FUNCTION:                                                  \
    {                                                                    \
            struct _URB_STRUCT *UrbStruct = ( struct _URB_STRUCT* )Urb;  \
                                                                         \
            if( UrbStruct->Hdr.Length < sizeof( struct _URB_STRUCT ) )   \
            {                                                            \
                break;                                                   \
            }                                                            \
                                                                         \
            if( 0x0 != UrbStruct->TransferBufferLength || NULL != UrbStruct->UrbLink )  \
                RequestedAccess |= OC_DEFAULT_USB_RIGHTS;                \
                                                                         \
            break;                                                       \
    };

#ifndef URB_FUNCTION_SYNC_RESET_PIPE
  #define URB_FUNCTION_SYNC_RESET_PIPE                 0x0030
  #define URB_FUNCTION_SYNC_CLEAR_STALL                0x0031
#endif

    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SELECT_CONFIGURATION,          _URB_SELECT_CONFIGURATION);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SELECT_INTERFACE,              _URB_SELECT_INTERFACE);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_ABORT_PIPE,                    _URB_PIPE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL,     _URB_FRAME_LENGTH_CONTROL); 
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL,  _URB_FRAME_LENGTH_CONTROL);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_FRAME_LENGTH,              _URB_GET_FRAME_LENGTH);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SET_FRAME_LENGTH,              _URB_GET_FRAME_LENGTH);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_CURRENT_FRAME_NUMBER,      _URB_GET_CURRENT_FRAME_NUMBER);

    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CONTROL_TRANSFER,              _URB_CONTROL_TRANSFER);
    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER,    _URB_BULK_OR_INTERRUPT_TRANSFER); 
    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_ISOCH_TRANSFER,                _URB_ISOCH_TRANSFER); 

    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_RESET_PIPE,                    _URB_PIPE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SYNC_RESET_PIPE,               _URB_PIPE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SYNC_CLEAR_STALL,              _URB_PIPE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE,    _URB_CONTROL_DESCRIPTOR_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT,  _URB_CONTROL_DESCRIPTOR_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE, _URB_CONTROL_DESCRIPTOR_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE,      _URB_CONTROL_DESCRIPTOR_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT,    _URB_CONTROL_DESCRIPTOR_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE,   _URB_CONTROL_DESCRIPTOR_REQUEST);

    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SET_FEATURE_TO_DEVICE,         _URB_CONTROL_FEATURE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SET_FEATURE_TO_INTERFACE,      _URB_CONTROL_FEATURE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SET_FEATURE_TO_ENDPOINT,       _URB_CONTROL_FEATURE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_SET_FEATURE_TO_OTHER,          _URB_CONTROL_FEATURE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE,       _URB_CONTROL_FEATURE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE,    _URB_CONTROL_FEATURE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT,     _URB_CONTROL_FEATURE_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CLEAR_FEATURE_TO_OTHER,        _URB_CONTROL_FEATURE_REQUEST); 

    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_STATUS_FROM_DEVICE,        _URB_CONTROL_GET_STATUS_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_STATUS_FROM_INTERFACE,     _URB_CONTROL_GET_STATUS_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_STATUS_FROM_ENDPOINT,      _URB_CONTROL_GET_STATUS_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_STATUS_FROM_OTHER,         _URB_CONTROL_GET_STATUS_REQUEST); 

    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_VENDOR_DEVICE,                 _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_VENDOR_INTERFACE,              _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_VENDOR_ENDPOINT,               _URB_CONTROL_VENDOR_OR_CLASS_REQUEST); //ruToken uses only this request
    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_VENDOR_OTHER,                  _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);

    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CLASS_DEVICE,                  _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CLASS_INTERFACE,               _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CLASS_ENDPOINT,                _URB_CONTROL_VENDOR_OR_CLASS_REQUEST); 
    CASE_URB_FUNCTION_COMMON( URB_FUNCTION_CLASS_OTHER,                   _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);

    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_CONFIGURATION,             _URB_CONTROL_GET_CONFIGURATION_REQUEST);
    //CASE_URB_FUNCTION_COMMON( URB_FUNCTION_GET_INTERFACE,                 _URB_CONTROL_GET_INTERFACE_REQUEST);

    case URB_FUNCTION_RESERVED0:
    case URB_FUNCTION_RESERVED:

        RequestedAccess |= OC_DEFAULT_USB_RIGHTS;
        break;

#undef CASE_URB_FUNCTION_COMMON
    }

    return RequestedAccess;
}

//-------------------------------------------------------------

OC_ACCESS_RIGHTS
OcCrConvertAntRightsToUsbRights(
    __in OC_ACCESS_RIGHTS    AccessRights
    )
{
    return ( DEVICE_NO_ANY_ACCESS != AccessRights )? OC_DEFAULT_USB_RIGHTS : DEVICE_NO_ANY_ACCESS;
}

//-------------------------------------------------------------

