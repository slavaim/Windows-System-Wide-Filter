/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
26.04.2007 ( April )
 Start
*/

/*
this file contains the code for the 
security descriptors management
*/
#include "struct.h"
#include "proto.h"

//------------------------------------------------------

typedef struct _OC_SECURITY_DESCRIPTOR_OBJECT{

    //
    // the device type for which this entry contains SDs
    //
    OC_DEVICE_TYPE           DeviceType;

    //
    // type of the SD
    //
    OC_SD_TYPE               SdType;

    //
    // the size of the security descriptor
    //
    ULONG                    SdSize;

    //
    // pointer to security descriptor, actually security descriptor
    // at the end of this structure
    //
    PSECURITY_DESCRIPTOR     SecurityDescriptor;

    //
    // this field is used for alignment on 8 byte boundary
    // to avoid different complication with SD at the end of
    // this structure
    //
    LONGLONG                 Alignment;

    //
    // at the end is variable length buffer with the security descriptor
    //
    // Security descriptor of size SdSize

} OC_SECURITY_DESCRIPTOR_OBJECT, *POC_SECURITY_DESCRIPTOR_OBJECT;


typedef struct _OC_SECURITY_DESCRIPTORS_TABLE_ENTRY{

    //
    // device type for which this entry contains SDs
    //
    OC_DEVICE_TYPE                    DeviceType;

    //
    // read write locks for protecting SdObjects array
    //
    OC_RW_SPIN_LOCK                   RwLockForSd[ OcMaximumSdType ];

    //
    // the following array conatins a pointer to a security
    // descriptor for each type( access, log, etc ) 
    // all security descriptors are referenced
    //
    POC_SECURITY_DESCRIPTOR_OBJECT    SdObjects[ OcMaximumSdType ];

    //
    // the following array says what to return if an entry in the 
    // SdObjects array is NULL
    //
    BOOLEAN                           ActionForNullSd[ OcMaximumSdType ];

    //
    // mapping from the general rights to device specific ones
    //
    GENERIC_MAPPING                   GenericMapping;

} OC_SECURITY_DESCRIPTORS_TABLE_ENTRY, *POC_SECURITY_DESCRIPTORS_TABLE_ENTRY;

//------------------------------------------------------

#define OC_NUMBER_OF_STACKED_TYPES   0x3

typedef struct _OC_DEVICE_TYPE_CONTEXT{
    POC_FULL_DEVICE_TYPE_STACK    DeviceTypeStack;
    POC_DEVICE_OBJECT             OcDeviceObject;
    OC_EN_SETUP_CLASS_GUID        UpperSetupClassGuidIndex;
    BOOLEAN                       BufferExhausted;
} OC_DEVICE_TYPE_CONTEXT, *POC_DEVICE_TYPE_CONTEXT;

//------------------------------------------------------

#if DBG
VOID
NTAPI
OcCrDeleteSecurityDescriptorObject(
    __in POC_SECURITY_DESCRIPTOR_OBJECT    OperationObject
    );
#endif//DBG

//------------------------------------------------------

//
// object type for security descriptor object
//
static OC_OBJECT_TYPE    g_OcSecurityDescriptorObjectType;

//
// the global table with security descriptor objects for each device type
//
static OC_SECURITY_DESCRIPTORS_TABLE_ENTRY     g_SdTable[ TYPES_COUNT + MINOR_TYPES_COUNT ];

//
// the matrix defines translation of a pair of types to a minor type,
// the matrix is not symmetric, because this is meaningless from
// the technical point of view, though is convinient
// from the programming one
//
static OC_DEVICE_TYPE    UpperAndLowerTypesToMinorType[ TYPES_COUNT/*Upper*/ ][ TYPES_COUNT/*Lower*/ ];

//
// the matrix defines the types redefinition made by a lower type
//
static OC_DEVICE_TYPE    UpperAndLowerTypesToMajorType[ TYPES_COUNT/*Upper*/ ][ TYPES_COUNT/*Lower*/ ];

//
// the following 2D array converts device type and setup class to either minor or base type
//
static OC_DEVICE_TYPE    DeviceTypeAndSetupClassToType[ TYPES_COUNT ][ en_GUID_DEVCLASS_LAST ];

//
// the following array sets the correspondence between device setup class and device type
//
static OC_DEVICE_TYPE    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_LAST ];

//
// the following array sets the correspondence between device enumerator and device type
//
static OC_DEVICE_TYPE    DeviceEnumeratorToDeviceType[ en_LastEnumerator ];

//
// the thread pool is used to call SeAccessCheck at PASSIVE_LEVEL for callers
// running at APC_LEVEL, should pool threads creating fail the check is made
// in the context of the caller, i.e. at APC_LEVEL
//
static POC_THREAD_POOL_OBJECT    g_SecurityCheckThreadPool = NULL;

static BOOLEAN    g_SecuritySubsystemInitialized = FALSE;

//------------------------------------------------------

__forceinline
VOID
OcCrFillArray(
    __in PVOID    Array,
    __in ULONG    ElemSize,// 1, 2, 4 or 8 bytes
    __in ULONG    ElemNumber,
    __in ULONG_PTR    Value
    )
{
    switch( ElemSize ){

        case 1:
            {

                PCHAR    CharArray = (PCHAR)Array;
                PCHAR    ArrayOpenedEnd = CharArray + ElemNumber;

                while( CharArray < ArrayOpenedEnd ){

                    *CharArray = (CHAR)Value;
                    ++CharArray;
                }

            }
            break;

        case 2:
            {

                PSHORT    ShortArray = (PSHORT)Array;
                PSHORT    ArrayOpenedEnd = ShortArray + ElemNumber;

                while( ShortArray < ArrayOpenedEnd ){

                    *ShortArray = (SHORT)Value;
                    ++ShortArray;
                }

            }
            break;

        case 4:
            {

                PULONG    UlongArray = (PULONG)Array;
                PULONG    ArrayOpenedEnd = UlongArray + ElemNumber;

                while( UlongArray < ArrayOpenedEnd ){

                    *UlongArray = (ULONG)Value;
                    ++UlongArray;
                }

            }
            break;

        case 8:
            {

                PULONGLONG    UlongLongArray = (PULONGLONG)Array;
                PULONGLONG    ArrayOpenedEnd = UlongLongArray + ElemNumber;

                while( UlongLongArray < ArrayOpenedEnd ){

                    *UlongLongArray = (ULONGLONG)Value;
                    ++UlongLongArray;
                }

            }
            break;

        default:
            ASSERT( !"Unsupported size" );
    }

}

//------------------------------------------------------

