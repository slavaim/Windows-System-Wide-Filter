/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
25.01.2006 
 Start
*/

#include "struct.h"
#include "proto.h"
#include <PnPFilterControl.h>
#include <stdio.h>

/*
This file contains code which is used to create the database snapshot.
This code is inteneded to be used only in the debug mode.
*/

//-----------------------------------------------------------------

typedef struct _OC_CR_HASH_TRAVERSE_CONTEXT{
    HANDLE    OutputFile;
    PVOID     Buffer;
    ULONG     BufferSize;
} OC_CR_HASH_TRAVERSE_CONTEXT, *POC_CR_HASH_TRAVERSE_CONTEXT;

//-----------------------------------------------------------------

#define OcCrWriteTypeMoveCursor( _Cursor_, _Type_, _Value_ )\
    do{\
    *( (_Type_*)(_Cursor_) ) = (_Value_);  \
    ( (_Type_*)(_Cursor_) ) += 0x1;  \
    }while( FALSE )

//----------------------------------------------------

#define OC_STATIC_ANSI_BUFFER_SIZE   0x100
CHAR     g_AnsiBuffer[ OC_STATIC_ANSI_BUFFER_SIZE ];

//----------------------------------------------------

PCHAR
OcCrRelationsTypeToString(
    IN DEVICE_RELATION_TYPE    Relation
    )
{
    switch( Relation ){
        case BusRelations:
            return "BusRelations";
        case EjectionRelations:
            return "EjectionRelations";
        case PowerRelations:
            return "PowerRelations";
        case RemovalRelations:
            return "RemovalRelations";
        case TargetDeviceRelation:
            return "TargetDeviceRelation";
        default:
            return "Unknown Relations Type";
    }
}

//----------------------------------------------------

PCHAR
OcCrMajorDeviceTypeToString(
    IN DEVICE_TYPE    DeviceType
    )
{
    switch( DeviceType ){
        case DEVICE_TYPE_UNKNOWN:
            return "DEVICE_TYPE_UNKNOWN";
        case DEVICE_TYPE_FLOPPY:
            return "DEVICE_TYPE_FLOPPY";
        case DEVICE_TYPE_REMOVABLE:
            return "DEVICE_TYPE_REMOVABLE";
        case DEVICE_TYPE_HARD_DRIVE:
            return "DEVICE_TYPE_HARD_DRIVE";
        case DEVICE_TYPE_REMOTE:
            return "DEVICE_TYPE_REMOTE or DEVICE_TYPE_NETWORK_DISK";
        case DEVICE_TYPE_CD_ROM:
            return "DEVICE_TYPE_CD_ROM or DEVICE_TYPE_DVD";
        case DEVICE_TYPE_RAM_VOL:
            return "DEVICE_TYPE_RAM_VOL";
        case DEVICE_TYPE_SERIAL_PORT:
            return "DEVICE_TYPE_SERIAL_PORT or DEVICE_TYPE_MOUSE_PORT";
        case DEVICE_TYPE_LPT:
            return "DEVICE_TYPE_LPT";
        case DEVICE_TYPE_TAPE:
            return "DEVICE_TYPE_TAPE";
        case DEVICE_TYPE_USBHUB:
            return "DEVICE_TYPE_USBHUB";
        case DEVICE_TYPE_IRDA:
            return "DEVICE_TYPE_IRDA";
        case DEVICE_TYPE_1394:
            return "DEVICE_TYPE_1394";
        case DEVICE_TYPE_BLUETOOTH:
            return "DEVICE_TYPE_BLUETOOTH";
        case DEVICE_TYPE_WIFI:
            return "DEVICE_TYPE_WIFI";
        case DEVICE_TYPE_WINDOWS_MOBILE:
            return "DEVICE_TYPE_WINDOWS_MOBILE";
        default:
            return "Invalid Major Type";
    }

}

//----------------------------------------------------

