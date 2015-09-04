/*
Author: Slava Imameev   
Copyright (c) 2006  , Slava Imameev
All Rights Reserved.

Revision history:
04.12.2006 ( December )
 Start
*/

/*
this file contains the PnP devices database
The heart and the brain of the driver!
*/
#include "struct.h"
#include "proto.h"
#include <PnPFilterControl.h>

//-------------------------------------------------

static
NTSTATUS
OcCrProcessNewPnPDevice(
    IN PDEVICE_OBJECT    Pdo,
    IN PDEVICE_OBJECT    AttachedDo OPTIONAL,
    IN OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType,
    IN BOOLEAN    PdoIsInitialized,
    OUT POC_DEVICE_OBJECT*    PtrPtrOcDeviceObject
    );

static
VOID
OcCrRedefineTypesForDeviceGrowingFromRoot(
    IN POC_DEVICE_OBJECT    RootDevice
    );

static
NTSTATUS
OcCrCreateDevicePropertyObjectForPdo(
    IN PDEVICE_OBJECT    Pdo,
    OUT POC_DEVICE_PROPERTY_OBJECT    *PtrPtrDevicePropertyObject
    );

static
NTSTATUS
OcCrCreateDevicePropertyObjectForNonPnPDevice(
    IN PDEVICE_OBJECT    LowerDevice,
    OUT POC_DEVICE_PROPERTY_OBJECT    *PtrPtrDevicePropertyObject
    );

static
NTSTATUS
NTAPI
OcCrCreateDevicePropertyObjectForPdoWR(
    IN ULONG_PTR    Pdo,//PDEVICE_OBJECT
    OUT ULONG_PTR   PtrPtrDevicePropertyObject,//POC_DEVICE_PROPERTY_OBJECT
    IN ULONG_PTR    Event,//PKEVENT
    OUT ULONG_PTR   PtrReturnedCode// NTSTATUS*
    );

static
NTSTATUS
OcCrFindAndProcessFdoForPdo(
    __in POC_DEVICE_OBJECT       PtrOcPdo
    );

static
NTSTATUS
OcCrDefinePnPFilterType(
    __inout_opt POC_DEVICE_OBJECT    PtrFido
    );

static
VOID
OcCrCheckParentDevice(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject
    );

static
NTSTATUS
OcCrSetFullDeviceType(
    __inout POC_DEVICE_OBJECT    OcDeviceObject
    );

//-------------------------------------------------

__forceinline
POC_DEVICE_OBJECT
OcCrReturnPDO(
    IN OUT POC_DEVICE_OBJECT    PtrOcDeviceObject
    )
    /*
    the function returns deivice's PDO, it doesn't change any reference count
    the function can't return NULL, at least it returns PtrOcDeviceObject
    */
{
    if( NULL != PtrOcDeviceObject->Pdo && 
        OcDevicePnPTypePdo != PtrOcDeviceObject->DevicePnPType && 
        OcDeviceLowerNoPnPType != PtrOcDeviceObject->DevicePnPType )
            return PtrOcDeviceObject->Pdo;

    ASSERT( OcDevicePnPTypePdo == PtrOcDeviceObject->DevicePnPType || 
            OcDeviceLowerNoPnPType == PtrOcDeviceObject->DevicePnPType );

    return PtrOcDeviceObject;
}

//-------------------------------------------------

__forceinline
VOID
OcCrUpperDeviceToPdoIfPossibe(
    IN OUT POC_DEVICE_OBJECT*    PtrPtrOcDeviceObject
    )
    /*
    function returns referenced PDO object
    and dereferences the *PtrPtrOcDeviceObject,
    if PDO doesn't exist the *PtrPtrOcDeviceObject
    is returned w/o changing its reference
    count, if PtrOcDeviceObject is PDO
    then it is returned w/o changing its
    reference count
    */
{
    POC_DEVICE_OBJECT    PtrOcDeviceObject = *PtrPtrOcDeviceObject;
    POC_DEVICE_OBJECT    PtrPDO;

    ASSERT( ( OcDevicePnPTypePdo == PtrOcDeviceObject->DevicePnPType ||
              OcDeviceLowerNoPnPType == PtrOcDeviceObject->DevicePnPType )? 
             NULL == PtrOcDeviceObject->Pdo :
             TRUE );

    PtrPDO = OcCrReturnPDO( PtrOcDeviceObject );
    if( PtrPDO != PtrOcDeviceObject ){

        OcObReferenceObject( PtrPDO );
        OcObDereferenceObject( PtrOcDeviceObject );

        *PtrPtrOcDeviceObject = PtrPDO;
    }//if

}

//-------------------------------------------------

VOID
OcCrDeviceStackBuildCompleted(
    IN POC_DEVICE_OBJECT   PtrOcDeviceObject
    )
{
    POC_DEVICE_OBJECT    PtrOcPdo;

    PtrOcPdo = OcCrReturnPDO( PtrOcDeviceObject );

    ASSERT( OcDevicePnPTypePdo == PtrOcPdo->DevicePnPType || OcDeviceLowerNoPnPType == PtrOcPdo->DevicePnPType );

    //
    // set event in a signal state to allow the PnP manager to work
    //
    KeSetEvent( &PtrOcPdo->PnPTreeBuildCompletedEvent,
                IO_NO_INCREMENT,
                FALSE );
}

//-------------------------------------------------

VOID
OcCrWaitUntilDeviceStackIsBuilt(
    IN POC_DEVICE_OBJECT   PtrOcDeviceObject
    )
{
    LARGE_INTEGER        liTimeout;
    NTSTATUS             RC;
    POC_DEVICE_OBJECT    PtrOcPdo;

    PtrOcPdo = OcCrReturnPDO( PtrOcDeviceObject );

    ASSERT( OcDevicePnPTypePdo == PtrOcPdo->DevicePnPType ||
            OcDeviceLowerNoPnPType == PtrOcPdo->DevicePnPType );

    //
    // I should not try to wait on cripple PDO
    //
    ASSERT( 0x0 == PtrOcPdo->Flags.CripplePdo );
    if( 0x1 == PtrOcPdo->Flags.CripplePdo ){

        //
        // something went wrong, may be resources were not allocated for the device's property
        // set the event for this cripple PDO to avoid system stalling
        //
        KeSetEvent( &PtrOcPdo->PnPTreeBuildCompletedEvent, IO_NO_INCREMENT, FALSE );
    }

    //
    // set timeout to 9 seconds.
    //
    liTimeout.QuadPart = (LONGLONG)-90000000;//9 sec in 100 nanoseconds units

    RC = KeWaitForSingleObject( &PtrOcPdo->PnPTreeBuildCompletedEvent,
                                Executive,
                                KernelMode,
                                FALSE,
                                &liTimeout );
    if( STATUS_TIMEOUT == RC ){

        ASSERT( !"The PnP tree build event has not been set in a signal state in a reasonable time" );

        //
        // consider the stack as built
        //
        OcCrDeviceStackBuildCompleted( PtrOcDeviceObject );
    }

}

//-------------------------------------------------

NTSTATUS
OcCrCreateNewDeviceEntryInsertInHash(
    IN PDEVICE_OBJECT    DeviceObject,
    IN POC_DEVICE_OBJECT   PtrOcPdoObject OPTIONAL,
    IN OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType, 
    OUT POC_DEVICE_OBJECT*    PtrPtrOcDeviceObject
    )
    /*
    The caller must acquire Global.DeviceHashResource exclusive.

    In cas of success the function returns REFERENCED *PtrPtrOcDeviceObject,
    this is the caller's responsibility to dereference the object.

    The counterpart function is OcCrCleanupDeviceAfterRemovingFromHash.

    PtrOcPdoObject must be NULL for the PDO and non NULL for others.
    */
{
    NTSTATUS             RC;
    KIRQL                OldIrql;
    POC_DEVICE_OBJECT    PtrOcDeviceObject = NULL;
    POBJECT_NAME_INFORMATION       PtrDeviceNameInfo = NULL;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( ExIsResourceAcquiredExclusiveLite( &Global.DeviceHashResource ) );
    ASSERT( ( OcDevicePnPTypePdo != PnPDeviceType && OcDeviceLowerNoPnPType != PnPDeviceType )? 
             ( NULL != PtrOcPdoObject ): 
             ( NULL == PtrOcPdoObject ) );

    ASSERT( (NULL != PtrOcPdoObject)?
             ( Global.PnPFilterDeviceObject->DriverObject != PtrOcPdoObject->KernelDeviceObject->DriverObject ):
             TRUE );

    //
    // hook the PDO's driver or middle device's driver or lower device's driver
    // before creating the PDO or lower device entry or middle device,
    // and do not forget that PnP filter is used for attaching to
    // non PnP devices, the attached devices have the type ( OcDeviceNoPnPTypeInMiddleOfStack ),
    // do not hook PnP filter
    //
    if( ( OcDevicePnPTypePdo == PnPDeviceType || 
          OcDevicePnPTypeInMiddleOfStack == PnPDeviceType || 
          OcDeviceNoPnPTypeInMiddleOfStack == PnPDeviceType ||
          OcDeviceLowerNoPnPType == PnPDeviceType ||
          OcDevicePnPTypeFunctionalDo == PnPDeviceType ) && 
          Global.PnPFilterDeviceObject->DriverObject != DeviceObject->DriverObject ){

        RC = Global.DriverHookerExports.HookDriver( DeviceObject->DriverObject );
        if( !NT_SUCCESS( RC ) )
            goto __exit;
    }

    //
    // first try to find the device in the hash,
    // if this is a real AddDevice it might be found
    // because has been inserted while processing
    // device relations. If the object is found then
    // return it.
    //
    PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                   (ULONG_PTR)DeviceObject,
                                                   OcObReferenceObject );
    if( NULL != PtrOcDeviceObject ){

        //
        // object has been found in the hash, but it might have the different PnP type,
        // set the new type if it puts the device in a more narrow set
        //
        if( GET_PNP_TYPE_CLASS( PnPDeviceType ) > GET_PNP_TYPE_CLASS( PtrOcDeviceObject->DevicePnPType ) ){

                ASSERT( OcDevicePnPTypePdo == PnPDeviceType || 
                        OcDevicePnPTypeFilterDo == PnPDeviceType ||
                        OcDevicePnPTypeFunctionalDo == PnPDeviceType );

                PtrOcDeviceObject->DevicePnPType = PnPDeviceType;
        }

        RC = STATUS_SUCCESS;
        goto __exit;
    }

    //
    // get the device name, it is safe to call ObQueryNameString
    // because for device objects there is no any query Irp is sent,
    // because there is no registered query callback for the device 
    // object type which can send an Irp.
    //
    RC = OcCrQueryObjectName( DeviceObject, &PtrDeviceNameInfo );
    ASSERT( STATUS_PENDING != RC );
    if( !NT_SUCCESS( RC ) ){

        //
        // let the function to run
        //
        PtrDeviceNameInfo = NULL;
        RC = STATUS_SUCCESS;
    }

    //
    // create the new device object
    //
    RC = OcObCreateObject( &Global.OcDeviceObjectType,
                           &PtrOcDeviceObject );
    if( !NT_SUCCESS( RC ) ){
        ASSERT(!"OcObCreateObject failed in OcCrCreateNewDeviceEntryInsertInHash");
        goto __exit;
    }

    //
    // initialize the body of the new object
    //

    //
    // start of body initialization
    //
    {
        RtlZeroMemory( PtrOcDeviceObject, sizeof( *PtrOcDeviceObject ) );
        KeInitializeEvent( &PtrOcDeviceObject->PnPTreeBuildCompletedEvent,
                           NotificationEvent,
                           FALSE );
        OcRwInitializeRwLock( &PtrOcDeviceObject->RwSpinLock );
        OcRwInitializeRwLock( &PtrOcDeviceObject->RwTraversingSpinLock );
        InitializeListHead( &PtrOcDeviceObject->ListEntry );
        OcRlInitializeRemoveLock( &PtrOcDeviceObject->RemoveLock.Common, OcFreeRemoveLock );
        PtrOcDeviceObject->Enumerator = en_OC_UNKNOWN_ENUM;
        PtrOcDeviceObject->DevicePnPType = PnPDeviceType;
        PtrOcDeviceObject->PnPState = NotStarted;
        PtrOcDeviceObject->KernelDeviceObject = DeviceObject;

        //
        // exchange the pointers to the device name information, the
        // memory will be freed when the device is being deleted
        //
        PtrOcDeviceObject->DeviceNameInfo = PtrDeviceNameInfo;
        PtrDeviceNameInfo = NULL;

        //
        // initialize upper devices list
        //
        if( OcDevicePnPTypePdo == PnPDeviceType || OcDeviceLowerNoPnPType == PnPDeviceType ){
            InitializeListHead( &PtrOcDeviceObject->UpperDevicesList.PdoHeadForListOfUpperDevices );
        } else {
            InitializeListHead( &PtrOcDeviceObject->UpperDevicesList.EntryForListOfUpperDevices );
        }

        KeInitializeSpinLock( &PtrOcDeviceObject->PdoUpperDevicesListSpinLock );

        //
        // initialize create and IO requests lists
        //
        OcRwInitializeRwLock( &PtrOcDeviceObject->DeviceRequests.CreateRequestsListRwLock );
        OcRwInitializeRwLock( &PtrOcDeviceObject->DeviceRequests.IoRequestsListRwLock );
        InitializeListHead( &PtrOcDeviceObject->DeviceRequests.CreateRequestsListHead );
        InitializeListHead( &PtrOcDeviceObject->DeviceRequests.IoRequestsListHead );

        //
        // initialize PDO pointer
        //
        if( NULL != PtrOcPdoObject ){

            PtrOcDeviceObject->Pdo = PtrOcPdoObject;
            //
            // reference the PDO object, the object
            // will be dereferenced when the PtrOcDeviceObject 
            // device is about to be removed
            //
            OcObReferenceObject( PtrOcDeviceObject->Pdo );

            if( OcDevicePnPTypeInMiddleOfStack == PnPDeviceType || 
                OcDeviceNoPnPTypeInMiddleOfStack == PnPDeviceType || 
                OcDevicePnPTypeFunctionalDo == PnPDeviceType ){

                KIRQL    OldIrql;

                //
                // save the pointer to this device in the
                // list of the middle stack devices
                //
                KeAcquireSpinLock( &PtrOcDeviceObject->Pdo->PdoUpperDevicesListSpinLock, &OldIrql );
                {// start of the lock
                    InsertTailList( &PtrOcDeviceObject->Pdo->UpperDevicesList.PdoHeadForListOfUpperDevices,
                                    &PtrOcDeviceObject->UpperDevicesList.EntryForListOfUpperDevices );
                }// end of the lock
                KeReleaseSpinLock( &PtrOcDeviceObject->Pdo->PdoUpperDevicesListSpinLock, OldIrql );

            }//if( OcDevicePnPTypeInMiddleOfStack

        } else {

            PtrOcDeviceObject->Pdo = NULL;
        }

        if( OcDevicePnPTypePdo == PnPDeviceType ){

            //
            // the PDO at this point is not fully initialized
            //
            PtrOcDeviceObject->Flags.CripplePdo = 0x1;
        }
    }
    //
    // end of body initialization
    //

    //
    // from that point the object is in a valid state to be referenced
    // and dereferenced
    //

    //
    // insert the device object in the hash, the key is 
    // OS kernel's device object
    //
    RC = OcHsInsertContextInHash( Global.PtrDeviceHashObject,
                                  (ULONG_PTR)DeviceObject,
                                  (PVOID)PtrOcDeviceObject,
                                  OcObReferenceObject );
    if( !NT_SUCCESS( RC ) ){
        ASSERT(!"OcHsInsertContextInHash failed in OcCrCreateNewDeviceEntryInsertInHash");
        goto __exit;
    }

    //
    // insert the device object in the global list
    //
    OcRwAcquireLockForWrite( &Global.DevObjListLock, &OldIrql );
    {// start of the lock
        InsertTailList( &Global.DevObjListHead, &PtrOcDeviceObject->ListEntry );
    }// end of the lock
    OcRwReleaseWriteLock( &Global.DevObjListLock, OldIrql );

__exit:

    ASSERT( NT_SUCCESS( RC ) );

    if( NT_SUCCESS( RC ) ){

        *PtrPtrOcDeviceObject = PtrOcDeviceObject;

    } else {

        //
        // ERROR! something went wrong
        //

        //
        // free the name info if it has not been copied to the device object
        //
        if( NULL != PtrDeviceNameInfo )
            ExFreePoolWithTag( PtrDeviceNameInfo, Global.OcDeviceObjectType.Tag );

        //
        // delete the device object
        //
        if( NULL != PtrOcDeviceObject )
            OcObDereferenceObject( PtrOcDeviceObject );
    }

    return RC;
}

//-------------------------------------------------

VOID
NTAPI
OcCrPnPFilterReportNewDevice(
    IN PDEVICE_OBJECT    Pdo,
    IN PDEVICE_OBJECT    AttachedDo OPTIONAL,
    IN OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType,
    IN BOOLEAN           IsPdoInitialized
    )
    /*
    IsPdoInitialized - is TRUE if the PnP manager initializes the PDO
    and it is safe to query information for this PDO from the PnP Manager
    */
{
    NTSTATUS    RC;
    POC_DEVICE_OBJECT    PtrOcDeviceObject = NULL;

    //
    // if the attached device is present then the PDO's
    // DeviceNode has been initialized by the PnP 
    // manager and this is an AddDevice stage or later stage
    //
    RC = OcCrProcessNewPnPDevice( Pdo,
                                  AttachedDo,
                                  PnPDeviceType,
                                  (BOOLEAN)( NULL != AttachedDo ) || IsPdoInitialized,
                                  &PtrOcDeviceObject );

    if( NT_SUCCESS( RC ) )
        OcObDereferenceObject( PtrOcDeviceObject );
}

//-------------------------------------------------