__forceinline
VOID
OcSetTypesTranslation(
    __in OC_DEVICE_TYPE  UpperDeviceType,
    __in OC_DEVICE_TYPE  LowerDeviceType,
    __in OC_DEVICE_TYPE  InferredType
    )
    /*
    LowerDeviceType and UpperDeviceType may be DEVICE_TYPE_UNKNOWN in that case
    the MinorType is set for a whole raw or column
    */
{
    ASSERT( UpperDeviceType < TYPES_COUNT );
    ASSERT( LowerDeviceType < TYPES_COUNT );
    ASSERT( !( DEVICE_TYPE_UNKNOWN == UpperDeviceType && DEVICE_TYPE_UNKNOWN == LowerDeviceType ) );

    //
    // if one of the type is unknown then
    // set the entire raw or column to the MinorType
    //
    if( DEVICE_TYPE_UNKNOWN == UpperDeviceType || DEVICE_TYPE_UNKNOWN == LowerDeviceType ){

        int    i;

        for( i = 0x0; i< TYPES_COUNT; ++i ){

            if( DEVICE_TYPE_UNKNOWN == LowerDeviceType ){
                //
                // warn me in case of rewriting!
                //
                ASSERT( MINOR_TYPE_UNKNOWN == UpperAndLowerTypesToMinorType [ UpperDeviceType ][ i ] || 
                        InferredType == UpperAndLowerTypesToMinorType [ UpperDeviceType ][ i ] );

                //
                // the matrix is not symmetric!
                //
                UpperAndLowerTypesToMinorType[ UpperDeviceType ][ i ] = InferredType;

            } else {

                ASSERT( DEVICE_TYPE_UNKNOWN == UpperDeviceType );

                //
                // warn me in case of rewriting!
                //
                ASSERT( MINOR_TYPE_UNKNOWN == UpperAndLowerTypesToMinorType [ i ][ LowerDeviceType ] || 
                        InferredType == UpperAndLowerTypesToMinorType [ i ][ LowerDeviceType ] );

                //
                // the matrix is not symmetric!
                //
                UpperAndLowerTypesToMinorType [ i ][ LowerDeviceType ] = InferredType;
            }
        }// for

        return;
    }

    //
    // the matrix is not symmetric!
    //
    ASSERT( MINOR_TYPE_UNKNOWN == UpperAndLowerTypesToMinorType [ UpperDeviceType ][ LowerDeviceType ] || 
            InferredType == UpperAndLowerTypesToMinorType [ UpperDeviceType ][ LowerDeviceType ] || 
            UpperAndLowerTypesToMinorType [ DEVICE_TYPE_UNKNOWN ][ LowerDeviceType ] == UpperAndLowerTypesToMinorType [ UpperDeviceType ][ LowerDeviceType ] || 
            UpperAndLowerTypesToMinorType [ UpperDeviceType ][ DEVICE_TYPE_UNKNOWN ] == UpperAndLowerTypesToMinorType [ UpperDeviceType ][ LowerDeviceType ] );

    UpperAndLowerTypesToMinorType [ UpperDeviceType ][ LowerDeviceType ] = InferredType;
}

//------------------------------------------------------

__forceinline
VOID
OcSetMajorTypesRedefinition(
    __in OC_DEVICE_TYPE  UpperDeviceType,
    __in OC_DEVICE_TYPE  LowerDeviceType,
    __in OC_DEVICE_TYPE  InferredType
    )
    /*
    LowerDeviceType and UpperDeviceType may be DEVICE_TYPE_UNKNOWN in that case
    the MinorType is set for a whole raw and column
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( UpperDeviceType < TYPES_COUNT );
    ASSERT( LowerDeviceType < TYPES_COUNT );
    ASSERT( !( DEVICE_TYPE_UNKNOWN == UpperDeviceType && DEVICE_TYPE_UNKNOWN == LowerDeviceType ) );

    //
    // if one of the type is unknown then
    // set the entire raw or column to the MinorType
    //
    if( DEVICE_TYPE_UNKNOWN == UpperDeviceType || DEVICE_TYPE_UNKNOWN == LowerDeviceType ){

        int    i;

        for( i = 0x0; i< TYPES_COUNT; ++i ){

            if( DEVICE_TYPE_UNKNOWN == LowerDeviceType ){
                //
                // warn me in case of rewriting!
                //
                ASSERT( DEVICE_TYPE_UNKNOWN == UpperAndLowerTypesToMajorType [ UpperDeviceType ][ i ] || 
                        InferredType == UpperAndLowerTypesToMajorType [ UpperDeviceType ][ i ] );

                //
                // the matrix is not symmetric!
                //
                UpperAndLowerTypesToMajorType[ UpperDeviceType ][ i ] = InferredType;

            } else {

                ASSERT( DEVICE_TYPE_UNKNOWN == UpperDeviceType );

                //
                // warn me in case of rewriting!
                //
                ASSERT( DEVICE_TYPE_UNKNOWN == UpperAndLowerTypesToMajorType [ i ][ LowerDeviceType ] || 
                        InferredType == UpperAndLowerTypesToMajorType [ i ][ LowerDeviceType ] );

                //
                // the matrix is not symmetric!
                //
                UpperAndLowerTypesToMajorType [ i ][ LowerDeviceType ] = InferredType;
            }
        }// for

        return;
    }

    //
    // the matrix is not symmetric!
    //
    ASSERT( DEVICE_TYPE_UNKNOWN == UpperAndLowerTypesToMajorType [ UpperDeviceType ][ LowerDeviceType ] || 
            InferredType == UpperAndLowerTypesToMajorType [ UpperDeviceType ][ LowerDeviceType ] || 
            UpperAndLowerTypesToMajorType [ DEVICE_TYPE_UNKNOWN ][ LowerDeviceType ] == UpperAndLowerTypesToMajorType [ UpperDeviceType ][ LowerDeviceType ] || 
            UpperAndLowerTypesToMajorType [ UpperDeviceType ][ DEVICE_TYPE_UNKNOWN ] == UpperAndLowerTypesToMajorType [ UpperDeviceType ][ LowerDeviceType ] );

    UpperAndLowerTypesToMajorType [ UpperDeviceType ][ LowerDeviceType ] = InferredType;
}

//------------------------------------------------------

__forceinline
OC_DEVICE_TYPE
OcGetFdoMofifiedType(
    __in OC_DEVICE_TYPE  FdoDeviceType,
    __in OC_DEVICE_TYPE  PdoDeviceType
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( FdoDeviceType < TYPES_COUNT );
    ASSERT( PdoDeviceType < TYPES_COUNT );

    return UpperAndLowerTypesToMajorType[ FdoDeviceType ][ PdoDeviceType ];
}

//------------------------------------------------------

__forceinline
OC_DEVICE_TYPE
OcGetDeviceTypeFromSetupClass(
    __in OC_EN_SETUP_CLASS_GUID  DeviceSetupClass
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( DeviceSetupClass < OC_STATIC_ARRAY_SIZE( DeviceSetupClassToDeviceType ) );
    ASSERT( DeviceSetupClass < en_GUID_DEVCLASS_LAST );

    return DeviceSetupClassToDeviceType[ DeviceSetupClass ];
}

//------------------------------------------------------

__forceinline
OC_DEVICE_TYPE
OcGetDeviceTypeFromEnumeratorBus(
    __in OC_EN_ENUMERATOR  DeviceEnumeratorBus
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( DeviceEnumeratorBus < OC_STATIC_ARRAY_SIZE( DeviceEnumeratorToDeviceType ) );
    ASSERT( DeviceEnumeratorBus < en_LastEnumerator );

    return DeviceEnumeratorToDeviceType[ DeviceEnumeratorBus ];
}

//------------------------------------------------------

__forceinline
OC_DEVICE_TYPE
OcGetMinorDeviceTypeFromPdoTypeAndSetupClass(
    __in OC_DEVICE_TYPE  PdoDeviceType,
    __in OC_EN_SETUP_CLASS_GUID  DeviceSetupClass
    )
{

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( PdoDeviceType < TYPES_COUNT );
    ASSERT( DeviceSetupClass < OC_STATIC_ARRAY_SIZE( DeviceSetupClassToDeviceType ) );
    ASSERT( DeviceSetupClass < en_GUID_DEVCLASS_LAST );

    return DeviceTypeAndSetupClassToType[ PdoDeviceType ][ DeviceSetupClass ];
}

//------------------------------------------------------

__forceinline
VOID
OcSetLowerTypeAndUpperSetupClassTranslation(
    __in OC_DEVICE_TYPE  DeviceType,
    __in OC_EN_SETUP_CLASS_GUID  DeviceSetupClass,
    __in OC_DEVICE_TYPE  InferredType
    )
    /*
    SetupClass may be en_OC_GUID_DEVCLASS_UNKNOWN
    */
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( DeviceType < TYPES_COUNT );
    ASSERT( DeviceSetupClass < en_GUID_DEVCLASS_LAST );

    //
    // if one setup class is unknown then
    // set the entire raw to the MinorType,
    // i.e. all devices which grows from this
    // device will have the MinorType if the
    // type will have not been redefined
    //
    if( en_OC_GUID_DEVCLASS_UNKNOWN == DeviceSetupClass ){

        ULONG    i;

        for( i = 0x0; i< en_GUID_DEVCLASS_LAST; ++i ){

            //
            // warn me in case of rewriting!
            //
            ASSERT( MINOR_TYPE_UNKNOWN == DeviceTypeAndSetupClassToType [ DeviceType ][ i ] );

            //
            // set the entire raw to InferredType
            //
            DeviceTypeAndSetupClassToType [ DeviceType ][ i ] = InferredType;
        }

        return;
    }

    DeviceTypeAndSetupClassToType[ DeviceType ][ DeviceSetupClass ] = InferredType;
}

