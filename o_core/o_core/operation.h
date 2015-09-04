/*
Author: Slava Imameev   
Copyright (c) 2007  , Slava Imameev
All Rights Reserved.

Revision history:
03.04.2007 ( March )
 Start
*/

/*
this file contains definitions for the operation object
*/

#ifndef _OC_OPERATION_OBJECT_
#define _OC_OPERATION_OBJECT_

//------------------------------------------------------

typedef enum _OC_PRIVATE_BUFFER_TYPE{

    OcCrPrivateBufferFromSystemPool = 0x1,
    OcCrPrivateBufferFromMappedMemory = 0x2
} OC_PRIVATE_BUFFER_TYPE, *POC_PRIVATE_BUFFER_TYPE;

//------------------------------------------------------

typedef ULONG    OC_ACCESS_RIGHTS;

typedef struct _OC_REQUEST_SECURITY_PARAMETERS{

    //
    // acccess rights recquired by the operation,
    // from the view point of a device which 
    // receives the request
    //
    OC_ACCESS_RIGHTS  RequestedAccess;

    //
    // 0x1 - allow request, 0x0 - disable request
    //
    ULONG             AllowRequest:0x1;


    //
    // should request be logged, only the upper
    // stack device( i.e. that receives the request )
    // can change this value
    //
    ULONG             LogRequest:0x1;

    //
    // should request be shadowed, only the upper
    // stack device( i.e. that receives the request )
    // can change this value
    //
    ULONG             ShadowWriteRequest:0x1;
    ULONG             ShadowReadRequest:0x1;

    //
    // defines the reason for the shadow flags
    //
    DLD_SHADOW_REASON    ShadowReason;

} OC_REQUEST_SECURITY_PARAMETERS, *POC_REQUEST_SECURITY_PARAMETERS;

//------------------------------------------------------

#define _OC_OPERATION_OBJ_SIGNATURE    (ULONG)0xFFABC123

typedef struct _OC_OPERATION_OBJECT{

    //
    // all currently active operations are
    // linked in a list with the head in a PDO
    //
    LIST_ENTRY           ListEntry;

#if DBG
    //
    // must be _OC_OPERATION_OBJ_SIGNATURE
    //
    ULONG                Signature;
#endif//DBG
    //
    // referenced file object for the operation
    //
    POC_FILE_OBJECT      FileObject;

    //
    // referenced device object for the operation
    //
    POC_DEVICE_OBJECT    DeviceObject;

    //
    // refernced device object in which queue the
    // operation object has been inserted
    //
    POC_DEVICE_OBJECT    QueueDeviceObject OPTIONAL;

    //
    // major function code for this operations
    //
    UCHAR                MajorFunction;

    //
    // Irp for Irp based operations
    //
    PIRP                 Irp;

    //
    // the requestor's thread
    //
    PETHREAD             RequestorThread;

    //
    // the lock for fields protection
    //
    KSPIN_LOCK            SpinLock;

    //
    // used for request completing in a shadow thread
    //
    PVOID                 CompletionContext;

    //
    // the event is used to signal the shadowing thread
    // that the buffers coping has been completed in
    // the completion routine
    //
    PKEVENT               CopingInPrivateBuffersCompleteEvent;

    //
    // the following flags contains log and shadowing settings
    // as they are defined by the current settings
    //
    OC_REQUEST_SECURITY_PARAMETERS    SecurityParameters;

    struct {
        ULONG    FreeMdl:0x1;
        ULONG    UnlockMdl:0x1;
        ULONG    ObjectSentInShadowingModule:0x1;

        //
        // the following four flags are protected by SpinLock
        //
        ULONG    CompleteInShadowThread:0x1;
        ULONG    WaitForCopingInPrivateBuffers:0x1;
        ULONG    ShadowingInProgress:0x1;

        //
        // this flag means that the data has been copied in 
        // a private driver's buffer and the request could
        // be completed safely without waiting for shadowing
        // completion or synchronizing with it,
        // some pieces of code use the SpinLock for 
        // syncronization in set-and-check for this flag
        //
        ULONG    DataInPrivateBuffer:0x1;

        //
        // if a private buffer has been allocated the
        // flag is set
        //
        ULONG    PrivateBufferAllocated:0x1;

        //
        // the following flag says that the
        // request has made its way through
        // all devices in the stack,
        // it is not necessary set for all
        // objects, do not make any
        // serios conclusion based on this flag
        //
        ULONG    HadHisTimeOnStack:0x1;

        //
        // There was an error during this request processing
        //
        ULONG    RequestProcessingError:0x1;

        //
        // set to 0xq if describes the request to
        // the FSD minifilter part of the driver
        //
        ULONG    MinifilterOperation:0x1;

    } Flags;

    //
    // the following structure is used to copy data
    // in a private buffer for shadowing purpose
    //
    struct{

        //
        // the buffer's address
        //
        PVOID                     Buffer;

        //
        // the buffer's type
        //
        OC_PRIVATE_BUFFER_TYPE    Type;

    } PrivateBufferInfo;

    //
    // contexts used for this operation, depend on operation
    //
    union{

        //
        // IRP_MJ_CREATE request
        //
        struct{
            POC_FILE_OBJECT_CREATE_INFO    RefCreationInfo;
            POC_FLT_CALLBACK_DATA_STORE    FltCallbackDataStore;
            struct {
                //
                // if the following flag is set then the 
                // request creator has not provided the 
                // FileObject, this happens when drivers
                // in a stack uses the IRP_MJ_CRATE request
                // for their disposal, for example,
                // to acquire the stack ownership like
                // the serialenum driver does
                //
                ULONG    IsFileObjectAbsent:0x1;
            } Flags;
        } Create;

        //
        // IRP_MJ_WRITE request
        //
        struct{
            PMDL     MDL OPTIONAL;
            PVOID    SystemBuffer OPTIONAL;
            PVOID    UserBuffer OPTIONAL;
            ULONG    BufferLength;
            LARGE_INTEGER    ByteOffset;
        } Write;

        //
        // IRP_MJ_READ request
        //
        struct{
            PMDL     MDL OPTIONAL;
            PVOID    SystemBuffer OPTIONAL;
            PVOID    UserBuffer OPTIONAL;
            ULONG    BufferLength;
            LARGE_INTEGER    ByteOffset;
        } Read;

    } OperationParameters;

} OC_OPERATION_OBJECT, *POC_OPERATION_OBJECT;