NTSTATUS
OcCrProcessNewPnPDevice(
    IN PDEVICE_OBJECT    Pdo,
    IN PDEVICE_OBJECT    AttachedDo OPTIONAL,
    IN OC_DEVICE_OBJECT_PNP_TYPE    PnPDeviceType,
    IN BOOLEAN    PdoIsInitialized,
    OUT POC_DEVICE_OBJECT*    PtrPtrOcDeviceObject
    )
    /*

    Arguments:
         Pdo - the lower device object in a stack, 
               a bad choice of the name, this might be a non-PnP device, 
               the actual type is defined by the combination of 
               PnPDeviceType and AttachedDo
         AttachedDo - the device in the same stack as Pdo
         PnPDeviceType - the type for AttachedDo if it is not NULL or else
                         the type for Pdo

    if AttachedDo is not NULL then its device
    object is returned, else the Pdo device
    object is returned in PtrPtrOcDeviceObject

    PdoIsInitialized is TRUE if the Pdo has 
    an initialized DeviceNode, this doesn't
    mean that the Pdo has been started!

    */
{
    NTSTATUS             RC;
    POC_DEVICE_OBJECT    PtrOcFidoObject = NULL;
    POC_DEVICE_OBJECT    PtrOcPdoObject = NULL;
    BOOLEAN              LockIsAcquired = FALSE;
    BOOLEAN              CripplePdo = FALSE;
    POC_DEVICE_PROPERTY_OBJECT    DevicePropertyObject = NULL;
    BOOLEAN              CheckParentDevices = FALSE;
    BOOLEAN              PnPDeviceStack = ( OcDeviceNoPnPTypeInMiddleOfStack != PnPDeviceType && 
                                            OcDeviceLowerNoPnPType != PnPDeviceType );

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( NULL != Pdo );
    ASSERT( AttachedDo != Pdo );
    //ASSERT( !( !PnPDeviceStack && ( NULL == Pdo || NULL ==AttachedDo ) ) );

    ASSERT( OcDevicePnPTypePdo == PnPDeviceType ||
            OcDevicePnPTypeFilterDo == PnPDeviceType ||
            OcDevicePnPTypeFunctionalDo == PnPDeviceType ||
            OcDevicePnPTypeInMiddleOfStack == PnPDeviceType ||
            OcDeviceNoPnPTypeInMiddleOfStack == PnPDeviceType ||
            OcDeviceLowerNoPnPType == PnPDeviceType);

    ASSERT( AttachedDo? ( OcDevicePnPTypeFilterDo == PnPDeviceType ||
                          OcDevicePnPTypeFunctionalDo == PnPDeviceType || 
                          OcDevicePnPTypeInMiddleOfStack == PnPDeviceType || 
                          OcDeviceNoPnPTypeInMiddleOfStack == PnPDeviceType ):
                         ( OcDevicePnPTypePdo == PnPDeviceType || 
                           OcDeviceLowerNoPnPType == PnPDeviceType ) );

    if( AttachedDo == Pdo ){

#if DBG
        KeBugCheckEx( OC_CORE_BUG_PDO_EQUAL_TO_ATTACHED_DEVICE, 
                      (ULONG_PTR)__LINE__,
                      (ULONG_PTR)Pdo, 
                      (ULONG_PTR)PnPDeviceType, 
                      (ULONG_PTR)&Global );
#endif//DBG

        return STATUS_INVALID_PARAMETER_1;
    }

    //
    // get the device information only if the device has been initialized
    // by the PnP manager, i.e. the Device Node has been created
    //
    if( TRUE == PdoIsInitialized && PnPDeviceStack ){

        if( IoIsSystemThread( PsGetCurrentThread() ) && KeGetCurrentIrql() == PASSIVE_LEVEL ){

            //
            // retreive the device's information
            //
            RC = OcCrCreateDevicePropertyObjectForPdo( Pdo,
                                                       &DevicePropertyObject );

        } else {

            //
            // there is not a system thread or not
            // the lowest IRQ level,
            // send the request in a worker thread
            // and wait for its completion
            //
            KEVENT    Event;
            NTSTATUS  ThreadRC;

            KeInitializeEvent( &Event, SynchronizationEvent, FALSE );

            ThreadRC = OcWthPostWorkItemParam4( 
                           Global.QueryDevicePropertyThread->PtrWorkItemListObject,
                           FALSE,
                           OcCrCreateDevicePropertyObjectForPdoWR,
                           (ULONG_PTR)Pdo,
                           (ULONG_PTR)&DevicePropertyObject,
                           (ULONG_PTR)&Event,
                           (ULONG_PTR)&RC );
            if( NT_SUCCESS( ThreadRC ) ){

                //
                // wait for OcCrCreateDevicePropertyObjectForPdoWR completion
                //
                KeWaitForSingleObject( &Event,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL );
            } else {

                ASSERT( !"Something went wrong with the Global.QueryDevicePropertyThread" );
                RC = ThreadRC;
            }

        }

        //
        // do not exit in case of error, 
        // this might be a cripple PDO( like the classdisk's PDOs with DevNode set to NULL )
        // remember that the PDO is cripple
        //
        if( !NT_SUCCESS( RC ) ){

            CripplePdo = TRUE;
            DevicePropertyObject = NULL;
            RC = STATUS_SUCCESS;
        }

    } else if( FALSE == PnPDeviceStack ){

        RC = OcCrCreateDevicePropertyObjectForNonPnPDevice( Pdo,
                                                            &DevicePropertyObject );

        ASSERT( NT_SUCCESS( RC ) );
        ASSERT( NULL != DevicePropertyObject );
        if( !NT_SUCCESS( RC ) ){

            CripplePdo = TRUE;
            DevicePropertyObject = NULL;
            RC = STATUS_SUCCESS;
        }
    }

    //
    // synchronize the insertion in the hash
    //
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite( &Global.DeviceHashResource, TRUE );
    LockIsAcquired = TRUE;

    //
    // create an object for the physical device object,
    // and insert it in the hash, PnPDeviceStack is used to 
    // define the type of the lower device - either PnP device 
    // or non-PnP device
    //
    RC = OcCrCreateNewDeviceEntryInsertInHash( Pdo,
                                               NULL,
                                               PnPDeviceStack? OcDevicePnPTypePdo : OcDeviceLowerNoPnPType,
                                               &PtrOcPdoObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    ASSERT( NULL != PtrOcPdoObject );

    if( NULL != AttachedDo ){

        //
        // create an object for the upper device object,
        // and insert it in the hash
        //
        RC = OcCrCreateNewDeviceEntryInsertInHash( AttachedDo,
                                                   PtrOcPdoObject,
                                                   PnPDeviceType,
                                                   &PtrOcFidoObject );
        if( !NT_SUCCESS( RC ) )
            goto __exit;

        ASSERT( NULL != PtrOcFidoObject );
    }

    //
    // save the device's property in the Pdo if it has not been already saved
    //
    if( NULL == PtrOcPdoObject->DevicePropertyObject ){

        PtrOcPdoObject->DevicePropertyObject = DevicePropertyObject;
        if( NULL != DevicePropertyObject ){

            ASSERT( TRUE == PdoIsInitialized || !PnPDeviceStack );
            ASSERT( FALSE == CripplePdo );

            if( !PnPDeviceStack ){

                //
                // mark the device as started as this is not a PnP device and
                // it actually doesn't have a PnP state, setting the state
                // to started says that we can safely intervene in the
                // device activity
                //
                PtrOcPdoObject->PnPState = Started;

            }

            //
            // save the enumerator in the device object, because
            // this is the most frequently used field
            //
            PtrOcPdoObject->Enumerator = DevicePropertyObject->Header.Enumerator;

            //
            // So this is the first time when we see this PDO as initialized.
            // Find the device with which the PDO is related as a child, if
            // the parent has not been found then send the Removal and Ejection
            // relation requests to already started devices.
            // Actually I might call this function in the main function
            // body, but calling in this place reduces the load on the system.
            // 
            CheckParentDevices = TRUE;

            //
            // clear the cripple flag, the PDO is initialized and 
            // becomes a full-fledged PDO
            //
            PtrOcPdoObject->Flags.CripplePdo = 0x0;

        }//if( NULL != DevicePropertyObject )

        //
        // the referenced pointer has been saved in DO
        //
        DevicePropertyObject = NULL;

    } else {

        ASSERT( 0x0 == PtrOcPdoObject->Flags.CripplePdo );
    }

    ASSERT( NULL != PtrOcPdoObject );

    //
    // save a refrenced pointer to the property object 
    // in the attached device structure
    //
    if( NULL != PtrOcFidoObject && NULL != PtrOcPdoObject->DevicePropertyObject ){

        ASSERT( TRUE == LockIsAcquired );
        ASSERT( NULL == PtrOcFidoObject->DevicePropertyObject );

        //
        // save the pointer and reference the property object
        //
        PtrOcFidoObject->DevicePropertyObject = PtrOcPdoObject->DevicePropertyObject;
        OcObReferenceObject( PtrOcFidoObject->DevicePropertyObject );

        //
        // save the enumerator index as has been done for the PDO
        //
        PtrOcFidoObject->Enumerator = PtrOcFidoObject->DevicePropertyObject->Header.Enumerator;

    }//if( NULL != PtrOcFidoObject )

__exit:

    if( LockIsAcquired ){

        ExReleaseResourceLite( &Global.DeviceHashResource );
        KeLeaveCriticalRegion();
        LockIsAcquired = FALSE;
    }

    ASSERT( NT_SUCCESS( RC ) );

    if( !NT_SUCCESS( RC ) ){

        //
        // something went wrong,
        // release and delete the FDO and PDO objects
        //

        if( NULL != PtrOcFidoObject )
            OcObDereferenceObject( PtrOcFidoObject );

        if( NULL != PtrOcPdoObject )
            OcObDereferenceObject( PtrOcPdoObject );

        PtrOcFidoObject = NULL;
        PtrOcPdoObject = NULL;

    } else {

        //
        // all were successful
        //
        ASSERT( NT_SUCCESS( RC ) );

        if( TRUE == CheckParentDevices ){

            ASSERT( NULL != PtrOcPdoObject );

            OcCrCheckParentDevice( PtrOcPdoObject );
        }

        if( NULL != PtrOcFidoObject ){

            //
            // return the referenced FiDO or FDO
            //
            *PtrPtrOcDeviceObject = PtrOcFidoObject;

            if( OcDevicePnPTypeFilterDo == PtrOcFidoObject->DevicePnPType ){

                //
                // so there is some upper or lower filter,
                // try to find an Fdo, it will be found 
                // if PtrOcFidoObject is an upper filter, if it is a
                // lower filter and this function was called from 
                // PnP filter's AddDevice then an Fdo won't be found
                // because has not been created yet
                //
                ASSERT( NULL != PtrOcPdoObject );
                ASSERT( OcDevicePnPTypePdo == PtrOcPdoObject->DevicePnPType );

                //
                // find the FDO
                //
                OcCrFindAndProcessFdoForPdo( PtrOcPdoObject );

                //
                // define the filter type - lower or upper, remeber that
                // this function can be called only when OcCrFindAndProcessFdoForPdo
                // has been called at least once
                //
                OcCrDefinePnPFilterType( PtrOcFidoObject );

            }

            //
            // dereference the PDO, the PDO is retained through the reference in FiDO or FDO
            // see the OcCrCreateNewDeviceEntryInsertInHash function
            //
            OcObDereferenceObject( PtrOcPdoObject );
            PtrOcPdoObject = NULL;
        }

        if( NULL != PtrOcPdoObject ){

            //
            // return the referenced PDO
            //
            *PtrPtrOcDeviceObject = PtrOcPdoObject;
        }

        //
        // now call the DlDriver AddDevice callback
        //
        {

            OC_DLDRVR_CONNECTION_HANDLE    ConnectionHandle;

            ConnectionHandle = OcReferenceDlDriverConnection();
            if( OC_INVALID_DLDRV_CONNECTION != ConnectionHandle ){

                POC_PNP_CALLBACKS              PnpCallbacks;

                PnpCallbacks = OcDlDrvConnectionHandleToPnpCalbacks( ConnectionHandle );

                PnpCallbacks->AddDevicePostCallback( Pdo,
                                                     AttachedDo,
                                                     PnPDeviceType,
                                                     PdoIsInitialized );

                OcDereferenceDlDriverConnection( ConnectionHandle );
            }
        }

    }

    //
    // free the unused device property
    //
    if( NULL != DevicePropertyObject )
        OcObDereferenceObject( DevicePropertyObject );

    ASSERT( !ExIsResourceAcquiredExclusiveLite( &Global.DeviceHashResource ) );
    ASSERT( FALSE == LockIsAcquired );

    return RC;
}

//-------------------------------------------------

VOID
NTAPI
OcCrPnPFilterRepotNewDeviceStateInternal(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject,
    IN DEVICE_PNP_STATE     NewState
    )
{
#if DBG
    PDEVICE_OBJECT    KernelDeviceObject = PtrOcDeviceObject->KernelDeviceObject;
#endif//DBG

    //
    // we are going to wait
    //
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    //
    // if the PDO has been initialized by the PnP manager the DevicePropertyObject
    // must be present
    //
    ASSERT( !( NotStarted != PtrOcDeviceObject->PnPState && 
               Deleted != PtrOcDeviceObject->PnPState &&
               NULL == PtrOcDeviceObject->DevicePropertyObject ) );

    //
    // before changing the object state to started 
    // perfom some final initialization
    //
    if( Started == NewState ){

        ASSERT( !( ( 0x1 == PtrOcDeviceObject->Flags.LowerFilter || 
                     0x1 == PtrOcDeviceObject->Flags.UpperFilter ) && 
                   NULL == PtrOcDeviceObject->Pdo ) );

        //
        // The following is a very interesting piece of code - 
        // this is a workaround for the Microsoft's bug - the class
        // driver for HID devices changes the device's flags of the
        // underlying bus created PDO while processing IRP_MN_START
        // request, this bug has been acknowledged by Microsoft team,
        // though unofficially.
        //
        if( 0x1 == PtrOcDeviceObject->Flags.LowerFilter && 
            NULL != PtrOcDeviceObject->Pdo ){

            PDEVICE_OBJECT    Fido = PtrOcDeviceObject->KernelDeviceObject;
            PDEVICE_OBJECT    Pdo = PtrOcDeviceObject->Pdo->KernelDeviceObject;

            if( ( ( DO_BUFFERED_IO | DO_DIRECT_IO ) & Fido->Flags ) != 
                ( ( DO_BUFFERED_IO | DO_DIRECT_IO ) & Pdo->Flags ) ){

                    //
                    // the PDO flags has been changed, propagate it to
                    // the Filted Device Object, it is interesting that
                    // DriverVerifier was unaware about this possibility
                    // and was caught in the trap with the invalid flags
                    //

                    //
                    // clear the old flags
                    //
                    Fido->Flags = Fido->Flags & ~( DO_BUFFERED_IO | DO_DIRECT_IO );

                    //
                    // set the new one
                    //
                    Fido->Flags |= Pdo->Flags & ( DO_BUFFERED_IO | DO_DIRECT_IO );

            }

        }//if( 0x1 == PtrOcDeviceObject->Flags.LowerFilter )

        //
        // wait for the PnP tree building completion if this is a "real" start,
        //
        if( PsGetCurrentThread() != Global.ThreadCallingReportDevice ){

            OcCrWaitUntilDeviceStackIsBuilt( PtrOcDeviceObject );

        } else {

            //
            // this is a device reported by the PnP filter just after start
            // consider the stack for this device as built
            //
            OcCrDeviceStackBuildCompleted( PtrOcDeviceObject );
        }

        //
        // determine the device type, set the type only for a PDO if one exists
        //
        if( PtrOcDeviceObject->Pdo )
            OcCrSetFullDeviceType( PtrOcDeviceObject->Pdo );
        else
            OcCrSetFullDeviceType( PtrOcDeviceObject );

        ASSERT( (PtrOcDeviceObject->Pdo)?
                  NULL != PtrOcDeviceObject->Pdo->DeviceType:
                  NULL != PtrOcDeviceObject->DeviceType );

    }

    //
    // moving from any state to NotStarted is an error because
    // the NotStarted state is an initial state in which 
    // transition is impossible
    //
    ASSERT( !( NotStarted == NewState && 
               NotStarted != PtrOcDeviceObject->PnPState ) );

    //
    // mark the device and its PDO as started, the PDO's driver is usually 
    // a hooked driver so this is the only place where it can be processed
    //
    PtrOcDeviceObject->PnPState = NewState;
    if( PtrOcDeviceObject->Pdo ){

        ASSERT( !( NotStarted == NewState && 
                   NotStarted != PtrOcDeviceObject->Pdo->PnPState ) );

        PtrOcDeviceObject->Pdo->PnPState = NewState;
    }

    //
    // process all states except started which has been already processed
    //
    if( Deleted == NewState ){

        //
        // remove the FiDO entry from the hash, as a side
        // effect the PDO's entry will aslo be removed,
        // all processing will be done in OcCrCleanupDeviceAfterRemovingFromHash
        //
        OcHsRemoveContextByKeyValue( Global.PtrDeviceHashObject,
                                     (ULONG_PTR)PtrOcDeviceObject->KernelDeviceObject,
                                     OcCrCleanupDeviceAfterRemovingFromHash );
#if DBG
        {
            POC_DEVICE_OBJECT    PtrOcDeviceObject;

            PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                           (ULONG_PTR)KernelDeviceObject,
                                                           OcObReferenceObject );
            if( NULL != PtrOcDeviceObject ){

                //
                // OOOPSSS! A very serious bug!
                // I found the deleted entry!
                // The call is synchronous and 
                // has been done before calling IoDeleteDevice( DeviceObject ),
                // so the system has not been able
                // to create device object with the same address.
                //
                KeBugCheckEx( OC_CORE_BUG_DELETED_ENTRY_FOUND, 
                              (ULONG_PTR)__LINE__,
                              (ULONG_PTR)PtrOcDeviceObject, 
                              (ULONG_PTR)KernelDeviceObject, 
                              (ULONG_PTR)&Global );
            }
        }
#endif//DBG
    }//if( Deleted == NewState )

    //
    // now call the DlDriver's report device state callback
    //
    {

        OC_DLDRVR_CONNECTION_HANDLE    ConnectionHandle;

        ConnectionHandle = OcReferenceDlDriverConnection();
        if( OC_INVALID_DLDRV_CONNECTION != ConnectionHandle ){

            POC_PNP_CALLBACKS   PnpCallbacks;
            ULONG               MinorPnpCode;

            PnpCallbacks = OcDlDrvConnectionHandleToPnpCalbacks( ConnectionHandle );

            if( Started == NewState ){
                MinorPnpCode = IRP_MN_START_DEVICE;
            } else if( Deleted == NewState ){
                MinorPnpCode = IRP_MN_REMOVE_DEVICE;
            } else {
                ASSERT( !"Invalid device state. Investigate immediatelly!" );
            }

            PnpCallbacks->DeviceStatePostCallback( OcCrReturnPDO( PtrOcDeviceObject )->KernelDeviceObject,
                                                   PtrOcDeviceObject->KernelDeviceObject,
                                                   MinorPnpCode );

            OcDereferenceDlDriverConnection( ConnectionHandle );
        }
    }
}

//--------------------------------------------------

VOID
NTAPI
OcCrPnPFilterRepotNewDeviceState(
    IN PDEVICE_OBJECT    DeviceObject,
    IN DEVICE_PNP_STATE    NewState
    )
{
    POC_DEVICE_OBJECT    PtrOcDeviceObject;

    //
    // find the device entry
    //
    PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                   (ULONG_PTR)DeviceObject,
                                                   OcObReferenceObject );
    //
    // if the state is Deleted and the device has not been found
    // then nothing serious, it will be deleted in any case,
    // mostly this means that the device was removed( as a device 
    // created by a hooked driver ) when an upper device was being removed,
    // usually PDOs and FDOs are removed in such a manner and this is
    // a correct behavior, because hooked driver might be unhooked and
    // notification about removing is not sent in this case
    //
    ASSERT( !( NULL == PtrOcDeviceObject && Deleted != NewState ) );
    if( NULL == PtrOcDeviceObject )
        return;

    OcCrPnPFilterRepotNewDeviceStateInternal( PtrOcDeviceObject, NewState );

    OcObDereferenceObject( PtrOcDeviceObject );
}

