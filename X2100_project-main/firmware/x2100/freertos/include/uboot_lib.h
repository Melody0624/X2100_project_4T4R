#ifndef _UBOOT_LIB_H
#define _UBOOT_LIB_H
#include <spl_rtos_argument.h>

#define IH_NMLEN        32

typedef struct image_header {
    uint32_t    ih_magic;               /* Image Header Magic Number */
    uint32_t    ih_hcrc;                /* Image Header CRC Checksum */
    uint32_t    ih_time;                /* Image Creation Timestamp */
    uint32_t    ih_size;                /* Image Data Size */
    uint32_t    ih_load;                /* Data     Load  Address */
    uint32_t    ih_ep;                  /* Entry Point Address */
    uint32_t    ih_dcrc;                /* Image Data CRC Checksum */
    uint8_t     ih_os;                  /* Operating System */
    uint8_t     ih_arch;                /* CPU architecture */
    uint8_t     ih_type;                /* Image Type */
    uint8_t     ih_comp;                /* Compression Type */
    uint8_t     ih_name[IH_NMLEN];      /* Image Name */
} image_header_t;

void jump_to_image_linux(struct rtos_boot_os_args *args);

#endif