//------------------------------------------------------

VOID
NTAPI
OcCrDeleteOperationObject(
    POC_OPERATION_OBJECT    OperationObject
    );

//------------------------------------------------------

NTSTATUS
OcCrProcessOperationObjectPrivateBuffers(
    __in POC_OPERATION_OBJECT    OperationObject,
    __in BOOLEAN                 CopyDataInPrivateBuffer
    );

//------------------------------------------------------

__forceinline
BOOLEAN
OcIsOperationShadowedAsReadRequest( 
    __in const OC_REQUEST_SECURITY_PARAMETERS*    SecurityParameters
    )
{
    return ( 0x1 == SecurityParameters->ShadowReadRequest );
}

__forceinline
BOOLEAN
OcIsOperationShadowedAsWriteRequest( 
    __in const OC_REQUEST_SECURITY_PARAMETERS*    SecurityParameters
    )
{
    return ( 0x1 == SecurityParameters->ShadowReadRequest );
}

__forceinline
BOOLEAN
OcIsOperationShadowed(
    __in const OC_REQUEST_SECURITY_PARAMETERS*    SecurityParameters
    )
{
    return ( OcIsOperationShadowedAsReadRequest( SecurityParameters ) || 
             OcIsOperationShadowedAsWriteRequest( SecurityParameters ) );
}

__forceinline
VOID
OcMarkForShadowAsWriteOperation( 
    __inout POC_REQUEST_SECURITY_PARAMETERS    SecurityParameters
    )
{
    SecurityParameters->ShadowWriteRequest = 0x1;
}

__forceinline
VOID
OcMarkForShadowAsReadOperation( 
    __inout POC_REQUEST_SECURITY_PARAMETERS    SecurityParameters
    )
{
    SecurityParameters->ShadowReadRequest = 0x1;
}

__forceinline
BOOLEAN
OcIsOperationLogged( 
    __in const OC_REQUEST_SECURITY_PARAMETERS*    SecurityParameters
    )
{
    return ( 0x1 == SecurityParameters->LogRequest );
}

__forceinline
VOID
OcMarkOperationAsLogged( 
    __inout POC_REQUEST_SECURITY_PARAMETERS    SecurityParameters
    )
{
    SecurityParameters->LogRequest = 0x1;
}

#endif//_OC_OPERATION_OBJECT_