//-------------------------------------------------

VOID
NTAPI
OcCrPnPFilterRepotNewDeviceRelations(
    IN PDEVICE_OBJECT          DeviceObject,
    IN DEVICE_RELATION_TYPE    RelationType,
    IN PDEVICE_RELATIONS       DeviceRelations OPTIONAL// may be NULL 
                                                       // or might be allocated
                                                       // from the paged pool
    )
{
    POC_DEVICE_OBJECT      PtrOcDeviceObject;
    ULONG                  SizeOfNewRelations;
    POC_RELATIONS_OBJECT   PtrNewRelationsObject = NULL;
    POC_RELATIONS_OBJECT   PtrOldRelationsObject = NULL;
    KIRQL                  OldIrql;
    NTSTATUS               RC;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    if( RelationType >= OC_STATIC_ARRAY_SIZE( PtrOcDeviceObject->DeviceRelations ) )
        return;

    //
    // find the device entry
    //
    PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                   (ULONG_PTR)DeviceObject,
                                                   OcObReferenceObject );
    ASSERT( NULL != PtrOcDeviceObject );
    if( NULL == PtrOcDeviceObject )
        return;

    //
    // convert the device object to PDO if possible
    //
    OcCrUpperDeviceToPdoIfPossibe( &PtrOcDeviceObject );

    if( NULL != DeviceRelations && 0x0 != DeviceRelations->Count )
        SizeOfNewRelations = FIELD_OFFSET( OC_DEVICE_RELATIONS, Objects ) + 
                             sizeof( PtrNewRelationsObject->Relations->Objects[0] ) * DeviceRelations->Count;
    else 
        SizeOfNewRelations = 0x0;

    //
    // do not exit in the case of an error, the old device relations object
    // must be freed in any case
    //
    if( 0x0 != SizeOfNewRelations ){

        //
        // create the new relations object
        //
        RC = OcObCreateObject( &Global.OcDeviceRelationsObjectType,
                               &PtrNewRelationsObject );
        if( NT_SUCCESS( RC ) ){

            RtlZeroMemory( PtrNewRelationsObject, sizeof( *PtrNewRelationsObject ) );
            PtrNewRelationsObject->Relations = ExAllocatePoolWithTag( NonPagedPool, 
                                                                      SizeOfNewRelations, 
                                                                      'eRcO' );
            //
            // fill in new relation with the device objects
            //
            if( NULL != PtrNewRelationsObject->Relations ){

                ULONG                i;
                POC_DEVICE_OBJECT*   PtrPtrDeviceObject;

                PtrPtrDeviceObject = &PtrNewRelationsObject->Relations->Objects[ 0x0 ];
                PtrNewRelationsObject->Relations->Count = DeviceRelations->Count;
                for( i = 0x0; i < DeviceRelations->Count; ++i ){

                    NTSTATUS    RC;

                    //
                    // self-defence
                    //
                    if( NULL == DeviceRelations->Objects[ i ] )
                        continue;

                    //
                    // create or find the PDO device object and insert
                    // it in the device relations, if there is the BusRelation then
                    // the PDO has been just created and now is being reported
                    // to the PnP manager and will be initialized later by the 
                    // PnP manager, it is illegal to query this PDO for 
                    // any PnP Manager's information
                    //
                    RC = OcCrProcessNewPnPDevice( DeviceRelations->Objects[ i ],
                                                  NULL,
                                                  OcDevicePnPTypePdo,
                                                  FALSE,
                                                  PtrPtrDeviceObject );
                    if( NT_SUCCESS( RC ) ){

                        POC_DEVICE_OBJECT    PtrOldParentObject;

                        //
                        // remember that the device object for DeviceRelations->Objects[ i ]
                        // depends from the PtrOcDeviceObject
                        //

                        //
                        // refernce the object which will be exchanged with the current
                        // paren object
                        //
                        OcObReferenceObject( PtrOcDeviceObject );

                        //
                        // exchange the parent objects
                        //
                        OcRwAcquireLockForWrite( &PtrOcDeviceObject->RwSpinLock, &OldIrql );
                        {// start of the lock
                            PtrOldParentObject = (*PtrPtrDeviceObject)->DependFrom[ RelationType ];
                            (*PtrPtrDeviceObject)->DependFrom[ RelationType ] = PtrOcDeviceObject;
                        }// end of the lock
                        OcRwReleaseWriteLock( &PtrOcDeviceObject->RwSpinLock, OldIrql );

                        //
                        // free the old parent object
                        //
                        if( NULL != PtrOldParentObject )
                            OcObDereferenceObject( PtrOldParentObject );
                        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrOldParentObject );

                        //
                        // the device has been created( found ), referenced and 
                        // now in the relations array, move pointer to the next 
                        // device in the array
                        //
                        PtrPtrDeviceObject = PtrPtrDeviceObject + 0x1;

                    } else {

                        //
                        // decrement the count, because the device object
                        // for the DeviceRelations->Objects[ i ] has not been
                        // created, but do not stop, may be I will be successful 
                        // with the other devices
                        //
                        PtrNewRelationsObject->Relations->Count -= 0x1;
                        ASSERT("!Error in OcCrPnPFilterRepotNewDeviceRelations while creating device!");
                    }
                }//for

            }//if( NULL != PtrNewRelationsObject->Relations )
        }
    }//if( 0x0 != SizeOfNewRelations )

    //
    // exchange the relations objects
    //
    OcRwAcquireLockForWrite( &PtrOcDeviceObject->RwSpinLock, &OldIrql );
    {// start of the lock
        PtrOldRelationsObject = PtrOcDeviceObject->DeviceRelations[ RelationType ];
        PtrOcDeviceObject->DeviceRelations[ RelationType ] = PtrNewRelationsObject;
    }// end of the lock
    OcRwReleaseWriteLock( &PtrOcDeviceObject->RwSpinLock, OldIrql );

    if( NULL != PtrOldRelationsObject )
        OcObDereferenceObject( PtrOldRelationsObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrOldRelationsObject );

    //
    // if the relations have changed the device types must also
    // be reconsidered for the newly arrived and old devices
    // TO DO - avoid multiple calling while the IRP is sent through the stack
    //
    OcCrRedefineTypesForDeviceGrowingFromRoot( PtrOcDeviceObject );

    //
    // now call the DlDriver's report device relations callback
    //
    {

        OC_DLDRVR_CONNECTION_HANDLE    ConnectionHandle;

        ConnectionHandle = OcReferenceDlDriverConnection();
        if( OC_INVALID_DLDRV_CONNECTION != ConnectionHandle ){

            POC_PNP_CALLBACKS   PnpCallbacks;

            PnpCallbacks = OcDlDrvConnectionHandleToPnpCalbacks( ConnectionHandle );

            PnpCallbacks->DeviceRelationsPostCallback( OcCrReturnPDO( PtrOcDeviceObject )->KernelDeviceObject,
                                                       PtrOcDeviceObject->KernelDeviceObject,
                                                       RelationType,
                                                       DeviceRelations );

            OcDereferenceDlDriverConnection( ConnectionHandle );
        }
    }

    OcObDereferenceObject( PtrOcDeviceObject );
}

//-------------------------------------------------

VOID
NTAPI
OcCrDeleteRelationsObject(
    IN POC_RELATIONS_OBJECT    PtrDeviceRelationsObject
    )
{
    ULONG i;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    if( NULL == PtrDeviceRelationsObject->Relations )
        return;

    for( i = 0x0; i < PtrDeviceRelationsObject->Relations->Count; ++i ){

        //
        // self-defence
        //
        if( NULL == PtrDeviceRelationsObject->Relations->Objects[ i ] )
            continue;

        OcObDereferenceObject( PtrDeviceRelationsObject->Relations->Objects[ i ] );
    }

    ExFreePoolWithTag( PtrDeviceRelationsObject->Relations, 'eRcO' );
}

//-------------------------------------------------

VOID
OcCrRemoveDeviceRelations(
    IN POC_DEVICE_OBJECT    PtrDeviceObject
    )
    /*
    this function acquires all necessary locks and leaves the object
    fields in a consistent state, but the use must be cautious - 
    a precaution which must be made by the caller is a guarantee
    that it will survive the non-determinism arising when this function
    reades the fields values while another function changes them as this
    function makes the check for NULL without acquiring any lock
    */
{
    ULONG                i;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    //
    // remove the next top-down PDO, this PDO is also a type of relation,
    // this is a PDO for the next PnP stack
    //
    if( NULL != PtrDeviceObject->NextTopDownTraversingPdo ){

        KIRQL                OldIrql;
        POC_DEVICE_OBJECT    NextTopDownTraversingPdo = NULL;

        OcRwAcquireLockForWrite( &PtrDeviceObject->RwTraversingSpinLock, &OldIrql );
        {// start of the lock

            NextTopDownTraversingPdo = PtrDeviceObject->NextTopDownTraversingPdo;
            PtrDeviceObject->NextTopDownTraversingPdo = NULL;

        }// end of the lock
        OcRwReleaseWriteLock( &PtrDeviceObject->RwTraversingSpinLock, OldIrql );

        if( NULL != NextTopDownTraversingPdo )
            OcObDereferenceObject( NextTopDownTraversingPdo );

        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( NextTopDownTraversingPdo );
    }

    //
    // I suppose there is no any contention
    //
    ASSERT( NULL == PtrDeviceObject->NextTopDownTraversingPdo );

    //
    // dereference devices from which this object depends, i.e.
    // parent devices
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( PtrDeviceObject->DependFrom ); ++i ){

        //
        // first check without lock
        //
        if( NULL != PtrDeviceObject->DependFrom[ i ] ){

            POC_DEVICE_OBJECT   PtrParentDeviceObject = NULL;
            KIRQL               OldIrql;

            OcRwAcquireLockForWrite( &PtrDeviceObject->RwSpinLock, &OldIrql );
            {// start of the lock
                PtrParentDeviceObject = PtrDeviceObject->DependFrom[ i ];
                PtrDeviceObject->DependFrom[ i ] = NULL;
            }// end of the lock
            OcRwReleaseWriteLock( &PtrDeviceObject->RwSpinLock, OldIrql );

            //
            // check for entering in this branch was made without
            // lock held, so the second check is needed
            //
            if( NULL != PtrParentDeviceObject )
                OcObDereferenceObject( PtrParentDeviceObject );
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrParentDeviceObject );
        }//if

        //
        // I suppose there is no any contention
        //
        ASSERT( NULL == PtrDeviceObject->DependFrom[ i ] );

    }//for

    //
    // remove devices related with this device
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( PtrDeviceObject->DeviceRelations ); ++i ){

        //
        // first check without lock
        //
        if( NULL != PtrDeviceObject->DeviceRelations[ i ] ){

            POC_RELATIONS_OBJECT   PtrOldRelationsObject = NULL;
            KIRQL                  OldIrql;

            OcRwAcquireLockForWrite( &PtrDeviceObject->RwSpinLock, &OldIrql );
            {// start of the lock
                PtrOldRelationsObject = PtrDeviceObject->DeviceRelations[ i ];
                PtrDeviceObject->DeviceRelations[ i ] = NULL;
            }// end of the lock
            OcRwReleaseWriteLock( &PtrDeviceObject->RwSpinLock, OldIrql );

            //
            // check PtrOldRelationsObject for NULL because 
            // the entering in this branch was made without
            // the lock held, so the second check is needed
            //
            if( NULL != PtrOldRelationsObject )
                OcObDereferenceObject( PtrOldRelationsObject );
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrOldRelationsObject );
        }//if

        //
        // I suppose there is no any contention
        //
        ASSERT( NULL == PtrDeviceObject->DeviceRelations[ i ] );

    }//for
}

//-------------------------------------------------

VOID
NTAPI
OcCrPnPFilterDeviceUsageNotificationPreOperationCallback(
    IN PDEVICE_OBJECT    DeviceObject,
    IN ULONG_PTR         RequstId,
    IN DEVICE_USAGE_NOTIFICATION_TYPE    Type,
    IN BOOLEAN           InPath,
    IN PVOID             Buffer OPTIONAL
    )
{

    POC_DEVICE_OBJECT    PtrOcDeviceObject;

    //
    // find the device entry
    //
    PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                   (ULONG_PTR)DeviceObject,
                                                   OcObReferenceObject );
    ASSERT( NULL != PtrOcDeviceObject );
    if( NULL == PtrOcDeviceObject )
        return;

    switch( Type ){

        case DeviceUsageTypePaging:

            PtrOcDeviceObject->DeviceUsage.DeviceUsageTypePaging = InPath? 0x1 : 0x0 ;
            if( PtrOcDeviceObject->Pdo )
                PtrOcDeviceObject->Pdo->DeviceUsage.DeviceUsageTypePaging = InPath? 0x1 : 0x0;

            break;

        case DeviceUsageTypeHibernation:

            PtrOcDeviceObject->DeviceUsage.DeviceUsageTypeHibernation = InPath? 0x1 : 0x0 ;
            if( PtrOcDeviceObject->Pdo )
                PtrOcDeviceObject->Pdo->DeviceUsage.DeviceUsageTypeHibernation = InPath? 0x1 : 0x0;

            break;

        case DeviceUsageTypeDumpFile:

            PtrOcDeviceObject->DeviceUsage.DeviceUsageTypeDumpFile = InPath? 0x1 : 0x0 ;
            if( PtrOcDeviceObject->Pdo )
                PtrOcDeviceObject->Pdo->DeviceUsage.DeviceUsageTypeDumpFile = InPath? 0x1 : 0x0;

            break;
    }

    OcObDereferenceObject( PtrOcDeviceObject );
}

//-------------------------------------------------

VOID
NTAPI
OcCrPnPFilterDeviceUsageNotificationPostOperationCallback(
    IN PDEVICE_OBJECT    DeviceObject,
    IN ULONG_PTR         RequstId,
    IN PIO_STATUS_BLOCK  StatusBlock
    )
{
    //
    // nothing to do
    //
}

//-------------------------------------------------

NTSTATUS
NTAPI
OcCrFreeDeviceNameInfoWR(
    IN ULONG_PTR    DeviceNameInfo//OcDeviceObject->DeviceNameInfo
    )
{
    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    OcCrFreeNameInformation( (POBJECT_NAME_INFORMATION)DeviceNameInfo );

    return STATUS_SUCCESS;
}

//-------------------------------------------------
VOID
NTAPI
OcCrDeleteDeviceObject(
    IN POC_DEVICE_OBJECT    PtrDeviceObject
    )
{
    //
    // PDO must be dereferenced and removed from the hash!
    //

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( NULL != Global.ThreadsPoolObject );

    //
    // I must free the device name information allocated from the 
    // Paged System Pool, so post the function in a worker thread,
    // If this postponing fails the memory will not be freed.
    //
    if( KeGetCurrentIrql() > APC_LEVEL && 
        NULL != PtrDeviceObject->DeviceNameInfo && 
        NULL != Global.ThreadsPoolObject ){

            POC_WORK_ITEM_LIST_OBJECT    PtrWorkItem;

            PtrWorkItem = OcTplReferenceSharedWorkItemList( Global.ThreadsPoolObject );
            ASSERT( NULL != PtrWorkItem );
            if( NULL != PtrWorkItem ){

                //
                // the memory for the object will be freed after return from 
                // OcCrDeleteDeviceObject so it can't be sent in a worker routine as
                // a parameter, one of the solution was when the objects manager
                // posts all objects deletion called at elevated IRQL to a worker thread,
                // but this was not done in order to simplify the object manager, because
                // most of the objects are tolerant to calling at elevated IRQL
                //
                OcWthPostWorkItemParam1( PtrWorkItem,
                                         FALSE,
                                         (Param1SysProc)OcCrFreeDeviceNameInfoWR,
                                         (ULONG_PTR)PtrDeviceObject->DeviceNameInfo
                                        );

                OcObDereferenceObject( PtrWorkItem );
            }

            //
            // set to NULL in any case, do not pay attention on errors
            //
            PtrDeviceObject->DeviceNameInfo = NULL;
    }

    //
    // check the IRQL, the postponing might have failed due to stopped threads
    //
    if( KeGetCurrentIrql() <= APC_LEVEL && 
        NULL != PtrDeviceObject->DeviceNameInfo ){

        OcCrFreeNameInformation( PtrDeviceObject->DeviceNameInfo );
    }

    ASSERT( NULL == PtrDeviceObject->Pdo );
    ASSERT( IsListEmpty( &PtrDeviceObject->DeviceRequests.CreateRequestsListHead ) );
    ASSERT( IsListEmpty( &PtrDeviceObject->DeviceRequests.IoRequestsListHead ) );

    if( NULL != PtrDeviceObject->DevicePropertyObject )
        OcObDereferenceObject( PtrDeviceObject->DevicePropertyObject );

    OcCrRemoveDeviceRelations( PtrDeviceObject );

    if( NULL != PtrDeviceObject->DeviceType )
        OcObDereferenceObject( PtrDeviceObject->DeviceType );
}

//-------------------------------------------------