//------------------------------------------------------

__forceinline
ULONG
OcSdGetSdTableIndexByDeviceType(
    __in OC_DEVICE_TYPE    DeviceType
    )
{
    if( DeviceType < TYPES_COUNT )
        return (ULONG)DeviceType;
    else{
        ASSERT( DeviceType >= MINOR_TYPE_BASE );
        return ( (ULONG)DeviceType - MINOR_TYPE_BASE + TYPES_COUNT );
    }
}

//------------------------------------------------------

__forceinline
OC_DEVICE_TYPE
OcSdGetDeviceTypeBySdTableIndex(
    __in ULONG    TableIndex
    )
{
    if( TableIndex < TYPES_COUNT )
        return (OC_DEVICE_TYPE)TableIndex;
    else
        return (OC_DEVICE_TYPE)( ( TableIndex - TYPES_COUNT ) + MINOR_TYPE_BASE );
}

//------------------------------------------------------

NTSTATUS
OcSdInitializeSecurityDescriptorsSubsystem()
{
    NTSTATUS    RC = STATUS_SUCCESS;
    OC_OBJECT_TYPE_INITIALIZER_VAR( ObjectTypeInitializer );
    ULONG    i;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcObIsObjectManagerInitialized() );

    RtlZeroMemory( &g_SdTable, sizeof( g_SdTable ) );

    //
    // start the threads pool at the beginning as it is very hard to
    // roll back the initialization in case of error
    //
    RC = OcTplCreateThreadPool( Global.BaseNumberOfWorkerThreads+0x1,
                                FALSE,
                                &g_SecurityCheckThreadPool );
    if( !NT_SUCCESS( RC ) ){

        //
        // something definitely went wrong
        //
        ASSERT( !"The security threads pool creating fails!" );
        g_SecurityCheckThreadPool = NULL;

        goto __exit;
    }

    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( g_SdTable ); ++i ){

        ULONG    m;

        for( m = 0x0; m < OC_STATIC_ARRAY_SIZE( g_SdTable[ i ].RwLockForSd ); ++m ){

            OcRwInitializeRwLock( &g_SdTable[ i ].RwLockForSd[ m ] );

        }//for( m = 0x0; m < OC_STATIC_ARRAY_SIZE( g_SdTable[ i ].RwLockForSd ); ++m 

        g_SdTable[ i ].DeviceType = OcSdGetDeviceTypeBySdTableIndex( i );

        //
        // NULL for access SD means - allow access for all
        //
        g_SdTable[ i ].ActionForNullSd[ OcAccessSd ] = TRUE;

        //
        // NULL for log SD means - do not log
        //
        g_SdTable[ i ].ActionForNullSd[ OcLogAllowedSd ] = FALSE;
        g_SdTable[ i ].ActionForNullSd[ OcLogDeniedSd ] = FALSE;

        //
        // set the generic mapping ( i.e. mapping of 
        // general rights such as GENERIC_READ to device specific ones )
        //
        g_SdTable[ i ].GenericMapping.GenericRead = DEVICE_ENCRYPTED_READ | 
                                     DEVICE_DIR_LIST | 
                                     DEVICE_READ | 
                                     DEVICE_PLAY_AUDIO_CD | 
                                     DEVICE_DIRECT_READ;

        g_SdTable[ i ].GenericMapping.GenericWrite = DEVICE_ENCRYPTED_DIRECT_WRITE | 
                                      DEVICE_ENCRYPTED_WRITE | 
                                      DEVICE_DIR_CREATE | 
                                      DEVICE_WRITE | 
                                      DEVICE_RENAME | 
                                      DEVICE_DELETE | 
                                      DEVICE_DIRECT_WRITE | 
                                      DEVICE_DISK_FORMAT | 
                                      DEVICE_VOLUME_DEFRAGMENT;

        g_SdTable[ i ].GenericMapping.GenericExecute = DEVICE_EXECUTE;

        g_SdTable[ i ].GenericMapping.GenericAll = g_SdTable[ i ].GenericMapping.GenericRead | 
                                                   g_SdTable[ i ].GenericMapping.GenericWrite | 
                                                   g_SdTable[ i ].GenericMapping.GenericExecute;

    }// for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( g_SdTable ); ++i )

    //
    // initialize the security descriptor object type
    //
    OC_TOGGLE_TYPE_INITIALIZER( &ObjectTypeInitializer );
    ObjectTypeInitializer.Tag = 'DScO';
    ObjectTypeInitializer.Flags = OcObjectTypeUseStdPoolAllocator;
    ObjectTypeInitializer.ObjectsBodySize = sizeof( OC_SECURITY_DESCRIPTOR_OBJECT );
