#ifndef _TYPES_H_
#define _TYPES_H_

#ifndef u8
typedef unsigned char u8;
typedef char s8;
#endif

#ifndef u16
typedef unsigned short u16;
typedef short s16;
#endif

#ifndef u32
typedef unsigned int u32;
typedef int s32;
#endif

#ifndef u64
typedef unsigned long long u64;
typedef long long s64;
#endif

#ifndef __SIZE_TYPE__
typedef unsigned int size_t;
#define _SIZE_T_DEFINED_
#endif

#ifndef uchar
typedef unsigned char uchar;
#endif

#ifndef ushort
typedef unsigned short ushort;
#endif

#ifndef uint
typedef unsigned int uint;
#endif

#ifndef ulong
typedef unsigned long ulong;
#endif

#ifndef NULL
#define NULL 0
#endif

#endif /* _TYPES_H_ */