VOID
NTAPI
OcCrCleanupDeviceAfterRemovingFromHash(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject
    )
    /*
    The caller must have at least one reference to PtrOcDeviceObject and must
    not close this reference himself! Usually this reference has been made
    before inserting the device in the hash.
    After returning from this function PtrOcDeviceObject may be invalid!
    This function is called only ONCE for each device object - the hash
    manager guarantees this, so there is no concurrent calling issue for 
    this function.
    */
{

    KIRQL    OldIrql;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( OcObGetObjectReferenceCount( PtrOcDeviceObject ) >= 0x1 );

    //
    // before removing the device from the list and hash 
    // wait until all critical processing on this device 
    // has ceased
    //
    if( NT_SUCCESS( OcRlAcquireRemoveLock( &PtrOcDeviceObject->RemoveLock.Common ) ) ){

        OcRlReleaseRemoveLockAndWait( &PtrOcDeviceObject->RemoveLock.Common );

    } else {

        ASSERT( !"A strange behavior is detected! Double call of OcCrCleanupDeviceAfterRemovingFromHash!" );
    }

    //
    // remove the device object from the global list
    //
    OcRwAcquireLockForWrite( &Global.DevObjListLock, &OldIrql );
    {// start of the lock
        if( !IsListEmpty( &PtrOcDeviceObject->ListEntry ) ){

            RemoveEntryList( &PtrOcDeviceObject->ListEntry );
            InitializeListHead( &PtrOcDeviceObject->ListEntry );
        }
    }// end of the lock
    OcRwReleaseWriteLock( &Global.DevObjListLock, OldIrql );

    //
    // release all related objects, the OcCrRemoveDeviceRelations
    // function is also called when the device is deleted because
    // of the reference count has dropped to zero, it is safe to
    // call this function here without fear of concurrent with the call
    // from deletion routine as the object is referenced here
    //
    OcCrRemoveDeviceRelations( PtrOcDeviceObject );

    //
    // remove all PDO's upper devices, remember that the objects have not been referenced
    // when was inserted in the list, so only remove them from the list and hash.
    //
    if( OcDevicePnPTypePdo == PtrOcDeviceObject->DevicePnPType || OcDeviceLowerNoPnPType == PtrOcDeviceObject->DevicePnPType ){

        PLIST_ENTRY    request;

        while( NULL != ( request = ExInterlockedRemoveHeadList( &PtrOcDeviceObject->UpperDevicesList.PdoHeadForListOfUpperDevices,
                                                                &PtrOcDeviceObject->PdoUpperDevicesListSpinLock ) ) ){
            POC_DEVICE_OBJECT    PtrOcUpperDevice;

            PtrOcUpperDevice = CONTAINING_RECORD( request,
                                                  OC_DEVICE_OBJECT,
                                                  UpperDevicesList.EntryForListOfUpperDevices );

            ASSERT( PtrOcUpperDevice != PtrOcDeviceObject );

            //
            // mark the upper device as removed from the list
            //
            InitializeListHead( &PtrOcUpperDevice->UpperDevicesList.EntryForListOfUpperDevices );

            //
            // remove the upper device entry from the hash,
            // we must done this here because 
            // device's driver might be unhooked
            // and we will never see IRP_MN_REMOVE request
            // for this upper device
            //
            OcHsRemoveContextByKeyValue( Global.PtrDeviceHashObject,
                                         (ULONG_PTR)PtrOcUpperDevice->KernelDeviceObject,
                                         OcCrCleanupDeviceAfterRemovingFromHash );
        }//while

    } else if( NULL != PtrOcDeviceObject->Pdo && 
               !IsListEmpty( &PtrOcDeviceObject->UpperDevicesList.EntryForListOfUpperDevices ) ){

        //
        // remove this object from the PDO's list
        //
        KeAcquireSpinLock( &PtrOcDeviceObject->Pdo->PdoUpperDevicesListSpinLock, &OldIrql );
        {// start of the lock
            ASSERT( !IsListEmpty( &PtrOcDeviceObject->UpperDevicesList.EntryForListOfUpperDevices ) );
            RemoveEntryList( &PtrOcDeviceObject->UpperDevicesList.EntryForListOfUpperDevices );
        }// end of the lock
        KeReleaseSpinLock( &PtrOcDeviceObject->Pdo->PdoUpperDevicesListSpinLock, OldIrql );

        //
        // mark the device as removed from the list
        //
        InitializeListHead( &PtrOcDeviceObject->UpperDevicesList.EntryForListOfUpperDevices );
    }

    ASSERT( (OcDevicePnPTypePdo == PtrOcDeviceObject->DevicePnPType || OcDeviceLowerNoPnPType == PtrOcDeviceObject->DevicePnPType)? 
                    (IsListEmpty( &PtrOcDeviceObject->UpperDevicesList.PdoHeadForListOfUpperDevices )): 
                    (IsListEmpty( &PtrOcDeviceObject->UpperDevicesList.EntryForListOfUpperDevices )));

    //
    // remove the PDO from the hash and 
    // dereference the PDO, if one exists
    //
    if( NULL != PtrOcDeviceObject->Pdo ){

        //
        // remove the PDO entry from the hash,
        // we must done this here because 
        // the PDO's driver might be unhooked
        // and we will never see IRP_MN_REMOVE request
        //
        OcHsRemoveContextByKeyValue( Global.PtrDeviceHashObject,
                                     (ULONG_PTR)PtrOcDeviceObject->Pdo->KernelDeviceObject,
                                     OcCrCleanupDeviceAfterRemovingFromHash );

        OcObDereferenceObject( PtrOcDeviceObject->Pdo );
        PtrOcDeviceObject->Pdo = NULL;
    }

    //
    // dereference the entry referenced before inserting in the hash
    //
    OcObDereferenceObject( PtrOcDeviceObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrOcDeviceObject );
}

//-------------------------------------------------

VOID
NTAPI
OcCrDeleteDeviceObjectType(
    IN POC_OBJECT_TYPE    PtrObjectType
    )
{
    KeSetEvent( &Global.OcDeviceObjectTypeUninitializationEvent,
                IO_DISK_INCREMENT,
                FALSE );
}

//-------------------------------------------------

static
NTSTATUS
OcCrCreateDevicePropertyObjectForPdo(
    IN PDEVICE_OBJECT    Pdo,
    OUT POC_DEVICE_PROPERTY_OBJECT    *PtrPtrDevicePropertyObject
    )
    /*
    the Pdo must be initialized by the PnP manager, i.e. the PDO must have been
    reported to the PnP manager and PnP Manager at least has started to build the 
    device stack( i.e. calling AddDevice )
    */
{
    NTSTATUS             RC;
    NTSTATUS             RC2;
    POC_OBJECT_TYPE      DevicePropertyObjectType = NULL;
    POC_DEVICE_PROPERTY_OBJECT    DevicePropertyObject = NULL;
    OC_EN_ENUMERATOR     EnumeratorIndex;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );

    //
    // first, get the device enumerator, if the PDO is cripple
    // an error will be returned and no other dangerous function
    // will be called
    //
    RC = OcCrGetDeviceEnumeratorIndex( Pdo,
                                       &EnumeratorIndex );
    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // only the full fledged PDO must survive the OcCrGetDeviceEnumeratorIndex calling
    //
    ASSERT( OcCrIsPdo( Pdo ) );

    if( en_USB == EnumeratorIndex ){

        //
        // USB
        //
        DevicePropertyObjectType = &Global.OcDevicePropertyUsbType;

    } else {

        //
        // unknown device type
        //
        DevicePropertyObjectType = &Global.OcDevicePropertyCommonType;
    }

    //
    // create the property object, the body will be 
    // set to zero in OcObCreateObject, because
    // the OcObjectTypeZeroObjectBody flag has been
    // set
    //
    ASSERT( OcIsFlagOn( DevicePropertyObjectType->Flags, OcObjectTypeZeroObjectBody ) );
    RC = OcObCreateObject( DevicePropertyObjectType,
                           &DevicePropertyObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // initialize the header
    //
    DevicePropertyObject->Header.Enumerator = EnumeratorIndex;

    //
    // get the class name, do not exit on errors,
    // this field is needed only for the dump
    //
    RC2 = OcCrGetDeviceProperty( Pdo,
                                 DevicePropertyClassName,
                                 &DevicePropertyObject->Header.DevicePropertyClassName,
                                 NULL,
                                 NULL );
    if( !NT_SUCCESS( RC2 ) )
        DevicePropertyObject->Header.DevicePropertyClassName = NULL;

    //
    // get the device description, do not exit on errors,
    // this field is needed only for the dump
    //
    RC2 = OcCrGetDeviceProperty( Pdo,
                                 DevicePropertyDeviceDescription,
                                 &DevicePropertyObject->Header.DevicePropertyDeviceDescription,
                                 NULL,
                                 NULL );
    if( !NT_SUCCESS( RC2 ) )
        DevicePropertyObject->Header.DevicePropertyDeviceDescription = NULL;

    //
    // get the device's key name, do not exit on errors,
    // this field is needed only for the dump
    //
    RC2 = OcCrGetDeviceProperty( Pdo,
                                 DevicePropertyDriverKeyName,
                                 &DevicePropertyObject->Header.DevicePropertyDriverKeyName,
                                 NULL,
                                 NULL );
    if( !NT_SUCCESS( RC2 ) )
        DevicePropertyObject->Header.DevicePropertyDriverKeyName = NULL;

    //
    // get the name of the enumerator for the device, 
    // do not exit on errors, this field is needed only for the dump
    //
    RC2 = OcCrGetDeviceProperty( Pdo,
                                 DevicePropertyEnumeratorName,
                                 &DevicePropertyObject->Header.DevicePropertyEnumeratorName,
                                 NULL,
                                 NULL );
    if( !NT_SUCCESS( RC2 ) )
        DevicePropertyObject->Header.DevicePropertyEnumeratorName = NULL;

    //
    // get the device's PnP registry key
    //
    RC2 = OcCrGetDeviceRegistryKeyString( Pdo,
                                          &DevicePropertyObject->Header.DevicePropertyPnpRegistryString );
    if( !NT_SUCCESS( RC2 ) )
        DevicePropertyObject->Header.DevicePropertyPnpRegistryString = NULL;

    //
    // get the device's PDO name, do not exit on errors,
    // this field is needed only for the dump and creating 
    // file object's names
    //
    RC2 = OcCrGetDeviceProperty( Pdo,
                                 DevicePropertyPhysicalDeviceObjectName,
                                 &DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer,
                                 NULL,
                                 NULL );
    if( !NT_SUCCESS( RC2 ) ){

        DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer = NULL;

    } else {

        UNICODE_STRING    DeviceName;

        DeviceName.Buffer = DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer;
        DeviceName.Length = (USHORT)wcslen( DeviceName.Buffer )*sizeof( WCHAR );
        DeviceName.MaximumLength = DeviceName.Length;

        DevicePropertyObject->Header.DevicePropertyPhysicalDeviceObjectName = DeviceName;
    }

    //
    // get the device's the GUID for the device's setup class, do not exit on error
    //
    RC2 = OcCrGetDeviceProperty( Pdo,
                                 DevicePropertyClassGuid,
                                 &DevicePropertyObject->Header.DevicePropertyClassGuid,
                                 NULL,
                                 NULL );
    if( NT_SUCCESS( RC2 ) ){

        UNICODE_STRING    SetupGuidString;

        RtlInitUnicodeString( &SetupGuidString, DevicePropertyObject->Header.DevicePropertyClassGuid );

        //
        // convert the string to the enum
        //
        DevicePropertyObject->Header.SetupClassGuidIndex = OcCrGetDeviceSetupClassGuidIndex( &SetupGuidString );

    } else
        DevicePropertyObject->Header.DevicePropertyClassGuid = NULL;


    //
    // initialize the device specific fields
    //
    if( en_USB == EnumeratorIndex ){

        //
        // USB
        //

        POC_DEVICE_PROPERTY_USB_OBJECT    UsbDevicePropetryObject;

        DevicePropertyObject->Header.PropertyType.USB = 0x1;

        //
        // cast the common object to a USB object
        //
        UsbDevicePropetryObject = (POC_DEVICE_PROPERTY_USB_OBJECT)DevicePropertyObject;

        RC = OcCrInitializeUsbDeviceDescriptor( Pdo,
                                                &UsbDevicePropetryObject->UsbDescriptor );
        ASSERT( NT_SUCCESS( RC ) );

    } else {

        //
        // an unknown device type
        //
        DevicePropertyObject->Header.PropertyType.Common = 0x1;
    }

__exit:

    if( NT_SUCCESS( RC ) ){

        *PtrPtrDevicePropertyObject = DevicePropertyObject;

    } else {

        //
        // free the device property object
        //
        if( DevicePropertyObjectType )
            OcObDereferenceObject( DevicePropertyObjectType );

    }

    return RC;
}

//-------------------------------------------------

NTSTATUS
NTAPI
OcCrCreateDevicePropertyObjectForPdoWR(
    IN ULONG_PTR    Pdo,//PDEVICE_OBJECT
    OUT ULONG_PTR   PtrPtrDevicePropertyObject,//POC_DEVICE_PROPERTY_OBJECT*
    IN ULONG_PTR    Event,//PKEVENT
    OUT ULONG_PTR   PtrReturnedCode// NTSTATUS*
    )
{
    NTSTATUS    RC;

    RC = OcCrCreateDevicePropertyObjectForPdo( (PDEVICE_OBJECT)Pdo,
                                               (POC_DEVICE_PROPERTY_OBJECT*)PtrPtrDevicePropertyObject );

    *(NTSTATUS*)PtrReturnedCode = RC;

    KeSetEvent( (PKEVENT)Event, IO_DISK_INCREMENT, FALSE );

    return RC;
}

//-------------------------------------------------

static
NTSTATUS
OcCrCreateDevicePropertyObjectForNonPnPDevice(
    IN PDEVICE_OBJECT    LowerDevice,
    OUT POC_DEVICE_PROPERTY_OBJECT    *PtrPtrDevicePropertyObject
    )
{
    NTSTATUS             RC;
    POC_OBJECT_TYPE      DevicePropertyObjectType = NULL;
    POC_DEVICE_PROPERTY_OBJECT    DevicePropertyObject = NULL;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    DevicePropertyObjectType = &Global.OcDevicePropertyCommonType;

    //
    // create the property object, the body will be 
    // set to zero in OcObCreateObject, because
    // the OcObjectTypeZeroObjectBody flag has been
    // set
    //
    ASSERT( OcIsFlagOn( DevicePropertyObjectType->Flags, OcObjectTypeZeroObjectBody ) );
    RC = OcObCreateObject( DevicePropertyObjectType,
                           &DevicePropertyObject );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    DevicePropertyObject->Header.BusGuidIndex = en_GUID_DL_BUS_TYPE_UNKNOWN;
    DevicePropertyObject->Header.Enumerator = en_OC_UNKNOWN_ENUM;
    DevicePropertyObject->Header.SetupClassGuidIndex = en_OC_GUID_DEVCLASS_UNKNOWN;

    *PtrPtrDevicePropertyObject = DevicePropertyObject;

__exit:
    return RC;
}

//-------------------------------------------------

VOID
NTAPI
OcCrFreeDevicePropertyObject(
    IN POC_OBJECT_BODY    Object
    )
{
    POC_DEVICE_PROPERTY_OBJECT    DevPropObject = (POC_DEVICE_PROPERTY_OBJECT)Object;

    if( NULL != DevPropObject->Header.DevicePropertyClassName )
        OcCrFreeDevicePropertyBuffer( DevPropObject->Header.DevicePropertyClassName );

    if( NULL != DevPropObject->Header.DevicePropertyDeviceDescription )
        OcCrFreeDevicePropertyBuffer( DevPropObject->Header.DevicePropertyDeviceDescription );

    if( NULL != DevPropObject->Header.DevicePropertyDriverKeyName )
        OcCrFreeDevicePropertyBuffer( DevPropObject->Header.DevicePropertyDriverKeyName );

    if( NULL != DevPropObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer )
        OcCrFreeDevicePropertyBuffer( DevPropObject->Header.DevicePropertyPhysicalDeviceObjectName.Buffer );

    if( NULL != DevPropObject->Header.DevicePropertyClassGuid )
        OcCrFreeDevicePropertyBuffer( DevPropObject->Header.DevicePropertyClassGuid );

    if( NULL != DevPropObject->Header.DevicePropertyEnumeratorName )
        OcCrFreeDevicePropertyBuffer( DevPropObject->Header.DevicePropertyEnumeratorName );

    if( NULL != DevPropObject->Header.DevicePropertyPnpRegistryString )
        OcCrFreeDeviceRegistryKeyString( DevPropObject->Header.DevicePropertyPnpRegistryString );

    if( 0x1 == DevPropObject->Header.PropertyType.USB ){

        //
        // this is a USB property object
        //
        POC_DEVICE_PROPERTY_USB_OBJECT    PtrUsbDevPropObj = (POC_DEVICE_PROPERTY_USB_OBJECT)DevPropObject;

        if( NULL != PtrUsbDevPropObj->UsbDescriptor.CompatibleIds )
            OcCrFreeCompatibleIdsStringForDevice( PtrUsbDevPropObj->UsbDescriptor.CompatibleIds );
    }
}

//-------------------------------------------------

POC_DEVICE_OBJECT
OcCrGetLowerPdoDevice(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject,
    IN OC_EN_ENUMERATOR*    EnumVectorToFind OPTIONAL,
    IN ULONG    VectorSize
    )
    /*
    The caller must reference the PtrOcDeviceObject
    object because this guarantee that the object and 
    all objects it points to will be alive, if EnumVectorToFind
    is NULL then the lowest found PDO is returned( may be the same
    as PtrOcDeviceObject), else the lower PDO with the EnumeratorToFind
    Enumerator is returned( or NULL if not found, can't be PtrOcDeviceObject or
    PtrOcDeviceObject->Pdo because this guarantee the advancing when the 
    several succeed calls are made ).
    The returned object is referenced, the caller must 
    dereference it when it is no longer needed.
    */
{
    POC_DEVICE_OBJECT             PtrLowerOcPdo;
    BOOLEAN                       FindLowest;
    BOOLEAN                       RequestedEnumFound = FALSE;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    FindLowest = ( NULL == EnumVectorToFind );

    //
    // if the lowest PDO must be found then whatever I'll
    // find is good
    //
    RequestedEnumFound = ( TRUE == FindLowest );

    //
    // get the PDO
    //
    PtrLowerOcPdo = OcCrReturnPDO( PtrOcDeviceObject );

    //
    // reference the object before starting the loop,
    // this is needed because objects inside the loop
    // are referenced and if the object is referenced
    // before the loop this gives us the simple loop
    // semantic and invariant - a referenced non NULL 
    // object before and after the loop
    //
    OcObReferenceObject( PtrLowerOcPdo );

    while( TRUE ){

        //
        // first check the parent using removal relations,
        // then using the bus relations
        //
        DEVICE_RELATION_TYPE    RelationsToCheck[] = { RemovalRelations,
                                                       BusRelations };
        ULONG                   i;
        POC_DEVICE_OBJECT       PtrParentOcPdo = NULL;

        ASSERT( OcDevicePnPTypePdo == PtrLowerOcPdo->DevicePnPType || OcDeviceLowerNoPnPType == PtrLowerOcPdo->DevicePnPType );

        for( i = 0x0; i<OC_STATIC_ARRAY_SIZE(RelationsToCheck); ++i ){

            KIRQL                   OldIrql;
            DEVICE_RELATION_TYPE    RelationsType = RelationsToCheck[ i ];

            //
            // first check is done without the helded lock
            //
            if( NULL == PtrLowerOcPdo->DependFrom[ RelationsType ] )
                continue;

            OcRwAcquireLockForRead( &PtrLowerOcPdo->RwSpinLock, &OldIrql );
            {// start of the lock
                PtrParentOcPdo = PtrLowerOcPdo->DependFrom[ RelationsType ];
                if( NULL != PtrParentOcPdo )
                    OcObReferenceObject( PtrParentOcPdo );
            }// end of the lock
            OcRwReleaseReadLock( &PtrLowerOcPdo->RwSpinLock, OldIrql );

            if( NULL != PtrParentOcPdo )
                break;
        }//for

        if( NULL == PtrParentOcPdo ){

            //
            // so, the last found PtrLowerOcPdo is 
            // the lowest PDO that can be achieved 
            // in our PnP model
            //
            break;
        }

        //
        // dereference the previous PDO which
        // has been supposed as the lowest one
        // and save the new PDO
        //
        OcObDereferenceObject( PtrLowerOcPdo );
        PtrLowerOcPdo = PtrParentOcPdo;

        ASSERT( NULL != PtrLowerOcPdo->DevicePropertyObject );

        //
        // compare the enumerator with the sought enumerators from the vector
        //
        if( FALSE == FindLowest ){

                ULONG    i;

                for( i = 0x0; i< VectorSize; ++i ){

                    ASSERT( en_OC_GET_NEXT_PARENT != PtrLowerOcPdo->Enumerator );

                    //
                    // the en_OC_GET_NEXT_PARENT value has a special
                    // treatment, the loop is broken and the
                    // found parent is returned, usually this value is
                    // used to retrieve the next nearest predecessor
                    //
                    if( EnumVectorToFind[ i ] != PtrLowerOcPdo->Enumerator && 
                        EnumVectorToFind[ i ] != en_OC_GET_NEXT_PARENT ){
                        continue;
                    }//if( EnumVectorToFind[ i ] != ...

                    RequestedEnumFound = TRUE;
                    break;
                }//for( i = 0x0; i< EnumVectorToFind; ++i )

                if( TRUE == RequestedEnumFound )
                    break;
        }//if( FALSE == FindLowest && ...

    }//while( TRUE ) 

    //
    // the semantic is- the object before
    // the loop and after the loop are referenced
    //
    ASSERT( PtrLowerOcPdo );

    if( FALSE == RequestedEnumFound ){

        ASSERT( FALSE == FindLowest );

        OcObDereferenceObject( PtrLowerOcPdo );
        PtrLowerOcPdo = NULL;
    }//if( FALSE == RequestedEnumFound )

    return PtrLowerOcPdo;
}