#if DBG
    ObjectTypeInitializer.Methods.DeleteObject = OcCrDeleteSecurityDescriptorObject;
#else//DBG
    ObjectTypeInitializer.Methods.DeleteObject = NULL;
#endif//DBG
    ObjectTypeInitializer.Methods.DeleteObjectType = NULL;

    OcObInitializeObjectType( &ObjectTypeInitializer,
                              &g_OcSecurityDescriptorObjectType );

    //
    // initialize the conversion tables
    //
    OcCrFillArray( DeviceSetupClassToDeviceType,
                   sizeof( DeviceSetupClassToDeviceType[ 0x0 ] ),
                   OC_STATIC_ARRAY_SIZE( DeviceSetupClassToDeviceType ),
                   DEVICE_TYPE_UNKNOWN );

    OcCrFillArray( DeviceEnumeratorToDeviceType,
                   sizeof( DeviceEnumeratorToDeviceType[ 0x0 ] ),
                   OC_STATIC_ARRAY_SIZE( DeviceEnumeratorToDeviceType ),
                   DEVICE_TYPE_UNKNOWN );

    OcCrFillArray( UpperAndLowerTypesToMajorType,
                   sizeof( UpperAndLowerTypesToMajorType[ 0x0 ][ 0x0 ] ),
                   TYPES_COUNT*TYPES_COUNT,
                   DEVICE_TYPE_UNKNOWN );

    OcCrFillArray( DeviceTypeAndSetupClassToType,
                   sizeof( DeviceTypeAndSetupClassToType[ 0x0 ][ 0x0 ] ),
                   TYPES_COUNT*en_GUID_DEVCLASS_LAST,
                   MINOR_TYPE_UNKNOWN );

    OcCrFillArray( UpperAndLowerTypesToMinorType,
                   sizeof( UpperAndLowerTypesToMinorType[ 0x0 ][ 0x0 ] ),
                   TYPES_COUNT*TYPES_COUNT,
                   MINOR_TYPE_UNKNOWN );

    //
    // initialize translation from the setup class to a device type
    //
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_1394 ]      = DEVICE_TYPE_1394;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_1394DEBUG ] = DEVICE_TYPE_1394;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_BLUETOOTH ] = DEVICE_TYPE_BLUETOOTH;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_CDROM ]     = DEVICE_TYPE_CD_ROM;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_DISKDRIVE ] = DEVICE_TYPE_HARD_DRIVE;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_FDC ]       = DEVICE_TYPE_FLOPPY;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_FLOPPYDISK ]= DEVICE_TYPE_FLOPPY;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_INFRARED ]  = DEVICE_TYPE_IRDA;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_TAPEDRIVE ] = DEVICE_TYPE_TAPE;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_USB ]       = DEVICE_TYPE_USBHUB;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_VOLUME ]    = DEVICE_TYPE_HARD_DRIVE;
    DeviceSetupClassToDeviceType[ en_GUID_DEVCLASS_WCEUSBS ]   = DEVICE_TYPE_WINDOWS_MOBILE;

    //
    // initialize translation from an enumerator( i.e. the Fdo type ) 
    // to a device type( i.e. the Pdo type )
    //
    DeviceEnumeratorToDeviceType[ en_BTH ]            = DEVICE_TYPE_BLUETOOTH;
    DeviceEnumeratorToDeviceType[ en_BTHENUM ]        = DEVICE_TYPE_BLUETOOTH;
    DeviceEnumeratorToDeviceType[ en_FDC ]            = DEVICE_TYPE_FLOPPY;
    DeviceEnumeratorToDeviceType[ en_IDE ]            = DEVICE_TYPE_HARD_DRIVE;
    DeviceEnumeratorToDeviceType[ en_LPTENUM ]        = DEVICE_TYPE_LPT;
    DeviceEnumeratorToDeviceType[ en_SBP2 ]           = DEVICE_TYPE_REMOVABLE;
    DeviceEnumeratorToDeviceType[ en_STORAGE ]        = DEVICE_TYPE_HARD_DRIVE;
    DeviceEnumeratorToDeviceType[ en_USB ]            = DEVICE_TYPE_USBHUB;
    DeviceEnumeratorToDeviceType[ en_USBSTOR ]        = DEVICE_TYPE_REMOVABLE;
    DeviceEnumeratorToDeviceType[ en_1394 ]           = DEVICE_TYPE_1394;
    DeviceEnumeratorToDeviceType[ en_WpdBusEnumRoot ] = DEVICE_TYPE_WINDOWS_MOBILE;

    //
    // define types redefined for the USB devices
    //
    OcSetMajorTypesRedefinition( DEVICE_TYPE_HARD_DRIVE, DEVICE_TYPE_USBHUB, DEVICE_TYPE_REMOVABLE );

    //
    // define minor and major types for USB basing on setup class
    //
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_OC_GUID_DEVCLASS_UNKNOWN, MINOR_TYPE_USB_DUMMY );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_USB, DEVICE_TYPE_USBHUB );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_CDROM, MINOR_TYPE_USBSTOR );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_DISKDRIVE, MINOR_TYPE_USBSTOR );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_DOT4, MINOR_TYPE_USBPRINT );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_DOT4PRINT, MINOR_TYPE_USBPRINT );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_FDC, MINOR_TYPE_USBSTOR );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_FLOPPYDISK, MINOR_TYPE_USBSTOR );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_HDC, MINOR_TYPE_USBSTOR );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_HIDCLASS, MINOR_TYPE_HIDUSB );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_KEYBOARD, MINOR_TYPE_HIDUSB );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_MOUSE, MINOR_TYPE_HIDUSB );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_PNPPRINTERS, MINOR_TYPE_USBPRINT );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_PRINTER, MINOR_TYPE_USBPRINT );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_TAPEDRIVE, MINOR_TYPE_USBSTOR );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_USBHUB, en_GUID_DEVCLASS_VOLUME, MINOR_TYPE_USBSTOR );

    //
    // define minor and major types for FireWire basing on setup class
    //
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_OC_GUID_DEVCLASS_UNKNOWN, MINOR_TYPE_IEEE_1394_DUMMY );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_USB, DEVICE_TYPE_1394 );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_CDROM, MINOR_TYPE_FW_DISK );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_DISKDRIVE, MINOR_TYPE_FW_DISK );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_FDC, MINOR_TYPE_FW_DISK );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_FLOPPYDISK, MINOR_TYPE_FW_DISK );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_HDC, MINOR_TYPE_FW_DISK );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_MOUSE, MINOR_TYPE_HIDUSB );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_TAPEDRIVE, MINOR_TYPE_FW_DISK );
    OcSetLowerTypeAndUpperSetupClassTranslation( DEVICE_TYPE_1394, en_GUID_DEVCLASS_VOLUME, MINOR_TYPE_FW_DISK );

    //
    // USB bus conversion table for conversion from the upper & lower type to an inferred minor type
    //
    OcSetTypesTranslation( DEVICE_TYPE_UNKNOWN,    DEVICE_TYPE_USBHUB, MINOR_TYPE_USB_DUMMY );
    OcSetTypesTranslation( DEVICE_TYPE_USBHUB,     DEVICE_TYPE_USBHUB, DEVICE_TYPE_USBHUB );
    OcSetTypesTranslation( DEVICE_TYPE_FLOPPY,     DEVICE_TYPE_USBHUB, MINOR_TYPE_USBSTOR );
    OcSetTypesTranslation( DEVICE_TYPE_REMOVABLE,  DEVICE_TYPE_USBHUB, MINOR_TYPE_USBSTOR );
    OcSetTypesTranslation( DEVICE_TYPE_HARD_DRIVE, DEVICE_TYPE_USBHUB, MINOR_TYPE_USBSTOR );
    OcSetTypesTranslation( DEVICE_TYPE_DVD,        DEVICE_TYPE_USBHUB, MINOR_TYPE_USBSTOR );
    OcSetTypesTranslation( DEVICE_TYPE_CD_ROM,     DEVICE_TYPE_USBHUB, MINOR_TYPE_USBSTOR );

    //
    // FireWire Bus ( IEEE1394 ) conversion table from the upper & lower type to an inferred minor type
    //
    OcSetTypesTranslation( DEVICE_TYPE_UNKNOWN,    DEVICE_TYPE_1394, MINOR_TYPE_IEEE_1394_DUMMY );
    OcSetTypesTranslation( DEVICE_TYPE_1394,       DEVICE_TYPE_1394, DEVICE_TYPE_1394 );
    OcSetTypesTranslation( DEVICE_TYPE_FLOPPY,     DEVICE_TYPE_1394, MINOR_TYPE_FW_DISK );
    OcSetTypesTranslation( DEVICE_TYPE_REMOVABLE,  DEVICE_TYPE_1394, MINOR_TYPE_FW_DISK );
    OcSetTypesTranslation( DEVICE_TYPE_HARD_DRIVE, DEVICE_TYPE_1394, MINOR_TYPE_FW_DISK );
    OcSetTypesTranslation( DEVICE_TYPE_DVD,        DEVICE_TYPE_1394, MINOR_TYPE_FW_DISK );
    OcSetTypesTranslation( DEVICE_TYPE_CD_ROM,     DEVICE_TYPE_1394, MINOR_TYPE_FW_DISK );

