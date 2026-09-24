#ifndef __GUNZIP_H
#define __GUNZIP_H

#include <zlib/zlib.h>

#define	ZALLOC_ALIGNMENT	16

int gunzip(void *dst, int dstlen, unsigned char *src, unsigned long *lenp,  alloc_func alloc_p, free_func free_p);

#endif