//-------------------------------------------------

BOOLEAN
OcCrIsEligibleParentFound(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject
    )
{
    //
    // the vector contains the enumerators
    // that are considered as eligible to be
    // parent device enumerators, the 
    // parent device is a device that
    // gives enough information about a
    // physical or logical device
    //
    OC_EN_ENUMERATOR    EnumeratorsVector[] = { en_PCI, 
                                                en_PCIIDE,
                                                en_USB,
                                                en_1394,
                                                en_V1394 };

    POC_DEVICE_OBJECT    ParentDevice;

    ParentDevice = OcCrGetLowerPdoDevice( PtrOcDeviceObject,
                                          EnumeratorsVector,
                                          OC_STATIC_ARRAY_SIZE( EnumeratorsVector ) );
    if( NULL != ParentDevice ){

        OcObDereferenceObject( ParentDevice );
        OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ParentDevice );
        return TRUE;
    }
    return FALSE;
}

//-------------------------------------------------

NTSTATUS
OcCrFindParentDeviceWR(
    IN ULONG_PTR    PtrOcDeviceObject OPTIONAL
    )
    /*
    The caller must reference the PtrOcDeviceObject which
    will be dereferenced in this function.
    If PtrOcDeviceObject is NULL then the function
    traverses all started devices and queries them
    for child.
    */
{
    KIRQL                OldIrql;
    PDEVICE_OBJECT       SysPdo = NULL;
    NTSTATUS             RC;
    POC_DEVICE_OBJECT    OcDevObjToCheckAndDeref = (POC_DEVICE_OBJECT)PtrOcDeviceObject;

    if( (ULONG_PTR)NULL == PtrOcDeviceObject )
        goto __traverse_list;

    //
    // work only with PDO
    //
    OcCrUpperDeviceToPdoIfPossibe( &OcDevObjToCheckAndDeref );
    if( OcDevicePnPTypePdo != OcDevObjToCheckAndDeref->DevicePnPType && 
        OcDeviceLowerNoPnPType != OcDevObjToCheckAndDeref->DevicePnPType ){
        //
        // nothing to do with this cripple device w/o PDO
        //
        ASSERT( !"Invalid device type for sending a PnP request or making any decision about it" );
        RC = STATUS_INVALID_DEVICE_REQUEST;
        goto __exit;
    }

    //
    // from this point the OcDevObjToCheckAndDeref points to referenced PDO
    //
    ASSERT( OcDevicePnPTypePdo == OcDevObjToCheckAndDeref->DevicePnPType || 
            OcDeviceLowerNoPnPType == OcDevObjToCheckAndDeref->DevicePnPType );

    //
    // the current design must call this function only once for each device
    // stack with an initialized PDO ( if the stack has been built the PDO 
    // is initialized, i.e. its DeviceNode is initialized but this doesn't
    // mean that PDO has been started! )
    //
    ASSERT( 0x0 == OcDevObjToCheckAndDeref->Flags.ParentChecked );

    if( 0x1 == OcDevObjToCheckAndDeref->Flags.ParentChecked ){

        RC = STATUS_SUCCESS;
        goto __exit;

    } else {

        //
        // mark as checked for the parent
        //
        OcDevObjToCheckAndDeref->Flags.ParentChecked = 0x1;
    }

    //
    // if there is a non PnP device then do not process it on a
    // way used for PnP devices. For instance this might be a mounted FSD's
    // device object which has been found when a PnP request has been
    // processed on the volume( i.e. RawMountVolume calls FsRtlNotifyVolumeEventEx
    // which sends PnP request to get a Pdo ), so sending any PnP request might 
    // result in a dead lock because this non PnP device's Event has not yet
    // been set in a signal state and a volume's stack is blocked by 
    // a current PnP request
    //
    if( OcDeviceLowerNoPnPType == OcDevObjToCheckAndDeref->DevicePnPType ){

        //
        // nothing to do except setting an event in a signal state
        //
        RC = STATUS_SUCCESS;
        goto __exit;
    }

    //
    // get the system object associated with the OC object
    //
    SysPdo = OcDevObjToCheckAndDeref->KernelDeviceObject;
    ASSERT( NULL != SysPdo );

    //
    // Keep sending the query requests until the parent is found
    //
__traverse_list:
    OcRwAcquireLockForRead( &Global.DevObjListLock, &OldIrql );
    {// start of the read lock

        PLIST_ENTRY    request;
        BOOLEAN        ParentFound = FALSE;

        //
        // go from this device to the start of the list, i.e. from the least recently
        // added devices, they are the most likely parents of the new device
        //
        for( (NULL != OcDevObjToCheckAndDeref)? (request = OcDevObjToCheckAndDeref->ListEntry.Blink) : (request = Global.DevObjListHead.Blink) ; 
             request != &Global.DevObjListHead; 
             request = request->Blink ){

            POC_DEVICE_OBJECT    PtrOcListDeviceObject;
            PDEVICE_OBJECT       TopStackDevice;

            PtrOcListDeviceObject = CONTAINING_RECORD( request, OC_DEVICE_OBJECT, ListEntry );

            //
            // lock the device from removing from the list and the hash and 
            // receiving IRP_MN_REMOVE request
            //
            RC = OcRlAcquireRemoveLock( &PtrOcListDeviceObject->RemoveLock.Common );
            if( !NT_SUCCESS( RC ) ){

                //
                // the device is being removed or in an unknown state for 
                // to be used( i.e. cripple ), do not send any PnP request to it
                //
                continue;
            } else if( 0x1 == PtrOcListDeviceObject->Flags.CripplePdo ){

                //
                // the device is being removed or in an unknown state for 
                // to be used( i.e. cripple ), do not send any PnP request to it
                //

                //
                // release the acquired lock
                //
                OcRlReleaseRemoveLock( &PtrOcListDeviceObject->RemoveLock.Common );
                continue;
            }

            //
            // reference the device object
            //
            OcObReferenceObject( PtrOcListDeviceObject );

            //
            // release the lock, because the entry is refernced and 
            // locked and can't be removed from the global list and 
            // the underlying system's device object won't receive 
            // the IRP_MN_REMOVE request
            // TO DO May be a good idea to lock the underlying PDO!
            //
            OcRwReleaseReadLock( &Global.DevObjListLock, OldIrql );

            //
            // Get the top device in the stack. There is possible BSOD if the 
            // stack is being torn down and the upper DO has transited in
            // the remove state and will fall when it receives the PnP request.
            // I block from tear down only the stack under the PtrOcListDeviceObject->KernelDeviceObject.
            // Actually, devices must complete all request when they are in 
            // a removed state, but some buggy driver might forgot about this.
            //
            TopStackDevice = Global.SystemFunctions.IoGetAttachedDeviceReference( PtrOcListDeviceObject->KernelDeviceObject );

            //
            // Send the device relations requests,
            // Send request only to the started devices!
            // Also, send request only to the PDO, this reduces
            // the system overhead and excludes non PnP devices
            // 
            if( Started == PtrOcListDeviceObject->PnPState && 
                OcDevicePnPTypePdo == PtrOcListDeviceObject->DevicePnPType ){

                PDEVICE_RELATIONS    ReferencedDeviceRelations;

                ASSERT( OcDevicePnPTypePdo == PtrOcListDeviceObject->DevicePnPType );
                ASSERT( IO_TYPE_DEVICE == PtrOcListDeviceObject->KernelDeviceObject->Type );
                ASSERT( IO_TYPE_DRIVER == PtrOcListDeviceObject->KernelDeviceObject->DriverObject->Type );

                RC = OcQueryDeviceRelations( TopStackDevice,
                                             RemovalRelations,
                                             &ReferencedDeviceRelations );
                if( NT_SUCCESS( RC ) && NULL != ReferencedDeviceRelations ){

                    ULONG    i;

                    //
                    // Check the response
                    // Is the sought SysPdo device present?
                    //
                    for( i = 0x0; i<ReferencedDeviceRelations->Count; ++i ){

                        //
                        // self-defence
                        //
                        if( NULL == ReferencedDeviceRelations->Objects[ i ] )
                            continue;

                        if( ReferencedDeviceRelations->Objects[ i ] == SysPdo || 
                            NULL == SysPdo ){

                            //
                            // save the found relations, in many cases they have been saved 
                            // using the PnP filter callback, but who knows.
                            //
                            OcCrPnPFilterRepotNewDeviceRelations( PtrOcListDeviceObject->KernelDeviceObject,
                                                                  RemovalRelations,
                                                                  ReferencedDeviceRelations );

                            if( NULL != SysPdo ){

                                ParentFound = TRUE;
                                break;// break the for loop
                            }
                        }//if( ReferencedDeviceRelations->Objects[ i ] == SysPdo )
                    }//for( i = 0x0; i<ReferencedDeviceRelations->Count; ++i )

                    OcDereferenceDevicesAndFreeDeviceRelationsMemory( ReferencedDeviceRelations );

                }//if( NT_SUCCESS( RC ) && NULL != ReferencedDeviceRelations )
            }

            ObDereferenceObject( TopStackDevice );

            OcObDereferenceObject( PtrOcListDeviceObject );

            //
            // reacquire the lock, then release entry's remove lock
            //
            OcRwAcquireLockForRead( &Global.DevObjListLock, &OldIrql );
            OcRlReleaseRemoveLock( &PtrOcListDeviceObject->RemoveLock.Common );
            if( TRUE == ParentFound )
                break;
        }//for

    }// end of the read lock
    OcRwReleaseReadLock( &Global.DevObjListLock, OldIrql );

    //
    // assume that all has been successful if we reach this point
    //
    RC = STATUS_SUCCESS;

__exit:

    //
    // the caller references the device object for us
    //
    if( NULL != OcDevObjToCheckAndDeref ){

        //
        // in any case set event in a signal state, I have done all
        // what I can do to find parent device
        //
        OcCrDeviceStackBuildCompleted( OcDevObjToCheckAndDeref );

        OcObDereferenceObject( OcDevObjToCheckAndDeref );

    }

    return RC;
}

//-------------------------------------------------

VOID
OcCrCheckParentDevice(
    IN POC_DEVICE_OBJECT    PtrOcDeviceObject OPTIONAL
    )
    /*
    if PtrOcDeviceObject is NULL then the function
    traverses all started devices and queries them
    for child
    */
{
    POC_WORK_ITEM_LIST_OBJECT    PtrWorkItem;
    BOOLEAN                      PostingFailed = FALSE;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // if this function is called when the PnP filter
    // is reporting about devices then do not process
    // the device, this floods the system with
    // requests, all devices will be processed later
    // by calling OcCrCheckParentDevice( NULL )
    //
    if( PsGetCurrentThread() == Global.ThreadCallingReportDevice )
        return;

    if( NULL != PtrOcDeviceObject && 
        TRUE == OcCrIsEligibleParentFound( PtrOcDeviceObject ) ){

        OcCrDeviceStackBuildCompleted( PtrOcDeviceObject );
        return;
    }

    //
    // now I use the thread pool with the shared queue, might
    // be in the future it will be replaced by a single
    // dedicated thread due to a dead lock
    //
    PtrWorkItem = OcTplReferenceSharedWorkItemList( Global.ThreadsPoolObject );
    ASSERT( NULL != PtrWorkItem );
    if( NULL != PtrWorkItem ){

        //
        // post the request in the worker thread,
        // reference the object before sending it,
        // the worker routine must dereference it
        //
        if( NULL != PtrOcDeviceObject )
            OcObReferenceObject( PtrOcDeviceObject );

        if( !NT_SUCCESS( OcWthPostWorkItemParam1( PtrWorkItem,
                                                  FALSE,
                                                  OcCrFindParentDeviceWR,
                                                  (ULONG_PTR)PtrOcDeviceObject ) ) ){

                ASSERT( !"Something went wrong with the worker threads!" );

                //
                // posting the request in the worker thread failed
                //
                if( NULL != PtrOcDeviceObject )
                    OcObDereferenceObject( PtrOcDeviceObject );

                PostingFailed = TRUE;
        }//if

        OcObDereferenceObject( PtrWorkItem );

    } else {//else for  if( NULL != PtrWorkItem )

        PostingFailed = TRUE;
    }

    if( PostingFailed && NULL != PtrOcDeviceObject ){

        //
        // something went wrong with the pool threads
        // consider the stack as built to allow the system to proceed
        //
        OcCrDeviceStackBuildCompleted( PtrOcDeviceObject);
    }
}

//-------------------------------------------------

VOID
OcCrProcessReportedDevices()
{
    OcCrCheckParentDevice( NULL );
}

//-------------------------------------------------

VOID
OcCrTraverseTopDown(
    IN POC_DEVICE_OBJECT    PtrOcTopDeviceObject,
    IN PtrOcCrNodeFunction  OcCrNodeFunction,
    IN PVOID                Context
    )
    /*
    the function calls OcCrNodeFunction for PtrOcTopDeviceObject and 
    all PDOs in the underlying device stacks, including the device's PDO
    */
{
    POC_DEVICE_OBJECT   CurrentPdoDevice;

    //
    // convert the PtrOcTopDeviceObject device to PDO
    //
    CurrentPdoDevice = OcCrReturnPDO( PtrOcTopDeviceObject );

    //
    // the first call is for the device itself if it is not PDO device
    // else the function for PDO will be called at the below while loop
    //
    if( CurrentPdoDevice != PtrOcTopDeviceObject && 
        FALSE == OcCrNodeFunction( PtrOcTopDeviceObject, Context ) )
        return;

    ASSERT( OcDevicePnPTypePdo == CurrentPdoDevice->DevicePnPType || 
            OcDeviceLowerNoPnPType == CurrentPdoDevice->DevicePnPType );

    //
    // the semantic is - CurrentPdoDevice is referenced before
    // and after the loop, this is very important because the
    // loop is broken with retained reference to the object
    //
    OcObReferenceObject( CurrentPdoDevice );
    {
        //
        // go through all PDO devices in the stack 
        // and call the context function
        // TO DO I tnink this is a good place for 
        // optimization, for example all lower
        // PDOs may be saved in the node!
        //
        while( TRUE ){

            KIRQL               OldIrql;
            POC_DEVICE_OBJECT   ParentPdoDevice;

            //
            // if the function returns FALSE then break 
            // the traversing
            //
            if( FALSE == OcCrNodeFunction( CurrentPdoDevice, Context ) )
                break;// exit the while loop with the referenced object!

            //
            // after calling for the the first driver( upper driver )
            // in the chain the flag should be cleaned
            //
            ASSERT( 0x0 == ((POC_NODE_CTX)Context)->RequestCurrentParameters.UpperDevice );

            //
            // get the next cashed parent PDO
            //
            OcRwAcquireLockForRead( &CurrentPdoDevice->RwTraversingSpinLock, &OldIrql );
            { // start of the read lock
                ParentPdoDevice = CurrentPdoDevice->NextTopDownTraversingPdo;
                if( NULL != ParentPdoDevice )
                    OcObReferenceObject( ParentPdoDevice );
            } // end of the read lock
            OcRwReleaseReadLock( &CurrentPdoDevice->RwTraversingSpinLock, OldIrql );

            if( NULL == ParentPdoDevice ){

                KIRQL               OldIrql;
                BOOLEAN             DereferenceLowerPdo = TRUE;
                POC_DEVICE_OBJECT   OldNextTopDownTraversingPdo = NULL;
                OC_EN_ENUMERATOR    EnumeratorsVector[] = { en_OC_GET_NEXT_PARENT };

                //
                // retrieve the next parent PDO
                //
                ParentPdoDevice = OcCrGetLowerPdoDevice( CurrentPdoDevice,
                                                         EnumeratorsVector,
                                                         OC_STATIC_ARRAY_SIZE( EnumeratorsVector ) );
                if( NULL == ParentPdoDevice )
                    break;// exit the while loop with the referenced object!

                //
                // reference the object before acquiring the lock, this reduces
                // the time spent while holding the lock to save the object pointer
                //
                OcObReferenceObject( ParentPdoDevice );
                ASSERT( TRUE == DereferenceLowerPdo );

                OcRwAcquireLockForWrite( &CurrentPdoDevice->RwTraversingSpinLock, &OldIrql );
                {// start of the write lock

                    //
                    // remember the last found top-down PDO to speed up the future traversing
                    //
                    if( ParentPdoDevice != CurrentPdoDevice->NextTopDownTraversingPdo ){

                        OldNextTopDownTraversingPdo = CurrentPdoDevice->NextTopDownTraversingPdo;
                        CurrentPdoDevice->NextTopDownTraversingPdo = ParentPdoDevice;

                        DereferenceLowerPdo = FALSE;

                    }//if( ParentPdoDevice != CurrentPdoDevice->NextTopDownTraversingPdo )

                }// end of the write lock
                OcRwReleaseWriteLock( &CurrentPdoDevice->RwTraversingSpinLock, OldIrql );

                ASSERT( OC_IS_BOOLEAN( DereferenceLowerPdo ) );

                //
                // derefrence the old next top-down traversing device
                // after releasing the lock to reduce the time spent 
                // while the lock is held
                //
                if( NULL != OldNextTopDownTraversingPdo )
                    OcObDereferenceObject( OldNextTopDownTraversingPdo );

                OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( OldNextTopDownTraversingPdo );

                //
                // dereference the object if it has not been assigned
                //
                if( TRUE == DereferenceLowerPdo )
                    OcObDereferenceObject( ParentPdoDevice );

            } // if( NULL == ParentPdoDevice )

            OcObDereferenceObject( CurrentPdoDevice );
            CurrentPdoDevice = ParentPdoDevice;

            ASSERT( OcDevicePnPTypePdo == ParentPdoDevice->DevicePnPType || 
                    OcDeviceLowerNoPnPType == ParentPdoDevice->DevicePnPType );
        }//while
    }
    OcObDereferenceObject( CurrentPdoDevice );// dereference the object referenced either in the loop or before
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( CurrentPdoDevice );
}