__exit:
    //
    // in cas of error deinitialize all objects here, do 
    // not postpone uninitialization of a partially
    // initialized suybsystem in OcSdUninitializeSecurityDescriptorsSubsystem
    //

    if( NT_SUCCESS( RC ) )
        g_SecuritySubsystemInitialized = TRUE;

    return RC;
}

//-------------------------------------------------

__forceinline
VOID
OcGetNodeBareDeviceType(
    __in OC_EN_ENUMERATOR        BusEnumerator,
    __in OC_EN_SETUP_CLASS_GUID  SetupClassGuidIndex,
    __out POC_FULL_DEVICE_TYPE   FullDeviceType
    )
{
    OC_DEVICE_TYPE    FdoRedefinedType;

    //
    // Step 1
    //

    //
    // retrieve device's 'bare' type, i.e. FDO relative type
    //
    FullDeviceType->FdoMajorType = OcGetDeviceTypeFromSetupClass( SetupClassGuidIndex );

    //
    // Step 2
    //
    FullDeviceType->PdoMajorType = OcGetDeviceTypeFromEnumeratorBus( BusEnumerator );

    //
    // Step 3
    //
    FdoRedefinedType = OcGetFdoMofifiedType( FullDeviceType->FdoMajorType,
                                             FullDeviceType->PdoMajorType );
    if( DEVICE_TYPE_UNKNOWN != FdoRedefinedType && 
        FullDeviceType->FdoMajorType != FdoRedefinedType )
        FullDeviceType->FdoMajorType = FdoRedefinedType;

    //
    // Step 4
    //
    FullDeviceType->PdoMinorType = OcGetMinorDeviceTypeFromPdoTypeAndSetupClass( 
                                       FullDeviceType->PdoMajorType, SetupClassGuidIndex );
}

//-------------------------------------------------

VOID
OcFixUpBareDeviceTypes(
    __in POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    __inout POC_FULL_DEVICE_TYPE   FullDeviceType
    )
    /*
    this function fixes the types by using any additional
    information from the device object, any found heuristics
    and tricks should be inserted in this function
    */
{

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // In many cases if there is a secondary bus driver between the
    // USB or other bus and the device( so the device is attached to
    // this bus but not USB ) then the USB FDO has the Setup Class
    // of the underlying BUS, i.e. USB. This is the case for USB storages
    // with the USBSTOR bus. This wrong minor type must be fixed to 
    // the proper type defining the nature of the USB Pdo.
    //

    //
    // try to define the minor type by using information
    // available from the device object
    //
    if( PtrOcNodeDeviceObject->DevicePropertyObject && 
        0x1 == PtrOcNodeDeviceObject->DevicePropertyObject->Header.PropertyType.USB ){

            POC_DEVICE_PROPERTY_USB_OBJECT    UsbDevProperty = 
            (POC_DEVICE_PROPERTY_USB_OBJECT)PtrOcNodeDeviceObject->DevicePropertyObject;

            switch( UsbDevProperty->UsbDescriptor.UsbClassId ){

                case 0x3:
                    //
                    // this is a HID device
                    //
                    FullDeviceType->PdoMinorType = MINOR_TYPE_HIDUSB;
                    break;

                case 0x8:
                    //
                    // this is a USB storage device
                    //
                    FullDeviceType->PdoMinorType = MINOR_TYPE_USBSTOR;
                    break;

                case 0x7:
                    //
                    // this is a printing support device
                    //
                    FullDeviceType->PdoMinorType = MINOR_TYPE_USBPRINT;
                    break;

            }// switch
    }
}

//-------------------------------------------------

