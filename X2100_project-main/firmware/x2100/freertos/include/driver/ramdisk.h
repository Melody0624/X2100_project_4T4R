#ifndef _RAMDISK_H_
#define _RAMDISK_H_
#include <stdio.h>

struct ramdisk_block {
    char name[32];
    void *start_addr;
    void *end_addr;
    uint32_t size;
};

int ramdisk_devices_init(void);

#endif /* _RAMDISK_H_ */