//-------------------------------------------------

__forceinline
NTSTATUS
OcCrTuneDeviceStackForPushing(
    __inout POC_DEVICE_OBJECT**    PtrDeviceStack,
    __inout ULONG*    PtrSizeOfStack,// in bytes
    __in ULONG    StackPointerInx,// index of the last valid entry in the *PtrDeviceStack array or (ULONG)(-1)
    __in ULONG    StackGrowValueBytes,// the minimal number of bytes to which stack should be grown
    __in ULONG    MemoryTag
    )
    /*
    this function grows a stack used for the PnP tree traversing at StackGrowValueByte bytes,
    the caller must free the returned stack by calling ExFreePoolWithTag( .., MemoryTag )
    */
{

    POC_DEVICE_OBJECT*    DeviceStack = *PtrDeviceStack;
    ULONG                 SizeOfStack = *PtrSizeOfStack;// size in bytes
    ULONG                 NewStackSize;
    POC_DEVICE_OBJECT*    NewDeviceStack;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    if( 0x0 == StackGrowValueBytes )
        return STATUS_SUCCESS;

    ASSERT( StackGrowValueBytes > 0x0 && 
        StackGrowValueBytes < 0xFFFF && 
        (SizeOfStack%sizeof( DeviceStack[0x0] )) == 0x0 &&
        (StackGrowValueBytes%sizeof( DeviceStack[0x0] )) == 0x0 && 
        ((StackPointerInx+0x1)*sizeof(DeviceStack[0x0])) <= SizeOfStack ); 

    NewStackSize = SizeOfStack + StackGrowValueBytes;
    NewDeviceStack = (POC_DEVICE_OBJECT*)ExAllocatePoolWithTag( NonPagedPool,
                                                                NewStackSize,
                                                                MemoryTag );
    if( NULL == NewDeviceStack ){
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // copy the valid content from the old to the new stack 
    // and free the old one
    //
    if( 0x0 != SizeOfStack ){

        ASSERT( NULL != DeviceStack );

        if( 0x0 != (StackPointerInx+0x1) ){

            RtlCopyMemory( NewDeviceStack,
                           DeviceStack,
                           (StackPointerInx+0x1)*sizeof( DeviceStack[0x0] ) );

        }//if( 0x0 != (StackPointerInx+0x1) )

        ExFreePoolWithTag( DeviceStack, MemoryTag );
    }//if( 0x0 != SizeOfStack )

    //DeviceStack = NewDeviceStack;
    //SizeOfStack = NewStackSize;

    //
    // copy the values in out parameters
    //
    *PtrDeviceStack = NewDeviceStack;
    *PtrSizeOfStack = NewStackSize;

    ASSERT( ((*PtrSizeOfStack)%sizeof( DeviceStack[0x0] )) == 0x0 );

    return STATUS_SUCCESS;

}

//-------------------------------------------------

__forceinline
VOID
OcCrPushOnStackObjectsFromRealtions(
    __inout POC_DEVICE_OBJECT*    DeviceStack,
    __in ULONG                    SizeOfStack,// size in bytes
    __inout PULONG                PtrStackPointerInx,// index of the last valid entry or (ULONG)(-1)
    __in POC_RELATIONS_OBJECT     RelationsObject
    )
    /*
    the function pushes the REFERENCED objects from RelationsObject to DeviceStack,
    the caller must pop the objects and dereference them
    */
{

    ULONG    PushIndex = 0x0;
    ULONG    StackPointerInx = *PtrStackPointerInx;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    ASSERT( (SizeOfStack%sizeof( DeviceStack[0x0] )) == 0x0 &&
            ((StackPointerInx+0x1)*sizeof(DeviceStack[0x0])) <= SizeOfStack ); 

    if( NULL == RelationsObject->Relations )
        return;

    while( PushIndex != RelationsObject->Relations->Count ){

        POC_DEVICE_OBJECT    OcPdo;

        //
        // get the PDO and then increment the push index
        //
        OcPdo = RelationsObject->Relations->Objects[ (PushIndex++) ];

        ASSERT( NULL!= OcPdo );

        //
        // push the device if it has been found
        //
        if( NULL != OcPdo ){

            //
            // reference the PDO before pushing it
            //
            OcObReferenceObject( OcPdo );

            //
            // push it
            //
            DeviceStack[ (++StackPointerInx) ] = OcPdo;
        }

        ASSERT( (StackPointerInx+0x1) <= SizeOfStack/sizeof( DeviceStack[0x0] ) );

    }

    ASSERT( (StackPointerInx+0x1) <= SizeOfStack/sizeof( DeviceStack[0x0] ) );
    ASSERT( (StackPointerInx+0x1) >= (*PtrStackPointerInx + 0x1) );

    *PtrStackPointerInx = StackPointerInx;
}

//-------------------------------------------------

NTSTATUS
OcCrTraverseFromDownToTop(
    IN POC_DEVICE_OBJECT    PtrDeviceObject,
    IN PtrOcCrNodeFunction  OcCrNodeFunction,
    IN PVOID                Context
    )
    /*
    The functions calls OcCrNodeFunction for all devices's PDO connected
    with the root PtrDeviceObject through relations, also
    it calls OcCrNodeFunction for PtrDeviceObject and its PDO.
    In contrast to OcCrTraverseTopDown this is a very time and resource
    consuming function, it shouldn't be called too frequently
    */
{
    NTSTATUS              RC = STATUS_SUCCESS;
    POC_DEVICE_OBJECT*    DeviceStack = NULL;
    ULONG                 SizeOfStack = 0x0;// size in bytes
    ULONG                 StackPointerInx = (ULONG)(-1);// index of the current pointer, i.e. the last valid entry
    ULONG                 MemoryTag = 'SDcO';
    POC_DEVICE_OBJECT     PdoDevice = NULL;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // get the Pdo or the lowest device in the device stack,
    // because it contains all relation objects
    //
    PdoDevice = OcCrReturnPDO( PtrDeviceObject );

    RC = OcCrTuneDeviceStackForPushing( &DeviceStack,
                                        &SizeOfStack,
                                        StackPointerInx,
                                        4*sizeof( DeviceStack[0x0] ),
                                        MemoryTag );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // reference the Pdo and push it as the first entry
    //
    OcObReferenceObject( PdoDevice );
    DeviceStack[ (++StackPointerInx) ] = PdoDevice;

    //
    // reference the device and push it if the device
    // is not the Pdo, it will be removed on the first
    // loop iteration and it's likely that no any
    // new device will be pushed in the stack because
    // non-PDO devices don't usually have related 
    // objects associated with them - the association 
    // is done with its PDO, the PDO will be popped from
    // the stack on the next iteration and initiates 
    // the upward tree traversal
    //
    if( PdoDevice != PtrDeviceObject ){

        ASSERT( (StackPointerInx+0x1) < SizeOfStack/sizeof( DeviceStack[0x0] ) );
        OcObReferenceObject( PtrDeviceObject );
        DeviceStack[ (++StackPointerInx) ] = PtrDeviceObject;
    }

    //
    // do the main job
    //
    do{

        KIRQL                 OldIrql;
        POC_RELATIONS_OBJECT  PtrBusRelations = NULL;
        POC_RELATIONS_OBJECT  PtrRemovalRelations = NULL;
        POC_DEVICE_OBJECT     DeviceObject;
        ULONG                 StackGrowValue;
        ULONG                 IndicesLeftInStack;
        ULONG                 IndicesToPushInStack;

        ASSERT( StackPointerInx != (ULONG)(-1) && StackPointerInx <= 0xFFFF );

        //
        // put the following code in a try-finally block to dereference 
        // the device before continue the loop
        //
        __try{

            //
            // pop the device from the stack, remember that it was referenced when
            // was inserted in the cache
            //
            DeviceObject = DeviceStack[ (StackPointerInx--) ];

            //
            //call the node function
            //
            if( FALSE == OcCrNodeFunction( DeviceObject, Context ) )
                break;

            //
            // put both the bus and removal relations in the stack
            // as I must traverse all branches related with this device
            //
            OcRwAcquireLockForWrite( &DeviceObject->RwSpinLock, &OldIrql );
            {// start of the lock

                PtrBusRelations = DeviceObject->DeviceRelations[ BusRelations ];
                PtrRemovalRelations = DeviceObject->DeviceRelations[ RemovalRelations ];

                if( NULL != PtrBusRelations )
                    OcObReferenceObject( PtrBusRelations );

                if( NULL != PtrRemovalRelations )
                    OcObReferenceObject( PtrRemovalRelations );

            }// end of the lock
            OcRwReleaseWriteLock( &DeviceObject->RwSpinLock, OldIrql );

            //
            // compute the number of bytes for which stack will be grown
            //
            IndicesLeftInStack = ( SizeOfStack/sizeof( DeviceStack[0x0] ) - (StackPointerInx+0x1) );
            IndicesToPushInStack = (PtrBusRelations && PtrBusRelations->Relations)? PtrBusRelations->Relations->Count: 0x0 + 
                (PtrRemovalRelations && PtrRemovalRelations->Relations)? PtrRemovalRelations->Relations->Count: 0x0;
            StackGrowValue = ( IndicesLeftInStack < IndicesToPushInStack )?
                sizeof( DeviceStack[0x0] )*( IndicesToPushInStack - IndicesLeftInStack ): // the stack must be grown
                0x0; // the stack is bulky enough

            //
            // grow the stack to prepare for pushing 
            // the related devices to the stack
            //
            RC = OcCrTuneDeviceStackForPushing( &DeviceStack,
                                                &SizeOfStack,
                                                StackPointerInx,
                                                StackGrowValue,
                                                MemoryTag );
            if( !NT_SUCCESS( RC ) ){

                //
                // the stack was not grown, break the loop
                // the devices left in the stack won't be processed
                //
                break;
            }

            //
            // now push the related objects in the stack
            //
            if( NULL != PtrBusRelations ){

                OcCrPushOnStackObjectsFromRealtions( DeviceStack,
                                                     SizeOfStack,
                                                     &StackPointerInx,
                                                     PtrBusRelations );

                ASSERT( (StackPointerInx+0x1) <= SizeOfStack/sizeof( DeviceStack[0x0] ) );

                //
                // dereference the relations
                //
                OcObDereferenceObject( PtrBusRelations );
                OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrBusRelations );
            }

            if( NULL != PtrRemovalRelations ){

                ASSERT( (StackPointerInx+0x1) < SizeOfStack/sizeof( DeviceStack[0x0] ) );

                OcCrPushOnStackObjectsFromRealtions( DeviceStack,
                                                     SizeOfStack,
                                                     &StackPointerInx,
                                                     PtrRemovalRelations );

                ASSERT( (StackPointerInx+0x1) <= SizeOfStack/sizeof( DeviceStack[0x0] ) );

                //
                // derefrence the relations
                //
                OcObDereferenceObject( PtrRemovalRelations );
                OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrRemovalRelations );
            }

            ASSERT( (StackPointerInx+0x1) <= SizeOfStack/sizeof( DeviceStack[0x0] ) );

        } __finally {

            //
            // dereference the object poped from the stack
            //
            OcObDereferenceObject( DeviceObject );
        }

        //
        // exit from the loop if the stack is empty,
        // i.e. the last device in the stack has been processed
        //

    } while( StackPointerInx != (ULONG)(-1) );

__exit:

    //
    // dereference all devices left in the stack
    //
    while( (ULONG)(-1) != StackPointerInx ){

        ASSERT( NULL != DeviceStack );

        //
        // dereference.and pop the device from the stack
        //
        OcObDereferenceObject( DeviceStack[ (StackPointerInx--) ] );

    }// while

    if( NULL != DeviceStack )
        ExFreePoolWithTag( DeviceStack, MemoryTag );

    return RC;
}

//-------------------------------------------------

NTSTATUS
OcCrProcessDeviceStartWR(
    IN ULONG_PTR    pDeviceToStart,
    IN ULONG_PTR    pNextLowerDriver,
    IN ULONG_PTR    pIrp
    )
{
    PDEVICE_OBJECT    DeviceToStart = (PDEVICE_OBJECT)pDeviceToStart;
    PDEVICE_OBJECT    NextLowerDriver = (PDEVICE_OBJECT)pNextLowerDriver;
    PIRP              Irp = (PIRP)pIrp;

    ASSERT( NULL != IoGetNextIrpStackLocation( Irp )->CompletionRoutine );

    IoCallDriver( NextLowerDriver, Irp );

    return STATUS_SUCCESS;
}

//-------------------------------------------------

NTSTATUS
NTAPI
OcPnPFilterPreStartCallback(
    PDEVICE_OBJECT    DeviceToStart,
    PDEVICE_OBJECT    NextLowerDriver,
    PIRP    Irp,
    POC_FILTER_IRP_DECISION    PtrIrpDecision
    )
    /*
    In the provided IRP the completion routine in the next 
    stack location is already set by the caller, if you want your
    own one then use completion routine hooking. You shouldn't fall the IRP
    in the worker thread, because the caller's completion routine won't
    be called! Do not try to call it himself!
    Only OcFilterReturnCode have a meaning for the PtrIrpDecision, all
    other return codes results in calling the lower device by the caller
    w/o any manipulation with the Irp, i.e. the caller's completion routine
    is always set.
    */
{
    NTSTATUS    RC = STATUS_SUCCESS;
    POC_DEVICE_OBJECT            PtrOcDeviceObject;
    POC_WORK_ITEM_LIST_OBJECT    PtrWorkItem;

    //
    // PnP Manager sends the IRP_MN_START request at passive level
    //
    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    //
    // caller(PnP filter) must set the completion routine
    //
    ASSERT( NULL != IoGetNextIrpStackLocation( Irp )->CompletionRoutine );

    //
    // get the PDO and check whether start request for this
    // PDO has already been posted in the worker pool,
    // do not send it second time because this is a 
    // request to the underlying filter or hooked driver,
    // if the request would be post a deadlock could arise
    // if the number of simultaneously repeteadly send request
    // exceeds the number of the worker threads
    //
    PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                   (ULONG_PTR)DeviceToStart,
                                                   OcObReferenceObject );
    if( NULL != PtrOcDeviceObject ){

        BOOLEAN    AlreadyPending = FALSE;

        //
        // check whether the request has already been posted in
        // a worker thread
        //
        if( ( NULL != PtrOcDeviceObject->Pdo && 
              0x1 == PtrOcDeviceObject->Pdo->Flags.DeviceStartPending ) || 
              0x1 == PtrOcDeviceObject->Flags.DeviceStartPending ){

            AlreadyPending = TRUE;

        } else {

            AlreadyPending = FALSE;
        }

        if( FALSE == AlreadyPending ){
            //
            // mark as pending
            //
            if( NULL != PtrOcDeviceObject->Pdo )
                PtrOcDeviceObject->Pdo->Flags.DeviceStartPending = 0x1;

            PtrOcDeviceObject->Flags.DeviceStartPending = 0x1;
        }

        OcObDereferenceObject( PtrOcDeviceObject );

        if( TRUE == AlreadyPending ){

            //
            // the request has been already posted in a worker thread
            //
            *PtrIrpDecision = OcFilterSendIrpToLowerDontSkipStack;
            return RC;
        }

    }//if( NULL != PtrOcDeviceObject )

    //
    // now I use the thread pool with the shared queue, might
    // be in the future it will be replaced by a single
    // dedicated thread
    //
    PtrWorkItem = OcTplReferenceSharedWorkItemList( Global.StartDeviceThreadsPoolObject );
    ASSERT( NULL != PtrWorkItem );
    if( NULL == PtrWorkItem ){

        ASSERT( !"Something went wrong with the worker threads!" );

        *PtrIrpDecision = OcFilterSendIrpToLowerDontSkipStack;
        return RC;
    }

    //
    // pend all!
    //
    IoMarkIrpPending( Irp );
    RC = STATUS_PENDING;
    *PtrIrpDecision = OcFilterReturnCode;

    //
    // post IRP in a special dedicated thread pool
    //
    if( !NT_SUCCESS( OcWthPostWorkItemParam3( PtrWorkItem,
                                              FALSE,
                                              OcCrProcessDeviceStartWR,
                                              (ULONG_PTR)DeviceToStart,
                                              (ULONG_PTR)NextLowerDriver,
                                              (ULONG_PTR)Irp ) ) ){

        ASSERT( !"Something went wrong with the worker threads!" );

        //
        // posting the request in the worker thread failed
        // allow the caller to complete the request
        //
        *PtrIrpDecision = OcFilterSendIrpToLowerDontSkipStack;
        RC = STATUS_SUCCESS;
    }

    OcObDereferenceObject( PtrWorkItem );

    ASSERT( STATUS_PENDING == RC );
    ASSERT( OcFilterReturnCode == *PtrIrpDecision );

    return RC;
}

//-------------------------------------------------

NTSTATUS
OcCrAddInDatabaseDeviceInMiddleOfStack(
    IN PDEVICE_OBJECT    DeviceInMiddle,
    OUT POC_DEVICE_OBJECT*    PtrPtrOcMiddleDeviceObject
    )
    /*
    the function returns the refrenced *PtrPtrOcMiddleDeviceObject,
    this is the caller's responsibility to dereference it
    */
{
    NTSTATUS             RC = STATUS_SUCCESS;
    PDEVICE_OBJECT       Pdo;
    POC_DEVICE_OBJECT    PtrOcPdo;
    ULONG                NumberOfAttempt = 0x0;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    //
    // get the underlying device, this should be PDO
    //
    Pdo = Global.SystemFunctions.IoGetDeviceAttachmentBaseRef( DeviceInMiddle );
    if( NULL == Pdo ){

        //
        // the device is not attached to any device( e.g. LanmanRedirector )
        //
        Pdo = DeviceInMiddle;
        ObReferenceObject( Pdo );
    }

    ASSERT( 0x0 == NumberOfAttempt );
    while( NumberOfAttempt != 0x2 ){

        NumberOfAttempt += 0x1;

        PtrOcPdo = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                              (ULONG_PTR)Pdo,
                                              OcObReferenceObject );
        if( NULL == PtrOcPdo && 0x1 == NumberOfAttempt ){

            //
            // attach to the lower device and repeat the attempt
            //
            RC = Global.PnPFilterExports.AttachToNonPnPDevice( Pdo );
            ASSERT( NT_SUCCESS( RC ) );
            if( !NT_SUCCESS( RC ) ){

                //
                // the PDO has not been found and attachment was unsuccessful
                //
                break;
            }//if( !NT_SUCCESS( RC ) )

            //
            // repeat the attempt to find lower device after attachment
            //
            continue;

        }

        //
        // the PDO or lower Device has been or hasn't been found
        //
        break;
    }

    ASSERT( NULL != PtrOcPdo && NT_SUCCESS( RC ) );

    if( NULL == PtrOcPdo && NT_SUCCESS( RC ) )
        RC = STATUS_INVALID_DEVICE_REQUEST;

    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // I need a PDO or a lower device
    //
    if( OcDevicePnPTypePdo != PtrOcPdo->DevicePnPType && 
        OcDeviceLowerNoPnPType != PtrOcPdo->DevicePnPType ){

        RC = STATUS_INVALID_DEVICE_REQUEST;
        goto __exit;
    }

    if( Pdo == DeviceInMiddle ){

        //
        // definitely this is a non PnP device, may be LanmanRedirector
        //
        goto __exit;
    }

    //
    // lock the PDO device from removing from the list and hash and 
    // receiving IRP_MN_REMOVE request
    //
    RC = OcRlAcquireRemoveLock( &PtrOcPdo->RemoveLock.Common );
    if( NT_SUCCESS( RC ) ){

        //
        // the remove lock is acquired
        //

        ASSERT( 0x0 == PtrOcPdo->Flags.CripplePdo );

        RC = OcCrProcessNewPnPDevice( PtrOcPdo->KernelDeviceObject,
                                      DeviceInMiddle,
                                      OcDevicePnPTypeInMiddleOfStack,
                                      (BOOLEAN)( 0x0 == PtrOcPdo->Flags.CripplePdo ),
                                      PtrPtrOcMiddleDeviceObject );

        //
        // set device's state to the PDO's state
        //
        if( NT_SUCCESS( RC ) ){

            OcCrPnPFilterRepotNewDeviceState( DeviceInMiddle, PtrOcPdo->PnPState );
        }

        OcRlReleaseRemoveLock( &PtrOcPdo->RemoveLock.Common );
    }
    //
    // the remove lock is released
    //