BOOLEAN
OcCrDetermineDeviceTypeInternal(
    __in POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    __in PVOID    Context // actually OC_DEVICE_TYPE_PROCESSING*
    )
{
    /*
    How to infer the device type and check permissions
    1. Get the device type basing on the device setup class, 
       this provides a type relative to the FDO, and the check 
       against this type should be made only on the Fdo or an upper level
    2. Get the device type basing on the enumerator, 
       this provides a device type relative to the bus,
       the access to an FDO should be checked either against 
       minor type( see the step 4 ) or the requested access 
       should be converted to a requested access having a 
       meaning on a PDO level
    3. Check the redefinition for the type get on the step 1 using
       the type get on the step 2
    4. Get the minor device type basing on the setup class 
       and the device class found at a step 2( i.e. relative to the bus )
       and save it as a minor type for the step 2 type
    5. Repeat the 3rd step with all found underlying device types
    */

    USHORT                i;
    USHORT                NumberOfValidEntries;
    OC_FULL_DEVICE_TYPE   FullDeviceType;
    POC_DEVICE_TYPE_CONTEXT         PtrDeviceTypeContext;
    POC_FULL_DEVICE_TYPE_STACK      PtrDeviceTypeStack;
    OC_EN_ENUMERATOR                BusEnumerator;
    OC_EN_SETUP_CLASS_GUID          SetupClassGuidIndex;

    PtrDeviceTypeContext = (POC_DEVICE_TYPE_CONTEXT)Context;
    PtrDeviceTypeStack = PtrDeviceTypeContext->DeviceTypeStack;

    ASSERT( FALSE == PtrDeviceTypeContext->BufferExhausted );
    ASSERT( PtrDeviceTypeStack->NumberOfEntries > 0x0 );

    //
    // stop processing if we ran out of entries
    //
    if( PtrDeviceTypeStack->NumberOfValidEntries == PtrDeviceTypeStack->NumberOfEntries ){

        //
        // it is obvious that we ran out of free entries
        //
        PtrDeviceTypeContext->BufferExhausted = TRUE;
        return FALSE;
    }

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( (PtrDeviceTypeContext->OcDeviceObject->DevicePropertyObject)?
        PtrDeviceTypeContext->UpperSetupClassGuidIndex == PtrDeviceTypeContext->OcDeviceObject->DevicePropertyObject->Header.SetupClassGuidIndex:
        en_OC_GUID_DEVCLASS_UNKNOWN == PtrDeviceTypeContext->UpperSetupClassGuidIndex );


    //
    // retrieve a device's enumerator and a setup class
    //

    BusEnumerator = PtrOcNodeDeviceObject->Enumerator;

    if( NULL != PtrOcNodeDeviceObject->DevicePropertyObject )
        SetupClassGuidIndex = PtrOcNodeDeviceObject->DevicePropertyObject->Header.SetupClassGuidIndex;
    else
        SetupClassGuidIndex = en_OC_GUID_DEVCLASS_UNKNOWN;

    if( 0x0 == PtrDeviceTypeStack->NumberOfValidEntries ){

        //
        // initialize the first entry by executing
        // the 1st, 2nd, 3rd and 4th steps
        //
        OcGetNodeBareDeviceType( BusEnumerator,
                                 SetupClassGuidIndex,
                                 &PtrDeviceTypeStack->FullDeviceType[ 0x0 ] );

        OcFixUpBareDeviceTypes( PtrOcNodeDeviceObject,
                                &PtrDeviceTypeStack->FullDeviceType[ 0x0 ] );

        PtrDeviceTypeStack->NumberOfValidEntries = 0x1;
        return TRUE;
    }

    NumberOfValidEntries = PtrDeviceTypeStack->NumberOfValidEntries;

    //
    // Step 1, Step2, Step3, Step 4
    //
    OcGetNodeBareDeviceType( BusEnumerator,
                             SetupClassGuidIndex,
                             &FullDeviceType );

    OcFixUpBareDeviceTypes( PtrOcNodeDeviceObject,
                            &FullDeviceType );

    //
    // step 5
    //
    ASSERT( NumberOfValidEntries >= 0x1 && NumberOfValidEntries == PtrDeviceTypeStack->NumberOfValidEntries );
    for( i = 0x0; i < NumberOfValidEntries; ++i ){

        OC_DEVICE_TYPE    UpperFdoRedefinedType;

        UpperFdoRedefinedType = OcGetFdoMofifiedType( PtrDeviceTypeStack->FullDeviceType[ i ].FdoMajorType, FullDeviceType.PdoMajorType );
        if( DEVICE_TYPE_UNKNOWN != UpperFdoRedefinedType && 
            UpperFdoRedefinedType != PtrDeviceTypeStack->FullDeviceType[ i ].FdoMajorType )
            PtrDeviceTypeStack->FullDeviceType[ i ].FdoMajorType = UpperFdoRedefinedType;
    }

    //
    // fill the next empty entry with the new data if the new data contains something new
    //
    ASSERT( NumberOfValidEntries >= 0x1 && NumberOfValidEntries == PtrDeviceTypeStack->NumberOfValidEntries );
    if( FullDeviceType.FdoMajorType != PtrDeviceTypeStack->FullDeviceType[ NumberOfValidEntries-0x1 ].FdoMajorType ||
        FullDeviceType.PdoMajorType != PtrDeviceTypeStack->FullDeviceType[ NumberOfValidEntries-0x1 ].PdoMajorType ||
        FullDeviceType.PdoMinorType != PtrDeviceTypeStack->FullDeviceType[ NumberOfValidEntries-0x1 ].PdoMinorType ){

            ASSERT( NumberOfValidEntries == PtrDeviceTypeStack->NumberOfValidEntries );

            NumberOfValidEntries += 1;
            PtrDeviceTypeStack->NumberOfValidEntries = NumberOfValidEntries;

            ASSERT( NumberOfValidEntries <= PtrDeviceTypeStack->NumberOfEntries );

            PtrDeviceTypeStack->FullDeviceType[ NumberOfValidEntries-0x1 ] = FullDeviceType;
    }

    //
    // do not make any conclusion about whether we ran out of free entires
    //
    return TRUE;
}

//-------------------------------------------------