PCHAR
OcCrMinorDeviceTypeToString(
    IN DEVICE_TYPE    MinorDeviceType
    )
{
    switch( MinorDeviceType ){

        case MINOR_TYPE_UNKNOWN:
            return "MINOR_TYPE_UNKNOWN";
        case MINOR_TYPE_FILE_SYSTEM_DRIVER:
            return "MINOR_TYPE_FILE_SYSTEM_DRIVER";
        case MINOR_TYPE_DISK_DRIVER:
            return "MINOR_TYPE_DISK_DRIVER";
        case MINOR_TYPE_USB_HUB:
            return "MINOR_TYPE_USB_HUB";
        case MINOR_TYPE_IEEE_1394:
            return "MINOR_TYPE_IEEE_1394";
        case MINOR_TYPE_USB_DUMMY:
            return "MINOR_TYPE_USB_DUMMY";
        case MINOR_TYPE_IEEE_1394_DUMMY:
            return "MINOR_TYPE_IEEE_1394_DUMMY";
        case MINOR_TYPE_CLASSDISK_DRIVER:
            return "MINOR_TYPE_CLASSDISK_DRIVER";
        case MINOR_TYPE_USBPRINT:
            return "MINOR_TYPE_USBPRINT";
        case MINOR_TYPE_BTHUSB:
            return "MINOR_TYPE_BTHUSB";
        case MINOR_TYPE_KBD_CLASS:
            return "MINOR_TYPE_KBD_CLASS";
        case MINOR_TYPE_SERIAL_PARALLEL:
            return "MINOR_TYPE_SERIAL_PARALLEL";
        case MINOR_TYPE_HIDUSB:
            return "MINOR_TYPE_HIDUSB";
        case MINOR_TYPE_IMAPI:
            return "MINOR_TYPE_IMAPI";
        case MINOR_TYPE_SBP2:
            return "MINOR_TYPE_SBP2 or MINOR_TYPE_FW_DISK";
        case MINOR_TYPE_BTHENUM:
            return "MINOR_TYPE_BTHENUM";
        case MINOR_TYPE_BTH_CONTROLLER_DYNAMIC:
            return "MINOR_TYPE_BTH_CONTROLLER_DYNAMIC";
        case MINOR_TYPE_BLUETOOTH_DUMMY:
            return "MINOR_TYPE_BLUETOOTH_DUMMY";
        case MINOR_TYPE_WIFI_DEVICE:
            return "MINOR_TYPE_WIFI_DEVICE";
        case MINOR_TYPE_BTH_DUMMY_COM:
            return "MINOR_TYPE_BTH_DUMMY_COM";
        case MINOR_TYPE_BTH_COM:
            return "MINOR_TYPE_BTH_COM";
        case MINOR_TYPE_NDISUIO:
            return "MINOR_TYPE_NDISUIO";
        case MINOR_TYPE_USBSCANER:
            return "MINOR_TYPE_USBSCANER";
        case MINOR_TYPE_USBNET:
            return "MINOR_TYPE_USBNET";
        case MINOR_TYPE_USBSTOR:
            return "MINOR_TYPE_USBSTOR";
        case MINOR_TYPE_IEEE1394_NET:
            return "MINOR_TYPE_IEEE1394_NET";
        case MINOR_TYPE_USBSTOR_DRIVER:
            return "MINOR_TYPE_USBSTOR_DRIVER";
        case MINOR_TYPES_CDROM_DEVICE:
            return "MINOR_TYPES_CDROM_DEVICE";
        default:
            {
                if( MinorDeviceType < TYPES_COUNT )
                    return OcCrMajorDeviceTypeToString( MinorDeviceType );

                return "Invalid Minor Type";
            }
    }

}

//----------------------------------------------------

PCHAR
OcCrPnPStateToString(
    IN DEVICE_PNP_STATE    PnPState
    )
{
    switch( PnPState ){
        case NotStarted:
            return "Not started yet";
        case Started:
            return "Device has received the START_DEVICE IRP";
        case StopPending:
            return "Device has received the QUERY_STOP IRP";
        case Stopped:
            return "Device has received the STOP_DEVICE IRP";
        case RemovePending:
            return "Device has received the QUERY_REMOVE IRP";
        case SurpriseRemovePending:
            return "Device has received the SURPRISE_REMOVE IRP";
        case Deleted:
            return "Device has received the REMOVE_DEVICE IRP";
        default:
            return "Unknown PnP State";
    }
}

