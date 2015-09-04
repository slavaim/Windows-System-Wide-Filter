/*
 **********************************************************************
 ** md5.h -- Header file for implementation of MD5                   **
 ** RSA Data Security, Slava Imameev MD5 Message Digest Algorithm             **
 **********************************************************************
 */
 
#ifndef _VFS_HASH_H
#define _VFS_HASH_H

/* typedef a 32 bit type */
typedef unsigned long int UINT4;

/* Data structure for MD5 (Message Digest) computation */
typedef struct {
  UINT4 i[2];                   /* number of _bits_ handled mod 2^64 */
  UINT4 buf[4];                 /* scratch buffer */
  unsigned char in1[64];         /* input buffer */
  unsigned char digest[16];     /* actual digest after MD5Final call */
} MD5_CTX;

void MD5Init ( MD5_CTX *mdContext );
void MD5Update ( MD5_CTX *mdContext, unsigned char *inBuf, unsigned int inLen );
void MD5Final ( MD5_CTX *mdContext );

#define MAGIC 0x1234DEADBEEF4321

unsigned __int64 AsciiHash( unsigned __int64 Magic, void* String, unsigned int StringLength );

#endif