BOOLEAN
OcCrGetFullDeviceType(
    __in POC_DEVICE_OBJECT                PtrOcTopDeviceObject,
    __inout POC_FULL_DEVICE_TYPE_STACK    PtrFullDeviceTypeStack
    )
    /*
    If FALSE is returned then the caller has provided the buffer 
    of insufficient size, else TRUE is returned. The bad side is that
    it is hard to suggest the needed size for the buffer so the caller
    have to guess it. In any case the function fills as much entries as possible.
    The caller must set PtrFullDeviceTypeStack->NumberOfEntries to a right
    value.
    */
{
    OC_DEVICE_TYPE_CONTEXT    DeviceTypeProcCtx = { 0x0 };

    ASSERT( PtrFullDeviceTypeStack->NumberOfEntries > 0x0 && 
            0x0 == PtrFullDeviceTypeStack->NumberOfValidEntries );

    //
    // just for safety, the caller has zeroed this field
    // ( at least he should have done )
    //
    PtrFullDeviceTypeStack->NumberOfValidEntries = 0x0;

    //
    // check for a suspicious input
    //
    if( PtrFullDeviceTypeStack->NumberOfEntries == 0x0 || 
        PtrFullDeviceTypeStack->NumberOfEntries >= 0x20 ){

            ASSERT( !"A wrong data has ben provided for OcCrGetFullDeviceType" );

            PtrFullDeviceTypeStack->NumberOfValidEntries = 0x0;

            //
            // stop the caller from keeping calling this function
            //
            return TRUE;
    }

    //
    // initialize the traverse structure
    //
    DeviceTypeProcCtx.DeviceTypeStack = PtrFullDeviceTypeStack;
    DeviceTypeProcCtx.OcDeviceObject  = PtrOcTopDeviceObject;
    DeviceTypeProcCtx.BufferExhausted = FALSE;

    if( NULL != PtrOcTopDeviceObject->DevicePropertyObject )
        DeviceTypeProcCtx.UpperSetupClassGuidIndex = PtrOcTopDeviceObject->DevicePropertyObject->Header.SetupClassGuidIndex;
    else
        DeviceTypeProcCtx.UpperSetupClassGuidIndex = en_OC_GUID_DEVCLASS_UNKNOWN;

    OcCrTraverseTopDown( PtrOcTopDeviceObject,
                         OcCrDetermineDeviceTypeInternal,
                         &DeviceTypeProcCtx
                         );

    return !(DeviceTypeProcCtx.BufferExhausted);
}

//------------------------------------------------------

VOID
OcSdUninitializeSecurityDescriptorsSubsystem()
{

    ULONG    i;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcObIsObjectManagerInitialized() );

    if( FALSE == g_SecuritySubsystemInitialized )
        return;

    //
    // there is no need to acquire any lock, before calling this 
    // function the caller must guarantee that no any other 
    // subsystem will use the security descriptors subsystem 
    // and objects pertaining to it
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( g_SdTable ); ++i ){

        ULONG    m;

        for( m = 0x0; m < OC_STATIC_ARRAY_SIZE( g_SdTable[ i ].SdObjects ); ++m ){

            if( NULL != g_SdTable[ i ].SdObjects[ m ] ){

                OcObDereferenceObject( g_SdTable[ i ].SdObjects[ m ] );
                g_SdTable[ i ].SdObjects[ m ] = NULL;
            }

        }//for( m = 0x0; m < OC_STATIC_ARRAY_SIZE( g_SdTable[ i ].RwLockForSd ); ++m 

    }// for

    //
    // all object should be closed
    //
    ASSERT( 0x0 == g_OcSecurityDescriptorObjectType.RefCount );

    if( NULL != g_SecurityCheckThreadPool )
        OcObDereferenceObject( g_SecurityCheckThreadPool );

    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( g_SecurityCheckThreadPool );

    OcObDeleteObjectType( &g_OcSecurityDescriptorObjectType );
}

//------------------------------------------------------

#if DBG
VOID
NTAPI
OcCrDeleteSecurityDescriptorObject(
    __in POC_SECURITY_DESCRIPTOR_OBJECT    OperationObject
    )
{
    RtlFillMemory( (PCHAR)(OperationObject + 0x1),
                   OperationObject->SdSize,
                   0xCC );
}
#endif//DBG

//------------------------------------------------------

NTSTATUS
OcSdSetSecurityDescriptor(
    __in OC_SD_TYPE           SdType,
    __in OC_DEVICE_TYPE       DeviceType,
    __in PSECURITY_DESCRIPTOR    SecurityDescriptor,
    __in ULONG                SdBufferSize,
    __in KPROCESSOR_MODE      AccessMode
    )
    /*
    Sets a new security descriptor of type SdType for device type DeviceType.
    AccessMode variable defines the probe mode for SecurityDescriptor.
    SdLength defines the size of the buffer to which SecurityDescriptor points.
    */
{

    NTSTATUS    RC = STATUS_SUCCESS;
    POC_SECURITY_DESCRIPTOR_OBJECT    NewSdObject;
    PSECURITY_DESCRIPTOR    PtrObjectSd;
    ULONG       SdExactSize;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( ( KernelMode == AccessMode )? 
            ( (PVOID)SecurityDescriptor > (PVOID)MM_USER_PROBE_ADDRESS ) : 
            ( ( (PCHAR)SecurityDescriptor+SdBufferSize < (PCHAR)MM_USER_PROBE_ADDRESS ) && ( (PCHAR)SecurityDescriptor+SdBufferSize > (PCHAR)SecurityDescriptor ) ) );

    if( SdBufferSize > 4*( PAGE_SIZE*PAGE_SIZE ) )
        return STATUS_INVALID_PARAMETER;

    if( OcSdGetSdTableIndexByDeviceType( DeviceType ) > OC_STATIC_ARRAY_SIZE( g_SdTable ) )
        return STATUS_INVALID_PARAMETER;

    //
    // probe input parameters if access mode is UserMode
    //
    if( UserMode == AccessMode ){

        __try{

            ProbeForRead( SecurityDescriptor,
                          SdBufferSize,
                          0x1 );

            if( !RtlValidSecurityDescriptor( SecurityDescriptor ) )
                ExRaiseStatus( STATUS_INVALID_PARAMETER );

            SdExactSize = RtlLengthSecurityDescriptor( SecurityDescriptor );

            if( SdExactSize > SdBufferSize )
                ExRaiseStatus( STATUS_INVALID_PARAMETER );

        } __except( EXCEPTION_EXECUTE_HANDLER ){

            RC = GetExceptionCode();
        }

    } else {

        ASSERT( KernelMode == AccessMode );

        SdExactSize = RtlLengthSecurityDescriptor( SecurityDescriptor );
    }

    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // create an object with the additional buffer for SD
    //
    RC = OcObCreateObjectEx( &g_OcSecurityDescriptorObjectType,
                             SdExactSize,
                             &NewSdObject );

    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // initialize the object's body
    //
    NewSdObject->DeviceType = DeviceType;
    NewSdObject->SdType = SdType;
    NewSdObject->SdSize = 0x0;
    NewSdObject->SecurityDescriptor = NULL;

    PtrObjectSd = (PSECURITY_DESCRIPTOR)(NewSdObject + 0x1);

    //
    // copy the SD to the object
    //
    if( UserMode == AccessMode ){

        __try{

            RtlCopyMemory( PtrObjectSd, SecurityDescriptor, SdExactSize );

        } __except( EXCEPTION_EXECUTE_HANDLER ){

            RC = GetExceptionCode();
        }

    } else {

        ASSERT( KernelMode == AccessMode );
        RtlCopyMemory( PtrObjectSd, SecurityDescriptor, SdExactSize );
    }

    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // check the SD
    //

    if( !RtlValidSecurityDescriptor( PtrObjectSd ) ){

        RC = STATUS_INVALID_PARAMETER;
        goto __exit;
    }

    if( RtlLengthSecurityDescriptor( PtrObjectSd ) != SdExactSize ){

        RC = STATUS_INVALID_PARAMETER;
        goto __exit;
    }

    //
    // initialize the SD object fields, related with SD, after checking the copied SD
    //
    NewSdObject->SdSize = SdExactSize;
    NewSdObject->SecurityDescriptor = PtrObjectSd;

__exit:

    if( NT_SUCCESS( RC ) ){

        //
        // exchange the objects
        //
        KIRQL    OldIrql;
        ULONG    SdTableIndex = OcSdGetSdTableIndexByDeviceType( DeviceType );
        POC_SECURITY_DESCRIPTOR_OBJECT    OldSdObject;

        OcRwAcquireLockForWrite( &g_SdTable[ SdTableIndex ].RwLockForSd[ SdType ], &OldIrql );
        {// start of the write lock

            //
            // exchange the objects, there is no need to reference the NewSdObject object
            // because it has been referenced by OcObCreateObjectEx
            //
            OldSdObject = g_SdTable[ SdTableIndex ].SdObjects[ SdType ];
            g_SdTable[ SdTableIndex ].SdObjects[ SdType ] = NewSdObject;

        }// end of the write lock
        OcRwReleaseWriteLock( &g_SdTable[ SdTableIndex ].RwLockForSd[ SdType ], OldIrql );

        //
        // derefrence the old Sd object
        //
        if( NULL != OldSdObject )
            OcObDereferenceObject( OldSdObject );

    } else {

        //
        // something went wrong!
        // delete the object
        //
        OcObDereferenceObject( NewSdObject );
    }

    return RC;
}

