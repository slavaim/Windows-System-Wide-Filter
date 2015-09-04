MAJORCOMP=oc
MINORCOMP=wthread

TARGETPATH=..\..\lib
TARGETNAME=wthread
TARGETTYPE=DRIVER_LIBRARY

MSC_WARNING_LEVEL= /W3 /WX

!if $(AMD64)
C_DEFINES=$(C_DEFINES) /D_AMD64_
!else
!endif

INCLUDES=$(INCLUDES); ..\..\include

SOURCES= ocwthread.c \
         octhreadpool.c \
         testthreadpool.c \

!if $(FREEBUILD)
C_DEFINES=$(C_DEFINES)
!else
C_DEFINES=$(C_DEFINES) /DDBG /DUSE_STD_ALLOCATOR=1
!endif