//----------------------------------------------------

NTSTATUS
OcCrCreateDeviceNodeSnapshot(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject,
    IN OUT PVOID    Buffer,
    IN ULONG    BufferSize,
    OUT ULONG*    BytesWritten
    )
    /*
    the output buffer's layout is

    (ULONG)Size Of Device Object
    (POC_DEVICE_OBJECT)Device Object Address
    (OC_DEVICE_OBJECT)Device Object
    (ULONG)0x0 <- the trailing zero, present if there is enough space for it

    */
{
    PCHAR    BufferCursor = (PCHAR)Buffer;
    PCHAR    BufferStart = (PCHAR)Buffer;
    PCHAR    BufferOpenedEnd = (PCHAR)Buffer + BufferSize;
    POC_DEVICE_OBJECT    PtrLowerOcDeviceObject;
    ANSI_STRING    AnsiString;
    UNICODE_STRING    UnicodeString;
    ULONG       i;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // write the Device Object in the buffer
    //
    if( BufferSize < sizeof( ULONG ) + sizeof( PtrOcDeviceObject ) + sizeof( *PtrOcDeviceObject ) )
        return STATUS_BUFFER_TOO_SMALL;

    /*
    //
    // write the device object size
    //
    //OcCrWriteTypeMoveCursor( BufferCursor, ULONG, sizeof( *PtrOcDeviceObject ) );

    //
    // write the device object address
    //
    OcCrWriteTypeMoveCursor( BufferCursor, POC_DEVICE_OBJECT, PtrOcDeviceObject );

    //
    // write the device object
    //
    OcCrWriteTypeMoveCursor( BufferCursor, OC_DEVICE_OBJECT, *PtrOcDeviceObject );

    //
    // write the trailing zero if there is enough space
    //
    if( BufferCursor + sizeof( ULONG ) <= BufferOpenedEnd ){

        OcCrWriteTypeMoveCursor( BufferCursor, ULONG, 0x0 );
    }*/

    sprintf( BufferCursor, ">-----Start of Device Description------>\r\n \r\n" );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    sprintf( BufferCursor, " OC Device Object = 0x%p\r\n\r\n", (PVOID)PtrOcDeviceObject );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    if( PtrOcDeviceObject->DevicePropertyObject && 
        PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyClassName ){

        AnsiString.Length = 0x0;
        AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
        AnsiString.Buffer = g_AnsiBuffer;

        UnicodeString.Buffer = PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyClassName;
        UnicodeString.Length = (USHORT)wcslen( UnicodeString.Buffer )*sizeof( WCHAR );
        UnicodeString.MaximumLength = UnicodeString.Length;

        if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
                                                      &UnicodeString,
                                                      FALSE ) ) ){

                AnsiString.Buffer[ AnsiString.Length ] = '\0';
                sprintf( BufferCursor, "    Setup Class Name = %s\r\n", AnsiString.Buffer );
                BufferCursor = BufferCursor + strlen( BufferCursor );
        }
    }

    if( PtrOcDeviceObject->DevicePropertyObject && 
        PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyClassGuid ){

        AnsiString.Length = 0x0;
        AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
        AnsiString.Buffer = g_AnsiBuffer;

        UnicodeString.Buffer = PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyClassGuid;
        UnicodeString.Length = (USHORT)wcslen( UnicodeString.Buffer )*sizeof( WCHAR );
        UnicodeString.MaximumLength = UnicodeString.Length;

        if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
                                                      &UnicodeString,
                                                      FALSE ) ) ){

                AnsiString.Buffer[ AnsiString.Length ] = '\0';
                sprintf( BufferCursor, "    Setup Class GUID = %s\r\n", AnsiString.Buffer );
                BufferCursor = BufferCursor + strlen( BufferCursor );
        }
    }

    if( PtrOcDeviceObject->DevicePropertyObject && 
        PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyDriverKeyName ){

        AnsiString.Length = 0x0;
        AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
        AnsiString.Buffer = g_AnsiBuffer;

        UnicodeString.Buffer = PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyDriverKeyName;
        UnicodeString.Length = (USHORT)wcslen( UnicodeString.Buffer )*sizeof( WCHAR );
        UnicodeString.MaximumLength = UnicodeString.Length;

        if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
                                                      &UnicodeString,
                                                      FALSE ) ) ){

                AnsiString.Buffer[ AnsiString.Length ] = '\0';
                sprintf( BufferCursor, "    Driver Key Name = %s\r\n", AnsiString.Buffer );
                BufferCursor = BufferCursor + strlen( BufferCursor );
        }
    }

    if( 0x0 != PtrOcDeviceObject->DeviceNameInfo->Name.Length ){

        AnsiString.Length = 0x0;
        AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
        AnsiString.Buffer = g_AnsiBuffer;

        UnicodeString.Buffer = PtrOcDeviceObject->DeviceNameInfo->Name.Buffer;
        UnicodeString.Length = PtrOcDeviceObject->DeviceNameInfo->Name.Length;
        UnicodeString.MaximumLength = UnicodeString.Length;

        if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
                                                      &UnicodeString,
                                                      FALSE ) ) ){

                AnsiString.Buffer[ AnsiString.Length ] = '\0';
                sprintf( BufferCursor, "    Device Name = %s\r\n", AnsiString.Buffer );
                BufferCursor = BufferCursor + strlen( BufferCursor );
        }
    }

    {// start of the PnP type printing

        PCHAR    DevicePnPTypeString = NULL;

        sprintf( BufferCursor, "    DeviceObject->DevicePnPType = %i\r\n", (ULONG)PtrOcDeviceObject->DevicePnPType );
        BufferCursor = BufferCursor + strlen( BufferCursor );

        switch( PtrOcDeviceObject->DevicePnPType ){
        case OcDevicePnpTypeUnknow:
            DevicePnPTypeString = "OcDevicePnpTypeUnknow";
            break;
        case OcDevicePnPTypePdo:
            DevicePnPTypeString = "OcDevicePnPTypePdo";
            break;
        case OcDevicePnPTypeFilterDo:
            DevicePnPTypeString = "OcDevicePnPTypeFilterDo";
            break;
        case OcDevicePnPTypeFunctionalDo:
            DevicePnPTypeString = "OcDevicePnPTypeFunctionalDo";
            break;
        case OcDevicePnPTypeInMiddleOfStack:
            DevicePnPTypeString = "OcDevicePnPTypeInMiddleOfStack";
            break;
        case OcDeviceNoPnPTypeInMiddleOfStack:
            DevicePnPTypeString = "OcDeviceNoPnPTypeInMiddleOfStack";
            break;
        case OcDeviceLowerNoPnPType:
            DevicePnPTypeString = "OcDeviceLowerNoPnPType";
            break;
        default:
            ASSERT( !"An illegal PnP type for the device! Investigate immediatelly!" );
            DevicePnPTypeString = "Illegal PnP Type!";
            break;
        }

        ASSERT( NULL != DevicePnPTypeString );

        sprintf( BufferCursor, "    Friendly device PnP Type string = %s\r\n", DevicePnPTypeString );
        BufferCursor = BufferCursor + strlen( BufferCursor );

        if( OcDevicePnPTypeFilterDo == PtrOcDeviceObject->DevicePnPType ){

            if( 0x1 == PtrOcDeviceObject->Flags.LowerFilter ){

                ASSERT( 0x0 == PtrOcDeviceObject->Flags.UpperFilter );

                sprintf( BufferCursor, "    Device PnP Filter Type = %s\r\n", "Lower PnP Filter" );
                BufferCursor = BufferCursor + strlen( BufferCursor );

            } else {

                ASSERT( 0x1 == PtrOcDeviceObject->Flags.UpperFilter );

                sprintf( BufferCursor, "    Device PnP Filter Type = %s\r\n", "Upper PnP Filter" );
                BufferCursor = BufferCursor + strlen( BufferCursor );

            }//if( ... ) else ...

        }//if( OcDevicePnPTypeFilterDo == PtrOcDeviceObject->DevicePnPType )

    }// end of the PnP type printing

    if( PtrOcDeviceObject->DevicePropertyObject && 
        PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer ){

        AnsiString.Length = 0x0;
        AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
        AnsiString.Buffer = g_AnsiBuffer;

        UnicodeString.Buffer = PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer;
        UnicodeString.Length = (USHORT)wcslen( UnicodeString.Buffer )*sizeof( WCHAR );
        UnicodeString.MaximumLength = UnicodeString.Length;

        if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
                                                      &UnicodeString,
                                                      FALSE ) ) ){

                AnsiString.Buffer[ AnsiString.Length ] = '\0';
                sprintf( BufferCursor, "    PDO Name = %s\r\n", AnsiString.Buffer );
                BufferCursor = BufferCursor + strlen( BufferCursor );
        }
    }

    if( PtrOcDeviceObject->DevicePropertyObject && 
        PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyDeviceDescription ){

        AnsiString.Length = 0x0;
        AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
        AnsiString.Buffer = g_AnsiBuffer;

        UnicodeString.Buffer = PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyDeviceDescription;
        UnicodeString.Length = (USHORT)wcslen( UnicodeString.Buffer )*sizeof( WCHAR );
        UnicodeString.MaximumLength = UnicodeString.Length;

        if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
                                                      &UnicodeString,
                                                      FALSE ) ) ){

                AnsiString.Buffer[ AnsiString.Length ] = '\0';
                sprintf( BufferCursor, "    Device Description = %s\r\n", AnsiString.Buffer );
                BufferCursor = BufferCursor + strlen( BufferCursor );
        }
    }

    if( PtrOcDeviceObject->DevicePropertyObject && 
        PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyEnumeratorName ){

        AnsiString.Length = 0x0;
        AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
        AnsiString.Buffer = g_AnsiBuffer;

        UnicodeString.Buffer = PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyEnumeratorName;
        UnicodeString.Length = (USHORT)wcslen( UnicodeString.Buffer )*sizeof( WCHAR );
        UnicodeString.MaximumLength = UnicodeString.Length;

        if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
                                                      &UnicodeString,
                                                      FALSE ) ) ){

                AnsiString.Buffer[ AnsiString.Length ] = '\0';
                sprintf( BufferCursor, "    Device Enumeratopr = %s\r\n", AnsiString.Buffer );
                BufferCursor = BufferCursor + strlen( BufferCursor );
        }
    }

    if( PtrOcDeviceObject->DevicePropertyObject && 
        PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyPnpRegistryString ){

        AnsiString.Length = 0x0;
        AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
        AnsiString.Buffer = g_AnsiBuffer;

        if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
            &PtrOcDeviceObject->DevicePropertyObject->Header.DevicePropertyPnpRegistryString->Name,
            FALSE ) ) ){

                AnsiString.Buffer[ AnsiString.Length ] = '\0';
                sprintf( BufferCursor, "    Device PnP Registry Key = %s\r\n", AnsiString.Buffer );
                BufferCursor = BufferCursor + strlen( BufferCursor );
        }
    }

    sprintf( BufferCursor, "    DeviceObject->KernelDeviceObject = 0x%p\r\n", (PVOID)PtrOcDeviceObject->KernelDeviceObject );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    sprintf( BufferCursor, "    DeviceObject->Pdo = 0x%p\r\n", (PVOID)PtrOcDeviceObject->Pdo );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    sprintf( BufferCursor, "    DeviceObject->PnPState = %s\r\n", OcCrPnPStateToString( PtrOcDeviceObject->PnPState ) );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    //
    // print the device relations
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( PtrOcDeviceObject->DeviceRelations ); ++i ){

        //
        // first check without lock
        //
        if( NULL != PtrOcDeviceObject->DeviceRelations[ i ] ){

            POC_RELATIONS_OBJECT   PtrRelationsObject = NULL;
            KIRQL                  OldIrql;
            ULONG                  m;

            sprintf( BufferCursor, "\r\n" );
            BufferCursor = BufferCursor + strlen( BufferCursor );

            OcRwAcquireLockForRead( &PtrOcDeviceObject->RwSpinLock, &OldIrql );
            {// start of the lock
                PtrRelationsObject = PtrOcDeviceObject->DeviceRelations[ i ];
                if( NULL != PtrRelationsObject )
                    OcObReferenceObject( PtrRelationsObject );
            }// end of the lock
            OcRwReleaseReadLock( &PtrOcDeviceObject->RwSpinLock, OldIrql );

            if( NULL != PtrRelationsObject ){

                for( m = 0x0; m < PtrRelationsObject->Relations->Count; ++m ){

                    sprintf( BufferCursor, "    DeviceObject->Relations[ %s ][ %i ] = 0x%p\r\n", 
                             OcCrRelationsTypeToString( i ), 
                             m,
                             (PVOID)PtrRelationsObject->Relations->Objects[ m ] );
                    BufferCursor = BufferCursor + strlen( BufferCursor );

                }//for

                OcObDereferenceObject( PtrRelationsObject );
            }//if( NULL != PtrRelationsObject )
        }//if
    }//for

    sprintf( BufferCursor, "\r\n" );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    //
    // print the parent device objects
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( PtrOcDeviceObject->DependFrom ); ++i ){

        //
        // first check without lock
        //
        if( NULL != PtrOcDeviceObject->DependFrom[ i ] ){

            POC_DEVICE_OBJECT      PtrParentDeviceObject = NULL;
            KIRQL                  OldIrql;

            OcRwAcquireLockForRead( &PtrOcDeviceObject->RwSpinLock, &OldIrql );
            {// start of the lock
                PtrParentDeviceObject = PtrOcDeviceObject->DependFrom[ i ];
                if( NULL != PtrParentDeviceObject )
                    OcObReferenceObject( PtrParentDeviceObject );
            }// end of the lock
            OcRwReleaseReadLock( &PtrOcDeviceObject->RwSpinLock, OldIrql );

            if( NULL != PtrParentDeviceObject ){

                sprintf( BufferCursor, "    DeviceObject->DependFrom[ %s ] = 0x%p\r\n", 
                         OcCrRelationsTypeToString( i ), 
                         (PVOID)PtrParentDeviceObject );
                BufferCursor = BufferCursor + strlen( BufferCursor );

                OcObDereferenceObject( PtrParentDeviceObject );
            }//if( NULL != PtrRelationsObject )
        }//if
    }//for

    PtrLowerOcDeviceObject = OcCrGetLowerPdoDevice( PtrOcDeviceObject, NULL, 0x0 );
    if( NULL != PtrLowerOcDeviceObject ){

        sprintf( BufferCursor, "\r\n" );
        BufferCursor = BufferCursor + strlen( BufferCursor );

        sprintf( BufferCursor, "    Lower Device Object = 0x%p\r\n", (PVOID)PtrLowerOcDeviceObject );
        BufferCursor = BufferCursor + strlen( BufferCursor );

        OcObDereferenceObject( PtrLowerOcDeviceObject );
    }

    //
    // print the USB info
    //
    if( PtrOcDeviceObject->DevicePropertyObject && 
        0x1 == PtrOcDeviceObject->DevicePropertyObject->Header.PropertyType.USB ){

        POC_DEVICE_PROPERTY_USB_OBJECT    UsbDevProperty = 
            (POC_DEVICE_PROPERTY_USB_OBJECT)PtrOcDeviceObject->DevicePropertyObject;

        sprintf( BufferCursor, "\r\n" );
        BufferCursor = BufferCursor + strlen( BufferCursor );

        sprintf( BufferCursor, "    USB VendorId = 0x%X\r\n", UsbDevProperty->UsbDescriptor.StandardDescriptor.idVendor );
        BufferCursor = BufferCursor + strlen( BufferCursor );

        sprintf( BufferCursor, "    USB ProductId = 0x%X\r\n", UsbDevProperty->UsbDescriptor.StandardDescriptor.idProduct );
        BufferCursor = BufferCursor + strlen( BufferCursor );

        sprintf( BufferCursor, "    USB ClassId = 0x%X\r\n", UsbDevProperty->UsbDescriptor.UsbClassId );
        BufferCursor = BufferCursor + strlen( BufferCursor );

        if( NULL != UsbDevProperty->UsbDescriptor.CompatibleIds ){

            PWCHAR    StringString = (PWCHAR)UsbDevProperty->UsbDescriptor.CompatibleIds->Data;

            while( L'\0' != StringString[ 0x0 ] ){

                UNICODE_STRING    CompatibleIdString;

                RtlInitUnicodeString( &CompatibleIdString, StringString );

                StringString += ( CompatibleIdString.Length/sizeof( WCHAR ) + 0x1 );

                AnsiString.Length = 0x0;
                AnsiString.MaximumLength = sizeof( g_AnsiBuffer ) - sizeof( '\0' );
                AnsiString.Buffer = g_AnsiBuffer;

                if( NT_SUCCESS( RtlUnicodeStringToAnsiString( &AnsiString,
                                                              &CompatibleIdString,
                                                              FALSE ) ) ){

                        AnsiString.Buffer[ AnsiString.Length ] = '\0';
                        sprintf( BufferCursor, "    USB Compatible ID string = %s\r\n", AnsiString.Buffer );
                        BufferCursor = BufferCursor + strlen( BufferCursor );
                }

            }//while

        }//if( NULL != UsbDevProperty->UsbDescriptor.CompatibleIds )

    }

    //
    // print the device types
    //
    if( NULL != PtrOcDeviceObject->DeviceType ){

        KIRQL                     OldIrql;
        POC_DEVICE_TYPE_OBJECT    DeviceType;

        OcRwAcquireLockForWrite( &PtrOcDeviceObject->RwSpinLock, &OldIrql );
        {
            DeviceType = PtrOcDeviceObject->DeviceType;
            if( NULL != DeviceType )
                OcObReferenceObject( DeviceType );
        }
        OcRwReleaseWriteLock( &PtrOcDeviceObject->RwSpinLock, OldIrql );

        if( NULL != DeviceType ){

            ULONG    i;

            sprintf( BufferCursor, "\r\n" );
            BufferCursor = BufferCursor + strlen( BufferCursor );

            for( i = 0x0; 
                 DeviceType->TypeStack && i < DeviceType->TypeStack->NumberOfValidEntries;
                 ++i ){

                sprintf( BufferCursor, "\r\n" );
                BufferCursor = BufferCursor + strlen( BufferCursor );

                sprintf( BufferCursor, "    FullDeviceType[ %i ].FdoMajorType = %s\r\n", 
                         i, OcCrMajorDeviceTypeToString( DeviceType->TypeStack->FullDeviceType[ i ].FdoMajorType ) );
                BufferCursor = BufferCursor + strlen( BufferCursor );

                sprintf( BufferCursor, "    FullDeviceType[ %i ].PdoMajorType = %s\r\n", 
                         i, OcCrMajorDeviceTypeToString( DeviceType->TypeStack->FullDeviceType[ i ].PdoMajorType ) );
                BufferCursor = BufferCursor + strlen( BufferCursor );

                sprintf( BufferCursor, "    FullDeviceType[ %i ].PdoMinorType = %s\r\n", 
                         i, OcCrMinorDeviceTypeToString( DeviceType->TypeStack->FullDeviceType[ i ].PdoMinorType ) );
                BufferCursor = BufferCursor + strlen( BufferCursor );

            }

            OcObDereferenceObject( DeviceType );

            sprintf( BufferCursor, "\r\n" );
            BufferCursor = BufferCursor + strlen( BufferCursor );

        }

    }

    sprintf( BufferCursor, "\r\n<-----End of Device Description-----<\r\n" );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    sprintf( BufferCursor, " \r\n" );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    sprintf( BufferCursor, " \r\n" );
    BufferCursor = BufferCursor + strlen( BufferCursor );

    *BytesWritten = (ULONG)( BufferCursor - (PCHAR)Buffer );

    return STATUS_SUCCESS;
}

