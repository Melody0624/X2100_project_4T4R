#ifndef _GADGET_MASS_STORAGE_H_
#define _GADGET_MASS_STORAGE_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_MASS_STORAGE

typedef int (*fsg_read_callback_t)(void *buf, int count, u64 pos);
typedef int (*fsg_write_callback_t)(const void *buf, int count, u64 pos);
typedef int (*fsg_sync_callback_t)(void);

struct fsg_lun_config {
	fsg_read_callback_t read_callback;
	fsg_write_callback_t write_callback;
	fsg_sync_callback_t sync_callback;

	char ro;			/* Read Only */
	char removable;		/* Removable Device */
	char cdrom;

	unsigned long long num_sectors; /*  Number of Sectors */
	unsigned int block_size; 		/* block size */

	const char *vendor_name;		/*  8 characters or less */
	const char *product_name;		/* 16 characters or less */
	unsigned int product_version;
};

struct fsg_config {
	const char *serialnumber; /*  USB drive serial number  */
	unsigned nluns; /* Hard disk part number */
	struct fsg_lun_config *luns;
	connect_callback_t connect_cb;
};

/*
 mass storage device init 
    param:
        <id>  Manufacturer's device ID 
        <config> mass storage config param
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_mass_storage_init(const struct gadget_id *id, struct fsg_config *config);

/* mass storage device cleanup */
extern void gadget_mass_storage_cleanup(void);

#endif

#endif /* _GADGET_MASS_STORAGE_H_ */