__exit:

    OcObDereferenceObject( PtrOcPdo );
    ObDereferenceObject( Pdo );

    return RC;
}

//-------------------------------------------------

POC_DEVICE_OBJECT
OcCrGetDeviceObjectOnWhichFsdMounted(
    IN PFILE_OBJECT    FileObject
    )
    /*
    the function returns a refrenced device object( or NULL ) related with file object
    usually on this device object FSD is mounted,
    the caller must dereference the returned object when it is no longer needed
    */
{
    POC_DEVICE_OBJECT    PtrOcDeviceObject;

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );
    ASSERT( NULL != FileObject );

    PtrOcDeviceObject = OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                                   (ULONG_PTR)FileObject->DeviceObject,
                                                   OcObReferenceObject );
    if( NULL == PtrOcDeviceObject ){

        NTSTATUS    RC;

        //
        // try to insert the device in the device database,
        // this may be a device in a middle of a stack 
        // with a VPB on which the RAW FSD has been mounted
        //
        RC = OcCrAddInDatabaseDeviceInMiddleOfStack( FileObject->DeviceObject, 
                                                     &PtrOcDeviceObject );
        if( !NT_SUCCESS( RC ) ){
            //
            // this might be LanmanRedirector's device,
            // which doesn't have any PDO under it
            //
            PtrOcDeviceObject = NULL;
        }
    }

    return PtrOcDeviceObject;
}

//-------------------------------------------------

NTSTATUS
OcCrOpenDeviceKey(
    __in PDEVICE_OBJECT    InitializedPdo,
    __inout HANDLE*    KeyHandle
    )
    /*++
    the function returns a handle for the device's PnP key, 
    the key should look like the following key
    "\REGISTRY\MACHINE\SYSTEM\ControlSetXXX\Enum\ENUMERATOR_TYPE\DEVICE_AND_VENDOR_ID\UNIQUE_ID"
    --*/
{
    NTSTATUS                    RC;
    HANDLE                      deviceKeyHandle = NULL;
    PVOID                       deviceKeyObject = NULL;
    POBJECT_NAME_INFORMATION    NameInfoBuffer = NULL;
    OBJECT_ATTRIBUTES           RegKeyObjectAttributes;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( NULL != Global.SystemFunctions.IoOpenDeviceRegistryKey );
    ASSERT( OcCrIsPdo( InitializedPdo ) );
    ASSERT( IoIsSystemThread( PsGetCurrentThread() ) );

    //
    // get handle for a device parameters key
    //
    RC = Global.SystemFunctions.IoOpenDeviceRegistryKey( InitializedPdo,
                                                         PLUGPLAY_REGKEY_DEVICE,
                                                         KEY_QUERY_VALUE,
                                                         &deviceKeyHandle );

    if( !NT_SUCCESS( RC ) )
        return RC;

    //
    // Reference the key object
    //
    RC = ObReferenceObjectByHandle ( deviceKeyHandle,
                                     KEY_QUERY_VALUE,
                                     NULL,//the object type can be NULL if the access mode is KernelMode
                                     KernelMode, //to avoid a security and type checking set mode to Kernel
                                     (PVOID *)&deviceKeyObject,
                                     NULL );

    ASSERT( NT_SUCCESS( RC ) );

    if( !NT_SUCCESS( RC ) )
        goto __exit;

    ZwClose( deviceKeyHandle );
    deviceKeyHandle = NULL;

    //
    // get a key name, such as "\REGISTRY\MACHINE\SYSTEM\ControlSet004\Enum\USB\Vid_0ea0&Pid_2168\611042017A1100EA\Device Parameters"
    //
    RC = OcCrQueryObjectName( deviceKeyObject, 
                              &NameInfoBuffer );
    ASSERT( STATUS_PENDING != RC );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    if( NameInfoBuffer->Name.Length <= sizeof("Device Parameters")*sizeof( WCHAR ) ){

        RC = STATUS_OBJECT_PATH_INVALID;
        goto __exit;
    }

    //
    // remove "\Device Parameters" suffix to create a device key
    //
    NameInfoBuffer->Name.Length -= (sizeof("Device Parameters")-0x1)*sizeof( WCHAR );
    while( NameInfoBuffer->Name.Length > sizeof( WCHAR ) && 
           L'\\' != NameInfoBuffer->Name.Buffer[ NameInfoBuffer->Name.Length/sizeof( WCHAR ) - 0x1 ] ){

               NameInfoBuffer->Name.Length -= sizeof( WCHAR );
    }
    //
    // remove L'\\'
    //
    NameInfoBuffer->Name.Length -= sizeof( WCHAR );

    InitializeObjectAttributes( &RegKeyObjectAttributes,
                                &NameInfoBuffer->Name,
                                OBJ_CASE_INSENSITIVE,
                                NULL,
                                NULL);

    //
    // open the device key
    //
    RC = ZwOpenKey( &deviceKeyHandle,
                    KEY_QUERY_VALUE,
                    &RegKeyObjectAttributes );

    if( !NT_SUCCESS( RC ) )
        goto __exit;

    //
    // Now we have a key handle for a device subkey,
    // namely the handle for the key such as the following -  
    // "\REGISTRY\MACHINE\SYSTEM\ControlSet004\Enum\USB\Vid_0ea0&Pid_2168\611042017A1100EA"
    //

    *KeyHandle = deviceKeyHandle;
    deviceKeyHandle = NULL;

__exit:

    if( NameInfoBuffer )
        OcCrFreeNameInformation( NameInfoBuffer );

    if( NULL != deviceKeyObject )
        ObDereferenceObject( deviceKeyObject );

    if( NULL != deviceKeyHandle )
        ZwClose( deviceKeyHandle );

    ASSERT( NT_SUCCESS( RC ) );

    return RC;
}

//-------------------------------------------------

NTSTATUS
OcCrFindAndReferenceFdoDriverObject(
    IN PDEVICE_OBJECT     InitializedPdo,
    OUT PDRIVER_OBJECT*   PtrFdoDriverObject
    )
    /*
    the function returns a referenced functional driver object
    */
{
    NTSTATUS         RC = STATUS_SUCCESS;
    HANDLE           deviceKeyHandle = NULL;
    UNICODE_STRING   ValueName;
    PVOID            Buffer = NULL;
    ULONG            BufferSize;
    PKEY_VALUE_PARTIAL_INFORMATION    pValuePartialInfo;
    UNICODE_STRING                    FdoDriverName = { 0x0 };

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcCrIsPdo( InitializedPdo ) );

    RC = OcCrOpenDeviceKey( InitializedPdo, &deviceKeyHandle );
    if( !NT_SUCCESS( RC ) ){

        goto __exit;
    }

    ASSERT( deviceKeyHandle );

    //
    // Now we have a key handle for a device subkey,
    // namely the handle for the key such as the following -  
    // "\REGISTRY\MACHINE\SYSTEM\ControlSet004\Enum\USB\Vid_0ea0&Pid_2168\611042017A1100EA"
    //

    //
    // Get the service name( i.e. the name of the driver object ).
    //

    RtlInitUnicodeString( &ValueName, L"Service" );

    RC = OcCrGetValueFromKey( deviceKeyHandle,
                              &ValueName,
                              KeyValuePartialInformation,
                              &Buffer,
                              &BufferSize );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    pValuePartialInfo = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;

    if( pValuePartialInfo->Type != REG_SZ ){

        RC = STATUS_OBJECT_PATH_INVALID;
        goto __exit;
    }

    ASSERT( BufferSize > ( pValuePartialInfo->DataLength + FIELD_OFFSET( KEY_VALUE_PARTIAL_INFORMATION, Data ) + sizeof( WCHAR ) ) );
    ASSERT( L'\0' == ((PWCHAR)pValuePartialInfo->Data)[ pValuePartialInfo->DataLength/sizeof( WCHAR )-0x1 ] );

    //
    // create the device driver's object name
    //
    FdoDriverName.Length = 0x0;
    FdoDriverName.MaximumLength = sizeof( L"\\Driver\\" ) + (USHORT)pValuePartialInfo->DataLength;
    FdoDriverName.Buffer = ExAllocatePoolWithTag( PagedPool, (ULONG)FdoDriverName.MaximumLength, '10cO' );
    if( NULL == FdoDriverName.Buffer ){

        RC = STATUS_INSUFFICIENT_RESOURCES;
        goto __exit;
    }

    RtlAppendUnicodeToString( &FdoDriverName, L"\\Driver\\" );
    RtlAppendUnicodeToString( &FdoDriverName, (PWCHAR)pValuePartialInfo->Data );

    ExFreePool( Buffer );
    Buffer = NULL;
    pValuePartialInfo = NULL;

    /*
    //
    // do not know what is wrong with Guardian dangles, but
    // it is definitely that something goes wrong
    // if they are hooked
    //
    {
        PWSTR   GuardDangle = L"\\Driver\\GrdUsb";
        UNICODE_STRING    UnGuardDangle;

        RtlInitUnicodeString( &UnGuardDangle, GuardDangle );
        if( 0x0 == RtlCompareUnicodeString( &UnGuardDangle, &FdoDriverName, TRUE ) ){

            RC = STATUS_INSUFFICIENT_RESOURCES;
            goto __exit;
        }
    }
    */

    RC = ObReferenceObjectByName( &FdoDriverName,
                                  OBJ_CASE_INSENSITIVE,
                                  NULL,
                                  FILE_READ_ACCESS,
                                  *IoDriverObjectType,
                                  KernelMode,
                                  NULL,
                                  PtrFdoDriverObject );

__exit:

    if( NULL != FdoDriverName.Buffer )
        ExFreePoolWithTag( FdoDriverName.Buffer, '10cO' );

    if( NULL != Buffer )
        OcCrFreeValueFromKey( Buffer );

    if( NULL != deviceKeyHandle )
        ZwClose( deviceKeyHandle );

    return RC;
}

//-------------------------------------------------

PDEVICE_OBJECT
OcCrFindReferencedFdoInStackByDriverObject(
    IN PDEVICE_OBJECT  PdoPtr,
    IN PDRIVER_OBJECT  FdoDriverObject
    )
    /*
    the function returnes a refrenced FOD for the PDO, the FdoDriverObject
    is used to find FDO in the stack
    */
{
    PDEVICE_OBJECT    ReferenceHighestDeviceInStack;
    PDEVICE_OBJECT    ReferencedCurrentDevice;

    ASSERT( OcCrIsPdo( PdoPtr ) );
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    //
    // Find the FDO for the PDO by traversing the device stack from the top.
    //
    // I must be sure that the device stack will not be torn down while I traversing it, 
    // to protect from this I reference the highest device in the stack, 
    // then get the next lower device( using function which acquires 
    // a system lock ) for the highest device an so on for all found devices.
    // Pay attention - in some cases PDO and FDO drivers are the same driver.
    //
    ReferenceHighestDeviceInStack = OcCrIoGetAttachedDeviceReference( PdoPtr );
    ReferencedCurrentDevice = ReferenceHighestDeviceInStack;

    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ReferenceHighestDeviceInStack );
    ASSERT( ReferencedCurrentDevice );

    while( ReferencedCurrentDevice && 
           ReferencedCurrentDevice->DriverObject != FdoDriverObject ){

        PDEVICE_OBJECT    OldReferencedDeviceObject = ReferencedCurrentDevice;

        ReferencedCurrentDevice = OcCrIoGetLowerDeviceObject( OldReferencedDeviceObject );

        ObDereferenceObject( OldReferencedDeviceObject );
    }//while

    //
    // process a special case when the found device is a PDO,
    // this means that the PDO and the FDO are created by the 
    // same driver, e.g.usbhub.sys creates PDOs and FDOs for 
    // USB hubs, and either the stack has not yet been build 
    // or the FDO is not created and all requests are sent 
    // directly to PDO, i.e. this is a raw Pdo
    //
    if( ReferencedCurrentDevice == PdoPtr ){

        //
        // this must be a PDO
        //
        ASSERT( NULL == OcCrIoGetLowerDeviceObject( ReferencedCurrentDevice ) );

        ObDereferenceObject( ReferencedCurrentDevice );
        ReferencedCurrentDevice = NULL;
    }

    return ReferencedCurrentDevice;
}

//-------------------------------------------------

NTSTATUS
OcCrGetReferencedFdoForThePdo(
    IN  PDEVICE_OBJECT    PdoPtr,
    OUT PDEVICE_OBJECT*   PtrFdoPtr
    )
    /*
    the function returns a refernced FDO for the PDO
    */
{
    NTSTATUS          RC;
    PDRIVER_OBJECT    ReferencedFdoDriverObject;
    PDEVICE_OBJECT    ReferencedFdoPtr;

    //
    // TO DO - some drivers create a filter and an FDO in the same stack! Resolve this.
    //

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcCrIsPdo( PdoPtr ) );

    RC = OcCrFindAndReferenceFdoDriverObject( PdoPtr, &ReferencedFdoDriverObject );
    if( !NT_SUCCESS( RC ) )
        return RC;

    ASSERT( ReferencedFdoDriverObject );

    ReferencedFdoPtr = OcCrFindReferencedFdoInStackByDriverObject( PdoPtr, 
                                                                   ReferencedFdoDriverObject );

    ObDereferenceObject( ReferencedFdoDriverObject );
    OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ReferencedFdoDriverObject );

    if( NULL != ReferencedFdoPtr )
        *PtrFdoPtr = ReferencedFdoPtr;
    else 
        RC = STATUS_NO_SUCH_DEVICE;

    return RC;
}

//-------------------------------------------------

NTSTATUS
OcCrFindAndProcessFdoForPdo(
    __in POC_DEVICE_OBJECT       PtrOcPdo
    )
    /*
    the function finds an Fdo for the initialized Pdo 
    and adds the found Fdo in the device database
    */
{
    NTSTATUS          RC;
    PDEVICE_OBJECT    Fdo;

    ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );
    ASSERT( OcDevicePnPTypePdo == PtrOcPdo->DevicePnPType );
    ASSERT( 0x0 == PtrOcPdo->Flags.CripplePdo );

    if( OcDevicePnPTypePdo != PtrOcPdo->DevicePnPType )
        return STATUS_INVALID_PARAMETER_1;

    //
    // remeber that at least one attempt to find an Fdo has been done
    //
    PtrOcPdo->Flags.AttemptToFindFdoDone = 0x1;

    //
    // first check whether the Fdo has been aleready found,
    // synchronize with the following code by locking the Pdo
    //
    RC = OcRlAcquireRemoveLock( &PtrOcPdo->RemoveLock.Common );
    if( NT_SUCCESS( RC ) ){

        BOOLEAN    FdoHasBeenFound;

        FdoHasBeenFound = ( 0x1 == PtrOcPdo->Flags.FdoFound )? TRUE : FALSE;

        //
        // release the lock acquired before entering in this scope
        //
        OcRlReleaseRemoveLock( &PtrOcPdo->RemoveLock.Common );

        //
        // exit with the success code if Fdo has been already found
        //
        if( TRUE == FdoHasBeenFound )
            return STATUS_SUCCESS;

    } else {

        //
        // it seems that the Pdo is being deleted
        //
        return RC;
    }

    RC = OcCrGetReferencedFdoForThePdo( PtrOcPdo->KernelDeviceObject, &Fdo );
    if( !NT_SUCCESS( RC ) )
        return RC;

    ASSERT( Fdo != PtrOcPdo->KernelDeviceObject );
    if( Fdo == PtrOcPdo->KernelDeviceObject ){

        RC = STATUS_INVALID_PARAMETER_1;
        goto __exit;
    }

    //
    // check whether the object has already been inserted in the
    // device database, this may happen only when this driver
    // receives information from the PnP filter on the start
    //
    if( NULL != OcHsFindContextByKeyValue( Global.PtrDeviceHashObject,
                                           (ULONG_PTR)Fdo,
                                           NULL ) ){

        ASSERT( PsGetCurrentThread() == Global.ThreadCallingReportDevice );
        RC = STATUS_SUCCESS;
        goto __exit;
    }

    //
    // lock the PDO device from removing from the list and hash and 
    // receiving IRP_MN_REMOVE request
    //
    RC = OcRlAcquireRemoveLock( &PtrOcPdo->RemoveLock.Common );
    if( NT_SUCCESS( RC ) ){

        POC_DEVICE_OBJECT    PtrOcFdo;

        //
        // the PDO's remove lock is acquired
        //

        ASSERT( 0x0 == PtrOcPdo->Flags.CripplePdo );

        //
        // Add the Fdo in the device database
        //
        RC = OcCrProcessNewPnPDevice( PtrOcPdo->KernelDeviceObject,
                                      Fdo,
                                      OcDevicePnPTypeFunctionalDo,
                                      (BOOLEAN)( 0x0 == PtrOcPdo->Flags.CripplePdo ),
                                      &PtrOcFdo );

        ASSERT( NT_SUCCESS( RC ) );
        //
        // set the FDO's state to the PDO's state
        //
        if( NT_SUCCESS( RC ) ){

            //
            // actually the FDO state is not properly handled through the
            // all states, the PDO should be used instead FDO to get any
            // information about a device stack
            //
            OcCrPnPFilterRepotNewDeviceState( Fdo, PtrOcPdo->PnPState );
            OcObDereferenceObject( PtrOcFdo );
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( PtrOcFdo );

            //
            // remeber that the FDO has been found, do this with locked Pdo
            //
            PtrOcPdo->Flags.FdoFound = 0x1;
        }

        //
        // release the lock acquired before entering in this scope of visibility
        //
        OcRlReleaseRemoveLock( &PtrOcPdo->RemoveLock.Common );
    }
    //
    // the PDO's remove lock is released
    //

