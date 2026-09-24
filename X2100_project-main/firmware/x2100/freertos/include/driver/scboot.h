#ifndef _SCBOOT_H
#define _SCBOOT_H

#include <soc/scboot.h>

int secure_scboot(void *input, void *output);

int secure_write_key(void *keybin);

int secure_enable_scboot(void);

#endif