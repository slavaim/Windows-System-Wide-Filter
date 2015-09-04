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

#ifndef _OC_FILEOBJECT_
#define _OC_FILEOBJECT_

//---------------------------------------------------

typedef enum _OC_REQUEST_TYPE{
    OC_REQUEST_FROM_MINIFILTER = 0x1,
    OC_REQUEST_FROM_HOOKED_FSD = 0x2,
    OC_REQUEST_FROM_DEVICE = 0x4
} OC_REQUEST_TYPE, *POC_REQUEST_TYPE;

//---------------------------------------------------

#define  OC_FILE_OBJECT_CREATE_INFO_SIGNATURE    (0xABC12908)
typedef struct _OC_FILE_OBJECT_CREATE_INFO{

    LIST_ENTRY       ListEntry;

    PFILE_OBJECT     SysFileObject;

    //
    // the name info buffer is allocated from the paged pool
    //
    UNICODE_STRING    FileName;

    //
    // the length in bytes of the volume name on which
    // the file is opened
    //
    USHORT            VolumeNameLength;

    HANDLE            ProcessID;

    //
    // the object is referenced
    //
    POC_DEVICE_OBJECT    ObjectOnWhichFileIsOpened;

    struct {

        //
        // the file object being opened is on a shadowed volume
        //
        ULONG    ShadowWriteRequests:0x1;
        ULONG    ShadowReadRequests:0x1;

    } Flags;

#if DBG
    POC_DEVICE_OBJECT    DevObjWhichListContainsThisObject;
    //
    // the signature must be OC_FILE_OBJECT_CREATE_INFO_SIGNATURE
    //
    ULONG                Signature;
#endif//DBG

} OC_FILE_OBJECT_CREATE_INFO, *POC_FILE_OBJECT_CREATE_INFO;

//---------------------------------------------------

typedef struct _OC_CONTEXT_OBJECT{

#if DBG
    //
    // in debug build all file object related with 
    // the context object are connected in the list
    //
    LIST_ENTRY    FileListHead;
    KSPIN_LOCK    FileListSpinLock;
#endif//DBG

    struct{
        //
        // the following flag is set after a
        // file object that supports a shared 
        // cache map is checked
        //
        ULONG    SharedCacheMapFoChecked:0x1;

        //
        // the data stream should be shadowed
        //
        ULONG    ShadowWriteRequests:0x1;
        ULONG    ShadowReadRequests:0x1;
    } Flags;

    //
    // defines the reason for the shadow flags
    //
    DLD_SHADOW_REASON    ShadowReason;

    //
    // the counter of opened file objects with this
    // context, the context is removed from the hash then
    // this counter drops to zero, this count must not 
    // be incremented to retain the context, it is used
    // strictly as the opend file objects counter!
    // If the file is being closed this counter is decremented
    // and the context object is thrown out from the hash
    // when this counters drops to zero, but the object 
    // might be still alive due to the object's header
    // reference count
    //
    ULONG    OpenedFileObjectCounter;

    //
    // the lock that is used for syncronization 
    // during the file object creation
    //
    OC_QUEUE_LOCK    ContexQueueLock;

    //
    // a refrenced creation info object
    //
    POC_FILE_OBJECT_CREATE_INFO    CreationInfoObject;


    //
    // the system context returned by OcCrGetSysFileObjectContext
    //
    ULONG_PTR    SystemContext;

} OC_CONTEXT_OBJECT, *POC_CONTEXT_OBJECT;

//---------------------------------------------------

typedef enum{
    OC_FILE_OBJECT_OPENED = 0x0,
    OC_FILE_OBJECT_CLOSED = 0x1,
    OC_FILE_OBJECT_MAX_STATE = (ULONG)(-1)
} OC_FILE_OBJECT_STATE;