__exit:

    ObDereferenceObject( Fdo );

    return RC;
}

//-------------------------------------------------

POC_DEVICE_OBJECT
OcCrReferenceFdoForPdo(
    __in POC_DEVICE_OBJECT    Pdo
    )
    /*
    the function reterns referenced Fdo for Pdo 
    if it has been found by OcCrFindAndProcessFdoForPdo
    */
{
    POC_DEVICE_OBJECT    ReferencedFdo = NULL;
    KIRQL                OldIrql;

    KeAcquireSpinLock( &Pdo->PdoUpperDevicesListSpinLock, &OldIrql );
    {// start of the spin lock

        POC_DEVICE_OBJECT    UpperDevice;
        PLIST_ENTRY          ListEntry;

        //
        // scan the list of attached devices
        //
        for( ListEntry = Pdo->UpperDevicesList.PdoHeadForListOfUpperDevices.Flink;
             ListEntry != &Pdo->UpperDevicesList.PdoHeadForListOfUpperDevices;
             ListEntry = UpperDevice->UpperDevicesList.EntryForListOfUpperDevices.Flink ){

                 UpperDevice = CONTAINING_RECORD( ListEntry, OC_DEVICE_OBJECT, UpperDevicesList.EntryForListOfUpperDevices );
                 //
                 // check whether this is an Fdo
                 //
                 if( OcDevicePnPTypeFunctionalDo == UpperDevice->DevicePnPType ){

                     //
                     // this is an Fdo, I find it!
                     //
                     ReferencedFdo = UpperDevice;
                     OcObReferenceObject( ReferencedFdo );
                     break;
                 }
        }

    }// end of the spin lock
    KeReleaseSpinLock( &Pdo->PdoUpperDevicesListSpinLock, OldIrql );

    return ReferencedFdo;

}

//-------------------------------------------------

NTSTATUS
OcCrDefinePnPFilterType(
    __inout_opt POC_DEVICE_OBJECT    PtrFido
    )
    /*
    the function defines the type of the filter device object - upper or lower filter with
    regard to the FDO device in the stack
    */
{
    NTSTATUS             RC = STATUS_SUCCESS;
    POC_DEVICE_OBJECT    Pdo;
    POC_DEVICE_OBJECT    ReferencedFdo = NULL;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( OcDevicePnPTypeFilterDo == PtrFido->DevicePnPType );

    Pdo = PtrFido->Pdo;
    if( NULL == Pdo ){

        ASSERT( !"Invalid Fdo has been sent to OcCrDefinePnPFilterType" );
        return STATUS_INVALID_PARAMETER_1;
    }

    ASSERT( OcDevicePnPTypePdo == Pdo->DevicePnPType );
    ASSERT( 0x1 == Pdo->Flags.AttemptToFindFdoDone );

    if( 0x0 == Pdo->Flags.AttemptToFindFdoDone ){

        ASSERT( !"Invalid Pdo has been sent to OcCrDefinePnPFilterType, the Fdo has not been found" );
        return STATUS_INVALID_PARAMETER_1;
    }

    if( 0x0 == Pdo->Flags.FdoFound ){

        //
        // there is no Fdo has been found, so the filter must have the Pdo's interface
        //

        PtrFido->Flags.LowerFilter = 0x1;
        ASSERT( 0x0 == PtrFido->Flags.UpperFilter );

        return STATUS_SUCCESS;
    }

    //
    // lock the PDO device from removing from the list and hash and 
    // receiving IRP_MN_REMOVE request
    //
    RC = OcRlAcquireRemoveLock( &Pdo->RemoveLock.Common );
    if( NT_SUCCESS( RC ) ){

        //
        // the Fdo has been found and should be in the Fdo list
        //
        ReferencedFdo = OcCrReferenceFdoForPdo( Pdo );
        if( NULL != ReferencedFdo ){

            //
            // find out the type of the filter device
            //

            PDEVICE_OBJECT    UpperDeviceObject;
            BOOLEAN           FdoHasBeenPassed = FALSE;

            //
            // use the simplest technique, the device can't go away because
            // IRR_MN_REMOVE request will be blocked until the lock is realised,
            // if the device go away when we have some rogue driver in the stack
            // TO DO - verify the upper utterance!
            //

            for( UpperDeviceObject = Pdo->KernelDeviceObject->AttachedDevice;
                 NULL != UpperDeviceObject; 
                 UpperDeviceObject = UpperDeviceObject->AttachedDevice ){

                    if( UpperDeviceObject == ReferencedFdo->KernelDeviceObject )
                        FdoHasBeenPassed = TRUE;

                    if( UpperDeviceObject == PtrFido->KernelDeviceObject ){

                        PtrFido->Flags.UpperFilter = FdoHasBeenPassed? 0x1: 0x0;
                        PtrFido->Flags.LowerFilter = FdoHasBeenPassed? 0x0: 0x1;
                        break;
                    }
            }//for( UpperDeviceObject = ...

            //
            // the filter device MUST be found in the stack!
            //
            ASSERT( NULL != UpperDeviceObject );

            OcObDereferenceObject( ReferencedFdo );
            OC_DEBUG_SET_POINTER_TO_INVALID_VALUE( ReferencedFdo );

        } else {

            ASSERT( !"Something went wrong - Pdo's flags said that Fdo has been found but Fdo hasn't been found by OcCrReferenceFdoForPdo" );
            RC = STATUS_INVALID_PARAMETER_1;
        }

        //
        // release the Pdo lock
        //
        OcRlReleaseRemoveLock( &Pdo->RemoveLock.Common );
    }

    ASSERT( NT_SUCCESS( RC ) );

    return RC;
}

//-------------------------------------------------

VOID
NTAPI
OcCrDeleteDeviceTypeObject(
    __in POC_DEVICE_TYPE_OBJECT    PtrDeviceRelationsObject
    )
{
    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

    if( NULL != PtrDeviceRelationsObject )
        ExFreePoolWithTag( PtrDeviceRelationsObject->TypeStack, OC_TYPE_STACK_MEM_TAG );
}

//------------------------------------------------------

BOOLEAN
OcCrSetFullDeviceTypeWhileTreeTraversing(
    IN POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    IN PVOID    Context// pointer to a root device
    )
{
    NTSTATUS    RC;

    //
    // do not process the request to a root device,
    // because its type has not changed
    //
    if( NULL != Context && 
        ( PtrOcNodeDeviceObject == (POC_DEVICE_OBJECT)Context || 
          PtrOcNodeDeviceObject->Pdo == (POC_DEVICE_OBJECT)Context ) )
        return TRUE;

    RC = OcCrSetFullDeviceType( PtrOcNodeDeviceObject );

    ASSERT( NT_SUCCESS( RC ) );

    return TRUE;
}

//------------------------------------------------------

NTSTATUS
OcCrSetFullDeviceType(
    __inout POC_DEVICE_OBJECT    OcDeviceObject
    )
    /*
    this is a very time and resource consuming function
    so it should be called carefully
    */
{
    NTSTATUS                      RC;
    POC_FULL_DEVICE_TYPE_STACK    TypeStack;
    POC_DEVICE_TYPE_OBJECT        OldDeviceType;
    POC_DEVICE_TYPE_OBJECT        NewDeviceType;
    ULONG                         StackSizeBt;
    USHORT                        CurrentDepth = 0x4;
    USHORT                        DepthGrowValue = 0x4;
    KIRQL                         OldIrql;

    ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
    ASSERT( CurrentDepth > 0x0 );

    StackSizeBt = FIELD_OFFSET( OC_FULL_DEVICE_TYPE_STACK, FullDeviceType ) + 
        ( CurrentDepth * sizeof( TypeStack->FullDeviceType[ 0x0 ] ) );

    TypeStack = ExAllocatePoolWithTag( NonPagedPool,
                                       StackSizeBt,
                                       OC_TYPE_STACK_MEM_TAG );
    if( NULL == TypeStack )
        return STATUS_INSUFFICIENT_RESOURCES;

    TypeStack->NumberOfEntries = CurrentDepth;
    TypeStack->NumberOfValidEntries = 0x0;

    while( !OcCrGetFullDeviceType( OcDeviceObject, TypeStack ) ){

        //
        // free the old stack
        //
        ExFreePoolWithTag( TypeStack, OC_TYPE_STACK_MEM_TAG );

        //
        // increase the size
        //
        CurrentDepth = CurrentDepth + DepthGrowValue;
        ASSERT( CurrentDepth > 0x1 );
        StackSizeBt = StackSizeBt + 
                      CurrentDepth* sizeof( TypeStack->FullDeviceType[ 0x0 ] );

        //
        // allocate the new
        //
        TypeStack = ExAllocatePoolWithTag( NonPagedPool,
                                           StackSizeBt,
                                           OC_TYPE_STACK_MEM_TAG );
        if( NULL == TypeStack )
            return STATUS_INSUFFICIENT_RESOURCES;

        TypeStack->NumberOfEntries = CurrentDepth;
        TypeStack->NumberOfValidEntries = 0x0;

    }// while

    //
    // create the object, the object is created as a referenced one
    //
    RC = OcObCreateObject( &Global.OcDeviceTypeObjectType,
                           &NewDeviceType );
    if( !NT_SUCCESS( RC ) ){

        ExFreePoolWithTag( TypeStack, OC_TYPE_STACK_MEM_TAG );
        return RC;
    }

    //
    // initialize the new object
    //
    NewDeviceType->TypeStack = TypeStack;

    //
    // lock the device object and exchange the type stack object
    //
    OcRwAcquireLockForWrite( &OcDeviceObject->RwSpinLock, &OldIrql );
    {// start of the lock
        OldDeviceType = OcDeviceObject->DeviceType;
        OcDeviceObject->DeviceType = NewDeviceType;
    }// end of the lock
    OcRwReleaseWriteLock( &OcDeviceObject->RwSpinLock, OldIrql );

    if( NULL != OldDeviceType )
        OcObDereferenceObject( OldDeviceType );

    return STATUS_SUCCESS;
}

//-------------------------------------------------

VOID
OcCrRedefineTypesForDeviceGrowingFromRoot(
    IN POC_DEVICE_OBJECT    RootDevice
    )
{
    OcCrTraverseFromDownToTop( RootDevice->Pdo? RootDevice->Pdo: RootDevice,
                               OcCrSetFullDeviceTypeWhileTreeTraversing,
                               RootDevice->Pdo? RootDevice->Pdo: RootDevice );
}

//-------------------------------------------------

BOOLEAN
OcCrEmulatePnpStackBuildingForDevice(
    IN POC_DEVICE_OBJECT    OcDeviceObject,
    IN PVOID    Context// POC_PNP_CALLBACKS
    )
    /*
    the function emulates the node building by
    the PnP Manager
    */
{
    POC_PNP_CALLBACKS    PnpCallbacks = (POC_PNP_CALLBACKS)Context;

    ASSERT( PASSIVE_LEVEL == KeGetCurrentIrql() );
    ASSERT( 0x0 == OcDeviceObject->Flags.DeviceIsProcessedTreeRoot );

    //
    // call AddDevice callback
    //
    PnpCallbacks->AddDevicePostCallback( OcDeviceObject->Pdo->KernelDeviceObject,
                                         OcDeviceObject->KernelDeviceObject,
                                         OcDeviceObject->DevicePnPType,
                                         TRUE );

    if( Started == OcDeviceObject->PnPState ){

        ULONG                   i;
        DEVICE_RELATION_TYPE    RelationsType[ 0x3 ];

        RelationsType[ 0x0 ] = BusRelations;
        RelationsType[ 0x1 ] = RemovalRelations;
        RelationsType[ 0x2 ] = EjectionRelations;

        //
        // call SetDeviceState callback for IRP_MN_START
        //
        PnpCallbacks->DeviceStatePostCallback( OcDeviceObject->Pdo->KernelDeviceObject,
                                               OcDeviceObject->KernelDeviceObject,
                                               IRP_MN_START_DEVICE );

        //
        // call DeviceRelaions callback for
        // BusRelations
        // RemoveRelations
        // EjectionRelations
        //

        for( i=0x0; i < OC_STATIC_ARRAY_SIZE( RelationsType ); ++i ){

            KIRQL                   OldIrql;
            POC_RELATIONS_OBJECT    RelationsObject;
            PDEVICE_RELATIONS       DeviceRelations;
            ULONG                   SizeOfStructure;
            BOOLEAN                 Resending = FALSE;

            do{

                //
                // exchange the relations objects
                //
                OcRwAcquireLockForWrite( &OcDeviceObject->RwSpinLock, &OldIrql );
                {// start of the lock

                    RelationsObject = OcDeviceObject->DeviceRelations[ RelationsType[ i ] ];
                    if( NULL != RelationsObject )
                        OcObReferenceObject( RelationsObject );

                }// end of the lock
                OcRwReleaseWriteLock( &OcDeviceObject->RwSpinLock, OldIrql );

                if( NULL == RelationsObject ){

                    if( Resending ){

                        //
                        // report that the relations has been removed
                        // thus removing relations reported on the previous
                        // scan
                        //

                        PnpCallbacks->DeviceRelationsPostCallback( OcDeviceObject->Pdo->KernelDeviceObject,
                                                                   OcDeviceObject->KernelDeviceObject,
                                                                   RelationsType[ i ],
                                                                   NULL );
                    }

                    continue;
                }

                if( NULL == RelationsObject->Relations ||
                    0x0 ==RelationsObject->Relations->Count ){

                        OcObDereferenceObject( RelationsObject );
                        continue;
                }

                SizeOfStructure = FIELD_OFFSET( DEVICE_RELATIONS, Objects ) + 
                    sizeof( DeviceRelations->Objects[ 0x0 ] )*RelationsObject->Relations->Count;

                DeviceRelations = ExAllocatePoolWithTag( NonPagedPool,
                                                         SizeOfStructure,
                                                         '10cO');
                if( NULL != DeviceRelations ){

                    ULONG    m;

                    DeviceRelations->Count = RelationsObject->Relations->Count;

                    for( m = 0x0; m < RelationsObject->Relations->Count; ++m ){

                        DeviceRelations->Objects[ m ] = RelationsObject->Relations->Objects[ m ]->KernelDeviceObject;
                    }

                    PnpCallbacks->DeviceRelationsPostCallback( OcDeviceObject->Pdo->KernelDeviceObject,
                                                               OcDeviceObject->KernelDeviceObject,
                                                               RelationsType[ i ],
                                                               DeviceRelations );

                    ExFreePoolWithTag( DeviceRelations, '10cO' );
                }// if( NULL != DeviceRelations )

                OcObDereferenceObject( RelationsObject );

                //
                // if the relation objects changed while the relations were being sent to
                // the DlDriver then repeat the sending to maintain the right
                // sequence of events
                //
                Resending = TRUE;
            } while( RelationsObject != OcDeviceObject->DeviceRelations[ RelationsType[ i ] ] );

        }// for

    }

    return TRUE;
}

//-------------------------------------------------

BOOLEAN
OcCrFindTreeRoot(
    IN POC_DEVICE_OBJECT    PtrOcNodeDeviceObject,
    IN PVOID    Context// &RootDeviceObject
    )
{

    ASSERT( PASSIVE_LEVEL == KeGetCurrentIrql() );

    if( 0x1 == PtrOcNodeDeviceObject->Flags.DeviceIsNotTreeRoot ){

        //
        // the device is definitely not a tree root, skip it
        //
        ASSERT( NULL == *((POC_DEVICE_OBJECT*)Context) );

        //
        // continue the top down processing
        //
        return TRUE;
    }

    //
    // check the previous found root
    // if it is not NULL derefrence it
    //
    if( NULL != *((POC_DEVICE_OBJECT*)Context) ){

        POC_DEVICE_OBJECT    PrevRootDeviceObject;

        PrevRootDeviceObject = *((POC_DEVICE_OBJECT*)Context);

        //
        // remember that the device is not a tree root
        //
        PrevRootDeviceObject->Flags.DeviceIsNotTreeRoot = 0x1;

        //
        // derefernce it as it was referenced on a previous 
        // invokation of the routine 
        //
        OcObDereferenceObject( PrevRootDeviceObject );
    }

    //
    // reference the current object if this is not 
    // a processed root, the device object becomes 
    // the new root
    //
    if( 0x0 == PtrOcNodeDeviceObject->Flags.DeviceIsProcessedTreeRoot ){

        OcObReferenceObject( PtrOcNodeDeviceObject );
        *((POC_DEVICE_OBJECT*)Context) = PtrOcNodeDeviceObject;

    } else {

        //
        // we already processed this root
        //
        *((POC_DEVICE_OBJECT*)Context) = NULL;
    }

    return TRUE;
}

//-------------------------------------------------

VOID
NTAPI
OcCrEmulateStackBuildingForDevice(
    IN PVOID    Context,//POC_DEVICE_OBJECT
    IN PVOID    ContextEx//POC_PNP_CALLBACKS
    )
    /*
    the function finds a root device object for
    a PnP tree with the device object and
    emulates this PnP tree building by the 
    PnP manager
    */
{
    POC_DEVICE_OBJECT    DeviceObject;
    POC_DEVICE_OBJECT    RootDeviceObject = NULL;
    POC_PNP_CALLBACKS    PnpCallbacks;

    ASSERT( PASSIVE_LEVEL == KeGetCurrentIrql() );

    DeviceObject = (POC_DEVICE_OBJECT)Context;
    PnpCallbacks = (POC_PNP_CALLBACKS)ContextEx;

    //
    // get the lowest device in the tree
    // this will be a tree root, RootDeviceObject
    // will contain a REFERENCED root device
    //
    OcCrTraverseTopDown( DeviceObject,
                         OcCrFindTreeRoot,
                         (PVOID)&RootDeviceObject );

    if( NULL == RootDeviceObject ){
        //
        // all trees have been processed
        //
        return;
    }

    //
    // the root has been found,
    // start the bottom up processing
    // thus emulating the sequence in which
    // devices waere discovered by the PnP
    // manager
    //
    OcCrTraverseFromDownToTop( RootDeviceObject,
                               OcCrEmulatePnpStackBuildingForDevice,
                               PnpCallbacks );

    //
    // remember that this root has been processed to 
    // avoid infinite cycle, to process all trees in the
    // forest this flag must be toggled
    //
    RootDeviceObject->Flags.DeviceIsProcessedTreeRoot = 0x1;

    //
    // derefrence the root referenced in OcCrFindTreeRoot
    //
    OcObDereferenceObject( RootDeviceObject );
}

//-------------------------------------------------

VOID
OcCrEmulatePnpManagerForMachine(
    __in POC_PNP_CALLBACKS      PnpCallbacks
    )
{

    ASSERT( PASSIVE_LEVEL == KeGetCurrentIrql() );

    //
    // start from any device in the hash to find
    // a root of the first tree in the forest then
    // process the tree from root to children
    // then the next unvisited root and repeat
    //
    OcHsTraverseAllEntriesInHash( Global.PtrDeviceHashObject,
                                  OcCrEmulateStackBuildingForDevice,
                                  PnpCallbacks );

}

//-------------------------------------------------

