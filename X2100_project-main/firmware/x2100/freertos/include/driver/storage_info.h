#ifndef _STORAGE_INFO_H_
#define _STORAGE_INFO_H_
#include <stdio.h>

struct  storage_info{
    const char    *name;
    uint32_t id;
    uint32_t pagesize;
    uint64_t chipsize;
    uint32_t erasesize;
    uint32_t addrsize;
};

#endif /*_STORAGE_INFO_H_*/