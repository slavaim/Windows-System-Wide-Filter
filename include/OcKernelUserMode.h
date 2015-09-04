/*
Revision history:
23.11.2006 
 Start
*/
#ifndef _OC_KERNELUSERMODE_H_
#define _OC_KERNELUSERMODE_H_

#include "DlDriver2OCore.h"

// {0B4989E9-6198-47c0-A4BE-4B7BAFD478C9} - generated using guidgen.exe
DEFINE_GUID(GUID_SD_CORE_CONTROL_OBJECT, 
0xb4989e9, 0x6198, 0x47c0, 0xa4, 0xbe, 0x4b, 0x7b, 0xaf, 0xd4, 0x78, 0xc9);

#ifdef __cplusplus
extern "C" {
#endif//__cplusplus

///////////////////////////////////////////////////////////
// Make up some private access rights.Only 24 bits.
///////////////////////////////////////////////////////////

#define DEVICE_NO_ANY_ACCESS            0x0
#define DEVICE_DIRECT_READ              0x0004//
#define DEVICE_DIRECT_WRITE             0x0008//
#define DEVICE_EJECT_MEDIA              0x0010//
#define DEVICE_DISK_FORMAT              0x0080//
#define DEVICE_VOLUME_DEFRAGMENT        0x0100//
#define DEVICE_DIR_LIST                 0x0200
#define DEVICE_DIR_CREATE               0x0400
#define DEVICE_READ                     0x0800
#define DEVICE_WRITE                    0x1000
#define DEVICE_EXECUTE                  0x2000
#define DEVICE_RENAME                   0x4000
#define DEVICE_DELETE                   0x8000
#define DEVICE_ENCRYPTED_READ         0x010000L
#define DEVICE_ENCRYPTED_WRITE        0x020000L
#define DEVICE_ENCRYPTED_DIRECT_WRITE 0x040000L
#define DEVICE_PLAY_AUDIO_CD          0x200000L
#define DIRECT_DEVICE_OPEN            0x400000L
#define DEVICE_RIGHTS_OPEN_END       0x1000000L


///////////////////////////////////////////////////////////
// Device Type. Maximum number 0xFFFF.
// Type numbers must be consecutive and smaller than MINOR_TYPE_BASE
///////////////////////////////////////////////////////////

#define DEVICE_TYPE_UNKNOWN        0
#define DEVICE_TYPE_FLOPPY         1
#define DEVICE_TYPE_REMOVABLE      2
#define DEVICE_TYPE_HARD_DRIVE     3
#define DEVICE_TYPE_REMOTE         4
#define DEVICE_TYPE_NETWORK_DISK   4
#define DEVICE_TYPE_DVD            5
#define DEVICE_TYPE_CD_ROM         5
#define DEVICE_TYPE_RAM_VOL        6  
#define DEVICE_TYPE_SERIAL_PORT    7
#define DEVICE_TYPE_MOUSE_PORT     7
#define DEVICE_TYPE_LPT            8
#define DEVICE_TYPE_TAPE           9
#define DEVICE_TYPE_USBHUB         10// a wrong name, should be DEVICE_TYPE_USB
#define DEVICE_TYPE_IRDA           11
#define DEVICE_TYPE_1394           12
#define DEVICE_TYPE_BLUETOOTH      13
#define DEVICE_TYPE_WIFI           14
#define DEVICE_TYPE_WINDOWS_MOBILE            15
#define TYPES_COUNT                16 //total number of device types, including Unknown type!

//
// the minor types, mus be consecutive!
//
#define MINOR_TYPE_BASE                  0x100
#define MINOR_TYPE_UNKNOWN               ( MINOR_TYPE_BASE + 0x0 )
#define MINOR_TYPE_FILE_SYSTEM_DRIVER    ( MINOR_TYPE_BASE + 0x01 ) // 0x101
#define MINOR_TYPE_DISK_DRIVER           ( MINOR_TYPE_BASE + 0x02 ) // 0x102
#define MINOR_TYPE_USB_HUB               ( MINOR_TYPE_BASE + 0x03 ) // 0x103
#define MINOR_TYPE_IEEE_1394             ( MINOR_TYPE_BASE + 0x04 ) // 0x104
#define MINOR_TYPE_USB_DUMMY             ( MINOR_TYPE_BASE + 0x05 ) // 0x105
#define MINOR_TYPE_IEEE_1394_DUMMY       ( MINOR_TYPE_BASE + 0x06 ) // 0x106
#define MINOR_TYPE_CLASSDISK_DRIVER      ( MINOR_TYPE_BASE + 0x07 ) // 0x107
#define MINOR_TYPE_USBPRINT              ( MINOR_TYPE_BASE + 0x08 ) // 0x108
#define MINOR_TYPE_BTHUSB                ( MINOR_TYPE_BASE + 0x09 ) // 0x109
#define MINOR_TYPE_KBD_CLASS             ( MINOR_TYPE_BASE + 0x0A ) // 0x10A
#define MINOR_TYPE_SERIAL_PARALLEL       ( MINOR_TYPE_BASE + 0x0B ) // 0x10B
#define MINOR_TYPE_HIDUSB                ( MINOR_TYPE_BASE + 0x0C ) // 0x10C
#define MINOR_TYPE_IMAPI                 ( MINOR_TYPE_BASE + 0x0D ) // 0x10D
#define MINOR_TYPE_SBP2                  ( MINOR_TYPE_BASE + 0x0E ) // 0x10E
#define MINOR_TYPE_FW_DISK               MINOR_TYPE_SBP2
#define MINOR_TYPE_BTHENUM               ( MINOR_TYPE_BASE + 0x10 ) // 0x110
#define MINOR_TYPE_BTH_CONTROLLER_DYNAMIC  ( MINOR_TYPE_BASE + 0x11 ) // 0x111
#define MINOR_TYPE_BLUETOOTH_DUMMY       ( MINOR_TYPE_BASE + 0x12 ) // 0x112
#define MINOR_TYPE_WIFI_DEVICE           ( MINOR_TYPE_BASE + 0x13 ) // 0x113
#define MINOR_TYPE_BTH_DUMMY_COM         ( MINOR_TYPE_BASE + 0x14 ) // 0x114
#define MINOR_TYPE_BTH_COM               ( MINOR_TYPE_BASE + 0x15 ) // 0x115
#define MINOR_TYPE_NDISUIO               ( MINOR_TYPE_BASE + 0x16 ) // 0x116
#define MINOR_TYPE_USBSCANER             ( MINOR_TYPE_BASE + 0x17 ) // 0x117
#define MINOR_TYPE_USBNET                ( MINOR_TYPE_BASE + 0x18 ) // 0x118
#define MINOR_TYPE_USBSTOR               ( MINOR_TYPE_BASE + 0x19 ) // 0x119
#define MINOR_TYPE_IEEE1394_NET          ( MINOR_TYPE_BASE + 0x1A ) // 0x11A
#define MINOR_TYPE_USBSTOR_DRIVER        ( MINOR_TYPE_BASE + 0x1B ) // 0x11B
#define MINOR_TYPES_CDROM_DEVICE         ( MINOR_TYPE_BASE + 0x1C ) // 0x11C
#define MINOR_TYPES_COUNT                0x1D //total number of minor device types, including Unknown type!

///////////////////////////////////////////////////////////
// Some predefined rights as used in driver
///////////////////////////////////////////////////////////

#define BASIC_DISK_ALL_RIGHTS          (DEVICE_WRITE |\
                                        DEVICE_DELETE |\
                                        DEVICE_DIRECT_READ |\
                                        DEVICE_DIR_CREATE |\
                                        DEVICE_DIR_LIST |\
                                        DEVICE_EXECUTE |\
                                        DEVICE_RENAME |\
                                        DEVICE_DIRECT_WRITE |\
                                        DEVICE_DISK_FORMAT)

