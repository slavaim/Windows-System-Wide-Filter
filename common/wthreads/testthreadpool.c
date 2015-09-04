/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
30.11.2006 
 Start
*/

#if DBG

#include <octhreadpool.h>

ULONG_PTR  g_Parameters[ 11 ] = { 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA };
BOOLEAN    g_FunctionCalled[ 11 ] = { 0x0 };

//-------------------------------------------------------------

#define OC_TPL_CHECK_PARAMETER( ParamNumber )  \
    do{                                        \
        if( Param##ParamNumber != g_Parameters[ ParamNumber ] ){ \
        DbgPrint( " ThreadpoolTest: Param%i is 0x%ph but must be 0x%ph \n", ParamNumber, Param##ParamNumber, g_Parameters[ ParamNumber ] ); \
            DbgBreakPoint(); \
        }\
    }while( FALSE ); \

//-------------------------------------------------------------

VOID 
FASTCALL 
Sleep(
    IN ULONG ulMilSecs
    )
{
    KEVENT            kEvent;
    LARGE_INTEGER    qTimeout;

    qTimeout.QuadPart = 10000L;
    qTimeout.QuadPart *= ulMilSecs;
    qTimeout.QuadPart = -(qTimeout.QuadPart);

    ASSERT( KeGetCurrentIrql() <= APC_LEVEL );

    KeInitializeEvent(&kEvent,SynchronizationEvent,FALSE);

    KeWaitForSingleObject((PVOID)&kEvent,Executive,KernelMode,FALSE,&qTimeout);
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam0()
{

    g_FunctionCalled[ 0x0 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam0  Thread = 0x%ph \n", PsGetCurrentThread() );

    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam1(
    IN ULONG_PTR    Param1
    )
{

    g_FunctionCalled[ 0x1 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam1  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam2(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2
    )
{

    g_FunctionCalled[ 0x2 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam2  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam3(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2,
    IN ULONG_PTR    Param3
    )
{

    g_FunctionCalled[ 0x3 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam3  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )
    OC_TPL_CHECK_PARAMETER( 3 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam4(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2,
    IN ULONG_PTR    Param3,
    IN ULONG_PTR    Param4
    )
{

    g_FunctionCalled[ 0x4 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam4  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )
    OC_TPL_CHECK_PARAMETER( 3 )
    OC_TPL_CHECK_PARAMETER( 4 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam5(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2,
    IN ULONG_PTR    Param3,
    IN ULONG_PTR    Param4,
    IN ULONG_PTR    Param5
    )
{

    g_FunctionCalled[ 0x5 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam5  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )
    OC_TPL_CHECK_PARAMETER( 3 )
    OC_TPL_CHECK_PARAMETER( 4 )
    OC_TPL_CHECK_PARAMETER( 5 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam6(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2,
    IN ULONG_PTR    Param3,
    IN ULONG_PTR    Param4,
    IN ULONG_PTR    Param5,
    IN ULONG_PTR    Param6
    )
{

    g_FunctionCalled[ 0x6 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam6  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )
    OC_TPL_CHECK_PARAMETER( 3 )
    OC_TPL_CHECK_PARAMETER( 4 )
    OC_TPL_CHECK_PARAMETER( 5 )
    OC_TPL_CHECK_PARAMETER( 6 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam7(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2,
    IN ULONG_PTR    Param3,
    IN ULONG_PTR    Param4,
    IN ULONG_PTR    Param5,
    IN ULONG_PTR    Param6,
    IN ULONG_PTR    Param7
    )
{

    g_FunctionCalled[ 0x7 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam7  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )
    OC_TPL_CHECK_PARAMETER( 3 )
    OC_TPL_CHECK_PARAMETER( 4 )
    OC_TPL_CHECK_PARAMETER( 5 )
    OC_TPL_CHECK_PARAMETER( 6 )
    OC_TPL_CHECK_PARAMETER( 7 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam8(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2,
    IN ULONG_PTR    Param3,
    IN ULONG_PTR    Param4,
    IN ULONG_PTR    Param5,
    IN ULONG_PTR    Param6,
    IN ULONG_PTR    Param7,
    IN ULONG_PTR    Param8
    )
{

    g_FunctionCalled[ 0x8 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam8  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )
    OC_TPL_CHECK_PARAMETER( 3 )
    OC_TPL_CHECK_PARAMETER( 4 )
    OC_TPL_CHECK_PARAMETER( 5 )
    OC_TPL_CHECK_PARAMETER( 6 )
    OC_TPL_CHECK_PARAMETER( 7 )
    OC_TPL_CHECK_PARAMETER( 8 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam9(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2,
    IN ULONG_PTR    Param3,
    IN ULONG_PTR    Param4,
    IN ULONG_PTR    Param5,
    IN ULONG_PTR    Param6,
    IN ULONG_PTR    Param7,
    IN ULONG_PTR    Param8,
    IN ULONG_PTR    Param9
    )
{

    g_FunctionCalled[ 0x9 ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam9  Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )
    OC_TPL_CHECK_PARAMETER( 3 )
    OC_TPL_CHECK_PARAMETER( 4 )
    OC_TPL_CHECK_PARAMETER( 5 )
    OC_TPL_CHECK_PARAMETER( 6 )
    OC_TPL_CHECK_PARAMETER( 7 )
    OC_TPL_CHECK_PARAMETER( 8 )
    OC_TPL_CHECK_PARAMETER( 9 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

NTSTATUS
OcTplTestFuncParam10(
    IN ULONG_PTR    Param1,
    IN ULONG_PTR    Param2,
    IN ULONG_PTR    Param3,
    IN ULONG_PTR    Param4,
    IN ULONG_PTR    Param5,
    IN ULONG_PTR    Param6,
    IN ULONG_PTR    Param7,
    IN ULONG_PTR    Param8,
    IN ULONG_PTR    Param9,
    IN ULONG_PTR    Param10
    )
{

    g_FunctionCalled[ 0xA ] = TRUE;
    DbgPrint(" ThreadpoolTest: OcTplTestFuncParam10 Thread = 0x%ph \n", PsGetCurrentThread() );
    OC_TPL_CHECK_PARAMETER( 1 )
    OC_TPL_CHECK_PARAMETER( 2 )
    OC_TPL_CHECK_PARAMETER( 3 )
    OC_TPL_CHECK_PARAMETER( 4 )
    OC_TPL_CHECK_PARAMETER( 5 )
    OC_TPL_CHECK_PARAMETER( 6 )
    OC_TPL_CHECK_PARAMETER( 7 )
    OC_TPL_CHECK_PARAMETER( 8 )
    OC_TPL_CHECK_PARAMETER( 9 )
    OC_TPL_CHECK_PARAMETER( 10 )

    Sleep(300);
    return STATUS_SUCCESS;
}

//-------------------------------------------------------------

VOID
OcTplTest(
    IN POC_THREAD_POOL_OBJECT    PtrThreadPoolObject
    )
{
    NTSTATUS    RC;
    POC_WORK_ITEM_LIST_OBJECT    PtrWorkItem;

    PtrWorkItem = OcTplReferenceSharedWorkItemList( PtrThreadPoolObject );
    ASSERT( NULL != PtrWorkItem );
    if( NULL == PtrWorkItem )
        return;

    RC = OcWthPostWorkItemParam0( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam0 );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam1( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam1,
                                  g_Parameters[ 0x1 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam2( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam2,
                                  g_Parameters[ 0x1 ],
                                  g_Parameters[ 0x2 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam3( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam3,
                                  g_Parameters[ 0x1 ],
                                  g_Parameters[ 0x2 ],
                                  g_Parameters[ 0x3 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam4( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam4,
                                  g_Parameters[ 0x1 ],
                                  g_Parameters[ 0x2 ],
                                  g_Parameters[ 0x3 ],
                                  g_Parameters[ 0x4 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam5( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam5,
                                  g_Parameters[ 0x1 ],
                                  g_Parameters[ 0x2 ],
                                  g_Parameters[ 0x3 ],
                                  g_Parameters[ 0x4 ],
                                  g_Parameters[ 0x5 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam6( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam6,
                                  g_Parameters[ 0x1 ],
                                  g_Parameters[ 0x2 ],
                                  g_Parameters[ 0x3 ],
                                  g_Parameters[ 0x4 ],
                                  g_Parameters[ 0x5 ],
                                  g_Parameters[ 0x6 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam7( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam7,
                                  g_Parameters[ 0x1 ],
                                  g_Parameters[ 0x2 ],
                                  g_Parameters[ 0x3 ],
                                  g_Parameters[ 0x4 ],
                                  g_Parameters[ 0x5 ],
                                  g_Parameters[ 0x6 ],
                                  g_Parameters[ 0x7 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam8( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam8,
                                  g_Parameters[ 0x1 ],
                                  g_Parameters[ 0x2 ],
                                  g_Parameters[ 0x3 ],
                                  g_Parameters[ 0x4 ],
                                  g_Parameters[ 0x5 ],
                                  g_Parameters[ 0x6 ],
                                  g_Parameters[ 0x7 ],
                                  g_Parameters[ 0x8 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam9( PtrWorkItem,
                                  FALSE,
                                  OcTplTestFuncParam9,
                                  g_Parameters[ 0x1 ],
                                  g_Parameters[ 0x2 ],
                                  g_Parameters[ 0x3 ],
                                  g_Parameters[ 0x4 ],
                                  g_Parameters[ 0x5 ],
                                  g_Parameters[ 0x6 ],
                                  g_Parameters[ 0x7 ],
                                  g_Parameters[ 0x8 ],
                                  g_Parameters[ 0x9 ] );
    ASSERT( NT_SUCCESS( RC ) );

    RC = OcWthPostWorkItemParam10( PtrWorkItem,
                                   FALSE,
                                   OcTplTestFuncParam10,
                                   g_Parameters[ 0x1 ],
                                   g_Parameters[ 0x2 ],
                                   g_Parameters[ 0x3 ],
                                   g_Parameters[ 0x4 ],
                                   g_Parameters[ 0x5 ],
                                   g_Parameters[ 0x6 ],
                                   g_Parameters[ 0x7 ],
                                   g_Parameters[ 0x8 ],
                                   g_Parameters[ 0x9 ],
                                   g_Parameters[ 0xA ] );
    ASSERT( NT_SUCCESS( RC ) );

    OcObDereferenceObject( PtrWorkItem );

}

//-------------------------------------------------------------

VOID
OcTestThreadPoolManager()
    /*
    the caller must initialize the worker thread manager!
    */
{
    NTSTATUS   RC;
    POC_THREAD_POOL_OBJECT    PtrThreadsPoolObject;

    //
    // create the worker threads pool with shared work item list
    //
    RC = OcTplCreateThreadPool( 0x3,
                                FALSE,//shared work item list
                                &PtrThreadsPoolObject );
    if( !NT_SUCCESS( RC ) ){

        ASSERT( " Test: OcTplCreateThreadPool with shared list failed! Investigate immediatelly!" );
        goto __exit;
    }

    ASSERT( 0x1 == OcObGetObjectReferenceCount( PtrThreadsPoolObject ) );

    OcTplTest( PtrThreadsPoolObject );
    OcTplTest( PtrThreadsPoolObject );

    ASSERT( 0x1 == OcObGetObjectReferenceCount( PtrThreadsPoolObject ) );

    //
    // stop the pool's threads and remove all objects
    //
    OcObDereferenceObject( PtrThreadsPoolObject );

__exit: 
    ;
}

//-------------------------------------------------------------

#endif//DBG