//------------------------------------------------------

NTAPI
OcIsAccessGranted(
    __inout POC_CHECK_SECURITY_REQUEST    Request
    )
    /*
    the strange calling semantics of this function
    is dictated by the convenience to use it as a
    worker thread routine
    */
{
    BOOLEAN              RetValue;
    KIRQL                OldIrql;
    ULONG                SdTableIndex;
    POC_SECURITY_DESCRIPTOR_OBJECT    SecDescObject;
    OC_SD_TYPE           SdType = Request->SdType;
    OC_DEVICE_TYPE       DeviceType = Request->DeviceType;
    PSECURITY_SUBJECT_CONTEXT    SubjectSecurityContext = Request->SubjectSecurityContext;
    BOOLEAN              SubjectContextLocked = Request->SubjectContextLocked;
    OC_ACCESS_RIGHTS     DesiredAccess = Request->DesiredAccess;
    KPROCESSOR_MODE      AccessMode = Request->AccessMode;
    PACCESS_MASK         GrantedAccess = Request->GrantedAccess;
    PNTSTATUS            AccessStatus = Request->AccessStatus;
    PBOOLEAN             IsAccessGranted = Request->IsAccessGranted;

    SdTableIndex = OcSdGetSdTableIndexByDeviceType( DeviceType );

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( SdTableIndex < OC_STATIC_ARRAY_SIZE( g_SdTable ) );
    ASSERT( SdType < OcMaximumSdType );

    OcRwAcquireLockForRead( &g_SdTable[ SdTableIndex ].RwLockForSd[ SdType ], &OldIrql );
    {// start of the read lock

        SecDescObject = g_SdTable[ SdTableIndex ].SdObjects[ SdType ];

        if( NULL == SecDescObject->SecurityDescriptor )
            SecDescObject = NULL;

        if( NULL != SecDescObject )
            OcObReferenceObject( SecDescObject );

    }// end of the read lock
    OcRwReleaseReadLock( &g_SdTable[ SdTableIndex ].RwLockForSd[ SdType ], OldIrql );

    if( NULL == SecDescObject ){

        *IsAccessGranted = g_SdTable[ SdTableIndex ].ActionForNullSd[ SdType ];
        return STATUS_SUCCESS;
    }

    ASSERT( NULL != SecDescObject->SecurityDescriptor );
    ASSERT( DeviceType == SecDescObject->DeviceType );

    //
    // Actually, this is not needed because desired access never contains GENERAL_XXX right bits
    //
    RtlMapGenericMask( &DesiredAccess, &g_SdTable[ SdTableIndex ].GenericMapping );

    RetValue = SeAccessCheck( SecDescObject->SecurityDescriptor,
                              SubjectSecurityContext,
                              SubjectContextLocked,
                              DesiredAccess,
                              0x0,
                              NULL,
                              &g_SdTable[ SdTableIndex ].GenericMapping,
                              AccessMode,
                              GrantedAccess,
                              AccessStatus );

    OcObDereferenceObject( SecDescObject );

    *IsAccessGranted = RetValue;
    return STATUS_SUCCESS;
}

//------------------------------------------------------

NTSTATUS
NTAPI
OcIsAccessGrantedWR(
    ULONG_PTR    Request
    )
{
    return OcIsAccessGranted( (POC_CHECK_SECURITY_REQUEST)Request );
}

//------------------------------------------------------

BOOLEAN
OcIsAccessGrantedSafe(
    __inout POC_CHECK_SECURITY_REQUEST    Request
    )
{
    NTSTATUS    RC = STATUS_UNSUCCESSFUL;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    if( KeGetCurrentIrql() == APC_LEVEL && NULL != g_SecurityCheckThreadPool ){

        //
        // send a synchronous request to the thread pool
        //
        POC_WORK_ITEM_LIST_OBJECT   WorkItemListObject;

        WorkItemListObject = OcTplReferenceSharedWorkItemList( g_SecurityCheckThreadPool );
        ASSERT( NULL != WorkItemListObject );

        RC = OcWthPostWorkItemParam1( WorkItemListObject,
                                      TRUE,
                                      OcIsAccessGrantedWR,
                                      (ULONG_PTR)Request );

        OcObDereferenceObject( WorkItemListObject );
    }

    if( NT_SUCCESS( RC ) )
        return (*Request->IsAccessGranted);

    //
    // if the execution has reached this point then either the 
    // caller is running at PASSIVE_LEVEL or processing in the 
    // worker thread failed
    //
    RC = OcIsAccessGranted( Request );

    ASSERT( NT_SUCCESS( RC ) );

    return (*Request->IsAccessGranted);
}

//------------------------------------------------------