typedef struct _OC_FILE_OBJECT{

#if DBG
    //
    // in debug build all file object related with 
    // a context object are connected in the list
    //
    LIST_ENTRY    FileListEntry;
#endif//DBG

    //
    // the system's file object
    //
    PFILE_OBJECT    SysFileObject;

    //
    // a pointer to a context object, the internal
    // OpenedFileObjectCounter is used for 
    // referencing, the context object header's refrence 
    // is incremented wnen the object is inserted in the hash
    // and when a file object references a context,
    // so the object manager's reference count is used
    // to retain context object in memory while the 
    // internal OpenedFileObjectCounter is used to retain
    // the object in hash
    //
    POC_CONTEXT_OBJECT    ContextObject;

    //
    // a refrenced creation info object
    //
    POC_FILE_OBJECT_CREATE_INFO    CreationInfoObject;

    OC_FILE_OBJECT_STATE     FileObjectState;

    struct {
        ULONG    DirectDeviceOpen:0x1;
    } Flags;

} OC_FILE_OBJECT, *POC_FILE_OBJECT;

//---------------------------------------------------

extern
VOID
NTAPI
OcCrDeleteContextObject(
    IN POC_CONTEXT_OBJECT    FileContextObject
    );

extern
VOID
NTAPI
OcCrDeleteContextObjectType(
    IN POC_OBJECT_TYPE    FileContextObjectType
    );

extern
VOID
NTAPI
OcCrDeleteFileObject(
    IN POC_FILE_OBJECT    FileObject
    );

extern
VOID
NTAPI
OcCrDeleteFileObjectInfo(
    IN POC_FILE_OBJECT_CREATE_INFO    FileObjectInfo
    );

extern
NTSTATUS
OcCrProcessFileObjectCreating(
    IN PFILE_OBJECT        SysFileObject,
    IN POC_FILE_OBJECT_CREATE_INFO    CreationInfoObject OPTIONAL,
    IN BOOLEAN             CheckForDuplicate,
    OUT POC_FILE_OBJECT*   RefOcFileObject
    );

extern
VOID
OcCrProcessFileObjectCloseRequest(
    IN PFILE_OBJECT    SysFileObject
    );

extern
VOID
NTAPI
OcCrProcessFileObjectRemovedFromHash(
    IN POC_FILE_OBJECT    FileObject
    );

extern
VOID
NTAPI
OcCrProcessFoContextRemovedFromHash(
    IN POC_CONTEXT_OBJECT    ContextObject
    );

extern
POC_FILE_OBJECT
OcCrReferenceFileObjectBySystemObject(
    IN PFILE_OBJECT    SysFileObject
    );

extern
NTSTATUS
OcCrGetFileObjectCreationInfo(
    IN const struct _OC_NODE_CTX*    CommonContext,
    IN PFLT_CALLBACK_DATA    CallbackData,
    IN POC_DEVICE_OBJECT     VolumeDevice,
    IN OC_REQUEST_TYPE       RequestType,
    OUT POC_FILE_OBJECT_CREATE_INFO*    PtrReferencedFileInfoObject
    );

extern
NTSTATUS
OcCrInsertFileCreateInfoObjectInDeviceList(
    IN POC_FILE_OBJECT_CREATE_INFO    FileCreateInfo
    );

extern
VOID
OcCrRemoveFileCreateInfoObjectFromDeviceList(
    IN POC_FILE_OBJECT_CREATE_INFO    FileCreateInfo
    );

extern
VOID
OcCrCheckSharedCacheMapFileObject(
    IN POC_FILE_OBJECT    FileObject
    );

extern
NTSTATUS
OcCrProcessFoCreateRequestCompletion(
    IN PIO_STATUS_BLOCK    IoStatusBlock,
    IN POC_FILE_OBJECT_CREATE_INFO    CreationInfo,
    IN PFILE_OBJECT    TargetFileObject,
    IN OC_REQUEST_TYPE    RequestType
    );

struct _OC_NODE_CTX;

extern
NTSTATUS
OcCrProcessFoCreateRequest(
    IN const struct _OC_NODE_CTX*   CommonContext,
    IN PFLT_CALLBACK_DATA    Data,
    IN POC_DEVICE_OBJECT     PtrOcDeviceObject,
    IN OC_REQUEST_TYPE       RequestType,
    OUT POC_FILE_OBJECT_CREATE_INFO*    RefCreationInfo
    );

extern
POC_FILE_OBJECT
OcCrRetriveReferencedFileObject(
    IN PFILE_OBJECT    SystemFileObject
    );

//---------------------------------------------------

#endif//_OC_FILEOBJECT_