//-----------------------------------------------------------------

VOID
NTAPI
OcCrProcessHashDeviceObject(
    IN PVOID    Context,//POC_DEVICE_OBJECT
    IN PVOID    ContextEx//POC_CR_HASH_TRAVERSE_CONTEXT
    )
{
    NTSTATUS    RC;
    POC_DEVICE_OBJECT    PtrOcDeviceObject;
    POC_CR_HASH_TRAVERSE_CONTEXT    PtrTraverseContext;
    IO_STATUS_BLOCK    IoStatusBlock;
    ULONG              BytesWritten;

    PtrOcDeviceObject = (POC_DEVICE_OBJECT)Context;
    PtrTraverseContext = (POC_CR_HASH_TRAVERSE_CONTEXT)ContextEx;

    RC = OcCrCreateDeviceNodeSnapshot( PtrOcDeviceObject,
                                       PtrTraverseContext->Buffer,
                                       PtrTraverseContext->BufferSize,
                                       &BytesWritten );

    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        return;

    //
    // write the buffer in the file,
    // the file must be opened with 
    // FILE_SYNCHRONOUS_IO_NONALERT flag!
    //
    RC = ZwWriteFile( PtrTraverseContext->OutputFile,
                      NULL,
                      NULL,
                      NULL,
                      &IoStatusBlock,
                      PtrTraverseContext->Buffer,
                      BytesWritten,
                      NULL,
                      NULL );

    ASSERT( RC != STATUS_PENDING );

    return;
}

