/*
Author: Slava Imameev   
(c) 2006 Slava Imameev, All Rights Reserved

Revision history:
03.12.2006 ( December )
 Start
*/
#include <ochash.h>

#if DBG

static ULONG_PTR    g_Array[ 500 ];

VOID
NTAPI
OcTestHashManager()
    /*
    the caller must initialize the hash manager!
    */
{
    NTSTATUS    RC;
    ULONG       i;
    POC_HASH_OBJECT    PtrHashObj;

    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( g_Array ); ++i ){

        g_Array[ i ] = (ULONG_PTR)&g_Array[ i ] + i;
    }

    RC = OcHsCreateHash( 273,
                         NULL,
                         &PtrHashObj );
    ASSERT( NT_SUCCESS( RC ) );
    if( !NT_SUCCESS( RC ) )
        goto __exit;

    ASSERT( 0x1 == OcObGetObjectReferenceCount( PtrHashObj ) );

    //
    // populate the hash
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( g_Array ); ++i ){

        RC = OcHsInsertContextInHash( PtrHashObj,
                                      g_Array[ i ],
                                      &g_Array[ i ],
                                      NULL );
        ASSERT( NT_SUCCESS( RC ) );
    }

    //
    // check the hash
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( g_Array ); ++i ){

        PVOID    Context;

        Context = OcHsFindContextByKeyValue( PtrHashObj,
                                             g_Array[ i ],
                                             NULL );
        ASSERT( NULL != Context );
        if( Context != &g_Array[ i ] ){

            ASSERT( !"Test: OcHsFindContextByKeyValue returns erroneous value" );
        }
    }

    //
    // remove the half of the hash entries
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( g_Array ); ++i ){

        PVOID    Context;

        if( i%2 != 0x0 )
            continue;

        OcHsRemoveContextByKeyValue( PtrHashObj,
                                     g_Array[ i ],
                                     NULL );

        Context = OcHsFindContextByKeyValue( PtrHashObj,
                                             g_Array[ i ],
                                             NULL );
        ASSERT( NULL == Context );
        if( NULL != Context ){

            ASSERT( !"Test: OcHsFindContextByKeyValue finds removed context" );
        }
    }

    //
    // remove all entries from the hash
    //
    OcHsPurgeAllEntriesFromHash( PtrHashObj, NULL );

    //
    // check that the hash is empty
    //
    for( i = 0x0; i < OC_STATIC_ARRAY_SIZE( g_Array ); ++i ){

        PVOID    Context;

        Context = OcHsFindContextByKeyValue( PtrHashObj,
                                             g_Array[ i ],
                                             NULL );
        ASSERT( NULL == Context );
        if( NULL != Context ){

            ASSERT( !"Test: OcHsFindContextByKeyValue finds removed context" );
        }
    }

    ASSERT( 0x1 == OcObGetObjectReferenceCount( PtrHashObj ) );

    //
    // remove the hash and its object
    //
    OcObDereferenceObject( PtrHashObj );

__exit:
    ;

}
#endif//DBG