#define FLPY_DISK_ALL_RIGHTS           (DEVICE_DIRECT_WRITE |\
                                        DEVICE_DISK_FORMAT)

#define BASIC_DISK_EVERYONE (READ_CONTROL|\
                             FILE_READ_ATTRIBUTES|\
                             FILE_TRAVERSE|\
                             SYNCHRONIZE)

#define BASIC_DISK_ADMIN (FILE_WRITE_ATTRIBUTES|\
                          FILE_DELETE_CHILD|\
                          FILE_WRITE_EA|\
                          FILE_READ_EA|\
                          FILE_APPEND_DATA|\
                          FILE_WRITE_DATA|\
                          FILE_READ_DATA) 

#define BASIC_DISK_DELETE (DELETE)/*|\
                           WRITE_OWNER|\
                           WRITE_DAC)*/

#define CDROM_ALL_RIGHTS (DEVICE_DIRECT_READ)


#define WRITE_ACCESS              (FILE_WRITE_DATA          |\
                                   FILE_APPEND_DATA         |\
                                   FILE_WRITE_ATTRIBUTES    |\
                                   FILE_WRITE_EA)


#define READ_ACCESS               (FILE_READ_DATA           |\
                                   FILE_READ_ATTRIBUTES     |\
                                   FILE_READ_EA             |\
                                   SYNCHRONIZE)

//--------------------------------------------------------------------

#define IOCTL_DUMP_DEVICE_DATABASE     CTL_CODE( FILE_DEVICE_OCORE, OCORE_IOCTL_INDEX + 0x0, METHOD_BUFFERED, FILE_ANY_ACCESS )
#define IOCTL_QUERY_UNLOAD             CTL_CODE( FILE_DEVICE_OCORE, OCORE_IOCTL_INDEX + 0x1, METHOD_BUFFERED, FILE_ANY_ACCESS )


typedef struct _PID_VID{
    USHORT  Pid;
    USHORT  Vid;
} PID_VID,*PPID_VID;

typedef struct _DLD_SHADOW_REASON{
    ULONG    DataWriteShadow:0x1;
    ULONG    FileTypeCheck:0x1;
} DLD_SHADOW_REASON, *PDLD_SHADOW_REASON;


#ifdef __cplusplus
}
#endif//__cplusplus

#endif//_OC_KERNELUSERMODE_H_