//-----------------------------------------------------------------

NTSTATUS
OcCrCreateDeviceDataBaseSnapshot(
    IN HANDLE    OutputFile
    )
{
    OC_CR_HASH_TRAVERSE_CONTEXT    TraverseContext;

    TraverseContext.OutputFile = OutputFile;
    TraverseContext.BufferSize = 0x2000;
    TraverseContext.Buffer = ExAllocatePoolWithTag( PagedPool, TraverseContext.BufferSize, 'rTcO' );
    if( NULL == TraverseContext.Buffer )
        return STATUS_INSUFFICIENT_RESOURCES;

    OcHsTraverseAllEntriesInHash( Global.PtrDeviceHashObject,
                                  OcCrProcessHashDeviceObject,
                                  &TraverseContext );

    ExFreePoolWithTag( TraverseContext.Buffer, 'rTcO' );

    return STATUS_SUCCESS;
}

//-----------------------------------------------------------------

NTSTATUS
OcCrWriteDeviceDataBaseInFile(
    IN PUNICODE_STRING    FileName
    )
{
    NTSTATUS             RC;
    OBJECT_ATTRIBUTES    ObjectAttributes;
    HANDLE               FileHandle;
    IO_STATUS_BLOCK      IoStatusBlock;

    InitializeObjectAttributes( &ObjectAttributes,
                                FileName,
                                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                NULL,
                                NULL
                                );

    RC = ZwCreateFile( &FileHandle,
                       GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                       &ObjectAttributes,
                       &IoStatusBlock,
                       NULL, 
                       FILE_ATTRIBUTE_NORMAL,
                       0x0,// sharing is disabled
                       FILE_SUPERSEDE,
                       FILE_NON_DIRECTORY_FILE |
                       FILE_SEQUENTIAL_ONLY |
                       FILE_SYNCHRONOUS_IO_NONALERT,
                       NULL,
                       0
                       );

    ASSERT( RC != STATUS_PENDING );

    if( NT_SUCCESS( RC ) ){

        RC = OcCrCreateDeviceDataBaseSnapshot( FileHandle );
        ZwClose( FileHandle);
    }

    return RC;
}

//-----------------------------------------------------------------

